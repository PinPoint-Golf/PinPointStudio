// swing_ref_anthro.h — GolferAnthro-from-2D estimation (WP4a, plan §3).
//
// Recovers the address-frame golfer anthropometry the swing-reference model
// needs — mid-shoulder hub (m, ball frame), lead-arm length (m), the image
// px/m scale, and the ball's along-target offset from stance centre — from the
// 2D products of ONE swing: the offline pose track, the shaft track, the ball
// track, and the segmentation. Pure, deterministic, degrades to std::nullopt on
// any missing input.
//
// This is the MONOCULAR seed used before (or without) a calibrated camera; it
// does NOT use CameraProjection. Depth along the camera axis is unobservable
// from a single face-on view, so hub/butt depth is a MODELLED quantity — see
// the geometry assumptions below. The P1/P7 anchor solve in the reference model
// keeps the reference touching measured reality regardless.
//
// ── Coordinate frame (identical to src/Models/swing_reference.h) ─────────────
//   origin = ball, X = target line (toward target +), Z = up,
//   Y = Z×X (away from a RH golfer, so a RH golfer's body sits at y < 0).
//
// ── Geometry assumptions (all documented, all deliberate) ────────────────────
//   A1. Face-on camera ≈ orthographic in the image X–Z plane. Image px map to
//       world (X, Z) by a single isotropic scale `pxPerM`; depth Y does not
//       change image scale. (True to first order for the long-lens FLIP rig;
//       the calibrated PnP path handles perspective when intrinsics exist.)
//   A2. Image axes: +x → +X (target line); +y is DOWN, so world +Z (up) maps to
//       DECREASING image y. Left-handed golfers mirror ONLY the world y sign
//       (T1 contract: "LH is exact y→−y mirror"); image x/z handling is
//       unchanged.
//   A3. At address the clubhead sits AT the ball (origin), so the P1 shaft
//       grip→head image vector IS the projected club (butt→head) vector, and its
//       image endpoint is the ball's image position. The origin/scale reference
//       is therefore the MEASURED P1 shaft head px (a primary tracker product),
//       NOT the detected ball centre — a single flaky ball blob (e.g. a spurious
//       static bright spot) otherwise corrupts BOTH the scale and the origin.
//       The detected ball is used only as a fallback origin when no P1 head
//       exists, and (when present) as an informational cross-check. The club's
//       image length prefers the fused club-length estimate (shaft.lengths.
//       fusedPx when confident, else measuredClubLenPx) over the raw grip→head
//       span, so the scale leans on the pipeline's own ±few-% club measurement.
//   A4. Shaft depth: the butt's camera-axis depth is seeded from the static lie
//       angle, |y_butt| = L_c·cos(lie) (lie measured from the ground). The
//       in-image projected club length is then L_c·sin(lie). A ≤5-iteration
//       fixed point re-solves y_butt against the club-length constraint given
//       the measured in-plane px split; it is a contraction that confirms the
//       seed within one step (documented — the seed dominates on face-on data).
//       NOTE the club LENGTH label (clubLengthM) is a near-pure world-scale
//       GAUGE here: it cancels from the recovered arm/club RATIO (a wrong label
//       — e.g. a 7-iron mislabelled as the driver default — rescales armLengthM
//       and pxPerM together and is absorbed by the projection). The club LIE is
//       NOT a gauge (it sets the in-plane/depth split), so lie must be right.
//   A5. Hub depth = butt depth + `hubDepthOffsetM` (hands hang under the
//       shoulders at address — the biggest single simplification; override keys
//       exist upstream). Hub X/Z come from the mid-shoulder px ÷ scale.
//   A6. Lead-arm length = |hub − butt3D| + `gripOffsetM` (grip end → mid-hands).
//
// C++17, Qt value types, no OpenCV. Deterministic (no wall-clock, no RNG,
// window sampling is conf-gated and order-independent).

#pragma once

#include <QtGlobal>
#include <QPointF>
#include <QVariantMap>

#include <limits>
#include <optional>

#include "../Models/swing_reference.h"   // GolferAnthro (pinpoint::swingref)
#include "../Core/pp_tuned_constants.h"  // tuned::swingref:: — frozen Phase A defaults

namespace pinpoint::analysis {
struct PoseTrack2D;
struct ShaftTrack2D;
struct BallTrack2D;
struct Segmentation;
}

namespace pinpoint::swingref {

// Tuning inputs (plain fields + defaults), seeded from pp_tuned_constants.h
// tuned::swingref::. `fromOverrides` maps the "swingref.anthro.*" dotted keys
// (analysis_tuning.h); an empty override map is byte-identical to plain
// default-construction. Manual override fields are NaN when unset; a
// fully-specified hub (all three) and/or armLengthM short-circuits the
// corresponding estimate.
struct AnthroConfig {
    int64_t addrWindowUs   = tuned::swingref::kAddrWindowUs;     // swingref.anthro.addrWindowUs — ± half-window centred on the Address event
    double gripOffsetM     = tuned::swingref::kGripOffsetM;      // swingref.anthro.gripOffsetM — butt3D → mid-hands hub, added to arm length
    double hubDepthOffsetM = tuned::swingref::kHubDepthOffsetM;  // swingref.anthro.hubDepthOffsetM — extra hub depth beyond butt depth (A5)
    double kpConfMin       = tuned::swingref::kKpConfMin;        // swingref.anthro.kpConfMin — per-keypoint confidence gate

    // Manual overrides (NaN = unset). swingref.anthro.hubX/hubY/hubZ/armLengthM.
    double hubX = std::numeric_limits<double>::quiet_NaN();
    double hubY = std::numeric_limits<double>::quiet_NaN();
    double hubZ = std::numeric_limits<double>::quiet_NaN();
    double armLengthM = std::numeric_limits<double>::quiet_NaN();

    static AnthroConfig fromOverrides(const QVariantMap &ov);
};

// Bit flags describing what the estimate leaned on / fell back to.
enum AnthroFlags : quint32 {
    AnthroFlagNone         = 0u,
    AnthroFlagManualHub    = 1u << 0,   // hub taken from override
    AnthroFlagManualArm    = 1u << 1,   // arm length taken from override
    AnthroFlagLowShoulder  = 1u << 2,   // shoulder conf below gate (used anyway / manual)
    AnthroFlagNoAnkles     = 1u << 3,   // ankle-mid unavailable → ballOffsetX left 0
    AnthroFlagDepthSeed     = 1u << 4,  // shaft-depth fixed point kept the lie seed
    AnthroFlagOriginFromBall = 1u << 5, // origin/scale fell back to detected ball (no P1 head px)
    AnthroFlagFusedClubLen   = 1u << 6, // club image length taken from the fused club-length estimate
    AnthroFlagBallInconsistent = 1u << 7, // detected ball far from the P1 head (spurious) — ignored for scale
};

struct AnthroEstimate {
    GolferAnthro anthro;            // hub (m), armLength (m), rightHanded
    double  ballOffsetX = 0.0;      // ball X vs stance centre (m); + = toward target
    double  pxPerM      = 0.0;      // isotropic image scale at the address plane
    QPointF originPx;               // image px the ball/clubhead-at-address (world origin) maps to
    float   conf        = 0.f;      // 0..1 aggregate of the evidence used
    quint32 flags       = AnthroFlagNone;
};

// Estimate the golfer anthropometry. `handedness`: +1 right-handed, −1 (or 0)
// left-handed (mirrors the T1 `sgn`). `lieDeg` is the static club lie (from the
// ground). Returns nullopt when address/ball/P1-shaft/shoulders are missing (and
// no manual override covers them).
std::optional<AnthroEstimate>
estimateGolferAnthro(const pinpoint::analysis::PoseTrack2D& pose,
                    const pinpoint::analysis::ShaftTrack2D& shaft,
                    const pinpoint::analysis::BallTrack2D& ball,
                    const pinpoint::analysis::Segmentation& seg,
                    double clubLengthM,
                    double lieDeg,
                    int handedness,
                    const AnthroConfig& cfg = {});

} // namespace pinpoint::swingref
