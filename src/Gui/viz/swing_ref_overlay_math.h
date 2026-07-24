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

// swing_ref_overlay_math.h — pure math for the WP5a swing-reference overlay
// (T7): the OpenCV-projection <-> Qt-Quick3D-camera conversion, and the
// playhead(us) -> reference-model global-s phase mapping.
//
// Header-only, Qt Core/Gui ONLY (QMatrix4x4, QVector3D, QQuaternion) + STL —
// deliberately NO QtQuick3D and NO OpenCV, so it is unit-testable from the
// analyzer-tests suite (Qt6::Core + Qt6::Gui are already linked by every
// suite there) without paying for either dependency. See
// src/Analysis/tests/swing_ref_overlay_math_test.cpp, which hand-rolls the
// OpenCV pinhole projection in plain arithmetic (per the T7 brief: do NOT
// link OpenCV into Gui code) to pin this file's Qt-side conversion against
// it. Precedent: src/Gui/cameras/viz_frame.h + its
// src/Analysis/tests/viz_frame_test.cpp (Gui math header, Analysis-suite test
// via a relative #include).
//
// ---------------------------------------------------------------------------
// OpenCV -> Qt Quick 3D camera conversion
// ---------------------------------------------------------------------------
// camera_projection.h's convention (pinned by camera_projection.cpp, mirrors
// cv::projectPoints exactly — see that file's own header comment):
//   Xc = R.Xw + t        world -> camera, R from a Rodrigues rvec (row-major)
//   camera looks down +Zc, image y is DOWN
//   u = fx.(Xc/Zc) + cx,  v = fy.(Yc/Zc) + cy      (no distortion: the
//   persisted SwingRefProjection block carries no dist[] — PoseFit's
//   nominal-FOV fit assumes zero distortion)
//
// Qt Quick 3D's Node/Camera convention: camera looks down LOCAL -Z, +Y is up
// (right-handed, OpenGL-style). The two camera-local frames differ by
// exactly a 180 degree rotation about the shared local X (right) axis:
//   D = diag(1,-1,-1)     (this IS a proper rotation: det(D) = +1)
// so, writing a prime for the Qt-convention camera-local point,
//   Xc' = D.Xc = D.R.Xw + D.t   =>   Rqt = D.R,   tqt = D.t   (world->camera)
//
// The camera's WORLD pose — i.e. the CustomCamera Node's `position`/
// `rotation` properties, since Quick3D has no way to set an arbitrary
// world<->camera matrix directly on a Node, only position+rotation+scale
// (QQuick3DNode has no settable raw-matrix property; see qquick3dnode_p.h) —
// is the INVERSE of the above:
//   position = -R^T . t                (D cancels: D is orthogonal/self-
//                                        inverse, D^T D = I)
//   rotation = quaternion(Rqt^T) = quaternion(R^T with its 2nd and 3rd
//                                  COLUMNS negated)
// `openCvExtrinsicsToQt()` below returns both, plus the raw 4x4 view matrix
// (world->camera-local, Qt axes) for composition/testing.
//
// Projection: solving x_ndc = 2u/w-1, y_ndc = 1-2v/h (standard OpenGL NDC,
// y-up; every Qt item's pixel origin is top-left, like the OpenCV image
// convention, so this is the standard "flip v" step) for a Qt-convention
// camera-space point yields the classic "OpenCV intrinsics -> OpenGL
// projection matrix" form (e.g. Kyle Simek's derivation; re-derived and
// pinned by the unit test here):
//
//   [ 2fx/w      0      (w-2cx)/w         0        ]
//   [   0      2fy/h    (2cy-h)/h         0        ]
//   [   0        0    -(f+n)/(f-n)   -2fn/(f-n)    ]
//   [   0        0         -1              0        ]
//
// `w`,`h` here MUST be the SAME width/height that fx/fy/cx/cy are expressed
// in — i.e. `SwingRefProjection::width/height` (the ORIGINAL capture frame
// dimensions), NOT the on-screen content-rect pixel size. NDC is resolution-
// independent: Qt Quick3D stretches the -1..1 clip range across whatever the
// View3D's CURRENT on-screen size happens to be, so as long as the View3D is
// aspect-locked to width:height (matching PpCameraFrame's existing
// aspect-locked video geometry), the projected geometry lands at the correct
// FRACTION of the viewport regardless of actual display resolution — exactly
// like the persisted `projected[].points[].buttX/Y` polyline data, which is
// ALSO normalized by frameWidth/frameHeight for the same reason. See
// swing_ref_overlay_model.cpp's header comment for the content-rect design
// note (contentX/Y/W/H are accepted as inputs but do not feed this matrix).
//
// near/far: fixed constants (kNearM/kFarM below), not tunable — the swing
// geometry is all within a few metres of the ball-frame origin.
// ---------------------------------------------------------------------------

#pragma once

#include <QMatrix4x4>
#include <QMatrix3x3>
#include <QVector3D>
#include <QQuaternion>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace pinpoint::swingref::overlaymath {

// Fixed clip planes (metres, ball-frame) — not tunable; see header note above.
inline constexpr float kNearM = 0.05f;
inline constexpr float kFarM  = 20.0f;

// ---------------------------------------------------------------------------
// Rodrigues rvec -> row-major 3x3 rotation matrix. Hand-rolled (no OpenCV) —
// standard Rodrigues' rotation formula, R = I + sin(theta) K + (1-cos(theta)) K^2,
// K = skew(unit axis). Matches cv::Rodrigues exactly for a proper rotation
// vector (this is the textbook closed form OpenCV itself uses).
// ---------------------------------------------------------------------------
inline std::array<double, 9> rodriguesToMatrix(const std::array<double, 3> &rvec)
{
    const double theta = std::sqrt(rvec[0] * rvec[0] + rvec[1] * rvec[1] + rvec[2] * rvec[2]);
    if (theta < 1e-12)
        return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const double kx = rvec[0] / theta, ky = rvec[1] / theta, kz = rvec[2] / theta;
    const double c = std::cos(theta), s = std::sin(theta), C = 1.0 - c;
    return {
        c + kx * kx * C,       kx * ky * C - kz * s,  kx * kz * C + ky * s,
        ky * kx * C + kz * s,  c + ky * ky * C,       ky * kz * C - kx * s,
        kz * kx * C - ky * s,  kz * ky * C + kx * s,  c + kz * kz * C
    };
}

// World point (ball frame) -> Qt-Quick3D camera-local space (Y up, looks down
// -Z), via the SAME extrinsics as camera_projection.h's imagePoint(). Exposed
// so the unit test can apply the view matrix and the "OpenCV path" (plain
// arithmetic, no Qt types) side by side.
inline QVector3D worldToQtCameraLocal(const std::array<double, 9> &R,
                                      const std::array<double, 3> &t,
                                      const QVector3D &world)
{
    const double xw = world.x(), yw = world.y(), zw = world.z();
    const double Xc = R[0] * xw + R[1] * yw + R[2] * zw + t[0];
    const double Yc = R[3] * xw + R[4] * yw + R[5] * zw + t[1];
    const double Zc = R[6] * xw + R[7] * yw + R[8] * zw + t[2];
    return QVector3D(float(Xc), float(-Yc), float(-Zc));   // D = diag(1,-1,-1)
}

struct CameraPose {
    QVector3D   position;      // CustomCamera world position (ball frame, m)
    QQuaternion rotation;      // CustomCamera world rotation
    QMatrix4x4  viewMatrix;    // world->camera-local (Qt axes), for testing/composition
};

// OpenCV extrinsics (Rodrigues rvec, tvec — SwingRefProjection::rvec/tvec) ->
// the Qt Quick3D CustomCamera's world position/rotation. See the header
// comment above for the derivation.
inline CameraPose openCvExtrinsicsToQt(const std::array<double, 3> &rvec,
                                       const std::array<double, 3> &tvec)
{
    const std::array<double, 9> R = rodriguesToMatrix(rvec);

    // position = -R^T . t
    const double px = -(R[0] * tvec[0] + R[3] * tvec[1] + R[6] * tvec[2]);
    const double py = -(R[1] * tvec[0] + R[4] * tvec[1] + R[7] * tvec[2]);
    const double pz = -(R[2] * tvec[0] + R[5] * tvec[1] + R[8] * tvec[2]);

    // Rqt^T = R^T with columns 2,3 (1-based) negated — the camera-to-world
    // rotation matrix (row-major).
    const float M[9] = {
        float(R[0]), float(-R[3]), float(-R[6]),
        float(R[1]), float(-R[4]), float(-R[7]),
        float(R[2]), float(-R[5]), float(-R[8])
    };

    CameraPose out;
    out.position = QVector3D(float(px), float(py), float(pz));
    out.rotation = QQuaternion::fromRotationMatrix(QMatrix3x3(M));

    // view = [Rqt | tqt; 0 0 0 1], Rqt = D.R (row-major: row0 unchanged, rows
    // 1,2 negated), tqt = D.t.
    out.viewMatrix = QMatrix4x4(
        float(R[0]),  float(R[1]),  float(R[2]),   float(tvec[0]),
        float(-R[3]), float(-R[4]), float(-R[5]),  float(-tvec[1]),
        float(-R[6]), float(-R[7]), float(-R[8]),  float(-tvec[2]),
        0.0f, 0.0f, 0.0f, 1.0f);
    return out;
}

// OpenCV pinhole intrinsics (fx,fy,cx,cy over an imgW x imgH frame) -> a Qt
// Quick3D-style projection matrix (see header derivation). Returns a default
// (zero) matrix when the inputs are degenerate — callers must check
// fx/fy/imgW/imgH validity themselves (this never guesses a fallback FOV).
inline QMatrix4x4 buildProjectionMatrix(double fx, double fy, double cx, double cy,
                                        double imgW, double imgH,
                                        float nearPlane = kNearM, float farPlane = kFarM)
{
    if (fx <= 0.0 || fy <= 0.0 || imgW <= 0.0 || imgH <= 0.0)
        return QMatrix4x4(0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0);   // explicit zero, NOT the identity QMatrix4x4() would default-construct

    const double w = imgW, h = imgH;
    const double n = double(nearPlane), f = double(farPlane);
    return QMatrix4x4(
        float(2.0 * fx / w), 0.0f,                float((w - 2.0 * cx) / w), 0.0f,
        0.0f,                float(2.0 * fy / h), float((2.0 * cy - h) / h), 0.0f,
        0.0f,                0.0f,                float(-(f + n) / (f - n)), float(-2.0 * f * n / (f - n)),
        0.0f,                0.0f,                -1.0f,                     0.0f);
}

// ---------------------------------------------------------------------------
// Orthographic (Phase A "2D-first") camera <-> Qt Quick3D CustomCamera
// ---------------------------------------------------------------------------
// camera_projection.h's OrthographicProjection maps world (ball frame) ->
// image px by DROPPING world Y (depth) entirely:
//   u = originX + xSign.s.X,   v = originY - s.Z
// (SwingRefProjection::sPxPerM/originX/originY/xSign — populated ONLY when
// kind == "Ortho"; this is now the PRIMARY path SwingRefStage resolves,
// see wrist_analyzer.cpp). No extrinsics were solved (no rvec/tvec, no PnP)
// so this conversion is built directly from first principles rather than
// going through openCvExtrinsicsToQt()/rodriguesToMatrix().
//
// Camera-local axes (Qt convention: looks down local -Z, +Y up, +X right),
// expressed as a fixed world-space rotation (no address-frame dependence,
// unlike the PnP path):
//   local +X (right) <- xSign . world X   (image right = xSign.X, matches u)
//   local +Y (up)    <- world Z           (image up = +Z, matches v)
//   local +Z         <- -xSign . world Y  (completes a right-handed frame:
//                                          local Z = local X (cross) local Y)
// world Y (depth) therefore only ever reaches the camera's Z axis — exactly
// the axis an ORTHOGRAPHIC projection matrix ignores for x/y, matching "world
// Y dropped" above bit-for-bit. The camera is placed at a fixed distance
// `kOrthoCamDistM` back along its own look direction (world position
// (0, -xSign.kOrthoCamDistM, 0)) purely so the golfer (near Y=0) sits inside
// [kNearM,kFarM] and the ruled-surface cylinders still z-sort front-to-back
// sanely; because the projection is orthographic this offset provably cannot
// change u/v (see buildOrthoProjectionMatrix below — x/y depend only on
// camera-local X/Y, never Z), only depth ordering.
//
// buildOrthoProjectionMatrix() is derived by substituting Xc = xSign.Xw and
// Yc = Zw (both hold exactly for this fixed rotation with the camera
// centred on the X/Z=0 axis) into the target NDC equations
// ndc.x = 2u/w - 1, ndc.y = 1 - 2v/h and solving for the clip-space linear
// map (w == 1 throughout — true parallel projection, no perspective divide):
//   clip.x = (2s/w).Xc + (2.originX/w - 1)
//   clip.y = (2s/h).Yc + (1 - 2.originY/h)
//   clip.z = standard OpenGL ortho depth map over [-far,-near] -> [-1,1]
//   clip.w = 1
// Pinned end-to-end (view * projection -> NDC -> pixel reproduces u,v
// exactly) by swing_ref_overlay_math_test.cpp, mirroring the PnP composed-
// pixel test above.
// ---------------------------------------------------------------------------

// Fixed camera standoff (metres, ball-frame) for the orthographic path — see
// header note above; any positive value leaves u/v unchanged (parallel
// projection), only near/far clipping and cylinder z-order.
inline constexpr float kOrthoCamDistM = 5.0f;

// World (ball-frame) -> Qt Quick3D CustomCamera world pose for the Ortho
// projection. `xSign` is SwingRefProjection::xSign (+1/-1). No rvec/tvec
// input — the rotation is a fixed axis permutation/mirror (see header note),
// not solved per-swing.
inline CameraPose orthoExtrinsicsToQt(double xSign, float camDistM = kOrthoCamDistM)
{
    const double sgn = xSign >= 0.0 ? 1.0 : -1.0;

    // World->camera-local rotation R (row-major), rows = local X/Y/Z axes
    // expressed in world coords' DUAL (R maps world->local; see the header
    // derivation — Row2 = Row0 x Row1 guarantees a proper rotation for any
    // sign choice, never a reflection).
    const std::array<double, 9> R = {
        sgn, 0.0, 0.0,
        0.0, 0.0, 1.0,
        0.0, -sgn, 0.0
    };
    const std::array<double, 3> t = { 0.0, 0.0, -double(camDistM) };

    CameraPose out;
    out.position = QVector3D(0.0f, float(-sgn * double(camDistM)), 0.0f);

    // Camera world ROTATION matrix (local->world; columns = local axes in
    // world coords) is R^T for this orthonormal R.
    const float M[9] = {
        float(R[0]), float(R[3]), float(R[6]),
        float(R[1]), float(R[4]), float(R[7]),
        float(R[2]), float(R[5]), float(R[8])
    };
    out.rotation = QQuaternion::fromRotationMatrix(QMatrix3x3(M));

    out.viewMatrix = QMatrix4x4(
        float(R[0]), float(R[1]), float(R[2]), float(t[0]),
        float(R[3]), float(R[4]), float(R[5]), float(t[1]),
        float(R[6]), float(R[7]), float(R[8]), float(t[2]),
        0.0f, 0.0f, 0.0f, 1.0f);
    return out;
}

// Orthographic projection matrix reproducing u = originX + s.Xc,
// v = originY - s.Yc EXACTLY (Xc/Yc are the camera-local coords produced by
// orthoExtrinsicsToQt() above — world Y already folded into Zc there, so it
// cannot leak into x/y here). Degenerate (zero matrix) when s/imgW/imgH are
// non-positive, mirroring buildProjectionMatrix()'s degrade contract.
inline QMatrix4x4 buildOrthoProjectionMatrix(double s, double originX, double originY,
                                             double imgW, double imgH,
                                             float nearPlane = kNearM, float farPlane = kFarM)
{
    if (!(s > 0.0) || imgW <= 0.0 || imgH <= 0.0)
        return QMatrix4x4(0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0);

    const double w = imgW, h = imgH;
    const double n = double(nearPlane), f = double(farPlane);
    return QMatrix4x4(
        float(2.0 * s / w), 0.0f,               0.0f, float(2.0 * originX / w - 1.0),
        0.0f,               float(2.0 * s / h), 0.0f, float(1.0 - 2.0 * originY / h),
        0.0f,               0.0f,               float(-2.0 / (f - n)), float(-(f + n) / (f - n)),
        0.0f,               0.0f,               0.0f,  1.0f);
}

// ---------------------------------------------------------------------------
// P-index -> GLOBAL s (P1..P8) — fixed by the reference model's own keyframe
// authoring (src/Models/swing_reference.h RefConfig comments: alphaBack/
// betaBack keyed {0,0.33,0.66,1.00} = P1..P4; alphaDown/betaDown keyed
// {0.00,0.40,0.80,1.00} = P4..P7; alphaFollow/betaFollow keyed {0.00,...,1.00}
// = P7,P8). Global s = segmentIndex(0/1/2) + local s. Cross-confirmed in
// wrist_analyzer.cpp's p6Us/p8Us callout-anchoring comment ("between P5@1.40
// and P6@1.80"). Returns -1 for p outside [1,8] (not a modelled P).
// ---------------------------------------------------------------------------
inline double globalSForP(int p)
{
    switch (p) {
    case 1: return 0.00;   // Backswing s=0            (Address)
    case 2: return 0.33;   // Backswing s=0.33
    case 3: return 0.66;   // Backswing s=0.66
    case 4: return 1.00;   // Backswing s=1 == Downswing s=0   (Top)
    case 5: return 1.40;   // Downswing s=0.40
    case 6: return 1.80;   // Downswing s=0.80
    case 7: return 2.00;   // Downswing s=1 == FollowThrough s=0   (Impact)
    case 8: return 3.00;   // FollowThrough s=1
    default: return -1.0;
    }
}

struct PhaseAnchor { int p = 0; std::int64_t tUs = 0; };

// Global s in [0,3] for `playheadUs`, from whatever (P, t_us) anchors are
// available. Callers should merge analysisDetail.club.positions (guaranteed
// non-empty whenever reference.valid — the stage's canRun gate requires
// !shaft.positions.empty()) with reference.callouts (sparser: at most P4/P6/
// P7, each independently optional, but its own t_us already degrades via
// wrist_analyzer.cpp's positionTime() fallback to a segmentation-event
// timestamp when `positions` lacks that P — a useful second source) BEFORE
// calling this. De-dups by P (first occurrence wins). Requires >=2 distinct-P
// anchors; returns nullopt otherwise (caller freezes/hides the ghost — no
// reliable phase to render).
//
// Edge behaviour: playheadUs before the first anchor / after the last anchor
// CLAMPS to that anchor's global s (the ghost freezes at the nearest known
// pose, rather than extrapolating). This is a clamp across the WHOLE P1..P8
// timeline — distinct from MonotoneCubic's internal per-segment clamp, which
// only bounds a single segment's own local s in [0,1].
inline std::optional<double> playheadToGlobalS(std::vector<PhaseAnchor> anchors,
                                               std::int64_t playheadUs)
{
    std::vector<PhaseAnchor> uniq;
    for (const PhaseAnchor &a : anchors) {
        if (globalSForP(a.p) < 0.0)
            continue;
        bool dup = false;
        for (const PhaseAnchor &u : uniq)
            if (u.p == a.p) { dup = true; break; }
        if (!dup)
            uniq.push_back(a);
    }
    if (uniq.size() < 2)
        return std::nullopt;
    std::sort(uniq.begin(), uniq.end(),
             [](const PhaseAnchor &a, const PhaseAnchor &b) { return a.p < b.p; });

    if (playheadUs <= uniq.front().tUs) return globalSForP(uniq.front().p);
    if (playheadUs >= uniq.back().tUs)  return globalSForP(uniq.back().p);

    for (std::size_t i = 0; i + 1 < uniq.size(); ++i) {
        const PhaseAnchor &a = uniq[i], &b = uniq[i + 1];
        if (playheadUs >= a.tUs && playheadUs <= b.tUs) {
            const double sa = globalSForP(a.p), sb = globalSForP(b.p);
            const double dt = double(b.tUs - a.tUs);
            const double t  = dt > 0.0 ? double(playheadUs - a.tUs) / dt : 0.0;
            return sa + (sb - sa) * t;
        }
    }
    return globalSForP(uniq.back().p);   // unreachable given the bounds checks above
}

// Global s [0,3] -> (Segment index 0/1/2, local s in that segment). Segment
// indices match pinpoint::swingref::Segment (Backswing=0, Downswing=1,
// FollowThrough=2) but are carried as plain ints so this header stays
// swingref-model-free.
struct SegmentPhase { int segment = 0; double localS = 0.0; };

inline SegmentPhase segmentForGlobalS(double globalS)
{
    const double g = std::clamp(globalS, 0.0, 3.0);
    if (g < 1.0) return { 0, g };
    if (g < 2.0) return { 1, g - 1.0 };
    return { 2, g - 2.0 };
}

} // namespace pinpoint::swingref::overlaymath
