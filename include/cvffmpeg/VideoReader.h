#pragma once

#include <opencv2/opencv.hpp>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace cvffmpeg {

/// Drop-in replacement for cv::VideoCapture with correct color space handling.
///
/// OpenCV's VideoCapture silently picks a YUV-to-RGB color matrix and gives
/// you no way to override it. This class uses FFmpeg directly and lets you
/// force BT.709, BT.601, or full-range conversion so the pixel values you
/// read are actually correct.
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

    /// Read the next frame as a BGR cv::Mat (same convention as OpenCV).
    bool read(cv::Mat& frame);

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

  private:
    void cleanup();
    bool setupScaler();

    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* frameBGR_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* swsCtx_ = nullptr;

    int videoStreamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 0.0;
    int64_t frame_count_ = 0;
    int64_t current_frame_ = 0;
    double current_timestamp_ = 0.0;

    bool force_bt709_ = false;
    bool force_full_range_ = false;
};

} // namespace cvffmpeg
