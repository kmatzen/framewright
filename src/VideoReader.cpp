#include "cvffmpeg/VideoReader.h"

#include <iostream>

namespace cvffmpeg {

VideoReader::VideoReader() {}

VideoReader::~VideoReader() { cleanup(); }

VideoReader::VideoReader(VideoReader&& other) noexcept
    : formatCtx_(other.formatCtx_), codecCtx_(other.codecCtx_), frame_(other.frame_),
      frameBGR_(other.frameBGR_), packet_(other.packet_), swsCtx_(other.swsCtx_),
      videoStreamIndex_(other.videoStreamIndex_), width_(other.width_), height_(other.height_),
      fps_(other.fps_), frame_count_(other.frame_count_), current_frame_(other.current_frame_),
      current_timestamp_(other.current_timestamp_),
      frame_pts_cache_(std::move(other.frame_pts_cache_)), force_bt709_(other.force_bt709_),
      force_full_range_(other.force_full_range_) {
    other.formatCtx_ = nullptr;
    other.codecCtx_ = nullptr;
    other.frame_ = nullptr;
    other.frameBGR_ = nullptr;
    other.packet_ = nullptr;
    other.swsCtx_ = nullptr;
}

VideoReader& VideoReader::operator=(VideoReader&& other) noexcept {
    if (this != &other) {
        cleanup();
        formatCtx_ = other.formatCtx_;
        codecCtx_ = other.codecCtx_;
        frame_ = other.frame_;
        frameBGR_ = other.frameBGR_;
        packet_ = other.packet_;
        swsCtx_ = other.swsCtx_;
        videoStreamIndex_ = other.videoStreamIndex_;
        width_ = other.width_;
        height_ = other.height_;
        fps_ = other.fps_;
        frame_count_ = other.frame_count_;
        current_frame_ = other.current_frame_;
        current_timestamp_ = other.current_timestamp_;
        frame_pts_cache_ = std::move(other.frame_pts_cache_);
        force_bt709_ = other.force_bt709_;
        force_full_range_ = other.force_full_range_;

        other.formatCtx_ = nullptr;
        other.codecCtx_ = nullptr;
        other.frame_ = nullptr;
        other.frameBGR_ = nullptr;
        other.packet_ = nullptr;
        other.swsCtx_ = nullptr;
    }
    return *this;
}

bool VideoReader::open(const std::string& filename, bool force_bt709, bool force_full_range) {
    cleanup();

    force_bt709_ = force_bt709;
    force_full_range_ = force_full_range;

    if (avformat_open_input(&formatCtx_, filename.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "cvffmpeg::VideoReader: Could not open file: " << filename << std::endl;
        return false;
    }

    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        std::cerr << "cvffmpeg::VideoReader: Could not find stream information" << std::endl;
        cleanup();
        return false;
    }

    videoStreamIndex_ = -1;
    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = i;
            break;
        }
    }

    if (videoStreamIndex_ == -1) {
        std::cerr << "cvffmpeg::VideoReader: Could not find video stream" << std::endl;
        cleanup();
        return false;
    }

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecpar = stream->codecpar;

    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "cvffmpeg::VideoReader: Unsupported codec" << std::endl;
        cleanup();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "cvffmpeg::VideoReader: Could not allocate codec context" << std::endl;
        cleanup();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecpar) < 0) {
        std::cerr << "cvffmpeg::VideoReader: Could not copy codec parameters" << std::endl;
        cleanup();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "cvffmpeg::VideoReader: Could not open codec" << std::endl;
        cleanup();
        return false;
    }

    width_ = codecCtx_->width;
    height_ = codecCtx_->height;

    if (stream->avg_frame_rate.den != 0) {
        fps_ = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.den != 0) {
        fps_ = av_q2d(stream->r_frame_rate);
    } else {
        fps_ = 30.0;
    }

    if (stream->nb_frames > 0) {
        frame_count_ = stream->nb_frames;
    } else {
        frame_count_ = -1;
    }

    frame_ = av_frame_alloc();
    frameBGR_ = av_frame_alloc();
    if (!frame_ || !frameBGR_) {
        std::cerr << "cvffmpeg::VideoReader: Could not allocate frame" << std::endl;
        cleanup();
        return false;
    }

    frameBGR_->format = AV_PIX_FMT_BGR24;
    frameBGR_->width = width_;
    frameBGR_->height = height_;
    if (av_frame_get_buffer(frameBGR_, 32) < 0) {
        std::cerr << "cvffmpeg::VideoReader: Could not allocate frame buffer" << std::endl;
        cleanup();
        return false;
    }

    packet_ = av_packet_alloc();
    if (!packet_) {
        std::cerr << "cvffmpeg::VideoReader: Could not allocate packet" << std::endl;
        cleanup();
        return false;
    }

    if (!setupScaler()) {
        cleanup();
        return false;
    }

    std::cerr << "cvffmpeg::VideoReader: Opened " << filename << std::endl;
    std::cerr << "  Resolution: " << width_ << "x" << height_ << std::endl;
    std::cerr << "  FPS: " << fps_ << std::endl;
    std::cerr << "  Pixel format: " << av_get_pix_fmt_name(codecCtx_->pix_fmt) << std::endl;
    std::cerr << "  Color space: " << av_color_space_name(codecCtx_->colorspace) << std::endl;
    std::cerr << "  Color range: " << av_color_range_name(codecCtx_->color_range) << std::endl;
    std::cerr << "  Color primaries: " << av_color_primaries_name(codecCtx_->color_primaries)
              << std::endl;
    std::cerr << "  Color transfer: " << av_color_transfer_name(codecCtx_->color_trc) << std::endl;

    if (force_bt709_) {
        std::cerr << "  -> Forcing BT.709 color matrix for conversion" << std::endl;
    }
    if (force_full_range_) {
        std::cerr << "  -> Forcing full range (0-255) for conversion" << std::endl;
    }

    current_frame_ = 0;
    frame_pts_cache_.clear();
    frame_pts_cache_.reserve(10000);

    return true;
}

bool VideoReader::setupScaler() {
    swsCtx_ = sws_getContext(width_, height_, codecCtx_->pix_fmt, width_, height_, AV_PIX_FMT_BGR24,
                             SWS_LANCZOS, nullptr, nullptr, nullptr);

    if (!swsCtx_) {
        std::cerr << "cvffmpeg::VideoReader: Could not create scaler context" << std::endl;
        return false;
    }

    int srcColorspace = codecCtx_->colorspace;
    int srcRange = codecCtx_->color_range;

    if (force_bt709_) {
        srcColorspace = AVCOL_SPC_BT709;
        std::cerr << "  Overriding color matrix to BT.709" << std::endl;
    } else if (srcColorspace == AVCOL_SPC_UNSPECIFIED) {
        if (height_ >= 720) {
            srcColorspace = AVCOL_SPC_BT709;
            std::cerr << "  Defaulting to BT.709 for HD content" << std::endl;
        } else {
            srcColorspace = AVCOL_SPC_BT470BG;
            std::cerr << "  Defaulting to BT.601 for SD content" << std::endl;
        }
    }

    if (force_full_range_) {
        srcRange = AVCOL_RANGE_JPEG;
        std::cerr << "  Overriding range to full (0-255)" << std::endl;
    } else if (srcRange == AVCOL_RANGE_UNSPECIFIED) {
        srcRange = AVCOL_RANGE_MPEG;
        std::cerr << "  Defaulting to limited range (16-235)" << std::endl;
    }

    const int* srcCoeffs = sws_getCoefficients(srcColorspace);
    const int* dstCoeffs = sws_getCoefficients(SWS_CS_DEFAULT);

    int brightness = 0;
    int contrast = 1 << 16;
    int saturation = 1 << 16;

    sws_setColorspaceDetails(swsCtx_, srcCoeffs, srcRange, dstCoeffs, AVCOL_RANGE_JPEG, brightness,
                             contrast, saturation);

    return true;
}

bool VideoReader::read(cv::Mat& frame) {
    if (!formatCtx_ || !codecCtx_) {
        return false;
    }

    while (true) {
        int ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0) {
            ret = avcodec_send_packet(codecCtx_, nullptr);
            if (ret < 0) {
                return false;
            }

            ret = avcodec_receive_frame(codecCtx_, frame_);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                return false;
            } else if (ret < 0) {
                std::cerr << "cvffmpeg::VideoReader: Error receiving flushed frame" << std::endl;
                return false;
            }

            sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height_, frameBGR_->data,
                      frameBGR_->linesize);

            frame = cv::Mat(height_, width_, CV_8UC3, frameBGR_->data[0], frameBGR_->linesize[0])
                        .clone();

            if (frame_->pts != AV_NOPTS_VALUE) {
                AVStream* stream = formatCtx_->streams[videoStreamIndex_];
                current_timestamp_ = frame_->pts * av_q2d(stream->time_base);
                frame_pts_cache_.push_back(frame_->pts);
            } else {
                frame_pts_cache_.push_back(AV_NOPTS_VALUE);
            }

            current_frame_++;
            return true;
        }

        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            std::cerr << "cvffmpeg::VideoReader: Error sending packet to decoder" << std::endl;
            return false;
        }

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            std::cerr << "cvffmpeg::VideoReader: Error receiving frame from decoder" << std::endl;
            return false;
        }

        sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height_, frameBGR_->data,
                  frameBGR_->linesize);

        frame =
            cv::Mat(height_, width_, CV_8UC3, frameBGR_->data[0], frameBGR_->linesize[0]).clone();

        if (frame_->pts != AV_NOPTS_VALUE) {
            AVStream* stream = formatCtx_->streams[videoStreamIndex_];
            current_timestamp_ = frame_->pts * av_q2d(stream->time_base);
            frame_pts_cache_.push_back(frame_->pts);
        } else {
            frame_pts_cache_.push_back(AV_NOPTS_VALUE);
        }

        current_frame_++;
        return true;
    }
}

bool VideoReader::seek(int64_t frame_number) {
    if (!formatCtx_ || !codecCtx_ || videoStreamIndex_ < 0) {
        return false;
    }

    if (current_frame_ > frame_number) {
        std::cerr << "cvffmpeg::VideoReader: Cannot seek backwards (current=" << current_frame_
                  << ", target=" << frame_number << ")" << std::endl;
        return false;
    }

    if (current_frame_ == frame_number) {
        return true;
    }

    std::cerr << "cvffmpeg::VideoReader: Reading forward from frame " << current_frame_ << " to "
              << frame_number << " (" << (frame_number - current_frame_) << " frames)" << std::endl;

    cv::Mat tempFrame;
    while (current_frame_ < frame_number && read(tempFrame)) {
    }

    bool success = (current_frame_ == frame_number);
    if (success) {
        std::cerr << "  Reached target frame " << frame_number << std::endl;
    } else {
        std::cerr << "  WARNING: Only reached frame " << current_frame_ << " (EOF?)" << std::endl;
    }

    return success;
}

void VideoReader::close() { cleanup(); }

void VideoReader::cleanup() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (frameBGR_) {
        av_frame_free(&frameBGR_);
        frameBGR_ = nullptr;
    }

    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }

    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }

    videoStreamIndex_ = -1;
    width_ = 0;
    height_ = 0;
    fps_ = 0.0;
    frame_count_ = 0;
    current_frame_ = 0;
    current_timestamp_ = 0.0;
    frame_pts_cache_.clear();
}

AVColorSpace VideoReader::getColorSpace() const {
    return codecCtx_ ? codecCtx_->colorspace : AVCOL_SPC_UNSPECIFIED;
}

AVColorRange VideoReader::getColorRange() const {
    return codecCtx_ ? codecCtx_->color_range : AVCOL_RANGE_UNSPECIFIED;
}

AVColorPrimaries VideoReader::getColorPrimaries() const {
    return codecCtx_ ? codecCtx_->color_primaries : AVCOL_PRI_UNSPECIFIED;
}

AVColorTransferCharacteristic VideoReader::getColorTransfer() const {
    return codecCtx_ ? codecCtx_->color_trc : AVCOL_TRC_UNSPECIFIED;
}

} // namespace cvffmpeg
