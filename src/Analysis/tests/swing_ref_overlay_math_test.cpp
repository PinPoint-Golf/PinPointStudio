// swing_ref_overlay_math_test.cpp — pins src/Gui/viz/swing_ref_overlay_math.h
// (T7, WP5a overlay): the OpenCV-projection <-> Qt-Quick3D-camera conversion
// and the playhead(us) -> reference-model global-s phase mapping.
//
// Precedent: src/Analysis/tests/viz_frame_test.cpp includes a Gui/ math
// header by relative path and is registered as a plain pp_add_test with no
// extra INCLUDE/LINK (Qt6::Core + Qt6::Gui are already linked by every suite
// in this CMakeLists). Same pattern here.
//
// The camera-conversion test hand-rolls the OpenCV pinhole projection (u,v)
// in plain arithmetic — mirroring camera_projection.cpp's projectOne() —
// WITHOUT linking OpenCV, per the T7 brief ("you can hand-roll the OpenCV
// projection math in the test with plain arithmetic; do NOT link OpenCV into
// the Gui code"). It reuses the header's own rodriguesToMatrix() to build R
// (that is Rodrigues' formula, not an OpenCV call), then independently
// re-implements the u,v pinhole formula to compare against the Qt path
// (buildProjectionMatrix * openCvExtrinsicsToQt().viewMatrix -> NDC -> pixel).

#include "../../Gui/viz/swing_ref_overlay_math.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <cmath>
#include <cstdio>

using namespace pinpoint::swingref::overlaymath;

static int g_fail = 0;

static void checkNear(const char *label, double got, double want, double tol)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  [%s] %-40s got %10.4f  want %10.4f  (tol %.4g)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}

static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-40s got %-5s want %-5s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

namespace {

// Independent (no OpenCV) re-implementation of camera_projection.cpp's
// projectOne(), no distortion (matches SwingRefProjection, which carries no
// dist[]). Used ONLY by this test, to keep the pinning independent of
// camera_projection.cpp itself.
struct Pixel { double u, v; bool valid; };

Pixel openCvProject(const std::array<double, 9> &R, const std::array<double, 3> &t,
                    double fx, double fy, double cx, double cy, const QVector3D &world)
{
    const double xw = world.x(), yw = world.y(), zw = world.z();
    const double Xc = R[0] * xw + R[1] * yw + R[2] * zw + t[0];
    const double Yc = R[3] * xw + R[4] * yw + R[5] * zw + t[1];
    const double Zc = R[6] * xw + R[7] * yw + R[8] * zw + t[2];
    if (Zc <= 1e-9)
        return { 0, 0, false };
    return { fx * (Xc / Zc) + cx, fy * (Yc / Zc) + cy, true };
}

// Full Qt-side pipeline: view * projection -> clip -> NDC -> pixel (using the
// SAME imgW/imgH the intrinsics were expressed in, per the header's design
// note — resolution-independent NDC).
Pixel qtProject(const CameraPose &pose, const QMatrix4x4 &proj,
                double imgW, double imgH, const QVector3D &world)
{
    const QVector4D clip = proj * pose.viewMatrix * QVector4D(world, 1.0f);
    if (clip.w() == 0.0f)
        return { 0, 0, false };
    const double xNdc = clip.x() / clip.w();
    const double yNdc = clip.y() / clip.w();
    const double u = (xNdc + 1.0) * 0.5 * imgW;
    const double v = (1.0 - yNdc) * 0.5 * imgH;
    return { u, v, true };
}

} // namespace

int main()
{
    // ── rodriguesToMatrix: known cases ───────────────────────────────────────
    std::printf("=== rodriguesToMatrix ===\n");
    {
        const auto I = rodriguesToMatrix({ 0, 0, 0 });
        checkNear("identity[0]", I[0], 1.0, 1e-9);
        checkNear("identity[4]", I[4], 1.0, 1e-9);
        checkNear("identity[8]", I[8], 1.0, 1e-9);
        checkNear("identity[1]", I[1], 0.0, 1e-9);

        // 90 deg about +Z: X axis -> Y axis, Y axis -> -X axis.
        const double halfPi = M_PI / 2.0;
        const auto Rz = rodriguesToMatrix({ 0, 0, halfPi });
        const QVector3D ex{ float(Rz[0]), float(Rz[3]), float(Rz[6]) };   // R * (1,0,0)
        const QVector3D ey{ float(Rz[1]), float(Rz[4]), float(Rz[7]) };   // R * (0,1,0)
        checkNear("Rz90 * X -> Y.x", ex.x(), 0.0, 1e-5);
        checkNear("Rz90 * X -> Y.y", ex.y(), 1.0, 1e-5);
        checkNear("Rz90 * Y -> -X.x", ey.x(), -1.0, 1e-5);
        checkNear("Rz90 * Y -> -X.y", ey.y(), 0.0, 1e-5);
    }

    // ── OpenCV <-> Qt camera conversion, composed end-to-end ────────────────
    std::printf("\n=== openCvExtrinsicsToQt + buildProjectionMatrix (composed pixel match) ===\n");
    {
        // A camera sitting back and slightly above the ball frame, looking
        // down at the origin-ish region with a modest tilt+yaw (rvec not
        // axis-aligned, to exercise the general case).
        const std::array<double, 3> rvec{ 0.15, -0.25, 0.05 };
        const std::array<double, 3> tvec{ 0.10, -0.30, 3.20 };
        const double fx = 1400.0, fy = 1400.0, cx = 720.0, cy = 540.0;
        const double imgW = 1440.0, imgH = 1080.0;

        const CameraPose pose = openCvExtrinsicsToQt(rvec, tvec);
        const QMatrix4x4 proj = buildProjectionMatrix(fx, fy, cx, cy, imgW, imgH);
        const std::array<double, 9> R = rodriguesToMatrix(rvec);

        // Several world points, including off-axis ones (not just the
        // principal point) to catch any axis-sign error.
        const QVector3D pts[] = {
            { 0.0f, 0.0f, 0.0f },      // the ball
            { 0.3f, -0.5f, 1.2f },     // roughly hub height
            { -0.4f, -0.2f, 0.05f },   // near the ball, off to one side
            { 0.6f, -0.6f, 1.8f },     // up and to the target side
            { -0.2f, -0.55f, 0.02f },
        };
        int i = 0;
        for (const QVector3D &p : pts) {
            const Pixel cvPix = openCvProject(R, tvec, fx, fy, cx, cy, p);
            const Pixel qtPix = qtProject(pose, proj, imgW, imgH, p);
            char label[64];
            std::snprintf(label, sizeof(label), "pt%d u", i);
            checkBool("pt validity agrees", qtPix.valid, cvPix.valid);
            if (cvPix.valid && qtPix.valid) {
                checkNear(label, qtPix.u, cvPix.u, 0.5);
                std::snprintf(label, sizeof(label), "pt%d v", i);
                checkNear(label, qtPix.v, cvPix.v, 0.5);
            }
            ++i;
        }

        // worldToQtCameraLocal should agree with view matrix multiplication
        // directly (both are exposed on the header's public API).
        const QVector3D wp(0.2f, -0.4f, 1.0f);
        const QVector3D viaMatrix = (pose.viewMatrix * QVector4D(wp, 1.0f)).toVector3D();
        const QVector3D viaFn     = worldToQtCameraLocal(R, tvec, wp);
        checkNear("worldToQtCameraLocal vs viewMatrix x", viaFn.x(), viaMatrix.x(), 1e-4);
        checkNear("worldToQtCameraLocal vs viewMatrix y", viaFn.y(), viaMatrix.y(), 1e-4);
        checkNear("worldToQtCameraLocal vs viewMatrix z", viaFn.z(), viaMatrix.z(), 1e-4);

        // Camera position sanity: the camera should be "behind" the ball
        // frame's origin along its own look direction — cheap check: the
        // world origin transformed into Qt camera space should have Zc' < 0
        // (in front, Qt convention) since tvec places the ball ~3.2 m out in
        // front (Zc_opencv ~ +3.2 > 0).
        const QVector4D originInCam = pose.viewMatrix * QVector4D(0, 0, 0, 1);
        checkBool("ball is in front of camera (Qt: z<0)", originInCam.z() < 0.0f, true);
    }

    // Degenerate projection matrix (invalid intrinsics) must not crash / must
    // return a default matrix.
    std::printf("\n=== buildProjectionMatrix degrade ===\n");
    {
        const QMatrix4x4 bad = buildProjectionMatrix(0, 0, 0, 0, 0, 0);
        checkNear("degenerate fx=0 -> zero matrix [0]", bad(0, 0), 0.0, 1e-9);
    }

    // ── orthoExtrinsicsToQt + buildOrthoProjectionMatrix (composed pixel
    // match) — the Ortho counterpart of the PnP test above. Mirrors
    // camera_projection.h's OrthographicProjection formula EXACTLY:
    //   u = originX + xSign.s.X,   v = originY - s.Z   (world Y dropped)
    // for both mirror signs, pinning this file's Qt-side conversion against
    // an independent hand-rolled re-implementation of that formula. ─────────
    std::printf("\n=== orthoExtrinsicsToQt + buildOrthoProjectionMatrix (composed pixel match) ===\n");
    for (const double xSign : { 1.0, -1.0 }) {
        const double s = 512.0, originX = 640.0, originY = 900.0;
        const double imgW = 1280.0, imgH = 1024.0;

        const CameraPose pose = orthoExtrinsicsToQt(xSign);
        const QMatrix4x4 proj = buildOrthoProjectionMatrix(s, originX, originY, imgW, imgH);

        // World points, including a spread of Y (depth) values that must NOT
        // change u/v at all — the load-bearing "world Y dropped" contract.
        const QVector3D pts[] = {
            { 0.0f, 0.0f, 0.0f },
            { 0.3f, -0.5f, 1.2f },
            { 0.3f,  0.9f, 1.2f },     // same X/Z as above, very different Y
            { -0.4f, -0.2f, 0.05f },
            { 0.6f, 1.4f, 1.8f },
        };
        char tag[16];
        std::snprintf(tag, sizeof(tag), "xSign=%d", int(xSign));
        int i = 0;
        for (const QVector3D &p : pts) {
            const double wantU = originX + xSign * s * double(p.x());
            const double wantV = originY - s * double(p.z());
            const Pixel qtPix  = qtProject(pose, proj, imgW, imgH, p);
            char label[64];
            std::snprintf(label, sizeof(label), "%s pt%d u", tag, i);
            checkBool("pt validity (ortho: always valid, w==1)", qtPix.valid, true);
            checkNear(label, qtPix.u, wantU, 0.5);
            std::snprintf(label, sizeof(label), "%s pt%d v", tag, i);
            checkNear(label, qtPix.v, wantV, 0.5);
            ++i;
        }

        // Depth (world Y) must land purely on Zc, never on Xc/Yc — direct
        // check on the view matrix, independent of the projection step above.
        // The "away from camera" direction is world +xSign.Y (a mirrored
        // xSign=-1 setup looks the other way down the Y axis too — see the
        // header derivation) so farY is scaled by xSign to stay in the
        // camera's actual forward direction for both cases.
        const double farY = 3.0 * xSign;
        const QVector4D near = pose.viewMatrix * QVector4D(0.2f, 0.0f, 0.4f, 1.0f);
        const QVector4D far  = pose.viewMatrix * QVector4D(0.2f, float(farY), 0.4f, 1.0f);
        char label[96];
        std::snprintf(label, sizeof(label), "%s Y-shift leaves Xc unchanged", tag);
        checkNear(label, near.x(), far.x(), 1e-5);
        std::snprintf(label, sizeof(label), "%s Y-shift leaves Yc unchanged", tag);
        checkNear(label, near.y(), far.y(), 1e-5);
        std::snprintf(label, sizeof(label), "%s farther world-Y => farther from camera (Zc more negative)", tag);
        checkBool(label, far.z() < near.z(), true);
    }

    std::printf("\n=== buildOrthoProjectionMatrix degrade ===\n");
    {
        const QMatrix4x4 bad = buildOrthoProjectionMatrix(0.0, 640.0, 512.0, 1280.0, 1024.0);
        checkNear("degenerate s=0 -> zero matrix [0]", bad(0, 0), 0.0, 1e-9);
        const QMatrix4x4 bad2 = buildOrthoProjectionMatrix(500.0, 640.0, 512.0, 0.0, 1024.0);
        checkNear("degenerate imgW=0 -> zero matrix [0]", bad2(0, 0), 0.0, 1e-9);
    }

    // ── globalSForP canonical table ──────────────────────────────────────────
    std::printf("\n=== globalSForP ===\n");
    {
        checkNear("P1", globalSForP(1), 0.00, 1e-9);
        checkNear("P2", globalSForP(2), 0.33, 1e-9);
        checkNear("P3", globalSForP(3), 0.66, 1e-9);
        checkNear("P4", globalSForP(4), 1.00, 1e-9);
        checkNear("P5", globalSForP(5), 1.40, 1e-9);
        checkNear("P6", globalSForP(6), 1.80, 1e-9);
        checkNear("P7", globalSForP(7), 2.00, 1e-9);
        checkNear("P8", globalSForP(8), 3.00, 1e-9);
        checkNear("P0 invalid", globalSForP(0), -1.0, 1e-9);
        checkNear("P9 invalid", globalSForP(9), -1.0, 1e-9);
    }

    // ── playheadToGlobalS ─────────────────────────────────────────────────────
    std::printf("\n=== playheadToGlobalS ===\n");
    {
        // P4 at 1,000,000 us, P7 at 2,000,000 us — matches wrist_analyzer.cpp's
        // own callout timestamps in spirit (a mid-swing pair).
        const std::vector<PhaseAnchor> anchors{ { 4, 1'000'000 }, { 7, 2'000'000 } };

        const auto sMid = playheadToGlobalS(anchors, 1'500'000);
        checkBool("mid: has value", sMid.has_value(), true);
        if (sMid) checkNear("mid: s == 1.5", *sMid, 1.5, 1e-9);

        const auto sBefore = playheadToGlobalS(anchors, 0);
        checkBool("before first anchor: has value", sBefore.has_value(), true);
        if (sBefore) checkNear("before: clamps to P4's s", *sBefore, 1.0, 1e-9);

        const auto sAfter = playheadToGlobalS(anchors, 5'000'000);
        checkBool("after last anchor: has value", sAfter.has_value(), true);
        if (sAfter) checkNear("after: clamps to P7's s", *sAfter, 2.0, 1e-9);

        // Three anchors spanning backswing+downswing (P1, P4, P7).
        const std::vector<PhaseAnchor> anchors3{
            { 1, 0 }, { 4, 1'000'000 }, { 7, 2'000'000 }
        };
        const auto sBack = playheadToGlobalS(anchors3, 500'000);
        if (sBack) checkNear("backswing half: s == 0.5", *sBack, 0.5, 1e-9);

        // < 2 distinct-P anchors -> nullopt.
        const std::vector<PhaseAnchor> one{ { 4, 1'000'000 } };
        checkBool("single anchor -> nullopt", playheadToGlobalS(one, 500'000).has_value(), false);
        checkBool("empty -> nullopt", playheadToGlobalS({}, 500'000).has_value(), false);

        // Duplicate P (e.g. positions ∪ callouts both carrying P7): first
        // occurrence wins, second is dropped as a dup (not a second anchor).
        const std::vector<PhaseAnchor> dup{ { 7, 2'000'000 }, { 7, 2'500'000 }, { 4, 1'000'000 } };
        const auto sDup = playheadToGlobalS(dup, 1'500'000);
        if (sDup) checkNear("dedup keeps first P7 t_us (2.0M, not 2.5M)", *sDup, 1.5, 1e-9);

        // Out-of-range P index in the anchor list is ignored, not crashed on.
        const std::vector<PhaseAnchor> bogus{ { 0, 500 }, { 4, 1'000'000 }, { 7, 2'000'000 } };
        checkBool("bogus P=0 ignored, still 2 valid anchors",
                 playheadToGlobalS(bogus, 1'500'000).has_value(), true);
    }

    // ── segmentForGlobalS ─────────────────────────────────────────────────────
    std::printf("\n=== segmentForGlobalS ===\n");
    {
        auto sp0 = segmentForGlobalS(0.0);
        checkNear("s=0 segment", sp0.segment, 0, 1e-9);
        checkNear("s=0 localS", sp0.localS, 0.0, 1e-9);

        auto sp1 = segmentForGlobalS(0.999);
        checkNear("s=0.999 segment", sp1.segment, 0, 1e-9);

        auto sp2 = segmentForGlobalS(1.0);
        checkNear("s=1.0 segment", sp2.segment, 1, 1e-9);
        checkNear("s=1.0 localS", sp2.localS, 0.0, 1e-9);

        auto sp3 = segmentForGlobalS(2.5);
        checkNear("s=2.5 segment", sp3.segment, 2, 1e-9);
        checkNear("s=2.5 localS", sp3.localS, 0.5, 1e-9);

        // Beyond [0,3] clamps.
        auto sp4 = segmentForGlobalS(3.5);
        checkNear("s=3.5 clamps segment", sp4.segment, 2, 1e-9);
        checkNear("s=3.5 clamps localS", sp4.localS, 1.0, 1e-9);

        auto sp5 = segmentForGlobalS(-1.0);
        checkNear("s=-1 clamps segment", sp5.segment, 0, 1e-9);
        checkNear("s=-1 clamps localS", sp5.localS, 0.0, 1e-9);
    }

    std::printf("\n%s (%d failing checks)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
