// Standalone tests for the clubhead Stage-2 per-frame measurement half
// (src/Analysis/clubhead_track — measureHeadRadius, rayEdgeRadius, the B2
// ball-length model helpers, isFlipSuspect). Synthetic frames with known
// geometry; the temporal half is the separate clubhead_temporal_test.
//
//   cmake --build build/analyzer-tests --target clubhead_measure_test
//   ctest --test-dir build/analyzer-tests -R clubhead_measure --output-on-failure

#include "../clubhead_track.h"

#include <opencv2/imgproc.hpp>

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
static bool inRange(double v, double lo, double hi) { return v >= lo && v <= hi; }

// A CV_32F frame filled with `bg`; draw horizontal bright lines with drawSeg().
static cv::Mat makeFrame(int W, int H, double bg) { return cv::Mat(H, W, CV_32F, cv::Scalar(bg)); }
// Draw a segment of the horizontal club ray at grip.x+r0 .. grip.x+r1 (θ=0).
static void drawSeg(cv::Mat &img, double gx, double gy, double r0, double r1, double val, int thick)
{
    cv::line(img, cv::Point(int(gx + r0), int(gy)), cv::Point(int(gx + r1), int(gy)),
             cv::Scalar(val), thick, cv::LINE_8);
}
static void sobels(const cv::Mat &g32, cv::Mat &gx, cv::Mat &gy)
{
    cv::Sobel(g32, gx, CV_32F, 1, 0, 3);
    cv::Sobel(g32, gy, CV_32F, 0, 1, 3);
}

int main()
{
    const ClubheadConfig cfg;

    // ── rayEdgeRadius geometry ───────────────────────────────────────────────
    std::printf("=== rayEdgeRadius ===\n");
    {
        const int W = 640, H = 480;
        check(near(rayEdgeRadius(100, 100, 0.0,   W, H), 539.0, 1.0), "θ=0 exits right edge at W−1−gx");
        check(near(rayEdgeRadius(100, 100, 180.0, W, H), 100.0, 1.0), "θ=180 exits left edge at gx");
        check(near(rayEdgeRadius(100, 100, 90.0,  W, H), 379.0, 1.0), "θ=90 exits bottom at H−1−gy");
        check(near(rayEdgeRadius(100, 100, 270.0, W, H), 100.0, 1.0), "θ=270 exits top at gy");
    }

    // ── gap-tolerant terminus: a 75-px specular gap must not sever the run ────
    std::printf("=== measureHeadRadius: 75-px gap tolerance ===\n");
    {
        const int W = 800, H = 400;
        const double gx = 120, gy = 200;
        cv::Mat g = makeFrame(W, H, 40.0);
        drawSeg(g, gx, gy, 30, 110, 200.0, 3);    // near shaft  r∈[30,110]
        drawSeg(g, gx, gy, 185, 215, 255.0, 5);   // dark-head terminus r∈[185,215]
        //          ^^^ 75-px blowout gap r∈[110,185] left blank
        cv::Mat gxs, gys; sobels(g, gxs, gys);
        HeadSceneCtx ctx = makeHeadSceneCtx(cv::Mat(), W, H, cfg);   // no scene ref
        const cv::Mat bg = g.clone(), prev = g.clone();             // no motion

        // A length prior at the far head selects the terminus 75 px BEYOND the
        // gap — only possible if the far segment is a valid grip-connected
        // candidate (support ≥ SUPPORT_MIN across the gap). Continuity required
        // ⇒ no far candidate ⇒ the prior could not reach it.
        const HeadMeasurement far = measureHeadRadius(g, prev, bg, gxs, gys, ctx, gx, gy, 0.0,
                                                      ctx.rMin, 260.0, /*rFloor=*/-1.0, /*lPrior=*/215.0);
        check(std::isfinite(far.rPx) && inRange(far.rPx, 200.0, 225.0),
              "far terminus (~215) recovered across the 75-px gap");
        // A prior at the near shaft keeps the near terminus.
        const HeadMeasurement nearM = measureHeadRadius(g, prev, bg, gxs, gys, ctx, gx, gy, 0.0,
                                                        ctx.rMin, 260.0, -1.0, /*lPrior=*/110.0);
        check(std::isfinite(nearM.rPx) && inRange(nearM.rPx, 100.0, 122.0),
              "near terminus (~110) selected when the prior sits there");

        // determinism: identical inputs ⇒ byte-identical output
        const HeadMeasurement a = measureHeadRadius(g, prev, bg, gxs, gys, ctx, gx, gy, 0.0, ctx.rMin, 260.0, -1.0, 215.0);
        const HeadMeasurement b = measureHeadRadius(g, prev, bg, gxs, gys, ctx, gx, gy, 0.0, ctx.rMin, 260.0, -1.0, 215.0);
        check(a.rPx == b.rPx && a.conf == b.conf, "deterministic (byte-identical rerun)");
    }

    // ── permanence veto: a line present in the scene median is scenery, vetoed ─
    std::printf("=== measureHeadRadius: permanence veto ===\n");
    {
        const int W = 800, H = 400;
        const double gx = 120, gy = 200;
        // scene median contains a permanent bright strip at r∈[150,210]
        cv::Mat sceneMed = makeFrame(W, H, 40.0);
        drawSeg(sceneMed, gx, gy, 150, 210, 230.0, 3);
        // current frame: the permanent strip (unchanged) + a moving club at r∈[30,90]
        cv::Mat g = makeFrame(W, H, 40.0);
        drawSeg(g, gx, gy, 150, 210, 230.0, 3);
        drawSeg(g, gx, gy, 30, 90, 200.0, 3);
        cv::Mat gxs, gys; sobels(g, gxs, gys);
        const cv::Mat bg = makeFrame(W, H, 40.0);
        const cv::Mat prev = makeFrame(W, H, 40.0);   // strip present in g but not prev ⇒ it "moves"

        HeadSceneCtx ctx = makeHeadSceneCtx(sceneMed, W, H, cfg);   // permanence veto ON
        // Even a prior favouring the far strip cannot select it: it is vetoed.
        const HeadMeasurement vetoed = measureHeadRadius(g, prev, bg, gxs, gys, ctx, gx, gy, 0.0,
                                                         ctx.rMin, 260.0, -1.0, /*lPrior=*/210.0);
        check(std::isfinite(vetoed.rPx) && inRange(vetoed.rPx, 78.0, 100.0),
              "permanent strip vetoed → near club terminus (~90) wins");

        HeadSceneCtx noVeto = ctx;              // same scene ref, permanence OFF
        noVeto.sceneMedGx.release();
        noVeto.sceneMedGy.release();
        const HeadMeasurement unvetoed = measureHeadRadius(g, prev, bg, gxs, gys, noVeto, gx, gy, 0.0,
                                                           noVeto.rMin, 260.0, -1.0, 210.0);
        check(std::isfinite(unvetoed.rPx) && inRange(unvetoed.rPx, 198.0, 222.0),
              "without the veto the (moving) strip terminus (~210) wins");
    }

    // ── ambiguity shaping: a near-tied runner-up halves the confidence ────────
    std::printf("=== measureHeadRadius: ambiguity conf ===\n");
    {
        const int W = 800, H = 400;
        const double gx = 120, gy = 200;
        cv::Mat gTie = makeFrame(W, H, 40.0);
        drawSeg(gTie, gx, gy, 30, 90,  150.0, 3);   // candidate A (near)
        drawSeg(gTie, gx, gy, 110, 170, 150.0, 3);  // candidate B (far) — comparable
        cv::Mat gxsT, gysT; sobels(gTie, gxsT, gysT);
        cv::Mat gSingle = makeFrame(W, H, 40.0);
        drawSeg(gSingle, gx, gy, 30, 90, 150.0, 3); // only one candidate
        cv::Mat gxsS, gysS; sobels(gSingle, gxsS, gysS);

        HeadSceneCtx ctx = makeHeadSceneCtx(cv::Mat(), W, H, cfg);
        const cv::Mat bgT = gTie.clone(), pvT = gTie.clone();
        const cv::Mat bgS = gSingle.clone(), pvS = gSingle.clone();

        const HeadMeasurement tie = measureHeadRadius(gTie, pvT, bgT, gxsT, gysT, ctx, gx, gy, 0.0,
                                                      ctx.rMin, 260.0, -1.0, /*lPrior=*/NAN);
        const HeadMeasurement one = measureHeadRadius(gSingle, pvS, bgS, gxsS, gysS, ctx, gx, gy, 0.0,
                                                      ctx.rMin, 260.0, -1.0, NAN);
        check(std::isfinite(tie.rPx) && std::isfinite(one.rPx), "both produce a terminus");
        check(one.conf > 0.8, "single clear candidate ⇒ high confidence");
        check(tie.conf < one.conf * 0.6 && tie.conf > 0.2,
              "near-tie halves the confidence (ambiguity shaping)");
    }

    // ── B2 ball-length model: floor / ceiling / prior incl. no-ball ──────────
    std::printf("=== headBounds / headPrior / arm+hard floor ===\n");
    {
        HeadSceneCtx ctx = makeHeadSceneCtx(cv::Mat(), 1000, 1000, cfg);   // rMin = 60
        check(near(ctx.rMin, 60.0, 1e-9), "rMin = max(20, rMinFrac·H) = 60");

        const cv::Point2d lSho(200, 100), tSho(300, 100);
        const double af = armFloorPx(lSho, tSho, /*gx=*/250, /*gy=*/300, /*still=*/true, cfg);
        check(near(af, 1.05 * std::hypot(50.0, 200.0), 1e-6), "arm floor = armFactor·max shoulder→grip");
        check(armFloorPx(lSho, tSho, 250, 300, /*still=*/false, cfg) < 0, "no arm floor when not still");

        check(near(hardFloorPx(af, /*lPx=*/500, true, cfg), 400.0, 1e-6), "hard floor = max(armFloor, 0.8·L_px)");
        check(hardFloorPx(af, 500, /*applies=*/false, cfg) < 0, "no hard floor when it doesn't apply");
        check(near(hardFloorPx(af, /*lPx=*/-1, true, cfg), af, 1e-6), "no ball ⇒ hard floor = arm floor");

        // ball present: ceiling caps projection at 1.15·L_px
        HeadBounds b1 = headBounds(/*rayEdge=*/800, /*lPx=*/500, /*fallbackCeil=*/0, af, /*floor=*/true, ctx);
        check(near(b1.rLo, 60, 1e-9) && near(b1.rHi, 575.0, 1e-6) && near(b1.rFloor, 400.0, 1e-6),
              "ball: rHi=min(rayEdge,1.15·L_px)=575, rFloor=400");
        // ball ceiling also clamps to the ray edge when the edge is nearer
        HeadBounds b2 = headBounds(/*rayEdge=*/500, 500, 0, af, true, ctx);
        check(near(b2.rHi, 500.0, 1e-6), "ceiling clamped to the ray edge when nearer");
        // no ball: fallback ladder length is BOTH the ceiling and the universal-
        // floor basis — rFloor = max(arm floor ≈216.5, 0.5·450 = 225) = 225
        HeadBounds b3 = headBounds(800, /*lPx=*/-1, /*fallbackCeil=*/450, af, true, ctx);
        check(near(b3.rHi, 450.0, 1e-6) && near(b3.rFloor, 225.0, 1e-6),
              "no ball ⇒ rHi=fallback ceiling, rFloor=max(arm floor, 0.5·L̂)=225");
        // no ball, no fallback ⇒ ray edge only, no floor when it doesn't apply
        HeadBounds b4 = headBounds(800, -1, 0, -1, false, ctx);
        check(near(b4.rHi, 800.0, 1e-6) && b4.rFloor < 0, "no ball/fallback ⇒ ray edge only, no floor");

        // universal measurement-acceptance floor (gateB-0705 fix): a MOVING no-
        // ball frame (floorApplies=false, no arm floor) still floors at 0.5·L̂
        HeadBounds b5 = headBounds(800, /*lPx=*/-1, /*fallbackCeil=*/300, /*armFloor=*/-1, /*floor=*/false, ctx);
        check(near(b5.rFloor, 150.0, 1e-6), "moving no-ball frame: universal floor = 0.5·L̂ (150)");
        // ball present, moving frame: universal floor from L_px (no 0.8 hard floor)
        HeadBounds b6 = headBounds(800, /*lPx=*/500, 0, -1, /*floor=*/false, ctx);
        check(near(b6.rFloor, 250.0, 1e-6), "moving ball frame: universal floor = 0.5·L_px (250)");
        // still/impact frame keeps the STRONGER 0.8·L_px hard floor (universal is weaker)
        HeadBounds b7 = headBounds(800, 500, 0, -1, /*floor=*/true, ctx);
        check(near(b7.rFloor, 400.0, 1e-6), "still frame: 0.8·L_px hard floor dominates the universal");

        // floorFracOverride — the wiring's phase-ramp hook (gateB iter-2). The
        // module stays phase-blind: the wiring computes the per-frame ramped
        // fraction from bs0/top/impact and passes it here; the ramp arithmetic
        // itself is wiring-level and covered by the corpus honesty gate.
        HeadBounds b8 = headBounds(800, 500, 0, -1, /*floor=*/false, ctx, /*override=*/0.8);
        check(near(b8.rFloor, 400.0, 1e-6), "override 0.8 ⇒ floor = 0.8·L_px (400) on a moving frame");
        HeadBounds b9 = headBounds(800, /*lPx=*/-1, /*fallbackCeil=*/300, -1, false, ctx, 0.7);
        check(near(b9.rFloor, 210.0, 1e-6), "override honoured against the fallback L̂ (0.7·300 = 210)");
        HeadBounds b10 = headBounds(800, 500, 0, -1, false, ctx, -1.0);
        check(near(b10.rFloor, 250.0, 1e-6), "override −1 ⇒ default constant-fraction behaviour (250)");

        check(near(headPrior(500, true), 500.0, 1e-9), "prior = L_px in quasi-still frames");
        check(std::isnan(headPrior(500, false)), "no prior in moving frames (never feed the KF its own prior)");
        check(std::isnan(headPrior(-1, true)), "no prior without a ball");
    }

    // ── universal floor end-to-end: counterfeit vs true terminus (gateB-0705) ─
    std::printf("=== measureHeadRadius: universal measurement floor ===\n");
    {
        const int W = 800, H = 400;
        const double gx = 120, gy = 200;
        const double lHat = 300.0;                       // ladder length (no ball)
        HeadSceneCtx ctx = makeHeadSceneCtx(cv::Mat(), W, H, cfg);
        // moving no-ball frame: bounds carry the universal floor 0.5·L̂ = 150
        const HeadBounds b = headBounds(rayEdgeRadius(gx, gy, 0.0, W, H), -1.0, lHat,
                                        -1.0, /*floorApplies=*/false, ctx);
        check(near(b.rFloor, 150.0, 1e-6), "bounds carry the 0.5·L̂ floor on a moving frame");

        // 1) counterfeit at ~0.35·L̂ (retro-band edge) + true terminus at ~1.0·L̂:
        //    the floor filters the counterfeit CANDIDATE but not the scan, so the
        //    true head past it still wins.
        cv::Mat gBoth = makeFrame(W, H, 40.0);
        drawSeg(gBoth, gx, gy, 30, 105, 200.0, 3);       // counterfeit run ends at 105 < 150
        drawSeg(gBoth, gx, gy, 280, 310, 255.0, 5);      // true head at ~1.0·L̂
        cv::Mat gxsB, gysB; sobels(gBoth, gxsB, gysB);
        const cv::Mat bgB = gBoth.clone(), pvB = gBoth.clone();
        const HeadMeasurement both = measureHeadRadius(gBoth, pvB, bgB, gxsB, gysB, ctx, gx, gy, 0.0,
                                                       b.rLo, b.rHi, b.rFloor, NAN);
        check(std::isfinite(both.rPx) && inRange(both.rPx, 295.0, 320.0),
              "true terminus (~310) wins past the sub-floor counterfeit");

        // 2) counterfeit ONLY: every candidate is below the floor ⇒ no measurement
        //    (frame degrades to honest pred downstream), never a blessed counterfeit.
        cv::Mat gCft = makeFrame(W, H, 40.0);
        drawSeg(gCft, gx, gy, 30, 105, 200.0, 3);
        cv::Mat gxsC, gysC; sobels(gCft, gxsC, gysC);
        const cv::Mat bgC = gCft.clone(), pvC = gCft.clone();
        const HeadMeasurement cft = measureHeadRadius(gCft, pvC, bgC, gxsC, gysC, ctx, gx, gy, 0.0,
                                                      b.rLo, b.rHi, b.rFloor, NAN);
        check(std::isnan(cft.rPx) && cft.conf == 0.0,
              "counterfeit-only frame yields NO measurement (honest pred)");
        // control: without the floor the counterfeit WOULD be measured — the
        // exact gateB-0705 failure this closes.
        const HeadMeasurement unfl = measureHeadRadius(gCft, pvC, bgC, gxsC, gysC, ctx, gx, gy, 0.0,
                                                       b.rLo, b.rHi, /*rFloor=*/-1.0, NAN);
        check(std::isfinite(unfl.rPx) && inRange(unfl.rPx, 95.0, 120.0),
              "control: floorless scan measures the counterfeit (~105)");

        // 3) genuinely foreshortened head at ~0.4·L̂ is refused too — the intended
        //    honesty trade: pred (honest), not meas.
        cv::Mat gFsh = makeFrame(W, H, 40.0);
        drawSeg(gFsh, gx, gy, 30, 120, 200.0, 3);        // true-but-foreshortened terminus at 0.4·L̂
        cv::Mat gxsF, gysF; sobels(gFsh, gxsF, gysF);
        const cv::Mat bgF = gFsh.clone(), pvF = gFsh.clone();
        const HeadMeasurement fsh = measureHeadRadius(gFsh, pvF, bgF, gxsF, gysF, ctx, gx, gy, 0.0,
                                                      b.rLo, b.rHi, b.rFloor, NAN);
        check(std::isnan(fsh.rPx), "foreshortened true head below 0.5·L̂ ⇒ pred, not meas (honesty trade)");
    }

    // ── 180° flip predicate ──────────────────────────────────────────────────
    std::printf("=== isFlipSuspect ===\n");
    {
        check(isFlipSuspect(/*fwd=*/0.4, /*opp=*/0.7, cfg), "opposite ray decisively stronger ⇒ suspect");
        check(!isFlipSuspect(0.6, 0.7, cfg), "opposite only marginally stronger ⇒ not suspect");
        check(!isFlipSuspect(0.9, 0.4, cfg), "forward stronger ⇒ not suspect");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
