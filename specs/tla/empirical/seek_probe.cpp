// Empirical check of the seek() position-tracking contract that
// specs/tla/VideoReaderSeek.tla claims is violated.
#include "framewright/VideoReader.h"
#include <cstdio>
#include <vector>

static int lumaOf(const cv::Mat& m) { return m.at<cv::Vec3b>(32, 32)[1]; }

// Which real frame index does this Mat correspond to?
static int identify(const cv::Mat& m, const std::vector<int>& ref) {
    int v = lumaOf(m), best = -1, bestd = 1 << 30;
    for (size_t i = 0; i < ref.size(); i++) {
        int d = std::abs(ref[i] - v);
        if (d < bestd) { bestd = d; best = (int)i; }
    }
    return bestd <= 1 ? best : -1;
}

int main(int argc, char** argv) {
    const char* path = argv[1];
    framewright::VideoReader r;
    cv::Mat f;

    // Ground truth: sequential decode.
    std::vector<int> ref;
    if (!r.open(path)) { printf("open failed\n"); return 1; }
    while (r.read(f)) ref.push_back(lumaOf(f));
    r.close();
    printf("sequential decode: %zu frames\n\n", ref.size());

    // from, to. Keyframes are at 0 and 30.
    int cases[][2] = {
        {1, 0},    {50, 0},   {50, 5},   {50, 29},  {50, 30},  {50, 35},  {50, 45},
        {40, 35},  {1, 5},    {1, 29},   {1, 45},   {35, 31},
    };
    printf("%-6s %-8s %-8s %-10s %-14s %s\n", "from", "seek(n)", "branch", "returned",
           "reports frame", "next read() is frame");
    for (auto& c : cases) {
        int from = c[0], n = c[1];
        framewright::VideoReader v;
        v.open(path);
        for (int i = 0; i < from; i++) v.read(f);  // walk to `from`
        int64_t before = v.getCurrentFrameNumber();
        const char* branch = (n < before || n - before > 50) ? "keyframe" : "scan";
        bool ok = v.seek(n);
        int reported = (int)v.getCurrentFrameNumber();
        int actual = v.read(f) ? identify(f, ref) : -1;
        printf("%-6d %-8d %-8s %-10s %-14d %-4d  %s\n", from, n, branch, ok ? "true" : "false",
               reported, actual, (ok && actual != reported) ? "<-- MISMATCH" : "");
    }
    return 0;
}
