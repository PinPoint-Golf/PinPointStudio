// Standalone tests for the late-pipeline event refinement engine
// (src/Analysis/event_refine): the three-tier at-ball evidence, the LAST-
// departure/no-return takeaway (the s0002 lesson — last-departure where first-
// departure fails), run debounce, the tier confidence ladder + minConf/maxShift
// abstention, the Club-provenance/version-3 mutation, the monotone-ladder
// invariant, the enabled=false byte-identity contract, and the swingStartUs
// coupling. Synthetic tracks with hand-computable expectations — no fixture, no
// decode. (Key plumbing lives in tuning_overrides_test; slot order + canRun skip
// in analysis_stage_test's generic orchestrator coverage — the EventRefineStage
// glue is file-local in wrist_analyzer.cpp.)
//
//   cmake --build build/analyzer-tests --target event_refine_test
//   ctest --test-dir build/analyzer-tests -R event_refine --output-on-failure

#include "../event_refine.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static constexpr double kPi = 3.14159265358979323846;
static constexpr double ATB = kPi / 2.0;          // club points at the ball ⇒ angDiff(θ,θ_ball)=0
static constexpr double DEP = kPi / 2.0 - 0.7;    // 0.7 rad ≈ 40° away ⇒ departed (> departThetaDeg 25)
static constexpr double DT  = 10000.0;            // 10 ms frame spacing
static constexpr int    W = 1000, H = 1000;

static int64_t T(int i) { return int64_t(i) * int64_t(DT); }

// ── synthetic builders ──────────────────────────────────────────────────────
static void addSample(ShaftTrack2D &s, int i, double gxp, double gyp, double th, uint16_t flags)
{
    ShaftSample2D sm;
    sm.t_us = T(i); sm.gripPx = QPointF(gxp, gyp); sm.thetaRad = th; sm.conf = 1.f; sm.flags = flags;
    s.samples.push_back(sm);
}
static void addBall(BallTrack2D &b, int i, double nx, double ny, float act)
{
    BallSample2D s;
    s.t_us = T(i); s.found = true; s.center = QPointF(nx, ny); s.radiusNorm = 0.02f; s.conf = 1.f;
    s.clubActivity = act;
    b.frames.push_back(s);
}
static void addEvent(Segmentation &seg, Phase p, int i)
{
    PhaseEvent e; e.phase = p; e.t_us = T(i); e.conf = 0.5f; seg.events.push_back(e);
}

// All-measured, grip fixed at (500,500) (θ_ball = +90° for a fixed ball at
// (0.5,0.8)); θ = ATB where atBall(i), else DEP. Covers scenarios 1–4.
static ShaftTrack2D buildShaftMeasured(int nf, const std::function<bool(int)> &atBall, int addrFrame)
{
    ShaftTrack2D s;
    s.frameWidth = W; s.frameHeight = H; s.valid = true;
    s.addressPhaseFrame = addrFrame; s.onsetFloorFrame = -1;
    for (int i = 0; i < nf; ++i)
        addSample(s, i, 500, 500, atBall(i) ? ATB : DEP, ShaftMeasured);
    ShaftPosition p1; p1.p = 1; p1.gripPx = QPointF(500, 500); s.positions.push_back(p1);
    return s;
}
static BallTrack2D buildBall(int nf, float act = -1.f)
{
    BallTrack2D b;
    for (int i = 0; i < nf; ++i) addBall(b, i, 0.5, 0.8, act);
    return b;
}
// Address a / Takeaway t / Top p / Impact m / Finish f frame indices; swingStart s.
static Segmentation buildSeg(int a, int t, int p, int m, int f, int s)
{
    Segmentation seg; seg.conf = 0.5f; seg.version = 2; seg.swingStartUs = T(s);
    addEvent(seg, Phase::Address, a); addEvent(seg, Phase::Takeaway, t);
    addEvent(seg, Phase::Top, p);     addEvent(seg, Phase::Impact, m);
    addEvent(seg, Phase::Finish, f);
    return seg;
}
static const PhaseEvent *ev(const Segmentation &seg, Phase p) { return seg.eventFor(p); }
static const ShaftPosition *p1Of(const ShaftTrack2D &s)
{
    for (const ShaftPosition &p : s.positions)
        if (p.p == 1) return &p;
    return nullptr;
}

int main()
{
    // Test config: enabled (the 2026-07-18 frozen default), minConf PINNED at the
    // pre-freeze 0.5 — the tier-ladder scenarios exercise a Tier-B apply (base 0.65
    // + corroboration 0.75), deliberately below the frozen 0.8 production floor
    // (which is asserted in tuning_overrides_test / tuned_constants_parity_test).
    // Other knobs default (depart 25, hold 200 ms, maxShift 3 s).
    EventRefineConfig on;  on.enabled = true;  on.minConf = 0.5;

    // ── 1. Clean departure → Club provenance + version 3 ─────────────────────
    std::printf("=== 1. clean departure ===\n");
    {
        const int nf = 40;
        ShaftTrack2D shaft = buildShaftMeasured(nf, [](int i){ return i <= 24; }, /*addr=*/24);
        BallTrack2D  ball  = buildBall(nf);
        Segmentation seg   = buildSeg(/*A*/20, /*T*/25, /*Top*/35, /*Imp*/38, /*Fin*/39, /*start*/15);

        const EventRefineResult r = refineEvents(seg, shaft, ball, /*impactUs=*/380000, on);
        check(r.refined && r.takeawayRefined && r.addressRefined, "refined both Address + Takeaway");
        check(seg.version == 3, "version bumped to 3");
        check(ev(seg, Phase::Address)->provenance == SegmentRole::Club
              && ev(seg, Phase::Takeaway)->provenance == SegmentRole::Club, "provenance = Club");
        check(r.departFrame == 24, "L = last at-ball frame (24)");
        check(ev(seg, Phase::Address)->t_us == T(24), "refined Address = hold end (frame 24)");
        check(ev(seg, Phase::Takeaway)->t_us > T(24) && ev(seg, Phase::Takeaway)->t_us < T(25),
              "refined Takeaway = sub-frame L→departure crossing");
        check(ev(seg, Phase::Address)->t_us <= ev(seg, Phase::Takeaway)->t_us
              && ev(seg, Phase::Takeaway)->t_us <= ev(seg, Phase::Top)->t_us, "ladder stays monotone");

        // P1 (club.positions) follows the refined Address — re-derived off the
        // refined frame's track sample and downgraded to a plain TrackSample.
        const ShaftPosition *p1 = p1Of(shaft);
        check(r.p1Synced && p1 && p1->t_us == ev(seg, Phase::Address)->t_us,
              "P1 position re-timed to the refined Address (frame 24)");
        check(p1 && p1->gripPx == shaft.samples[24].gripPx
              && p1->thetaRad == shaft.samples[24].thetaRad && p1->conf == shaft.samples[24].conf,
              "P1 geometry re-derived from the refined frame's track sample");
        check(p1 && p1->source == uint8_t(PositionSource::TrackSample)
              && p1->sigmaThetaDeg == -1.f && p1->sigmaLenPx == -1.f && p1->stackN == 0,
              "P1 downgraded to TrackSample (fit cleared) at the new anchor");
    }

    // ── 2. Fidget-requiet swing (s0002 shape): last-departure, not first ──────
    std::printf("=== 2. fidget-requiet (last-departure) ===\n");
    {
        // settle A [0,24] at-ball, fidget [25,34] away, settle B [35,59] at-ball,
        // real takeaway [60,79] away. A FIRST-departure tk0 fires at settle A's end
        // (~24); last-departure must fire at settle B's end (59).
        const int nf = 80;
        auto atBall = [](int i){ return (i <= 24) || (i >= 35 && i <= 59); };
        ShaftTrack2D shaft = buildShaftMeasured(nf, atBall, /*addr=*/59);
        BallTrack2D  ball  = buildBall(nf);
        Segmentation seg   = buildSeg(/*A*/58, /*T*/60, /*Top*/72, /*Imp*/76, /*Fin*/79, /*start*/50);

        const EventRefineResult r = refineEvents(seg, shaft, ball, -1, on);
        check(r.departFrame == 59, "L = end of the LAST at-ball run (59), not the fidget's first departure (24)");
        check(r.takeawayUs > T(59) && r.takeawayUs < T(60), "Takeaway at the final departure, not the fidget");
    }

    // ── 3. Oscillating waggle → every genuine return handled, last wins ───────
    std::printf("=== 3. oscillating waggle ===\n");
    {
        const int nf = 110;
        auto atBall = [](int i){ return (i <= 24) || (i >= 35 && i <= 59) || (i >= 70 && i <= 94); };
        ShaftTrack2D shaft = buildShaftMeasured(nf, atBall, /*addr=*/94);
        BallTrack2D  ball  = buildBall(nf);
        Segmentation seg   = buildSeg(/*A*/93, /*T*/95, /*Top*/105, /*Imp*/108, /*Fin*/109, /*start*/85);

        const EventRefineResult r = refineEvents(seg, shaft, ball, -1, on);
        check(r.departFrame == 94, "three genuine at-ball runs → L = last run end (94)");
    }

    // ── 4. Sub-returnHoldMs flicker ≠ return (debounced out) ──────────────────
    std::printf("=== 4. flicker debounce ===\n");
    {
        // hold [0,24] at-ball, depart [25,29], 3-frame flicker [30,32] (30 ms <
        // returnHoldMs 200 ms), depart [33,49]. The flicker must NOT move L.
        const int nf = 50;
        auto atBall = [](int i){ return (i <= 24) || (i >= 30 && i <= 32); };
        ShaftTrack2D shaft = buildShaftMeasured(nf, atBall, /*addr=*/24);
        BallTrack2D  ball  = buildBall(nf);
        Segmentation seg   = buildSeg(/*A*/20, /*T*/25, /*Top*/45, /*Imp*/48, /*Fin*/49, /*start*/15);

        const EventRefineResult r = refineEvents(seg, shaft, ball, -1, on);
        check(r.departFrame == 24, "sub-returnHoldMs flicker debounced — L stays at the genuine hold end (24)");
    }

    // ── 5. Tier ladder ───────────────────────────────────────────────────────
    std::printf("=== 5. tier ladder ===\n");
    {
        // 5a. no ball ⇒ Tier C (grip radius) ⇒ conf 0.45 < minConf ⇒ untouched.
        const int nf = 40;
        ShaftTrack2D s;
        s.frameWidth = W; s.frameHeight = H; s.valid = true; s.addressPhaseFrame = 24; s.onsetFloorFrame = -1;
        for (int i = 0; i < nf; ++i)
            addSample(s, i, i <= 24 ? 500 : 700, 500, ATB, ShaftMeasured);   // grip leaves P1 at takeaway
        ShaftPosition p1; p1.p = 1; p1.gripPx = QPointF(500, 500); s.positions.push_back(p1);
        BallTrack2D noBall;
        Segmentation seg = buildSeg(20, 25, 35, 38, 39, 15);
        const EventRefineResult r = refineEvents(seg, s, noBall, -1, on);
        check(!r.refined && seg.version == 2 && r.tier == 1,
              "no-ball ⇒ Tier C ⇒ conf < minConf ⇒ untouched, version stays 2");
        check(ev(seg, Phase::Address)->t_us == T(20) && ev(seg, Phase::Takeaway)->t_us == T(25),
              "5a: events untouched");

        // 5b. activity-only: coasted samples (no Tier A), club-quiet at address.
        ShaftTrack2D sb;
        sb.frameWidth = W; sb.frameHeight = H; sb.valid = true; sb.addressPhaseFrame = 24; sb.onsetFloorFrame = -1;
        for (int i = 0; i < nf; ++i) addSample(sb, i, 500, 500, ATB, ShaftCoasted);   // NOT measured
        ShaftPosition p1b; p1b.p = 1; p1b.gripPx = QPointF(500, 500); sb.positions.push_back(p1b);
        BallTrack2D ballB;
        for (int i = 0; i < nf; ++i) addBall(ballB, i, 0.5, 0.8, i <= 24 ? 1.0f : 5.0f);   // quiet then active
        Segmentation segB = buildSeg(20, 25, 35, 38, 39, 15);
        const EventRefineResult rb = refineEvents(segB, sb, ballB, -1, on);
        check(rb.refined && rb.tier == 2 && segB.version == 3, "activity-only ⇒ Tier B decides, applied");

        // 5c. θ-only: measured samples, no activity ⇒ Tier A.
        ShaftTrack2D sc = buildShaftMeasured(nf, [](int i){ return i <= 24; }, 24);
        BallTrack2D  ballC = buildBall(nf);   // clubActivity -1
        Segmentation segC = buildSeg(20, 25, 35, 38, 39, 15);
        const EventRefineResult rc = refineEvents(segC, sc, ballC, -1, on);
        check(rc.refined && rc.tier == 3 && segC.version == 3, "θ-only ⇒ Tier A decides, applied");

        // 5d. BallAnchored exclusion from Tier A: anchored hold frames assert
        //     at-ball DIRECTLY even with a wrong stored θ (θ = +90°+1.5 rad).
        auto buildAnchoredHold = [&](uint16_t holdFlags) {
            ShaftTrack2D sd;
            // addressPhaseFrame = -1 so the tk0-style adaptive floor stays at
            // departThetaDeg (25°) — otherwise the deliberately-WRONG hold θ at bs0
            // would inflate effDepart and mask the distance test we're isolating.
            sd.frameWidth = W; sd.frameHeight = H; sd.valid = true; sd.addressPhaseFrame = -1; sd.onsetFloorFrame = -1;
            for (int i = 0; i < nf; ++i) {
                if (i <= 24) addSample(sd, i, 500, 500, ATB + 1.5, holdFlags);       // wrong θ, hold
                else         addSample(sd, i, 500, 500, DEP, ShaftMeasured);         // departed
            }
            ShaftPosition p; p.p = 1; p.gripPx = QPointF(500, 500); sd.positions.push_back(p);
            return sd;
        };
        BallTrack2D ballD = buildBall(nf);
        Segmentation segAnc = buildSeg(20, 25, 35, 38, 39, 15);
        ShaftTrack2D sAnc = buildAnchoredHold(ShaftBallAnchored);
        const EventRefineResult rAnc = refineEvents(segAnc, sAnc, ballD, -1, on);
        check(rAnc.refined && rAnc.departFrame == 24 && rAnc.tier == 3,
              "ShaftBallAnchored hold asserts at-ball (excluded from the θ distance test)");
        Segmentation segMeas = buildSeg(20, 25, 35, 38, 39, 15);
        ShaftTrack2D sMeas = buildAnchoredHold(ShaftMeasured);
        const EventRefineResult rMeas = refineEvents(segMeas, sMeas, ballD, -1, on);
        check(!rMeas.refined && rMeas.departFrame < 0 && segMeas.version == 2,
              "same wrong-θ hold as ShaftMeasured fails the distance test ⇒ abstain (contrast)");
    }

    // ── 6. Abstain: no-Top / inversion / maxShift ────────────────────────────
    std::printf("=== 6. abstain cases ===\n");
    {
        const int nf = 40;
        ShaftTrack2D shaft = buildShaftMeasured(nf, [](int i){ return i <= 24; }, 24);
        BallTrack2D  ball  = buildBall(nf);

        // 6a. no Top event ⇒ abstain.
        Segmentation noTop; noTop.conf = 0.5f; noTop.version = 2;
        addEvent(noTop, Phase::Address, 20); addEvent(noTop, Phase::Takeaway, 25);
        addEvent(noTop, Phase::Impact, 38);  addEvent(noTop, Phase::Finish, 39);
        const EventRefineResult rNoTop = refineEvents(noTop, shaft, ball, -1, on);
        check(!rNoTop.refined && noTop.version == 2, "no Top landmark ⇒ abstain");

        // 6b. inversion: with only Address refined and an OLD Takeaway sitting
        //     BEFORE the refined Address, addrUs > addrHi ⇒ Address abstains.
        EventRefineConfig addrOnly = on; addrOnly.takeaway = false;
        Segmentation segInv = buildSeg(/*A*/20, /*T*/5, /*Top*/35, /*Imp*/38, /*Fin*/39, 15);   // Takeaway before hold end
        const EventRefineResult rInv = refineEvents(segInv, shaft, ball, -1, addrOnly);
        check(!rInv.addressRefined && ev(segInv, Phase::Address)->t_us == T(20),
              "refinedAddress > (old) Takeaway ⇒ inversion guard abstains Address");

        // 6c. maxShiftS tiny ⇒ both shifts exceed it ⇒ abstain.
        EventRefineConfig tight = on; tight.maxShiftS = 0.001;   // 1 ms
        Segmentation segTight = buildSeg(20, 25, 35, 38, 39, 15);
        const EventRefineResult rTight = refineEvents(segTight, shaft, ball, -1, tight);
        check(!rTight.refined && segTight.version == 2, "|shift| > maxShiftS ⇒ abstain");
    }

    // ── 7. refine.enabled = false ⇒ seg bit-identical ────────────────────────
    std::printf("=== 7. enabled=false byte-identity ===\n");
    {
        const int nf = 40;
        ShaftTrack2D shaft = buildShaftMeasured(nf, [](int i){ return i <= 24; }, 24);
        BallTrack2D  ball  = buildBall(nf);
        Segmentation seg   = buildSeg(20, 25, 35, 38, 39, 15);

        // snapshot
        struct Snap { Phase p; int64_t t; float c; SegmentRole prov; };
        std::vector<Snap> before;
        for (const PhaseEvent &e : seg.events) before.push_back({ e.phase, e.t_us, e.conf, e.provenance });
        const int    ver0   = seg.version;
        const int64_t start0 = seg.swingStartUs;

        const ShaftPosition p1Before = *p1Of(shaft);   // built at t_us=0 by buildShaftMeasured

        EventRefineConfig off = on; off.enabled = false;
        const EventRefineResult r = refineEvents(seg, shaft, ball, -1, off);

        bool same = !r.refined && seg.version == ver0 && seg.swingStartUs == start0
                    && seg.events.size() == before.size();
        for (size_t k = 0; same && k < before.size(); ++k)
            same = seg.events[k].phase == before[k].p && seg.events[k].t_us == before[k].t
                && seg.events[k].conf == before[k].c && seg.events[k].provenance == before[k].prov;
        check(same, "enabled=false ⇒ seg (events/version/swingStart) byte-identical");

        const ShaftPosition *p1After = p1Of(shaft);
        check(!r.p1Synced && p1After && p1After->t_us == p1Before.t_us
              && p1After->gripPx == p1Before.gripPx && p1After->source == p1Before.source,
              "enabled=false ⇒ P1 position left untouched");
    }

    // ── 8. swingStartUs coupling (clamp ≤ refined Address, never later) ───────
    std::printf("=== 8. swingStartUs coupling ===\n");
    {
        const int nf = 40;
        ShaftTrack2D shaft = buildShaftMeasured(nf, [](int i){ return i <= 24; }, 24);
        BallTrack2D  ball  = buildBall(nf);

        // 8a. swingStart already before the refined Address ⇒ left untouched (the
        //     span already contains it; on the vision path swingStart is bs0-derived,
        //     not Address−pad, so we never delta-shift it past the event).
        Segmentation segA = buildSeg(/*A*/20, /*T*/25, /*Top*/35, /*Imp*/38, /*Fin*/39, /*start*/15);
        const int64_t start0 = segA.swingStartUs;
        const EventRefineResult rA = refineEvents(segA, shaft, ball, -1, on);
        check(rA.addressRefined && ev(segA, Phase::Address)->t_us == T(24), "Address refined to hold end (24)");
        check(segA.swingStartUs == start0, "swingStart already <= refined Address ⇒ untouched");

        // 8b. swingStart AFTER the refined Address ⇒ clamped down to it.
        Segmentation segB = buildSeg(/*A*/20, /*T*/25, /*Top*/35, /*Imp*/38, /*Fin*/39, /*start*/30);
        refineEvents(segB, shaft, ball, -1, on);
        check(segB.swingStartUs == ev(segB, Phase::Address)->t_us && segB.swingStartUs == T(24),
              "swingStart > refined Address ⇒ clamped to the Address");

        // Address not refined (refine.address off) ⇒ swingStartUs untouched.
        EventRefineConfig takeOnly = on; takeOnly.address = false;
        Segmentation seg2 = buildSeg(20, 25, 35, 38, 39, 30);
        const int64_t s2 = seg2.swingStartUs;
        refineEvents(seg2, shaft, ball, -1, takeOnly);
        check(seg2.swingStartUs == s2, "Address not refined ⇒ swingStartUs untouched");
    }

    // ── impactResidual telemetry (log-only; computed regardless of refine) ────
    std::printf("=== impactResidual telemetry ===\n");
    {
        const int nf = 40;
        ShaftTrack2D shaft = buildShaftMeasured(nf, [](int i){ return i <= 24; }, 24);
        BallTrack2D  ball  = buildBall(nf);
        ball.launchTUs = 381000;                       // launch 1 ms after impact
        Segmentation seg = buildSeg(20, 25, 35, 38, 39, 15);
        const EventRefineResult r = refineEvents(seg, shaft, ball, /*impactUs=*/380000, on);
        check(r.impactResidualValid && r.impactResidualUs == 1000, "impactResidual = launch − impact = +1000 us");

        EventRefineConfig noRes = on; noRes.impactResidual = false;
        Segmentation seg2 = buildSeg(20, 25, 35, 38, 39, 15);
        const EventRefineResult r2 = refineEvents(seg2, shaft, ball, 380000, noRes);
        check(!r2.impactResidualValid, "refine.impactResidual=false ⇒ no residual computed");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
