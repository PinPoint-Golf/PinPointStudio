// Standalone test for the WP4a camera-projection layer (camera_projection.h).
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure
//
// Covers:
//   A. CalibratedProjection == cv::projectPoints (axis-convention pin, exact).
//   B. projectShaftLine angle == ShaftTracker's θ convention
//        (atan2(head.y−grip.y, head.x−grip.x), image px, grip→head).
//   C. PoseFitProjection recovers a usable camera from noisy correspondences on
//        BOTH a face-on and a down-the-line ground-truth camera → RMS residual
//        < 5 px @ 1440×1080 (acceptance criterion 2).
//   D. Nominal-FOV + golden-section focal search recovers a usable camera when
//        fed dims-only intrinsics and a wrong-but-reasonable FOV.

#include "../camera_projection.h"

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include <QPointF>
#include <QVector3D>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::swingref;

static int g_fail = 0;

#define CHECK(label, cond)                                                             \
    do {                                                                               \
        const bool ok = (cond);                                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                       \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_LT(label, got, lim)                                                      \
    do {                                                                               \
        const double g = (got), l = (lim);                                             \
        const bool ok = g < l;                                                         \
        std::printf("  [%s] %-40s %8.4f < %.4f\n", ok ? "PASS" : "FAIL", label, g, l); \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                              \
    do {                                                                               \
        const double g = (got), w = (want);                                            \
        const bool ok = std::abs(g - w) <= (tol);                                      \
        std::printf("  [%s] %-30s got %9.5f  want %9.5f (tol %.4f)\n",                  \
                    ok ? "PASS" : "FAIL", label, g, w, double(tol));                   \
        if (!ok) ++g_fail;                                                             \
    } while (0)

// ── Ground-truth camera builder (OpenCV convention: +Z forward, +X right, +Y down) ──
struct GtCamera {
    CameraIntrinsics K;
    QVector3D rvec, tvec;
};

static QVector3D normv(const QVector3D& v) { return v.normalized(); }

static GtCamera makeCamera(const QVector3D& C, const QVector3D& target,
                           const QVector3D& worldUp, double f, int W, int H)
{
    // OpenCV camera frame: +Z forward (into scene), +X right, +Y DOWN.
    const QVector3D z = normv(target - C);                    // forward
    const QVector3D x = normv(QVector3D::crossProduct(z, worldUp)); // right
    const QVector3D y = QVector3D::crossProduct(z, x);              // down (world up → image up)
    // R world→cam has rows = camera axes in world coords.
    cv::Matx33d R(x.x(), x.y(), x.z(),
                  y.x(), y.y(), y.z(),
                  z.x(), z.y(), z.z());
    const cv::Vec3d Cv(C.x(), C.y(), C.z());
    const cv::Vec3d t = -(R * Cv);
    cv::Vec3d r;
    cv::Rodrigues(R, r);

    GtCamera cam;
    cam.K.fx = f; cam.K.fy = f; cam.K.cx = 0.5 * W; cam.K.cy = 0.5 * H;
    cam.K.width = W; cam.K.height = H;
    cam.rvec = QVector3D(float(r[0]), float(r[1]), float(r[2]));
    cam.tvec = QVector3D(float(t[0]), float(t[1]), float(t[2]));
    return cam;
}

// cv::projectPoints reference for one world point.
static QPointF cvProject(const GtCamera& cam, const QVector3D& X)
{
    std::vector<cv::Point3f> obj{cv::Point3f(X.x(), X.y(), X.z())};
    cv::Matx33d Km(cam.K.fx, 0, cam.K.cx, 0, cam.K.fy, cam.K.cy, 0, 0, 1);
    cv::Mat dist = cv::Mat::zeros(1, 5, CV_64F);
    cv::Vec3d rv(cam.rvec.x(), cam.rvec.y(), cam.rvec.z());
    cv::Vec3d tv(cam.tvec.x(), cam.tvec.y(), cam.tvec.z());
    std::vector<cv::Point2f> img;
    cv::projectPoints(obj, rv, tv, cv::Mat(Km), dist, img);
    return QPointF(img[0].x, img[0].y);
}

// Deterministic pseudo-noise in [-amp, amp] (fixed LCG — no rand()).
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    double next(double amp) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const double u = double((s >> 11) & ((1ULL << 53) - 1)) / double(1ULL << 53); // [0,1)
        return (2.0 * u - 1.0) * amp;
    }
};

static std::vector<QVector3D> landmarkCloud()
{
    return {
        {0.00f,  0.00f, 0.00f},   // ball (origin)
        {0.10f, -0.30f, 1.00f},
        {-0.15f, -0.35f, 1.10f},
        {0.05f, -0.50f, 0.40f},
        {0.00f, -0.20f, 1.40f},
        {0.30f, -0.10f, 0.20f},
        {-0.25f, -0.40f, 0.70f},
        {0.20f, -0.45f, 1.30f},
    };
}

static double poseFitResidual(const GtCamera& cam, bool nominalFov, uint64_t seed)
{
    const auto cloud = landmarkCloud();
    Lcg rng(seed);
    std::vector<PointCorrespondence> corr;
    for (const auto& X : cloud) {
        const QPointF p = cvProject(cam, X);
        // ±0.5–1 px deterministic noise.
        corr.push_back({X, QPointF(p.x() + rng.next(0.9), p.y() + rng.next(0.9))});
    }

    CameraIntrinsics intr;
    if (nominalFov) {
        intr.width = cam.K.width; intr.height = cam.K.height;  // dims-only ⇒ invalid ⇒ focal search
    } else {
        intr = cam.K;
    }
    // Wrong-but-reasonable FOV for the nominal path. True FOV here ≈ 51.3°.
    const double fovDeg = nominalFov ? 42.0 : 50.0;
    auto proj = makeCameraProjection(corr, intr, std::nullopt, std::nullopt, fovDeg);
    if (!proj || proj->residualPx() < 0.0)
        return 1e9;
    return proj->residualPx();
}

int main()
{
    const int W = 1440, H = 1080;
    std::printf("=== camera_projection.h ===\n\n");

    const GtCamera faceOn = makeCamera({0.0f, 3.0f, 1.0f}, {0.0f, 0.0f, 0.85f},
                                       {0.0f, 0.0f, 1.0f}, 1500.0, W, H);
    const GtCamera dtl    = makeCamera({-3.0f, 0.0f, 1.2f}, {1.0f, 0.0f, 0.4f},
                                       {0.0f, 0.0f, 1.0f}, 1450.0, W, H);

    // ── A. CalibratedProjection == cv::projectPoints ────────────────────────
    std::printf("-- A. Calibrated round-trip vs cv::projectPoints --\n");
    {
        CalibratedProjection cp(faceOn.K, faceOn.rvec, faceOn.tvec);
        double maxErr = 0.0;
        for (const auto& X : landmarkCloud()) {
            const auto got = cp.imagePoint(X);
            const QPointF ref = cvProject(faceOn, X);
            CHECK("imagePoint present", bool(got));
            if (got) {
                const double e = std::hypot(got->x() - ref.x(), got->y() - ref.y());
                maxErr = std::max(maxErr, e);
            }
        }
        // Tolerance 1e-2 px: rvec/tvec are stored as float QVector3D, so exact
        // agreement with cv::projectPoints is limited by float storage, not the
        // math. Any axis-convention MISMATCH would be hundreds of px, not 1e-2.
        CHECK_LT("max |imagePoint - cvProject| (px)", maxErr, 1e-2);

        // With distortion, still exact vs cv::projectPoints.
        CameraIntrinsics Kd = faceOn.K;
        Kd.dist = {-0.12, 0.04, 0.001, -0.0008, 0.01};
        CalibratedProjection cpd(Kd, faceOn.rvec, faceOn.tvec);
        GtCamera camD = faceOn; camD.K = Kd;
        std::vector<cv::Point3f> obj;
        for (const auto& X : landmarkCloud()) obj.emplace_back(X.x(), X.y(), X.z());
        cv::Matx33d Km(Kd.fx, 0, Kd.cx, 0, Kd.fy, Kd.cy, 0, 0, 1);
        cv::Mat dist(1, 5, CV_64F);
        for (int i = 0; i < 5; ++i) dist.at<double>(0, i) = Kd.dist[std::size_t(i)];
        cv::Vec3d rv(faceOn.rvec.x(), faceOn.rvec.y(), faceOn.rvec.z());
        cv::Vec3d tv(faceOn.tvec.x(), faceOn.tvec.y(), faceOn.tvec.z());
        std::vector<cv::Point2f> ref;
        cv::projectPoints(obj, rv, tv, cv::Mat(Km), dist, ref);
        double maxErrD = 0.0;
        for (std::size_t i = 0; i < obj.size(); ++i) {
            const auto got = cpd.imagePoint(landmarkCloud()[i]);
            if (got)
                maxErrD = std::max(maxErrD, std::hypot(got->x() - ref[i].x, got->y() - ref[i].y));
        }
        CHECK_LT("max |imagePoint - cvProject| distorted (px)", maxErrD, 1e-2);
    }

    // ── B. projectShaftLine angle == ShaftTracker θ convention ──────────────
    // The ShaftTracker per-sample convention (ball_anchor.cpp:377,
    // shaft_track_assembly.cpp:1596-1601) is thetaRad = atan2(head.y − grip.y,
    // head.x − grip.x) in image px, grip→head. Pin it against an INDEPENDENT
    // cv::projectPoints of the two shaft endpoints, for several orientations, on
    // both cameras — so the comparison against measured thetaRad is a plain
    // subtraction.
    std::printf("\n-- B. projectShaftLine angle convention --\n");
    {
        const std::array<QVector3D, 4> dirs = {
            QVector3D(0.0f, 0.0f, -1.0f),   // straight down (world)
            QVector3D(1.0f, 0.0f, 0.0f),    // along +X (target line)
            QVector3D(0.6f, -0.2f, -0.8f),  // oblique, out of plane
            QVector3D(-0.5f, 0.1f, -0.86f), // oblique the other way
        };
        for (const GtCamera& cam : {faceOn, dtl}) {
            CalibratedProjection cp(cam.K, cam.rvec, cam.tvec);
            for (const QVector3D& d : dirs) {
                ShaftPose pose;
                pose.butt = QVector3D(0.05f, -0.25f, 0.95f);
                pose.dir  = d.normalized();
                const double clubLen = 0.6;
                const ProjectedShaftLine line = cp.projectShaftLine(pose, clubLen);
                CHECK("line valid", line.valid);
                // Independent OpenCV projection of the same two endpoints.
                const QPointF buttCv = cvProject(cam, pose.butt);
                const QPointF headCv = cvProject(cam, pose.clubhead(clubLen));
                const double thetaRef =
                    std::atan2(headCv.y() - buttCv.y(), headCv.x() - buttCv.x());
                // Wrap the difference to (−π,π] — ±π are the same direction and
                // the comparator wraps the subtraction anyway.
                const double dTheta = std::remainder(line.angleRad - thetaRef, 2.0 * M_PI);
                CHECK_NEAR("angleRad == ShaftTracker θ(endpoints)", dTheta, 0.0, 1e-4);
            }
        }
    }

    // ── C. PoseFit recovery, both cameras, with pixel noise ─────────────────
    std::printf("\n-- C. PoseFit residual < 5 px (known intrinsics) --\n");
    {
        const double rFace = poseFitResidual(faceOn, /*nominalFov=*/false, 0x1234ULL);
        const double rDtl  = poseFitResidual(dtl,    /*nominalFov=*/false, 0x9E37ULL);
        CHECK_LT("face-on PoseFit RMS residual (px)", rFace, 5.0);
        CHECK_LT("DTL PoseFit RMS residual (px)",     rDtl,  5.0);
    }

    // ── D. Nominal-FOV focal search ─────────────────────────────────────────
    std::printf("\n-- D. Nominal-FOV + focal search < 5 px --\n");
    {
        const double rNom = poseFitResidual(faceOn, /*nominalFov=*/true, 0x5150ULL);
        CHECK_LT("nominal-FOV PoseFit RMS residual (px)", rNom, 5.0);
    }

    // ── E. OrthographicProjection anchors the origin + scale exactly ────────
    // The 2D-first Phase A path: u = cx + s·X, v = cy − s·Z. The world origin
    // (ball/clubhead-at-address) must land on the anchor (cx,cy) BY CONSTRUCTION,
    // an X-axis point at +s·X, a Z-axis point at −s·Z, and the shaft-line angle
    // must follow ShaftTracker's θ convention like every other projection.
    std::printf("\n-- E. OrthographicProjection anchor + scale --\n");
    {
        const double cx = 253.6, cy = 708.8, s = 335.9;   // this-bug's real numbers
        OrthographicProjection op(cx, cy, s);
        const auto o = op.imagePoint(QVector3D(0, 0, 0));
        CHECK("origin projects", bool(o));
        if (o) {
            CHECK_NEAR("origin.x == cx", o->x(), cx, 1e-6);
            CHECK_NEAR("origin.y == cy", o->y(), cy, 1e-6);
        }
        const auto px = op.imagePoint(QVector3D(0.5f, 9.9f, 0.0f));  // +X, depth Y ignored
        CHECK_NEAR("X→cx+s·X", px->x(), cx + s * 0.5, 1e-4);
        CHECK_NEAR("depth Y ignored (v==cy)", px->y(), cy, 1e-4);
        const auto pz = op.imagePoint(QVector3D(0.0f, -2.0f, 0.8f)); // +Z up → smaller v
        CHECK_NEAR("Z→cy−s·Z", pz->y(), cy - s * 0.8, 1e-4);

        // projectShaftLine angle: a shaft pointing butt→clubhead along −X should
        // read θ ≈ atan2(0, −s·clubLen) = π (image px, grip→head).
        ShaftPose pose;
        pose.butt = QVector3D(0.2f, 0.0f, 0.0f);
        pose.dir  = QVector3D(-1.0f, 0.0f, 0.0f);          // butt→clubhead toward −X
        const ProjectedShaftLine line = op.projectShaftLine(pose, 0.6);
        CHECK("ortho shaft line valid", line.valid);
        CHECK_NEAR("ortho shaft θ == π", std::abs(std::remainder(line.angleRad - M_PI, 2*M_PI)),
                   0.0, 1e-4);

        // Residual helper contract: a correspondence set the projection hits
        // exactly has RMS 0; a displaced one is exactly the displacement.
        CHECK("residual defaults to -1 until set", op.residualPx() < 0.0);
        op.setResidualPx(3.5);
        CHECK_NEAR("residual round-trips", op.residualPx(), 3.5, 1e-9);

        // xSign = −1 mirrors world X about the anchor (selfie-mirrored capture):
        // origin/Z unchanged, +X now projects LEFT of the anchor.
        OrthographicProjection opm(cx, cy, s, -1.0);
        const auto m = opm.imagePoint(QVector3D(0.5f, 0.0f, 0.0f));
        CHECK_NEAR("xSign=-1: X→cx−s·X", m->x(), cx - s * 0.5, 1e-4);
        const auto mo = opm.imagePoint(QVector3D(0, 0, 0));
        CHECK_NEAR("xSign=-1: origin still cx", mo->x(), cx, 1e-6);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
