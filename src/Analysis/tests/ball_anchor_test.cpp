// Standalone tests for the v3.4 ball anchor (src/Analysis/ball_anchor):
// medianGripBallLenPx (address-hold window derivation / order-independent
// two-pass mis-lock cluster gate / min-count abstention / no-stillness
// fallback) and applyBallAnchor's A3 head recompute + no-op contracts.
// Synthetic inputs with hand-computable expectations — no fixture, no decode.
//
//   cmake --build build/analyzer-tests --target ball_anchor_test
//   ctest --test-dir build/analyzer-tests -R ball_anchor --output-on-failure

#include "../ball_anchor.h"
#include "../shot_analyzer.h"   // ShotAnalysisJob (applyBallAnchor arg)

#include <QPointF>

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
static constexpr double kPi = 3.14159265358979323846;

// Append one ball frame (found) at normalized centre (nx,ny) with time t.
static void addBall(BallTrack2D &b, int64_t t, double nx, double ny)
{
    BallSample2D s;
    s.t_us = t; s.found = true; s.center = QPointF(nx, ny); s.radiusNorm = 0.02f; s.conf = 1.f;
    b.frames.push_back(s);
}

// Append a found=false ball frame at time t (gap — the detector saw nothing).
static void addBallMiss(BallTrack2D &b, int64_t t)
{
    BallSample2D s;
    s.t_us = t; s.found = false;
    b.frames.push_back(s);
}

int main()
{
    constexpr int W = 1000, H = 1000;

    // ── medianGripBallLenPx: clean median over the address hold ──────────────
    std::printf("=== medianGripBallLenPx: median ===\n");
    {
        const int nf = 8;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf, 300.0), gy(nf, 200.0);   // grip fixed
        std::vector<char> still(nf, 1);                      // whole clip = one hold ending at bs0
        BallTrack2D ball;
        for (int i = 0; i < nf; ++i) { tUs[i] = int64_t(i) * 10000; addBall(ball, tUs[i], 0.30, 0.30); }
        // ball (300,300), grip (300,200) ⇒ |B−G| = 100 every frame
        const double m = medianGripBallLenPx(ball, gx, gy, tUs, W, H, /*bs0=*/nf, /*collar=*/5, &still);
        check(near(m, 100.0, 1e-6), "median grip→ball distance = 100 px");
    }

    // ── medianGripBallLenPx: teeing scenario (window = late hold only) ───────
    std::printf("=== medianGripBallLenPx: teeing vs address hold ===\n");
    {
        // 0..9 teeing/setup: quasi-still, hands AT the ball (d = 20 — the corpus
        // poison); 10..14 moving; 15..24 the address hold proper (d = 300);
        // takeaway at bs0 = 25. The window must be the LAST still run, so the
        // teeing frames never enter the median.
        const int nf = 30, bs0 = 25, collar = 5;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf), gy(nf);
        std::vector<char> still(nf, 0);
        BallTrack2D ball;
        for (int i = 0; i < nf; ++i) {
            tUs[i] = int64_t(i) * 10000;
            addBall(ball, tUs[i], 0.50, 0.80);               // ball fixed (500,800)
            gx[i] = 500.0;
            if (i < 10)      { gy[i] = 780.0; still[i] = 1; }   // teeing: |B−G| = 20
            else if (i < 15) { gy[i] = 650.0; }                 // transition (moving)
            else if (i < 25) { gy[i] = 500.0; still[i] = 1; }   // address hold: |B−G| = 300
            else             { gy[i] = 480.0; }                 // takeaway
        }
        const double m = medianGripBallLenPx(ball, gx, gy, tUs, W, H, bs0, collar, &still);
        check(near(m, 300.0, 1e-6), "length measured from the late hold, not teeing (300, not 20)");
    }

    // ── medianGripBallLenPx: order-independent mis-lock cluster gate ─────────
    std::printf("=== medianGripBallLenPx: warm-up mis-lock ===\n");
    {
        // First two found samples are a detector warm-up lock 100 px off; the
        // remaining ten sit at the true position. A chained first-accepted gate
        // would anchor on the warm-up cluster and reject every true sample; the
        // two-pass median-position gate keeps the majority cluster instead.
        const int nf = 12;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf, 500.0), gy(nf, 500.0);
        std::vector<char> still(nf, 1);
        BallTrack2D ball;
        for (int i = 0; i < nf; ++i) {
            tUs[i] = int64_t(i) * 10000;
            if (i < 2) addBall(ball, tUs[i], 0.40, 0.80);    // warm-up mis-lock (400,800)
            else       addBall(ball, tUs[i], 0.50, 0.80);    // true ball (500,800) ⇒ d = 300
        }
        const double m = medianGripBallLenPx(ball, gx, gy, tUs, W, H, nf, /*collar=*/nf, &still);
        check(near(m, 300.0, 1e-6), "true cluster wins over early warm-up locks (300)");
    }

    // ── medianGripBallLenPx: min accepted-sample count ───────────────────────
    std::printf("=== medianGripBallLenPx: min count ===\n");
    {
        // Only 4 found samples in the hold (< kMinLenSamples = 5) — abstain.
        const int nf = 10;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf, 500.0), gy(nf, 500.0);
        std::vector<char> still(nf, 1);
        BallTrack2D ball;
        for (int i = 0; i < nf; ++i) {
            tUs[i] = int64_t(i) * 10000;
            if (i < 4) addBall(ball, tUs[i], 0.50, 0.80);
            else       addBallMiss(ball, tUs[i]);            // detector gap — not found
        }
        check(medianGripBallLenPx(ball, gx, gy, tUs, W, H, nf, nf, &still) < 0,
              "fewer than 5 accepted samples ⇒ -1");
    }

    // ── medianGripBallLenPx: null-stillness fallback window ──────────────────
    std::printf("=== medianGripBallLenPx: fallback window ===\n");
    {
        // No stillness info ⇒ trailing `collar` frames before bs0. Grip sits at
        // d = 300 only there; earlier frames (d = 20) must stay outside.
        const int nf = 20, bs0 = 15, collar = 6;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf, 500.0), gy(nf);
        BallTrack2D ball;
        for (int i = 0; i < nf; ++i) {
            tUs[i] = int64_t(i) * 10000;
            addBall(ball, tUs[i], 0.50, 0.80);
            gy[i] = (i >= bs0 - collar && i < bs0) ? 500.0 : 780.0;
        }
        const double m = medianGripBallLenPx(ball, gx, gy, tUs, W, H, bs0, collar, nullptr);
        check(near(m, 300.0, 1e-6), "null still ⇒ trailing collar frames before bs0 (300)");
    }

    // ── medianGripBallLenPx: golf-prior plausibility gate ────────────────────
    std::printf("=== medianGripBallLenPx: ankle/feet gate ===\n");
    {
        // Shared rig: whole-clip hold, grip (500,500), ankles at y=900 with
        // x 430/570. Margins on the 1000 px frame: ankle 20 px, feet 100 px.
        const int nf = 10;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf, 500.0), gy(nf, 500.0);
        std::vector<char> still(nf, 1);
        for (int i = 0; i < nf; ++i) tUs[i] = int64_t(i) * 10000;
        const std::vector<AnklePx> ankles(size_t(nf), AnklePx{430, 900, 570, 900, true});
        auto makeBall = [&](double nx, double ny) {
            BallTrack2D b;
            for (int i = 0; i < nf; ++i) addBall(b, tUs[size_t(i)], nx, ny);
            return b;
        };
        int reason = -9;

        // consistent lock ABOVE the ankle line (the gateA-0704 driver-head
        // failure: cluster-consistent, so only the prior can catch it)
        const BallTrack2D high = makeBall(0.50, 0.85);       // y=850 ≤ 900−20
        check(medianGripBallLenPx(high, gx, gy, tUs, W, H, nf, nf, &still, &ankles, &reason) < 0
              && reason == 1, "consistent lock above the ankle line rejected (reason 1)");

        // real ball below the ankles — accepted, reason cleared
        const BallTrack2D low = makeBall(0.50, 0.95);        // y=950 > 880
        const double m = medianGripBallLenPx(low, gx, gy, tUs, W, H, nf, nf, &still, &ankles, &reason);
        check(near(m, 450.0, 1e-6) && reason == 0, "ball below the ankle line accepted (450, reason 0)");

        // below the ankle line but outside the between-the-feet corridor
        const BallTrack2D wide = makeBall(0.80, 0.95);       // x=800 > 570+100
        check(medianGripBallLenPx(wide, gx, gy, tUs, W, H, nf, nf, &still, &ankles, &reason) < 0
              && reason == 2, "lock outside the feet corridor rejected (reason 2)");

        // no usable pose in the window ⇒ gate skipped, accepted as before
        const std::vector<AnklePx> noPose(static_cast<size_t>(nf));   // all ok=false
        const double m2 = medianGripBallLenPx(high, gx, gy, tUs, W, H, nf, nf, &still, &noPose, &reason);
        check(near(m2, 350.0, 1e-6) && reason == 0, "no-pose window skips the gate (accepted)");
    }

    // ── medianGripBallLenPx: shape / empty no-ops ────────────────────────────
    std::printf("=== medianGripBallLenPx: no-op contracts ===\n");
    {
        std::vector<int64_t> tUs = {0, 10000, 20000};
        std::vector<double> gx = {300, 300, 300}, gy = {200, 200, 200};
        BallTrack2D empty;
        check(medianGripBallLenPx(empty, gx, gy, tUs, W, H, 3, 3, nullptr) < 0, "no ball ⇒ -1");
        BallTrack2D ball; addBall(ball, 0, 0.3, 0.3);
        std::vector<double> gxBad = {300, 300};   // size mismatch vs tUs
        check(medianGripBallLenPx(ball, gxBad, gy, tUs, W, H, 3, 3, nullptr) < 0, "grip/time size mismatch ⇒ -1");
    }

    // ── applyBallAnchor: A3 head recompute on anchored frames ────────────────
    std::printf("=== applyBallAnchor: A3 head recompute ===\n");
    {
        const int nf = 20, bs0 = 15;
        const ShotAnalysisJob job;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf, 500.0), gy(nf, 500.0);
        BallTrack2D ball;
        ShaftTrack2D out;
        out.frameWidth = W; out.frameHeight = H; out.addressPhaseFrame = bs0;
        for (int i = 0; i < nf; ++i) {
            tUs[i] = int64_t(i) * 10000;
            addBall(ball, tUs[i], 0.50, 0.80);               // ball (500,800), grip (500,500) ⇒ θ_ball = +90°
            ShaftSample2D s;
            s.t_us = tUs[i];
            s.gripPx = QPointF(500, 500);
            s.thetaRad = kPi / 2.0;                          // club points at the ball ⇒ no early tk0 departure
            s.headPx = QPointF(0, 0);                        // stale projected head
            s.visibleLenPx = 0;
            s.flags = ShaftHeadProjected | ShaftCoasted;     // not ShaftMeasured
            out.samples.push_back(s);
        }
        // a genuinely measured sample inside the address window must survive
        // untouched (corroborate-only) — its head is never rewritten.
        out.samples[3].flags = ShaftMeasured;
        out.samples[3].headPx = QPointF(111, 222);

        applyBallAnchor(out, ball, gx, gy, tUs, W, H, /*impf=*/-1, job, /*trace=*/nullptr);

        const ShaftSample2D &an = out.samples[5];            // inside [0,tk0=bs0) — anchored
        check(near(an.headPx.x(), 500.0, 1e-6) && near(an.headPx.y(), 800.0, 1e-6),
              "anchored head moved onto the ball");
        check(near(an.visibleLenPx, 300.0, 1e-6), "visibleLenPx = |head−grip| = 300");
        check((an.flags & ShaftBallAnchored) != 0, "ShaftBallAnchored set");
        check((an.flags & ShaftHeadProjected) == 0, "ShaftHeadProjected cleared");

        const ShaftSample2D &meas = out.samples[3];          // measured — corroborate only
        check((meas.flags & ShaftMeasured) != 0 && near(meas.headPx.x(), 111.0, 1e-6)
              && near(meas.headPx.y(), 222.0, 1e-6), "measured sample head untouched");

        const ShaftSample2D &past = out.samples[17];         // beyond tk0 — untouched
        check(near(past.headPx.x(), 0.0, 1e-6) && (past.flags & ShaftHeadProjected) != 0,
              "post-address sample left projected");
    }

    // ── applyBallAnchor: no-op contracts (empty ball / size mismatch) ────────
    std::printf("=== applyBallAnchor: no-op contracts ===\n");
    {
        const ShotAnalysisJob job;
        std::vector<int64_t> tUs = {0, 10000, 20000};
        std::vector<double> gx = {500, 500, 500}, gy = {500, 500, 500};
        ShaftTrack2D out;
        out.frameWidth = W; out.frameHeight = H; out.addressPhaseFrame = 1;
        for (int i = 0; i < 3; ++i) {
            ShaftSample2D s; s.t_us = tUs[size_t(i)]; s.headPx = QPointF(7, 9);
            s.flags = ShaftHeadProjected; out.samples.push_back(s);
        }
        // empty ball ⇒ no-op
        BallTrack2D empty;
        applyBallAnchor(out, empty, gx, gy, tUs, W, H, -1, job, nullptr);
        check(near(out.samples[0].headPx.x(), 7.0, 1e-6) && out.samples[0].flags == ShaftHeadProjected,
              "empty ball ⇒ track unchanged");
        // size mismatch (samples != tUs) ⇒ no-op even with a real ball
        BallTrack2D ball; addBall(ball, 0, 0.5, 0.8);
        std::vector<int64_t> tUsShort = {0, 10000};
        applyBallAnchor(out, ball, gx, gy, tUsShort, W, H, -1, job, nullptr);
        check(near(out.samples[0].headPx.x(), 7.0, 1e-6), "size mismatch ⇒ track unchanged");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
