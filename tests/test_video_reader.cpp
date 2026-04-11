#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <framewright/VideoReader.h>

#include <string>

static const std::string fixtures = TEST_FIXTURES_DIR;

TEST_CASE("VideoReader opens a valid file", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    CHECK(reader.getWidth() == 1280);
    CHECK(reader.getHeight() == 720);
    CHECK(reader.getFPS() > 0.0);
}

TEST_CASE("VideoReader fails on nonexistent file", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE_FALSE(reader.open("/nonexistent/path/video.mp4"));
}

TEST_CASE("VideoReader reads frames", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    cv::Mat frame;
    REQUIRE(reader.read(frame));

    CHECK_FALSE(frame.empty());
    CHECK(frame.cols == 1280);
    CHECK(frame.rows == 720);
    CHECK(frame.type() == CV_8UC3);
}

TEST_CASE("VideoReader reads all frames until EOF", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    cv::Mat frame;
    int count = 0;
    while (reader.read(frame)) {
        count++;
        REQUIRE(count < 1000); // safety limit
    }

    CHECK(count > 0);
    CHECK(reader.getCurrentFrameNumber() == count);
}

TEST_CASE("VideoReader reports color metadata for BT.709", "[reader][color]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    CHECK(reader.getColorSpace() == AVCOL_SPC_BT709);
    // color_primaries and color_trc may not be tagged by all ffmpeg versions
    CHECK(reader.getColorRange() == AVCOL_RANGE_MPEG);
}

TEST_CASE("VideoReader reports full range", "[reader][color]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_full.mp4"));

    CHECK(reader.getColorRange() == AVCOL_RANGE_JPEG);
}

TEST_CASE("VideoReader force_bt709 flag", "[reader][color]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/sd_480p.mp4", /*force_bt709=*/true));

    CHECK(reader.isForcingBT709());
    CHECK_FALSE(reader.isForcingFullRange());
}

TEST_CASE("VideoReader force_full_range flag", "[reader][color]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4", /*force_bt709=*/false,
                        /*force_full_range=*/true));

    CHECK_FALSE(reader.isForcingBT709());
    CHECK(reader.isForcingFullRange());
}

TEST_CASE("VideoReader seek forward", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    // Read first frame
    cv::Mat frame;
    REQUIRE(reader.read(frame));
    CHECK(reader.getCurrentFrameNumber() == 1);

    // Seek forward (target frame 2 means we need to read one more)
    REQUIRE(reader.seek(2));
    CHECK(reader.getCurrentFrameNumber() == 2);
}

TEST_CASE("VideoReader seek backward fails", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    cv::Mat frame;
    reader.read(frame);
    reader.read(frame);
    CHECK(reader.getCurrentFrameNumber() == 2);

    // Backward seek is now supported via keyframe seeking
    REQUIRE(reader.seek(0));
}

TEST_CASE("VideoReader close and reopen", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    cv::Mat frame;
    reader.read(frame);
    reader.close();

    // Reopen same reader
    REQUIRE(reader.open(fixtures + "/bt709_full.mp4"));
    CHECK(reader.getWidth() == 320);
    CHECK(reader.getHeight() == 240);

    REQUIRE(reader.read(frame));
    CHECK(frame.cols == 320);
}

TEST_CASE("VideoReader move constructor", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    framewright::VideoReader moved(std::move(reader));
    CHECK(moved.getWidth() == 1280);

    cv::Mat frame;
    REQUIRE(moved.read(frame));
    CHECK_FALSE(frame.empty());
}

TEST_CASE("VideoReader move assignment", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    framewright::VideoReader other;
    other = std::move(reader);
    CHECK(other.getWidth() == 1280);

    cv::Mat frame;
    REQUIRE(other.read(frame));
}

#ifdef HAVE_HDR_FIXTURE
TEST_CASE("VideoReader opens HDR10 file", "[reader][hdr]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/hdr10.mp4"));

    CHECK(reader.getColorSpace() == AVCOL_SPC_BT2020_NCL);
    // color_primaries and color_trc tagging depends on ffmpeg version
    CHECK(reader.getColorRange() == AVCOL_RANGE_MPEG);

    cv::Mat frame;
    REQUIRE(reader.read(frame));
    CHECK_FALSE(frame.empty());
}
#endif
