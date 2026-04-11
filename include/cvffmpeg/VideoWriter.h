#pragma once

#include <opencv2/opencv.hpp>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libswscale/swscale.h>
}

namespace cvffmpeg {

/// HDR10 static metadata for mastering display and content light level.
struct HDR10Metadata {
    double red_x = 0.708, red_y = 0.292;
    double green_x = 0.170, green_y = 0.797;
    double blue_x = 0.131, blue_y = 0.046;
    double white_x = 0.3127, white_y = 0.3290;
    double max_luminance = 1000.0;
    double min_luminance = 0.0001;
    unsigned int max_cll = 1000;
    unsigned int max_fall = 400;
};

/// Video writer with explicit color space control and HDR10 support.
///
/// Unlike cv::VideoWriter, this class gives you direct control over:
/// - Color matrix (BT.709 / BT.2020)
/// - Range (full / limited)
/// - Pixel format (4:2:0, 4:2:2, 4:4:4, 8-bit and 10-bit)
/// - HDR10 static metadata (mastering display, content light level)
/// - Lossless encoding (FFV1, H.264 qp=0)
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
    /// @param bitrate    Target bitrate in bits/sec (default 25 Mbps).
    /// @param pix_fmt    Output pixel format (default YUV420P).
    /// @param is_10bit   Enable HDR10 mode (BT.2020 + PQ + mastering display metadata).
    /// @param full_range Use full range (0-255) instead of limited (16-235).
    /// @param use_444    Use 4:4:4 chroma (no subsampling).
    /// @param lossless   Enable mathematically lossless encoding.
    bool open(const std::string& filename, int codec_id, int width, int height,
              AVRational framerate, int bitrate = 25000000,
              AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P, bool is_10bit = false,
              bool full_range = false, bool use_444 = false, bool lossless = false);

    /// Write a BGR frame (CV_8UC3 or CV_16UC3).
    bool write(const cv::Mat& image);

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

} // namespace cvffmpeg
