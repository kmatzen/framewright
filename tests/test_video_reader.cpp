#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <framewright/VideoReader.h>

#include <cmath>
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

#ifdef HAVE_HDR_PIXEL_FIXTURES

// Reference implementation of the HDR-to-SDR pipeline, written from the spec
// formulas in double precision, independent of the float/LUT implementation
// in VideoReader. The tests feed it the code values actually decoded by
// read16() so it validates the transform math in isolation -- fixture
// generation error cancels out.
namespace hdr_reference {

double pqEotf(double e) {
    const double m1 = 2610.0 / 16384.0;
    const double m2 = 2523.0 / 4096.0 * 128.0;
    const double c1 = 3424.0 / 4096.0;
    const double c2 = 2413.0 / 4096.0 * 32.0;
    const double c3 = 2392.0 / 4096.0 * 32.0;
    double p = std::pow(std::max(e, 0.0), 1.0 / m2);
    double num = std::max(p - c1, 0.0);
    double den = c2 - c3 * p;
    if (den <= 0.0) return 10000.0;
    return 10000.0 * std::pow(num / den, 1.0 / m1);
}

double hlgInverseOetf(double e) {
    const double a = 0.17883277, b = 0.28466892, c = 0.55991073;
    e = std::max(e, 0.0);
    if (e <= 0.5) return e * e / 3.0;
    return (std::exp((e - c) / a) + b) / 12.0;
}

// 16-bit BGR code values -> expected tone-mapped 8-bit BGR.
cv::Vec3b toneMap(const cv::Vec3w& bgr16, bool hlg) {
    double lin[3]; // b, g, r
    for (int i = 0; i < 3; i++) {
        double e = bgr16[i] / 65535.0;
        lin[i] = hlg ? 10.0 * std::pow(hlgInverseOetf(e), 1.2) : pqEotf(e) / 100.0;
    }
    double b = lin[0], g = lin[1], r = lin[2];
    double lum = 0.2627 * r + 0.6780 * g + 0.0593 * b;
    if (lum > 0.0) {
        const double peak = 10.0;
        double mapped = lum * (1.0 + lum / (peak * peak)) / (1.0 + lum);
        double s = mapped / lum;
        r *= s; g *= s; b *= s;
    }
    double r709 =  1.6605 * r - 0.5876 * g - 0.0728 * b;
    double g709 = -0.1246 * r + 1.1329 * g - 0.0083 * b;
    double b709 = -0.0182 * r - 0.1006 * g + 1.1187 * b;
    auto enc = [](double x) -> uint8_t {
        x = std::min(std::max(x, 0.0), 1.0);
        double e = x < 0.018 ? 4.5 * x : 1.099 * std::pow(x, 0.45) - 0.099;
        return static_cast<uint8_t>(std::lround(std::min(std::max(e, 0.0), 1.0) * 255.0));
    };
    return {enc(b709), enc(g709), enc(r709)};
}

} // namespace hdr_reference

// The matrix fixture is solid RGB (208, 64, 32) encoded to BT.2020 YUV by an
// independent converter. If the reader applied BT.709 or BT.601 coefficients
// instead of BT.2020, the decode would land ~7 counts off on R and G, well
// outside the +/-3 tolerance.
TEST_CASE("VideoReader read16 recovers HDR10 code values through the BT.2020 matrix",
          "[reader][hdr][color]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/hdr10_matrix.mp4"));
    REQUIRE(reader.getColorSpace() == AVCOL_SPC_BT2020_NCL);

    cv::Mat frame;
    REQUIRE(reader.read16(frame));
    REQUIRE(frame.type() == CV_16UC3);
    REQUIRE(frame.cols == 192);
    REQUIRE(frame.rows == 108);

    cv::Vec3w px = frame.at<cv::Vec3w>(frame.rows / 2, frame.cols / 2);
    const int tol16 = 3 * 257;
    CHECK(std::abs(px[0] - 32 * 257) <= tol16);  // B
    CHECK(std::abs(px[1] - 64 * 257) <= tol16);  // G
    CHECK(std::abs(px[2] - 208 * 257) <= tol16); // R
}

TEST_CASE("VideoReader 8-bit passthrough uses the BT.2020 matrix for HDR10",
          "[reader][hdr][color]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/hdr10_matrix.mp4"));
    CHECK_FALSE(reader.isToneMappingActive());

    cv::Mat frame;
    REQUIRE(reader.read(frame));
    REQUIRE(frame.type() == CV_8UC3);

    cv::Vec3b px = frame.at<cv::Vec3b>(frame.rows / 2, frame.cols / 2);
    CHECK(std::abs(px[0] - 32) <= 3);
    CHECK(std::abs(px[1] - 64) <= 3);
    CHECK(std::abs(px[2] - 208) <= 3);
}

static void checkToneMappedAgainstReference(const std::string& file, bool hlg) {
    // First pass: raw code values.
    cv::Vec3w code16;
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(file));
        cv::Mat raw;
        REQUIRE(reader.read16(raw));
        code16 = raw.at<cv::Vec3w>(raw.rows / 2, raw.cols / 2);
    }

    // The fixture encodes RGB (140, 150, 110); the raw values must be intact
    // (this also pins the fixture itself, so the reference below is fed real
    // HDR code values rather than garbage).
    CHECK(std::abs(code16[0] - 110 * 257) <= 3 * 257);
    CHECK(std::abs(code16[1] - 150 * 257) <= 3 * 257);
    CHECK(std::abs(code16[2] - 140 * 257) <= 3 * 257);

    // Second pass: tone-mapped read must match the independent double-precision
    // reference applied to the very code values decoded above.
    framewright::VideoReaderOptions opts;
    opts.hdr_mode = framewright::HdrMode::ToneMapSDR;
    framewright::VideoReader reader;
    REQUIRE(reader.open(file, opts));
    CHECK(reader.isToneMappingActive());

    cv::Mat frame;
    REQUIRE(reader.read(frame));
    REQUIRE(frame.type() == CV_8UC3);
    cv::Vec3b actual = frame.at<cv::Vec3b>(frame.rows / 2, frame.cols / 2);

    cv::Vec3b expected = hdr_reference::toneMap(code16, hlg);

    // Chosen so the expected output sits well inside (0, 255) on every
    // channel; a pipeline that skipped linearization, tone mapping, the gamut
    // matrix, or the output gamma lands far outside +/-3.
    for (int i = 0; i < 3; i++) {
        CHECK(static_cast<int>(expected[i]) > 20);
        CHECK(static_cast<int>(expected[i]) < 235);
        CHECK(std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])) <= 3);
    }
}

TEST_CASE("VideoReader tone maps PQ to SDR matching the reference math",
          "[reader][hdr][color]") {
    checkToneMappedAgainstReference(fixtures + "/hdr10_tonemap.mp4", /*hlg=*/false);
}

TEST_CASE("VideoReader tone maps HLG to SDR matching the reference math",
          "[reader][hdr][color]") {
    checkToneMappedAgainstReference(fixtures + "/hlg_tonemap.mp4", /*hlg=*/true);
}

TEST_CASE("VideoReader ToneMapSDR leaves SDR sources untouched", "[reader][hdr]") {
    cv::Mat plain, mapped;
    {
        framewright::VideoReader reader;
        REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));
        REQUIRE(reader.read(plain));
    }
    {
        framewright::VideoReaderOptions opts;
        opts.hdr_mode = framewright::HdrMode::ToneMapSDR;
        framewright::VideoReader reader;
        REQUIRE(reader.open(fixtures + "/bt709_limited.mp4", opts));
        CHECK_FALSE(reader.isToneMappingActive());
        REQUIRE(reader.read(mapped));
    }
    CHECK(cv::norm(plain, mapped, cv::NORM_INF) == 0.0);
}

TEST_CASE("VideoReader read16 works on 8-bit SDR sources", "[reader][hdr]") {
    framewright::VideoReader reader;
    REQUIRE(reader.open(fixtures + "/bt709_limited.mp4"));

    cv::Mat frame16, frame8;
    REQUIRE(reader.read16(frame16));
    REQUIRE(frame16.type() == CV_16UC3);

    // read() and read16() must agree on the same content to within scaling.
    framewright::VideoReader reader8;
    REQUIRE(reader8.open(fixtures + "/bt709_limited.mp4"));
    REQUIRE(reader8.read(frame8));

    cv::Vec3w p16 = frame16.at<cv::Vec3w>(frame16.rows / 2, frame16.cols / 2);
    cv::Vec3b p8 = frame8.at<cv::Vec3b>(frame8.rows / 2, frame8.cols / 2);
    for (int i = 0; i < 3; i++) {
        CHECK(std::abs(p16[i] / 257 - p8[i]) <= 2);
    }
}
#endif
