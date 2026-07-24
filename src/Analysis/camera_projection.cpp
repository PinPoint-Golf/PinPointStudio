// camera_projection.cpp — implementation of the WP4a projection layer.
//
// All OpenCV (calib3d) usage is confined here. The projection math mirrors
// cv::projectPoints exactly (world→camera via R,t; pinhole + 5-coeff
// distortion) so imagePoint() and any cv::projectPoints round-trip agree.

#include "camera_projection.h"

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include <cmath>
#include <limits>

namespace pinpoint::swingref {

namespace {

// Project one ball-frame point with a row-major rotation R, translation t, and
// pinhole+distortion intrinsics. Returns nullopt when the point is behind the
// camera (Zc ≤ 0). Identical model to cv::projectPoints.
std::optional<QPointF> projectOne(const std::array<double, 9>& R,
                                  const QVector3D& t,
                                  const CameraIntrinsics& K,
                                  const QVector3D& X)
{
    const double xw = X.x(), yw = X.y(), zw = X.z();
    const double Xc = R[0] * xw + R[1] * yw + R[2] * zw + t.x();
    const double Yc = R[3] * xw + R[4] * yw + R[5] * zw + t.y();
    const double Zc = R[6] * xw + R[7] * yw + R[8] * zw + t.z();
    if (Zc <= 1e-9)
        return std::nullopt;

    const double x = Xc / Zc, y = Yc / Zc;
    const double k1 = K.dist[0], k2 = K.dist[1], p1 = K.dist[2], p2 = K.dist[3], k3 = K.dist[4];
    const double r2 = x * x + y * y;
    const double radial = 1.0 + r2 * (k1 + r2 * (k2 + r2 * k3));
    const double xd = x * radial + 2.0 * p1 * x * y + p2 * (r2 + 2.0 * x * x);
    const double yd = y * radial + p1 * (r2 + 2.0 * y * y) + 2.0 * p2 * x * y;
    return QPointF(K.fx * xd + K.cx, K.fy * yd + K.cy);
}

// Rodrigues rvec → row-major 3×3 rotation.
std::array<double, 9> rodrigues(const QVector3D& rvec)
{
    cv::Vec3d r(rvec.x(), rvec.y(), rvec.z());
    cv::Matx33d Rm;
    cv::Rodrigues(r, Rm);
    return {Rm(0, 0), Rm(0, 1), Rm(0, 2),
            Rm(1, 0), Rm(1, 1), Rm(1, 2),
            Rm(2, 0), Rm(2, 1), Rm(2, 2)};
}

cv::Matx33d intrinsicsMatrix(const CameraIntrinsics& K)
{
    return cv::Matx33d(K.fx, 0.0, K.cx,
                       0.0, K.fy, K.cy,
                       0.0, 0.0, 1.0);
}

// Solve PnP for a given intrinsics and report the RMS reprojection residual.
// Deterministic (no RANSAC either way). SOLVEPNP_ITERATIVE's default DLT
// initialisation (useExtrinsicGuess=false) requires >= 6 NON-coplanar points —
// it throws a cv::Exception ("DLT algorithm needs at least 6 points") for the
// 4-5 point case, which is the only regime Phase A's correspondence set
// (buildSwingRefCorrespondences: ball + P1 grip/head + shoulder-mid +
// ankle-mid, max 5) ever produces on real data. SOLVEPNP_EPNP has no such
// floor (min 4, coplanar or not) and is itself deterministic, so it is used
// whenever ITERATIVE's DLT init would be underdetermined; ITERATIVE is kept
// for >= 6 points (a future richer correspondence set / calibrated path),
// where its LM refinement is more accurate.
bool solvePnpRms(const std::vector<cv::Point3f>& obj,
                 const std::vector<cv::Point2f>& img,
                 const CameraIntrinsics& K,
                 QVector3D& rvecOut, QVector3D& tvecOut, double& rmsOut)
{
    if (obj.size() < 4 || obj.size() != img.size())
        return false;

    const cv::Matx33d Kmat = intrinsicsMatrix(K);
    cv::Mat dist(1, 5, CV_64F);
    for (int i = 0; i < 5; ++i) dist.at<double>(0, i) = K.dist[std::size_t(i)];

    const int method = (obj.size() >= 6) ? cv::SOLVEPNP_ITERATIVE : cv::SOLVEPNP_EPNP;
    cv::Vec3d rvec, tvec;
    bool ok = false;
    try {
        ok = cv::solvePnP(obj, img, cv::Mat(Kmat), dist, rvec, tvec,
                          /*useExtrinsicGuess=*/false, method);
    } catch (const cv::Exception&) {
        return false;   // degrade: an unsolvable configuration is a valid "no fit" outcome
    }
    if (!ok)
        return false;

    // RMS reprojection error (our own projectOne, consistent with imagePoint()).
    const std::array<double, 9> R = rodrigues(QVector3D(float(rvec[0]), float(rvec[1]), float(rvec[2])));
    const QVector3D t{float(tvec[0]), float(tvec[1]), float(tvec[2])};
    double sse = 0.0;
    int n = 0;
    for (std::size_t i = 0; i < obj.size(); ++i) {
        const auto p = projectOne(R, t, K, QVector3D(obj[i].x, obj[i].y, obj[i].z));
        if (!p)
            return false;   // a fit that puts a correspondence behind the camera is unusable
        const double dx = p->x() - double(img[i].x);
        const double dy = p->y() - double(img[i].y);
        sse += dx * dx + dy * dy;
        ++n;
    }
    rvecOut = QVector3D(float(rvec[0]), float(rvec[1]), float(rvec[2]));
    tvecOut = QVector3D(float(tvec[0]), float(tvec[1]), float(tvec[2]));
    rmsOut  = std::sqrt(sse / double(n));
    return true;
}

} // namespace

// ── CameraProjection ─────────────────────────────────────────────────────────

ProjectedShaftLine CameraProjection::projectShaftLine(const ShaftPose& pose, double clubLenM) const
{
    ProjectedShaftLine out;
    const auto b = imagePoint(pose.butt);
    const auto h = imagePoint(pose.clubhead(clubLenM));
    if (!b || !h)
        return out;   // valid stays false
    out.butt2d = *b;
    out.head2d = *h;
    // ShaftTracker convention: atan2(head.y − grip.y, head.x − grip.x), image px.
    out.angleRad = std::atan2(out.head2d.y() - out.butt2d.y(),
                              out.head2d.x() - out.butt2d.x());
    out.valid = true;
    return out;
}

// ── CalibratedProjection ─────────────────────────────────────────────────────

CalibratedProjection::CalibratedProjection(const CameraIntrinsics& intr,
                                           const QVector3D& rvec, const QVector3D& tvec)
    : m_intr(intr), m_rvec(rvec), m_tvec(tvec), m_R(rodrigues(rvec))
{
}

std::optional<QPointF> CalibratedProjection::imagePoint(const QVector3D& world) const
{
    if (!m_intr.valid())
        return std::nullopt;
    return projectOne(m_R, m_tvec, m_intr, world);
}

// ── PoseFitProjection ────────────────────────────────────────────────────────

PoseFitProjection::PoseFitProjection(const std::vector<PointCorrespondence>& corr,
                                     const CameraIntrinsics& intrinsics,
                                     double nominalFovDeg,
                                     int imageWidth, int imageHeight)
{
    std::vector<cv::Point3f> obj;
    std::vector<cv::Point2f> img;
    obj.reserve(corr.size());
    img.reserve(corr.size());
    for (const auto& c : corr) {
        obj.emplace_back(float(c.world.x()), float(c.world.y()), float(c.world.z()));
        img.emplace_back(float(c.image.x()), float(c.image.y()));
    }
    if (obj.size() < 4)
        return;

    if (intrinsics.valid()) {
        // Known intrinsics — a single deterministic PnP solve.
        m_intr = intrinsics;
        if (solvePnpRms(obj, img, m_intr, m_rvec, m_tvec, m_residualPx)) {
            m_R = rodrigues(m_rvec);
            m_solved = true;
        }
        return;
    }

    // Nominal pinhole: principal point at the image centre, focal from the FOV,
    // then a fixed-iteration golden-section search over focal length minimising
    // the PnP RMS residual. Deterministic.
    const int W = imageWidth  > 0 ? imageWidth  : intrinsics.width;
    const int H = imageHeight > 0 ? imageHeight : intrinsics.height;
    if (W <= 0 || H <= 0)
        return;

    const double fov = (nominalFovDeg > 1.0 && nominalFovDeg < 179.0) ? nominalFovDeg : 50.0;
    const double f0  = (0.5 * double(W)) / std::tan(0.5 * fov * M_PI / 180.0);

    auto makeK = [&](double f) {
        CameraIntrinsics K;
        K.fx = f; K.fy = f;
        K.cx = 0.5 * double(W);
        K.cy = 0.5 * double(H);
        K.width = W; K.height = H;
        return K;
    };
    auto evalF = [&](double f, QVector3D& rv, QVector3D& tv, double& rms) -> bool {
        return solvePnpRms(obj, img, makeK(f), rv, tv, rms);
    };

    // Golden-section minimisation of residual(f) over [0.35·f0, 3·f0].
    const double gr = 0.6180339887498949;
    double a = 0.35 * f0, b = 3.0 * f0;
    double c = b - gr * (b - a);
    double d = a + gr * (b - a);
    QVector3D rvc, tvc, rvd, tvd;
    double rc = 0.0, rd = 0.0;
    bool okc = evalF(c, rvc, tvc, rc);
    bool okd = evalF(d, rvd, tvd, rd);
    if (!okc) rc = std::numeric_limits<double>::infinity();
    if (!okd) rd = std::numeric_limits<double>::infinity();
    for (int it = 0; it < 48; ++it) {
        if (rc < rd) {
            b = d; d = c; rd = rc; rvd = rvc; tvd = tvc;
            c = b - gr * (b - a);
            okc = evalF(c, rvc, tvc, rc);
            if (!okc) rc = std::numeric_limits<double>::infinity();
        } else {
            a = c; c = d; rc = rd; rvc = rvd; tvc = tvd;
            d = a + gr * (b - a);
            okd = evalF(d, rvd, tvd, rd);
            if (!okd) rd = std::numeric_limits<double>::infinity();
        }
    }
    const double fBest = 0.5 * (a + b);
    QVector3D rvBest, tvBest;
    double rmsBest = 0.0;
    if (!evalF(fBest, rvBest, tvBest, rmsBest))
        return;
    m_intr = makeK(fBest);
    m_rvec = rvBest;
    m_tvec = tvBest;
    m_R = rodrigues(m_rvec);
    m_residualPx = rmsBest;
    m_solved = true;
}

std::optional<QPointF> PoseFitProjection::imagePoint(const QVector3D& world) const
{
    if (!m_solved)
        return std::nullopt;
    return projectOne(m_R, m_tvec, m_intr, world);
}

// ── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<CameraProjection>
makeCameraProjection(const std::vector<PointCorrespondence>& corr,
                    const CameraIntrinsics& intrinsics,
                    std::optional<QVector3D> rvec,
                    std::optional<QVector3D> tvec,
                    double nominalFovDeg)
{
    if (rvec && tvec)
        return std::make_unique<CalibratedProjection>(intrinsics, *rvec, *tvec);
    return std::make_unique<PoseFitProjection>(corr, intrinsics, nominalFovDeg,
                                               intrinsics.width, intrinsics.height);
}

} // namespace pinpoint::swingref
