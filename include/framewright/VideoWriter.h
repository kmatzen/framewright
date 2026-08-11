#pragma once

#include <opencv2/opencv.hpp>
#include <string>

#include "framewright/HDR10Metadata.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libswscale/swscale.h>
}

namespace framewright {

/// Options for VideoWriter::open().
struct VideoWriterOptions {
    int bitrate = 25000000;                    ///< Target bitrate in bits/sec.
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P; ///< Output pixel format.
    bool is_10bit = false;   ///< Enable HDR10 mode (BT.2020 + PQ + mastering display metadata).
    bool full_range = false; ///< Use full range (0-255) instead of limited (16-235).
    bool use_444 = false;    ///< Use 4:4:4 chroma (no subsampling).
    bool lossless = false;   ///< Enable mathematically lossless encoding.
};

/// Video writer with explicit color space control and HDR10 support.
///
/// Unlike cv::VideoWriter, this class gives you direct control over:
/// - Color matrix (BT.709 / BT.2020)
/// - Range (full / limited)
/// - Pixel format (4:2:0, 4:2:2, 4:4:4, 8-bit and 10-bit)
/// - HDR10 static metadata (mastering display, content light level)
/// - Lossless encoding (FFV1, H.264 qp=0)
/// @note Not thread-safe. An instance holds mutable encoder state and mutates
/// it on every write(), so concurrent calls on one instance race. Separate
/// instances in separate threads are fine. The library-wide log level (see
/// LogLevel.h) is process-global and shared by all instances.
class VideoWriter {
  public:
    VideoWriter();
    ~VideoWriter();

    // Non-copyable
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    // Movable
    VideoWriter(VideoWriter&& other) noexcept;
    VideoWriter& operator=(VideoWriter&& other) noexcept;

    /// Open an output file for writing.
    /// @param filename   Output path (extension determines container).
    /// @param codec_id   AVCodecID (e.g., AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_FFV1).
    /// @param width      Frame width.
    /// @param height     Frame height.
    /// @param framerate  Frame rate as AVRational (e.g., {60000, 1001} for 59.94fps).
    /// @param opts       Encoding options (bitrate, pixel format, HDR, lossless, etc.).
    bool open(const std::string& filename, int codec_id, int width, int height,
              AVRational framerate, const VideoWriterOptions& opts = {});

    /// @deprecated Use the VideoWriterOptions overload instead.
    bool open(const std::string& filename, int codec_id, int width, int height,
              AVRational framerate, int bitrate,
              AVPixelFormat pix_fmt, bool is_10bit,
              bool full_range, bool use_444, bool lossless);

    /// Write a BGR frame (CV_8UC3 or CV_16UC3).
    ///
    /// @note Values are taken as already display-encoded for the writer's
    /// mode: in HDR10 mode (is_10bit) they are treated as PQ code values, in
    /// SDR mode as gamma-encoded. Use writeLinear() to write from linear
    /// light instead.
    bool write(const cv::Mat& image);

    /// Write a linear-light BGR frame (CV_32FC3), normalized so 1.0 == SDR
    /// reference white (100 nits) -- the exact convention
    /// VideoReader::readLinear() produces.
    ///
    /// In HDR10 mode the values are PQ-encoded (SMPTE ST 2084; 100.0 maps to
    /// the 10000-nit peak); in SDR mode they are clamped to [0, 1] and
    /// encoded with the BT.709 OETF. readLinear -> writeLinear -> readLinear
    /// round-trips to within codec loss.
    bool writeLinear(const cv::Mat& image);

    /// Flush buffered frames and finalize the file.
    void release();

    /// Flush buffered encoder packets without finalizing.
    void flush();

    void setHDR10Metadata(const HDR10Metadata& metadata);
    const HDR10Metadata& getHDR10Metadata() const { return hdr10_metadata_; }

    /// Get the current timestamp in seconds.
    double getCurrentTimestamp() const;

  private:
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVStream* videoStream_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    AVRational framerate_ = AVRational{1, 30};
    int64_t pts_ = 0;
    bool open_ = false;
    bool is_10bit_ = false;
    bool full_range_ = false;
    bool use_444_ = false;
    AVPixelFormat pix_fmt_ = AV_PIX_FMT_YUV420P;
    int codec_id_ = 0;
    HDR10Metadata hdr10_metadata_;
};

} // namespace framewright
