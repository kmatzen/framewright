#include <catch2/catch_test_macros.hpp>
#include <framewright/VideoWriter.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static const std::string fixtures = TEST_FIXTURES_DIR;

static std::string temp_path(const std::string& name) {
    return fixtures + "/tmp_" + name;
}

static void remove_file(const std::string& path) { std::remove(path.c_str()); }

// Helper: create a solid-color BGR frame
static cv::Mat make_frame(int width, int height, cv::Scalar color, int type = CV_8UC3) {
    return cv::Mat(height, width, type, color);
}

TEST_CASE("VideoWriter creates H.264 file", "[writer]") {
    std::string path = temp_path("writer_h264.mp4");

    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, 640, 480, {30, 1}));

        cv::Mat frame = make_frame(640, 480, {128, 64, 32});
        REQUIRE(writer.write(frame));
        REQUIRE(writer.write(frame));
        REQUIRE(writer.write(frame));
        writer.release();
    }

    // Verify the file exists and is non-empty
    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    CHECK(size > 0);

    remove_file(path);
}

TEST_CASE("VideoWriter creates H.264 with full range", "[writer]") {
    std::string path = temp_path("writer_h264_full.mp4");

    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {24, 1},
                            5000000, AV_PIX_FMT_YUV420P, false, true, false, false));

        cv::Mat frame = make_frame(320, 240, {200, 100, 50});
        REQUIRE(writer.write(frame));
        writer.release();
    }

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter creates H.264 with 4:4:4", "[writer]") {
    std::string path = temp_path("writer_h264_444.mp4");

    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1},
                            5000000, AV_PIX_FMT_YUV420P, false, false, true, false));

        cv::Mat frame = make_frame(320, 240, {100, 150, 200});
        REQUIRE(writer.write(frame));
        writer.release();
    }

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter creates lossless H.264", "[writer]") {
    std::string path = temp_path("writer_h264_lossless.mp4");

    {
        framewright::VideoWriter writer;
        REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1},
                            0, AV_PIX_FMT_YUV420P, false, false, false, true));

        cv::Mat frame = make_frame(320, 240, {50, 100, 150});
        REQUIRE(writer.write(frame));
        writer.release();
    }

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter write rejects empty frame", "[writer]") {
    std::string path = temp_path("writer_empty.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    cv::Mat empty;
    CHECK_FALSE(writer.write(empty));

    writer.release();
    remove_file(path);
}

TEST_CASE("VideoWriter write rejects wrong type", "[writer]") {
    std::string path = temp_path("writer_wrongtype.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    cv::Mat gray(240, 320, CV_8UC1, cv::Scalar(128));
    CHECK_FALSE(writer.write(gray));

    writer.release();
    remove_file(path);
}

TEST_CASE("VideoWriter getCurrentTimestamp advances", "[writer]") {
    std::string path = temp_path("writer_ts.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    double t0 = writer.getCurrentTimestamp();
    cv::Mat frame = make_frame(320, 240, {0, 0, 0});
    writer.write(frame);
    double t1 = writer.getCurrentTimestamp();
    writer.write(frame);
    double t2 = writer.getCurrentTimestamp();

    CHECK(t1 > t0);
    CHECK(t2 > t1);

    writer.release();
    remove_file(path);
}

TEST_CASE("VideoWriter move constructor", "[writer]") {
    std::string path = temp_path("writer_move.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    cv::Mat frame = make_frame(320, 240, {64, 128, 192});
    REQUIRE(writer.write(frame));

    framewright::VideoWriter moved(std::move(writer));
    REQUIRE(moved.write(frame));
    moved.release();

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter HEVC 10-bit HDR", "[writer][hdr]") {
    std::string path = temp_path("writer_hdr10.mp4");

    framewright::VideoWriter writer;
    bool opened = writer.open(path, AV_CODEC_ID_HEVC, 1920, 1080, {30, 1},
                              25000000, AV_PIX_FMT_YUV420P10LE, true, false, false, false);

    if (!opened) {
        // libx265 may not be available
        SKIP("libx265 not available, skipping HEVC 10-bit test");
    }

    cv::Mat frame = make_frame(1920, 1080, {30000, 20000, 10000}, CV_16UC3);
    REQUIRE(writer.write(frame));
    REQUIRE(writer.write(frame));
    writer.release();

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    // Validate HDR metadata with ffprobe if available
#ifdef FFPROBE_EXECUTABLE
    std::string cmd = std::string(FFPROBE_EXECUTABLE) +
                      " -v quiet -show_streams -of json \"" + path + "\"";
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);

    // Verify BT.2020 color metadata is present
    CHECK(output.find("bt2020") != std::string::npos);
    CHECK(output.find("smpte2084") != std::string::npos);
#endif

    remove_file(path);
}

TEST_CASE("VideoWriter rejects H.264 with HDR10", "[writer]") {
    std::string path = temp_path("writer_h264_hdr10.mp4");

    framewright::VideoWriter writer;
    bool opened = writer.open(path, AV_CODEC_ID_H264, 640, 480, {30, 1},
                              25000000, AV_PIX_FMT_YUV420P10LE, true, false, false, false);
    CHECK_FALSE(opened);

    remove_file(path);
}

TEST_CASE("VideoWriter rejects H.264 with 10-bit pixel format", "[writer]") {
    std::string path = temp_path("writer_h264_10bit.mp4");

    framewright::VideoWriter writer;
    bool opened = writer.open(path, AV_CODEC_ID_H264, 640, 480, {30, 1},
                              25000000, AV_PIX_FMT_YUV420P10LE, false, false, false, false);
    CHECK_FALSE(opened);

    remove_file(path);
}

TEST_CASE("VideoWriter creates HEVC 8-bit SDR", "[writer]") {
    std::string path = temp_path("writer_hevc_8bit.mp4");

    {
        framewright::VideoWriter writer;
        bool opened = writer.open(path, AV_CODEC_ID_HEVC, 640, 480, {30, 1});
        if (!opened) {
            SKIP("HEVC encoder not available");
        }

        cv::Mat frame = make_frame(640, 480, {128, 64, 32});
        REQUIRE(writer.write(frame));
        REQUIRE(writer.write(frame));
        writer.release();
    }

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter rejects invalid framerate", "[writer]") {
    std::string path = temp_path("writer_bad_framerate.mp4");

    framewright::VideoWriter writer;
    CHECK_FALSE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {0, 1}));
    CHECK_FALSE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 0}));
    CHECK_FALSE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {-30, 1}));

    remove_file(path);
}

TEST_CASE("VideoWriter rejects unknown codec id", "[writer]") {
    std::string path = temp_path("writer_bad_codec.mp4");

    framewright::VideoWriter writer;
    // An id no encoder advertises -- avcodec_find_encoder() must return null.
    CHECK_FALSE(writer.open(path, static_cast<int>(AV_CODEC_ID_NONE), 320, 240, {30, 1}));

    remove_file(path);
}

TEST_CASE("VideoWriter write rejects mismatched dimensions", "[writer]") {
    std::string path = temp_path("writer_dim_mismatch.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    cv::Mat wrong_size = make_frame(160, 120, {10, 20, 30});
    CHECK_FALSE(writer.write(wrong_size));

    writer.release();
    remove_file(path);
}

TEST_CASE("VideoWriter write before open fails without crashing", "[writer]") {
    framewright::VideoWriter writer;

    cv::Mat frame = make_frame(320, 240, {1, 2, 3});
    CHECK_FALSE(writer.write(frame));
}

TEST_CASE("VideoWriter flush can be called directly", "[writer]") {
    std::string path = temp_path("writer_flush.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    cv::Mat frame = make_frame(320, 240, {5, 10, 15});
    REQUIRE(writer.write(frame));
    writer.flush(); // must not crash; buffered packets are written out

    writer.release();

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter double release is safe", "[writer]") {
    std::string path = temp_path("writer_double_release.mp4");

    framewright::VideoWriter writer;
    REQUIRE(writer.open(path, AV_CODEC_ID_H264, 320, 240, {30, 1}));

    cv::Mat frame = make_frame(320, 240, {7, 8, 9});
    REQUIRE(writer.write(frame));

    writer.release();
    writer.release(); // must not crash or double-free

    remove_file(path);
}

TEST_CASE("VideoWriter custom HDR10 metadata is applied", "[writer][hdr]") {
    std::string path = temp_path("writer_hdr10_custom.mp4");

    framewright::VideoWriter writer;

    framewright::HDR10Metadata metadata;
    metadata.max_cll = 4000;
    metadata.max_fall = 1600;
    metadata.max_luminance = 4000.0;
    writer.setHDR10Metadata(metadata);

    CHECK(writer.getHDR10Metadata().max_cll == 4000);
    CHECK(writer.getHDR10Metadata().max_fall == 1600);

    bool opened = writer.open(path, AV_CODEC_ID_HEVC, 1920, 1080, {30, 1},
                              25000000, AV_PIX_FMT_YUV420P10LE, true, false, false, false);
    if (!opened) {
        SKIP("libx265 not available, skipping HEVC 10-bit test");
    }

    cv::Mat frame = make_frame(1920, 1080, {30000, 20000, 10000}, CV_16UC3);
    // The custom max_cll/max_fall/luminance values feed the x265-params
    // string built in open() (see VideoWriter.cpp); there is no portable way
    // to read HEVC HDR SEI light-level values back out via ffprobe, so this
    // exercises the code path (metadata set before open(), used while
    // encoding) without re-parsing the bitstream.
    REQUIRE(writer.write(frame));
    writer.release();

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}

TEST_CASE("VideoWriter FFV1 lossless", "[writer]") {
    std::string path = temp_path("writer_ffv1.mkv");

    {
        framewright::VideoWriter writer;
        // Not a SKIP: FFV1 is built into ffmpeg, so a failure here means we
        // handed the encoder something it cannot accept -- which is exactly
        // how #70 stayed hidden. Fail loudly instead.
        REQUIRE(writer.open(path, AV_CODEC_ID_FFV1, 320, 240, {30, 1}));

        cv::Mat frame = make_frame(320, 240, {42, 84, 168});
        REQUIRE(writer.write(frame));
        writer.release();
    }

    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) > 0);
    fclose(f);

    remove_file(path);
}
