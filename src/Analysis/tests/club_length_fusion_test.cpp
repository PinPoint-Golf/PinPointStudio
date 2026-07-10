// Standalone tests for the multi-estimator club-length fusion + persistent prior
// (src/Analysis/club_length_fusion.h). Pure scalar math, no fixture, no OpenCV.
// Synthetic candidate sets with hand-computable expectations.
//
//   cmake --build build/analyzer-tests --target club_length_fusion_test
//   ctest --test-dir build/analyzer-tests -R club_length_fusion --output-on-failure

#include "../club_length_fusion.h"

#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
// NB: the harness helpers are named check/approx — they must NOT collide with any
// caller-local; every test body below uses its own distinct locals.
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool approx(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static LengthCandidate cand(LengthSource s, double px) { return LengthCandidate{s, px, -1.0}; }

int main()
{
    const LengthFusionConfig cfg;   // validated-starting defaults
    const double frameH = 1000.0;

    // ── (1) two agreeing estimators fuse BETWEEN them with high conf ──────────
    std::printf("=== two agreeing ===\n");
    {
        std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 380.0),
                                           cand(LengthSource::Band, 390.0)};
        const LengthFused f = fuseClubLength(cs, /*poseBound=*/400.0, /*armFloor=*/0.0, frameH, 0, cfg);
        check(!f.abstained && f.nUsed == 2, "both survive (nUsed=2)");
        check(f.fusedPx > 380.0 && f.fusedPx < 390.0, "fused strictly between the two");
        check(f.conf > 0.7, "agreement ⇒ high conf");
        check(f.sigmaPx > 0.0, "posterior σ recorded");
    }

    // ── (2) three with one 30% outlier → outlier rejected (fused ≈ good pair) ─
    std::printf("=== leave-one-out outlier ===\n");
    {
        std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 380.0),
                                           cand(LengthSource::Band, 390.0),
                                           cand(LengthSource::Head, 500.0)};   // ~30% high
        // Pin the rejection threshold: this tests the LOO mechanism, not the
        // shipped default (the P6 re-fit widened outlierFrac past this fixture).
        LengthFusionConfig oc = cfg;
        oc.outlierFrac = 0.15;
        const LengthFused f = fuseClubLength(cs, 400.0, 0.0, frameH, 0, oc);
        check(f.nUsed == 2, "outlier dropped (2 instantaneous survive)");
        check(approx(f.fusedPx, 385.0, 0.05 * 385.0), "fused within 5% of the good pair (~385)");
        check(f.fusedPx < 400.0, "not dragged toward the 500 outlier");
    }

    // ── (3) single estimator → conf ≤ ~0.6 ───────────────────────────────────
    std::printf("=== single estimator ===\n");
    {
        std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 380.0)};
        const LengthFused f = fuseClubLength(cs, /*poseBound=*/0.0, 0.0, frameH, 0, cfg);
        check(!f.abstained && f.nUsed == 1, "one survivor");
        check(approx(f.fusedPx, 380.0, 1e-9), "fused = the lone estimate");
        check(f.conf <= 0.61 && f.conf >= 0.59, "single ⇒ conf ≈ 0.60 (confSupport floor)");
    }

    // ── (4) zero estimators → abstain (+ all-clipped ⇒ abstain) ───────────────
    std::printf("=== abstain ===\n");
    {
        const LengthFused f = fuseClubLength({}, 400.0, 0.0, frameH, 0, cfg);
        check(f.abstained && f.fusedPx < 0.0 && f.conf == 0.0 && f.nSurvivors == 0, "empty ⇒ abstain");
        // a lone candidate below the pose band is clipped ⇒ abstain
        std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 380.0)};
        const LengthFused g = fuseClubLength(cs, /*poseBound=*/1000.0, 0.0, frameH, 0, cfg);   // band ≥ 900
        check(g.abstained, "all candidates sanity-clipped ⇒ abstain");
    }

    // ── (5) conf monotonically DECREASES with spread ──────────────────────────
    std::printf("=== conf vs spread monotone ===\n");
    {
        double prev = 2.0;
        bool mono = true;
        for (double d : {0.0, 10.0, 20.0, 40.0}) {
            std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 380.0),
                                               cand(LengthSource::Band, 380.0 + d)};
            const LengthFused f = fuseClubLength(cs, 420.0, 0.0, frameH, 0, cfg);
            if (!(f.conf <= prev + 1e-12)) mono = false;
            prev = f.conf;
        }
        check(mono, "conf non-increasing as the two estimates spread apart");
        // strict at the extremes
        std::vector<LengthCandidate> tight = {cand(LengthSource::Ball, 380.0), cand(LengthSource::Band, 382.0)};
        std::vector<LengthCandidate> wide  = {cand(LengthSource::Ball, 380.0), cand(LengthSource::Band, 420.0)};
        check(fuseClubLength(tight, 420.0, 0.0, frameH, 0, cfg).conf
            > fuseClubLength(wide,  420.0, 0.0, frameH, 0, cfg).conf, "tight conf > wide conf");
    }

    // ── (6) pose-bound + frame-height + arm-floor sanity clipping ─────────────
    std::printf("=== sanity clipping ===\n");
    {
        // 250 is below 0.9·poseBound(400)=360 ⇒ clipped; 380 survives, fused not dragged
        std::vector<LengthCandidate> lo = {cand(LengthSource::Ball, 380.0), cand(LengthSource::Band, 250.0)};
        const LengthFused fl = fuseClubLength(lo, 400.0, 0.0, frameH, 0, cfg);
        check(fl.nUsed == 1 && approx(fl.fusedPx, 380.0, 1e-9), "below-band candidate clipped");
        // 1000 is above 2.2·poseBound(400)=880 ⇒ clipped
        std::vector<LengthCandidate> hi = {cand(LengthSource::Ball, 380.0), cand(LengthSource::Band, 1000.0)};
        check(fuseClubLength(hi, 400.0, 0.0, frameH, 0, cfg).nUsed == 1, "above-band candidate clipped");
        // frame-height clamp: 0.62·1000 = 620; 700 clipped with no pose band
        std::vector<LengthCandidate> fh = {cand(LengthSource::Ball, 500.0), cand(LengthSource::Band, 700.0)};
        check(fuseClubLength(fh, 0.0, 0.0, frameH, 0, cfg).nUsed == 1, "frameHFrac clamp drops 700");
        // arm floor lower clamp: armFloor 450 drops a 380 candidate
        std::vector<LengthCandidate> af = {cand(LengthSource::Ball, 380.0), cand(LengthSource::Band, 500.0)};
        check(fuseClubLength(af, 0.0, /*armFloor=*/450.0, frameH, 0, cfg).nUsed == 1, "armFloor clamp drops 380");
    }

    // ── (7) prior EMA converges over 10 updates (monotone approach) ───────────
    std::printf("=== prior EMA convergence ===\n");
    {
        LengthPriorState st;
        updateLengthPrior(st, 340.0, 0.6, cfg);          // seed
        check(approx(st.emaPx, 340.0, 1e-9) && st.n == 1, "seed takes the first value");
        double last = st.emaPx;
        bool increasing = true;
        for (int k = 0; k < 10; ++k) {
            updateLengthPrior(st, 380.0, 0.6, cfg);
            if (!(st.emaPx > last - 1e-9)) increasing = false;
            last = st.emaPx;
        }
        check(increasing, "EMA monotonically approaches the target");
        check(st.emaPx > 375.0 && st.emaPx < 380.0, "converged toward 380 without overshoot");
        check(st.n > 1 && st.varPx > 0.0, "n grows + EW variance tracked");
        // a below-conf update is a no-op
        LengthPriorState pre = st;
        updateLengthPrior(st, 500.0, 0.4, cfg);          // conf < updateConfMin
        check(st.emaPx == pre.emaPx && st.n == pre.n, "low-conf update ignored");
    }

    // ── (8) kσ gate + reset-after-3 (silent camera-move self-heal) ────────────
    std::printf("=== prior kσ gate + reset ===\n");
    {
        LengthPriorState st;
        updateLengthPrior(st, 380.0, 0.6, cfg);          // seed, tight σ ≈ 22.8
        updateLengthPrior(st, 250.0, 0.6, cfg);          // |Δ|=130 > 3σ ⇒ disagree 1
        check(approx(st.emaPx, 380.0, 1e-9) && st.disagreeRun == 1, "1st disagreement gated out");
        updateLengthPrior(st, 250.0, 0.6, cfg);          // disagree 2
        check(approx(st.emaPx, 380.0, 1e-9) && st.disagreeRun == 2, "2nd disagreement gated out");
        updateLengthPrior(st, 250.0, 0.6, cfg);          // disagree 3 ⇒ RESET
        check(approx(st.emaPx, 250.0, 1e-9) && st.n == 1 && st.disagreeRun == 0, "3rd ⇒ reset to new value");

        // an in-gate value clears a partial disagree run (no premature reset)
        LengthPriorState s2;
        updateLengthPrior(s2, 380.0, 0.6, cfg);
        updateLengthPrior(s2, 250.0, 0.6, cfg);          // disagree 1
        check(s2.disagreeRun == 1, "one disagreement pending");
        updateLengthPrior(s2, 385.0, 0.6, cfg);          // in-gate ⇒ clears run + EMA moves
        check(s2.disagreeRun == 0 && s2.emaPx > 380.0, "in-gate value clears the run + updates EMA");
    }

    // ── (9) determinism: identical inputs ⇒ identical outputs ─────────────────
    std::printf("=== determinism ===\n");
    {
        std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 381.0),
                                           cand(LengthSource::Band, 397.0),
                                           cand(LengthSource::Head, 372.0)};
        const LengthFused a = fuseClubLength(cs, 405.0, 300.0, frameH, 3, cfg);
        const LengthFused b = fuseClubLength(cs, 405.0, 300.0, frameH, 3, cfg);
        check(a.fusedPx == b.fusedPx && a.conf == b.conf && a.sigmaPx == b.sigmaPx
              && a.nUsed == b.nUsed && a.spread == b.spread, "fuseClubLength bit-identical twice");

        LengthPriorState p1, p2;
        for (int k = 0; k < 6; ++k) {
            updateLengthPrior(p1, 380.0 + k, 0.7, cfg);
            updateLengthPrior(p2, 380.0 + k, 0.7, cfg);
        }
        check(p1.emaPx == p2.emaPx && p1.varPx == p2.varPx && p1.n == p2.n
              && p1.disagreeRun == p2.disagreeRun, "updateLengthPrior bit-identical twice");
    }

    // ── (10) prior joins the fuse + confSupport rewards priorN ────────────────
    std::printf("=== prior participation ===\n");
    {
        // ball + a matured prior: prior tightens the posterior + lifts confSupport
        std::vector<LengthCandidate> cs = {cand(LengthSource::Ball, 380.0),
                                           LengthCandidate{LengthSource::Prior, 384.0, 12.0}};
        const LengthFused f = fuseClubLength(cs, 400.0, 0.0, frameH, /*priorN=*/4, cfg);
        check(f.nUsed == 1 && f.nSurvivors == 2, "prior survives but is not counted as instantaneous");
        check(f.conf > 0.85, "priorN lifts confSupport (ball alone would be 0.60)");
        // prior-only fusion still produces a value (cold session, strong prior)
        std::vector<LengthCandidate> only = {LengthCandidate{LengthSource::Prior, 400.0, 15.0}};
        const LengthFused p = fuseClubLength(only, 420.0, 0.0, frameH, 4, cfg);
        check(!p.abstained && approx(p.fusedPx, 400.0, 1e-9) && p.conf >= cfg.ladderConfMin,
              "prior-only fusion drives a usable length");
    }

    // ── (11) fromOverrides applies fusion.* keys ──────────────────────────────
    std::printf("=== fromOverrides ===\n");
    {
        QVariantMap ov;
        ov["fusion.enabled"] = false;
        ov["fusion.sigFracBall"] = 0.02;
        ov["fusion.ladderConfMin"] = 0.5;
        ov["fusion.headMinMeas"] = 8;
        const LengthFusionConfig c = LengthFusionConfig::fromOverrides(ov);
        check(c.enabled == false, "fusion.enabled overridden");
        check(approx(c.sigFracBall, 0.02, 1e-12), "fusion.sigFracBall overridden");
        check(approx(c.ladderConfMin, 0.5, 1e-12), "fusion.ladderConfMin overridden");
        check(c.headMinMeas == 8, "fusion.headMinMeas overridden");
        check(approx(c.sigFracBand, LengthFusionConfig{}.sigFracBand, 1e-12),
              "untouched key keeps its default");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
