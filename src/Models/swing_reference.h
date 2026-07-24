// swing_reference.h — Idealised parametric swing-reference model core (Phase A / WP1).
//
// A golfer-scaled, geometric-parametric reference of the club SHAFT from address
// (P1) through top (P4), impact (P7) to the end of the planar region (P8). The
// two-segment linkage (lead arm + club) sweeps a time-varying inclined plane; the
// shaft pose is evaluated per phase s∈[0,1] within each of three segments
// (P1→P4, P4→P7, P7→P8) — never wall-clock time.
//
// Coordinate frame (brief §Coordinate frame): right-handed, origin at the ball,
//   X = target line, Z = up, Y = Z×X (away from the golfer; hub y<0 for a RH golfer).
//
// Conventions are PINNED by the anchor unit tests (src/Models/tests/
// swing_reference_test.cpp), not assumed — see that file for the sign contract.
//
// C++17, Qt value types, quaternions for all rotation composition. In-plane scalar
// angles (α arm, β interior arm–shaft, θ plane inclination) are permitted — they are
// evaluated to quaternions before any rotation is applied, never stored as an
// intermediate rotation representation.

#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector3D>
#include <QQuaternion>
#include <QtMath>

#include <memory>
#include <utility>
#include <vector>

#include "../Core/pp_tuned_constants.h"   // tuned::swingref:: — frozen Phase A defaults

namespace pinpoint::swingref {

// Three phase segments. Backswing P1→P4, Downswing P4→P7, FollowThrough P7→P8.
enum class Segment { Backswing, Downswing, FollowThrough };

// Golfer anthropometry at address, ball frame. hub/armLength are derived from the
// pose pipeline at P1 (manual override in config); here they are plain inputs.
struct GolferAnthro {
    QVector3D hub;              // mid-shoulder at address (m)
    double    armLength = 0.62; // hub → mid-hands (m)
    bool      rightHanded = true;
};

// Club geometry. lieDeg / forwardLeanP7Deg default from lieLeanDefaultsFor(name).
struct ClubSpec {
    double length          = 1.14; // butt → clubhead (m)
    double lieDeg          = 56.0; // static lie
    double ballOffsetX     = 0.0;  // ball position along target line vs stance centre (m)
    double forwardLeanP7Deg = 0.0; // 0 driver … ~8 wedge
};

// Keyframe track: {phase s, value(deg)}. Endpoint values for α/β are overwritten by
// the P1/P7 anchor solve; interior points are tabulated defaults.
using Track = std::vector<std::pair<double, double>>;

// All reference constants in one struct (serialised per golfer/club by the repository
// layer in a later task). `fromOverrides` (below) is the dotted-key "swingref.*"
// tuning seam (analysis_tuning.h); struct defaults ARE the tuned-constant seed, so
// an empty override map is byte-identical to plain default-construction.
struct RefConfig {
    // Master dark flag — the SwingRefStage canRun gate (wired by a later task).
    // false ⇒ the stage never runs, so the whole feature is byte- and
    // code-path-identical to its absence. swingref.enabled.
    bool   enabled                 = tuned::swingref::kEnabled;
    double backswingPlaneOffsetDeg = tuned::swingref::kBackswingPlaneOffsetDeg; // swingref.planeOffsetDeg; Δθ_bs, 0 → one-plane reference
    int    samplesPerSegment       = tuned::swingref::kSamplesPerSegment;       // swingref.samplesPerSegment (ALSO reused by ComparatorConfig::sGridPerSegment)
    double referenceTempoRatio     = tuned::swingref::kReferenceTempoRatio;     // swingref.referenceTempoRatio; classic 3:1 backswing:downswing reference

    // Camera-projection carry-through: camera_projection.h has no config struct
    // of its own, so RefConfig is the one settings surface the stage threads into
    // makeCameraProjection()'s nominalFovDeg parameter and the overlay's
    // residual-warning-chip comparison (ComparatorConfig::summary.projectionResidualPx).
    double nominalFovDeg  = tuned::swingref::kNominalFovDeg;   // swingref.proj.nominalFovDeg
    double residualWarnPx = tuned::swingref::kResidualWarnPx;  // swingref.proj.residualWarnPx

    // Interior keyframes are authored so the shaft is horizontal ∥ the target line
    // at P2 (s=0.33, ∥−X), P4 (s=1.0, ∥+X) and P6 (s=0.80, ∥−X): the shaft is ∥±X
    // exactly when β = α∓90 (a plane-inclination-independent identity — see the cpp).
    // The α/β endpoints at P1 (backswing s=0) and P7 (downswing s=1 == followthrough
    // s=0) are placeholders here — the anchor solve overwrites them, and buildTracks()
    // inserts a runtime C¹ join knot into the follow-through so the P7 join is smooth.
    Track alphaBack {{0.00,   0}, {0.33,  40}, {0.66, 110}, {1.00, 180}};
    Track betaBack  {{0.00, 143}, {0.33, 130}, {0.66, 100}, {1.00,  90}};
    Track alphaDown {{0.00, 180}, {0.40, 100}, {0.80,  40}, {1.00,   0}};
    Track betaDown  {{0.00,  90}, {0.40,  90}, {0.80, 130}, {1.00, 168}};
    // Follow-through: P7 endpoints (s=0) overwritten by the shared anchor; P8 (s=1)
    // authored ∥−X (β=α+90, α=65 → β=155).
    Track alphaFollow {{0.00,   0}, {0.55,  35}, {1.00,  65}};
    Track betaFollow  {{0.00, 168}, {1.00, 155}};
    // Fraction of Δθ_bs still applied through the downswing: 1 at P4 → 0 from P5.5.
    Track thetaBlendDown {{0.00, 1.0}, {0.50, 0.15}, {0.75, 0.0}, {1.00, 0.0}};

    // Dotted-key overrides ("swingref.*", analysis_tuning.h). Keyframe Track
    // tables (above) are NOT tunable — C++ defaults only, per the Phase A plan.
    // Implemented in swing_reference.cpp (needs pinpoint::analysis::tuning::apply).
    static RefConfig fromOverrides(const QVariantMap &ov);
};

// One evaluated shaft pose in the ball frame.
struct ShaftPose {
    QVector3D butt;   // hands / grip end (m)
    QVector3D dir;    // unit vector, butt → clubhead
    double    s = 0;  // phase within segment
    Segment   segment = Segment::Backswing;
    QVector3D clubhead(double clubLen) const { return butt + float(clubLen) * dir; }
};

// Fritsch–Carlson monotone cubic (C¹), clamped evaluation outside [first,last].
// TODO: upgrade to quintic C² if curvature continuity proves visible in the sweep.
class MonotoneCubic {
public:
    MonotoneCubic() = default;
    explicit MonotoneCubic(const Track& keys);
    double eval(double s) const;   // clamped to [first, last]

private:
    std::vector<double> m_s, m_v, m_m;  // knots, values, endpoint-limited tangents
};

// Abstract base — the factory returns a concrete model behind this interface.
class SwingReferenceModel {
public:
    virtual ~SwingReferenceModel() = default;
    virtual ShaftPose evaluate(Segment seg, double s) const = 0;
    virtual std::vector<ShaftPose> sample() const = 0;   // all three segments, ordered
    virtual double fspInclinationDeg() const = 0;
};

// Static default lie / P7 forward-lean per club name (driver ≈56°/0° … wedge ≈64°/8°;
// irons interpolated). Athlete club records lack these; overridable via ClubSpec.
struct LieLean { double lieDeg; double forwardLeanP7Deg; };
LieLean lieLeanDefaultsFor(const QString& clubName);

// Factory.
std::unique_ptr<SwingReferenceModel>
makeSwingReferenceModel(GolferAnthro anthro, ClubSpec club, RefConfig cfg = {});

} // namespace pinpoint::swingref
