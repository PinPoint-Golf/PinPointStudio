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

// The jointed Y-bot the skeleton3d fit solves for, and the 3-D swing view draws — ONE
// definition of its degrees of freedom and ONE forward kinematics, shared by both
// (docs/design/swing_3d_viz_design.md §3.2, §4). Plain C++: no Qt, no Eigen, so the GUI
// driver can rebuild bone rotations from the persisted DoFs without pulling the solver in.
//
// ── Frames ────────────────────────────────────────────────────────────────────
// RIG frame (ybot_rig.h): glTF — +X the model's LEFT, +Y up, +Z the model's FRONT.
// WORLD frame (the fit's): the analysis frame club3d uses — +X face-on image-right,
// +Y the face-on view ray (away from that camera), +Z up. The root's rotation carries
// rig → world; at zero it is kRigToWorld, a golfer FACING the face-on camera (their
// front −Y, their left image-right = +X).
//
// ── Degrees of freedom ────────────────────────────────────────────────────────
// Every DoF is a scalar: a root translation along a world axis, or a rotation about an
// axis. That makes each one's effect on any point EXACT and cheap — a rotation about a
// world axis a through pivot o moves p at  a × (p − o)  per radian — which is the whole
// Jacobian the solver needs (skeleton3d_fit.cpp), and makes limits and smoothness
// plain scalar terms.
//
//   joint local rotation = restQ · N · Π_k Rot(axis_k, θ_k)
//
// N is a NEUTRAL pre-rotation so θ = 0 is a relaxed standing pose, not the T-pose
// (only the upper arms have one: hanging, 75° down). Axes are defined in the pose where
// they mean what their names say, from a bone direction b and the direction the named
// motion carries the bone, f:  axis = b × f, so +θ IS flexion / abduction / elevation…
// — that is what lets the knee and elbow limits be one-sided and right.
//
// Knees and elbows are HINGES (one DoF). That is an anatomical constraint doing work:
// with the child's direction fixed by the cameras, a hinge fixes the parent's roll.

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ybot_rig.h"

namespace pinpoint::skeleton3d {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

// ── minimal vector / quaternion ──────────────────────────────────────────────
struct V3 {
    double x = 0, y = 0, z = 0;
    V3 operator+(const V3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator-(const V3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator*(double s) const { return { x * s, y * s, z * s }; }
    V3 operator-() const { return { -x, -y, -z }; }
    V3 &operator+=(const V3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    double dot(const V3 &o) const { return x * o.x + y * o.y + z * o.z; }
    V3 cross(const V3 &o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    double norm() const { return std::sqrt(dot(*this)); }
    V3 unit() const { const double n = norm(); return n > 0 ? (*this) * (1.0 / n) : *this; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};

struct Q {
    double w = 1, x = 0, y = 0, z = 0;
    Q operator*(const Q &o) const
    {
        return { w * o.w - x * o.x - y * o.y - z * o.z,
                 w * o.x + x * o.w + y * o.z - z * o.y,
                 w * o.y - x * o.z + y * o.w + z * o.x,
                 w * o.z + x * o.y - y * o.x + z * o.w };
    }
    Q conj() const { return { w, -x, -y, -z }; }
    Q normalized() const
    {
        const double n = std::sqrt(w * w + x * x + y * y + z * z);
        return n > 0 ? Q { w / n, x / n, y / n, z / n } : Q {};
    }
    V3 rotate(const V3 &v) const
    {
        // v' = v + 2w(q×v) + 2 q×(q×v)
        const V3 q { x, y, z };
        const V3 t = q.cross(v) * 2.0;
        return v + t * w + q.cross(t);
    }
    static Q axisAngle(const V3 &axis, double ang)
    {
        const V3 a = axis.unit();
        const double s = std::sin(0.5 * ang);
        return { std::cos(0.5 * ang), a.x * s, a.y * s, a.z * s };
    }
    // Rotation angle of this quaternion, [0, π].
    double angle() const
    {
        const double v = std::sqrt(x * x + y * y + z * z);
        return 2.0 * std::atan2(v, std::fabs(w));
    }
};

// ── length groups: one scale each, L = R ─────────────────────────────────────
enum LengthGroup : int {
    GPelvis = 0, GSpine, GHead, GClavicle, GUpperArm, GForearm, GHand, GThigh, GShank, GFoot,
    GroupCount
};
inline const char *groupName(int g)
{
    static const char *n[GroupCount] = { "pelvis", "spine", "head", "clavicle", "upperArm",
                                         "forearm", "hand", "thigh", "shank", "foot" };
    return (g >= 0 && g < GroupCount) ? n[g] : "?";
}

// Which group scales joint j's offset from its parent (−1 = the root: none).
inline int jointGroup(int j)
{
    using namespace ybot;
    switch (j) {
    case Hips: return -1;
    case Spine: case Spine1: case Spine2: case Neck: return GSpine;
    case Head: case HeadTop_End: return GHead;
    case LeftShoulder: case RightShoulder: case LeftArm: case RightArm: return GClavicle;
    case LeftForeArm: case RightForeArm: return GUpperArm;
    case LeftHand: case RightHand: return GForearm;
    case LeftHandMiddle1: case RightHandMiddle1: return GHand;
    case LeftUpLeg: case RightUpLeg: return GPelvis;
    case LeftLeg: case RightLeg: return GThigh;
    case LeftFoot: case RightFoot: return GShank;
    default: return GFoot;   // ToeBase, Toe_End
    }
}

// ── DoFs ─────────────────────────────────────────────────────────────────────
enum class DofKind : uint8_t { RootTrans, RootRot, JointRot };

struct Dof {
    int      joint = 0;
    DofKind  kind = DofKind::JointRot;
    V3       axis;              // RootTrans/RootRot: world axis; JointRot: joint-LOCAL axis
    double   lo = -kPi, hi = kPi;   // soft limits (rad or m); lo ≥ hi = none
    const char *name = "";
};

// COCO-WholeBody body + feet markers the fit reads (0–16 body, 17–22 feet).
constexpr int kMarkerCount = 23;

struct Marker {
    int  joint = 0;             // the segment the marker rides on
    V3   offsetPrior;           // joint-LOCAL offset prior (m, at unit scale)
    double sigmaM = 0.02;       // prior σ on the offset
    bool fitOffset = false;     // a shared unknown, or held at the prior
    int  mirror = -1;           // the other side's marker (label swaps), −1 = none
    // A SYMMETRIC fitted offset (0 = the hip keypoints, 1 = the shoulder keypoints, −1 = none):
    // the pair moves outward/up together by two shared unknowns, side·lat·latLocal + up·upLocal.
    // A symmetric shift is not a rotation of the segment, so it cannot reopen the gauge that
    // fitting free per-marker offsets did.
    int  symGroup = -1;
    // Feet only: the marker's height above the floor with the foot flat (rest pose, unit scale).
    // Planted-foot contact holds the marker at THIS height, not at the floor — a heel marker sits
    // ~1 cm above the toe markers, and forcing all three to the floor tilted the world.
    double floorHeight = 0;
    double side = 0;            // +1 left, −1 right
    V3   latLocal, upLocal;     // joint-local lateral (toward the model's left) and up axes
};
constexpr int kSymGroups = 2;

// The model's rest data, derived once from ybot_rig.h.
struct Rig {
    static constexpr int N = ybot::JointCount;
    std::array<int, N>  parent {};
    std::array<V3, N>   restT {};
    std::array<Q, N>    restQ {};
    std::array<Q, N>    neutral {};      // N_j (identity except the upper arms)
    std::array<int, N>  firstDof {};     // index of the joint's first DoF, −1 = none
    std::array<int, N>  nDof {};
    std::array<uint64_t, N> ancestorsOrSelf {};   // bit j set ⇒ joint j is on the chain
    std::vector<Dof>    dofs;
    std::array<Marker, kMarkerCount> markers {};
    Q   rigToWorld;                      // kRigToWorld
    double restHeadTopY = 0;             // rest-pose height to the head-top end joint (m)
    double restEyeY = 0, restAnkleY = 0; // rest eye / ankle heights (m) — the ruler span
    // Hand-local directions of the palm normal and the thumb (T-pose: palm down, thumb
    // forward), [0] = left hand, [1] = right hand — the grip priors are authored in them.
    std::array<V3, 2> palmLocal {}, thumbLocal {};

    int dofCount() const { return int(dofs.size()); }
};

// Rig → world for a golfer facing the face-on camera: rig X (left) → world +X,
// rig Y (up) → world +Z, rig Z (front) → world −Y. A proper rotation (−90° about X).
inline Q rigToWorldQ() { return Q::axisAngle({ 1, 0, 0 }, kPi / 2.0); }

// ── forward kinematics ───────────────────────────────────────────────────────
struct Pose {
    std::array<V3, Rig::N> pos {};       // world joint positions
    std::array<Q, Rig::N>  rot {};       // world joint rotations (rig-local → world)
    std::array<Q, Rig::N>  local {};     // parent-local rotations restQ·N·ΠRot (root: world)
    // Per DoF: its world axis and pivot at this pose (the exact Jacobian ingredients).
    std::vector<V3> dofAxisW, dofPivotW;
};

// θ: one value per DoF. scale: one per LengthGroup (1 = Y-bot).
inline void forwardKinematics(const Rig &rig, const double *theta, const double *scale, Pose &out)
{
    const int nd = rig.dofCount();
    out.dofAxisW.resize(size_t(nd));
    out.dofPivotW.resize(size_t(nd));
    for (int j = 0; j < Rig::N; ++j) {
        const int p = rig.parent[j];
        if (p < 0) {
            // Root: translation DoFs place the joint; rotation DoFs are world-axis, applied
            // outermost first, then rig → world and the rest rotation.
            V3 t;
            Q  r;
            for (int k = rig.firstDof[j]; k < rig.firstDof[j] + rig.nDof[j]; ++k) {
                const Dof &d = rig.dofs[size_t(k)];
                if (d.kind == DofKind::RootTrans) t += d.axis * theta[k];
            }
            out.pos[j] = t;
            for (int k = rig.firstDof[j]; k < rig.firstDof[j] + rig.nDof[j]; ++k) {
                const Dof &d = rig.dofs[size_t(k)];
                if (d.kind == DofKind::RootTrans) {
                    out.dofAxisW[size_t(k)] = d.axis;
                    out.dofPivotW[size_t(k)] = t;
                } else {
                    out.dofAxisW[size_t(k)] = r.rotate(d.axis);
                    out.dofPivotW[size_t(k)] = t;
                    r = r * Q::axisAngle(d.axis, theta[k]);
                }
            }
            out.local[j] = (r * rig.rigToWorld * rig.restQ[j] * rig.neutral[j]).normalized();
            out.rot[j] = out.local[j];
            continue;
        }
        const int g = jointGroup(j);
        const double s = g >= 0 ? scale[g] : 1.0;
        out.pos[j] = out.pos[p] + out.rot[p].rotate(rig.restT[j] * s);
        Q local = rig.restQ[j] * rig.neutral[j];
        Q w = out.rot[p] * local;
        for (int k = rig.firstDof[j]; k < rig.firstDof[j] + rig.nDof[j]; ++k) {
            const Dof &d = rig.dofs[size_t(k)];
            out.dofAxisW[size_t(k)] = w.rotate(d.axis);
            out.dofPivotW[size_t(k)] = out.pos[j];
            const Q r = Q::axisAngle(d.axis, theta[k]);
            local = local * r;
            w = w * r;
        }
        out.local[j] = local.normalized();
        out.rot[j] = w.normalized();
    }
}

// World position of a marker at a pose: the joint plus its local offset.
inline V3 markerWorld(const Pose &p, int joint, const V3 &offsetLocal)
{
    return p.pos[joint] + p.rot[joint].rotate(offsetLocal);
}

// ── rig construction ─────────────────────────────────────────────────────────
inline Rig buildRig()
{
    using namespace ybot;
    Rig rig;
    for (int j = 0; j < Rig::N; ++j) {
        const RigJoint &J = kJoints[j];
        rig.parent[j] = J.parent;
        rig.restT[j] = { J.restT[0], J.restT[1], J.restT[2] };
        rig.restQ[j] = Q { J.restQ[0], J.restQ[1], J.restQ[2], J.restQ[3] }.normalized();
        rig.neutral[j] = Q {};
        rig.ancestorsOrSelf[j] = (uint64_t(1) << j) | (J.parent >= 0 ? rig.ancestorsOrSelf[J.parent] : 0);
    }
    rig.rigToWorld = rigToWorldQ();

    // Rest world rotations in the RIG frame (no rig→world), T-pose.
    std::array<Q, Rig::N> restW {};
    std::array<V3, Rig::N> restP {};
    for (int j = 0; j < Rig::N; ++j) {
        const int p = rig.parent[j];
        restW[j] = p < 0 ? rig.restQ[j] : (restW[p] * rig.restQ[j]).normalized();
        restP[j] = p < 0 ? rig.restT[j] : restP[p] + restW[p].rotate(rig.restT[j]);
    }
    rig.restHeadTopY = restP[HeadTop_End].y;
    rig.restAnkleY = 0.5 * (restP[LeftFoot].y + restP[RightFoot].y);

    // Upper-arm neutral: hanging 75° down, about the rig's front axis.
    for (int side = 0; side < 2; ++side) {
        const int arm = side == 0 ? LeftArm : RightArm;
        const double L = side == 0 ? 1.0 : -1.0;       // left arm points +X in the T-pose
        const V3 frontLocal = restW[arm].conj().rotate({ 0, 0, 1 });
        rig.neutral[arm] = Q::axisAngle(frontLocal, -L * 75.0 * kDeg);
    }
    // The frame each joint's own DoFs (and marker offsets) are authored in: the T-pose
    // chain with the joint's OWN neutral only. A joint's local axes do not depend on its
    // parents' pose, so authoring below the upper arm in the T-pose is exact — and it is
    // where "palm down, thumb forward" is true. (Authoring the hand after the arm's
    // neutral would cross its bone with a world axis it nearly lies along.)
    std::array<Q, Rig::N> neutW {};
    for (int j = 0; j < Rig::N; ++j) neutW[j] = (restW[j] * rig.neutral[j]).normalized();

    auto addJoint = [&](int j, std::initializer_list<std::pair<V3, std::pair<double, double>>> worldAxes,
                        std::initializer_list<const char *> names) {
        rig.firstDof[j] = rig.dofCount();
        auto nm = names.begin();
        for (const auto &wa : worldAxes) {
            Dof d;
            d.joint = j;
            d.kind = DofKind::JointRot;
            d.axis = neutW[j].conj().rotate(wa.first.unit());     // world(rig) → joint-local
            d.lo = wa.second.first * kDeg;
            d.hi = wa.second.second * kDeg;
            d.name = *nm++;
            rig.dofs.push_back(d);
        }
        rig.nDof[j] = rig.dofCount() - rig.firstDof[j];
    };
    auto boneDir = [&](int j) { return neutW[j].rotate({ 0, 1, 0 }); };
    const V3 X { 1, 0, 0 }, Y { 0, 1, 0 }, Z { 0, 0, 1 };

    for (int j = 0; j < Rig::N; ++j) { rig.firstDof[j] = -1; rig.nDof[j] = 0; }

    // Root: translation (world X, Y, Z), then rotation — yaw about world Z, then pitch,
    // roll. Translation is unbounded; pitch/roll are soft-limited (a golfer does not lie
    // down), yaw is free (the finish turns past 90°).
    rig.firstDof[Hips] = 0;
    const char *tn[3] = { "root.x", "root.y", "root.z" };
    for (int a = 0; a < 3; ++a)
        rig.dofs.push_back({ Hips, DofKind::RootTrans, a == 0 ? X : a == 1 ? Y : Z, 1, -1, tn[a] });
    rig.dofs.push_back({ Hips, DofKind::RootRot, Z, 1, -1, "root.yaw" });
    rig.dofs.push_back({ Hips, DofKind::RootRot, X, -60 * kDeg, 60 * kDeg, "root.pitch" });
    rig.dofs.push_back({ Hips, DofKind::RootRot, Y, -45 * kDeg, 45 * kDeg, "root.roll" });
    rig.nDof[Hips] = 6;

    // Spine segments, and the head: flexion (forward), lateral bend (to the left), twist.
    for (int j : { Spine, Spine1, Spine2 })
        addJoint(j, { { Y.cross(Z), { -20, 30 } }, { Y.cross(X), { -20, 20 } }, { Y, { -25, 25 } } },
                 { j == Spine ? "spine.flex" : j == Spine1 ? "spine1.flex" : "spine2.flex",
                   j == Spine ? "spine.lat" : j == Spine1 ? "spine1.lat" : "spine2.lat",
                   j == Spine ? "spine.twist" : j == Spine1 ? "spine1.twist" : "spine2.twist" });
    // The head turns on the NECK's base (the Y-bot's Neck joint), not at its Head joint 10 cm
    // higher: address is a flexed NECK, and pivoting at the skull base made the corpus fit shrink
    // the head to a fifth of its size to get the face down to the ball.
    addJoint(Neck, { { Y.cross(Z), { -50, 60 } }, { Y.cross(X), { -40, 40 } }, { Y, { -75, 75 } } },
             { "head.flex", "head.lat", "head.twist" });

    for (int side = 0; side < 2; ++side) {
        const double L = side == 0 ? 1.0 : -1.0;
        const V3 out { L, 0, 0 };
        const bool lf = side == 0;
        const int sh = lf ? LeftShoulder : RightShoulder, arm = lf ? LeftArm : RightArm,
                  fa = lf ? LeftForeArm : RightForeArm, hd = lf ? LeftHand : RightHand,
                  ul = lf ? LeftUpLeg : RightUpLeg, lg = lf ? LeftLeg : RightLeg,
                  ft = lf ? LeftFoot : RightFoot;
        // Clavicle: elevation, protraction.
        addJoint(sh, { { boneDir(sh).cross(Y), { -15, 40 } }, { boneDir(sh).cross(Z), { -25, 35 } } },
                 { lf ? "lClav.elev" : "rClav.elev", lf ? "lClav.prot" : "rClav.prot" });
        // Shoulder (glenohumeral), from the hanging neutral: flexion (forward), abduction
        // (outward), rotation about the humerus.
        addJoint(arm, { { boneDir(arm).cross(Z), { -60, 180 } }, { boneDir(arm).cross(out), { -60, 170 } },
                        { boneDir(arm), { -100, 100 } } },
                 { lf ? "lArm.flex" : "rArm.flex", lf ? "lArm.abd" : "rArm.abd", lf ? "lArm.rot" : "rArm.rot" });
        // Elbow: a HINGE (flexion carries the forearm forward in the T-pose), then the
        // forearm's own pronation about its long axis.
        addJoint(fa, { { boneDir(fa).cross(Z), { -5, 150 } }, { boneDir(fa), { -110, 110 } } },
                 { lf ? "lElbow.flex" : "rElbow.flex", lf ? "lForearm.pron" : "rForearm.pron" });
        // Wrist: flexion (toward the palm, which faces down in the T-pose), radial deviation
        // (toward the thumb, forward in the T-pose).
        addJoint(hd, { { boneDir(hd).cross(-Y), { -80, 80 } }, { boneDir(hd).cross(Z), { -35, 45 } } },
                 { lf ? "lWrist.flex" : "rWrist.flex", lf ? "lWrist.rad" : "rWrist.rad" });
        // Hip: flexion, abduction, rotation.
        addJoint(ul, { { boneDir(ul).cross(Z), { -30, 130 } }, { boneDir(ul).cross(out), { -30, 50 } },
                       { boneDir(ul), { -50, 50 } } },
                 { lf ? "lHip.flex" : "rHip.flex", lf ? "lHip.abd" : "rHip.abd", lf ? "lHip.rot" : "rHip.rot" });
        // Knee: a HINGE; flexion carries the shank backward.
        addJoint(lg, { { boneDir(lg).cross(-Z), { -5, 150 } } }, { lf ? "lKnee.flex" : "rKnee.flex" });
        // Ankle: dorsiflexion (toes up), inversion about the front axis.
        addJoint(ft, { { boneDir(ft).cross(Y), { -50, 35 } }, { Z, { -35, 35 } } },
                 { lf ? "lAnkle.dorsi" : "rAnkle.dorsi", lf ? "lAnkle.inv" : "rAnkle.inv" });
    }

    // ── markers: COCO body 0–16 + feet 17–22 ──
    // Offsets are authored in the RIG frame at rest (relative to the joint) and turned
    // joint-local here. Joint-centre keypoints sit on the joint; the face and feet are
    // SURFACE points at an anatomical offset.
    // ⚠ The offsets are FIXED priors, not fitted. Fitting them (the first build did) hands
    // the solve a gauge: a constant offset shift on a rigid segment's markers is the same
    // image as a constant rotation of that segment, so the head tilted 30°, the lead foot
    // pitched 46° and the pelvis turned 30° on a synthetic swing whose reprojection was
    // perfect. A wrong prior is now a constant, visible bias instead of a silent gauge.
    auto mk = [&](int m, int joint, V3 restOffsetRig, double sigma, bool fit, int mirror) {
        Marker &M = rig.markers[size_t(m)];
        M.joint = joint;
        M.offsetPrior = neutW[joint].conj().rotate(restOffsetRig);
        M.sigmaM = sigma;
        M.fitOffset = fit;
        M.mirror = mirror;
    };
    mk(0, Head, { 0, 0.06, 0.11 }, 0.02, false, -1);                 // nose
    mk(1, Head, { 0.032, 0.085, 0.085 }, 0.02, false, 2);            // L eye
    mk(2, Head, { -0.032, 0.085, 0.085 }, 0.02, false, 1);           // R eye
    mk(3, Head, { 0.075, 0.06, 0.0 }, 0.02, false, 4);               // L ear
    mk(4, Head, { -0.075, 0.06, 0.0 }, 0.02, false, 3);              // R ear
    // COCO shoulders are the ACROMION (above and a little outside the glenohumeral joint), and
    // COCO hips the TROCHANTERS (outside the hip joint centre): surface points that ride the
    // clavicle and the pelvis, not the humerus or the femur. Each pair also carries the
    // symmetric fitted offset above.
    auto sym = [&](int m, int group, double side) {
        Marker &M = rig.markers[size_t(m)];
        M.symGroup = group;
        M.side = side;
        M.latLocal = neutW[M.joint].conj().rotate({ 1, 0, 0 });
        M.upLocal = neutW[M.joint].conj().rotate({ 0, 1, 0 });
    };
    mk(5, LeftShoulder, restP[LeftArm] - restP[LeftShoulder] + V3 { 0.01, 0.03, 0 }, 0.02, false, 6);
    mk(6, RightShoulder, restP[RightArm] - restP[RightShoulder] + V3 { -0.01, 0.03, 0 }, 0.02, false, 5);
    sym(5, 1, 1.0); sym(6, 1, -1.0);
    mk(7, LeftForeArm, {}, 0.02, false, 8); mk(8, RightForeArm, {}, 0.02, false, 7);
    mk(9, LeftHand, {}, 0.02, false, 10);  mk(10, RightHand, {}, 0.02, false, 9);
    mk(11, Hips, restP[LeftUpLeg] - restP[Hips] + V3 { 0.03, -0.02, 0 }, 0.03, false, 12);
    mk(12, Hips, restP[RightUpLeg] - restP[Hips] + V3 { -0.03, -0.02, 0 }, 0.03, false, 11);
    sym(11, 0, 1.0); sym(12, 0, -1.0);
    mk(13, LeftLeg, {}, 0.02, false, 14);  mk(14, RightLeg, {}, 0.02, false, 13);
    mk(15, LeftFoot, {}, 0.02, false, 16); mk(16, RightFoot, {}, 0.02, false, 15);
    for (int side = 0; side < 2; ++side) {
        const bool lf = side == 0;
        const double L = lf ? 1.0 : -1.0;
        const int ft = lf ? LeftFoot : RightFoot, toe = lf ? LeftToeBase : RightToeBase;
        const V3 toeRel = restP[toe] - restP[ft];
        const int base = lf ? 17 : 20, other = lf ? 20 : 17;
        mk(base + 0, ft, toeRel + V3 { -L * 0.02, -0.02, 0.06 }, 0.02, false, other + 0);   // big toe
        mk(base + 1, ft, toeRel + V3 { L * 0.035, -0.02, 0.02 }, 0.02, false, other + 1);   // small toe
        mk(base + 2, ft, V3 { 0, -0.08, -0.05 }, 0.02, false, other + 2);                   // heel
        for (int k = 0; k < 3; ++k) {
            Marker &M = rig.markers[size_t(base + k)];
            M.floorHeight = (restP[ft] + neutW[ft].rotate(M.offsetPrior)).y;
        }
    }
    rig.restEyeY = restP[Head].y + 0.085;
    for (int side = 0; side < 2; ++side) {
        const int hd = side == 0 ? LeftHand : RightHand;
        rig.palmLocal[size_t(side)]  = neutW[hd].conj().rotate({ 0, -1, 0 });
        rig.thumbLocal[size_t(side)] = neutW[hd].conj().rotate({ 0, 0, 1 });
    }
    return rig;
}

// The rig's single shared instance.
inline const Rig &rig()
{
    static const Rig r = buildRig();
    return r;
}

} // namespace pinpoint::skeleton3d
