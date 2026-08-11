#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace framewright {

/// How read() converts HDR (PQ/HLG) sources to 8-bit BGR.
enum class HdrMode {
    /// Matrix-only conversion (historical behavior). For PQ/HLG sources the
    /// decoded pixel values remain HDR-encoded and will look washed out when
    /// treated as sRGB/BT.709 downstream. A warning is logged at open().
    Passthrough,
    /// Linearize PQ/HLG, tone map to SDR (BT.2390-style extended Reinhard,
    /// 1000-nit nominal peak), convert BT.2020 primaries to BT.709 and encode
    /// with BT.709 gamma. SDR sources are unaffected by this mode.
    ToneMapSDR,
};

/// Options for VideoReader::open().
struct VideoReaderOptions {
    bool force_bt709 = false;      ///< Force BT.709 color matrix regardless of metadata.
    bool force_full_range = false; ///< Treat input as full range (0-255).
    HdrMode hdr_mode = HdrMode::Passthrough; ///< How to handle PQ/HLG sources in read().
};

/// Drop-in replacement for cv::VideoCapture with correct color space handling.
///
/// @note Not thread-safe. An instance holds mutable decoder state and mutates
/// its position counters on every read(), so concurrent calls on one instance
/// race. Separate instances in separate threads are fine -- they share no
/// state. The one exception is the library-wide log level (see LogLevel.h),
/// which is process-global.
///
/// OpenCV's VideoCapture silently picks a YUV-to-RGB color matrix and gives
/// you no way to override it. This class uses FFmpeg directly and lets you
/// force BT.709, BT.601, or full-range conversion so the pixel values you
/// read are actually correct.
///
/// HDR sources (BT.2020 + PQ/HLG): the correct color matrix is always used,
/// and two dedicated paths exist beyond that. read16() returns the untouched
/// (still PQ/HLG-encoded) code values at 16-bit precision for callers doing
/// their own mapping, and opening with HdrMode::ToneMapSDR makes read()
/// return display-ready SDR BT.709 frames. Only codec-level color metadata is
/// honored; frame-level dynamic metadata (HDR10+, Dolby Vision RPUs) is
/// ignored.
class VideoReader {
  public:
    VideoReader();
    ~VideoReader();

    // Non-copyable
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    // Movable
    VideoReader(VideoReader&& other) noexcept;
    VideoReader& operator=(VideoReader&& other) noexcept;

    /// Open a video file.
    /// @param filename     Path to the video file.
    /// @param force_bt709  Force BT.709 color matrix regardless of metadata.
    /// @param force_full_range  Treat input as full range (0-255).
    bool open(const std::string& filename, bool force_bt709 = false,
              bool force_full_range = false);

    /// Open a video file with full options, including HDR handling.
    bool open(const std::string& filename, const VideoReaderOptions& options);

    /// Read the next frame as a BGR cv::Mat (same convention as OpenCV).
    /// The frame is a deep copy and stays valid for as long as you hold it.
    bool read(cv::Mat& frame);

    /// Read the next frame without copying it.
    ///
    /// @warning The returned cv::Mat is a *view* onto an internal buffer that
    /// the reader overwrites in place. It is invalidated by the next call to
    /// read(), readRef(), read16(), seek() or close(), and by destroying the
    /// reader. Clone it yourself if you need to keep it. Use read() unless
    /// the copy is measurably hurting you -- it avoids this hazard entirely.
    bool readRef(cv::Mat& frame);

    /// Read the next frame as 16-bit BGR (CV_16UC3), preserving the source's
    /// code values at full precision.
    ///
    /// Only the color matrix and range are applied -- the transfer function is
    /// NOT: for PQ/HLG sources the returned values are still PQ/HLG-encoded
    /// (10-bit code values scaled to 16-bit). Combined with
    /// getColorTransfer()/getColorPrimaries() this is the lossless input for
    /// doing your own tone mapping, and it round-trips with
    /// VideoWriter::write(CV_16UC3) in HDR mode. The frame is a deep copy.
    bool read16(cv::Mat& frame);

    /// Seek to a specific frame number (forward and backward, best effort).
    bool seek(int64_t frame_number);

    void close();

    // Properties
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    /// Get the frame rate. Returns 0.0 if the container does not report one.
    double getFPS() const { return fps_; }

    /// Get the total frame count. Returns -1 if the container does not
    /// report a frame count (common with MKV and some MP4 files).
    int64_t getFrameCount() const { return frame_count_; }

    int64_t getCurrentFrameNumber() const { return current_frame_; }
    double getCurrentTimestamp() const { return current_timestamp_; }

    /// Get the file's pixel format (e.g. AV_PIX_FMT_YUV420P).
    AVPixelFormat getPixelFormat() const;

    /// Get the file's codec ID (e.g. AV_CODEC_ID_H264).
    AVCodecID getCodecID() const;

    // Color space metadata from the file
    AVColorSpace getColorSpace() const;
    AVColorRange getColorRange() const;
    AVColorPrimaries getColorPrimaries() const;
    AVColorTransferCharacteristic getColorTransfer() const;

    bool isForcingBT709() const { return force_bt709_; }
    bool isForcingFullRange() const { return force_full_range_; }

    /// The HDR mode this reader was opened with.
    HdrMode getHdrMode() const { return hdr_mode_; }

    /// True when the source is PQ/HLG and read() is tone mapping it to SDR.
    bool isToneMappingActive() const { return tone_map_active_; }

  private:
    void cleanup();
    bool setupScaler();
    /// Apply matrix/range configuration shared by the 8- and 16-bit scalers.
    void configureScalerColorspace(SwsContext* ctx);
    /// Lazily create the 16-bit (BGR48) scaler and frame buffer.
    bool ensure16BitScaler();
    /// Decodes the next frame into frame_ and advances the position counters.
    bool decodeNextFrame();
    /// Convert the decoded frame_ into frameBGR_ (8-bit, tone-map aware).
    bool convertToBGR8();
    /// Convert the decoded frame_ into frameBGR16_ (16-bit code values).
    bool convertToBGR16();
    /// Tone map frameBGR16_ (linear-light via LUT) into frameBGR_.
    void toneMapFrame();
    /// Build the 16-bit-code -> linear-light LUT for the source transfer.
    void buildToneMapLut();

    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* frameBGR_ = nullptr;
    AVFrame* frameBGR16_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    SwsContext* swsCtx16_ = nullptr;

    int videoStreamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 0.0;
    int64_t frame_count_ = 0;
    int64_t current_frame_ = 0;
    double current_timestamp_ = 0.0;

    bool force_bt709_ = false;
    bool force_full_range_ = false;
    HdrMode hdr_mode_ = HdrMode::Passthrough;
    bool tone_map_active_ = false;

    /// 16-bit code value -> linear light in SDR-reference-white units
    /// (1.0 == 100 nits). Populated only when tone mapping is active.
    std::vector<float> toneMapLut_;
};

} // namespace framewright
