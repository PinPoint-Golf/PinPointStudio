// swing_comparator.h — SwingComparator2D (WP2a), reference-vs-measured diagnostics
// in IMAGE space.
//
// The 3D idealised reference (src/Models/swing_reference.h) is projected through
// the SAME per-camera CameraProjection (src/Analysis/camera_projection.h) that the
// measured 2D shaft track passed through, so reference and measurement carry
// identical projection ambiguity and a plain signed subtraction remains a valid
// diagnostic. This is the permanent single-camera (phone) product mode; the 3D
// comparator lands later behind the same SwingComparator ABC.
//
// Phase alignment is by PHASE, never wall-clock time: detected P-events map the
// measured clock onto the reference's s-grid, so tempo cancels by construction.
// GLOBAL phase s runs [0,3] over the whole trajectory — Backswing [0,1] (P1→P4),
// Downswing [1,2] (P4→P7), FollowThrough [2,3] (P7→P8) — which is monotone in both
// s and t and lets the flat MaskEntry {sLo,sHi} address any span of the swing.
//
// D5 (DTL-ready, face-on enabled): every diagnostic compiles for both camera
// placements. Face-on diagnostics are enabled (real mask weight); DTL diagnostics
// are implemented + unit-tested against a synthetic DTL camera but DARK by default
// (mask weight 0) — the live pipeline only ever calls this with the face-on view.
// D6 (validity mask = data): the mask is a table of MaskEntry rows, not code.
//
// Namespace pinpoint::swingref. C++17, deterministic (no wall-clock, no RNG, no
// unordered iteration into outputs). Qt value types + quaternions; no OpenCV in
// this header (the projection hides it).

#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector3D>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "camera_projection.h"          // CameraProjection, ProjectedShaftLine
#include "swing_analysis.h"             // analysis::ShaftTrack2D/PoseTrack2D/Segmentation/MetricSeries
#include "../Models/swing_reference.h"  // SwingReferenceModel, GolferAnthro, Segment
#include "../Core/pp_tuned_constants.h" // tuned::swingref:: — frozen Phase A defaults

namespace pinpoint::swingref {

// Camera placement the comparison is run for (CameraPlacement-like). Face-on is
// enabled live; DTL is unit-tested only (dark by default).
inline constexpr int kViewFaceOn = 0;
inline constexpr int kViewDtl    = 1;

// ── Validity mask (D6: data, not code) ──────────────────────────────────────
// One masking row. A row matches a diagnostic series when diagnosticId + view
// agree and the sample's GLOBAL s lies in [sLo,sHi]. Overlapping matches REDUCE
// the weight (effective weight = min over matching rows, starting at 1.0), so a
// weight-0 row darkens a span regardless of any other row. Fractional weights are
// supported by the same min rule (the defaults only use 0 and 1).
struct MaskEntry {
    QString diagnosticId;
    int     view = kViewFaceOn;
    double  sLo = 0.0, sHi = 3.0;   // GLOBAL s span [0,3]
    double  weight = 0.0;
};

// The default mask table (D6). Face-on plane-pitch descriptives are dark
// everywhere (face-on cannot see plane pitch); every DTL diagnostic is dark by
// default (DTL never runs live in Phase A). The physically-meaningful DTL live
// mask — inclination/yaw conflation over backswing s∈[0.25,0.75] — is present as
// its own row so Phase B can drop the blanket DTL rows and keep it.
std::vector<MaskEntry> defaultMaskTable();

// Effective mask weight for (diagnosticId, view, globalS) over a table.
double maskWeightFor(const std::vector<MaskEntry>& table,
                     const QString& diagnosticId, int view, double globalS);

// ── PhaseMap: measured clock ↔ reference GLOBAL s ───────────────────────────
// Built from P timestamps — preferring ShaftTrack2D.positions (P1..P8), falling
// back per-P to Segmentation.eventFor. Non-monotone interior anchors are dropped;
// the two present endpoints of the covered range are required. Interpolation is
// monotone piecewise-linear between adjacent P anchors, so both directions are
// single-valued. Anchor s-positions are the reference model's own keyframe
// s-positions (brief §Keyframe constraints): P2@0.33 P3@0.66 in the backswing,
// P5@0.40 P6@0.80 in the downswing — so a detected Pk maps to the exact phase
// where the reference's Pk constraint lives.
class PhaseMap {
public:
    PhaseMap() = default;
    PhaseMap(const analysis::ShaftTrack2D& shaft, const analysis::Segmentation& seg);

    bool valid() const { return m_anchors.size() >= 2; }

    // Measured time → GLOBAL s [firstAnchor.s, lastAnchor.s], nullopt outside range.
    std::optional<double>  toGlobalS(int64_t t_us) const;
    // GLOBAL s → measured time, nullopt outside the anchored s range.
    std::optional<int64_t> toTime(double globalS) const;
    // Measured time of a specific P (1..8), nullopt if that P was not anchored.
    std::optional<int64_t> timeForP(int p) const;

    // GLOBAL s of P-index p (1..8); brief keyframe table. -1 for out of range.
    static double globalSForP(int p);
    // GLOBAL s → (Segment, local s∈[0,1]).
    static std::pair<Segment, double> segmentForGlobalS(double globalS);

    struct Anchor { int p = 0; double s = 0.0; int64_t t_us = 0; };
    const std::vector<Anchor>& anchors() const { return m_anchors; }

private:
    std::vector<Anchor> m_anchors;   // ascending in both s and t_us
};

// ── Diagnostic + result types ───────────────────────────────────────────────
struct DiagnosticSample {
    double  s      = 0.0;   // GLOBAL s [0,3]
    int64_t t_us   = 0;     // measured time (ascending)
    double  value  = 0.0;   // diagnostic value (unit-specific)
    double  weight = 1.0;   // effective mask weight (0 = masked; kept for the overlay)
};

// A rich, weighted diagnostic trace. Masked samples are KEPT here (weight 0) so
// the overlay can render explicit gaps and the DTL values stay inspectable; the
// catalogue-facing MetricSeries below drop them.
struct DiagnosticSeries {
    QString id;
    int     view = kViewFaceOn;
    QString unit;                          // "deg" | "cm" | "px"
    std::vector<DiagnosticSample> samples; // ascending s and t_us
};

// A per-segment reduction of the shaft-angle delta (mask-weighted). n is the count
// of samples with weight > 0 that fed rms/peak.
struct SegmentStat {
    double rmsDeg  = 0.0;
    double peakDeg = 0.0;
    int    n       = 0;
};

// Plain summary — computed here; thresholds / grading are config-owned (a later
// task) and deliberately absent. Every *Valid flag / n gates whether the scalar
// is meaningful.
struct ScorecardSummary {
    // Shaft-angle delta (a), per segment.
    SegmentStat backswing;   // P1→P4
    SegmentStat downswing;   // P4→P7
    SegmentStat exit;        // P7→P8 (exit trace)

    // Descriptive plane shift: peak |shaft-angle delta| over the downswing + the
    // GLOBAL s at which it peaks.
    double planeShiftMagDeg  = 0.0;
    double planeShiftPhaseS  = -1.0;   // -1 = n/a
    int    planeShiftN       = 0;

    // P4 laid-off/across (DTL): signed shaft-angle delta at Top.
    double p4LaidOffDeg   = 0.0;
    bool   p4LaidOffValid = false;

    // Lag retention: measured lead-arm↔shaft image angle (lag proxy, deg) at
    // downswing s≈0.75.
    double lagRetentionDeg   = 0.0;
    bool   lagRetentionValid = false;

    // Tempo ratio backswing/downswing duration (dimensionless, measured swing).
    double tempoRatio  = 0.0;
    bool   tempoValid  = false;

    // Transition timing: Top (P4) → lead-arm-parallel-down (P5), microseconds.
    int64_t transitionTimeUs = -1;

    // P7 forward-lean delta: signed shaft-angle delta at Impact (same convention
    // as refShaftDelta).
    double leanDeltaP7Deg   = 0.0;
    bool   leanDeltaP7Valid = false;

    // Butt-point deviation from the ball-line at P3 / P5 (DTL), px (measured grip
    // − projected reference grip, along the ball-line image normal).
    double buttDeviationP3Px = 0.0;
    double buttDeviationP5Px = 0.0;
    bool   buttDeviationValid = false;

    // Projection residual passthrough (px); -1 = n/a.
    double projectionResidualPx = -1.0;
};

struct ComparisonResult {
    bool valid = false;
    std::vector<DiagnosticSeries>       diagnostics;   // rich, weighted (masked kept)
    std::vector<analysis::MetricSeries> metrics;       // refShaftDelta/refLagDelta/refHubShift
    ScorecardSummary                    summary;
};

// anthro carries the reference hub (3D) + arm length + handedness the β-proxy /
// arm-line diagnostics need — the stage fills it from the same estimate that
// built the model. `fromOverrides` maps ONLY "swingref.samplesPerSegment" onto
// sGridPerSegment — the SAME dotted key RefConfig::samplesPerSegment uses (one
// tuning knob feeding both the model's own sampling density and the comparator's
// s-grid resolution). The other fields have no tuned-constant/dotted-key seam yet.
struct ComparatorConfig {
    int    sGridPerSegment = tuned::swingref::kSamplesPerSegment; // swingref.samplesPerSegment (reused key — see RefConfig)
    double kpConfMin       = 0.30;   // pose keypoint confidence floor (β-proxy, hub) — not yet tunable
    double shaftConfMin    = 0.0;    // measured shaft-sample θ confidence floor — not yet tunable
    double pxPerM          = 1000.0; // hub-shift image px → metres (stage supplies real scale) — not yet tunable

    GolferAnthro anthro;             // reference hub / arm length / handedness
    bool         anthroValid = false;// false ⇒ β-proxy + arm-line diagnostics skipped

    std::vector<MaskEntry> maskOverride;   // empty ⇒ defaultMaskTable()

    static ComparatorConfig fromOverrides(const QVariantMap &ov);
};

// ── SwingComparator ABC + factory ───────────────────────────────────────────
class SwingComparator {
public:
    virtual ~SwingComparator() = default;

    virtual ComparisonResult compare(const SwingReferenceModel&    model,
                                     const CameraProjection&       proj,
                                     const analysis::ShaftTrack2D& shaft,
                                     const analysis::PoseTrack2D&  pose,
                                     const analysis::Segmentation& seg,
                                     double clubLenM, int view,
                                     const ComparatorConfig& cfg) const = 0;

    virtual QString tier() const = 0;   // "2D" | "3D"
};

enum class ComparatorTier { TwoD, ThreeD };

// TwoD → the image-space comparator. ThreeD is Phase B (returns nullptr today).
std::unique_ptr<SwingComparator> makeSwingComparator(ComparatorTier tier = ComparatorTier::TwoD);

} // namespace pinpoint::swingref
