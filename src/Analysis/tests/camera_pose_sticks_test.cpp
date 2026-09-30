// Standalone test for the stick-based camera pose solve (src/Analysis/camera_pose_sticks.h).
// ONE synthetic bay: the ball at the origin, the target-line and hands-line sticks along X,
// the cross stick along Y, a plumbed stick along Z; a face-on camera on −Y and a DTL camera
// on −X, each with a deliberate yaw, pitch and roll and a pinhole with an off-centre
// principal point (a recorded ROI). Every image line is fitted through the PROJECTIONS of
// points along the stick, never drawn from the truth; the solve must give back the pose,
// and the DTL-relative-to-face-on numbers must be what fusion::dtlCamera reproduces.
// The same scene is tools/shaftlab/dtl_calib_solve.py --selftest; the two print the same
// expected numbers so a divergence is visible.
//
//   cmake --build build/tests --target camera_pose_sticks_test
//   ctest --test-dir build/tests -R camera_pose_sticks_test --output-on-failure
#include "../camera_pose_sticks.h"
#include <cmath>
#include <cstdio>
using namespace pinpoint::analysis::calib;
using pinpoint::analysis::fusion::Vec3;
using pinpoint::analysis::fusion::kPi;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A truth camera: centre C, optical axis toward `look`, then rolled. R = world→camera.
struct TruthCam {
    Mat3 R; Vec3 C; Intrinsics K;
    static TruthCam make(const Vec3 &C, const Vec3 &look, double rollDeg, const Intrinsics &K)
    {
        TruthCam c; c.C = C; c.K = K;
        const Vec3 z = (look - C).unit();
        const Vec3 up { 0, 0, 1 };
        Vec3 x = z.cross(up).unit();          // level image-right
        Vec3 y = z.cross(x).unit();           // image-down
        const double r = rollDeg * kPi / 180.0;
        const Vec3 xr = x * std::cos(r) + y * std::sin(r), yr = y * std::cos(r) - x * std::sin(r);
        // rows of R are the camera axes in world coordinates
        c.R.m[0][0] = xr.x; c.R.m[0][1] = xr.y; c.R.m[0][2] = xr.z;
        c.R.m[1][0] = yr.x; c.R.m[1][1] = yr.y; c.R.m[1][2] = yr.z;
        c.R.m[2][0] = z.x;  c.R.m[2][1] = z.y;  c.R.m[2][2] = z.z;
        return c;
    }
    bool project(const Vec3 &P, double &u, double &v) const
    {
        const Vec3 p = R.apply(P - C);
        if (p.z <= 1e-9) return false;
        u = K.cx + K.f * p.x / p.z; v = K.cy + K.f * p.y / p.z;
        return true;
    }
    // Least-squares line through the projections of points along a world segment.
    ImageLine lineOf(const Vec3 &a, const Vec3 &b) const
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0; int n = 0;
        for (int i = 0; i <= 40; ++i) {
            const double f = double(i) / 40.0;
            double u, v;
            if (!project(a + (b - a) * f, u, v)) continue;
            sx += u; sy += v; sxx += u * u; sxy += u * v; syy += v * v; ++n;
        }
        const double mx = sx / n, my = sy / n;
        const double cxx = sxx / n - mx * mx, cxy = sxy / n - mx * my, cyy = syy / n - my * my;
        // principal direction of the scatter
        const double th = 0.5 * std::atan2(2 * cxy, cxx - cyy);
        const double dx = std::cos(th), dy = std::sin(th);
        return ImageLine::through(mx, my, mx + 100 * dx, my + 100 * dy);
    }
};

static double angleBetween(const Mat3 &A, const Mat3 &B)
{
    // rotation angle of Aᵀ·B, from ‖A − B‖_F = 2√2·sin(θ/2): well conditioned near 0,
    // where acos(½(tr − 1)) loses half its digits.
    double ss = 0;
    for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) ss += (A.m[i][k] - B.m[i][k]) * (A.m[i][k] - B.m[i][k]);
    return 2.0 * std::asin(std::clamp(std::sqrt(ss) / (2.0 * std::sqrt(2.0)), 0.0, 1.0)) * 180.0 / kPi;
}

int main()
{
    std::printf("camera_pose_sticks_test\n");
    const double L = 1.219;
    const Vec3 ball { 0, 0, 0 };
    // The bay: target stick from −0.3 to +L−0.3 along X? No — the protocol tapes its ENDS and the
    // far end is the target end; put the stick from the ball to (L, 0, 0) so the target end is
    // a known point. The hands line is parallel at +Y (the golfer's side), the cross stick
    // straddles the ball, the vertical stands on the spot.
    const Vec3 tA = ball, tB { L, 0, 0 };
    const Vec3 hA { -0.2, 0.55, 0 }, hB { L - 0.2, 0.55, 0 };
    const Vec3 cA { 0, -0.6, 0 }, cB { 0, 0.6, 0 };
    const Vec3 vA = ball, vB { 0, 0, L };

    // Cameras. Face-on on −Y at 2 m, 1.05 m up, aimed a little right of the ball and a touch
    // down, rolled 1.5°. DTL on −X at 2.2 m, 0.95 m up, displaced 0.7 m toward the golfer and
    // aimed 6° toward them, 3° down, rolled −2°. ROI'd principal points.
    const Intrinsics KF { 1500.0, 700.0, 480.0 }, KD { 1400.0, 300.0, 540.0 };
    const TruthCam fo  = TruthCam::make({ 0.15, -2.0, 1.05 }, { 0.25, 0.4, 0.75 }, 1.5, KF);
    const TruthCam dtl = TruthCam::make({ -2.2, 0.7, 0.95 }, { 0.2, 0.7 + 2.4 * std::tan(6.0 * kPi / 180.0) - 0.0, 0.95 - 2.4 * std::tan(3.0 * kPi / 180.0) }, -2.0, KD);

    auto observe = [&](const TruthCam &c, bool withVertical, bool withTop) {
        StickObs o;
        o.target = c.lineOf(tA, tB); o.hands = c.lineOf(hA, hB); o.cross = c.lineOf(cA, cB);
        if (withVertical) o.vertical = c.lineOf(vA, vB);
        c.project(ball, o.ballU, o.ballV);
        c.project(tB, o.targetEndU, o.targetEndV);
        o.stickLenM = L;
        if (withTop) { o.haveVerticalTop = true; c.project(vB, o.verticalTopU, o.verticalTopV); o.verticalLenM = L; }
        return o;
    };

    std::printf("=== §1 each camera's pose from its own stick lines ===\n");
    CameraPose pf, pd;
    {
        pf = solveCameraPose(KF, observe(fo, true, true));
        pd = solveCameraPose(KD, observe(dtl, true, true));
        check(pf.ok && pd.ok, "§1 both solves succeed");
        std::printf("       face-on: yaw %.4f pitch %.4f roll %.4f C (%.4f, %.4f, %.4f) rollResid %.2e scaleResid %.2e\n",
                    pf.yawDeg, pf.pitchDeg, pf.rollDeg, pf.C.x, pf.C.y, pf.C.z, pf.rollResidualDeg, pf.scaleResidual);
        std::printf("       dtl:     yaw %.4f pitch %.4f roll %.4f C (%.4f, %.4f, %.4f) rollResid %.2e scaleResid %.2e\n",
                    pd.yawDeg, pd.pitchDeg, pd.rollDeg, pd.C.x, pd.C.y, pd.C.z, pd.rollResidualDeg, pd.scaleResidual);
        check(angleBetween(pf.R, fo.R) < 1e-6, "§1 face-on rotation recovered to < 1e-6°");
        check(angleBetween(pd.R, dtl.R) < 1e-6, "§1 DTL rotation recovered to < 1e-6°");
        check((pf.C - fo.C).norm() < 1e-6, "§1 face-on centre recovered to < 1 µm");
        check((pd.C - dtl.C).norm() < 1e-6, "§1 DTL centre recovered to < 1 µm");
        check(near(pf.rollDeg, 1.5, 1e-6) && near(pd.rollDeg, -2.0, 1e-6), "§1 the rolls read 1.5° and −2.0°");
        check(pf.rollResidualDeg < 1e-6 && pd.rollResidualDeg < 1e-6, "§1 cross and vertical agree (residual < 1e-6°)");
        check(std::fabs(pf.scaleResidual) < 1e-6 && std::fabs(pd.scaleResidual) < 1e-6, "§1 the two scale witnesses agree");
    }
    std::printf("=== §2 without the vertical stick, and without its top ===\n");
    {
        const CameraPose a = solveCameraPose(KD, observe(dtl, false, false));
        check(a.ok && angleBetween(a.R, dtl.R) < 1e-6 && (a.C - dtl.C).norm() < 1e-6,
              "§2 three floor sticks + the target end alone recover the DTL pose");
        check(a.rollResidualDeg == 0.0 && a.scaleResidual == 0.0, "§2 no vertical ⇒ residuals reported as 0");
        const CameraPose b = solveCameraPose(KF, observe(fo, false, false));
        check(b.ok && angleBetween(b.R, fo.R) < 1e-6, "§2 face-on too, with its X vanishing point at infinity");
    }
    std::printf("=== §3 refusals ===\n");
    {
        StickObs o = observe(dtl, true, true);
        o.hands = o.target;
        check(!solveCameraPose(KD, o).ok, "§3 coincident target and hands lines are refused");
        check(!solveCameraPose(Intrinsics { 0, 0, 0 }, observe(dtl, true, true)).ok, "§3 f = 0 is refused");
        StickObs p = observe(dtl, true, false);
        p.targetEndU = p.ballU; p.targetEndV = p.ballV;
        check(!solveCameraPose(KD, p).ok, "§3 no scale witness is refused");
    }
    std::printf("=== §4 the DTL camera relative to the face-on one, as fusion::dtlCamera spells it ===\n");
    {
        const RelativeDtl r = dtlRelativeToFaceOn(pf, pd);
        check(r.ok, "§4 relative pose solves");
        std::printf("       yaw %.4f pitch %.4f roll %.4f offset (%.4f, %.4f, %.4f) inter-camera %.3f face-on pitch %.3f roll %.3f\n",
                    r.yawDeg, r.pitchDeg, r.rollDeg, r.offsetM.x, r.offsetM.y, r.offsetM.z, r.interCameraDeg,
                    r.faceOnPitchDeg, r.faceOnRollDeg);
        // Rebuild the DTL axes from the fusion model with those numbers and compare with the
        // truth camera's axes expressed in the fusion frame.
        const pinpoint::analysis::fusion::Camera m = pinpoint::analysis::fusion::dtlCamera(r.yawDeg, r.pitchDeg, r.rollDeg);
        const Vec3 Z { 0, 0, 1 };
        const Vec3 axF = fo.R.applyT({ 0, 0, 1 });
        const Vec3 yF = Vec3 { axF.x, axF.y, 0 }.unit(), xF = yF.cross(Z).unit();
        auto toFus = [&](const Vec3 &v) { return Vec3 { v.dot(xF), v.dot(yF), v.dot(Z) }; };
        const Vec3 d = toFus(dtl.R.applyT({ 0, 0, 1 })), rt = toFus(dtl.R.applyT({ 1, 0, 0 })), dn = toFus(dtl.R.applyT({ 0, 1, 0 }));
        check((m.d - d).norm() < 1e-9 && (m.right - rt).norm() < 1e-9 && (m.down - dn).norm() < 1e-9,
              "§4 fusion::dtlCamera(yaw, pitch, roll) reproduces the measured DTL axes");
        check((r.offsetM - toFus(dtl.C - fo.C)).norm() < 1e-9, "§4 the offset is the centre difference in the fusion frame");
        check(near(r.faceOnRollDeg, 1.5, 1e-6), "§4 the face-on roll the fusion frame ignores is reported");
        // The level-camera assumption every earlier run made, quantified on this bay.
        std::printf("       the assumed-zero placement was off by yaw %.2f°, pitch %.2f°, roll %.2f° on this bay\n",
                    r.yawDeg, r.pitchDeg, r.rollDeg);
    }
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
