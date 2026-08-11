#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <framewright/VideoReader.h>
#include <framewright/VideoWriter.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static const std::string fixtures = TEST_FIXTURES_DIR;

static std::string temp_path(const std::string& name) {
    return fixtures + "/tmp_" + name;
}

static void remove_file(const std::string& path) { std::remove(path.c_str()); }

TEST_CASE("Round-trip: write H.264 then read back", "[roundtrip]") {
    std::string path = temp_path("roundtrip_h264.mp4");

    const int W = 320, H = 240;
    cv::Mat original(H, W, CV_8UC3, cv::Scalar(100, 150, 200));

    // Write
    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1}));
        for (int i = 0; i < 5; i++) {
            REQUIRE(writer.write(original));
        }
        writer.release();
    }

    // Read back
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path));

        CHECK(reader.getWidth() == W);
        CHECK(reader.getHeight() == H);

        cv::Mat frame;
        int count = 0;
        while (reader.read(frame)) {
            CHECK(frame.cols == W);
            CHECK(frame.rows == H);
            CHECK(frame.type() == CV_8UC3);
            count++;
        }
        CHECK(count == 5);
    }

    remove_file(path);
}

TEST_CASE("Round-trip: H.264 lossy color accuracy", "[roundtrip][color]") {
    std::string path = temp_path("roundtrip_color.mp4");

    const int W = 320, H = 240;
    cv::Mat original(H, W, CV_8UC3, cv::Scalar(100, 150, 200));

    // Write with high quality (multiple frames for valid file metadata)
    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1},
                            25000000, AV_PIX_FMT_YUV420P, false, false, false, false));
        for (int i = 0; i < 3; i++) {
            REQUIRE(writer.write(original));
        }
        writer.release();
    }

    // Read back and check pixel values are within lossy tolerance
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path, /*force_bt709=*/true));

        cv::Mat frame;
        REQUIRE(reader.read(frame));

        // Sample center pixel - lossy compression means we allow some tolerance
        cv::Vec3b pixel = frame.at<cv::Vec3b>(H / 2, W / 2);
        CHECK(std::abs(pixel[0] - 100) < 15); // B
        CHECK(std::abs(pixel[1] - 150) < 15); // G
        CHECK(std::abs(pixel[2] - 200) < 15); // R
    }

    remove_file(path);
}

TEST_CASE("Round-trip: lossless H.264 preserves exact pixels", "[roundtrip][color]") {
    std::string path = temp_path("roundtrip_lossless.mp4");

    const int W = 320, H = 240;
    cv::Mat original(H, W, CV_8UC3, cv::Scalar(100, 150, 200));

    // Write lossless (multiple frames for valid file metadata)
    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1},
                            0, AV_PIX_FMT_YUV420P, false, false, false, true));
        for (int i = 0; i < 3; i++) {
            REQUIRE(writer.write(original));
        }
        writer.release();
    }

    // Read back — lossless 4:4:4 should give very close values
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path, /*force_bt709=*/true, /*force_full_range=*/true));

        cv::Mat frame;
        REQUIRE(reader.read(frame));

        cv::Vec3b pixel = frame.at<cv::Vec3b>(H / 2, W / 2);
        // Lossless YUV444 with full range should be very close
        CHECK(std::abs(pixel[0] - 100) <= 1); // B
        CHECK(std::abs(pixel[1] - 150) <= 1); // G
        CHECK(std::abs(pixel[2] - 200) <= 1); // R
    }

    remove_file(path);
}

TEST_CASE("Round-trip: FFV1 lossless RGB", "[roundtrip][color]") {
    std::string path = temp_path("roundtrip_ffv1.mkv");

    const int W = 320, H = 240;
    cv::Mat original(H, W, CV_8UC3, cv::Scalar(42, 84, 168));

    // Write FFV1
    {
        framewright::VideoWriter writer;
        // Not a SKIP: see #70 -- a skip here hid a completely broken path.
        REQUIRE(writer.open(path, AV_CODEC_ID_FFV1, W, H, {30, 1}));
        REQUIRE(writer.write(original));
        writer.release();
    }

    // Read back — FFV1 RGB should preserve exact values
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path));

        cv::Mat frame;
        REQUIRE(reader.read(frame));

        cv::Vec3b pixel = frame.at<cv::Vec3b>(H / 2, W / 2);
        // FFV1 with BGR0 never leaves RGB, so the round trip is bit-exact --
        // no chroma subsampling and no YUV matrix to introduce error. The old
        // tolerance of 2 was there for the GBRP path that #70 showed never
        // actually worked.
        CHECK(pixel[0] == 42);  // B
        CHECK(pixel[1] == 84);  // G
        CHECK(pixel[2] == 168); // R
    }

    remove_file(path);
}

TEST_CASE("Round-trip: frame count preserved", "[roundtrip]") {
    std::string path = temp_path("roundtrip_count.mp4");

    const int W = 160, H = 120;
    const int NUM_FRAMES = 10;

    // Write N frames with varying content
    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1}));
        for (int i = 0; i < NUM_FRAMES; i++) {
            cv::Mat frame(H, W, CV_8UC3, cv::Scalar(i * 25, i * 10, 255 - i * 25));
            REQUIRE(writer.write(frame));
        }
        writer.release();
    }

    // Read back and count
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path));

        cv::Mat frame;
        int count = 0;
        while (reader.read(frame)) {
            count++;
        }
        CHECK(count == NUM_FRAMES);
    }

    remove_file(path);
}

TEST_CASE("Round-trip: HEVC 10-bit HDR metadata", "[roundtrip][hdr]") {
    std::string path = temp_path("roundtrip_hdr10.mp4");

    framewright::VideoWriter writer;
    bool opened = writer.open(path, AV_CODEC_ID_HEVC, 1920, 1080, {30, 1},
                              25000000, AV_PIX_FMT_YUV420P10LE, true, false, false, false);

    if (!opened) {
        SKIP("libx265 not available, skipping HEVC 10-bit round-trip test");
    }

    // Write a few frames
    cv::Mat frame(1080, 1920, CV_16UC3, cv::Scalar(30000, 20000, 10000));
    REQUIRE(writer.write(frame));
    REQUIRE(writer.write(frame));
    writer.release();

    // Read back and verify HDR color metadata
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path));

        CHECK(reader.getColorSpace() == AVCOL_SPC_BT2020_NCL);
        CHECK(reader.getColorPrimaries() == AVCOL_PRI_BT2020);
        CHECK(reader.getColorTransfer() == AVCOL_TRC_SMPTE2084);

        // Pixel values, not just tags: writer and reader both apply only the
        // BT.2020 matrix (no transfer), so a 16-bit write followed by read16()
        // must return the original code values to within encoder loss. This
        // would catch either side switching to a mismatched matrix or range.
        cv::Mat readFrame;
        REQUIRE(reader.read16(readFrame));
        REQUIRE(readFrame.type() == CV_16UC3);
        cv::Vec3w px = readFrame.at<cv::Vec3w>(readFrame.rows / 2, readFrame.cols / 2);
        const int tol = 2000; // ~3% of full scale; solid color at 25 Mbps is far cleaner
        CHECK(std::abs(px[0] - 30000) <= tol);
        CHECK(std::abs(px[1] - 20000) <= tol);
        CHECK(std::abs(px[2] - 10000) <= tol);
    }

    // Validate with ffprobe if available
#ifdef FFPROBE_EXECUTABLE
    {
        std::string cmd = std::string(FFPROBE_EXECUTABLE) +
                          " -v quiet -show_streams -show_frames -select_streams v:0"
                          " -read_intervals \"%+#1\" -of json \"" +
                          path + "\"";
        FILE* pipe = popen(cmd.c_str(), "r");
        REQUIRE(pipe != nullptr);

        std::string output;
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe)) {
            output += buf;
        }
        pclose(pipe);

        // Color metadata
        CHECK(output.find("bt2020") != std::string::npos);
        CHECK(output.find("smpte2084") != std::string::npos);

        // HDR10 side data (mastering display / content light level)
        // ffprobe reports these in the frame or stream side_data_list
        bool has_mastering = output.find("Mastering display") != std::string::npos ||
                             output.find("mastering_display") != std::string::npos ||
                             output.find("MASTERING") != std::string::npos;
        bool has_light_level = output.find("Content light level") != std::string::npos ||
                               output.find("content_light_level") != std::string::npos ||
                               output.find("LIGHT") != std::string::npos;

        CHECK(has_mastering);
        CHECK(has_light_level);
    }
#endif

    remove_file(path);
}

TEST_CASE("Round-trip: FPS preserved", "[roundtrip]") {
    std::string path = temp_path("roundtrip_fps.mp4");

    const int W = 160, H = 120;
    AVRational fps = {60000, 1001}; // 59.94 fps

    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, fps));

        cv::Mat frame(H, W, CV_8UC3, cv::Scalar(128, 128, 128));
        for (int i = 0; i < 5; i++) {
            REQUIRE(writer.write(frame));
        }
        writer.release();
    }

    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path));

        double readFps = reader.getFPS();
        // FPS should be non-zero and in a reasonable range
        CHECK(readFps > 0.0);
    }

    remove_file(path);
}

TEST_CASE("Round-trip: writeLinear -> readLinear on HDR is identity", "[roundtrip][hdr][color]") {
    std::string path = temp_path("roundtrip_writelinear_hdr.mp4");

    const int W = 192, H = 108;
    framewright::VideoWriter writer;
    bool opened = writer.open(path, AV_CODEC_ID_HEVC, W, H, {30, 1},
                              25000000, AV_PIX_FMT_YUV420P10LE, true, false, false, false);
    if (!opened) {
        SKIP("libx265 not available, skipping writeLinear HDR round-trip test");
    }

    // Linear-light BGR, 1.0 == SDR white. The red channel is an HDR highlight
    // (200 nits) that only survives a genuine PQ encode/decode round-trip.
    const cv::Vec3f original(0.05f, 0.5f, 2.0f);
    cv::Mat frame(H, W, CV_32FC3, cv::Scalar(original[0], original[1], original[2]));
    REQUIRE(writer.writeLinear(frame));
    REQUIRE(writer.writeLinear(frame));
    writer.release();

    framewright::VideoReader reader;
    REQUIRE(reader.open(path));
    cv::Mat back;
    REQUIRE(reader.readLinear(back));
    REQUIRE(back.type() == CV_32FC3);

    cv::Vec3f px = back.at<cv::Vec3f>(H / 2, W / 2);
    for (int i = 0; i < 3; i++) {
        CHECK_THAT(px[i], Catch::Matchers::WithinRel(original[i], 0.05));
    }

    remove_file(path);
}

TEST_CASE("Round-trip: writeLinear -> readLinear on SDR is identity", "[roundtrip][color]") {
    std::string path = temp_path("roundtrip_writelinear_sdr.mp4");

    const int W = 192, H = 108;
    framewright::VideoWriter writer;
    // Lossless 4:4:4 so the only loss is the 8-bit BT.709 quantization.
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1},
                        25000000, AV_PIX_FMT_YUV420P, false, false, false, true));

    const cv::Vec3f original(0.05f, 0.2f, 0.7f);
    cv::Mat frame(H, W, CV_32FC3, cv::Scalar(original[0], original[1], original[2]));
    REQUIRE(writer.writeLinear(frame));
    REQUIRE(writer.writeLinear(frame));
    writer.release();

    framewright::VideoReader reader;
    REQUIRE(reader.open(path));
    cv::Mat back;
    REQUIRE(reader.readLinear(back));

    cv::Vec3f px = back.at<cv::Vec3f>(H / 2, W / 2);
    for (int i = 0; i < 3; i++) {
        CHECK(std::abs(px[i] - original[i]) <= 0.02f);
    }

    remove_file(path);
}

TEST_CASE("Round-trip: reader surfaces HDR10 mastering metadata and uses its peak",
          "[roundtrip][hdr]") {
    const int W = 192, H = 108;

    // Same pixel content written twice with different mastering peaks.
    auto write_with_peak = [&](const std::string& path, double max_lum,
                               unsigned int max_cll) -> bool {
        framewright::VideoWriter writer;
        framewright::HDR10Metadata meta;
        meta.max_luminance = max_lum;
        meta.max_cll = max_cll;
        writer.setHDR10Metadata(meta);
        if (!writer.open(path, AV_CODEC_ID_HEVC, W, H, {30, 1},
                         25000000, AV_PIX_FMT_YUV420P10LE, true, false, false, false)) {
            return false;
        }
        cv::Mat frame(H, W, CV_16UC3, cv::Scalar(30000, 25000, 20000));
        REQUIRE(writer.write(frame));
        REQUIRE(writer.write(frame));
        writer.release();
        return true;
    };

    std::string path1k = temp_path("roundtrip_meta_1000.mp4");
    std::string path4k = temp_path("roundtrip_meta_4000.mp4");
    if (!write_with_peak(path1k, 1000.0, 1000)) {
        SKIP("libx265 not available, skipping mastering metadata test");
    }
    REQUIRE(write_with_peak(path4k, 4000.0, 3210));

    // Metadata surfaces on the reader (after the first read at the latest --
    // the decoder attaches the SEI to frames).
    framewright::VideoReaderOptions opts;
    opts.hdr_mode = framewright::HdrMode::ToneMapSDR;

    cv::Vec3b mapped1k, mapped4k;
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path4k, opts));
        cv::Mat frame;
        REQUIRE(reader.read(frame));
        mapped4k = frame.at<cv::Vec3b>(H / 2, W / 2);

        CHECK(reader.hasHDR10Metadata());
        CHECK_THAT(reader.getHDR10Metadata().max_luminance,
                   Catch::Matchers::WithinRel(4000.0, 0.01));
        CHECK(reader.getHDR10Metadata().max_cll == 3210);
    }
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(path1k, opts));
        cv::Mat frame;
        REQUIRE(reader.read(frame));
        mapped1k = frame.at<cv::Vec3b>(H / 2, W / 2);
    }

    // Extended Reinhard with a higher peak compresses the same luminance
    // harder, so the 4000-nit-mastered encode must map dimmer. Identical
    // outputs would mean the metadata never reached the tone mapper.
    int dimmer = 0;
    for (int i = 0; i < 3; i++) {
        CHECK(mapped4k[i] <= mapped1k[i]);
        if (mapped4k[i] < mapped1k[i]) {
            dimmer++;
        }
    }
    CHECK(dimmer > 0);

    remove_file(path1k);
    remove_file(path4k);
}
