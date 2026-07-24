// camera_projection.h — Camera projection layer (WP4a) for the swing-reference model.
//
// One pure, per-camera abstraction that turns a 3D point in the BALL FRAME
// (origin = ball, X = target line, Z = up, Y = Z×X away from a RH golfer — the
// frame pinned by src/Models/swing_reference.h and its anchor tests) into an
// image pixel, and projects a reference ShaftPose to a 2D line whose angle is
// numerically identical to ShaftTracker's per-sample `thetaRad`, so the
// comparator (WP2a) compares by plain subtraction.
//
// Two concrete projections behind one ABC:
//   • CalibratedProjection — rig intrinsics + extrinsics (Rodrigues rvec/tvec).
//   • PoseFitProjection    — extrinsics unknown: solve the camera pose from
//                            address-frame world↔image correspondences with
//                            cv::solvePnP (SOLVEPNP_ITERATIVE, deterministic),
//                            optionally over a golden-section focal-length search
//                            when intrinsics are only nominal.
//
// This header is Qt-only (no OpenCV) so it can be included by the comparator,
// the stage, and serialization without dragging calib3d into every TU. All
// OpenCV usage lives in camera_projection.cpp.
//
// Coordinate / axis convention (must match OpenCV's projectPoints so the T7
// View3D overlay can pin `cameraProjection * worldPoint == imagePoint`):
//   camera-frame  Xc = R·Xworld + t,   camera looks down +Zc,  image y is DOWN.
//   u = fx·xd + cx,  v = fy·yd + cy   with the OpenCV 5-coeff distortion model
//   (k1,k2,p1,p2,k3). A point with Zc ≤ 0 (behind the camera) has NO image point.
//
// C++17, deterministic (no wall-clock, no RNG, fixed iteration counts).

#pragma once

#include <QPointF>
#include <QString>
#include <QVector3D>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "../Models/swing_reference.h"   // ShaftPose (pinpoint::swingref)

namespace pinpoint::swingref {

// Pinhole intrinsics + OpenCV 5-coefficient distortion. `width`/`height` are the
// image dimensions the model is expressed in — required even when fx/fy are
// unknown (a dims-only intrinsics is `valid()==false` and steers the factory to
// the nominal-FOV PoseFit path, which puts the principal point at the centre).
struct CameraIntrinsics {
    double fx = 0.0, fy = 0.0;      // focal length (px)
    double cx = 0.0, cy = 0.0;      // principal point (px)
    std::array<double, 5> dist{};   // k1,k2,p1,p2,k3 (OpenCV order); default = no distortion
    int width = 0, height = 0;      // image size (px)

    bool valid() const { return fx > 0.0 && fy > 0.0 && width > 0 && height > 0; }
};

// A projected reference shaft line, image pixels. `angleRad` uses ShaftTracker's
// convention EXACTLY: atan2(head2d.y − butt2d.y, head2d.x − butt2d.x) in image
// space (y DOWN), grip→head direction — so `measured.thetaRad − angleRad` is the
// signed shaft-angle delta with no re-parameterisation. Pinned by a unit test.
struct ProjectedShaftLine {
    QPointF butt2d, head2d;
    double  angleRad = 0.0;
    bool    valid    = false;
};

// One world↔image correspondence the caller (the STAGE, later) assembles from
// the P1 address frame: ball centre→(0,0,0), model-P1 butt/head→shaft grip/head
// px, ankle-mid→stance point, hub→mid-shoulder px. This layer stays pure — it
// never reads the pose/shaft/ball tracks itself.
struct PointCorrespondence {
    QVector3D world;   // ball frame (m)
    QPointF   image;   // image px
};

// Abstract per-camera projection.
class CameraProjection {
public:
    virtual ~CameraProjection() = default;

    // World (ball frame) → image px, or nullopt when the point is behind the
    // camera / the model is unsolved.
    virtual std::optional<QPointF> imagePoint(const QVector3D& world) const = 0;

    // Project a reference shaft pose (butt + clubhead at `clubLenM`) to an image
    // line. Non-virtual — built purely on imagePoint(), so every subclass shares
    // the exact ShaftTracker angle convention. valid=false when either endpoint
    // fails to project.
    ProjectedShaftLine projectShaftLine(const ShaftPose& pose, double clubLenM) const;

    virtual QString kind() const = 0;      // "Calibrated" | "PoseFit"

    // RMS reprojection residual over the fit correspondences (px); −1 when N/A
    // (e.g. CalibratedProjection carries no residual).
    virtual double residualPx() const { return -1.0; }
};

// Rig intrinsics + known extrinsics (Rodrigues rvec / tvec, world→camera).
class CalibratedProjection : public CameraProjection {
public:
    CalibratedProjection(const CameraIntrinsics& intr,
                         const QVector3D& rvec, const QVector3D& tvec);

    std::optional<QPointF> imagePoint(const QVector3D& world) const override;
    QString kind() const override { return QStringLiteral("Calibrated"); }

    const CameraIntrinsics& intrinsics() const { return m_intr; }
    QVector3D rvec() const { return m_rvec; }
    QVector3D tvec() const { return m_tvec; }

private:
    CameraIntrinsics    m_intr;
    QVector3D           m_rvec, m_tvec;
    std::array<double, 9> m_R{};   // precomputed rotation matrix (row-major)
};

// Extrinsics solved from correspondences via PnP. When `intrinsics` is valid it
// is used as-is; otherwise a nominal pinhole is synthesised from `nominalFovDeg`
// (principal point = image centre) and a deterministic 1-D golden-section search
// over focal length wraps the PnP inner loop. Stores the resolved intrinsics /
// extrinsics and the RMS reprojection residual.
class PoseFitProjection : public CameraProjection {
public:
    PoseFitProjection(const std::vector<PointCorrespondence>& corr,
                     const CameraIntrinsics& intrinsics,   // valid()==false ⇒ nominal
                     double nominalFovDeg,
                     int imageWidth, int imageHeight);

    bool solved() const { return m_solved; }
    std::optional<QPointF> imagePoint(const QVector3D& world) const override;
    QString kind() const override { return QStringLiteral("PoseFit"); }
    double residualPx() const override { return m_residualPx; }

    const CameraIntrinsics& intrinsics() const { return m_intr; }
    QVector3D rvec() const { return m_rvec; }
    QVector3D tvec() const { return m_tvec; }

private:
    CameraIntrinsics    m_intr;
    QVector3D           m_rvec, m_tvec;
    std::array<double, 9> m_R{};
    double              m_residualPx = -1.0;
    bool                m_solved     = false;
};

// Anthro-anchored orthographic face-on projection (Phase A "2D-first" path).
//
//   u = cx + s·X,   v = cy − s·Z      (world depth Y dropped)
//
// The face-on camera is treated as orthographic (assumption A1 in
// swing_ref_anthro.h): a single isotropic image scale `s` (= the anthro
// `pxPerM`) maps world (X target-line, Z up) to image px, with the principal
// point `(cx,cy)` pinned to the ball/clubhead-at-address image position (the
// anthro `originPx`). The model's P1 butt / clubhead / hub therefore land on
// the measured grip / head / mid-shoulder px BY CONSTRUCTION — the reference
// touches measured reality at P1 with no scale to trade.
//
// This exists because full 6-DoF PnP over the 3–4 near-coplanar address
// correspondences the PoseFit path actually gets on real face-on data (ball at
// the clubhead ⇒ redundant, ankles often below the confidence gate) cannot pin
// camera distance / focal: the solve trades scale for reprojection error and
// inflates the projected arc. When the intrinsics are genuinely known, the
// PoseFit PnP path is still used; this orthographic anchor is the honest choice
// only when they are not. Left-handed golfers need no special case — the map
// uses X and Z, which the T1 LH mirror leaves unchanged (only world Y flips).
class OrthographicProjection : public CameraProjection {
public:
    // `xSign` (+1 / −1) mirrors the world target-line X onto image X. The model
    // builds a CANONICAL swing (target = world +X); a face-on capture whose
    // target line runs image-LEFT — e.g. a selfie-mirrored webcam, or the camera
    // simply set up on the other side — needs xSign = −1 so the reference P1
    // shaft points the SAME way as the measured P1 shaft (grip→head). The stage
    // derives it from the measured address shaft direction, so no camera-mirror
    // flag is required offline. The anchor (origin, hub ≈ X-0) is unaffected.
    OrthographicProjection(double cx, double cy, double scalePxPerM, double xSign = 1.0)
        : m_cx(cx), m_cy(cy), m_s(scalePxPerM), m_xSign(xSign >= 0.0 ? 1.0 : -1.0) {}

    std::optional<QPointF> imagePoint(const QVector3D& world) const override
    {
        if (!(m_s > 0.0))
            return std::nullopt;
        return QPointF(m_cx + m_xSign * m_s * double(world.x()),
                       m_cy - m_s * double(world.z()));
    }
    QString kind() const override { return QStringLiteral("Ortho"); }
    double residualPx() const override { return m_residualPx; }

    // RMS reprojection residual over the caller's correspondences (px). Not part
    // of the fit (nothing is fitted — the anchor is exact) but a genuine measure
    // of how well the MODEL's solved P1 pose reproduces the measured address px.
    void setResidualPx(double r) { m_residualPx = r; }

    double scale() const { return m_s; }
    double xSign() const { return m_xSign; }
    QPointF principalPoint() const { return QPointF(m_cx, m_cy); }

private:
    double m_cx, m_cy, m_s, m_xSign = 1.0;
    double m_residualPx = -1.0;
};

// Factory: CalibratedProjection when BOTH extrinsics are supplied, else a
// PoseFitProjection solved from `corr`. `intrinsics` may be dims-only (invalid)
// to request the nominal-FOV focal search on the PoseFit path.
std::unique_ptr<CameraProjection>
makeCameraProjection(const std::vector<PointCorrespondence>& corr,
                    const CameraIntrinsics& intrinsics,
                    std::optional<QVector3D> rvec = std::nullopt,
                    std::optional<QVector3D> tvec = std::nullopt,
                    double nominalFovDeg = 50.0);

} // namespace pinpoint::swingref
