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

// Down-the-line shaft tracker — the PRODUCT types (dtl_shaft_tracker_design.md
// §5.9, §5.11): the per-frame sample, the bands, the DTL ball and the track that
// carries them. Split out of dtl_shaft_types.h so SwingAnalysis can hold a
// DtlShaftTrack2D (analysis.clubDtl) without every includer of swing_analysis.h
// inheriting OpenCV. No cv:: here, and none may be added — the working types that
// need it (the witness's companions, the decide/post inputs) stay in
// dtl_shaft_types.h, which includes this.

#include <QPointF>
#include <QString>
#include <QJsonObject>

#include <cstdint>
#include <limits>
#include <vector>

#include "types.h"                // pinpoint::SourceId

namespace pinpoint::analysis {

// ── what a DTL frame earned ──────────────────────────────────────────────────
// The first six are the original ladder, least- to most-evidenced, and their
// integer values are FROZEN: the trace and the persisted tier ints of every run
// before 2026-10-02 read them, and a bit-identical "continuous track OFF" run
// depends on nothing renumbering. The three added by the continuous-track update
// (dtl_continuous_track_design_update.md §3.1) are APPENDED, so "published"
// can no longer be spelled `>= Ray` — use dtlMeasured() / dtlDrawn() below.
//
//   Held          — a frame INSIDE a sighted band that failed the RAY gate, bounded
//                   on both sides by measured frames of the SAME band, carrying the
//                   band's own Viterbi θ_D. Drawn (dimmer), never fed to fusion or
//                   any metric, never across an end-on gap. Not the PRED tier the
//                   design rejected: PRED invented measurements at impact across a
//                   gap; this coasts a solved path between two measurements.
//   OccludedWrist — the anchor was quarantined because the pose had no confident
//                   wrist: the HANDS were hidden, the club may be in plain view.
//   OccludedRow   — quarantined on the cross-view row residual: the anchor is not
//                   where face-on says the hands are.
//   Occluded stays for the two causes that name neither (no grip at all, or a
//   caller-supplied quarantine).
enum class DtlTier : uint8_t {
    Unseen = 0, EndOn = 1, Occluded = 2, Ray = 3, Seg = 4, Band = 5,
    Held = 6, OccludedWrist = 7, OccludedRow = 8
};

// A MEASURED angle: earned from DTL pixels (§5.9). This is what fusion, the
// P-position borrow and every count of "published" read.
inline bool dtlMeasured(DtlTier t)
{ return t == DtlTier::Ray || t == DtlTier::Seg || t == DtlTier::Band; }
// DRAWN on the tile: measured, or held inside a band between two measurements.
inline bool dtlDrawn(DtlTier t) { return dtlMeasured(t) || t == DtlTier::Held; }
// The anchor was quarantined, whichever cause.
inline bool dtlOccluded(DtlTier t)
{ return t == DtlTier::Occluded || t == DtlTier::OccludedWrist || t == DtlTier::OccludedRow; }

inline const char* dtlTierName(DtlTier t)
{
    switch (t) {
        case DtlTier::Unseen:        return "UNSEEN";
        case DtlTier::EndOn:         return "END_ON";
        case DtlTier::Occluded:      return "OCCLUDED";
        case DtlTier::Ray:           return "RAY";
        case DtlTier::Seg:           return "SEG";
        case DtlTier::Band:          return "BAND";
        case DtlTier::Held:          return "HELD";
        case DtlTier::OccludedWrist: return "OCCLUDED_WRIST";
        case DtlTier::OccludedRow:   return "OCCLUDED_ROW";
    }
    return "UNSEEN";
}

// Why an anchor was quarantined (dtl_shaft_decide's cross-view check, §5.2),
// carried on the solve state so the tier ladder can NAME the cause rather than
// parse it back out of a reason string.
enum class DtlQuarantineCause : int8_t { None = 0, NoGrip = 1, Wrist = 2, RowResidual = 3, Caller = 4 };

namespace dtl {
// Absent numerics are NaN, never 0 and never −1: 0 is a legal angle and −1 a
// legal cost, and the whole point of the tier ladder is that "we did not measure
// this" must not be readable as a measurement.
inline constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
} // namespace dtl

// ── where ρ̂_D's face-on input came from ──────────────────────────────────────
// Measured: face-on published a club length on this frame and ρ_F is that ratio.
// Bound: face-on measured an ANGLE but no length (it coasts at address and
// reconstructs at impact), so the schedule substitutes ρ_F := 1, which is the
// CONSERVATIVE side — it maximises |u_x| = |cos θ_F| and therefore MINIMISES
// ρ̂_D, so a near-horizontal face-on shaft of unknown length still reads END-ON
// while a near-vertical one (address, impact) reads sighted. It is a bound on the
// SCHEDULE only: the corridor (D5) and the half-plane (D4) still require a
// measured face-on tier, because those read the face-on geometry itself rather
// than "could anything be seen here at all".
// None: no witness on this frame at all.
enum class DtlRhoSrc : uint8_t { None, Measured, Bound };

inline const char* dtlRhoSrcName(DtlRhoSrc s)
{
    switch (s) {
        case DtlRhoSrc::None:     return "none";
        case DtlRhoSrc::Measured: return "measured";
        case DtlRhoSrc::Bound:    return "bound";
    }
    return "none";
}

// ── where the published lenPx was measured ───────────────────────────────────
// The visible run is what the minimum-length rule is asked about, so HOW it was
// measured is part of the claim. Rend is the DP's own terminus radius, read off
// a ray cast from the POSE grip — and the pose grip sits tens of px off the shaft
// axis, so that ray leaves the thin shaft after ~100 px however long the club
// really is. MEASURED on the dev six: rEnd was EXACTLY 98 px (ridgeSweep's floor,
// rLo 8 + minLenPx 90) on 51–101 of the refused frames per swing — the ridge
// score's argmax sitting at its own floor, which is not a length at all. The two
// other sources measure the run AFTER the line has been re-registered:
//   SnapLine — along the SNAPPED line from the snapped grip point;
//   LatBand  — along the solved θ from the pose grip, the best of a few lateral
//              origin offsets, for frames the snap did not or could not move.
// Face-on made this same correction ("the head search never looked at the club")
// by measuring off the re-registered line, and this is that sentence in this view.
//   Schedule — (continuous-track update §3.1 "one length per band") the DRAWN
//              length is the visibility law's own ρ̂_D · L̂_D, with L̂_D from the
//              ball at address and held for the whole swing; the measured run the
//              ladder judged is carried beside it as `runPx`. The three sources
//              above switched frame by frame, and that switching was the flicker
//              in the drawn head.
enum class DtlLenSrc : uint8_t { Rend, SnapLine, LatBand, Schedule };

inline const char* dtlLenSrcName(DtlLenSrc s)
{
    switch (s) {
        case DtlLenSrc::Rend:     return "rend";
        case DtlLenSrc::SnapLine: return "snapLine";
        case DtlLenSrc::LatBand:  return "latBand";
        case DtlLenSrc::Schedule: return "schedule";
    }
    return "rend";
}

// ── where the RAY tier's evidence gate was satisfied ─────────────────────────
// Ev is the normalised E2 evidence read along a ray FROM THE POSE GRIP, and that
// ray is the same off-axis ray DtlLenSrc is a record of: the grip anchor sits
// tens of px off the shaft axis, so on a frame where the shaft is a plainly
// visible bright streak the ray reads 0.37–0.40 against a 0.45 gate. LineConf is
// the alternative the snap already earned — the support under the RE-REGISTERED
// line, which is a measurement of the line the frame would publish rather than of
// a ray nobody draws. It is admissible only where the snap was ACCEPTED (it had
// to beat its own origin line, clear the limb veto and stay inside the ball gate
// to be), and it replaces the EV gate alone: SUP, the limb veto, the minimum
// length, the ball gate and the schedule all still apply.
enum class DtlEvSrc : uint8_t { Ev, LineConf };

inline const char* dtlEvSrcName(DtlEvSrc s)
{
    switch (s) {
        case DtlEvSrc::Ev:       return "ev";
        case DtlEvSrc::LineConf: return "lineConf";
    }
    return "ev";
}

// ── why a reverse ray was not held against a frame (D1, DTL) ─────────────────
// None means the test stood as it is written. Arm means the reverse direction
// runs up the golfer's own arm; BallGate that the direction was already decided
// by DTL's own ball. Both are recorded rather than inferred, because "the test
// passed" and "the test was waived" are different statements about a frame.
enum class DtlRevWaiver : uint8_t { None, Arm, BallGate };

inline const char* dtlRevWaiverName(DtlRevWaiver w)
{
    switch (w) {
        case DtlRevWaiver::None:     return "";
        case DtlRevWaiver::Arm:      return "arm";
        case DtlRevWaiver::BallGate: return "ballGate";
    }
    return "";
}

// ── the limb veto's joint set (D2, generalised) ──────────────────────────────
// Face-on's C2 free-space schedule does not port (§5.6), and at address and
// impact the DTL shaft runs down the trouser line to the feet: a long,
// high-contrast leg edge out of the hands that ties the shaft on evidence alone.
// The discriminator is the one the research record found the counterfeit fails —
// the ray passes CLOSE TO A JOINT as well as along its direction — so the veto is
// asked of every limb joint below the shoulders, not only the elbows.
// Deliberately NOT the shoulders or the head: at P3 the true shaft passes near
// them, and a veto there would refuse a real frame.
enum class DtlLimb : int8_t {
    None = -1, LeadElbow = 0, TrailElbow = 1,
    LeftHip = 2, RightHip = 3, LeftKnee = 4, RightKnee = 5, LeftAnkle = 6, RightAnkle = 7
};

inline const char* dtlLimbName(int j)
{
    switch (j) {
        case 0: return "leadElbow";
        case 1: return "trailElbow";
        case 2: return "leftHip";
        case 3: return "rightHip";
        case 4: return "leftKnee";
        case 5: return "rightKnee";
        case 6: return "leftAnkle";
        case 7: return "rightAnkle";
    }
    return "";
}

// One DTL frame. Emitted for EVERY frame in the swing span, including the
// absences — `tier` and `reason` say what happened, the numerics are NaN when
// nothing was measured.
struct DtlSample {
    int64_t t_us        = 0;
    QPointF gripPx;                      // DTL pose anchor (image px)
    QPointF headPx;                      // measured terminus, or absent (see tier)
    double  thetaRad    = dtl::kNan;     // image angle, atan2 convention (NOT unwrapped)
    double  lenPx       = dtl::kNan;     // the DRAWN length along θ (the evidenced run, or ρ̂_D·L̂_D — see lenSrc)
    DtlLenSrc lenSrc    = DtlLenSrc::Rend;   // … and where that number came from
    // The evidenced run the tier ladder actually judged, kept beside lenPx once
    // lenSrc is Schedule so the measurement is never lost behind the drawn length.
    // NaN where none was measured (a Held frame) or where lenSrc is not Schedule
    // (then lenPx IS the run).
    double  runPx       = dtl::kNan;
    // … and which gate let the RAY tier through: the ray's own EV, or the support
    // under the snapped line. Ev on an absence, because a frame that published
    // nothing satisfied neither.
    DtlEvSrc  evSrc     = DtlEvSrc::Ev;
    float   conf        = 0.f;           // 0..1
    DtlTier tier        = DtlTier::Unseen;
    // The visibility schedule (§5.3/§5.7), inherited: ρ̂_D = √(1 − u_x²) with
    // u_x = ρ_F·cos θ_F from the face-on witness. This is what makes a forearm
    // lock at the top impossible rather than merely out-scored.
    double  rhoPred     = dtl::kNan;
    // … and WHERE its face-on input came from. A ρ̂_D built on the ρ_F := 1 bound
    // is a different claim from one built on a measured length, and a report that
    // cannot tell them apart cannot say which of the two best-seen moments of the
    // swing the tracker is standing on.
    DtlRhoSrc rhoSrc    = DtlRhoSrc::None;
    // The face-on corridor as it stood on this frame. Two centres because the
    // depth sign is unknown until the sign table settles the phase; corrHalfDeg
    // is the measured half-width, NOT an assumed one.
    double  corrCentreDeg[2] = { dtl::kNan, dtl::kNan };
    double  corrHalfDeg = dtl::kNan;
    bool    corridorOn     = false;      // false ⇒ gated off (tier PRED/RECON, ρ_F > max, finish)
    bool    corridorEscape = false;      // clean evidence won OUTSIDE the corridor — always worth a frame number
    int     band        = -1;            // index into DtlShaftTrack2D::bands; −1 = in a gap
    QString reason;                      // why an absence is an absence; empty when published
    double  bandS       = dtl::kNan;     // E1 lock scale (px/mm) when tier == Band
    double  bandR0      = dtl::kNan;     // E1 lock butt→anchor offset (mm) when tier == Band
    // Shaft uncertainty (shaft_uncertainty_propagation_design.md §4.1): 1σ of thetaRad in
    // degrees and the gross-error probability; NaN = not assessed. The DTL table is
    // calibrated at address only — beyond it σ is inflated by 1/ρ̂_D and tagged propagated.
    double  sigmaThetaDeg = dtl::kNan;
    double  pGross        = dtl::kNan;
};

// A maximal run of solvable frames (§5.8). Bands are solved INDEPENDENTLY;
// nothing connects them and no sample is published in a gap.
struct DtlBand {
    int     lo = -1, hi = -1;            // inclusive frame indices
    int64_t loUs = 0, hiUs = 0;
    QString name;                        // e.g. "address", "P2.5-P3.5" — for the report, never gated on
    // Admitted by the band-EDGE rule (continuous-track update §3.1): a sighted run
    // shorter than minBandFrames that sits within a few non-end-on frames of a
    // full band. Solved on its own, joined to nothing, and the Held tier never
    // crosses the hole between them.
    bool    edge = false;
};

// ── the DTL ball (§4.3, §5.5) ────────────────────────────────────────────────
// A direct DTL measurement: at address and impact the head is AT the ball, so
// grip→ball is an angle prior that owes face-on nothing — and it covers exactly
// the two bands where the corridor of §4.1 (c) is degenerate. `found` false is
// the normal answer, not a failure: `reason` says which gate refused, and D6 is
// then simply absent (never a fabricated centre).
// ONE detector, TWO cues, ONE verdict. The bright cue is the ball imaged as a
// bright compact blob; the shadow cue is the ball's own contact shadow on a mat
// blown so white the ball has no edge on it (06-11: 0 of 9 by brightness, and
// the address hold is the wrong time to look at all, because the clubhead and
// its shadow sit on top of the ball there). The shadow cue is only CONSULTED
// where the bright one found nothing, but it is always MEASURED, so a trace can
// put the two beside each other on a scene where both exist.
struct DtlBall {
    bool    found = false;
    double  x = dtl::kNan, y = dtl::kNan;   // image px
    QString reason;
    // Which cue answered. "none" when there is no ball — a first-class outcome.
    QString source = QStringLiteral("none");        // "bright" | "shadow" | "none"
    // The shadow cue's own numbers, recorded whether or not it supplied the
    // verdict: the crescent's centroid (the ball centre is radiusPx ABOVE it),
    // how much brightness that patch regained once the ball had gone, the radius
    // the scene scale implied, and how many candidates survived the shape gates.
    double  shadowX = dtl::kNan, shadowY = dtl::kNan;
    double  launchRise = dtl::kNan;
    double  radiusPx   = dtl::kNan;
    int     nCandidates = 0;
};

// Band-lock truth, generated WITHOUT any face-on input (§6) — so the instrument
// that grades the coupling does not depend on it.
struct DtlTruthSample {
    int64_t t_us = 0;
    double  thetaRad = dtl::kNan;
    double  s        = dtl::kNan;        // px/mm
    double  r0       = dtl::kNan;        // mm
    QPointF gripPx, headPx;
};

// One frame of the 3-D SYNTHETIC shaft projected into the DTL tile
// (dtl_shaft_synth3d.h; dtl_continuous_track_design_update.md §3.2). Never a
// measurement: drawn dimmer, read by no metric and no fusion, absent unless the
// stage was enabled. `plane` is synth3d::PlaneUsed as int, `anchorSrc`
// synth3d::AnchorSrc as int; `preview` is true when the camera it was projected
// through was the assumed-zero placement rather than a measured calibration.
struct DtlSynth3DSample {
    int64_t t_us     = 0;
    QPointF gripPx;                  // the anchor used (DTL px)
    QPointF headPx;                  // grip + lenPx·(cos θ, sin θ)
    double  thetaRad = dtl::kNan;    // projected DTL angle
    double  lenPx    = dtl::kNan;    // ρ_D · L̂_D
    double  rhoD     = dtl::kNan;
    double  u[3]     = { dtl::kNan, dtl::kNan, dtl::kNan };   // butt → head, cameras' frame
    int     plane    = 0;
    int     anchorSrc = 0;
    double  etaDeg   = dtl::kNan;    // (§3.2a A) the out-of-plane angle applied; NaN = in-plane
};

// The product. Produced in the app by DtlShaftStage (SwingAnalysis::shaftDtl) and
// by swinglab_run --dtl; persisted as swing.json `analysis.clubDtl` and SwingLab's
// club_dtl.json through the one builder in dtl_shaft_json.h (§5.11). Drawn on the
// DTL replay tile; no metric reads it.
struct DtlShaftTrack2D {
    bool  valid  = false;
    pinpoint::SourceId camera = pinpoint::kInvalidSourceId;
    int   frameWidth = 0, frameHeight = 0;
    int64_t clockOffsetUs = 0;           // DTL t_us − face-on t_us, 0 on a shared clock
    std::vector<DtlSample>      samples;
    std::vector<DtlBand>        bands;
    std::vector<DtlTruthSample> truth;
    // The second number §5.9 insists on: what fraction of the swing span was
    // sighted at all. Coverage over sighted-band frames alone is not honest by
    // itself — this is the denominator that stops a 0.95 hiding the gaps.
    double sightedFrac = 0.0;
    // A pin: how many samples were published in a frame whose ρ̂_D put it END-ON.
    // Should be 0. Anything else means the schedule was overruled.
    int    publishedInEndOn = 0;
    // ── the continuous-track update's own ledger (design update §3.1) ────────
    // `continuous` is true when ANY of its rules was enabled for this run, and
    // only then does the JSON carry a `summary.continuous` block — so a run with
    // every rule off writes the bytes it wrote before the update. Every count is
    // a count, never asserted: "held" is how many frames the HELD tier filled,
    // "lateEscapesRefused" how many post-P8 corridor escapes were kept off the
    // tile, "endOnBeforeQuarantine" how many quarantined frames the schedule had
    // already called end-on, "edgeBands" how many short runs the band-edge rule
    // admitted as bands of their own.
    bool   continuous = false;
    int    held = 0;
    int    lateEscapesRefused = 0;
    int    endOnBeforeQuarantine = 0;
    int    occludedWrist = 0, occludedRow = 0;
    int    edgeBands = 0;
    // The DTL ball and the DTL full club length L̂_D that D3/D6 were built on —
    // carried on the product because the report has to say which SOURCE the
    // length came from (§5.6 D3: address grip→ball / ρ̂_D, else the cross-view
    // row scale, else none). A length with no stated provenance is a number.
    DtlBall ball;
    double  lFullPx = dtl::kNan;
    QString lFullSource;          // "ball" | "faceOnRowScale" | "none"
    // The fitted cross-view row scale/offset (§5.2), carried out of the solve so
    // a report can say what the quarantine's verdicts were measured against.
    double  rowFitA = dtl::kNan, rowFitB = dtl::kNan;
    // WHICH rule placed the club-away window (§5.4): "P2P5" | "P4P7" | "P4+120" |
    // "P1impact" | "none". The window is the one input BOTH the phase-aware clean
    // plate's low region and the shadow ball cue are built from, and it is placed
    // from the face-on ladder — so on a swing whose ladder is short it is placed
    // by a FALLBACK, and a report that cannot say which rule ran cannot tell a
    // scene difference from a ladder difference.
    QString clubAwayWindow;
    // The config the track was produced under, as the `config` block and
    // summary.configHash of pinpoint.clubDtl/1 (dtlShaftConfigJson / dtlConfigHash),
    // so a writer holding only the track (serializeAnalysis) echoes the config that
    // RAN without needing DtlShaftConfig. Set by DtlShaftStage.
    QJsonObject configJson;
    QString     configHash;
    // The 3-D synthetic shaft's DTL projection (DtlSynth3DStage), empty unless
    // that stage ran and was enabled. `synth3dPreview` records that the camera was
    // uncalibrated. Persisted as `synth3d` + `summary.synth3d` only when non-empty.
    std::vector<DtlSynth3DSample> synth3d;
    bool    synth3dPreview = true;
    double  synth3dYawDeg = 0, synth3dPitchDeg = 0, synth3dRollDeg = 0;
    // The recorded serial of the stream this track is OF (the window's
    // device_serial for `camera`), so a writer holding the manifest can name the
    // stream (dtlStreamName) without the SwingWindow. Not serialised itself.
    QString streamSerial;
};

} // namespace pinpoint::analysis
