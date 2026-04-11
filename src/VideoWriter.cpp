#include "cvffmpeg/VideoWriter.h"

#include <cstddef>
#include <iostream>

namespace cvffmpeg {

VideoWriter::VideoWriter() { av_log_set_level(AV_LOG_QUIET); }

VideoWriter::VideoWriter(VideoWriter&& other) {
    formatCtx = other.formatCtx;
    codecCtx = other.codecCtx;
    videoStream = other.videoStream;
    swsCtx = other.swsCtx;
    frame = other.frame;
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

    other.formatCtx = nullptr;
    other.codecCtx = nullptr;
    other.videoStream = nullptr;
    other.swsCtx = nullptr;
    other.frame = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.framerate_ = AVRational{1, 30};
    other.pts_ = 0;
    other.open_ = false;
    other.is_10bit_ = false;
    other.full_range_ = false;
    other.use_444_ = false;
}

VideoWriter::~VideoWriter() { release(); }

bool VideoWriter::open(const std::string& filename, int codec_id, int width, int height,
                       AVRational framerate, int bitrate, AVPixelFormat pix_fmt, bool is_10bit,
                       bool full_range, bool use_444, bool lossless) {
    open_ = true;
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
        std::cerr << "Using 4:4:4 chroma (no subsampling) for maximum color fidelity" << std::endl;
    }

    // For lossless encoding, force 4:4:4 and full range for maximum quality
    if (lossless && !is_10bit) {
        this->pix_fmt_ = AV_PIX_FMT_YUV444P;
        pix_fmt = this->pix_fmt_;
        use_444_ = true;
        full_range_ = true;
        std::cerr << "LOSSLESS mode: Using YUV444P (4:4:4) full range for zero quality loss"
                  << std::endl;
    }

    avformat_alloc_output_context2(&formatCtx, nullptr, nullptr, filename.c_str());
    if (!formatCtx) {
        std::cerr << "Failed to allocate output context." << std::endl;
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
                std::cerr << "Using libx265 for HEVC 10-bit HDR" << std::endl;
            } else {
                std::cerr << "Using libx265 for HEVC 10-bit SDR" << std::endl;
            }
        }
    }

    // For FFV1, use RGB pixel format to avoid any YUV conversion errors
    if (codec_id == AV_CODEC_ID_FFV1) {
        this->pix_fmt_ = AV_PIX_FMT_GBRP;
        pix_fmt = this->pix_fmt_;
        std::cerr << "Using FFV1 lossless codec with RGB pixel format (no YUV conversion)"
                  << std::endl;
    }

    if (!codec) {
        codec = avcodec_find_encoder(static_cast<AVCodecID>(codec_id));
        if (!codec) {
            std::cerr << "Codec not found." << std::endl;
            return false;
        }
    }

    videoStream = avformat_new_stream(formatCtx, codec);
    if (!videoStream) {
        std::cerr << "Failed to create stream." << std::endl;
        return false;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        std::cerr << "Failed to allocate codec context." << std::endl;
        return false;
    }

    codecCtx->codec_id = static_cast<AVCodecID>(codec_id);
    codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    codecCtx->pix_fmt = pix_fmt;

    codecCtx->width = width;
    codecCtx->height = height;
    codecCtx->bit_rate = bitrate;
    std::cerr << "framerate: " << framerate.num << " / " << framerate.den << std::endl;
    videoStream->time_base = AVRational{framerate.den, framerate.num};
    codecCtx->time_base = videoStream->time_base;
    videoStream->r_frame_rate = framerate;
    codecCtx->framerate = framerate;
    codecCtx->gop_size = 12;
    codecCtx->max_b_frames = 0;

    if (is_10bit) {
        codecCtx->color_primaries = AVCOL_PRI_BT2020;
        codecCtx->color_trc = AVCOL_TRC_SMPTE2084;
        codecCtx->colorspace = AVCOL_SPC_BT2020_NCL;
        codecCtx->color_range = AVCOL_RANGE_MPEG;
    } else {
        codecCtx->color_primaries = AVCOL_PRI_BT709;
        codecCtx->color_trc = AVCOL_TRC_BT709;
        codecCtx->colorspace = AVCOL_SPC_BT709;
        codecCtx->color_range = full_range_ ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

        if (is_10bit_pix_fmt_early) {
            std::cerr << "Using 10-bit SDR (BT.709, higher precision for intermediates)"
                      << std::endl;
        }
    }

    if (formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary* opts = nullptr;
    bool using_libx265 = (codec && strcmp(codec->name, "libx265") == 0);

    if (codec_id == AV_CODEC_ID_FFV1) {
        av_dict_set(&opts, "level", "3", 0);
        av_dict_set(&opts, "coder", "1", 0);
        av_dict_set(&opts, "context", "0", 0);
        av_dict_set(&opts, "slices", "4", 0);
        av_dict_set(&opts, "slicecrc", "1", 0);
        std::cerr << "FFV1 lossless encoding (RGB, no YUV conversion)" << std::endl;
    } else if (is_10bit && codec_id == AV_CODEC_ID_HEVC && using_libx265) {
        av_dict_set(&opts, "preset", "medium", 0);
        av_dict_set(
            &opts, "x265-params",
            "profile=main10:"
            "colorprim=bt2020:"
            "transfer=smpte2084:"
            "colormatrix=bt2020nc:"
            "master-display=G(8500,39850)B(6550,2300)R(35400,14600)WP(15635,16450)L(10000000,1):"
            "max-cll=1000,400",
            0);
    } else if (is_10bit_pix_fmt_early && codec_id == AV_CODEC_ID_HEVC && using_libx265) {
        av_dict_set(&opts, "preset", "medium", 0);

        const char* x265_params =
            full_range_
                ? "profile=main10:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=pc"
                : "profile=main10:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=tv";
        av_dict_set(&opts, "x265-params", x265_params, 0);

        std::cerr << "HEVC 10-bit SDR encoding (Main10, BT.709, "
                  << (full_range_ ? "full" : "limited") << " range)" << std::endl;
    } else if (!is_10bit && codec_id == AV_CODEC_ID_H264) {
        if (lossless) {
            av_dict_set(&opts, "preset", "medium", 0);
            av_dict_set(&opts, "profile", "high444", 0);
            av_dict_set(&opts, "qp", "0", 0);
            av_dict_set(&opts, "x264-params",
                        "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=pc", 0);
            std::cerr << "H.264 lossless encoding (qp=0, 4:4:4)" << std::endl;
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

    int ret = avcodec_open2(codecCtx, codec, &opts);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to open codec: " << errbuf << " (error code: " << ret << ")"
                  << std::endl;
        std::cerr << "Codec: " << codec->name
                  << ", Pixel format: " << av_get_pix_fmt_name(codecCtx->pix_fmt) << std::endl;
        return false;
    }

    av_dict_free(&opts);

    avcodec_parameters_from_context(videoStream->codecpar, codecCtx);

    // Set codec tag to 'hvc1' for QuickTime compatibility
    if (codec_id == AV_CODEC_ID_HEVC) {
        videoStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        std::cerr << "Set codec tag to hvc1 for QuickTime compatibility" << std::endl;
    }

    if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&formatCtx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Failed to open output file." << std::endl;
            return false;
        }
    }

    if (avformat_write_header(formatCtx, nullptr) < 0) {
        std::cerr << "Failed to write header." << std::endl;
        return false;
    }

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Failed to allocate frame." << std::endl;
        return false;
    }

    frame->format = codecCtx->pix_fmt;
    frame->width = codecCtx->width;
    frame->height = codecCtx->height;

    if (av_frame_get_buffer(frame, 32) < 0) {
        std::cerr << "Failed to allocate frame buffer." << std::endl;
        return false;
    }

    bool needs_16bit_input =
        (pix_fmt == AV_PIX_FMT_YUV420P10LE || pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         pix_fmt == AV_PIX_FMT_YUV444P10LE || codecCtx->pix_fmt == AV_PIX_FMT_YUV420P10LE ||
         codecCtx->pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         codecCtx->pix_fmt == AV_PIX_FMT_YUV444P10LE);
    AVPixelFormat input_format = needs_16bit_input ? AV_PIX_FMT_BGR48LE : AV_PIX_FMT_BGR24;

    swsCtx = sws_getContext(width, height, input_format, width, height, codecCtx->pix_fmt,
                            SWS_LANCZOS, nullptr, nullptr, nullptr);

    if (!swsCtx) {
        std::cerr << "Failed to create swscale context." << std::endl;
        return false;
    }

    // Configure color space conversion
    if (codec_id == AV_CODEC_ID_FFV1) {
        std::cerr << "FFV1: No colorspace conversion needed (RGB->RGB)" << std::endl;
    } else if (is_10bit) {
        int* inv_table = nullptr;
        int* table = nullptr;
        int srcRange, dstRange, brightness, contrast, saturation;

        int ret = sws_getColorspaceDetails(swsCtx, &inv_table, &srcRange, &table, &dstRange,
                                           &brightness, &contrast, &saturation);
        if (ret >= 0) {
            const int* src_coeff = sws_getCoefficients(SWS_CS_BT2020);
            const int* dst_coeff = sws_getCoefficients(SWS_CS_BT2020);
            sws_setColorspaceDetails(swsCtx, src_coeff, 1, dst_coeff, 0, brightness, contrast,
                                     saturation);
            std::cerr << "Configured swscale for BT.2020 HDR colorspace" << std::endl;
        } else {
            std::cerr << "Warning: Failed to get colorspace details from swscale context"
                      << std::endl;
        }
    } else {
        int* inv_table = nullptr;
        int* table = nullptr;
        int srcRange, dstRange, brightness, contrast, saturation;

        int ret = sws_getColorspaceDetails(swsCtx, &inv_table, &srcRange, &table, &dstRange,
                                           &brightness, &contrast, &saturation);
        if (ret >= 0) {
            const int* src_coeff = sws_getCoefficients(SWS_CS_ITU709);
            const int* dst_coeff = sws_getCoefficients(SWS_CS_ITU709);
            int dst_range = full_range_ ? 1 : 0;
            sws_setColorspaceDetails(swsCtx, src_coeff, 1, dst_coeff, dst_range, brightness,
                                     contrast, saturation);
            std::cerr << "Configured swscale for BT.709 SDR colorspace ("
                      << (full_range_ ? "full" : "limited") << " range)" << std::endl;
        }
    }

    return true;
}

bool VideoWriter::write(const cv::Mat& image) {
    if (image.empty()) {
        std::cerr << "Empty frame provided." << std::endl;
        return false;
    }

    if (image.type() != CV_8UC3 && image.type() != CV_16UC3) {
        std::cerr << "Invalid image type: " << image.type() << ". Expected CV_8UC3 or CV_16UC3."
                  << std::endl;
        return false;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    bool needs_16bit_input =
        (pix_fmt_ == AV_PIX_FMT_YUV420P10LE || pix_fmt_ == AV_PIX_FMT_YUV422P10LE ||
         pix_fmt_ == AV_PIX_FMT_YUV444P10LE || codecCtx->pix_fmt == AV_PIX_FMT_YUV420P10LE ||
         codecCtx->pix_fmt == AV_PIX_FMT_YUV422P10LE ||
         codecCtx->pix_fmt == AV_PIX_FMT_YUV444P10LE);

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

    int ret = sws_scale(swsCtx, srcSlice, srcStride, 0, height_, frame->data, frame->linesize);
    if (ret < 0) {
        std::cerr << "sws_scale failed with error: " << ret << std::endl;
        return false;
    }

    // Attach HDR10 static metadata for 10-bit HDR
    if (is_10bit_) {
        av_frame_remove_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        av_frame_remove_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);

        AVMasteringDisplayMetadata* mdcv = av_mastering_display_metadata_create_side_data(frame);
        if (mdcv) {
            mdcv->display_primaries[0][0] = av_make_q(35400, 50000);
            mdcv->display_primaries[0][1] = av_make_q(14600, 50000);
            mdcv->display_primaries[1][0] = av_make_q(8500, 50000);
            mdcv->display_primaries[1][1] = av_make_q(39850, 50000);
            mdcv->display_primaries[2][0] = av_make_q(6550, 50000);
            mdcv->display_primaries[2][1] = av_make_q(2300, 50000);
            mdcv->white_point[0] = av_make_q(15635, 50000);
            mdcv->white_point[1] = av_make_q(16450, 50000);
            mdcv->max_luminance = av_make_q(10000000, 10000);
            mdcv->min_luminance = av_make_q(1, 10000);
            mdcv->has_primaries = 1;
            mdcv->has_luminance = 1;
        }

        AVContentLightMetadata* clli = av_content_light_metadata_create_side_data(frame);
        if (clli) {
            clli->MaxCLL = 1000;
            clli->MaxFALL = 400;
        }
    }

    frame->pts = av_rescale_q(pts_, {framerate_.den, framerate_.num}, codecCtx->time_base);
    pts_ += framerate_.den;

    int send_ret = avcodec_send_frame(codecCtx, frame);
    if (send_ret < 0) {
        std::cerr << "Failed to send frame to encoder. Error code: " << send_ret << std::endl;
        return false;
    }

    while (true) {
        int receive_ret = avcodec_receive_packet(codecCtx, &packet);
        if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
            break;
        } else if (receive_ret < 0) {
            std::cerr << "Error receiving packet from encoder: " << receive_ret << std::endl;
            break;
        }

        packet.stream_index = videoStream->index;
        if (av_interleaved_write_frame(formatCtx, &packet) < 0) {
            std::cerr << "Failed to write frame." << std::endl;
            av_packet_unref(&packet);
            return false;
        }
        av_packet_unref(&packet);
    }

    return true;
}

void VideoWriter::release() {
    if (!open_) {
        return;
    }
    flush();

    if (formatCtx) {
        av_write_trailer(formatCtx);
    }

    if (frame) {
        av_frame_free(&frame);
    }

    if (codecCtx) {
        avcodec_free_context(&codecCtx);
    }

    if (swsCtx) {
        sws_freeContext(swsCtx);
    }

    if (formatCtx) {
        if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&formatCtx->pb);
        }
        avformat_free_context(formatCtx);
    }

    open_ = false;
}

void VideoWriter::flush() {
    if (!open_) {
        return;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    if (avcodec_send_frame(codecCtx, nullptr) < 0) {
        std::cerr << "Failed to send flush frame to encoder." << std::endl;
        return;
    }

    while (true) {
        int ret = avcodec_receive_packet(codecCtx, &packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "Error during encoder flush." << std::endl;
            break;
        }

        packet.stream_index = videoStream->index;
        if (av_interleaved_write_frame(formatCtx, &packet) < 0) {
            std::cerr << "Failed to write flush packet." << std::endl;
        }

        av_packet_unref(&packet);
    }
}

double VideoWriter::getCurrentTimestamp() const {
    return static_cast<double>(pts_) / static_cast<double>(framerate_.num);
}

} // namespace cvffmpeg
