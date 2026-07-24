// Synthetic-recovery + determinism + degrade tests for the P-position fit
// (src/Analysis/swing_ref_fit.{h,cpp}). Self-contained (own main() + CHECK macros;
// no googletest), the same style as swing_reference_test.cpp.
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests --parallel 4 && ctest --test-dir build/analyzer-tests -R swing_ref_fit
//
// The fit is deterministic (fixed sweeps/iters, no RNG). The recovery test builds a
// TRUTH model, projects its P butt/head through a fixed orthographic projection to
// synthesise measured anchors, then confirms the fit — seeded with perturbed params
// (all with truth inside the bounds) — drives the projected reference back onto the
// truth arc. No OpenCV.

#include "../swing_ref_fit.h"

#include <QPointF>
#include <QVector3D>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace pinpoint::swingref;

static int g_fail = 0;

#define CHECK(label, cond)                                                              \
    do {                                                                               \
        const bool ok = (cond);                                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                       \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                              \
    do {                                                                               \
        const double g = (got), w = (want), t = (tol);                                 \
        const bool ok = std::abs(g - w) <= t;                                          \
        std::printf("  [%s] %-34s got %11.5f  want %11.5f  (tol %.5f)\n",              \
                    ok ? "PASS" : "FAIL", label, g, w, t);                             \
        if (!ok) ++g_fail;                                                             \
    } while (0)

// P-index → (segment 0/1/2, localS) — the fit's own mapping (globalSForP +
// segmentForGlobalS). Returns false outside [1,8].
static bool pSeg(int p, int& seg, double& ls)
{
    double g;
    switch (p) {
    case 1: g = 0.00; break; case 2: g = 0.33; break; case 3: g = 0.66; break;
    case 4: g = 1.00; break; case 5: g = 1.40; break; case 6: g = 1.80; break;
    case 7: g = 2.00; break; case 8: g = 3.00; break; default: return false;
    }
    if (g < 1.0)      { seg = 0; ls = g; }
    else if (g < 2.0) { seg = 1; ls = g - 1.0; }
    else              { seg = 2; ls = g - 2.0; }
    return true;
}

// Fixed orthographic projection (OrthographicProjection convention).
static QPointF proj(const QVector3D& w, double ox, double oy, double s, double xSign)
{
    return QPointF(ox + xSign * s * double(w.x()), oy - s * double(w.z()));
}

int main()
{
    std::printf("=== swing_ref_fit.h synthetic-recovery / determinism / degrade ===\n\n");

    // Projection.
    const double kOx = 650.0, kOy = 990.0, kS = 415.0, kXsign = 1.0;

    // ── TRUTH model ──────────────────────────────────────────────────────────
    const double kTruthLc = 0.94;
    GolferAnthro ta{ QVector3D(0.05f, -0.43f, 1.32f), 0.58, /*rightHanded=*/true };
    ClubSpec tc; tc.length = kTruthLc; tc.lieDeg = 62.5; tc.forwardLeanP7Deg = 5.0; tc.ballOffsetX = -0.05;
    RefConfig tcfg; tcfg.backswingPlaneOffsetDeg = -4.0;
    const std::unique_ptr<SwingReferenceModel> truth = makeSwingReferenceModel(ta, tc, tcfg);

    // Measured anchors = the truth model's P butt/head projected (conf 0.8).
    std::vector<SwingRefFitAnchor> anchors;
    for (int p = 1; p <= 8; ++p) {
        int seg; double ls; pSeg(p, seg, ls);
        const ShaftPose pose = truth->evaluate(static_cast<Segment>(seg), ls);
        SwingRefFitAnchor a;
        a.p      = p;
        a.conf   = 0.8f;
        a.gripPx = proj(pose.butt,               kOx, kOy, kS, kXsign);
        a.headPx = proj(pose.clubhead(kTruthLc), kOx, kOy, kS, kXsign);
        anchors.push_back(a);
    }

    // ── Fit input: perturbed seed (truth inside every bound) ─────────────────
    SwingRefFitInput in;
    in.seedAnthro  = GolferAnthro{ QVector3D(0.15f, -0.43f, 1.55f), 0.72, true };  // hubY fixed = truth
    in.ballOffsetX = -0.05;
    { ClubSpec sc; sc.length = 1.12; sc.lieDeg = 62.5; sc.forwardLeanP7Deg = 5.0; sc.ballOffsetX = -0.05; in.seedClub = sc; }
    in.cfg         = RefConfig{};          // backswingPlaneOffsetDeg default 4.0 → the +4 Δθ_bs seed
    in.anchors     = anchors;
    in.originPx    = QPointF(kOx, kOy);
    in.pxPerM      = kS;
    in.xSign       = kXsign;

    // ── 1. Synthetic recovery ────────────────────────────────────────────────
    std::printf("-- 1. Synthetic recovery: perturbed seed → truth arc --\n");
    const SwingRefFitResult r = fitSwingReference(in);
    CHECK("fitted", r.fitted);
    CHECK("anchorsUsed == 8", r.anchorsUsed == 8);
    std::printf("   rmsBefore=%.3f px  rmsAfter=%.3f px  arm=%.4f Lc=%.4f hubX=%.4f hubZ=%.4f dth=%.3f\n",
                r.rmsBeforePx, r.rmsAfterPx, r.anthro.armLength, r.clubLengthM,
                double(r.anthro.hub.x()), double(r.anthro.hub.z()), r.planeOffsetDeg);
    CHECK("rmsAfterPx <= 3", r.rmsAfterPx <= 3.0);
    CHECK("rmsAfterPx < rmsBeforePx/3", r.rmsBeforePx > 0.0 && r.rmsAfterPx < r.rmsBeforePx / 3.0);

    // Fitted model's projected P anchors within 8 px of truth (== the measured px).
    {
        ClubSpec fc = in.seedClub; fc.length = r.clubLengthM;
        RefConfig fcfg = in.cfg;   fcfg.backswingPlaneOffsetDeg = r.planeOffsetDeg;
        const std::unique_ptr<SwingReferenceModel> fm = makeSwingReferenceModel(r.anthro, fc, fcfg);
        double worst = 0.0;
        for (int p = 1; p <= 8; ++p) {
            int seg; double ls; pSeg(p, seg, ls);
            const ShaftPose pose = fm->evaluate(static_cast<Segment>(seg), ls);
            const QPointF g = proj(pose.butt,              kOx, kOy, kS, kXsign);
            const QPointF h = proj(pose.clubhead(r.clubLengthM), kOx, kOy, kS, kXsign);
            const SwingRefFitAnchor& t = anchors[std::size_t(p - 1)];
            worst = std::max(worst, std::hypot(g.x() - t.gripPx.x(), g.y() - t.gripPx.y()));
            worst = std::max(worst, std::hypot(h.x() - t.headPx.x(), h.y() - t.headPx.y()));
        }
        CHECK_NEAR("worst P grip/head reprojection (px)", worst, 0.0, 8.0);
    }

    // ── 2. Determinism ───────────────────────────────────────────────────────
    std::printf("\n-- 2. Determinism: two runs bit-identical --\n");
    {
        const SwingRefFitResult r2 = fitSwingReference(in);
        CHECK("fitted ==",        r2.fitted == r.fitted);
        CHECK("anchorsUsed ==",   r2.anchorsUsed == r.anchorsUsed);
        CHECK("rmsBeforePx ==",   r2.rmsBeforePx == r.rmsBeforePx);
        CHECK("rmsAfterPx ==",    r2.rmsAfterPx == r.rmsAfterPx);
        CHECK("armLength ==",     r2.anthro.armLength == r.anthro.armLength);
        CHECK("clubLengthM ==",   r2.clubLengthM == r.clubLengthM);
        CHECK("hub ==",           r2.anthro.hub == r.anthro.hub);
        CHECK("planeOffsetDeg ==", r2.planeOffsetDeg == r.planeOffsetDeg);
    }

    // ── 3. Degrade: < 4 distinct P anchors → seed unchanged ──────────────────
    std::printf("\n-- 3. Degrade: 3 anchors → fitted=false, seed unchanged --\n");
    {
        SwingRefFitInput d = in;
        d.anchors.assign(anchors.begin(), anchors.begin() + 3);   // P1, P2, P3
        const SwingRefFitResult dr = fitSwingReference(d);
        CHECK("fitted == false",            !dr.fitted);
        CHECK("seed hub unchanged",         dr.anthro.hub == in.seedAnthro.hub);
        CHECK("seed armLength unchanged",   dr.anthro.armLength == in.seedAnthro.armLength);
        CHECK("seed clubLength unchanged",  dr.clubLengthM == in.seedClub.length);
        CHECK("seed planeOffset unchanged", dr.planeOffsetDeg == in.cfg.backswingPlaneOffsetDeg);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
