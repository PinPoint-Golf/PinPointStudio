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

// Clubhead Stage-2 — measured club-head terminus + temporal model (Phase B of
// the clubhead-tracking plan, docs/plans validated-nibbling-kitten). A faithful
// C++ port of the AS-BUILT shaftlab exemplar, NOT the design-doc's rejected
// E_head product formula:
//   * H1 per-frame gap-tolerant on-axis terminus  — tools/shaftlab/clubhead_measure.py
//   * H2 segmented 1-D Kalman + per-segment RTS tiers — tools/shaftlab/clubhead_annotate.py
//   * ray_edge_radius / scene primitives          — tools/shaftlab/clubhead_scan.py
// Design context: docs/design/clubhead_detection_design.md (note the doc's
// peak-of-E_head formula was rejected by data at H1 — the working code measures a
// gap-tolerant terminus instead).
//
// DELIBERATE DEVIATION FROM THE PYTHON (the honesty fix, plan §B2): the Python's
// per-swing self-fit length model (length_model.py / clubhead_measure pass-1) is
// NOT ported. Against dense fast-phase truth the censored self-fit was the weak
// link (length errors to −215 px → 7/10 swings failed the ≤5% high-conf-bad
// clause). It is replaced by the ball-measured club length L_px (grip→ball at
// address, |B−G|) as a hard floor / annulus ceiling / Gaussian prior — see
// docs/research/club_detection_from_video.md §4.3/§5.5. There is therefore only
// ONE decode pass (the Python's pass 1 existed solely to fit the model).
//
// Doctrine matches shaft_track_assembly / ball_anchor: pure free functions over
// cv::Mat + plain vectors, no SwingWindow / no decode inside. The temporal half
// (runHeadTemporal + HeadKf1D) is entirely frameless so it is unit-testable
// without any video. The measurement half samples one decided ray per frame.
//
// WIRING (Phase B3, a later agent, edits shaft_track_assembly.cpp): after
// reconcilePsi, before the tiering loop, build one HeadSceneCtx from the already-
// computed float32 sceneMed, then per span-frame call measureHeadRadius on the
// decided θ/grip (forward + θ+180 for isFlipSuspect), collect z/zconf, and call
// runHeadTemporal once. This module NEVER feeds back into the DP/reconcile — the
// stage-1 θ path is byte-identical with head measurement on or off.

#include <QVariantMap>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <opencv2/core.hpp>

namespace pinpoint::analysis {

// The full Stage-2 parameter set. Every field defaults to the validated exemplar
// constant; fromOverrides() applies "shaft.head.<name>" keys from a tuning map
// (SwingLab sweeps). `enabled` is DEFAULT OFF — Phase B merges dark; the flip to
// on is its own commit after the corpus gates pass.
struct ClubheadConfig {
    bool   enabled = false;                 // shaft.head.enabled — master gate (dark at merge)

    // ── H1 per-frame measurement (clubhead_measure.py) ───────────────────────
    double hitThr        = 0.30;            // HIT_THR  — per-sample evidence threshold
    double permThr       = 0.50;            // PERM_THR — sceneMed edge-pair above this = permanent line (veto)
    int    localWin      = 8;               // LOCAL_WIN  — local-sustainment boxcar (px)
    double localFrac     = 0.50;            // LOCAL_FRAC — boxcar hit fraction to call a sample 'sustained'
    double supportMin    = 0.30;            // SUPPORT_MIN — accumulated grip→terminus support (gaps allowed)
    double startFrac     = 0.55;            // START_FRAC — first support must begin within this fraction of the terminus
    double tauP          = 90.0;            // TAU_P   — edge-pair normalisation (grey-level gradient scale)
    double tauM          = 20.0;            // TAU_M   — motion normalisation
    double tauChg        = 20.0;            // TAU_CHG — change-vs-sceneMed normalisation
    double rMinFrac      = 0.06;            // R_MIN_FRAC — skip the hands: rMin = max(20, rMinFrac·H)
    double edgeCensorPx  = 12.0;            // EDGE_CENSOR — terminus this close to the frame edge = censored
    // Multi-width edge-pair ridge: thin shaft / bloomed over-exposed shaft / head
    // blade. Take the MAX — a ridge at any scale counts (clubhead_measure §port notes).
    double edgeWidthThin  = 5.0;            // SHAFT_W_PX
    double edgeWidthBloom = 12.0;
    double edgeWidthBlade = 24.0;
    double ambigFloor     = 0.15;           // ambiguity conf floor: conf *= clamp(1−s2/s1, ambigFloor, 1)^0.5
    double latMaxPx       = 0.0;            // lateral band half-width (px); 0 = centre ray (real blurred footage)
    double bgAlpha        = 0.02;           // BG_ALPHA — running-background EMA rate (B3 owns the running bg)

    // ── B2 ball-length model (replaces the Python self-fit) ──────────────────
    double annulusCeilFactor = 1.15;        // r_hi = min(rayEdge, annulusCeilFactor·L_px) — projection can't exceed true length
    double hardFloorFactor   = 0.80;        // hard floor = max(armFloor, hardFloorFactor·L_px) in still/impact frames
    double priorSigmaFrac    = 0.30;        // Gaussian prior σ = max(priorSigmaFrac·L_px, priorSigmaFloorPx)
    double priorSigmaFloorPx = 40.0;        // σ floor: a starved model must not crush the search onto its own error
    double offFactor         = 0.80;        // off tier: rayEdge < offFactor·L_hat ⇒ head expected outside frame
    // Universal measurement-acceptance floor (design §4's own clause: r_meas ∈
    // [0.5, 1.15]·L — the ceiling was ported first, this is the missing floor).
    // Candidates below measFloorFrac·L̂ are not measurable on ANY frame, where
    // L̂ = L_px when the ball measured it, else the caller's ladder length
    // (headBounds' fallbackCeilPx). gateB-0705 failure mode: with no ball
    // (L_px = −1) a MOVING early-backswing frame had no floor at all, so a
    // short-radius on-ray counterfeit (retro-band edge / specular break at ~⅓
    // club length) was measured and the KF converged on it — 70% high-conf-bad.
    // Weaker (0.5) but universal, unlike the quasi-still/impact 0.8·L_px hard
    // floor above. Honesty trade: genuine top-of-swing foreshortening below
    // 0.5·L̂ becomes pred (honest) rather than meas — intended per the design's
    // honesty philosophy.
    double measFloorFrac     = 0.50;        // shaft.head.measFloorFrac
    // Phase-ramped floor ceiling (corpus gateB iteration 2): face-on, the club
    // stays near-FULL projected length at takeaway and again at impact —
    // foreshortening develops toward the top. The WIRING ramps the acceptance-
    // floor fraction floorRampHi → measFloorFrac linearly across [bs0, top] and
    // mirrors it back up over (top, impact], passing the per-frame fraction via
    // headBounds' floorFracOverride (this module stays phase-blind — it never
    // sees bs0/top/impact). Kills the early-backswing short-locks: streak
    // terminus at 0.5–0.96·L̂ while truth is near 1.0·L̂ (26/28 of the iter-2
    // confident-bad labels sat in bs0+20..bs0+45).
    double floorRampHi       = 0.80;        // shaft.head.floorRampHi
    // Backswing streak confidence cap (applied in the WIRING, post-temporal):
    // the FINAL emitted sample headConf is capped at this for frames in
    // [bs0, top]. Corpus-proven systematic short-lock on motion-blur streaks in
    // that phase; confident (≥0.5) head claims are reserved for the delivery
    // phase where the metrics consume them. Tier/KF behaviour and the raw trace
    // are unchanged — only the sample field is capped.
    double streakConfCap     = 0.45;        // shaft.head.streakConfCap

    // ── arm-length plausibility floor ────────────────────────────────────────
    double armFactor  = 1.05;               // ARM_FACTOR — club is always longer than the lead arm
    double armConfMin = 0.30;               // shoulder-keypoint conf gate (reserved; decideTrack joints carry no conf)

    // ── H2 temporal model (clubhead_annotate.py) ─────────────────────────────
    double segThetaJump = 20.0;             // SEG_THETA_JUMP — stage-1 θ jump (deg/frame) that splits KF segments
    double sigmaAcc     = 4.0e4;            // SIGMA_ACC — KF white-accel Q (px/s²), foreshortening recovery
    double gateSig      = 3.0;              // GATE_SIG — Mahalanobis innovation gate (σ)
    int    coastSlow    = 12;               // COAST_SLOW — coast budget frames (gentle motion)
    int    coastFast    = 4;                // COAST_FAST — coast budget when |ṙ| > fastRdot (blind drift at speed)
    double fastRdot     = 800.0;            // FAST_RDOT — px/s above which the fast coast budget applies
    double confMeasMin  = 0.35;             // CONF_MEAS_MIN — measurement conf floor to enter the filter
    int    runMin       = 4;                // RUN_MIN — confirmed-run length for the meas tier
    double reinitCap    = 0.35;             // REINIT_CAP — conf cap until a run is confirmed
    double sigMeasMax   = 10.0;             // SIG_MEAS_MAX — posterior σ_r (px) below which a converged meas is label-grade
    double initSigR     = 15.0;             // KF init σ_r (px)
    double initSigRdot  = 1500.0;           // KF init σ_ṙ (px/s)
    double measSigBase  = 8.0;              // measurement σ_r = measSigBase + (1−zconf)·measSigSlope
    double measSigSlope = 40.0;
    double flipConfRatio = 1.3;             // 180° flip suspect: confOpp > max(flipConfRatio·confFwd, flipConfAbs)
    double flipConfAbs   = 0.5;

    // Apply "shaft.head.*" dotted keys from a SwingLab tuning map onto the
    // defaults. B3 calls this (ShaftV3Config::fromOverrides lives in
    // shaft_track_assembly.cpp which Phase B1 does not edit; add one line there:
    // `cfg.head = ClubheadConfig::fromOverrides(ov);`).
    static ClubheadConfig fromOverrides(const QVariantMap &ov);
};

// Per-swing scene context — everything computed ONCE that the per-frame
// measurement reuses. B3 builds it right after the float32 sceneMed exists
// (decideTrack already computes sceneMed as the median of every-8th frame; the
// Python instead samples 11 frames spanning the clip — B3 reuses decideTrack's,
// a documented deviation). sceneMed empty ⇒ no scene reference (change gate open,
// permanence veto off) — the honest degradation for clips with no club-free span.
struct HeadSceneCtx {
    cv::Mat sceneMed;                       // CV_32F, or empty
    cv::Mat sceneMedGx, sceneMedGy;         // Sobel of sceneMed (permanence veto); empty ⇒ veto off
    int     W = 0, H = 0;
    double  rMin = 0.0;                     // max(20, rMinFrac·H) — skip the hands
    std::vector<double> offsets;            // lateral offsets across the ray normal (from latMaxPx)
    ClubheadConfig cfg;
};

// Build the per-swing context (computes sceneMed's Sobel + the lateral offset set
// + rMin). sceneMed32 may be empty.
HeadSceneCtx makeHeadSceneCtx(const cv::Mat &sceneMed32, int W, int H, const ClubheadConfig &cfg);

// Radius at which the ray grip + r·dir(θ) leaves the [0,W)×[0,H) frame (px).
// 0 when the grip is already outside / the ray never exits forward.
double rayEdgeRadius(double gx, double gy, double thetaDeg, int W, int H);

// Per-frame arm-length plausibility floor (px): armFactor × max(|leadSho−grip|,
// |trailSho−grip|) — the club is always longer than the lead arm. Applied ONLY
// where the caller says the frame is quasi-still (grip near-stationary AND before
// the top); returns −1 otherwise. Conf-gating is dropped vs the Python (the
// smoothed decideTrack joints carry no per-keypoint confidence — same as the
// existing length-ladder arm floor at shaft_track_assembly.cpp).
double armFloorPx(const cv::Point2d &leadSho, const cv::Point2d &trailSho,
                  double gx, double gy, bool quasiStill, const ClubheadConfig &cfg);

// B2 hard floor: in quasi-still pre-top frames or the impact window the head can
// be no closer than the arm and no much-shorter than the measured club:
// max(armFloor, hardFloorFactor·L_px). Returns armFloor (or −1) when L_px<0.
// `applies` false ⇒ −1 (moving frames get no hard floor — foreshortening legit-
// imately collapses r at the top; that IS the un-built L-collapse, measured now).
double hardFloorPx(double armFloor, double lPx, bool applies, const ClubheadConfig &cfg);

// Per-frame search bounds for measureHeadRadius under the B2 ball-length model.
//   rLo    = ctx.rMin (skip the hands).
//   rHi    = min(rayEdge, annulusCeilFactor·L_px)  when L_px > 0,
//            min(rayEdge, fallbackCeilPx)          when L_px ≤ 0 and fallbackCeilPx > 0
//                                                  (the caller's ladder length projLenPx,
//                                                   doubling as the no-ball ceiling),
//            rayEdge                               otherwise.
//   rFloor = max(hardFloorPx(armFloor, L_px, floorApplies),      still/impact hard floor
//                floorFrac·L̂)                                    UNIVERSAL acceptance floor,
//            where L̂ = L_px when > 0, else fallbackCeilPx; −1 = no floor at all,
//            and floorFrac = floorFracOverride when ≥ 0, else cfg.measFloorFrac.
// floorFracOverride is the phase-ramp hook: the wiring passes the per-frame
// ramped fraction (floorRampHi → measFloorFrac across the backswing, mirrored
// through the downswing) while this module stays phase-blind; −1 (default)
// reproduces the constant-fraction behaviour exactly.
// The universal floor applies on EVERY frame (floorApplies gates only the strong
// 0.8·L_px + arm floor). rFloor filters CANDIDATES inside measureHeadRadius, it
// does not shrink the scan range — the scan still runs [rLo, rHi] so support
// accumulates from the grip and a true terminus past a sub-floor counterfeit can
// win; a frame whose only candidates sit below the floor yields no measurement
// (NaN → pred/off downstream), never a blessed counterfeit.
struct HeadBounds { double rLo = 0.0, rHi = 0.0, rFloor = -1.0; };
HeadBounds headBounds(double rayEdge, double lPx, double fallbackCeilPx,
                      double armFloor, bool floorApplies, const HeadSceneCtx &ctx,
                      double floorFracOverride = -1.0);

// The Gaussian-prior centre passed to measureHeadRadius: L_px in quasi-still
// frames, NaN otherwise / when L_px < 0. Moving frames run prior-free — the KF
// gating does the temporal discrimination; never feed the KF its own prior (B2).
double headPrior(double lPx, bool quasiStill);

// One per-frame terminus measurement. rPx NaN ⇒ no admissible terminus.
struct HeadMeasurement {
    double rPx  = std::numeric_limits<double>::quiet_NaN();
    double conf = 0.0;
};

// Gap-tolerant on-axis terminus of the club line along the decided ray (H1).
//   support = (thin-line OR moving) AND (changed-vs-sceneMed OR moving)
//             AND NOT permanent-line (sceneMed's own edge-pair)
// candidate termini = ends of locally-sustained support segments; the winner
// maximises tail-evidence-quality × length-model prior. Continuity is NEVER
// required (a 75-px specular blowout gap must not terminate the run). Output is
// ON-AXIS (grip + rPx·dir(θ)) — no lateral centroid (the hand label sits at the
// shaft-axis terminus, not the blade-mass centroid).
//   gray32/prev32/bg32 : current frame, previous frame, running background (CV_32F)
//   gxs/gys            : Sobel(gray, x)/(y) — FULL-frame CV_32F (B3 may compute
//                        these over an ROI written into a full-size Mat; the ray
//                        only samples inside the annulus so BORDER_CONSTANT=0
//                        outside the ROI is harmless)
//   rFloor < 0         : no plausibility floor.   lPrior NaN : prior-free.
HeadMeasurement measureHeadRadius(const cv::Mat &gray32, const cv::Mat &prev32,
                                  const cv::Mat &bg32, const cv::Mat &gxs, const cv::Mat &gys,
                                  const HeadSceneCtx &ctx, double gx, double gy, double thetaDeg,
                                  double rLo, double rHi, double rFloor, double lPrior);

// 180° flip suspicion: the opposite ray (θ+180) decisively out-supports the
// forward one — confOpp > max(flipConfRatio·confFwd, flipConfAbs). B3 computes
// confOpp with a second measureHeadRadius on θ+180. The ray is never corrected
// (decoupling): a suspect frame is only refused meas-tier blessing (fed as
// flipSuspect into runHeadTemporal). Only meaningful when confFwd is from a
// finite forward measurement.
bool isFlipSuspect(double confFwd, double confOpp, const ClubheadConfig &cfg);

// ── H2 temporal model ────────────────────────────────────────────────────────
enum class HeadTier : uint8_t { Off = 0, Pred = 1, Meas = 2 };

// [r, ṙ] constant-velocity 1-D Kalman with white-accel Q (clubhead_annotate Kf1D).
// Deliberately tiny (2-state, no angle wrapping — r is a plain scalar). The RTS
// gain uses the STORED predicted covariance (hist tuples), same as the stage-1
// smoother; init/step/rts mirror the Python byte-for-byte.
class HeadKf1D {
public:
    HeadKf1D(double dt, const ClubheadConfig &cfg);
    void   init(double r);
    // Predict then (if hasZ and within the 3σ gate) update. Returns whether the
    // measurement was accepted.
    bool   step(bool hasZ, double z, double sigR);
    double rdot() const { return m_x1; }               // current posterior ṙ (px/s) — coast-budget switch
    std::size_t size() const { return m_hist.size(); } // number of stored steps
    void   trimTail(int n);                            // drop the last n coasted steps before RTS
    // RTS smooth over the stored history: rOut[k] = smoothed r, varOut[k] = its variance.
    void   rts(std::vector<double> &rOut, std::vector<double> &varOut) const;

private:
    double m_dt;
    double m_x0 = 0.0, m_x1 = 0.0;                      // state [r, ṙ]
    double m_P00 = 0, m_P01 = 0, m_P10 = 0, m_P11 = 0;  // covariance
    double m_Q00 = 0, m_Q01 = 0, m_Q10 = 0, m_Q11 = 0;  // process noise
    double m_initP00 = 0, m_initP11 = 0;                // init covariance seeds (cfg)
    double m_gate2 = 9.0;                               // gateSig²
    struct Hist {
        double x0, x1, P00, P01, P10, P11;              // posterior at step k
        double xp0, xp1, Pp00, Pp01, Pp10, Pp11;        // prediction INTO step k (P_{k|k-1})
    };
    std::vector<Hist> m_hist;
};

// Frameless inputs to the temporal model. All vectors are one entry per span
// frame in decode order; a frame whose thetaDeg is NaN does not participate
// (no shaft θ there). z is the raw measureHeadRadius radius (NaN = none), zconf
// its confidence. s1IsMeas[i] = the stage-1 tier for frame i is a real vision
// measurement (RAY|BAND); flipSuspect[i] = isFlipSuspect fired. rEdge = the ray-
// edge radius (off/clamp). lPx = ball-measured club length (px, <0 = no ball) —
// the off-tier + pred-fallback L_hat when no smoothed value exists (this is where
// L_px replaces the Python's self-fit L_pred). dt = seconds per frame (TRUE fps).
struct HeadTemporalInput {
    std::vector<double> z, zconf;
    std::vector<double> thetaDeg;           // decided θ (deg) — for segment breaks
    std::vector<char>   s1IsMeas, flipSuspect;
    std::vector<double> rEdge;
    double lPx = -1.0;
    double dt  = 0.0;
    ClubheadConfig cfg;
};

struct HeadFrameResult {
    double   rOut     = std::numeric_limits<double>::quiet_NaN(); // radial estimate (NaN = none / off)
    float    headConf = 0.0f;
    HeadTier tier     = HeadTier::Pred;
    double   sigmaR   = std::numeric_limits<double>::quiet_NaN(); // posterior σ_r (px); NaN = no smoothed value
};

// Segmented KF + per-segment RTS + honesty tiers (clubhead_annotate temporal
// pass). Segments split on stage-1 θ jumps > segThetaJump and on coast-budget
// overrun; RTS runs PER SEGMENT (never across a break); coasted tails are trimmed
// before smoothing. Output is length in.thetaDeg.size(), aligned to the input.
// Deterministic: same input ⇒ byte-identical output.
std::vector<HeadFrameResult> runHeadTemporal(const HeadTemporalInput &in);

} // namespace pinpoint::analysis
