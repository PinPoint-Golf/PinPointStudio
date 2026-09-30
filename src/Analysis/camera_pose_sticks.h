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

// A camera's pose in the bay from the alignment sticks
// (docs/design/camera_calibration_design.md §4.4 "the geometry path that needs no
// printer", §3a.6 the two-camera pass; docs/validation/two_camera_capture_protocol.md
// §4 B the stick clips) — and the down-the-line camera RELATIVE to the face-on one in
// the frame the shaft fusion works in (shaft_fusion.h), which is what
// dtl_continuous_track_design_update.md §4 item 1 asks the protocol session for:
// club3d.camera.{calibrated, yawDeg, pitchDeg, rollDeg, offset}.
//
// THE BAY FRAME. Origin at the ball spot. X along the target-line stick toward the
// declared target end. Z up. Y = Z × X. (For a right-hander at address the golfer
// stands on the +Y side; the face-on camera is on −Y looking +Y, and its image-right
// is then +X — the fusion's "for a right-hander +X is the target".)
//
// WHAT IS SOLVED FROM WHAT (design §4.4 "design to the primitives"):
//   rotation — from LINE DIRECTIONS only, never endpoints. Each image line back-
//     projects to a plane through the camera centre with normal Kᵀl; a world
//     direction seen along that line is perpendicular to that normal. Two parallel
//     floor sticks (the target line and the hands line) fix world X as the cross
//     product of their two plane normals — well conditioned whether their vanishing
//     point is in the frame (the DTL camera, looking along them) or at infinity (the
//     face-on camera, seeing them parallel). The cross stick then fixes the rotation
//     about X (Y ⊥ X and ⊥ its plane normal), and the plumbed vertical stick, where
//     given, refines that one angle by least squares with the cross stick: the two
//     over-determine it by one, and the residual is reported.
//   origin — the ball's image point gives the translation's DIRECTION.
//   scale — from the target stick's far end (a known 1.219 m along +X) and, where
//     given, the vertical stick's top (a known height above the ball spot): the
//     endpoints are 5–15 px things and are used for scale and nothing else.
//   signs — +X from the operator's declared target end; up from the camera being
//     roughly upright (world Z maps to image −y).
//
// INTRINSICS ARE AN INPUT. Everything on the ground is coplanar and one view of a
// plane does not constrain focal length (design §4.4 ⛔); f, cx, cy come from the
// card clips (§4 A) or a typed lens. cx, cy are in the SAME pixel frame as the
// observations — a recorded ROI moves them, and the protocol writes the ROI down.
//
// Pure std, no Qt, no OpenCV: the same arithmetic runs in the app, in swinglab and
// in tools/shaftlab/dtl_calib_solve.py (which must agree with the unit test's
// synthetic bay to 1e-6°).

#include <cmath>
#include <string>

#include "shaft_fusion.h"   // fusion::Vec3, kPi

namespace pinpoint::analysis::calib {

using fusion::Vec3;
using fusion::kPi;

struct Intrinsics { double f = 0, cx = 0, cy = 0; };

// An image line through two points (px). Stored as ax + by + c = 0.
struct ImageLine {
    double a = 0, b = 0, c = 0;
    bool   ok = false;
    static ImageLine through(double x0, double y0, double x1, double y1)
    {
        ImageLine l;
        l.a = y1 - y0; l.b = x0 - x1; l.c = -(l.a * x0 + l.b * y0);
        const double n = std::hypot(l.a, l.b);
        if (n > 1e-9) { l.a /= n; l.b /= n; l.c /= n; l.ok = true; }
        return l;
    }
};

struct StickObs {
    ImageLine target;       // the target-line stick (through the ball spot, along ±X)
    ImageLine hands;        // the hands-line stick (parallel to it, at +Y)
    ImageLine cross;        // the cross stick (through the ball spot, along ±Y)
    ImageLine vertical;     // the plumbed stick at the ball spot (along Z); ok=false ⇒ none
    double ballU = 0, ballV = 0;           // the ball spot, px
    double targetEndU = 0, targetEndV = 0; // the +X end of the target stick, px
    double stickLenM = 1.219;              // 48 in
    bool   haveVerticalTop = false;
    double verticalTopU = 0, verticalTopV = 0;   // the vertical stick's top, px (scale, optional)
    double verticalLenM = 1.219;
};

struct Mat3 {
    double m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    Vec3 row(int i) const { return { m[i][0], m[i][1], m[i][2] }; }
    Vec3 col(int j) const { return { m[0][j], m[1][j], m[2][j] }; }
    Vec3 apply(const Vec3 &v) const
    { return { row(0).dot(v), row(1).dot(v), row(2).dot(v) }; }
    Vec3 applyT(const Vec3 &v) const
    { return { col(0).dot(v), col(1).dot(v), col(2).dot(v) }; }
    static Mat3 fromColumns(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2)
    {
        Mat3 r;
        r.m[0][0] = c0.x; r.m[1][0] = c0.y; r.m[2][0] = c0.z;
        r.m[0][1] = c1.x; r.m[1][1] = c1.y; r.m[2][1] = c1.z;
        r.m[0][2] = c2.x; r.m[1][2] = c2.y; r.m[2][2] = c2.z;
        return r;
    }
};

// A solved camera. R maps WORLD (bay) coordinates to CAMERA coordinates (x right,
// y down, z forward = the optical axis); C is the camera centre in the bay frame.
struct CameraPose {
    bool   ok = false;
    std::string reason;
    Mat3   R;
    Vec3   C;
    // Read-outs in the bay frame: the optical axis's heading (atan2(dy, dx), deg),
    // its dip below level (+ = looking down), and roll about the axis (+ = image
    // clockwise as seen by the camera).
    double yawDeg = 0, pitchDeg = 0, rollDeg = 0;
    // Angular residual (deg) between the cross stick's and the vertical stick's
    // opinion of the rotation about X; 0 when no vertical was given.
    double rollResidualDeg = 0;
    // Scale residual: the two scale estimates (target end, vertical top) as a ratio
    // − 1; 0 when only one was given.
    double scaleResidual = 0;
};

namespace detail {
inline Vec3 planeNormal(const Intrinsics &K, const ImageLine &l)
{
    // Points x on the line satisfy lᵀx = 0 with x = K X_cam, so the back-projected
    // plane's normal in camera coordinates is Kᵀl: (a f, b f, a cx + b cy + c).
    return Vec3 { l.a * K.f, l.b * K.f, l.a * K.cx + l.b * K.cy + l.c }.unit();
}
inline Vec3 ray(const Intrinsics &K, double u, double v)
{ return Vec3 { (u - K.cx) / K.f, (v - K.cy) / K.f, 1.0 }; }
} // namespace detail

inline CameraPose solveCameraPose(const Intrinsics &K, const StickObs &o)
{
    CameraPose out;
    if (!(K.f > 0) || !o.target.ok || !o.hands.ok || !o.cross.ok) {
        out.reason = "need f > 0 and the target, hands and cross stick lines";
        return out;
    }
    using detail::planeNormal;
    using detail::ray;
    const Vec3 nT = planeNormal(K, o.target), nH = planeNormal(K, o.hands), nC = planeNormal(K, o.cross);
    // ── world X in camera coordinates: ⊥ both parallel sticks' planes ──────────
    Vec3 r1 = nT.cross(nH);
    if (r1.norm() < 1e-6) { out.reason = "the target and hands sticks coincide in the image"; return out; }
    r1 = r1.unit();
    // Sign: a point moving from the ball toward the target end moves in the image
    // along +X's projected direction. The image velocity of a point X_cam under a
    // camera-frame direction d is ∝ (d_x − d_z·x_n, d_y − d_z·y_n) at normalised
    // image position (x_n, y_n).
    {
        const Vec3 b = ray(K, o.ballU, o.ballV), e = ray(K, o.targetEndU, o.targetEndV);
        const double vx = r1.x - r1.z * b.x, vy = r1.y - r1.z * b.y;
        if (vx * (e.x - b.x) + vy * (e.y - b.y) < 0) r1 = r1 * -1.0;
    }
    // ── world Y: ⊥ X and ⊥ the cross stick's plane; world Z = X × Y ───────────
    Vec3 r2 = r1.cross(nC);
    if (r2.norm() < 1e-6) { out.reason = "the cross stick runs along the target line in the image"; return out; }
    r2 = r2.unit();
    Vec3 r3 = r1.cross(r2);
    if (r3.y > 0) { r2 = r2 * -1.0; r3 = r3 * -1.0; }   // up images as −y on an upright camera
    // ── the vertical stick refines the one free angle (rotation about X) ───────
    if (o.vertical.ok) {
        const Vec3 nV = planeNormal(K, o.vertical);
        // r2(φ) = cos φ·a + sin φ·b with a = r2, b = r3 (both ⊥ r1); r3(φ) = r1 × r2(φ)
        // = cos φ·r3 − sin φ·r2. Minimise (nC·r2)² + (nV·r3)² over the unit vector
        // (cos φ, sin φ): the smallest eigenvector of the 2×2 normal matrix.
        const Vec3 a = r2, b = r3;
        const double c1 = nC.dot(a), s1 = nC.dot(b);           // nC·r2(φ)
        const double c2 = nV.dot(b), s2 = -nV.dot(a);          // nV·r3(φ)
        const double M00 = c1 * c1 + c2 * c2, M01 = c1 * s1 + c2 * s2, M11 = s1 * s1 + s2 * s2;
        const double tr = M00 + M11, det = M00 * M11 - M01 * M01;
        const double lam = 0.5 * tr - std::sqrt(std::max(0.0, 0.25 * tr * tr - det));
        double vx = M01, vy = lam - M00;
        if (std::hypot(vx, vy) < 1e-12) { vx = lam - M11; vy = M01; }
        if (std::hypot(vx, vy) < 1e-12) { vx = 1; vy = 0; }
        const double nrm = std::hypot(vx, vy);
        const double cphi = vx / nrm, sphi = vy / nrm;
        Vec3 r2n = a * cphi + b * sphi;
        Vec3 r3n = r1.cross(r2n);
        if (r3n.y > 0) { r2n = r2n * -1.0; r3n = r3n * -1.0; }
        const double rc = std::asin(std::clamp(std::fabs(nC.dot(r2n)), 0.0, 1.0)) * 180.0 / kPi;
        const double rv = std::asin(std::clamp(std::fabs(nV.dot(r3n)), 0.0, 1.0)) * 180.0 / kPi;
        out.rollResidualDeg = std::max(rc, rv);
        r2 = r2n; r3 = r3n;
    }
    out.R = Mat3::fromColumns(r1, r2, r3);
    // ── translation: direction from the ball, scale from the stick ends ────────
    const Vec3 dB = ray(K, o.ballU, o.ballV);
    auto scaleFrom = [&](const Vec3 &Xw, double u, double v, bool &ok) {
        // K(R·Xw + s·dB) ∥ ray(u, v)  ⇒  (q + s·dB) × dE = 0, q = R·Xw
        const Vec3 q = out.R.apply(Xw), dE = ray(K, u, v);
        const Vec3 A = dB.cross(dE), B = q.cross(dE);
        const double den = A.dot(A);
        ok = den > 1e-12;
        return ok ? -B.dot(A) / den : 0.0;
    };
    bool okT = false, okV = false;
    const double sT = scaleFrom({ o.stickLenM, 0, 0 }, o.targetEndU, o.targetEndV, okT);
    double sV = 0;
    if (o.haveVerticalTop) sV = scaleFrom({ 0, 0, o.verticalLenM }, o.verticalTopU, o.verticalTopV, okV);
    if (!okT && !okV) { out.reason = "no scale: the stick end coincides with the ball in the image"; return out; }
    double s = okT ? sT : sV;
    if (okT && okV) { s = 0.5 * (sT + sV); out.scaleResidual = sT / sV - 1.0; }
    if (!(s > 0)) { out.reason = "the ball is behind the camera"; return out; }
    const Vec3 t = dB * s;
    out.C = out.R.applyT(t) * -1.0;
    // ── read-outs ──────────────────────────────────────────────────────────────
    const Vec3 axis = out.R.applyT({ 0, 0, 1 });          // optical axis in the bay
    const Vec3 right = out.R.applyT({ 1, 0, 0 });
    out.yawDeg   = std::atan2(axis.y, axis.x) * 180.0 / kPi;
    out.pitchDeg = -std::asin(std::clamp(axis.z, -1.0, 1.0)) * 180.0 / kPi;
    // Level image-right for this axis: axis × Z, normalised; roll is the signed angle
    // from it to the actual image-right, about the axis.
    const Vec3 levelRight = axis.cross(Vec3 { 0, 0, 1 }).unit();
    const Vec3 levelDown  = axis.cross(levelRight).unit();
    out.rollDeg = std::atan2(right.dot(levelDown), right.dot(levelRight)) * 180.0 / kPi;
    out.ok = true;
    return out;
}

// ── the DTL camera relative to the face-on one, in the FUSION frame ──────────
// shaft_fusion.h works in the face-on camera's level frame: X = face-on image-right
// levelled, Y = the face-on view ray levelled, Z up. Its DTL camera is
// dtlCamera(yaw, pitch, roll): view ray +X turned by yaw toward +Y, dipped by pitch,
// rolled about the axis. This maps two solved bay poses onto those numbers, and the
// offset is the DTL centre relative to the face-on centre in the same frame.
struct RelativeDtl {
    bool   ok = false;
    std::string reason;
    double yawDeg = 0, pitchDeg = 0, rollDeg = 0;
    Vec3   offsetM;                 // DTL centre − face-on centre, fusion frame
    double interCameraDeg = 0;      // angle between the two level view rays
    double faceOnPitchDeg = 0;      // what the fusion frame ignores: the face-on dip
    double faceOnRollDeg = 0;       // … and its roll
};

inline RelativeDtl dtlRelativeToFaceOn(const CameraPose &fo, const CameraPose &dtl)
{
    RelativeDtl r;
    if (!fo.ok || !dtl.ok) { r.reason = "both poses must be solved"; return r; }
    const Vec3 Z { 0, 0, 1 };
    const Vec3 axF = fo.R.applyT({ 0, 0, 1 });
    Vec3 yF { axF.x, axF.y, 0 };
    if (yF.norm() < 1e-9) { r.reason = "the face-on camera looks straight down"; return r; }
    yF = yF.unit();
    const Vec3 xF = yF.cross(Z).unit();        // level image-right of a camera looking along yF
    auto toFus = [&](const Vec3 &v) { return Vec3 { v.dot(xF), v.dot(yF), v.dot(Z) }; };
    const Vec3 axD = toFus(dtl.R.applyT({ 0, 0, 1 }));
    const Vec3 rtD = toFus(dtl.R.applyT({ 1, 0, 0 }));
    r.yawDeg   = std::atan2(axD.y, axD.x) * 180.0 / kPi;
    r.pitchDeg = -std::asin(std::clamp(axD.z, -1.0, 1.0)) * 180.0 / kPi;
    // The model's un-rolled image-right for this axis, then the signed roll to the
    // measured one — the same construction fusion::dtlCamera undoes.
    const fusion::Camera model = fusion::dtlCamera(r.yawDeg, r.pitchDeg, 0.0);
    r.rollDeg = std::atan2(rtD.dot(model.down), rtD.dot(model.right)) * 180.0 / kPi;
    r.offsetM = toFus(dtl.C - fo.C);
    const Vec3 lD { axD.x, axD.y, 0 };
    r.interCameraDeg = std::acos(std::clamp(lD.unit().dot({ 0, 1, 0 }), -1.0, 1.0)) * 180.0 / kPi;
    r.faceOnPitchDeg = fo.pitchDeg;
    r.faceOnRollDeg  = fo.rollDeg;
    r.ok = true;
    return r;
}

} // namespace pinpoint::analysis::calib
