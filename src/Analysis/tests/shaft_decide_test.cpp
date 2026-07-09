// Standalone tests for the shaft v3.0-r1 deciding half
// (src/Analysis/shaft_track_assembly — phase model, φ smoothing, C2 geometry,
// per-frame DP emission, banded Viterbi, ψ-isotonic reconcile). Synthetic
// inputs with hand-computable expectations. Full NUMERIC parity vs the Python
// exemplar is the separate shaft_parity_test (Phase 5).
//
//   cmake --build build/analyzer-tests --target shaft_decide_test
//   ctest --test-dir build/analyzer-tests -R shaft_decide --output-on-failure

#include "../shaft_track_assembly.h"

#include <opencv2/imgproc.hpp>   // cv::line/circle for the Phase-B synthetic frames

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

int main()
{
    const ShaftV3Config cfg;

    // ── exact weighted PAVA ──────────────────────────────────────────────────
    std::printf("=== pava ===\n");
    {
        // y=[1,3,2,4] increasing: 3>2 pool → [1,2.5,2.5,4]
        const std::vector<double> y = {1, 3, 2, 4}, w = {1, 1, 1, 1};
        const std::vector<double> x = pava(y, w, true);
        check(near(x[0], 1, 1e-9) && near(x[1], 2.5, 1e-9) && near(x[2], 2.5, 1e-9) && near(x[3], 4, 1e-9),
              "increasing pool of a single violation");
        // already-monotone increasing input is unchanged
        const std::vector<double> mono = {0, 1, 2, 3};
        const std::vector<double> xm = pava(mono, w, true);
        check(near(xm[3], 3, 1e-9) && near(xm[0], 0, 1e-9), "monotone input unchanged");
        // decreasing direction
        const std::vector<double> yd = {4, 2, 3, 1};
        const std::vector<double> xd = pava(yd, w, false);
        check(near(xd[1], 2.5, 1e-9) && near(xd[2], 2.5, 1e-9), "decreasing pool");
    }

    // ── robust isotonic down-weights an outlier ──────────────────────────────
    std::printf("=== robustIsotonic ===\n");
    {
        std::vector<double> y = {0, 1, 2, 3, 4, 5, 6, 7};
        std::vector<double> w(8, 1.0);
        y[4] = 40.0;   // single wild outlier
        const std::vector<double> x = robustIsotonic(y, w, true, cfg);
        // the fit stays near the clean line at the flanks, not dragged to 40
        check(x[3] < 6 && x[5] < 10, "outlier down-weighted (flanks stay low)");
        check(x[7] >= x[0], "still monotone increasing");
    }

    // ── banded Viterbi picks the decreasing well path (downswing) ────────────
    std::printf("=== viterbiDP ===\n");
    {
        const int NS = 16, nf = 3;
        std::vector<std::vector<float>> emis(nf, std::vector<float>(NS, 10.f));
        emis[0][8] = 0.f; emis[1][7] = 0.f; emis[2][6] = 0.f;   // decreasing bins
        const std::vector<SwingPhase> phase(nf, SwingPhase::Downswing);
        const DPResult dp = viterbiDP(emis, phase, cfg);
        check(dp.thstar.size() == 3 && dp.thstar[0] == 8 && dp.thstar[1] == 7 && dp.thstar[2] == 6,
              "path follows the decreasing wells 8→7→6");
    }

    // ── frameEmission: band well LAST + arm veto ─────────────────────────────
    std::printf("=== frameEmission ===\n");
    {
        const int NS = 360;
        std::vector<float> gridRad(NS), gridDeg(NS);
        for (int k = 0; k < NS; ++k) { gridDeg[k] = float(k); gridRad[k] = float(k * kPi / 180.0); }
        std::vector<float> evMax(NS, 0.2f), rawNorm(NS, 0.f);
        evMax[100] = 0.9f;
        BandMatch bm; bm.ok = true; bm.thetaDeg = 100.f; bm.n = 5;
        std::vector<float> em, inside;
        frameEmission(em, inside, evMax, rawNorm, bm, /*phiSDeg=*/0.0, SwingPhase::Downswing,
                      /*chir=*/1, /*gx=*/300, /*gy=*/300, /*poly=*/nullptr, cv::Mat(),
                      gridRad, gridDeg, cfg);
        check(near(em[100], -cfg.wBand, 1e-4), "band bin = -wBand (applied last)");
        check(near(em[180], cfg.wE2 * 0.8 + cfg.wArm, 1e-3), "arm-veto bin (φ+180) = wE2·(1-ev)+wArm");
        check(near(em[90], cfg.wE2 * 0.8, 1e-3), "neutral bin = wE2·(1-ev)");
    }

    // ── body geometry: half-plane inside/outside ─────────────────────────────
    std::printf("=== bodyPolys ===\n");
    {
        // a square torso 100..300 in x and y (one frame)
        std::vector<std::vector<cv::Point2d>> joints = {{{100, 100}, {300, 100}, {300, 300}, {100, 300}}};
        const std::vector<BodyPoly> polys = bodyPolys(joints);
        check(polys.size() == 1 && polys[0].n.size() == 4, "one 4-edge hull");
        auto insideFrac = [&](double px, double py) {
            double mx = -1e30;
            for (size_t e = 0; e < polys[0].n.size(); ++e)
                mx = std::max(mx, polys[0].n[e][0] * px + polys[0].n[e][1] * py - polys[0].d[e]);
            return mx;   // ≤ margin ⇒ inside
        };
        check(insideFrac(200, 200) <= cfg.bodyMargin, "centre inside");
        check(insideFrac(500, 200) > cfg.bodyMargin, "far-right point outside");
    }

    // ── segmentPhases on a synthetic swing ───────────────────────────────────
    std::printf("=== segmentPhases ===\n");
    {
        const int nf = 170;
        std::vector<double> gx(nf), gy(nf);
        double x = 200, y = 300;
        for (int f = 0; f < nf; ++f) {
            if (f >= 40 && f < 70)      { x -= 4; y -= 9; }   // backswing (speed ≈ 9.8 > swSpd)
            else if (f >= 77 && f < 109){ x += 3; y += 9; }   // downswing
            gx[f] = x; gy[f] = y;
        }
        const PhaseModel pm = segmentPhases(gx, gy, nf, 150.0, -1, cfg);
        check(pm.phase.size() == size_t(nf), "phase per frame");
        check(pm.phase.front() == SwingPhase::Addr, "starts at address");
        check(pm.phase.back() == SwingPhase::Finish, "ends at finish");
        check(pm.bs0 > 30 && pm.bs0 < 50, "takeaway near f40");
        check(pm.top > pm.bs0 && pm.impact > pm.top && pm.fin0 >= pm.impact, "landmarks ordered");
        bool hasImpact = false, hasBackswing = false;
        for (SwingPhase p : pm.phase) { if (p == SwingPhase::Impact) hasImpact = true; if (p == SwingPhase::Backswing) hasBackswing = true; }
        check(hasImpact && hasBackswing, "impact + backswing phases present");
    }

    // ── reconcilePsi: monotone ψ untouched; impact counterfeit reconstructed ─
    std::printf("=== reconcilePsi ===\n");
    {
        const int nf = 20;
        std::vector<double> theta(nf), phi(nf, 0.0), evAt(nf, 0.6);
        std::vector<SwingPhase> phase(nf, SwingPhase::Downswing);
        std::vector<char> bandOk(nf, 0);
        for (int f = 0; f < nf; ++f) theta[f] = 100.0 - 5.0 * f;   // strictly decreasing ψ (φ=0)
        const ReconResult a = reconcilePsi(theta, phi, phase, bandOk, evAt, /*top=*/1000, nf, cfg);
        bool noRecon = true; for (char r : a.recon) if (r) noRecon = false;
        check(noRecon, "monotone ψ ⇒ no reconstruction");
        check(near(a.thetaOut[10], theta[10], 1e-6), "θ preserved when monotone");

        std::vector<double> theta2 = theta;
        theta2[10] = 200.0;                     // wild impact-frame counterfeit
        std::vector<SwingPhase> phase2 = phase; phase2[10] = SwingPhase::Impact;
        const ReconResult b = reconcilePsi(theta2, phi, phase2, bandOk, evAt, 1000, nf, cfg);
        check(b.recon[10] == 1, "impact counterfeit flagged recon");
        check(std::abs(b.thetaOut[10] - 200.0) > 20.0, "θ pulled back toward the monotone rail");
    }

    // ── vision-only segmentation mapping ─────────────────────────────────────
    std::printf("=== phasesToSegmentation ===\n");
    {
        PhaseModel pm;
        pm.bs0 = 40; pm.top = 80; pm.impact = 110; pm.fin0 = 150;
        const int nf = 200;
        std::vector<int64_t> tUs(nf);
        for (int i = 0; i < nf; ++i) tUs[i] = int64_t(i) * 6700;   // ~149 fps
        const Segmentation seg = phasesToSegmentation(pm, tUs, 0.5f);
        check(seg.events.size() == 4, "four ladder events");
        check(seg.conf == 0.5f, "vision-grade conf");
        bool ordered = true;
        for (size_t i = 1; i < seg.events.size(); ++i) if (seg.events[i].t_us < seg.events[i - 1].t_us) ordered = false;
        check(ordered, "events time-ordered");
        check(seg.events[0].phase == Phase::Address && seg.events[0].t_us == tUs[40], "Address at bs0");
        check(seg.events[2].phase == Phase::Impact && seg.events[2].t_us == tUs[110], "Impact at impact frame");
        check(seg.swingStartUs >= tUs.front() && seg.swingStartUs < tUs[40], "swingStart padded + clamped");
        check(seg.swingEndUs <= tUs.back() && seg.swingEndUs > tUs[150], "swingEnd padded + clamped");
        // degenerate (conf 0) still returns bounds but no swing claim
        const Segmentation deg = phasesToSegmentation(pm, tUs, 0.0f);
        check(deg.conf == 0.0f, "conf 0 passthrough (no swing)");
    }

    // ── A2 length ladder: rung precedence + clamps (projectedClubLenPx) ───────
    std::printf("=== projectedClubLenPx ===\n");
    {
        const double frameH = 1000.0, clubLenMm = 1120.0;
        int rung = 0;
        // rung 1 — ball measurement wins over every lower source
        double L = projectedClubLenPx(/*meas=*/400, /*sTypical=*/0.5, /*r0Med=*/100,
                                      /*poseExtent=*/705.5, /*armFloor=*/0, clubLenMm, frameH, cfg, rung);
        check(rung == 1 && near(L, 400, 1e-6), "rung 1 = ball L_px (400)");
        // rung 2 — band scale, grip-corrected: 0.5·(1120−100) = 510
        L = projectedClubLenPx(-1, 0.5, 100, 705.5, 0, clubLenMm, frameH, cfg, rung);
        check(rung == 2 && near(L, 510, 1e-6), "rung 2 = sTypical·(clubLenMm−r0Med) (510)");
        // rung 3 — pose scale: 705.5/(0.83·1.70)=500 px/m; 500·(1.12−0.13)=495
        L = projectedClubLenPx(-1, 0, 0, 705.5, 0, clubLenMm, frameH, cfg, rung);
        check(rung == 3 && near(L, 495, 0.5), "rung 3 = pose-scale surrogate (495)");
        // rung 4 — frame-height fallback: 0.45·1000
        L = projectedClubLenPx(-1, 0, 0, 0, 0, clubLenMm, frameH, cfg, rung);
        check(rung == 4 && near(L, 450, 1e-6), "rung 4 = 0.45·frameH (450)");
        // arm floor raises a short fallback length
        L = projectedClubLenPx(-1, 0, 0, 0, /*armFloor=*/600, clubLenMm, frameH, cfg, rung);
        check(rung == 4 && near(L, 600, 1e-6), "arm floor lifts the length (600)");
        // fallback ceiling caps a runaway band scale at 0.62·frameH = 620
        L = projectedClubLenPx(-1, 2.0, 0, 0, 0, clubLenMm, frameH, cfg, rung);
        check(rung == 2 && near(L, 620, 1e-6), "fallback ceiling caps at 0.62·frameH (620)");
        // ball ceiling (1.1·L_px) is authoritative even over a longer arm floor
        L = projectedClubLenPx(400, 0, 0, 0, /*armFloor=*/1000, clubLenMm, frameH, cfg, rung);
        check(rung == 1 && near(L, 440, 1e-6), "ball ceiling 1.1·L_px wins over the floor (440)");
    }

    // ── θ invariance: the ball only changes head/length, never θ ─────────────
    std::printf("=== decideTrack θ invariance vs ball ===\n");
    {
        const int nf = 60;
        const int W = 1000, H = 1000;
        const double fps = 100.0;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf), gy(nf), phiRaw(nf, 90.0);
        std::vector<std::vector<cv::Point2d>> joints(nf, std::vector<cv::Point2d>(8));
        double x = 500, y = 600;
        for (int f = 0; f < nf; ++f) {
            tUs[f] = int64_t(f) * 10000;
            // long still address hold (0..39) so staticRuns clears stillMin=25
            // even after speed-smoothing bleed near the takeaway.
            if (f >= 40 && f < 53)      { x -= 4; y -= 9; }   // backswing (speed ≈ 9.8 > swSpd)
            else if (f >= 53 && f < 60) { x += 4; y += 9; }   // downswing
            gx[f] = x; gy[f] = y;
            joints[f] = {{450, 300}, {550, 300}, {460, 600}, {540, 600},
                         {465, 750}, {535, 750}, {470, 900}, {530, 900}};  // sh/hip/knee/ankle
        }
        // ball fixed below the grip; still-hold grip→ball ≈ 200 px
        BallTrack2D ball;
        for (int f = 0; f < nf; ++f) {
            BallSample2D b; b.t_us = tUs[f]; b.found = true;
            b.center = QPointF(0.50, 0.80); b.radiusNorm = 0.02f; b.conf = 1.f;
            ball.frames.push_back(b);
        }
        const FrameSource noFrames = [](int) -> cv::Mat { return cv::Mat(); };

        const ShaftTrack2D a = decideTrack(noFrames, tUs, gx, gy, phiRaw, joints, W, H, fps,
                                           /*bandsMm=*/{}, /*clubLenMm=*/1120.0, /*impactFrame=*/-1,
                                           cfg, nullptr, /*ball=*/nullptr);
        const ShaftTrack2D b = decideTrack(noFrames, tUs, gx, gy, phiRaw, joints, W, H, fps,
                                           {}, 1120.0, -1, cfg, nullptr, &ball);

        bool sameCount = a.samples.size() == b.samples.size() && !a.samples.empty();
        bool thetaIdentical = sameCount;
        bool headDiffers = false;
        for (size_t i = 0; sameCount && i < a.samples.size(); ++i) {
            if (a.samples[i].thetaRad != b.samples[i].thetaRad) thetaIdentical = false;
            if (a.samples[i].headPx != b.samples[i].headPx) headDiffers = true;
        }
        check(sameCount, "same sample count with/without ball");
        check(thetaIdentical, "θ bit-identical with/without ball");
        check(a.measuredClubLenPx < 0 && near(b.measuredClubLenPx, 200.0, 2.0),
              "ball populates measuredClubLenPx (~200 px); null leaves −1");
        check(headDiffers, "projected head length changed (length ladder used the ball)");
    }

    // ── Phase B: tier hoist is a pure refactor + head pass never perturbs θ ──
    std::printf("=== decideTrack head pass (Phase B) ===\n");
    {
        const int nf = 60, W = 480, H = 480;
        const double fps = 100.0;
        std::vector<int64_t> tUs(nf);
        std::vector<double> gx(nf), gy(nf), phiRaw(nf, 90.0);
        std::vector<std::vector<cv::Point2d>> joints(nf, std::vector<cv::Point2d>(8));
        double x = 240, y = 150;
        for (int f = 0; f < nf; ++f) {
            tUs[f] = int64_t(f) * 10000;
            // long still address hold (0..39) so staticRuns clears stillMin=25,
            // then a short backswing/downswing (grip speed ≈ 6.7 > swSpd).
            if (f >= 40 && f < 53)      { x -= 3; y += 6; }
            else if (f >= 53 && f < 60) { x += 3; y -= 6; }
            gx[f] = x; gy[f] = y;
            joints[f] = {{210, 90}, {270, 90}, {215, 200}, {265, 200},
                         {218, 300}, {262, 300}, {220, 380}, {260, 380}};  // sh/hip/knee/ankle
        }
        // ball fixed below the grip so still-hold grip→ball ≈ 196 px is measurable
        BallTrack2D ball;
        for (int f = 0; f < nf; ++f) {
            BallSample2D b; b.t_us = tUs[f]; b.found = true;
            b.center = QPointF(0.50, 0.72); b.radiusNorm = 0.02f; b.conf = 1.f;
            ball.frames.push_back(b);
        }
        // Deterministic synthetic frames: mid-grey with a downward club line from
        // the grip + a ball blob, so sceneMed is non-empty (else the head pass
        // no-ops) and the pass does real Sobel/measure work. Measurement QUALITY
        // is irrelevant — the invariants below hold regardless.
        const FrameSource render = [&](int f) -> cv::Mat {
            if (f < 0 || f >= nf) return cv::Mat();
            cv::Mat img(H, W, CV_8UC1, cv::Scalar(30));
            const cv::Point g(int(gx[f]), int(gy[f]));
            cv::line(img, g, cv::Point(g.x, std::min(H - 1, g.y + 150)), cv::Scalar(220), 3);
            cv::circle(img, cv::Point(int(0.50 * W), int(0.72 * H)), 7, cv::Scalar(240), -1);
            return img;
        };

        ShaftV3Config cfgOff = cfg;                       // head.enabled = false (default)
        ShaftV3Config cfgOn  = cfg; cfgOn.head.enabled = true;
        ShaftDecideTrace trOff, trOn;
        const ShaftTrack2D a = decideTrack(render, tUs, gx, gy, phiRaw, joints, W, H, fps,
                                           {}, 1120.0, -1, cfgOff, &trOff, &ball);
        const ShaftTrack2D b = decideTrack(render, tUs, gx, gy, phiRaw, joints, W, H, fps,
                                           {}, 1120.0, -1, cfgOn,  &trOn,  &ball);

        // (b) θ bit-identical head off vs on, on every sample — the head pass
        // must never perturb the stage-1 θ path.
        bool sameCount = a.samples.size() == b.samples.size() && !a.samples.empty();
        bool thetaIdentical = sameCount;
        for (size_t i = 0; sameCount && i < a.samples.size(); ++i)
            if (a.samples[i].thetaRad != b.samples[i].thetaRad) thetaIdentical = false;
        check(sameCount, "same sample count head off/on");
        check(thetaIdentical, "θ bit-identical head off/on");

        // (a) the tier hoist is a pure refactor: tier/conf/θ trace identical off
        // vs on (the head pass reads tierOf[], never rewrites it).
        bool traceMatch = trOff.tier.size() == trOn.tier.size()
                       && trOff.conf.size() == trOn.conf.size()
                       && trOff.thetaDeg.size() == trOn.thetaDeg.size() && !trOff.tier.empty();
        for (size_t i = 0; traceMatch && i < trOff.tier.size(); ++i) {
            if (trOff.tier[i]     != trOn.tier[i])     traceMatch = false;
            if (trOff.conf[i]     != trOn.conf[i])     traceMatch = false;
            if (trOff.thetaDeg[i] != trOn.thetaDeg[i]) traceMatch = false;
        }
        check(traceMatch, "tier/conf/θ trace identical head off/on (pure hoist)");

        // (b) headMs + head trace populated only when enabled.
        check(trOff.headMs == 0.0 && trOff.headTier.empty(), "head trace empty when disabled");
        check(trOn.headMs > 0.0 && trOn.headTier.size() == size_t(nf),
              "headMs + headTier populated when enabled");

        // (c) the off-frame flag is only ever set when headPx lies on the frame
        // boundary (edge-clamped, not a head) and always co-set with 0x10.
        bool offInvariant = true;
        for (const ShaftSample2D &sm : b.samples) {
            if (!(sm.flags & ShaftHeadOffFrame)) continue;
            const double hx = sm.headPx.x(), hy = sm.headPx.y();
            const double dEdge = std::min(std::min(std::abs(hx), std::abs(hx - (W - 1))),
                                          std::min(std::abs(hy), std::abs(hy - (H - 1))));
            if (dEdge > 0.5) offInvariant = false;
            if (!(sm.flags & ShaftHeadProjected)) offInvariant = false;
        }
        check(offInvariant, "off-frame flag ⇒ headPx on the frame boundary + projected");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
