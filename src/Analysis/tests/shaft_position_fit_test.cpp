// Standalone tests for Layer B "B-fit" — the milestone fitter
// (src/Analysis/shaft_position_fit.h): ±k-frame shift-and-stack registration +
// joint (grip, θ, L) fit against the shared polarity-aware ridge integral, arm
// sector, and ball evidence. Synthetic OpenCV renders with hand-placed geometry.
//
//   cmake --build build/tests-Analysis --target shaft_position_fit_test
//   ctest --test-dir build/tests-Analysis -R shaft_position_fit --output-on-failure
//
// Archetypes (design §2 Layer B): (a) still P1 stack; (b) blur-gap P7 with a
// deliberately 180°-FLIPPED track init recovered via the wide sector + ball;
// (c) pure-noise reject; (d) determinism.

#include "../shaft_position_fit.h"

#include <opencv2/imgproc.hpp>   // cv::line for the synthetic shafts

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
static constexpr double kPi = 3.14159265358979323846;

// Wrap a degree delta to (−180,180].
static double wrapDeg(double d) { return std::fmod(std::fmod(d + 180.0, 360.0) + 360.0, 360.0) - 180.0; }

// Render a bright shaft (fg) on a dark background (bg) from (gx,gy) at `thetaDeg`,
// length L. A motion-blur smear is a FAN of `nSub` sub-lines over ±blurDeg/2 about
// θ, ADDITIVELY blended with a cos² weight peaked at the true instantaneous angle
// (a graded point-spread — the shaft's mid-exposure position is the brightest
// ridge, edges fade), so the smear has a findable centre rather than a flat wedge
// whose only ridges are its edges. nSub=1 ⇒ a crisp still line. Optional Gaussian
// noise (seeded per frame) so one frame is ambiguous but the ±k stack is clean.
static cv::Mat renderShaft(int W, int H, double gx, double gy, double thetaDeg, double L,
                           double blurDeg, int nSub, unsigned seed, double noiseSigma,
                           int bg = 30, int fg = 220)
{
    cv::Mat acc(H, W, CV_32F, cv::Scalar(0.0));
    const int sub = std::max(1, nSub);
    for (int j = 0; j < sub; ++j) {
        const double t  = (sub == 1) ? 0.0 : (double(j) / double(sub - 1) - 0.5);   // −0.5..0.5
        const double w  = (sub == 1) ? 1.0 : std::pow(std::cos(t * kPi), 2.0);      // peak 1 at centre
        const double th = (thetaDeg + t * blurDeg) * kPi / 180.0;
        const cv::Point a(int(std::lround(gx)), int(std::lround(gy)));
        const cv::Point b(int(std::lround(gx + L * std::cos(th))), int(std::lround(gy + L * std::sin(th))));
        cv::Mat mask(H, W, CV_8UC1, cv::Scalar(0));
        cv::line(mask, a, b, cv::Scalar(255), 3);
        cv::Mat mf; mask.convertTo(mf, CV_32F, w * double(fg - bg) / 255.0);
        acc = cv::max(acc, mf);   // graded overwrite (brightest sub-line wins per pixel)
    }
    cv::Mat img(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const float *arow = acc.ptr<float>(y);
        uchar *row = img.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) row[x] = cv::saturate_cast<uchar>(double(bg) + arow[x]);
    }
    if (noiseSigma > 0.0) {
        std::mt19937 rng(seed);
        std::normal_distribution<double> nd(0.0, noiseSigma);
        for (int y = 0; y < H; ++y) {
            uchar *row = img.ptr<uchar>(y);
            for (int x = 0; x < W; ++x)
                row[x] = cv::saturate_cast<uchar>(int(row[x]) + int(std::lround(nd(rng))));
        }
    }
    return img;
}

// A flat gray field + noise — no shaft (the reject fixture).
static cv::Mat renderNoise(int W, int H, unsigned seed, double sigma, int gray = 128)
{
    cv::Mat img(H, W, CV_8UC1, cv::Scalar(gray));
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, sigma);
    for (int y = 0; y < H; ++y) {
        uchar *row = img.ptr<uchar>(y);
        for (int x = 0; x < W; ++x)
            row[x] = cv::saturate_cast<uchar>(int(row[x]) + int(std::lround(nd(rng))));
    }
    return img;
}

int main()
{
    const RidgeConfig       rc;    // defaults (rLo 8, rStep 2, bgHi 200, eClip 30/90)
    const PositionFitConfig cfg;   // defaults
    const double armVetoDeg = 12.0;

    // ── (a) still P1: 9 noisy frames stack, fit recovers θ (≤1°) and L (≤3%) ────
    std::printf("=== still P1 (stack) ===\n");
    {
        const int W = 400, H = 500, nf = 9, center = 4;
        const double gx = 200.0, gy = 160.0, thTrue = 70.0, LTrue = 220.0;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gxA(nf, gx), gyA(nf, gy), thA(nf, thTrue), om(nf, 0.0);
        for (int f = 0; f < nf; ++f) tUs[f] = int64_t(f) * 5000;
        const std::vector<double> phiEmpty;

        std::function<cv::Mat(int)> frameAt = [&](int f) -> cv::Mat {
            if (f < 0 || f >= nf) return cv::Mat();
            return renderShaft(W, H, gx, gy, thTrue, LTrue, /*blur*/0.0, /*nSub*/1,
                               /*seed*/unsigned(1000 + f), /*noise*/20.0);
        };

        const PositionFitResult r = fitPosition(frameAt, /*p*/1, center, tUs[center], tUs,
                                                gxA, gyA, thA, om, phiEmpty, /*armFloor*/0.0,
                                                /*fused*/0.0, /*ball*/nullptr, W, H, rc, cfg, armVetoDeg);
        check(r.accepted, "still P1 fit accepted");
        check(r.stackN == nf, "all 9 frames entered the stack");
        const double recDeg = r.thetaRad * 180.0 / kPi;
        check(near(recDeg, thTrue, 1.0), "θ recovered within 1°");
        check(std::abs(r.lenPx - LTrue) / LTrue <= 0.03, "L recovered within 3%");
        check(r.conf >= float(cfg.minFitConf), "conf ≥ fit floor");
        check(r.sigmaThetaDeg > 0.f && r.sigmaLenPx > 0.f, "honest σθ / σL reported");
    }

    // ── (b) blur-gap P7: flipped track init recovered by the wide sector + ball ─
    std::printf("=== blur-gap P7 (flipped init, wide + ball) ===\n");
    {
        const int W = 400, H = 500, nf = 9, center = 4;
        const double gx = 200.0, gy = 150.0, thTrue = 95.0, LTrue = 250.0;
        const double omega = 2000.0;   // deg/s at impact (≈10°/frame @ 200 Hz)
        std::vector<int64_t> tUs(nf);
        std::vector<double> gxA(nf, gx), gyA(nf, gy), thA(nf, thTrue + 180.0), om(nf, omega);
        for (int f = 0; f < nf; ++f) tUs[f] = int64_t(f) * 5000;
        const std::vector<double> phiEmpty;   // no arm witness ⇒ ball anchors the wide centre

        std::function<cv::Mat(int)> frameAt = [&](int f) -> cv::Mat {
            if (f < 0 || f >= nf) return cv::Mat();
            const double thF = thTrue + omega * double(tUs[f] - tUs[center]) * 1e-6;   // per-frame rotation
            return renderShaft(W, H, gx, gy, thF, LTrue, /*blur*/25.0, /*nSub*/13,
                               /*seed*/unsigned(2000 + f), /*noise*/15.0);
        };

        // Ball at the true impact head — stationary on the tee (pre-launch).
        BallSample2D ball;
        ball.found      = true;
        ball.t_us       = tUs[center];
        const double hx = gx + LTrue * std::cos(thTrue * kPi / 180.0);
        const double hy = gy + LTrue * std::sin(thTrue * kPi / 180.0);
        ball.center     = QPointF(hx / double(W), hy / double(H));
        ball.radiusNorm = 0.02f;

        const PositionFitResult r = fitPosition(frameAt, /*p*/7, center, tUs[center], tUs,
                                                gxA, gyA, thA, om, phiEmpty, /*armFloor*/0.0,
                                                /*fused*/0.0, &ball, W, H, rc, cfg, armVetoDeg);
        check(r.accepted, "blur-gap P7 fit accepted");
        const double recDeg = r.thetaRad * 180.0 / kPi;
        check(std::abs(wrapDeg(recDeg - thTrue)) <= 5.0, "true θ recovered within 5° (NOT the flip)");
        check(std::abs(wrapDeg(recDeg - (thTrue + 180.0))) > 90.0, "recovered θ is not the 180° flip");
        check(r.conf >= float(cfg.minFitConf), "conf ≥ fit floor");
    }

    // ── (c) reject: pure-noise frames ⇒ fit rejects (caller keeps B1) ───────────
    std::printf("=== reject (pure noise) ===\n");
    {
        const int W = 400, H = 500, nf = 9, center = 4;
        const double gx = 200.0, gy = 200.0;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gxA(nf, gx), gyA(nf, gy), thA(nf, 45.0), om(nf, 0.0);
        for (int f = 0; f < nf; ++f) tUs[f] = int64_t(f) * 5000;
        const std::vector<double> phiEmpty;

        std::function<cv::Mat(int)> frameAt = [&](int f) -> cv::Mat {
            if (f < 0 || f >= nf) return cv::Mat();
            return renderNoise(W, H, unsigned(3000 + f), /*sigma*/8.0);
        };

        const PositionFitResult r = fitPosition(frameAt, /*p*/4, center, tUs[center], tUs,
                                                gxA, gyA, thA, om, phiEmpty, 0.0, 0.0,
                                                nullptr, W, H, rc, cfg, armVetoDeg);
        check(!r.accepted, "pure-noise fit rejects (support < floor)");
    }

    // ── (d) determinism: same inputs ⇒ byte-identical fit ──────────────────────
    std::printf("=== determinism ===\n");
    {
        const int W = 400, H = 500, nf = 9, center = 4;
        const double gx = 210.0, gy = 150.0, thTrue = 60.0, LTrue = 200.0;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gxA(nf, gx), gyA(nf, gy), thA(nf, thTrue), om(nf, 0.0);
        for (int f = 0; f < nf; ++f) tUs[f] = int64_t(f) * 5000;
        const std::vector<double> phiEmpty;
        std::function<cv::Mat(int)> frameAt = [&](int f) -> cv::Mat {
            if (f < 0 || f >= nf) return cv::Mat();
            return renderShaft(W, H, gx, gy, thTrue, LTrue, 0.0, 1, unsigned(4000 + f), 18.0);
        };
        const PositionFitResult a = fitPosition(frameAt, 1, center, tUs[center], tUs, gxA, gyA, thA, om,
                                                phiEmpty, 0.0, 0.0, nullptr, W, H, rc, cfg, armVetoDeg);
        const PositionFitResult b = fitPosition(frameAt, 1, center, tUs[center], tUs, gxA, gyA, thA, om,
                                                phiEmpty, 0.0, 0.0, nullptr, W, H, rc, cfg, armVetoDeg);
        check(a.accepted && b.accepted, "both runs accepted");
        check(a.thetaRad == b.thetaRad && a.lenPx == b.lenPx
              && a.gripPx == b.gripPx && a.conf == b.conf, "identical inputs ⇒ identical fit");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
