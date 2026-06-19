// Standalone test for the anchored radial shaft detection math core
// (src/Analysis/shaft_tracker_math.{h,cpp} — ShaftTracker stage S1).
// Self-contained (own main() + CHECK macros, no googletest); renders synthetic
// scenes: tapered anti-aliased shaft lines over noisy flat/gradient
// backgrounds, bright (steel) and dark (graphite), motion-blur wedge fans,
// alignment-stick / shadow clutter, forearm lines, and anchor error.
// Tolerances from design addendum B.9. Run via CTest
// (src/Analysis/tests/CMakeLists.txt):
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure

#include "../shaft_tracker_math.h"

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                                              \
    do {                                                                                \
        const bool ok = (cond);                                                         \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                        \
        if (!ok) ++g_fail;                                                              \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                               \
    do {                                                                                \
        const double g = (got), w = (want);                                             \
        const bool ok = std::abs(g - w) <= (tol);                                       \
        std::printf("  [%s] %-38s got %9.3f  want %9.3f (tol %.3f)\n",                  \
                    ok ? "PASS" : "FAIL", label, g, w, double(tol));                    \
        if (!ok) ++g_fail;                                                              \
    } while (0)

static constexpr double kPi = 3.14159265358979323846;
static double deg2rad(double d) { return d * kPi / 180.0; }

// Signed angular difference a-b wrapped to (-180, 180], in degrees.
static double angDiffDeg(double aRad, double bRad)
{
    double d = std::fmod((aRad - bRad) * 180.0 / kPi + 180.0, 360.0);
    if (d < 0) d += 360.0;
    return d - 180.0;
}

// ---------------------------------------------------------------- renderer --

static cv::Mat makeBackground(bool gradient, int w = 640, int h = 640)
{
    cv::Mat bg(h, w, CV_8UC1, cv::Scalar(110));
    if (gradient)
        for (int y = 0; y < h; ++y)
            bg.row(y).setTo(cv::Scalar(60 + (100 * y) / h));
    return bg;
}

static void addNoise(cv::Mat &img, double sigma, uint64_t seed)
{
    cv::Mat noise(img.size(), CV_16SC1);
    cv::RNG rng(seed);
    rng.fill(noise, cv::RNG::NORMAL, 0, sigma);
    cv::Mat tmp;
    img.convertTo(tmp, CV_16S);
    tmp += noise;
    tmp.convertTo(img, CV_8U);   // saturating
}

// Tapered anti-aliased line from g along thetaRad: segments of decreasing
// thickness (6→2 px for a shaft, thinner for shadow-like clutter). Intensity
// = local background ± delta, sampled at each segment midpoint pre-noise.
// Endpoints are passed in 1/16-px fixed point (cv::line's shift parameter) so
// the rendered truth is sub-pixel accurate — integer endpoints would tilt the
// drawn line by up to ~0.2° and corrupt the tolerance checks.
static void drawTaperedLine(cv::Mat &img, cv::Point2f g, double thetaRad, double lenPx,
                            int delta, const int *thick = nullptr)
{
    static const int kShaft[4] = {6, 4, 3, 2};
    const int *th = thick ? thick : kShaft;
    const cv::Point2f dir(static_cast<float>(std::cos(thetaRad)),
                          static_cast<float>(std::sin(thetaRad)));
    const auto fx = [](const cv::Point2f &p) {
        return cv::Point(static_cast<int>(std::lround(p.x * 16.f)),
                         static_cast<int>(std::lround(p.y * 16.f)));
    };
    for (int s = 0; s < 4; ++s) {
        const cv::Point2f p0 = g + dir * static_cast<float>(lenPx * s / 4.0);
        const cv::Point2f p1 = g + dir * static_cast<float>(lenPx * (s + 1) / 4.0);
        const cv::Point2f mid = (p0 + p1) * 0.5f;
        const int mx = std::min(img.cols - 1, std::max(0, static_cast<int>(mid.x)));
        const int my = std::min(img.rows - 1, std::max(0, static_cast<int>(mid.y)));
        const int val = std::min(255, std::max(0, img.at<uchar>(my, mx) + delta));
        cv::line(img, fx(p0), fx(p1), cv::Scalar(val), th[s], cv::LINE_AA, 4);
    }
}

// Motion-blur fan: average renders of the line at N sub-angles across spanDeg.
static cv::Mat renderWedge(cv::Point2f g, double centerRad, double spanDeg,
                           double lenPx, int delta, int nSub, double noiseSigma)
{
    const cv::Mat bg = makeBackground(false);
    cv::Mat acc(bg.size(), CV_32FC1, cv::Scalar(0));
    for (int i = 0; i < nSub; ++i) {
        cv::Mat frame = bg.clone();
        const double off = -spanDeg / 2.0 + spanDeg * i / (nSub - 1);
        drawTaperedLine(frame, g, centerRad + deg2rad(off), lenPx, delta);
        cv::Mat f32;
        frame.convertTo(f32, CV_32F);
        acc += f32;
    }
    acc /= static_cast<float>(nSub);
    cv::Mat out;
    acc.convertTo(out, CV_8U);
    addNoise(out, noiseSigma, 99);
    return out;
}

// ------------------------------------------------------------------- tests --

static const cv::Point2f kGrip(320.f, 200.f);
static const double      kThetaDeg = 65.0;
static const double      kLenPx    = 320.0;

static void testCleanBright()
{
    std::printf("=== Clean slow frame — bright (steel), flat noise bg ===\n");
    cv::Mat img = makeBackground(false);
    drawTaperedLine(img, kGrip, deg2rad(kThetaDeg), kLenPx, +80);
    addNoise(img, 6.0, 7);

    ShaftDetectConfig cfg;
    AnchorPrior prior;
    prior.gripPx = kGrip;
    const auto cands = detectShaft(img, cfg, prior);

    CHECK("has candidates", !cands.empty());
    if (cands.empty()) return;
    const ShaftCandidate &c = cands.front();
    CHECK_NEAR("theta error (deg)", angDiffDeg(c.thetaRad, deg2rad(kThetaDeg)), 0.0, 0.5);
    const double expLen = kLenPx - cfg.rhoMinPx;
    CHECK_NEAR("visibleLenPx", c.visibleLenPx, expLen, 0.05 * expLen);
    CHECK("polarity = bright (top-hat)", !c.darkPolarity);
    CHECK("not a wedge", !c.wedge);
    const cv::Point2f tip(kGrip.x + static_cast<float>(kLenPx * std::cos(deg2rad(kThetaDeg))),
                          kGrip.y + static_cast<float>(kLenPx * std::sin(deg2rad(kThetaDeg))));
    const double headErr = cv::norm(c.headPx - tip);
    std::printf("  (info) head seed error: %.1f px, score %.2f\n", headErr, c.score);
    CHECK("head seed near shaft tip (<25 px)", headErr < 25.0);
}

static void testCleanDarkGradient()
{
    std::printf("=== Clean slow frame — dark (graphite), gradient bg ===\n");
    const double theta = deg2rad(115.0);
    cv::Mat img = makeBackground(true);
    drawTaperedLine(img, kGrip, theta, kLenPx, -70);
    addNoise(img, 6.0, 11);

    ShaftDetectConfig cfg;
    AnchorPrior prior;
    prior.gripPx = kGrip;
    const auto cands = detectShaft(img, cfg, prior);

    CHECK("has candidates", !cands.empty());
    if (cands.empty()) return;
    const ShaftCandidate &c = cands.front();
    CHECK_NEAR("theta error (deg)", angDiffDeg(c.thetaRad, theta), 0.0, 0.5);
    const double expLen = kLenPx - cfg.rhoMinPx;
    CHECK_NEAR("visibleLenPx", c.visibleLenPx, expLen, 0.05 * expLen);
    CHECK("polarity = dark (black-hat)", c.darkPolarity);
    CHECK("not a wedge", !c.wedge);
}

static void testWedge()
{
    std::printf("=== Motion-blur wedge — 8 deg fan ===\n");
    const double fanSpanDeg = 8.0;
    cv::Mat img = renderWedge(kGrip, deg2rad(kThetaDeg), fanSpanDeg, kLenPx, +90,
                              /*nSub=*/17, /*noiseSigma=*/5.0);

    ShaftDetectConfig cfg;
    AnchorPrior prior;
    prior.gripPx = kGrip;
    const auto cands = detectShaft(img, cfg, prior);

    CHECK("has candidates", !cands.empty());
    if (cands.empty()) return;
    const ShaftCandidate &c = cands.front();
    CHECK("flagged as wedge", c.wedge);
    CHECK_NEAR("centroid theta error (deg)", angDiffDeg(c.thetaRad, deg2rad(kThetaDeg)),
               0.0, 2.0);
    const double halfSpanRad = deg2rad(fanSpanDeg / 2.0);
    std::printf("  (info) sigmaTheta %.2f deg (fan half-span %.2f deg), visLen %.0f px\n",
                c.sigmaThetaRad * 180.0 / kPi, fanSpanDeg / 2.0, double(c.visibleLenPx));
    CHECK_NEAR("sigmaTheta ~ fan half-span (rad)", c.sigmaThetaRad, halfSpanRad,
               0.4 * halfSpanRad);
}

static void testClutterStickAndShadow()
{
    std::printf("=== Clutter — alignment stick (non-radial) + radial shadow ===\n");
    cv::Mat img = makeBackground(false);
    drawTaperedLine(img, kGrip, deg2rad(kThetaDeg), kLenPx, +80);
    // Alignment stick: strong straight line NOT through the grip (ground,
    // ~360 px below the hands — inside the search disc).
    cv::line(img, cv::Point(20, 560), cv::Point(620, 580), cv::Scalar(195), 5, cv::LINE_AA);
    // Shadow-like second radial line: through the grip, +20 deg, dim + thin.
    static const int kThin[4] = {3, 2, 2, 1};
    drawTaperedLine(img, kGrip, deg2rad(kThetaDeg + 20.0), kLenPx * 0.9, +35, kThin);
    addNoise(img, 6.0, 13);

    ShaftDetectConfig cfg;
    AnchorPrior prior;
    prior.gripPx = kGrip;
    const auto cands = detectShaft(img, cfg, prior);

    CHECK("has candidates", !cands.empty());
    if (cands.empty()) return;
    CHECK_NEAR("true shaft ranks FIRST (theta err deg)",
               angDiffDeg(cands.front().thetaRad, deg2rad(kThetaDeg)), 0.0, 1.0);

    // The stick must not produce a candidate: every reported candidate must be
    // radial through g, i.e. near the shaft or the deliberate shadow line.
    bool stickFound = false, shadowFound = false;
    for (const auto &c : cands) {
        const double dShaft  = std::abs(angDiffDeg(c.thetaRad, deg2rad(kThetaDeg)));
        const double dShadow = std::abs(angDiffDeg(c.thetaRad, deg2rad(kThetaDeg + 20.0)));
        if (dShadow < 2.5 && dShaft > 5.0) shadowFound = true;
        if (dShaft > 5.0 && dShadow > 5.0) stickFound = true;
    }
    CHECK("non-radial stick NOT in candidates", !stickFound);
    std::printf("  (info) shadow line detected as secondary candidate: %s (%zu candidates)\n",
                shadowFound ? "yes" : "no", cands.size());
    if (cands.size() > 1)
        CHECK("shadow scores below shaft", cands[1].score < cands[0].score);
}

static void testAnchorError()
{
    std::printf("=== Anchor error — grip perturbed (+3, -2) px ===\n");
    cv::Mat img = makeBackground(false);
    drawTaperedLine(img, kGrip, deg2rad(kThetaDeg), kLenPx, +80);
    addNoise(img, 6.0, 17);

    ShaftDetectConfig cfg;
    AnchorPrior prior;
    prior.gripPx = kGrip + cv::Point2f(3.f, -2.f);
    const auto cands = detectShaft(img, cfg, prior);

    CHECK("has candidates", !cands.empty());
    if (cands.empty()) return;
    CHECK_NEAR("theta error (deg)", angDiffDeg(cands.front().thetaRad, deg2rad(kThetaDeg)),
               0.0, 1.0);
}

static void testElbowMask()
{
    std::printf("=== Elbow clutter mask — forearm ridge into the anchor ===\n");
    const double elbowDeg = 15.0;   // forearm direction g->elbow (radial!)
    cv::Mat img = makeBackground(false);
    drawTaperedLine(img, kGrip, deg2rad(kThetaDeg), kLenPx, +80);
    // Forearm: bright radial line into the anchor, ~90 px (sleeve contrast).
    static const int kArm[4] = {8, 8, 7, 7};
    drawTaperedLine(img, kGrip, deg2rad(elbowDeg), 90.0, +60, kArm);
    addNoise(img, 6.0, 23);

    ShaftDetectConfig cfg;
    AnchorPrior prior;
    prior.gripPx = kGrip;
    prior.numElbowDirs = 1;
    prior.elbowDirRad[0] = static_cast<float>(deg2rad(elbowDeg));
    const auto cands = detectShaft(img, cfg, prior);

    CHECK("has candidates", !cands.empty());
    if (cands.empty()) return;
    CHECK_NEAR("true shaft ranks first (theta err deg)",
               angDiffDeg(cands.front().thetaRad, deg2rad(kThetaDeg)), 0.0, 1.0);
    bool inMask = false;
    for (const auto &c : cands)
        if (std::abs(angDiffDeg(c.thetaRad, deg2rad(elbowDeg))) <= cfg.clutterMaskDeg)
            inMask = true;
    CHECK("no candidate within the elbow mask", !inMask);
}

static void testTiming()
{
    std::printf("=== Timing — 600x600 ROI, 50 calls ===\n");
    cv::Mat img = makeBackground(false);
    const cv::Point2f grip(320.f, 320.f);   // centred: full 600^2 ROI
    drawTaperedLine(img, grip, deg2rad(kThetaDeg), 240.0, +80);
    addNoise(img, 6.0, 31);

    ShaftDetectConfig cfg;
    cfg.maxRadiusPx = 300.f;                // 600^2 search square
    AnchorPrior prior;
    prior.gripPx = grip;

    // Warm-up (first-touch page faults, OpenCV lazy init).
    (void)detectShaft(img, cfg, prior);

    const int N = 50;
    const auto t0 = std::chrono::steady_clock::now();
    size_t sink = 0;
    for (int i = 0; i < N; ++i)
        sink += detectShaft(img, cfg, prior).size();
    const auto t1 = std::chrono::steady_clock::now();
    const double meanMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count() / N;
    std::printf("  (info) mean detectShaft time: %.3f ms over %d calls (candidates/call: %.1f)\n",
                meanMs, N, double(sink) / N);
    CHECK("mean detect time < 10 ms (CI bound; design target 2 ms)", meanMs < 10.0);
}

// ---- Skeleton-aware enhancement (K1): R5 length gate, R2 kinematic prior ----

static bool candNear(const std::vector<ShaftCandidate> &cands, double thetaDeg, double tolDeg)
{
    for (const ShaftCandidate &c : cands)
        if (angDiffDeg(c.thetaRad, deg2rad(thetaDeg)) < tolDeg)
            return true;
    return false;
}

// R5: a candidate shorter than the arm-relative floor (cfg.minShaftLenPx) is
// dropped at selection (kills the "impossibly small shaft"); a real full-length
// shaft clears the same floor. The floor only ever tightens, so the default
// config (minShaftLenPx == minVisibleLenPx) is unchanged — proven by the other
// tests still passing.
static void testR5LengthFloor()
{
    std::printf("=== R5 length floor — sub-arm ridge rejected, long shaft kept ===\n");
    // A short, strong bright ridge (the glove-seam / logo false positive R5
    // targets): 90 px, well above the default 30 px floor.
    cv::Mat shortImg = makeBackground(false);
    drawTaperedLine(shortImg, kGrip, deg2rad(kThetaDeg), 90.0, +110);
    addNoise(shortImg, 6.0, 11);

    ShaftDetectConfig def;                        // default floor (== minVisibleLenPx)
    AnchorPrior prior; prior.gripPx = kGrip;
    CHECK("short ridge detected at default floor",
          candNear(detectShaft(shortImg, def, prior), kThetaDeg, 4.0));

    ShaftDetectConfig hi = def;
    hi.minShaftLenPx = 200.f;                     // ≈ one arm — longer than the 90 px ridge
    CHECK("short ridge rejected above one-arm floor",
          !candNear(detectShaft(shortImg, hi, prior), kThetaDeg, 4.0));

    // A real, full-length shaft clears the same floor.
    cv::Mat longImg = makeBackground(false);
    drawTaperedLine(longImg, kGrip, deg2rad(kThetaDeg), kLenPx, +80);
    addNoise(longImg, 6.0, 12);
    CHECK("full-length shaft survives one-arm floor",
          candNear(detectShaft(longImg, hi, prior), kThetaDeg, 4.0));
}

// R2: the kinematic-direction bump favours the pointed-at direction and, with
// priorFloor, never zeroes the true bin even when it points the wrong way.
static void testR2KinematicPrior()
{
    std::printf("=== R2 kinematic prior — bump favours true shaft, never hard-gates ===\n");
    const double trueDeg = 65.0, clutterDeg = 115.0;
    cv::Mat img = makeBackground(false);
    drawTaperedLine(img, kGrip, deg2rad(trueDeg),    kLenPx, +70);   // true shaft
    drawTaperedLine(img, kGrip, deg2rad(clutterDeg), kLenPx, +70);   // equal-strength off-axis clutter
    addNoise(img, 6.0, 21);

    AnchorPrior toTrue; toTrue.gripPx = kGrip;
    toTrue.hasKinematicDir   = true;
    toTrue.kinematicDirRad   = deg2rad(trueDeg);
    toTrue.kinematicSigmaRad = static_cast<float>(deg2rad(25.0));
    ShaftDetectConfig cfg;
    const auto cands = detectShaft(img, cfg, toTrue);
    CHECK("candidates found", !cands.empty());
    if (!cands.empty())
        CHECK_NEAR("top candidate pulled to kinematic direction",
                   angDiffDeg(cands.front().thetaRad, deg2rad(trueDeg)), 0.0, 4.0);

    // A prior pointing the wrong way must not erase a real shaft (floor 0.3).
    cv::Mat solo = makeBackground(false);
    drawTaperedLine(solo, kGrip, deg2rad(trueDeg), kLenPx, +70);
    addNoise(solo, 6.0, 22);
    AnchorPrior away; away.gripPx = kGrip;
    away.hasKinematicDir   = true;
    away.kinematicDirRad   = deg2rad(trueDeg + 180.0);
    away.kinematicSigmaRad = static_cast<float>(deg2rad(25.0));
    CHECK("true shaft survives an opposed prior (no hard gate)",
          candNear(detectShaft(solo, cfg, away), trueDeg, 4.0));
}

// R1 (observable effect only): cfg.maxRadiusPx bounds the detection reach — the
// lever the arm-scale ladder sets. The arm-vs-silhouette ladder itself lives in
// the orchestrator (shaft_tracker.cpp) and is validated on real lower-body-cropped
// clips in K5; here we only confirm the radius genuinely caps reach.
static void testR1RadiusBound()
{
    std::printf("=== R1 search-radius bound — maxRadiusPx caps detection reach ===\n");
    cv::Mat img = makeBackground(false);
    drawTaperedLine(img, kGrip, deg2rad(kThetaDeg), kLenPx, +80);    // 320 px shaft
    addNoise(img, 6.0, 31);
    AnchorPrior prior; prior.gripPx = kGrip;

    ShaftDetectConfig full;                        // maxRadiusPx = 400 default
    const auto cf = detectShaft(img, full, prior);
    CHECK("full-radius detects long shaft", !cf.empty());
    const double fullLen = cf.empty() ? 0.0 : cf.front().visibleLenPx;

    ShaftDetectConfig clipped = full;
    clipped.maxRadiusPx = 150.f;                   // a small arm-scaled radius
    const auto cc = detectShaft(img, clipped, prior);
    CHECK("clipped radius still detects the shaft", candNear(cc, kThetaDeg, 4.0));
    const double clipLen = cc.empty() ? 0.0 : cc.front().visibleLenPx;
    std::printf("  (info) visibleLen: full=%.0f px, clipped=%.0f px\n", fullLen, clipLen);
    CHECK("clipped reach bounded by maxRadius", clipLen <= clipped.maxRadiusPx + 10.0);
    CHECK("full reach materially longer than clipped", fullLen > clipLen + 80.0);
}

// R8: a faint motion-blur fan that the anchored run scan misses is recovered by
// the blur-mode integrator inside the kinematic envelope — and blur mode does
// not fabricate a fan on a blank frame (never invent a shaft that is not there).
static void testR8BlurWedge()
{
    std::printf("=== R8 blur mode — faint fan recovered inside the envelope ===\n");
    const double cdeg = 70.0;
    // A motion-blur fan: spread over a wide span so each pixel is only partly on
    // the shaft (semi-transparent), the response broken — the anchored run scan
    // struggles; integrating the partial pixels in the envelope recovers it.
    cv::Mat fan = renderWedge(kGrip, deg2rad(cdeg), 12.0, kLenPx, 60, 9, 5.0);

    ShaftDetectConfig sharp;
    AnchorPrior pp; pp.gripPx = kGrip;
    const auto sc = detectShaft(fan, sharp, pp);
    const bool sharpHit = candNear(sc, cdeg, 8.0);
    std::printf("  (info) sharp path: %d cands, hit=%d\n", int(sc.size()), sharpHit ? 1 : 0);

    ShaftDetectConfig blur;
    blur.blurMode       = true;
    blur.predFanHalfRad = float(deg2rad(5.0));
    AnchorPrior bp; bp.gripPx = kGrip;
    bp.kinematicDirRad   = float(deg2rad(cdeg));
    bp.kinematicSigmaRad = float(deg2rad(20.0));
    const auto bc = detectShaft(fan, blur, bp);
    std::printf("  (info) blur path: %d cands, hit=%d\n", int(bc.size()), candNear(bc, cdeg, 8.0) ? 1 : 0);
    CHECK("blur mode detects the faint fan", candNear(bc, cdeg, 8.0));

    // No fabrication: same blur config over a blank noisy frame must not invent
    // a fan in the envelope.
    cv::Mat blank = makeBackground(false);
    addNoise(blank, 5.0, 71);
    CHECK("blur mode does not fabricate on a blank frame",
          !candNear(detectShaft(blank, blur, bp), cdeg, 12.0));
}

int main()
{
    testCleanBright();
    testCleanDarkGradient();
    testWedge();
    testClutterStickAndShadow();
    testAnchorError();
    testElbowMask();
    testR5LengthFloor();
    testR2KinematicPrior();
    testR1RadiusBound();
    testR8BlurWedge();
    testTiming();

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASS",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
