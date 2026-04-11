#include "cvffmpeg/VideoWriter.h"

#include "cvffmpeg/LogLevel.h"

#include <cstddef>
#include <cstdio>

namespace cvffmpeg {

VideoWriter::VideoWriter() { av_log_set_level(AV_LOG_QUIET); }

VideoWriter::VideoWriter(VideoWriter&& other) noexcept
    : formatCtx_(other.formatCtx_), codecCtx_(other.codecCtx_), videoStream_(other.videoStream_),
      swsCtx_(other.swsCtx_), frame_(other.frame_), width_(other.width_), height_(other.height_),
      framerate_(other.framerate_), pts_(other.pts_), open_(other.open_),
      is_10bit_(other.is_10bit_), full_range_(other.full_range_), use_444_(other.use_444_),
      pix_fmt_(other.pix_fmt_), codec_id_(other.codec_id_),
      hdr10_metadata_(other.hdr10_metadata_) {
    av_log_set_level(AV_LOG_QUIET);

    other.formatCtx_ = nullptr;
    other.codecCtx_ = nullptr;
    other.videoStream_ = nullptr;
    other.swsCtx_ = nullptr;
    other.frame_ = nullptr;
    other.open_ = false;
}

VideoWriter& VideoWriter::operator=(VideoWriter&& other) noexcept {
    if (this != &other) {
        release();

        formatCtx_ = other.formatCtx_;
        codecCtx_ = other.codecCtx_;
        videoStream_ = other.videoStream_;
        swsCtx_ = other.swsCtx_;
        frame_ = other.frame_;
        width_ = other.width_;
        height_ = other.height_;
        framerate_ = other.framerate_;
        pts_ = other.pts_;
        open_ = other.open_;
        is_10bit_ = other.is_10bit_;
        full_range_ = other.full_range_;
        use_444_ = other.use_444_;
        pix_fmt_ = other.pix_fmt_;
        codec_id_ = other.codec_id_;
        hdr10_metadata_ = other.hdr10_metadata_;

        other.formatCtx_ = nullptr;
        other.codecCtx_ = nullptr;
        other.videoStream_ = nullptr;
        other.swsCtx_ = nullptr;
        other.frame_ = nullptr;
        other.open_ = false;
    }
    return *this;
}

VideoWriter::~VideoWriter() { release(); }

bool VideoWriter::open(const std::string& filename, int codec_id, int width, int height,
                       AVRational framerate, const VideoWriterOptions& opts) {
    return open(filename, codec_id, width, height, framerate, opts.bitrate, opts.pix_fmt,
                opts.is_10bit, opts.full_range, opts.use_444, opts.lossless);
}

bool VideoWriter::open(const std::string& filename, int codec_id, int width, int height,
                       AVRational framerate, int bitrate, AVPixelFormat pix_fmt, bool is_10bit,
                       bool full_range, bool use_444, bool lossless) {
    if (framerate.num <= 0 || framerate.den <= 0) {
        std::cerr << "Invalid framerate: " << framerate.num << "/" << framerate.den << std::endl;
        return false;
    }

    // Clean up any previously opened state (#19)
    release();

    this->width_ = width;
    this->height_ = height;
    this->framerate_ = framerate;
    this->is_10bit_ = is_10bit;
    this->full_range_ = full_range;
    this->use_444_ = use_444;
    this->pix_fmt_ = pix_fmt;
    this->codec_id_ = codec_id;

    // Override pix_fmt if 4:4:4 is requested
    if (use_444 && !is_10bit) {
        this->pix_fmt_ = AV_PIX_FMT_YUV444P;
        pix_fmt = this->pix_fmt_;
        detail::log(LogLevel::Info) << "Using 4:4:4 chroma (no subsampling) for maximum color fidelity" << std::endl;
    }

    // For lossless encoding, force 4:4:4 and full range for maximum quality
    if (lossless && !is_10bit) {
        this->pix_fmt_ = AV_PIX_FMT_YUV444P;
        pix_fmt = this->pix_fmt_;
        use_444_ = true;
        full_range_ = true;
        detail::log(LogLevel::Info) << "LOSSLESS mode: Using YUV444P (4:4:4) full range for zero quality loss"
                  << std::endl;
    }

    avformat_alloc_output_context2(&formatCtx_, nullptr, nullptr, filename.c_str());
    if (!formatCtx_) {
        detail::log(LogLevel::Error) << "Failed to allocate output context." << std::endl;
        release();
        return false;
    }

    const AVCodec* codec = nullptr;

    bool is_10bit_pix_fmt_early =
        (pix_fmt == AV_PIX_FMT_YUV420P10LE || pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         pix_fmt == AV_PIX_FMT_YUV444P10LE);

    // Use libx265 software encoder for all HEVC 10-bit
    if (codec_id == AV_CODEC_ID_HEVC && (is_10bit || is_10bit_pix_fmt_early)) {
        codec = avcodec_find_encoder_by_name("libx265");
        if (codec) {
            if (is_10bit) {
                detail::log(LogLevel::Info) << "Using libx265 for HEVC 10-bit HDR" << std::endl;
            } else {
                detail::log(LogLevel::Info) << "Using libx265 for HEVC 10-bit SDR" << std::endl;
            }
        }
    }

    // For FFV1, use RGB pixel format to avoid any YUV conversion errors
    if (codec_id == AV_CODEC_ID_FFV1) {
        this->pix_fmt_ = AV_PIX_FMT_GBRP;
        pix_fmt = this->pix_fmt_;
        detail::log(LogLevel::Info) << "Using FFV1 lossless codec with RGB pixel format (no YUV conversion)"
                  << std::endl;
    }

    if (!codec) {
        codec = avcodec_find_encoder(static_cast<AVCodecID>(codec_id));
        if (!codec) {
            detail::log(LogLevel::Error) << "Codec not found." << std::endl;
            release();
            return false;
        }
    }

    videoStream_ = avformat_new_stream(formatCtx_, codec);
    if (!videoStream_) {
        detail::log(LogLevel::Error) << "Failed to create stream." << std::endl;
        release();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        detail::log(LogLevel::Error) << "Failed to allocate codec context." << std::endl;
        release();
        return false;
    }

    codecCtx_->codec_id = static_cast<AVCodecID>(codec_id);
    codecCtx_->codec_type = AVMEDIA_TYPE_VIDEO;
    codecCtx_->pix_fmt = pix_fmt;

    codecCtx_->width = width;
    codecCtx_->height = height;
    codecCtx_->bit_rate = bitrate;
    detail::log(LogLevel::Info) << "framerate: " << framerate.num << " / " << framerate.den << std::endl;
    videoStream_->time_base = AVRational{framerate.den, framerate.num};
    codecCtx_->time_base = videoStream_->time_base;
    videoStream_->r_frame_rate = framerate;
    codecCtx_->framerate = framerate;
    codecCtx_->gop_size = 12;
    codecCtx_->max_b_frames = 0;

    if (is_10bit) {
        codecCtx_->color_primaries = AVCOL_PRI_BT2020;
        codecCtx_->color_trc = AVCOL_TRC_SMPTE2084;
        codecCtx_->colorspace = AVCOL_SPC_BT2020_NCL;
        codecCtx_->color_range = AVCOL_RANGE_MPEG;
    } else {
        codecCtx_->color_primaries = AVCOL_PRI_BT709;
        codecCtx_->color_trc = AVCOL_TRC_BT709;
        codecCtx_->colorspace = AVCOL_SPC_BT709;
        codecCtx_->color_range = full_range_ ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

        if (is_10bit_pix_fmt_early) {
            detail::log(LogLevel::Info) << "Using 10-bit SDR (BT.709, higher precision for intermediates)"
                      << std::endl;
        }
    }

    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary* opts = nullptr;
    bool using_libx265 = (codec && strcmp(codec->name, "libx265") == 0);

    if (codec_id == AV_CODEC_ID_FFV1) {
        av_dict_set(&opts, "level", "3", 0);
        av_dict_set(&opts, "coder", "1", 0);
        av_dict_set(&opts, "context", "0", 0);
        av_dict_set(&opts, "slices", "4", 0);
        av_dict_set(&opts, "slicecrc", "1", 0);
        detail::log(LogLevel::Info) << "FFV1 lossless encoding (RGB, no YUV conversion)" << std::endl;
    } else if (is_10bit && codec_id == AV_CODEC_ID_HEVC && using_libx265) {
        av_dict_set(&opts, "preset", "medium", 0);
        const auto& m = hdr10_metadata_;
        char x265_hdr_params[512];
        snprintf(x265_hdr_params, sizeof(x265_hdr_params),
                 "profile=main10:"
                 "colorprim=bt2020:"
                 "transfer=smpte2084:"
                 "colormatrix=bt2020nc:"
                 "master-display=G(%d,%d)B(%d,%d)R(%d,%d)WP(%d,%d)L(%d,%d):"
                 "max-cll=%u,%u",
                 static_cast<int>(m.green_x * 50000), static_cast<int>(m.green_y * 50000),
                 static_cast<int>(m.blue_x * 50000), static_cast<int>(m.blue_y * 50000),
                 static_cast<int>(m.red_x * 50000), static_cast<int>(m.red_y * 50000),
                 static_cast<int>(m.white_x * 50000), static_cast<int>(m.white_y * 50000),
                 static_cast<int>(m.max_luminance * 10000), static_cast<int>(m.min_luminance * 10000),
                 m.max_cll, m.max_fall);
        av_dict_set(&opts, "x265-params", x265_hdr_params, 0);
    } else if (is_10bit_pix_fmt_early && codec_id == AV_CODEC_ID_HEVC && using_libx265) {
        av_dict_set(&opts, "preset", "medium", 0);

        const char* x265_params =
            full_range_
                ? "profile=main10:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=pc"
                : "profile=main10:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=tv";
        av_dict_set(&opts, "x265-params", x265_params, 0);

        detail::log(LogLevel::Info) << "HEVC 10-bit SDR encoding (Main10, BT.709, "
                  << (full_range_ ? "full" : "limited") << " range)" << std::endl;
    } else if (!is_10bit && codec_id == AV_CODEC_ID_H264) {
        if (lossless) {
            av_dict_set(&opts, "preset", "medium", 0);
            av_dict_set(&opts, "profile", "high444", 0);
            av_dict_set(&opts, "qp", "0", 0);
            av_dict_set(&opts, "x264-params",
                        "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=pc", 0);
            detail::log(LogLevel::Info) << "H.264 lossless encoding (qp=0, 4:4:4)" << std::endl;
        } else {
            av_dict_set(&opts, "preset", "slow", 0);
            if (use_444_) {
                av_dict_set(&opts, "profile", "high444", 0);
            } else {
                av_dict_set(&opts, "profile", "high", 0);
            }
            av_dict_set(&opts, "crf", "10", 0);

            const char* x264_params =
                full_range_ ? "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=pc"
                            : "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=tv";
            av_dict_set(&opts, "x264-params", x264_params, 0);
        }
    }

    int ret = avcodec_open2(codecCtx_, codec, &opts);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        detail::log(LogLevel::Error) << "Failed to open codec: " << errbuf << " (error code: " << ret << ")"
                  << std::endl;
        detail::log(LogLevel::Info) << "Codec: " << codec->name
                  << ", Pixel format: " << av_get_pix_fmt_name(codecCtx_->pix_fmt) << std::endl;
        av_dict_free(&opts);
        release();
        return false;
    }

    av_dict_free(&opts);

    avcodec_parameters_from_context(videoStream_->codecpar, codecCtx_);

    // Set codec tag to 'hvc1' for QuickTime compatibility
    if (codec_id == AV_CODEC_ID_HEVC) {
        videoStream_->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        detail::log(LogLevel::Info) << "Set codec tag to hvc1 for QuickTime compatibility" << std::endl;
    }

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&formatCtx_->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
            detail::log(LogLevel::Error) << "Failed to open output file." << std::endl;
            release();
            return false;
        }
    }

    if (avformat_write_header(formatCtx_, nullptr) < 0) {
        detail::log(LogLevel::Error) << "Failed to write header." << std::endl;
        release();
        return false;
    }

    frame_ = av_frame_alloc();
    if (!frame_) {
        detail::log(LogLevel::Error) << "Failed to allocate frame." << std::endl;
        release();
        return false;
    }

    frame_->format = codecCtx_->pix_fmt;
    frame_->width = codecCtx_->width;
    frame_->height = codecCtx_->height;

    if (av_frame_get_buffer(frame_, 32) < 0) {
        detail::log(LogLevel::Error) << "Failed to allocate frame buffer." << std::endl;
        release();
        return false;
    }

    bool needs_16bit_input =
        (pix_fmt == AV_PIX_FMT_YUV420P10LE || pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         pix_fmt == AV_PIX_FMT_YUV444P10LE || codecCtx_->pix_fmt == AV_PIX_FMT_YUV420P10LE ||
         codecCtx_->pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         codecCtx_->pix_fmt == AV_PIX_FMT_YUV444P10LE);
    AVPixelFormat input_format = needs_16bit_input ? AV_PIX_FMT_BGR48LE : AV_PIX_FMT_BGR24;

    swsCtx_ = sws_getContext(width, height, input_format, width, height, codecCtx_->pix_fmt,
                            SWS_LANCZOS, nullptr, nullptr, nullptr);

    if (!swsCtx_) {
        detail::log(LogLevel::Error) << "Failed to create swscale context." << std::endl;
        release();
        return false;
    }

    // Configure color space conversion
    if (codec_id == AV_CODEC_ID_FFV1) {
        detail::log(LogLevel::Info) << "FFV1: No colorspace conversion needed (RGB->RGB)" << std::endl;
    } else if (is_10bit) {
        int* inv_table = nullptr;
        int* table = nullptr;
        int srcRange, dstRange, brightness, contrast, saturation;

        int ret = sws_getColorspaceDetails(swsCtx_, &inv_table, &srcRange, &table, &dstRange,
                                           &brightness, &contrast, &saturation);
        if (ret >= 0) {
            const int* src_coeff = sws_getCoefficients(SWS_CS_BT2020);
            const int* dst_coeff = sws_getCoefficients(SWS_CS_BT2020);
            sws_setColorspaceDetails(swsCtx_, src_coeff, 1, dst_coeff, 0, brightness, contrast,
                                     saturation);
            detail::log(LogLevel::Info) << "Configured swscale for BT.2020 HDR colorspace" << std::endl;
        } else {
            detail::log(LogLevel::Error) << "Warning: Failed to get colorspace details from swscale context"
                      << std::endl;
        }
    } else {
        int* inv_table = nullptr;
        int* table = nullptr;
        int srcRange, dstRange, brightness, contrast, saturation;

        int ret = sws_getColorspaceDetails(swsCtx_, &inv_table, &srcRange, &table, &dstRange,
                                           &brightness, &contrast, &saturation);
        if (ret >= 0) {
            const int* src_coeff = sws_getCoefficients(SWS_CS_ITU709);
            const int* dst_coeff = sws_getCoefficients(SWS_CS_ITU709);
            int dst_range = full_range_ ? 1 : 0;
            sws_setColorspaceDetails(swsCtx_, src_coeff, 1, dst_coeff, dst_range, brightness,
                                     contrast, saturation);
            detail::log(LogLevel::Info) << "Configured swscale for BT.709 SDR colorspace ("
                      << (full_range_ ? "full" : "limited") << " range)" << std::endl;
        }
    }

    open_ = true;
    return true;
}

bool VideoWriter::write(const cv::Mat& image) {
    if (image.empty()) {
        detail::log(LogLevel::Error) << "Empty frame provided." << std::endl;
        return false;
    }

    if (image.type() != CV_8UC3 && image.type() != CV_16UC3) {
        detail::log(LogLevel::Error) << "Invalid image type: " << image.type() << ". Expected CV_8UC3 or CV_16UC3."
                  << std::endl;
        return false;
    }

    if (image.cols != width_ || image.rows != height_) {
        detail::log(LogLevel::Error) << "Frame dimensions " << image.cols << "x" << image.rows
                  << " do not match writer dimensions " << width_ << "x" << height_ << std::endl;
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        detail::log(LogLevel::Error) << "Failed to allocate packet." << std::endl;
        return false;
    }

    bool needs_16bit_input =
        (pix_fmt_ == AV_PIX_FMT_YUV420P10LE || pix_fmt_ == AV_PIX_FMT_YUV422P10LE ||
         pix_fmt_ == AV_PIX_FMT_YUV444P10LE || codecCtx_->pix_fmt == AV_PIX_FMT_YUV420P10LE ||
         codecCtx_->pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         codecCtx_->pix_fmt == AV_PIX_FMT_YUV444P10LE);

    cv::Mat input_image;
    if (needs_16bit_input && image.type() == CV_8UC3) {
        image.convertTo(input_image, CV_16UC3, 257.0);
    } else if (!needs_16bit_input && image.type() == CV_16UC3) {
        image.convertTo(input_image, CV_8UC3, 1.0 / 257.0);
    } else {
        input_image = image;
    }

    const uint8_t* srcSlice[1] = {input_image.data};
    int srcStride[1] = {static_cast<int>(input_image.step)};

    int ret = sws_scale(swsCtx_, srcSlice, srcStride, 0, height_, frame_->data, frame_->linesize);
    if (ret < 0) {
        detail::log(LogLevel::Error) << "sws_scale failed with error: " << ret << std::endl;
        return false;
    }

    // Attach HDR10 static metadata for 10-bit HDR
    if (is_10bit_) {
        av_frame_remove_side_data(frame_, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        av_frame_remove_side_data(frame_, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);

        const auto& m = hdr10_metadata_;

        AVMasteringDisplayMetadata* mdcv = av_mastering_display_metadata_create_side_data(frame_);
        if (mdcv) {
            mdcv->display_primaries[0][0] = av_make_q(static_cast<int>(m.red_x * 50000), 50000);
            mdcv->display_primaries[0][1] = av_make_q(static_cast<int>(m.red_y * 50000), 50000);
            mdcv->display_primaries[1][0] = av_make_q(static_cast<int>(m.green_x * 50000), 50000);
            mdcv->display_primaries[1][1] = av_make_q(static_cast<int>(m.green_y * 50000), 50000);
            mdcv->display_primaries[2][0] = av_make_q(static_cast<int>(m.blue_x * 50000), 50000);
            mdcv->display_primaries[2][1] = av_make_q(static_cast<int>(m.blue_y * 50000), 50000);
            mdcv->white_point[0] = av_make_q(static_cast<int>(m.white_x * 50000), 50000);
            mdcv->white_point[1] = av_make_q(static_cast<int>(m.white_y * 50000), 50000);
            mdcv->max_luminance = av_make_q(static_cast<int>(m.max_luminance * 10000), 10000);
            mdcv->min_luminance = av_make_q(static_cast<int>(m.min_luminance * 10000), 10000);
            mdcv->has_primaries = 1;
            mdcv->has_luminance = 1;
        }

        AVContentLightMetadata* clli = av_content_light_metadata_create_side_data(frame_);
        if (clli) {
            clli->MaxCLL = m.max_cll;
            clli->MaxFALL = m.max_fall;
        }
    }

    frame_->pts = av_rescale_q(pts_, {framerate_.den, framerate_.num}, codecCtx_->time_base);
    pts_ += framerate_.den;

    int send_ret = avcodec_send_frame(codecCtx_, frame_);
    if (send_ret < 0) {
        detail::log(LogLevel::Error) << "Failed to send frame to encoder. Error code: " << send_ret << std::endl;
        return false;
    }

    while (true) {
        int receive_ret = avcodec_receive_packet(codecCtx_, packet);
        if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
            break;
        } else if (receive_ret < 0) {
            detail::log(LogLevel::Error) << "Error receiving packet from encoder: " << receive_ret << std::endl;
            break;
        }

        packet->stream_index = videoStream_->index;
        if (av_interleaved_write_frame(formatCtx_, packet) < 0) {
            detail::log(LogLevel::Error) << "Failed to write frame." << std::endl;
            av_packet_free(&packet);
            return false;
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return true;
}

void VideoWriter::release() {
    if (open_) {
        flush();
        if (formatCtx_) {
            av_write_trailer(formatCtx_);
        }
    }

    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }

    if (formatCtx_) {
        if (!(formatCtx_->oformat->flags & AVFMT_NOFILE) && formatCtx_->pb) {
            avio_closep(&formatCtx_->pb);
        }
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
    }

    videoStream_ = nullptr;
    open_ = false;
}

void VideoWriter::flush() {
    if (!open_) {
        return;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        detail::log(LogLevel::Error) << "Failed to allocate packet for flush." << std::endl;
        return;
    }

    if (avcodec_send_frame(codecCtx_, nullptr) < 0) {
        detail::log(LogLevel::Error) << "Failed to send flush frame to encoder." << std::endl;
        av_packet_free(&packet);
        return;
    }

    while (true) {
        int ret = avcodec_receive_packet(codecCtx_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            detail::log(LogLevel::Error) << "Error during encoder flush." << std::endl;
            break;
        }

        packet->stream_index = videoStream_->index;
        if (av_interleaved_write_frame(formatCtx_, packet) < 0) {
            detail::log(LogLevel::Error) << "Failed to write flush packet." << std::endl;
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
}

void VideoWriter::setHDR10Metadata(const HDR10Metadata& metadata) {
    hdr10_metadata_ = metadata;
}

double VideoWriter::getCurrentTimestamp() const {
    return static_cast<double>(pts_) / static_cast<double>(framerate_.num);
}

} // namespace cvffmpeg
