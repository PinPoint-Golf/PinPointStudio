/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

// Down-the-line shaft tracker parameters (dtl_shaft_tracker_design.md §5.6–§5.8).
// Same contract as ShaftV3Config: every field defaults to the design's value and
// fromOverrides() applies "shaft.dtl.<name>" keys, so a SwingLab sweep iterates at
// binary speed.
//
// ⚠ Several of these are PLACEHOLDERS the design says out loud are placeholders —
// corridor.w0Deg above all ("set from Stage 0's measured residual … not chosen
// here", §5.7). They are here so the tracker compiles and runs; they are not
// values anyone has graded.
//
// The "shaft.dtl.*" keys cover the DTL-OWN fields below and nothing else. The
// four evidence sub-configs (ridge/band/seg/snap) have NO shaft.dtl.* spelling
// and never will: the engines are shared and unchanged (§5.5), so they take the
// face-on values — and they take them AFTER the run's own overrides, via
// ShaftV3Config::fromOverrides(ov). A sweep that sets "shaft.ridge.*",
// "shaft.seg.*" or "shaft.snap.*" is changing the shared engines, and the DTL
// view must be running the same engines the face-on view is; taking the compiled
// defaults here instead would have made every such sweep compare two different
// evidence stacks and call the difference a view difference.

#include <QString>
#include <QVariantMap>

#include <cstdint>

#include "analysis_tuning.h"          // pinpoint::analysis::tuning::apply
#include "shaft_track_assembly.h"     // ShaftV3Config (the source of the shared defaults)
#include "shaft_tracker_math.h"       // RidgeConfig / BandMatchConfig / SegmentConfig
#include "uncertainty_config.h"       // UncertaintyConfig (shaft_uncertainty_propagation_design.md)

namespace pinpoint::analysis {

struct DtlShaftConfig {
    bool   enabled   = true;    // master gate; false ⇒ DtlShaftTracker::track returns an invalid track
    // Truth-only mode: run the DTL band lock and NOTHING that reads the face-on
    // witness. This is the instrument that grades the coupling (§6), so it must
    // be able to run with the witness pointer null.
    bool   truthOnly = false;

    // D3 / §5.8: below this predicted projected length nothing is solved at all.
    // The club is pointing at the lens; there is no angle to measure.
    //
    // 0.50, not the design's 0.35. MEASURED on the dev six: at the top of the
    // backswing ρ̂_D reaches only 0.40–0.59 on swings 0007 and 0008, so at 0.35
    // the end-on gap never opened and the solve ran one 610–636 ms band straight
    // through the frames where the club is pointing at the lens. Coverage is the
    // thing the programme trades (§8, "raise rhoSolveMin and accept narrower
    // bands"); a band through the top is the forearm lock §2 exists to prevent.
    double rhoSolveMin = 0.50;

    // ── the visibility schedule (§5.3) ───────────────────────────────────────
    // The SECOND ablation the grading depends on (§6, "same run with the schedule
    // off too"): with this false every non-quarantined frame inside the inherited
    // span is treated as sighted and ρ̂_D is still RECORDED, so the report can put
    // "what face-on buys in total" beside "what the angle prior buys" without
    // either row changing anything else.
    struct Schedule {
        bool enabled = true;
    } schedule;

    // A maximal sighted run shorter than this is not a band (§5.8): a two-frame
    // run is a flicker in ρ̂_D at an end-on edge, and a Viterbi over it is a
    // per-frame pick wearing a global solve's clothes.
    int minBandFrames = 6;

    // ── the continuous-track update (dtl_continuous_track_design_update.md §3.1) ──
    // Six rules for the SPORADIC holes inside a sighted band. None of them bridges
    // an end-on gap; every one is a separate switch with a bit-identical OFF, and
    // each default is set by its own gate on the 21 DTL corpus swings against the
    // held-out band truth (docs/research/data/markerless/dtl_continuous_20261002.md).
    //
    // held — a frame inside a sighted band that failed the RAY gate, bounded on
    // BOTH sides by measured frames of the same band, keeps the band's own Viterbi
    // θ_D with tier HELD and the neighbours' confidence, for a hole of at most
    // maxFrames (6 ≈ 40 ms at 150 fps — the same span the tile used to bridge
    // silently). Never at a band's edge: a hole with a measurement on one side
    // only is a tail, and coasting into a tail is the PRED-at-impact failure.
    //
    // ON by Mark's decision (1 Oct 2026, dtl_continuous_20261002.md §3.4): the gate's
    // ≥ 0.85 threshold was not met — the tier fills 97 mid-backswing and 43 delivery
    // frames on the 21 corpus swings, drawn coverage 0.69 → 0.80 and 0.66 → 0.75, no
    // truth frame worse, and the deficit that remains is band-edge tails no
    // hole-filling rule can reach — so it ships on to be judged in the app on the
    // re-analysed 4 July session, and this one key turns it back off.
    struct Held {
        bool enabled   = true;
        int  maxFrames = 6;
    } held;
    // endOnFirst — classify END_ON (ρ̂_D < rhoSolveMin) BEFORE the quarantine. The
    // ladder used to let OCCLUDED outrank END_ON, so the whole top of the
    // backswing read as "hands hidden" when the geometry alone had already said
    // nothing could be seen. Counted: 1,078 of 2,063 OCCLUDED frames on the 21
    // corpus swings were end-on by the schedule.
    bool endOnFirst = true;
    // quarantineCause — split OCCLUDED into OCCLUDED_WRIST (the pose had no
    // confident wrist: the HANDS were hidden) and OCCLUDED_ROW (the anchor is not
    // where face-on says the hands are). A tier name, not a reason string, so the
    // tile and the report can act on it.
    bool quarantineCause = true;
    // edge — a sighted run shorter than minBandFrames but at least minFrames long
    // becomes a band of its own when it sits within maxGapFrames NON-end-on frames
    // of a full band: the hole between them is a quarantined or undecodable frame
    // or two, not the schedule saying the club has gone end-on. Solved
    // independently and never joined — the Held tier does not cross the hole.
    // … or (b) when the run's own ρ̂_D median is at or above minRho: the six-frame
    // minimum guards a run sitting AT rhoSolveMin, and a short run at ρ̂_D 0.95 is
    // a sighted club whose neighbours flickered (07-04 s7's whole impact band).
    struct Edge {
        bool   enabled      = true;
        int    minFrames    = 3;
        int    maxGapFrames = 2;
        double minRho       = 0.70;
    } edge;
    // lenSchedule — ONE drawn length: ρ̂_D · L̂_D, L̂_D from the ball at address,
    // instead of the per-frame switch between rend / snapLine / latBand that was
    // the flicker in the drawn head (506 source switches and 713 jumps of more
    // than 40 px between consecutive published frames on the 21 corpus swings).
    // The measured run still decides the tier and is carried as runPx.
    bool lenSchedule = true;
    // refuseLateEscape — a RAY frame after P8 whose solved θ escaped the face-on
    // corridor is not published: measured on the corpus these are θ ≈ 2π with the
    // head at the frame edge (07-04 s4, three frames), noise the tile drew.
    bool refuseLateEscape = true;
    // armChain — a corridor escape that runs from the hands through the LEAD elbow and on
    // through the LEAD shoulder is that arm, not the club. Two witnesses, and it takes both:
    // face-on says the club is not in that direction (the escape), and the line is the arm's
    // (both joints ahead along the ray, in order, within armChainLatPx of it). At the top the
    // club is end-on or out of the light, and a straight lead arm read along its lit edge is
    // the only line there is: 33–51 px off the elbow and 17–51 px off the shoulder, so D2's
    // per-joint test (arm.latPx 25, shoulders deliberately absent) never fires. Measured on
    // 5 Oct 2026: 3 of 28 swings as captured (5 on a re-run) published 4–24 frames of it, θ 20–48°, drawn at the club's
    // full length; and on the 21-swing corpus 34 frames on four 07-04 swings (the "P4.0→P4.3"
    // bands), every one an escape and every one on the bare forearm when looked at.
    // Refused at publication, not charged in the solve: as a D2 cost the solve stepped to
    // the arm's other edge and published that, and widened to reach it the rule moved 16–21
    // true P2–P3 frames a swing (the true shaft passes the TRAIL arm there) — and at P5 the
    // true shaft runs within 100 px of the lead arm without being an escape.
    bool   armChain      = true;
    double armChainLatPx = 100.0;
    // postImpactContinuity — after impact the club swings away down the line: in this view it
    // keeps its direction (56–74° over the 150 published post-impact frames of 5 Oct, drifting
    // about 1° a frame) and SHORTENS until it is end-on. A measured frame inside
    // impact + postImpactWindowUs whose θ steps more than postImpactStepDeg from the last
    // accepted one (the step scales with the hole it is taken across, per 20 ms) has left the club — measured, it is the trail leg's trouser edge at 78–89°
    // with the anchor no longer on the hands (8 of 28 swings, 3–12 frames each) — and neither
    // it nor anything after it in the window is published or held.
    bool    postImpactContinuity = true;
    double  postImpactStepDeg    = 10.0;
    int64_t postImpactWindowUs   = 150000;
    // postImpactRunLength — after impact the schedule's length is wrong: face-on coasts there,
    // so ρ̂_D comes from the ρ_F := 1 bound on a θ_F that has stopped moving, and stays near 1
    // while the club foreshortens. Drawn 1.6× the measured run on 5 Oct (317 px against 150 px
    // 24 ms after impact). After impact the drawn length is the measured run where that is
    // the shorter.
    bool postImpactRunLength = true;

    // ── D5 the face-on corridor (§5.7) ───────────────────────────────────────
    struct Corridor {
        bool   enabled = true;
        double w0Deg   = 25.0;   // PLACEHOLDER half-width; Stage 0 measures it (p99 of centre vs band-lock θ)
        double wCorr   = 5.0;    // cost ceiling — deliberately BELOW wE2/2 so clean evidence outside can still win
        double rhoFMax = 0.93;   // above this ρ_F the centre expression is degenerate: gate OFF, D6 or nothing
    } corridor;

    // ── D4 half-plane (§5.6b) ────────────────────────────────────────────────
    struct Half {
        double wHalf = 6.0;
    } half;

    // ── D3 length consistency (§5.6) ─────────────────────────────────────────
    // ONE-SIDED: a run LONGER than slack × ρ̂_D × L̂_D costs wLen; a short run is
    // always allowed (occlusion, dim steel — the markerless length-gate lesson).
    struct Len {
        double wLen  = 8.0;
        double slack = 1.25;
        // The largest evidence-free gap (px) bridged inside ONE evidenced run when
        // the published length is measured off the re-registered line (§5.9, and
        // DtlLenSrc). The taped groups put black tape over a dark mat and the bare
        // steel between the lit stretches drops out entirely, so a run measured
        // with no hole allowance stops at the first tape edge and reports a stub.
        // 30 px is the same order as the segment engine's maxHolePx (80 px on the
        // face-on scale) reduced for this view's shorter projection.
        double holePx = 30.0;
        // ── how close to ridgeSweep's own floor still counts AS the floor ────
        // ridgeSweep searches its cumulative-score argmax only from
        // j0 = minLenPx / rStep onward, so its shortest possible terminus is
        // rLo + minLenPx — 98 px on the shipped constants — and a ray that leaves
        // the club early reports that number to the digit. A published length
        // within this many px of it was never MEASURED: the argmax is parked on
        // its own lower bound. 14 px is two ridge steps' worth of rounding either
        // side of the floor and nothing more; it is a tolerance on an equality,
        // not a threshold on a length. MEASURED on 06-11: the two P4 tiles the
        // results §8 calls "98–100 px", the stray mid-swing frames that appeared
        // when L̂_D moved, and the short impact-band runs all sit inside it.
        double floorSlackPx = 14.0;
    } len;

    // ── D2 limb vetoes (§5.6, generalised from the forearms) ─────────────────
    // BOTH arms and every limb joint below the shoulders, ALL phases. The lateral
    // test is the discriminator: the counterfeit runs along the limb AND near its
    // joint, the shaft only ever does the first. Generalised beyond the elbows
    // because the measured address failure is the TROUSER LINE — a long ray from
    // the hands down the trail leg to the feet — and D2-on-elbows says nothing
    // about it. Never the shoulders or the head: at P3 the true shaft passes near
    // them.
    struct Arm {
        double vetoDeg = 12.0;
        double latPx   = 25.0;
        double wArm    = 16.0;
        // A joint nearer the grip than this is not a limb the ray can "run along":
        // the direction grip→J is then pose jitter, and a veto built on it would
        // refuse a legitimate direction for no reason.
        double minJointPx = 60.0;
    } arm;

    // ── D1 reverse ray (§5.6) ────────────────────────────────────────────────
    // Face-on's C1 weak form, the same two constants (wC1 / c1Tol) under their
    // own DTL spelling: strong evidence pointing the OTHER way out of the grip is
    // a scene line, not a club that terminates at the hands. The face-on version
    // additionally excuses the forearm direction; here D2 owns the forearms in
    // every phase, so this term does not have to.
    //
    // … and in THIS view the test has a standing excuse. Face-on's C1 assumes
    // FREE SPACE behind the butt; down the line there is none. The lead arm is
    // near-collinear with the shaft at address and at impact, and the forearms
    // are at P3/P5, on the opposite side of the grip — so the reverse ray of a
    // CORRECT direction runs up the golfer's own arm and "the reverse ray is as
    // strong" is the normal condition of a right frame. MEASURED: 126 in-span
    // frames refused on it across the dev six, four of them adjudicated-right
    // ladder tiles whose reverse sits 1.0°, 6.9°, 18.0° and 19.1° off an elbow
    // direction. armDeg 25 clears all four with margin and is not wide enough to
    // excuse a reverse that points into free space.
    struct Rev {
        double wRev = ShaftV3Config{}.wC1;
        double tol  = ShaftV3Config{}.c1Tol;
        double armDeg   = 25.0;   // |θ+180 − grip→(elbow|shoulder)| within this ⇒ the test is waived
        double armMinPx = 40.0;   // … and only for a joint this far out; nearer is pose jitter
    } rev;

    // ── D6 the DTL ball soft anchor (§4.3, §5.6) ─────────────────────────────
    // A PRIOR, never a pin: a shallow Gaussian well at grip→ball over the address
    // hold and ±2 frames of impact, worth a fraction of wE2 so clean evidence
    // elsewhere still wins. No ball ⇒ the whole term is absent.
    struct Ball {
        double wBall    = 4.0;
        double sigmaDeg = 8.0;
        // ── the GATE at the still club (measured, and a different thing ───────
        // D6 above is a soft well of depth 4 and σ 8°, and it LOSES: at address
        // the long high-contrast trail-leg line ties the shaft after the shared
        // percentile normalisation (both reach EV ≈ 1), and a 4-deep well cannot
        // separate a tie against a 132° error. So where the club is KNOWN to be at
        // the ball — the address hold, and ±20 ms of impact — a candidate more
        // than gateDeg from grip→ball takes wGate. It is a gate in effect and a
        // COST in form, so the trace can show what it refused rather than the
        // refusal being invisible. With no DTL ball the frame is left unsolved
        // instead: no witness, no claim.
        double gateDeg  = 20.0;
        double wGate    = 30.0;
        // ── how long the still club STAYS still ──────────────────────────────
        // P1 + 30 ms is the instant face-on calls the takeaway, not the instant
        // the club leaves the ball. MEASURED on swings 0005 and 0007: the golfer's
        // one-piece takeaway keeps the shaft within 5° of the ball line for ~190 ms
        // after P1, and the frame after the 30 ms window closes the solve jumps to
        // the trouser/shin edge at 103–132°. So the hold RELEASES when the face-on
        // witness says the club has left address — |θ_F(t) − θ_F(P1)| above
        // stillDeg — and never later than stillMaxUs, which is the backstop for a
        // swing whose face-on θ happens to sit still for another reason. θ_F here
        // is inherited TIMING/STATE, not a DTL measurement: the gate's DIRECTION
        // is still DTL's own grip→ball. A frame with no usable witness falls back
        // to the P1 + 30 ms rule rather than inheriting a hold nobody witnessed.
        double  stillDeg   = 10.0;
        int64_t stillMaxUs = 300000;

        // ── the SHADOW cue, for a blown-white mat ────────────────────────────
        // MEASURED, 06-11, all nine swings: the mat under the ball images at
        // 199–231 (p50 over the prior) and saturates at 253–254 where the ball
        // sits, so a white ball on it has no edge at all — the bright cue is not
        // wrong there, it is looking at a ball that is not visible. What IS
        // visible is the ball's own contact SHADOW: a crisp dark crescent on the
        // blown mat at the ball's lower rim, 51–79 px of area, 2.4–3.2:1
        // elongated, reading 77–110 against a 253 mat — and GONE once the ball
        // has been struck. These four scalars are that cue, and every default is
        // the measurement, not a guess.
        //
        // shadowMatMin — the mat must be BLOWN for this cue to mean anything: the
        // local 31×31 median at the crescent is 253 on 06-11, where the whole
        // point is that white-on-white hides the ball. On 07-04 the mat under the
        // ball is a dim 90–100 and this gate excludes that region by design; the
        // bright cue owns that scene and wins there anyway.
        double shadowMatMin = 200.0;
        // shadowDrop — how far below its own local median a pixel must sit. The
        // crescent runs 143–176 grey levels below; the 99th percentile of the
        // drop over the whole prior is 26–27. 60 is the middle of a wide gap.
        double shadowDrop = 60.0;
        // shadowLaunchRise — the DISCRIMINATOR, and the reason a shoe scuff or a
        // tee hole cannot pass: the same pixels must have LOST their darkness
        // once the ball has left. Measured at the true crescent: +113 to +124.
        // Measured at the four static marks that survive the shape gates on the
        // same swings: −30 to +1. 40 separates them with room on both sides.
        double shadowLaunchRise = 40.0;
        // shadowToCentreR — the crescent is the shadow AT THE BALL'S LOWER RIM,
        // so the ball centre is one radius above its centroid. The radius comes
        // from the scene scale (see dtlFindBall). It is a small correction by
        // construction: at a 316–338 px grip→ball distance a 6 px error in the
        // centre is 0.6° in θ_ball, inside a ±20° gate.
        double shadowToCentreR = 1.0;
    } ball;

    // ── the solve (§5.8) ─────────────────────────────────────────────────────
    // Rate bound ω_max(t) = omegaBaseDegPerFrame / ρ̂_D(t): θ_D legitimately moves
    // fast as the projection shortens toward a gap, so a fixed per-phase ceiling
    // would clip the band edges or be loose everywhere else. No phase-signed
    // direction term — the sign of dθ_D/dt is not a law in this view.
    double omegaBaseDegPerFrame = 12.0;
    double kSmooth = ShaftV3Config{}.kSmooth;   // transition θ-smoothness, face-on's value
    double grid    = 1.0;                       // θ grid step (deg)

    // ── emission weights + tier thresholds (§5.9) ────────────────────────────
    double wE2     = ShaftV3Config{}.wE2;       // emission span, face-on's value
    double wBand   = ShaftV3Config{}.wBand;     // band-lock negative well, face-on's value
    double bandTol = 6.0;     // |θ* − θ_band| (deg) to claim BAND
    double evRay   = 0.45;    // normalised E2 evidence at the solve to claim RAY
    double supRay  = 0.40;    // absolute ridge support at the solve to claim RAY
    double revRatio = 1.15;   // RAY must beat its own reverse ray by this factor
    // … or the snapped line's own support may stand in for evRay. Same root as
    // DtlLenSrc: EV is read along a ray from the POSE grip, which sits tens of px
    // off the shaft axis, so on a frame where the shaft is a plainly visible
    // bright streak on a black background the ray reads 0.37–0.40 against the
    // 0.45 gate. Where the snap was ACCEPTED there is a better measurement to
    // hand — the support under the line the frame would publish — and this is the
    // floor it has to clear. NOT a guess: 0.36 is the 10th percentile of
    // bestLineConf over the 925 dev-six frames that already published on EV and
    // whose tiles the montage review adjudicated right — p05 0.29, p10 0.36,
    // p25 0.70, p50 0.76, p90 0.81. The distribution is BIMODAL (a mass at
    // 0.70–0.81 and a tail down to 0.10), so the p10 sits in the tail's shoulder
    // rather than on a natural break: it is the published set's own tenth
    // percentile and nothing more, and it must be re-measured whenever the snap
    // or the contrast channel moves. SUP, the limb veto, the minimum length, the
    // ball gate and the schedule all still apply; this replaces the EV gate alone.
    double lineConfRay = 0.36;
    // … and it must be a SHAFT's worth of line. The visibility law says how long
    // the club should look on this frame (ρ̂_D × L̂_D); a run far shorter than that
    // is a stub of something else. One-sided the other way from D3, and applied
    // only where L̂_D is known — a 40 px run with no length prior is not evidence
    // of a fault, it is evidence of nothing.
    double minLenFrac = 0.35;

    // ── the post-solve snap (§5.9, dtl_shaft_post) ───────────────────────────
    // Re-registration is a spatial correction to a line that is ALREADY roughly
    // right; below this ρ̂_D the projection is short and moving fast, the ridge
    // search has a few tens of px to work with, and what it finds is as likely to
    // be a limb as the club. Skip, and keep the DP's line.
    double snapRhoMin = 0.60;

    // ── evidence honesty (S1, face-on's evAbsFloor) ──────────────────────────
    // normScores() rescales EVERY frame's ridge row into [0,1] against its own
    // p50/p97, so a frame with no line in it still mints a full-strength winner.
    // These are the absolute, pre-normalisation statement that a channel saw a
    // line at all. SEEDED FROM the run's face-on values in fromOverrides (so
    // "shaft.evAbsFloor" reaches both views at once, like the engines), then
    // overridable per-view with "shaft.dtl.evAbsFloor*" — because the DTL
    // channels look at a lit simulator screen and may genuinely need their own.
    double evAbsFloor    = ShaftV3Config{}.evAbsFloor;
    double evAbsFloorDif = ShaftV3Config{}.evAbsFloorDif;   // motion/contrast override; < 0 ⇒ use evAbsFloor

    // The polarity-free local-contrast channel (§5.5, the POLARITY TRAP): the
    // shaft alternates black and white bands over a mid-grey lit screen, where a
    // SIGNED bright-ridge response cancels along the shaft and a wide bright limb
    // wins. |frame − boxblur(frame, k×k)| does not cancel. Measured on the dev
    // six: |on-line − lateral bg| is 37–48 grey levels along the true shaft over
    // screen and mat, against 1–8 on a control line.
    int contrastKsz = 31;

    // Frames entering each phase-aware clean-plate median (§5.4). A median is a
    // rank statistic — 25 well-chosen frames say the same thing 250 do, and the
    // cost is linear in the count at every pixel.
    int plateMaxFrames = 25;

    // ── the cross-view anchor check (§5.2) ───────────────────────────────────
    // p95Mult is the design's spelling; Stage 0 measured the fit's p95 residual
    // at 27–97 px even PRE-impact, so a p95-based quarantine is too loose to
    // catch the thing it exists for (the post-impact invented hands). The three
    // absolute keys below are what actually bite: an absolute ceiling everywhere,
    // and a tighter one once the body has turned its back to the camera.
    struct Quarantine {
        double  p95Mult      = 1.0;     // quarantine above p95Mult × the row fit's p95 residual
        double  absPx        = 80.0;    // … and unconditionally above this many px of row disagreement
        int64_t postImpactUs = 80000;   // after impact + this …
        double  postAbsPx    = 40.0;    // … the ceiling tightens to this
    } quarantine;

    // Evidence engines, unchanged from face-on (§5.5) — shared code, shared
    // constants. `s` bounds are re-derived for the DTL scale inside the engine.
    // Defaults here; fromOverrides replaces them with the run's face-on values.
    RidgeConfig     ridge = ShaftV3Config{}.ridge;
    BandMatchConfig band  = ShaftV3Config{}.band;
    SegmentConfig   seg   = ShaftV3Config{}.seg;
    SnapConfig      snap  = ShaftV3Config{}.snap;
    // Shaft uncertainty — the same "uncertainty.*" keys as face-on. Off ⇒ no σ written.
    UncertaintyConfig unc;

    static DtlShaftConfig fromOverrides(const QVariantMap& ov)
    {
        namespace tn = pinpoint::analysis::tuning;
        DtlShaftConfig c;
        // The shared engines, configured exactly as face-on configured them for
        // THIS run — so "shaft.ridge.*" / "shaft.seg.*" / "shaft.snap.*" reach
        // both views at once and a sweep compares one variable, not two.
        const ShaftV3Config fo = ShaftV3Config::fromOverrides(ov);
        c.ridge = fo.ridge;
        c.band  = fo.band;
        c.seg   = fo.seg;
        c.snap  = fo.snap;
        // Same rule for the evidence FLOORS as for the engines: they belong to
        // the shared evidence stack, so "shaft.evAbsFloor*" reaches both views
        // first and the DTL spelling below is the per-view escape hatch.
        c.evAbsFloor    = fo.evAbsFloor;
        c.evAbsFloorDif = fo.evAbsFloorDif;
        c.unc           = fo.unc;
        tn::apply(ov, "shaft.dtl.enabled",             c.enabled);
        tn::apply(ov, "shaft.dtl.truthOnly",           c.truthOnly);
        tn::apply(ov, "shaft.dtl.rhoSolveMin",         c.rhoSolveMin);
        tn::apply(ov, "shaft.dtl.schedule.enabled",    c.schedule.enabled);
        tn::apply(ov, "shaft.dtl.minBandFrames",       c.minBandFrames);
        tn::apply(ov, "shaft.dtl.held.enabled",        c.held.enabled);
        tn::apply(ov, "shaft.dtl.held.maxFrames",      c.held.maxFrames);
        tn::apply(ov, "shaft.dtl.endOnFirst",          c.endOnFirst);
        tn::apply(ov, "shaft.dtl.quarantineCause",     c.quarantineCause);
        tn::apply(ov, "shaft.dtl.edge.enabled",        c.edge.enabled);
        tn::apply(ov, "shaft.dtl.edge.minFrames",      c.edge.minFrames);
        tn::apply(ov, "shaft.dtl.edge.maxGapFrames",   c.edge.maxGapFrames);
        tn::apply(ov, "shaft.dtl.edge.minRho",         c.edge.minRho);
        tn::apply(ov, "shaft.dtl.lenSchedule",         c.lenSchedule);
        tn::apply(ov, "shaft.dtl.refuseLateEscape",    c.refuseLateEscape);
        tn::apply(ov, "shaft.dtl.armChain",            c.armChain);
        tn::apply(ov, "shaft.dtl.armChainLatPx",       c.armChainLatPx);
        tn::apply(ov, "shaft.dtl.postImpactContinuity", c.postImpactContinuity);
        tn::apply(ov, "shaft.dtl.postImpactStepDeg",   c.postImpactStepDeg);
        tn::apply(ov, "shaft.dtl.postImpactWindowUs",  c.postImpactWindowUs);
        tn::apply(ov, "shaft.dtl.postImpactRunLength", c.postImpactRunLength);
        tn::apply(ov, "shaft.dtl.corridor.enabled",    c.corridor.enabled);
        tn::apply(ov, "shaft.dtl.corridor.w0Deg",      c.corridor.w0Deg);
        tn::apply(ov, "shaft.dtl.corridor.wCorr",      c.corridor.wCorr);
        tn::apply(ov, "shaft.dtl.corridor.rhoFMax",    c.corridor.rhoFMax);
        tn::apply(ov, "shaft.dtl.half.wHalf",          c.half.wHalf);
        tn::apply(ov, "shaft.dtl.len.wLen",            c.len.wLen);
        tn::apply(ov, "shaft.dtl.len.slack",           c.len.slack);
        tn::apply(ov, "shaft.dtl.len.holePx",          c.len.holePx);
        tn::apply(ov, "shaft.dtl.len.floorSlackPx",    c.len.floorSlackPx);
        tn::apply(ov, "shaft.dtl.arm.vetoDeg",         c.arm.vetoDeg);
        tn::apply(ov, "shaft.dtl.arm.latPx",           c.arm.latPx);
        tn::apply(ov, "shaft.dtl.arm.wArm",            c.arm.wArm);
        tn::apply(ov, "shaft.dtl.arm.minJointPx",      c.arm.minJointPx);
        tn::apply(ov, "shaft.dtl.rev.wRev",            c.rev.wRev);
        tn::apply(ov, "shaft.dtl.rev.tol",             c.rev.tol);
        tn::apply(ov, "shaft.dtl.rev.armDeg",          c.rev.armDeg);
        tn::apply(ov, "shaft.dtl.rev.armMinPx",        c.rev.armMinPx);
        tn::apply(ov, "shaft.dtl.ball.wBall",          c.ball.wBall);
        tn::apply(ov, "shaft.dtl.ball.sigmaDeg",       c.ball.sigmaDeg);
        tn::apply(ov, "shaft.dtl.ball.gateDeg",        c.ball.gateDeg);
        tn::apply(ov, "shaft.dtl.ball.wGate",          c.ball.wGate);
        tn::apply(ov, "shaft.dtl.ball.stillDeg",       c.ball.stillDeg);
        tn::apply(ov, "shaft.dtl.ball.stillMaxUs",     c.ball.stillMaxUs);
        tn::apply(ov, "shaft.dtl.ball.shadowMatMin",     c.ball.shadowMatMin);
        tn::apply(ov, "shaft.dtl.ball.shadowDrop",       c.ball.shadowDrop);
        tn::apply(ov, "shaft.dtl.ball.shadowLaunchRise", c.ball.shadowLaunchRise);
        tn::apply(ov, "shaft.dtl.ball.shadowToCentreR",  c.ball.shadowToCentreR);
        tn::apply(ov, "shaft.dtl.evAbsFloor",          c.evAbsFloor);
        tn::apply(ov, "shaft.dtl.evAbsFloorDif",       c.evAbsFloorDif);
        tn::apply(ov, "shaft.dtl.contrastKsz",         c.contrastKsz);
        tn::apply(ov, "shaft.dtl.plateMaxFrames",      c.plateMaxFrames);
        tn::apply(ov, "shaft.dtl.omegaBaseDegPerFrame", c.omegaBaseDegPerFrame);
        tn::apply(ov, "shaft.dtl.kSmooth",             c.kSmooth);
        tn::apply(ov, "shaft.dtl.grid",                c.grid);
        tn::apply(ov, "shaft.dtl.wE2",                 c.wE2);
        tn::apply(ov, "shaft.dtl.wBand",               c.wBand);
        tn::apply(ov, "shaft.dtl.bandTol",             c.bandTol);
        tn::apply(ov, "shaft.dtl.evRay",               c.evRay);
        tn::apply(ov, "shaft.dtl.supRay",              c.supRay);
        tn::apply(ov, "shaft.dtl.revRatio",            c.revRatio);
        tn::apply(ov, "shaft.dtl.lineConfRay",         c.lineConfRay);
        tn::apply(ov, "shaft.dtl.minLenFrac",          c.minLenFrac);
        tn::apply(ov, "shaft.dtl.snapRhoMin",          c.snapRhoMin);
        tn::apply(ov, "shaft.dtl.quarantine.p95Mult",  c.quarantine.p95Mult);
        tn::apply(ov, "shaft.dtl.quarantine.absPx",       c.quarantine.absPx);
        tn::apply(ov, "shaft.dtl.quarantine.postImpactUs", c.quarantine.postImpactUs);
        tn::apply(ov, "shaft.dtl.quarantine.postAbsPx",   c.quarantine.postAbsPx);
        return c;
    }
};

// Any of the six continuous-track rules on ⇒ the track is marked `continuous`
// and its JSON carries a summary.continuous ledger (dtl_shaft_track.h).
inline bool dtlContinuousAny(const DtlShaftConfig& c)
{
    return c.held.enabled || c.endOnFirst || c.quarantineCause || c.edge.enabled
        || c.lenSchedule || c.refuseLateEscape;
}

// ── the resolved config's fingerprint ────────────────────────────────────────
// A stable 64-bit FNV-1a over every RESOLVED scalar, formatted as 16 hex digits
// and written into club_dtl.json and trace_dtl.jsonl. It exists so a montage or
// a results table can say WHICH configuration produced it: "the numbers moved"
// and "the config moved" are different findings, and a run whose settings are
// only in a shell history cannot tell them apart. Deterministic by construction
// — fixed field order, fixed 17-significant-digit formatting, no map iteration,
// no addresses — so two runs of one config hash identically on any host.
inline QString dtlConfigHash(const DtlShaftConfig& c)
{
    QString s;
    const auto n = [&s](double v) { s += QString::number(v, 'g', 17) + QLatin1Char(';'); };
    const auto i = [&s](long long v) { s += QString::number(v) + QLatin1Char(';'); };
    i(c.enabled); i(c.truthOnly); n(c.rhoSolveMin); i(c.schedule.enabled); i(c.minBandFrames);
    i(c.corridor.enabled); n(c.corridor.w0Deg); n(c.corridor.wCorr); n(c.corridor.rhoFMax);
    n(c.half.wHalf); n(c.len.wLen); n(c.len.slack); n(c.len.holePx); n(c.len.floorSlackPx);
    n(c.arm.vetoDeg); n(c.arm.latPx); n(c.arm.wArm); n(c.arm.minJointPx);
    n(c.rev.wRev); n(c.rev.tol); n(c.rev.armDeg); n(c.rev.armMinPx);
    n(c.ball.wBall); n(c.ball.sigmaDeg); n(c.ball.gateDeg); n(c.ball.wGate);
    n(c.ball.stillDeg); i(c.ball.stillMaxUs);
    n(c.ball.shadowMatMin); n(c.ball.shadowDrop); n(c.ball.shadowLaunchRise);
    n(c.ball.shadowToCentreR);
    n(c.omegaBaseDegPerFrame); n(c.kSmooth); n(c.grid);
    n(c.wE2); n(c.wBand); n(c.bandTol); n(c.evRay); n(c.supRay); n(c.revRatio); n(c.lineConfRay);
    n(c.minLenFrac); n(c.snapRhoMin);
    n(c.evAbsFloor); n(c.evAbsFloorDif); i(c.contrastKsz); i(c.plateMaxFrames);
    n(c.quarantine.p95Mult); n(c.quarantine.absPx); i(c.quarantine.postImpactUs);
    n(c.quarantine.postAbsPx);
    // The continuous-track rules (2026-10-02). Appended AFTER every field above so a
    // run with all six off still hashes to a different value from a pre-update run
    // — that is correct: the producer changed (kDtlShaftStageVersion 2), and the
    // frames' byte-identity is what the OFF gate measures, not the hash.
    i(c.held.enabled); i(c.held.maxFrames); i(c.endOnFirst); i(c.quarantineCause);
    i(c.edge.enabled); i(c.edge.minFrames); i(c.edge.maxGapFrames); n(c.edge.minRho);
    i(c.lenSchedule); i(c.refuseLateEscape);
    i(c.armChain); n(c.armChainLatPx); i(c.postImpactContinuity); n(c.postImpactStepDeg);
    i(c.postImpactWindowUs); i(c.postImpactRunLength);
    // The shared engines belong in the fingerprint too: "shaft.ridge.*" and
    // "shaft.snap.*" reach this view (fromOverrides above), so a sweep that moved
    // one of them produced a different DTL run and the hash has to say so.
    n(c.ridge.rStep); n(c.ridge.rLo); n(c.ridge.rHi); n(c.ridge.bgHi);
    n(c.ridge.eClipNeg); n(c.ridge.eClipPos); n(c.ridge.minLenPx);
    i(c.band.satT); i(c.band.areaMin); i(c.band.areaMax); i(c.band.maxBlobs);
    n(c.band.gripGate); n(c.band.latTol); n(c.band.sMin); n(c.band.sMax);
    n(c.band.r0Min); n(c.band.r0Max); n(c.band.rms4); n(c.band.rms5);
    n(c.band.gapMmMax); i(c.band.gapDark);
    i(c.snap.enabled); n(c.snap.maxOffsetPx); n(c.snap.maxDeltaDeg); n(c.snap.minLineConf);
    i(c.snap.corridorHalfPx); n(c.snap.coarseStepPx); n(c.snap.coarseStepDeg);
    n(c.snap.fineHalfPx); n(c.snap.fineHalfDeg);
    i(c.snap.skipAddr); i(c.snap.skipTakeawayUs); i(c.snap.skipBlur);
    // The uncertainty pass adds σ to the frames; it enters the fingerprint only when on, so an
    // uncertainty-off run hashes exactly as before the design existed.
    if (c.unc.enabled) { i(1); n(c.unc.rho); }
    const QByteArray b = s.toUtf8();
    uint64_t h = 1469598103934665603ull;
    for (const char ch : b) { h ^= uint64_t(uint8_t(ch)); h *= 1099511628211ull; }
    return QString::number(qulonglong(h), 16).rightJustified(16, QLatin1Char('0'));
}

} // namespace pinpoint::analysis
