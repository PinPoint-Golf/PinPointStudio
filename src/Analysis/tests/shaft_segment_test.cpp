// E4 steel-segment lock (src/Analysis/shaft_tracker_math — rayProfile +
// segmentLock), docs/design/markerless_club_tracker_design.md §4.2.
//
// Synthetic clubs drawn at a known px/mm scale s with the lab 7-iron's geometry
// (grip end 265 mm, hosel 882 mm, length 940 mm from the butt): a dark grip, a
// bright thin steel run, a dark ferrule gap, a wide bright head. The engine must
// recover (θ, s, r0) from the run's ENDS, survive the polarity flip over a blown
// mat, refuse the counterfeits (wide bar, off-frame terminus, wrong direction),
// honour the length and scale gates, use bands as extra landmarks when drawn,
// and fall back to Terminus mode when the hands bloom over the onset.
//
//   cmake --build build/tests --target shaft_segment_test -j 8
//   ctest --test-dir build/tests -R shaft_segment --output-on-failure

#include "../shaft_tracker_math.h"

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

static constexpr double kPi = 3.14159265358979323846;

static SegmentGeom lab7iron(bool bands = false)
{
    SegmentGeom g;
    g.gripEndMm = 265.0; g.hoselMm = 882.0; g.clubLenMm = 940.0;
    if (bands) g.bandsMm = {308, 362, 560, 758, 808, 854};
    return g;
}

struct ClubDraw {
    double s      = 0.30;   // px/mm
    double r0     = 200.0;  // butt→anchor (mm): the anchor sits 200 mm down the grip
    double thDeg  = 35.0;
    int    bg     = 40;
    int    grip   = 30;
    int    steel  = 200;
    int    head   = 230;
    double ferruleMm = 12.0;
    bool   hands  = false;  // white bloom over the anchor (glove + hands)
    bool   bands  = false;  // 25 mm saturated bands at the lab positions
    bool   blownBelow = false;   // rows below the anchor are a blown mat (254)
    bool   noHead = false;
};

struct Scene { cv::Mat g32; double gx, gy; };

static Scene drawClub(const ClubDraw& d, int W = 800, int H = 640)
{
    cv::Mat g8(H, W, CV_8UC1, cv::Scalar(d.bg));
    const double gx = 260.0, gy = 250.0;
    if (d.blownBelow) g8(cv::Rect(0, int(gy) + 60, W, H - int(gy) - 60)).setTo(cv::Scalar(254));
    const double th = d.thDeg * kPi / 180.0, ux = std::cos(th), uy = std::sin(th);
    auto P = [&](double mm) { const double r = d.s * (mm - d.r0); return cv::Point2d(gx + ux * r, gy + uy * r); };
    auto seg = [&](double m0, double m1, int val, int thick) {
        cv::line(g8, P(m0), P(m1), cv::Scalar(val), thick, cv::LINE_8);
    };
    const SegmentGeom geo = lab7iron();
    seg(0.0, geo.gripEndMm, d.grip, 9);                              // grip: dark, fat
    seg(geo.gripEndMm, geo.hoselMm - d.ferruleMm, d.steel, 4);       // exposed steel: thin, bright
    seg(geo.hoselMm - d.ferruleMm, geo.hoselMm, d.grip, 5);          // ferrule: dark gap
    if (!d.noHead) {
        seg(geo.hoselMm, geo.hoselMm + 40.0, d.head, 4);             // hosel: thin chrome, 40 mm
        const double headR = 16.0;                                   // wide chrome head beyond the hosel
        cv::Point2d hc = P(geo.hoselMm + 40.0 + headR / d.s + 2.0);
        cv::circle(g8, cv::Point(int(hc.x), int(hc.y)), int(headR), cv::Scalar(d.head), -1);
    }
    if (d.bands)
        for (double m : lab7iron(true).bandsMm) seg(m - 12.5, m + 12.5, 255, 5);
    if (d.hands) cv::circle(g8, cv::Point(int(gx), int(gy)), 26, cv::Scalar(255), -1);
    Scene sc; g8.convertTo(sc.g32, CV_32F); sc.gx = gx; sc.gy = gy;
    return sc;
}

static SegmentLock lock(const Scene& sc, const ClubDraw& d, const SegmentGeom& geo,
                        double sPrior = 0.0, double lenPriorPx = 0.0, double dThetaDeg = 0.0,
                        SegmentConfig cfg = SegmentConfig{})
{
    const double th = (d.thDeg + dThetaDeg) * kPi / 180.0;
    return segmentLock(sc.g32, sc.gx, sc.gy, th, 0.62 * sc.g32.rows, geo, cfg, RidgeConfig{}, sPrior, lenPriorPx);
}

static void report(const SegmentLock& L, const char* tag)
{
    std::printf("    %s: ok=%d mode=%d n=%d s=%.3f r0=%.1f rG=%.0f rF=%.0f sup=%.2f distal=%d rms=%.2f\n",
                tag, L.ok ? 1 : 0, int(L.mode), L.n, L.s, L.r0, L.rG, L.rF, L.support, L.distal, L.rms);
}

int main()
{
    // ── 0. shared sampler pin: rayProfile at E2's grid reproduces ridgeSweep ─
    std::printf("=== rayProfile ≡ ridgeSweep per-sample evidence (bit-for-bit) ===\n");
    {
        ClubDraw d; const Scene sc = drawClub(d);
        RidgeConfig rc;
        std::vector<float> thetas(360);
        for (int i = 0; i < 360; ++i) thetas[size_t(i)] = float(i * kPi / 180.0);
        const RidgeResult rs = ridgeSweep(sc.g32, sc.gx, sc.gy, thetas, rc, false);
        const int j0 = int(rc.minLenPx / rc.rStep);
        bool same = true; int bad = -1;
        for (int i = 0; i < 360 && same; ++i) {
            const RayProfile P = rayProfile(sc.g32, sc.gx, sc.gy, double(thetas[size_t(i)]), rc.rLo, rc.rHi, rc.rStep, rc);
            double cum = 0.0, best = -1e300;
            for (int j = 0; j < int(P.e.size()); ++j) {
                cum += double(P.e[size_t(j)]);
                if (j >= j0) best = std::max(best, cum / std::sqrt(double(j) + 8.0));
            }
            if (float(best) != rs.score[size_t(i)]) { same = false; bad = i; }
        }
        check(same, "ridge score recomputed from rayProfile e[] equals ridgeSweep for all 360 θ");
        if (!same) std::printf("    first mismatch at θ=%d\n", bad);
    }

    // ── 1. FULL lock on a bare steel shaft, bright on dark ───────────────────
    std::printf("=== FULL lock: bare steel, bright on dark ===\n");
    {
        ClubDraw d; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron());
        report(L, "full");
        check(L.ok, "locks");
        check(L.mode == SegmentMode::Full, "mode FULL");
        check(L.n == 2, "two landmarks (no bands drawn)");
        check(std::abs(L.s - d.s) <= 0.04 * d.s, "s within 4% of drawn scale");
        check(std::abs(L.r0 - d.r0) <= 20.0, "r0 within 20 mm of drawn anchor offset");
        check(L.distal == 1, "ferrule resolved (dark gap before the hosel)");
        check(std::abs(L.rF - d.s * (870.0 - d.r0)) <= 3.0f, "terminus within 3 px of the steel's end");
        check(L.support >= 0.9, "support ≥ 0.9");
        // determinism
        const SegmentLock L2 = lock(sc, d, lab7iron());
        check(L2.s == L.s && L2.r0 == L.r0 && L2.rF == L.rF, "deterministic rerun");
    }

    // ── 2. hands bloom over the onset: Terminus mode needs a scale prior ─────
    std::printf("=== hands bloom: onset hidden ===\n");
    {
        ClubDraw d; d.hands = true; const Scene sc = drawClub(d);
        const SegmentLock L0 = lock(sc, d, lab7iron());
        report(L0, "no prior");
        check(!L0.ok, "no onset and no scale prior ⇒ no lock");
        const SegmentLock L1 = lock(sc, d, lab7iron(), d.s);
        report(L1, "with sPrior");
        check(L1.ok && L1.mode == SegmentMode::Terminus, "with sPrior ⇒ TERMINUS lock");
        check(L1.ok && std::abs(L1.r0 - d.r0) <= 25.0, "terminus r0 within 25 mm");
        check(L1.rG < 0.f, "onset reported unresolved");
    }

    // ── 3. polarity flip: bright over dark, then dark over the blown mat ─────
    std::printf("=== polarity flip along the run (blown mat below) ===\n");
    {
        ClubDraw d; d.thDeg = 75.0; d.blownBelow = true; d.head = 120; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron());
        report(L, "flip");
        check(L.ok && L.mode == SegmentMode::Full, "FULL lock across the polarity flip");
        check(L.ok && std::abs(L.s - d.s) <= 0.05 * d.s, "s within 5% across the flip");
    }

    // ── 4. counterfeits ──────────────────────────────────────────────────────
    std::printf("=== counterfeits ===\n");
    {
        // a forearm: a 28 px wide bright bar along the ray, no thin line
        cv::Mat g8(640, 800, CV_8UC1, cv::Scalar(40));
        const double gx = 260, gy = 250, th = 35.0 * kPi / 180.0;
        cv::line(g8, cv::Point(int(gx + 20 * std::cos(th)), int(gy + 20 * std::sin(th))),
                 cv::Point(int(gx + 260 * std::cos(th)), int(gy + 260 * std::sin(th))), cv::Scalar(200), 28, cv::LINE_8);
        cv::Mat g32; g8.convertTo(g32, CV_32F);
        const SegmentLock L = segmentLock(g32, gx, gy, th, 0.62 * 640, lab7iron(), SegmentConfig{}, RidgeConfig{});
        report(L, "forearm");
        check(!L.ok, "wide bright bar (forearm) does not lock");
    }
    {
        // wrong direction: 8° off the drawn shaft — the run is too short
        ClubDraw d; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron(), 0.0, 0.0, 8.0);
        report(L, "off-axis");
        check(!L.ok, "8° off-axis probe does not lock");
    }
    {
        // terminus off-frame: a big scale pushes the hosel past the image edge
        ClubDraw d; d.s = 0.75; d.thDeg = 10.0; const Scene sc = drawClub(d);
        SegmentConfig cfg; cfg.sMax = 0.9f;
        const SegmentLock L = lock(sc, d, lab7iron(), 0.0, 0.0, 0.0, cfg);
        report(L, "off-frame");
        check(!L.ok, "run leaving the frame has no terminus ⇒ no lock");
    }
    {
        // no head and no dark end: the steel just stops at the frame's search radius
        ClubDraw d; d.noHead = true; d.ferruleMm = 0.0; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron());
        report(L, "no-head");
        // a bright→dark end IS a landmark (distal 3); it must still lock, at hosel tolerance
        check(L.ok && L.distal == 3, "bright→dark end locks as a dark-end terminus");
    }

    // ── 5. gates ─────────────────────────────────────────────────────────────
    std::printf("=== length and scale gates ===\n");
    {
        ClubDraw d; const Scene sc = drawClub(d);
        const double Ltrue = d.s * (940.0 - d.r0);
        check(lock(sc, d, lab7iron(), 0.0, Ltrue).ok, "length prior at the true length passes");
        check(!lock(sc, d, lab7iron(), 0.0, 0.5 * Ltrue).ok, "length prior at half the true length refuses");
        check(lock(sc, d, lab7iron(), d.s).ok, "scale prior at the true scale passes (FULL)");
        check(!lock(sc, d, lab7iron(), 2.0 * d.s).ok, "scale prior at 2× refuses a FULL fit");
    }

    // ── 6. bands as extra landmarks ──────────────────────────────────────────
    std::printf("=== marked club: bands join the fit ===\n");
    {
        ClubDraw d; d.bands = true; d.steel = 150; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron(true));
        report(L, "bands");
        check(L.ok && L.mode == SegmentMode::Full, "FULL lock on the marked club");
        check(L.ok && L.n >= 6, "≥ 4 bands joined the two ends");
        check(L.ok && L.rms <= 3.0f, "joint fit RMS ≤ 3 px");
        check(L.ok && std::abs(L.s - d.s) <= 0.03 * d.s, "s within 3% with bands");
        // the same image with an unmarked geometry still locks on the ends alone
        const SegmentLock U = lock(sc, d, lab7iron(false));
        check(U.ok && U.n == 2, "bands ignored when the record says unmarked; ends still lock");
    }

    // ── 7. foreshortening range ─────────────────────────────────────────────
    std::printf("=== foreshortened scales ===\n");
    for (double s : {0.16, 0.20, 0.45}) {
        ClubDraw d; d.s = s; d.thDeg = 300.0; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron());
        char buf[96]; std::snprintf(buf, sizeof buf, "s=%.2f px/mm: FULL lock, s within 5%%", s);
        report(L, buf);
        check(L.ok && L.mode == SegmentMode::Full && std::abs(L.s - s) <= 0.05 * s, buf);
    }

    {
        // heavily foreshortened: the visible grip is < 8 px, so FULL is impossible
        // and Terminus with a scale prior is the honest result
        ClubDraw d; d.s = 0.12; d.thDeg = 300.0; const Scene sc = drawClub(d);
        const SegmentLock L = lock(sc, d, lab7iron(), d.s);
        report(L, "s=0.12 with prior");
        check(L.ok && L.mode == SegmentMode::Terminus, "s=0.12: Terminus lock with a scale prior");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
