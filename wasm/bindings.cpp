// Emscripten (WASM) bindings for framewright, targeting Node.js and browsers.
//
// The module works on files in the Emscripten virtual filesystem (MEMFS by
// default): write your video bytes there first (FS.writeFile), then open that
// path. Frames cross the JS boundary as copies (Uint8Array / Uint16Array /
// Float32Array), so they stay valid after the next read.
//
// Codec availability in the WASM build is narrower than native: H.264, HEVC
// and FFV1 decode; only FFV1 encodes (x264/x265 are not compiled in), so
// lossless FFV1 writing works and H.264/HEVC writing does not.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <framewright/LogLevel.h>
#include <framewright/VideoReader.h>
#include <framewright/VideoWriter.h>

using emscripten::val;

namespace {

// Copy a cv::Mat into a fresh JS typed array. The TypedArray constructor
// copies when given another typed array, so the result owns its bytes and
// outlives the Mat.
val matToTypedArray(const cv::Mat& m) {
    const size_t count = m.total() * m.channels();
    switch (m.depth()) {
    case CV_8U:
        return val::global("Uint8Array")
            .new_(emscripten::typed_memory_view(count, reinterpret_cast<const uint8_t*>(m.data)));
    case CV_16U:
        return val::global("Uint16Array")
            .new_(emscripten::typed_memory_view(count, reinterpret_cast<const uint16_t*>(m.data)));
    case CV_32F:
        return val::global("Float32Array")
            .new_(emscripten::typed_memory_view(count, reinterpret_cast<const float*>(m.data)));
    default:
        return val::null();
    }
}

class JsVideoReader {
  public:
    bool open(const std::string& path, bool forceBt709, bool forceFullRange, bool toneMapHdr) {
        framewright::VideoReaderOptions opts;
        opts.force_bt709 = forceBt709;
        opts.force_full_range = forceFullRange;
        opts.hdr_mode = toneMapHdr ? framewright::HdrMode::ToneMapSDR
                                   : framewright::HdrMode::Passthrough;
        return reader_.open(path, opts);
    }

    // (H*W*3) BGR Uint8Array, or null at EOF.
    val read() {
        cv::Mat f;
        if (!reader_.read(f)) {
            return val::null();
        }
        return matToTypedArray(f);
    }

    // (H*W*4) RGBA Uint8ClampedArray, directly usable for an ImageData of
    // the reader's width/height, or null at EOF. Same conversion as read()
    // (so tone mapping applies when enabled), expanded from BGR with
    // alpha = 255.
    val readRGBA() {
        cv::Mat f;
        if (!reader_.read(f)) {
            return val::null();
        }
        const size_t pixels = f.total();
        std::vector<uint8_t> rgba(pixels * 4);
        const uint8_t* src = f.data;
        for (size_t i = 0; i < pixels; i++) {
            rgba[i * 4 + 0] = src[i * 3 + 2];
            rgba[i * 4 + 1] = src[i * 3 + 1];
            rgba[i * 4 + 2] = src[i * 3 + 0];
            rgba[i * 4 + 3] = 255;
        }
        return val::global("Uint8ClampedArray")
            .new_(emscripten::typed_memory_view(rgba.size(), rgba.data()));
    }

    // (H*W*3) BGR Uint16Array with source code values preserved, or null.
    val read16() {
        cv::Mat f;
        if (!reader_.read16(f)) {
            return val::null();
        }
        return matToTypedArray(f);
    }

    // (H*W*3) BGR Float32Array, linear light, 1.0 == SDR white, or null.
    val readLinear() {
        cv::Mat f;
        if (!reader_.readLinear(f)) {
            return val::null();
        }
        return matToTypedArray(f);
    }

    bool seek(double frameNumber) { return reader_.seek(static_cast<int64_t>(frameNumber)); }
    void close() { reader_.close(); }

    int width() const { return reader_.getWidth(); }
    int height() const { return reader_.getHeight(); }
    double fps() const { return reader_.getFPS(); }
    double frameCount() const { return static_cast<double>(reader_.getFrameCount()); }
    double currentFrame() const { return static_cast<double>(reader_.getCurrentFrameNumber()); }
    double timestamp() const { return reader_.getCurrentTimestamp(); }
    bool toneMappingActive() const { return reader_.isToneMappingActive(); }

    std::string colorSpace() const {
        const char* n = av_color_space_name(reader_.getColorSpace());
        return n ? n : "unknown";
    }
    std::string colorRange() const {
        const char* n = av_color_range_name(reader_.getColorRange());
        return n ? n : "unknown";
    }
    std::string colorPrimaries() const {
        const char* n = av_color_primaries_name(reader_.getColorPrimaries());
        return n ? n : "unknown";
    }
    std::string colorTransfer() const {
        const char* n = av_color_transfer_name(reader_.getColorTransfer());
        return n ? n : "unknown";
    }
    std::string pixelFormat() const {
        const char* n = av_get_pix_fmt_name(reader_.getPixelFormat());
        return n ? n : "none";
    }

  private:
    framewright::VideoReader reader_;
};

class JsVideoWriter {
  public:
    // FFV1 in Matroska/MP4 is the only encoder compiled into the WASM build.
    bool openFfv1(const std::string& path, int width, int height, int fpsNum, int fpsDen) {
        width_ = width;
        height_ = height;
        framewright::VideoWriterOptions opts;
        return writer_.open(path, AV_CODEC_ID_FFV1, width, height, {fpsNum, fpsDen}, opts);
    }

    // data: (H*W*3) BGR Uint8Array.
    bool write(val data) {
        const size_t expected = static_cast<size_t>(width_) * height_ * 3;
        if (data["length"].as<size_t>() != expected) {
            return false;
        }
        cv::Mat frame(height_, width_, CV_8UC3);
        val view = val(emscripten::typed_memory_view(expected, frame.data));
        view.call<void>("set", data);
        return writer_.write(frame);
    }

    void release() { writer_.release(); }

  private:
    framewright::VideoWriter writer_;
    int width_ = 0;
    int height_ = 0;
};

void setLogLevelInt(int level) {
    framewright::setLogLevel(static_cast<framewright::LogLevel>(level));
}

} // namespace

EMSCRIPTEN_BINDINGS(framewright) {
    emscripten::class_<JsVideoReader>("VideoReader")
        .constructor<>()
        .function("open", &JsVideoReader::open)
        .function("read", &JsVideoReader::read)
        .function("readRGBA", &JsVideoReader::readRGBA)
        .function("read16", &JsVideoReader::read16)
        .function("readLinear", &JsVideoReader::readLinear)
        .function("seek", &JsVideoReader::seek)
        .function("close", &JsVideoReader::close)
        .function("width", &JsVideoReader::width)
        .function("height", &JsVideoReader::height)
        .function("fps", &JsVideoReader::fps)
        .function("frameCount", &JsVideoReader::frameCount)
        .function("currentFrame", &JsVideoReader::currentFrame)
        .function("timestamp", &JsVideoReader::timestamp)
        .function("toneMappingActive", &JsVideoReader::toneMappingActive)
        .function("colorSpace", &JsVideoReader::colorSpace)
        .function("colorRange", &JsVideoReader::colorRange)
        .function("colorPrimaries", &JsVideoReader::colorPrimaries)
        .function("colorTransfer", &JsVideoReader::colorTransfer)
        .function("pixelFormat", &JsVideoReader::pixelFormat);

    emscripten::class_<JsVideoWriter>("VideoWriter")
        .constructor<>()
        .function("openFfv1", &JsVideoWriter::openFfv1)
        .function("write", &JsVideoWriter::write)
        .function("release", &JsVideoWriter::release);

    // 0 = Quiet, 1 = Error, 2 = Warning, 3 = Info
    emscripten::function("setLogLevel", &setLogLevelInt);
}
