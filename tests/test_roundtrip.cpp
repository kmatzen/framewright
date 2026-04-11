#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cvffmpeg/VideoReader.h>
#include <cvffmpeg/VideoWriter.h>

#include <cstdio>
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
        cvffmpeg::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1}));
        for (int i = 0; i < 5; i++) {
            REQUIRE(writer.write(original));
        }
        writer.release();
    }

    // Read back
    {
        cvffmpeg::VideoReader reader;
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

    // Write with high quality
    {
        cvffmpeg::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1}, 25000000));
        REQUIRE(writer.write(original));
        writer.release();
    }

    // Read back and check pixel values are within lossy tolerance
    {
        cvffmpeg::VideoReader reader;
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

    // Write lossless
    {
        cvffmpeg::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1}, 0, AV_PIX_FMT_YUV420P,
                            /*is_10bit=*/false, /*full_range=*/false, /*use_444=*/false,
                            /*lossless=*/true));
        REQUIRE(writer.write(original));
        writer.release();
    }

    // Read back — lossless 4:4:4 should give very close values
    {
        cvffmpeg::VideoReader reader;
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
        cvffmpeg::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_FFV1, W, H, {30, 1}));
        REQUIRE(writer.write(original));
        writer.release();
    }

    // Read back — FFV1 RGB should preserve exact values
    {
        cvffmpeg::VideoReader reader;
        REQUIRE(reader.open(path));

        cv::Mat frame;
        REQUIRE(reader.read(frame));

        cv::Vec3b pixel = frame.at<cv::Vec3b>(H / 2, W / 2);
        // FFV1 uses GBRP (planar RGB), so the round-trip through the reader's
        // scaler may introduce small differences depending on colorspace interpretation.
        // Allow tolerance of 2 for the colorspace conversion path.
        CHECK(std::abs(pixel[0] - 42) <= 2);  // B
        CHECK(std::abs(pixel[1] - 84) <= 2);  // G
        CHECK(std::abs(pixel[2] - 168) <= 2); // R
    }

    remove_file(path);
}

TEST_CASE("Round-trip: frame count preserved", "[roundtrip]") {
    std::string path = temp_path("roundtrip_count.mp4");

    const int W = 160, H = 120;
    const int NUM_FRAMES = 10;

    // Write N frames with varying content
    {
        cvffmpeg::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, {30, 1}));
        for (int i = 0; i < NUM_FRAMES; i++) {
            cv::Mat frame(H, W, CV_8UC3, cv::Scalar(i * 25, i * 10, 255 - i * 25));
            REQUIRE(writer.write(frame));
        }
        writer.release();
    }

    // Read back and count
    {
        cvffmpeg::VideoReader reader;
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

    cvffmpeg::VideoWriter writer;
    bool opened = writer.open(path, AV_CODEC_ID_HEVC, 1920, 1080, {30, 1}, 25000000,
                              AV_PIX_FMT_YUV420P10LE, /*is_10bit=*/true);

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
        cvffmpeg::VideoReader reader;
        REQUIRE(reader.open(path));

        CHECK(reader.getColorSpace() == AVCOL_SPC_BT2020_NCL);
        CHECK(reader.getColorPrimaries() == AVCOL_PRI_BT2020);
        CHECK(reader.getColorTransfer() == AVCOL_TRC_SMPTE2084);

        cv::Mat readFrame;
        REQUIRE(reader.read(readFrame));
        CHECK_FALSE(readFrame.empty());
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
        cvffmpeg::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, W, H, fps));

        cv::Mat frame(H, W, CV_8UC3, cv::Scalar(128, 128, 128));
        REQUIRE(writer.write(frame));
        writer.release();
    }

    {
        cvffmpeg::VideoReader reader;
        REQUIRE(reader.open(path));

        double readFps = reader.getFPS();
        double expectedFps = 60000.0 / 1001.0;
        CHECK_THAT(readFps, Catch::Matchers::WithinRel(expectedFps, 0.01));
    }

    remove_file(path);
}
