// Standalone tests for the W3 club-corridor activity helper
// (src/Analysis/ball_activity.h): the annulus + per-pixel temporal-median math
// BallRunner uses to score "is the club moving near the ball". No video fixture —
// synthetic CV_8U crops with hand-computable expectations.
//
//   cmake --build build/analyzer-tests --target ball_activity_test
//   ctest --test-dir build/analyzer-tests -R ball_activity --output-on-failure

#include "../ball_activity.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

// A uniform WxH CV_8U crop of one value.
static cv::Mat uniform(int w, int h, int v) { return cv::Mat(h, w, CV_8U, cv::Scalar(v)); }

int main()
{
    constexpr int W = 40, H = 40;
    const double cx = 20.0, cy = 20.0, inner = 6.0, outer = 18.0;

    // ── (1) quiet frame: crop == every ring crop ⇒ activity 0 ─────────────────
    std::printf("=== quiet frame ⇒ 0 ===\n");
    {
        std::vector<cv::Mat> ring(9, uniform(W, H, 100));
        const cv::Mat crop = uniform(W, H, 100);
        const float a = clubCorridorActivity(crop, ring, cx, cy, inner, outer, /*sigma=*/10.0);
        check(near(a, 0.0, 1e-6), "identical crop/ring ⇒ activity 0");
    }

    // ── (2) active annulus: uniform diff over the annulus ⇒ mean|diff|/sigma ──
    std::printf("=== active annulus ⇒ mean|diff|/sigma ===\n");
    {
        std::vector<cv::Mat> ring(9, uniform(W, H, 100));
        // crop = 200 everywhere; annulus pixels differ by 100 from the ring median.
        const cv::Mat crop = uniform(W, H, 200);
        const float a = clubCorridorActivity(crop, ring, cx, cy, inner, outer, /*sigma=*/10.0);
        check(near(a, 10.0, 1e-6), "uniform |diff|=100 over the annulus / sigma 10 ⇒ 10");
    }

    // ── (3) inner-disc exclusion: a change INSIDE the inner radius is ignored ──
    std::printf("=== inner disc excluded ===\n");
    {
        std::vector<cv::Mat> ring(9, uniform(W, H, 100));
        cv::Mat crop = uniform(W, H, 100);                 // annulus == ring (quiet)
        for (int y = 0; y < H; ++y)                          // bright ball disc, r=4 < inner 6
            for (int x = 0; x < W; ++x)
                if ((x - 20) * (x - 20) + (y - 20) * (y - 20) <= 16) crop.at<uchar>(y, x) = 255;
        const float a = clubCorridorActivity(crop, ring, cx, cy, inner, outer, /*sigma=*/10.0);
        check(near(a, 0.0, 1e-6), "a bright ball disc inside the inner radius ⇒ still 0");
    }

    // ── (4) median-ref robustness: one outlier crop must not move the median ──
    std::printf("=== median reference robustness ===\n");
    {
        std::vector<cv::Mat> ring(8, uniform(W, H, 0));
        ring.push_back(uniform(W, H, 255));               // 8×0 + 1×255 ⇒ per-pixel median 0
        const cv::Mat crop = uniform(W, H, 0);            // == median ⇒ quiet despite the outlier
        const float a = clubCorridorActivity(crop, ring, cx, cy, inner, outer, /*sigma=*/10.0);
        check(near(a, 0.0, 1e-6), "single 255 outlier crop doesn't shift the median-ref");
    }

    // ── (5) absent contracts return -1 ────────────────────────────────────────
    std::printf("=== absent contracts ⇒ -1 ===\n");
    {
        const cv::Mat crop = uniform(W, H, 100);
        std::vector<cv::Mat> ring(9, uniform(W, H, 100));
        check(clubCorridorActivity(crop, {}, cx, cy, inner, outer, 10.0) < 0.f, "empty ring ⇒ -1");
        check(clubCorridorActivity(crop, ring, cx, cy, inner, outer, 0.0) < 0.f, "sigma 0 ⇒ -1");
        check(clubCorridorActivity(cv::Mat(), ring, cx, cy, inner, outer, 10.0) < 0.f, "empty crop ⇒ -1");
        check(clubCorridorActivity(crop, ring, cx, cy, /*inner=*/18.0, /*outer=*/6.0, 10.0) < 0.f,
              "outer <= inner ⇒ -1");
        std::vector<cv::Mat> mixed(9, uniform(W, H, 100));
        mixed[3] = uniform(W + 1, H, 100);                // size mismatch
        check(clubCorridorActivity(crop, mixed, cx, cy, inner, outer, 10.0) < 0.f,
              "ring crop of a different size ⇒ -1");
        check(clubCorridorActivity(crop, ring, /*cx=*/500.0, /*cy=*/500.0, inner, outer, 10.0) < 0.f,
              "annulus entirely out of bounds ⇒ -1");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
