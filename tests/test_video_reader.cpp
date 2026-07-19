#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <framewright/VideoReader.h>

#include <cstdlib>
#include <string>
#include <vector>

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

#ifdef HAVE_SEEK_FIXTURE

// seek_numbered.mp4 encodes each frame's index in its luma (16 + 3*N) and has
// keyframes at 0 and 30, so we can check *which* frame a seek actually landed
// on rather than just that seek() returned true.
namespace {

constexpr int kSeekFixtureFrames = 60;

int lumaOf(const cv::Mat& m) { return m.at<cv::Vec3b>(m.rows / 2, m.cols / 2)[1]; }

// Reference table of per-frame luma, built by a plain sequential decode.
std::vector<int> seekReference(const std::string& path) {
    std::vector<int> r;
    framewright::VideoReader reader;
    REQUIRE(reader.open(path));
    cv::Mat frame;
    while (reader.read(frame)) r.push_back(lumaOf(frame));
    return r;
}

// Which frame index does this Mat correspond to? -1 if it matches nothing.
int identifyFrame(const cv::Mat& m, const std::vector<int>& ref) {
    int v = lumaOf(m), best = -1, bestDist = 1 << 30;
    for (size_t i = 0; i < ref.size(); i++) {
        int d = std::abs(ref[i] - v);
        if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
    }
    return bestDist <= 1 ? best : -1;
}

// The seek contract, checked against whichever numbered fixture is given.
// Run against both the all-I/P fixture and the B-frame one: the latter makes
// the decoder hold frames in a reorder buffer, so decode order and
// presentation order differ and seek()'s pts-based position recovery has to
// cope with that.
void checkSeekCases(const std::string& path) {
    const std::vector<int> ref = seekReference(path);
    REQUIRE(ref.size() == static_cast<size_t>(kSeekFixtureFrames));

    // Guard the fixture itself. If two frames decode to the same luma then
    // identifyFrame() cannot tell them apart and every check below is
    // meaningless. Catches e.g. luma values clipping at the limited-range
    // ceiling, which would otherwise surface as a confusing frame mismatch.
    for (size_t i = 1; i < ref.size(); i++) {
        INFO("frames " << (i - 1) << " and " << i << " decode to luma " << ref[i - 1] << " and "
                       << ref[i]);
        REQUIRE(std::abs(ref[i] - ref[i - 1]) > 2);
    }

    // {start position, seek target}. Must include backward seeks and forward
    // jumps of more than 50 frames: those are the only ones that take seek()'s
    // keyframe path. A test built only from short forward jumps takes the
    // scan path and passes even when the keyframe path is broken.
    struct Case { int from; int target; };
    const Case cases[] = {
        {1, 0},    // backward to the very start
        {50, 0},   // backward across a keyframe
        {50, 5},   // backward, target between keyframes
        {50, 29},  // backward, target just before a keyframe
        {50, 30},  // backward, target IS a keyframe
        {50, 35},  // backward, target just after a keyframe
        {40, 35},  // short backward hop
        {35, 31},  // short backward hop, just past a keyframe
        {0, 55},   // forward jump > 50 frames, also takes the keyframe path
        {1, 5},    // short forward jump: scan path
        {1, 29},   // scan path
        {1, 45},   // scan path
    };

    for (const Case& c : cases) {
        INFO("seek from " << c.from << " to " << c.target << " in " << path);

        framewright::VideoReader reader;
        REQUIRE(reader.open(path));

        cv::Mat frame;
        for (int i = 0; i < c.from; i++) REQUIRE(reader.read(frame));

        REQUIRE(reader.seek(c.target));
        CHECK(reader.getCurrentFrameNumber() == c.target);

        // The contract: the next read() returns exactly the frame that
        // getCurrentFrameNumber() just advertised.
        REQUIRE(reader.read(frame));
        CHECK(identifyFrame(frame, ref) == c.target);
    }
}

} // namespace

TEST_CASE("VideoReader seek lands on the frame it reports", "[reader][seek]") {
    checkSeekCases(fixtures + "/seek_numbered.mp4");
}

#ifdef HAVE_SEEK_BFRAME_FIXTURE
TEST_CASE("VideoReader seek lands on the frame it reports (B-frames)", "[reader][seek]") {
    checkSeekCases(fixtures + "/seek_numbered_bframes.mp4");
}
#endif

// Regression for #59: the decoder's reorder buffer must be fully drained at
// EOF. Sending a second flush packet returns AVERROR_EOF, and treating that as
// fatal used to strand every buffered frame but the first -- silently costing
// the tail of any B-frame encoded file.
TEST_CASE("VideoReader reads every frame through to EOF", "[reader][seek]") {
    CHECK(seekReference(fixtures + "/seek_numbered.mp4").size()
          == static_cast<size_t>(kSeekFixtureFrames));

#ifdef HAVE_SEEK_BFRAME_FIXTURE
    INFO("B-frame fixture: decoder holds frames in a reorder buffer at EOF");
    CHECK(seekReference(fixtures + "/seek_numbered_bframes.mp4").size()
          == static_cast<size_t>(kSeekFixtureFrames));
#endif
}

TEST_CASE("VideoReader seek leaves the position counter consistent", "[reader][seek]") {
    const std::string path = fixtures + "/seek_numbered.mp4";
    const std::vector<int> ref = seekReference(path);

    framewright::VideoReader reader;
    REQUIRE(reader.open(path));

    cv::Mat frame;

    // A sequence of seeks in both directions must not accumulate drift.
    const int targets[] = {45, 10, 44, 30, 0, 59, 31};
    for (int t : targets) {
        INFO("seek to " << t);
        REQUIRE(reader.seek(t));
        CHECK(reader.getCurrentFrameNumber() == t);
        REQUIRE(reader.read(frame));
        CHECK(identifyFrame(frame, ref) == t);
        // read() advanced us by exactly one.
        CHECK(reader.getCurrentFrameNumber() == t + 1);
    }
}

TEST_CASE("VideoReader seek past the end fails", "[reader][seek]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/seek_numbered.mp4"));

    cv::Mat frame;
    REQUIRE(reader.read(frame));

    CHECK_FALSE(reader.seek(kSeekFixtureFrames + 100));
}

// A seek that cannot succeed should be rejected outright, not by scanning to
// EOF and discovering the failure there. The observable difference is the
// position: rejecting early leaves the reader where it was, whereas the old
// scan consumed the rest of the file first. See #67.
TEST_CASE("VideoReader rejects out-of-range seek without moving", "[reader][seek]") {
    const std::string path = fixtures + "/seek_numbered.mp4";
    const std::vector<int> ref = seekReference(path);

    framewright::VideoReader reader;
    REQUIRE(reader.open(path));
    REQUIRE(reader.getFrameCount() == kSeekFixtureFrames);

    cv::Mat frame;
    for (int i = 0; i < 5; i++) REQUIRE(reader.read(frame));
    REQUIRE(reader.getCurrentFrameNumber() == 5);

    CHECK_FALSE(reader.seek(kSeekFixtureFrames));       // one past the last valid index
    CHECK_FALSE(reader.seek(kSeekFixtureFrames + 100));

    // Still at frame 5, and the next read still returns frame 5.
    CHECK(reader.getCurrentFrameNumber() == 5);
    REQUIRE(reader.read(frame));
    CHECK(identifyFrame(frame, ref) == 5);
}

TEST_CASE("VideoReader readRef returns the same pixels as read", "[reader][zerocopy]") {
    const std::string path = fixtures + "/seek_numbered.mp4";

    framewright::VideoReader a, b;
    REQUIRE(a.open(path));
    REQUIRE(b.open(path));

    cv::Mat copied, viewed;
    for (int i = 0; i < 5; i++) {
        INFO("frame " << i);
        REQUIRE(a.read(copied));
        REQUIRE(b.readRef(viewed));
        REQUIRE(copied.size() == viewed.size());
        CHECK(cv::countNonZero(cv::sum(cv::abs(copied - viewed)) != cv::Scalar(0, 0, 0)) == 0);
    }

    // Position tracking must not differ between the two paths.
    CHECK(a.getCurrentFrameNumber() == b.getCurrentFrameNumber());
}

// The point of readRef() is that it does NOT copy. A version that quietly kept
// cloning would satisfy every "same pixels" check above, so assert the actual
// aliasing: the previously returned Mat must change when the next frame is
// decoded over the top of it. See #73.
TEST_CASE("VideoReader readRef aliases the internal buffer", "[reader][zerocopy]") {
    const std::string path = fixtures + "/seek_numbered.mp4";

    framewright::VideoReader reader;
    REQUIRE(reader.open(path));

    cv::Mat view;
    REQUIRE(reader.readRef(view));
    const uchar* firstData = view.data;
    const int firstLuma = view.at<cv::Vec3b>(view.rows / 2, view.cols / 2)[1];

    REQUIRE(reader.readRef(view));

    // Same buffer address: it is a view, not a copy.
    CHECK(view.data == firstData);
    // And its contents were overwritten by the second decode. The fixture's
    // luma steps by 3 per frame, so this is a real change, not noise.
    CHECK(view.at<cv::Vec3b>(view.rows / 2, view.cols / 2)[1] != firstLuma);
}

TEST_CASE("VideoReader read returns an independent copy", "[reader][zerocopy]") {
    const std::string path = fixtures + "/seek_numbered.mp4";

    framewright::VideoReader reader;
    REQUIRE(reader.open(path));

    cv::Mat first;
    REQUIRE(reader.read(first));
    const int firstLuma = first.at<cv::Vec3b>(first.rows / 2, first.cols / 2)[1];

    cv::Mat second;
    REQUIRE(reader.read(second));

    // read() clones, so the first frame must survive the second decode.
    CHECK(first.data != second.data);
    CHECK(first.at<cv::Vec3b>(first.rows / 2, first.cols / 2)[1] == firstLuma);
}

TEST_CASE("VideoReader reports unknown frame count after close", "[reader]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/seek_numbered.mp4"));
    CHECK(reader.getFrameCount() > 0);

    reader.close();

    // -1 is "unknown", as documented on getFrameCount(). 0 would read as a
    // real count of zero frames.
    CHECK(reader.getFrameCount() == -1);
}

#endif // HAVE_SEEK_FIXTURE

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
