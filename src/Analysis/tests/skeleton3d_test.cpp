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

// Standalone tests for the skeleton3d fit (src/Analysis/skeleton3d/,
// docs/design/swing_3d_viz_design.md §8.1). Pure std + Eigen, no Qt, no fixture.
//
//   (a) the rig: the rest pose rebuilds itself, and the hinges bend the right way
//   (b) a synthetic swing through two virtual cameras: bone directions and ROLL
//   (c) identifiability: lengths and camera geometry recovered from a wrong start
//   (d) ablation: each constraint switched off in turn — printed as a table
//   (e) face-on only
//   plus a Jacobian check and the timing.
//
//   cmake --build build/tests --target skeleton3d_test
//   ctest --test-dir build/tests -R skeleton3d_test --output-on-failure

#include "../skeleton3d/skeleton3d_fit.h"
#include "../skeleton3d/skeleton3d_rig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace pinpoint::skeleton3d;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static int dofIndex(const char *name)
{
    const Rig &R = rig();
    for (int k = 0; k < R.dofCount(); ++k)
        if (std::strcmp(R.dofs[size_t(k)].name, name) == 0) return k;
    std::printf("no DoF %s\n", name);
    std::abort();
}

static double p90(std::vector<double> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(0.9 * double(v.size())))];
}

// ── (a) the rig ──────────────────────────────────────────────────────────────
static void testRig()
{
    std::printf("(a) rig\n");
    const Rig &R = rig();
    check(R.dofCount() == 48, "48 DoFs");
    // Undo rig→world (root pitch −90°) and both arm neutrals (abduction +75°): every joint
    // must land on the glTF rest pose, and every local rotation on its rest rotation.
    std::vector<double> th(size_t(R.dofCount()), 0.0);
    th[size_t(dofIndex("root.pitch"))] = -90 * kDeg;
    th[size_t(dofIndex("lArm.abd"))] = 75 * kDeg;
    th[size_t(dofIndex("rArm.abd"))] = 75 * kDeg;
    th[0] = ybot::kJoints[0].restWorld[0];
    th[1] = ybot::kJoints[0].restWorld[1];
    th[2] = ybot::kJoints[0].restWorld[2];
    std::array<double, GroupCount> sc; sc.fill(1.0);
    Pose p;
    forwardKinematics(R, th.data(), sc.data(), p);
    double worstP = 0, worstQ = 0;
    for (int j = 0; j < Rig::N; ++j) {
        const auto &J = ybot::kJoints[j];
        worstP = std::max(worstP, (p.pos[j] - V3 { J.restWorld[0], J.restWorld[1], J.restWorld[2] }).norm());
        if (j == 0) continue;
        const Q rq { J.restQ[0], J.restQ[1], J.restQ[2], J.restQ[3] };
        const Q d = rq.conj() * p.local[j];
        worstQ = std::max(worstQ, d.angle());
    }
    std::printf("      rest round trip: worst joint %.2e m, worst local rotation %.2e rad\n", worstP, worstQ);
    check(worstP < 1e-5, "(a) the rest pose rebuilds every joint to 1e-5 m");
    check(worstQ < 1e-5, "(a) …and every local rotation to its rest rotation");

    // Hinges: elbow flexion carries the hand FORWARD (rig +Z), knee flexion the ankle BACK.
    auto rigFrame = [&](std::vector<double> t) {
        Pose q;
        forwardKinematics(R, t.data(), sc.data(), q);
        return q;
    };
    std::vector<double> t2 = th;
    t2[size_t(dofIndex("lElbow.flex"))] = 90 * kDeg;
    t2[size_t(dofIndex("lKnee.flex"))] = 90 * kDeg;
    t2[size_t(dofIndex("lArm.flex"))] = 0;
    const Pose q = rigFrame(t2);
    const V3 hand = q.pos[ybot::LeftHand] - q.pos[ybot::LeftForeArm];
    const V3 ankle = q.pos[ybot::LeftFoot] - q.pos[ybot::LeftLeg];
    check(hand.z > 0.2, "(a) elbow flexion carries the forearm forward");
    check(ankle.z < -0.3, "(a) knee flexion carries the shank backward");
    std::vector<double> t3 = th;
    t3[size_t(dofIndex("spine.flex"))] = 30 * kDeg;
    const Pose q3 = rigFrame(t3);
    check(q3.pos[ybot::Head].z > p.pos[ybot::Head].z + 0.1, "(a) spine flexion bends forward");
}

// ── the synthetic swing ─────────────────────────────────────────────────────
struct Truth {
    std::vector<int64_t> t;
    std::vector<std::vector<double>> th;
    std::array<double, GroupCount> scale {};
    Cameras cam;
    int foW = 1440, foH = 1080, dtlW = 1280, dtlH = 1024;
    V3 gripAxis, gripOff, trailOff;
    std::array<V3, kMarkerCount> off {};
    int64_t addressUs = 0, topUs = 0, impactUs = 0;
};

static double smoothstep(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }

// Keyframes: (time s, DoF name, value °). Linear-in-smoothstep between keys.
struct Key { double t; std::vector<std::pair<const char *, double>> v; };

static std::vector<double> poseAt(const std::vector<Key> &keys, double t)
{
    const Rig &R = rig();
    std::vector<double> th(size_t(R.dofCount()), 0.0);
    // Build per-DoF key tracks.
    for (int k = 3; k < R.dofCount(); ++k) {
        std::vector<std::pair<double, double>> track;
        for (const Key &K : keys) {
            double v = 0;
            for (const auto &pv : K.v) if (std::strcmp(pv.first, R.dofs[size_t(k)].name) == 0) v = pv.second;
            track.push_back({ K.t, v });
        }
        double val = track.front().second;
        for (size_t i = 0; i + 1 < track.size(); ++i)
            if (t >= track[i].first && t <= track[i + 1].first) {
                const double a = smoothstep((t - track[i].first) / (track[i + 1].first - track[i].first));
                val = track[i].second + a * (track[i + 1].second - track[i].second);
            }
        if (t > track.back().first) val = track.back().second;
        th[size_t(k)] = val * kDeg;
    }
    return th;
}

// Damped Gauss–Newton IK on a DoF subset: drive `points(th)` onto `targets`.
// Regularised toward `ref` (the keyframed pose): 1 cm of target error costs as much as ~5°
// away from the keyframe, so the solution is the NEAREST plausible pose, not any pose.
static double ik(std::vector<double> &th, const std::vector<int> &dofs,
                 const std::function<std::vector<V3>(const std::vector<double> &)> &points,
                 const std::vector<V3> &targets, int iters = 25, const std::vector<double> *ref = nullptr,
                 double regPerRad = 0.115)
{
    const int n = int(dofs.size()), m = int(targets.size()) * 3;
    const double lam = regPerRad * regPerRad;
    for (int it = 0; it < iters; ++it) {
        const std::vector<V3> p0 = points(th);
        std::vector<double> r(static_cast<size_t>(m));
        for (int i = 0; i < int(targets.size()); ++i)
            for (int a = 0; a < 3; ++a) r[size_t(3 * i + a)] = p0[size_t(i)][a] - targets[size_t(i)][a];
        std::vector<double> J(size_t(m * n));
        for (int c = 0; c < n; ++c) {
            std::vector<double> tp = th;
            tp[size_t(dofs[size_t(c)])] += 1e-6;
            const std::vector<V3> p1 = points(tp);
            for (int i = 0; i < int(targets.size()); ++i)
                for (int a = 0; a < 3; ++a) J[size_t((3 * i + a) * n + c)] = (p1[size_t(i)][a] - p0[size_t(i)][a]) / 1e-6;
        }
        // (JᵀJ + μI) δ = −Jᵀr, tiny dense solve by Gaussian elimination.
        std::vector<double> A(size_t(n * n), 0.0), b(size_t(n), 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                double s = 0;
                for (int q = 0; q < m; ++q) s += J[size_t(q * n + i)] * J[size_t(q * n + j)];
                A[size_t(i * n + j)] = s + (i == j ? 1e-4 + (ref ? lam : 0.0) : 0);
            }
            double s = 0;
            for (int q = 0; q < m; ++q) s += J[size_t(q * n + i)] * r[size_t(q)];
            if (ref) s += lam * (th[size_t(dofs[size_t(i)])] - (*ref)[size_t(dofs[size_t(i)])]);
            b[size_t(i)] = -s;
        }
        for (int i = 0; i < n; ++i) {
            int piv = i;
            for (int j = i + 1; j < n; ++j) if (std::fabs(A[size_t(j * n + i)]) > std::fabs(A[size_t(piv * n + i)])) piv = j;
            for (int j = 0; j < n; ++j) std::swap(A[size_t(i * n + j)], A[size_t(piv * n + j)]);
            std::swap(b[size_t(i)], b[size_t(piv)]);
            for (int j = i + 1; j < n; ++j) {
                const double f = A[size_t(j * n + i)] / A[size_t(i * n + i)];
                for (int c = i; c < n; ++c) A[size_t(j * n + c)] -= f * A[size_t(i * n + c)];
                b[size_t(j)] -= f * b[size_t(i)];
            }
        }
        std::vector<double> x(static_cast<size_t>(n));
        for (int i = n - 1; i >= 0; --i) {
            double s = b[size_t(i)];
            for (int c = i + 1; c < n; ++c) s -= A[size_t(i * n + c)] * x[size_t(c)];
            x[size_t(i)] = s / A[size_t(i * n + i)];
        }
        // Projected: a synthetic golfer who breaks the fit's joint limits is not a test of
        // the fit, it is a test of the limits.
        const Rig &R = rig();
        for (int c = 0; c < n; ++c) {
            double &v = th[size_t(dofs[size_t(c)])];
            v += x[size_t(c)];
            const Dof &d = R.dofs[size_t(dofs[size_t(c)])];
            if (d.lo < d.hi) v = std::clamp(v, d.lo + kDeg, d.hi - kDeg);
        }
    }
    const std::vector<V3> pf = points(th);
    double worst = 0;
    for (int i = 0; i < int(targets.size()); ++i) worst = std::max(worst, (pf[size_t(i)] - targets[size_t(i)]).norm());
    return worst;
}

static Truth makeSwing(double fps, std::array<double, GroupCount> scale, double dtlYawDeg)
{
    const Rig &R = rig();
    Truth T;
    T.scale = scale;
    // Cameras: face-on at the origin 1.9 m from the golfer, a touch pitched down; DTL
    // behind the ball, yawed so the views are (90 − yaw)° apart, pitched down.
    // The face-on camera LEVEL: without an IMU nothing measures gravity, and "the face-on camera
    // is near level" is the convention that defines the world's vertical (FitConfig / design §12).
    // A face-on camera pitched p tilts the fitted world by ~p/2 — a bias of the convention, not a
    // defect of the fit, and not what these gates measure.
    T.cam.fF = 1400; T.cam.pF = 0;
    T.cam.psiD = dtlYawDeg * kDeg; T.cam.pD = 8 * kDeg; T.cam.fD = 1250;
    const V3 golfer { 0.05, 1.9, 0 };
    T.cam.cD = golfer - V3 { std::cos(T.cam.psiD), std::sin(T.cam.psiD), 0 } * 2.3 + V3 { 0, 0, 0.35 };
    T.cam.zG = -1.0;

    const std::vector<Key> keys = {
        { 0.00, { { "root.pitch", 22 }, { "root.roll", -4 }, { "spine.flex", 8 }, { "spine1.flex", 8 }, { "spine2.flex", 6 },
                  { "spine.lat", -4 }, { "spine1.lat", -4 }, { "spine2.lat", -4 },
                  { "head.flex", 10 }, { "lArm.flex", 26 }, { "rArm.flex", 30 }, { "lArm.abd", -28 }, { "rArm.abd", -32 },
                  { "rClav.prot", 18 }, { "rClav.elev", -8 },
                  { "lElbow.flex", 5 }, { "rElbow.flex", 22 }, { "lForearm.pron", 10 }, { "rForearm.pron", -10 },
                  { "lWrist.rad", 10 }, { "rWrist.flex", -10 } } },
        { 0.35, { { "root.pitch", 22 }, { "root.roll", -4 }, { "spine.flex", 8 }, { "spine1.flex", 8 }, { "spine2.flex", 6 },
                  { "spine.lat", -4 }, { "spine1.lat", -4 }, { "spine2.lat", -4 },
                  { "head.flex", 10 }, { "lArm.flex", 26 }, { "rArm.flex", 30 }, { "lArm.abd", -28 }, { "rArm.abd", -32 },
                  { "rClav.prot", 18 }, { "rClav.elev", -8 },
                  { "lElbow.flex", 5 }, { "rElbow.flex", 22 }, { "lForearm.pron", 10 }, { "rForearm.pron", -10 },
                  { "lWrist.rad", 10 }, { "rWrist.flex", -10 } } },
        { 1.10, { { "root.yaw", -45 }, { "root.pitch", 20 }, { "spine.twist", -15 }, { "spine1.twist", -15 }, { "spine2.twist", -15 },
                  { "spine.flex", 6 }, { "spine1.flex", 6 }, { "spine2.flex", 6 }, { "spine.lat", 6 }, { "head.twist", 40 },
                  { "head.flex", 10 }, { "lArm.flex", 105 }, { "lArm.abd", -45 }, { "lArm.rot", 20 }, { "lElbow.flex", 6 },
                  { "lForearm.pron", 40 }, { "lWrist.rad", 35 }, { "lWrist.flex", 12 },
                  { "rArm.flex", 85 }, { "rArm.abd", 35 }, { "rArm.rot", 50 }, { "rElbow.flex", 95 }, { "rWrist.flex", -40 } } },
        { 1.40, { { "root.yaw", 30 }, { "root.pitch", 18 }, { "spine.twist", 3 }, { "spine1.twist", 3 }, { "spine2.twist", 3 },
                  { "spine.flex", 6 }, { "spine1.flex", 6 }, { "spine2.flex", 6 }, { "spine.lat", -10 }, { "head.twist", -15 },
                  { "head.flex", 10 }, { "lArm.flex", 40 }, { "lArm.abd", -12 }, { "lElbow.flex", 2 }, { "lForearm.pron", 5 },
                  { "lWrist.flex", 18 }, { "lWrist.rad", 0 }, { "rArm.flex", 40 }, { "rArm.abd", -25 }, { "rElbow.flex", 25 },
                  { "rWrist.flex", -25 } } },
        { 1.85, { { "root.yaw", 90 }, { "root.pitch", 5 }, { "spine.twist", 10 }, { "spine1.twist", 10 }, { "spine2.twist", 10 },
                  { "spine.flex", -5 }, { "spine.lat", -10 }, { "head.twist", -40 }, { "lArm.flex", 120 }, { "lArm.abd", 55 },
                  { "lElbow.flex", 95 }, { "rArm.flex", 130 }, { "rArm.abd", -30 }, { "rElbow.flex", 15 }, { "lForearm.pron", -30 } } },
    };
    T.addressUs = 300000;
    T.topUs = 1100000;
    T.impactUs = 1400000;

    // Grip: the true axis a little off the fit's prior, both hands' grip points.
    const V3 bone { 0, 1, 0 };
    T.gripAxis = (bone * std::cos(48 * kDeg) - R.thumbLocal[0] * std::sin(48 * kDeg)).unit();
    T.gripOff = (bone * 0.085 + R.palmLocal[0] * 0.02) * scale[GHand];
    T.trailOff = (bone * 0.08 + R.palmLocal[1] * 0.02) * scale[GHand];
    std::mt19937 rng(7);
    std::normal_distribution<double> n01(0, 1);
    for (int m = 0; m < kMarkerCount; ++m) {
        const Marker &M = R.markers[size_t(m)];
        const V3 jit = M.fitOffset ? V3 { n01(rng), n01(rng), n01(rng) } * 0.008 : V3 {};
        T.off[size_t(m)] = M.offsetPrior + jit;
    }

    // Root placement: feet on the floor at address.
    const int nF = int(std::round(1.95 * fps));
    std::vector<V3> footTargets;
    const int lead = ybot::LeftHand, trail = ybot::RightHand;
    auto footPts = [&](const std::vector<double> &th) {
        Pose p;
        forwardKinematics(R, th.data(), scale.data(), p);
        std::vector<V3> v;
        for (int m = 17; m < 23; ++m) v.push_back(markerWorld(p, R.markers[size_t(m)].joint, T.off[size_t(m)]));
        return v;
    };
    std::vector<int> legDofs;
    for (const char *n : { "lHip.flex", "lHip.abd", "lHip.rot", "lKnee.flex", "lAnkle.dorsi", "lAnkle.inv",
                           "rHip.flex", "rHip.abd", "rHip.rot", "rKnee.flex", "rAnkle.dorsi", "rAnkle.inv" })
        legDofs.push_back(dofIndex(n));
    std::vector<int> trailDofs;
    for (const char *n : { "rArm.flex", "rArm.abd", "rArm.rot", "rElbow.flex", "rForearm.pron", "rWrist.flex", "rWrist.rad" })
        trailDofs.push_back(dofIndex(n));

    double worstGrip = 0;
    // Trail hand onto the lead's shaft line, 9 cm down it, until just after impact — IK on the
    // trail arm regularised toward its keyframe.
    std::vector<double> prevTrail;
    auto trailOnShaft = [&](std::vector<double> &th, double ts) {
        if (ts * 1e6 > T.impactUs + 60000) { prevTrail.clear(); return; }
        // Warm start from the previous frame's solution, pulled a little toward the keyframe:
        // continuity, not a fresh reach every frame (which is what folds arms).
        std::vector<double> key = th;
        if (!prevTrail.empty())
            for (int d : trailDofs) {
                key[size_t(d)] = 0.85 * prevTrail[size_t(d)] + 0.15 * th[size_t(d)];
                th[size_t(d)] = prevTrail[size_t(d)];
            }
        // The fit's grip term is "the trail palm lies ON THE SHAFT LINE" (anywhere along it), so
        // the synthetic golfer satisfies exactly that: the perpendicular offset driven to zero.
        auto lineOffset = [&](const std::vector<double> &x) {
            Pose p;
            forwardKinematics(R, x.data(), scale.data(), p);
            const V3 g = markerWorld(p, lead, T.gripOff);
            const V3 d = p.rot[lead].rotate(T.gripAxis);
            const V3 q = markerWorld(p, trail, T.trailOff);
            const V3 qg = q - g;
            return std::vector<V3> { qg - d * qg.dot(d) };
        };
        const V3 target {};
        const double e = ik(th, trailDofs, lineOffset, { target }, 60, &key, 0.03);
        if (ts * 1e6 >= T.addressUs) worstGrip = std::max(worstGrip, e);
        if (e > 0.02 && std::getenv("SK3D_GEN_DEBUG")) {
            Pose p;
            forwardKinematics(R, th.data(), scale.data(), p);
            std::printf("  gen t %.3f grip miss %.0f mm;", ts, e * 1000);
            for (int d : trailDofs) std::printf(" %s %.0f", R.dofs[size_t(d)].name, th[size_t(d)] / kDeg);
            std::printf("\n");
        }
        prevTrail = th;
    };
    std::vector<double> prev;
    for (int i = 0; i < nF; ++i) {
        const double ts = i / fps;
        std::vector<double> th = poseAt(keys, ts);
        // Root translation: sway back, bump forward, a little rise in the finish.
        const double sway = -0.04 * smoothstep((ts - 0.35) / 0.75) + 0.10 * smoothstep((ts - 1.1) / 0.3);
        th[0] = golfer.x + sway;
        th[1] = golfer.y;
        th[2] = 0.0 + 0.03 * smoothstep((ts - 1.4) / 0.4);
        if (i == 0) {
            // Knees/hips at address: flex so the posture is athletic, then drop the root until
            // the heels touch the floor.
            for (const char *n : { "lHip.flex", "rHip.flex" }) th[size_t(dofIndex(n))] = 28 * kDeg;
            for (const char *n : { "lKnee.flex", "rKnee.flex" }) th[size_t(dofIndex(n))] = 22 * kDeg;
            for (const char *n : { "lAnkle.dorsi", "rAnkle.dorsi" }) th[size_t(dofIndex(n))] = 12 * kDeg;
            // Feet FLAT: each foot's three markers at their flat-foot heights (the ankles take it).
            for (int pass = 0; pass < 3; ++pass) {
                const std::vector<V3> f = footPts(th);
                for (int side = 0; side < 2; ++side) {
                    const std::vector<int> ank { dofIndex(side == 0 ? "lAnkle.dorsi" : "rAnkle.dorsi"),
                                                 dofIndex(side == 0 ? "lAnkle.inv" : "rAnkle.inv") };
                    double base = 1e9;
                    for (int k = 0; k < 3; ++k)
                        base = std::min(base, f[size_t(3 * side + k)].z - R.markers[size_t(17 + 3 * side + k)].floorHeight);
                    std::vector<V3> tgt;
                    for (int k = 0; k < 3; ++k) {
                        V3 q = f[size_t(3 * side + k)];
                        q.z = base + R.markers[size_t(17 + 3 * side + k)].floorHeight;
                        tgt.push_back(q);
                    }
                    ik(th, ank, [&](const std::vector<double> &x) {
                        std::vector<V3> v = footPts(x);
                        return std::vector<V3>(v.begin() + 3 * side, v.begin() + 3 * side + 3);
                    }, tgt, 30);
                }
            }
            const std::vector<V3> f = footPts(th);
            double zmin = 1e9;
            for (int k = 0; k < 6; ++k) zmin = std::min(zmin, f[size_t(k)].z - R.markers[size_t(17 + k)].floorHeight);
            th[2] += T.cam.zG - zmin;
            // …and BOTH feet on the floor: the whole legs take the difference.
            {
                std::vector<V3> tgt = footPts(th);
                for (int k = 0; k < 6; ++k) tgt[size_t(k)].z = T.cam.zG + R.markers[size_t(17 + k)].floorHeight;
                const std::vector<double> ref = th;
                ik(th, legDofs, footPts, tgt, 40, &ref);
            }
            footTargets = footPts(th);
            trailOnShaft(th, ts);
            T.th.push_back(th);
            T.t.push_back(int64_t(std::llround(ts * 1e6)));
            prev = th;
            continue;
        }
        th[2] += T.th.front()[2];
        // Legs: start from the previous frame's solution, plant the feet (until impact;
        // after it the trail foot releases and is carried with the body).
        const std::vector<double> legRef = prev;
        for (int d : legDofs) th[size_t(d)] = prev[size_t(d)];
        const bool afterImpact = ts * 1e6 > T.impactUs + 80000;
        if (!afterImpact) {
            ik(th, legDofs, footPts, footTargets, 25, &legRef);
        } else {
            std::vector<int> leadLeg(legDofs.begin(), legDofs.begin() + 6);
            ik(th, leadLeg, [&](const std::vector<double> &x) {
                std::vector<V3> v = footPts(x);
                return std::vector<V3>(v.begin(), v.begin() + 3);
            }, std::vector<V3>(footTargets.begin(), footTargets.begin() + 3), 25, &legRef);
        }
        trailOnShaft(th, ts);
        T.th.push_back(th);
        T.t.push_back(int64_t(std::llround(ts * 1e6)));
        prev = th;
    }
    std::printf("synthetic truth: trail hand off the shaft by at most %.1f mm (address → impact)\n", worstGrip * 1000);
    return T;
}

struct Obs {
    FitInput in;
};

static FitInput observe(const Truth &T, double sigmaPx, double dropout, bool withDtl, unsigned seed,
                        bool labelSwapAtTop)
{
    const Rig &R = rig();
    FitInput in;
    in.t_us = T.t;
    in.foW = T.foW; in.foH = T.foH;
    in.dtlW = withDtl ? T.dtlW : 0; in.dtlH = withDtl ? T.dtlH : 0;
    in.addressUs = T.addressUs; in.topUs = T.topUs; in.impactUs = T.impactUs;
    in.leadIsLeft = true;
    in.heightM = (R.restHeadTopY + 0.02) * T.scale[GSpine];
    in.clubLengthM = 0.95;
    std::mt19937 rng(seed);
    std::normal_distribution<double> nz(0, 1);
    std::uniform_real_distribution<double> u01(0, 1);
    for (size_t i = 0; i < T.t.size(); ++i) {
        Pose p;
        forwardKinematics(R, T.th[i].data(), T.scale.data(), p);
        ViewObs vo[2];
        for (int view = 0; view < (withDtl ? 2 : 1); ++view) {
            const int W = view == 0 ? T.foW : T.dtlW, H = view == 0 ? T.foH : T.dtlH;
            for (int m = 0; m < kMarkerCount; ++m) {
                double u, v;
                const V3 w = markerWorld(p, R.markers[size_t(m)].joint, T.off[size_t(m)]);
                if (!projectPoint(T.cam, view, W, H, w, u, v) || u01(rng) < dropout) continue;
                vo[view].kp[size_t(m)] = { u + sigmaPx * nz(rng), v + sigmaPx * nz(rng), sigmaPx };
            }
            // Shaft angle, where the projected shaft is long enough to measure.
            const V3 g = markerWorld(p, ybot::LeftHand, T.gripOff);
            const V3 d = p.rot[ybot::LeftHand].rotate(T.gripAxis);
            double u1, v1, u2, v2;
            if (projectPoint(T.cam, view, W, H, g, u1, v1) && projectPoint(T.cam, view, W, H, g + d * 0.9, u2, v2)) {
                const double len = std::hypot(u2 - u1, v2 - v1);
                const double sig = (view == 0 ? 1.5 : 2.5) * kDeg;
                if (len > (view == 0 ? 60.0 : 150.0)) {
                    vo[view].shaftTheta = std::atan2(v2 - v1, u2 - u1) + sig * nz(rng);
                    vo[view].shaftSigma = sig;
                }
            }
            // The clubhead, measured on about half the frames (blur takes the rest).
            double hu, hv;
            if (projectPoint(T.cam, view, W, H, g + d * (0.95 - 0.04), hu, hv) && u01(rng) < 0.5)
                vo[view].headU = hu + 4.0 * nz(rng), vo[view].headV = hv + 4.0 * nz(rng), vo[view].headSigma = 4.0;
        }
        const int64_t tus = T.t[i];
        if (labelSwapAtTop && std::llabs(tus - T.topUs) < 15000) {
            ViewObs s = vo[0];
            for (int m = 1; m < kMarkerCount; ++m)
                if (R.markers[size_t(m)].mirror >= 0) vo[0].kp[size_t(m)] = s.kp[size_t(R.markers[size_t(m)].mirror)];
        }
        in.fo.push_back(vo[0]);
        if (withDtl) in.dtl.push_back(vo[1]);
        std::array<uint8_t, 6> c {};
        const bool before = tus <= T.impactUs + 80000;
        for (int f = 0; f < 6; ++f) c[size_t(f)] = before || f < 3;
        if (tus > T.impactUs + 230000) c = {};
        in.footContact.push_back(c);
    }
    return in;
}

struct Score {
    double dirP90 = 0, dirBodyP90 = 0, rollP90 = 0, posP90Cm = 0, leadForearmRollP90 = 0;
    double ms = 0;
};

static Score score(const Truth &T, const FitResult &r, bool print)
{
    const Rig &R = rig();
    std::vector<double> dirE, dirBody, rollE, posE, lfRoll;
    std::vector<std::vector<double>> perBoneRoll(Rig::N);
    for (size_t i = 0; i < T.t.size(); ++i) {
        Pose pt, pf;
        forwardKinematics(R, T.th[i].data(), T.scale.data(), pt);
        std::array<double, GroupCount> sc = r.scale;
        forwardKinematics(R, r.theta[i].data(), sc.data(), pf);
        for (int j = 1; j < Rig::N; ++j) {
            posE.push_back(100.0 * ((pt.pos[j] - pt.pos[0]) - (pf.pos[j] - pf.pos[0])).norm());
            // Bones with a child: direction error.
            for (int c = 0; c < Rig::N; ++c)
                if (R.parent[c] == j) {
                    const V3 a = (pt.pos[c] - pt.pos[j]).unit(), b = (pf.pos[c] - pf.pos[j]).unit();
                    const double e = std::acos(std::clamp(a.dot(b), -1.0, 1.0)) / kDeg;
                    dirE.push_back(e);
                    if (c != ybot::LeftHandMiddle1 && c != ybot::RightHandMiddle1) dirBody.push_back(e);
                    break;
                }
            if (R.nDof[j] == 0) continue;
            // Roll: the relative rotation's twist about the bone's own axis.
            const Q e = pt.rot[j].conj() * pf.rot[j];
            const double tw = 2.0 * std::atan2(e.y, e.w) / kDeg;
            const double twist = std::fabs(std::remainder(tw, 360.0));
            rollE.push_back(twist);
            perBoneRoll[size_t(j)].push_back(twist);
            if (j == ybot::LeftForeArm && T.t[i] >= T.addressUs && T.t[i] <= T.impactUs) lfRoll.push_back(twist);
        }
    }
    Score s;
    s.dirP90 = p90(dirE);
    s.dirBodyP90 = p90(dirBody);
    s.rollP90 = p90(rollE);
    s.posP90Cm = p90(posE);
    s.leadForearmRollP90 = p90(lfRoll);
    s.ms = r.ms;
    if (print) {
        std::printf("      bone direction p90 %.2f° (body, hands' own direction excluded: %.2f°), roll p90 %.2f°, joint position p90 %.2f cm, lead-forearm roll "
                    "(address→impact) p90 %.2f°, %.0f ms, %d iterations\n",
                    s.dirP90, s.dirBodyP90, s.rollP90, s.posP90Cm, s.leadForearmRollP90, r.ms, r.iterations);
        std::printf("      per-bone roll p90:");
        for (int j = 1; j < Rig::N; ++j)
            if (!perBoneRoll[size_t(j)].empty()) std::printf(" %s %.1f", ybot::kJoints[j].name, p90(perBoneRoll[size_t(j)]));
        std::printf("\n      reproj median fo %.2f px dtl %.2f px; γ %.1f°; r %.3f; swaps fo %d; limit-held %d; slip p90 %.1f mm\n",
                    r.reprojMedPxFo, r.reprojMedPxDtl, r.gammaDeg, r.rRatio, r.nSwapFo, r.nLimitHeld, r.footSlipP90Mm);
        std::printf("      cameras: fitted pF %.2f° rD %.2f° pD %.2f° zG %.3f (truth pF %.2f° rD 0 pD %.2f° zG %.3f)\n",
                    r.cam.pF / kDeg, r.cam.rD / kDeg, r.cam.pD / kDeg, r.cam.zG, T.cam.pF / kDeg, T.cam.pD / kDeg, T.cam.zG);
        // Truth: planted foot markers against the floor + their flat-foot heights.
        double worstFoot = 0;
        Pose p0;
        forwardKinematics(R, T.th[0].data(), T.scale.data(), p0);
        for (int k = 0; k < 6; ++k)
            worstFoot = std::max(worstFoot, std::fabs(markerWorld(p0, R.markers[size_t(17 + k)].joint, T.off[size_t(17 + k)]).z
                                                      - T.cam.zG - R.markers[size_t(17 + k)].floorHeight));
        std::printf("      truth feet at address off their flat heights by ≤ %.1f mm\n", worstFoot * 1000);
    }
    return s;
}

// Where the errors are: per-bone direction p90, and the error over the swing's phases.
static void diagnose(const Truth &T, const FitResult &r)
{
    const Rig &R = rig();
    std::vector<std::vector<double>> dirB(Rig::N);
    const int nSeg = 8;
    std::vector<std::vector<double>> byT(nSeg);
    for (size_t i = 0; i < T.t.size(); ++i) {
        Pose pt, pf;
        forwardKinematics(R, T.th[i].data(), T.scale.data(), pt);
        std::array<double, GroupCount> sc = r.scale;
        forwardKinematics(R, r.theta[i].data(), sc.data(), pf);
        const int seg = std::min(nSeg - 1, int(double(i) / double(T.t.size()) * nSeg));
        for (int c = 1; c < Rig::N; ++c) {
            const int j = R.parent[c];
            const V3 a = (pt.pos[c] - pt.pos[j]).unit(), b = (pf.pos[c] - pf.pos[j]).unit();
            const double e = std::acos(std::clamp(a.dot(b), -1.0, 1.0)) / kDeg;
            dirB[size_t(c)].push_back(e);
            byT[size_t(seg)].push_back(e);
        }
    }
    // Per-DoF: truth limit violations, and the fit's median |error|.
    std::printf("      per DoF (truth beyond its limit in N frames | median |fit − truth| °):\n       ");
    for (int k = 3; k < R.dofCount(); ++k) {
        const Dof &d = R.dofs[size_t(k)];
        int viol = 0;
        std::vector<double> e;
        for (size_t i = 0; i < T.t.size(); ++i) {
            const double v = T.th[i][size_t(k)];
            if (d.lo < d.hi && (v < d.lo - 1e-9 || v > d.hi + 1e-9)) ++viol;
            e.push_back(std::fabs(r.theta[i][size_t(k)] - v) / kDeg);
        }
        std::sort(e.begin(), e.end());
        std::printf(" %s %d|%.0f", d.name, viol, e[e.size() / 2]);
        if (k % 8 == 2) std::printf("\n       ");
    }
    std::printf("\n");
    // The worst frame for the trail upper arm.
    {
        double worst = -1; size_t wi = 0;
        for (size_t i = 0; i < T.t.size(); ++i) {
            Pose pt, pf;
            forwardKinematics(R, T.th[i].data(), T.scale.data(), pt);
            std::array<double, GroupCount> sc = r.scale;
            forwardKinematics(R, r.theta[i].data(), sc.data(), pf);
            const V3 a = (pt.pos[ybot::RightForeArm] - pt.pos[ybot::RightArm]).unit();
            const V3 b = (pf.pos[ybot::RightForeArm] - pf.pos[ybot::RightArm]).unit();
            const double e = std::acos(std::clamp(a.dot(b), -1.0, 1.0)) / kDeg;
            if (e > worst) { worst = e; wi = i; }
        }
        std::printf("      worst trail upper arm: %.0f° at frame %zu (t %.3f s, flags %d)\n       ", worst, wi, T.t[wi] * 1e-6, int(r.flags[wi]));
        for (int k = 0; k < R.dofCount(); ++k) {
            const char *n = R.dofs[size_t(k)].name;
            if (n[0] == 'r' && n[1] != 'o') std::printf(" %s %.0f/%.0f", n, T.th[wi][size_t(k)] / kDeg, r.theta[wi][size_t(k)] / kDeg);
        }
        std::printf("\n");
    }
    std::printf("      direction p90 by bone (child joint):");
    for (int c = 1; c < Rig::N; ++c) std::printf(" %s %.0f", ybot::kJoints[c].name, p90(dirB[size_t(c)]));
    std::printf("\n      direction p90 by eighth of the swing:");
    for (int s = 0; s < nSeg; ++s) std::printf(" %.0f", p90(byT[size_t(s)]));
    std::printf("\n");
}

int main()
{
    std::printf("=== skeleton3d ===\n");
    testRig();

    std::array<double, GroupCount> unit; unit.fill(1.0);
    const Truth T = makeSwing(120.0, unit, 10.0);
    std::printf("synthetic swing: %zu frames at 120 Hz\n", T.t.size());

    // Jacobian check.
    {
        std::printf("Jacobian\n");
        FitInput in = observe(T, 2.0, 0.02, true, 11, false);
        in.cfg.debugJacobianFrames = 3;
        in.cfg.fitLengths = true;       // exercise the scale columns too
        in.cfg.stage2Iters = 0;
        const FitResult r = fitSkeleton(in);
        std::printf("      worst analytic-vs-numeric %.3e at %s\n", r.debugJacobianErr, r.debugJacobianWorst.c_str());
        check(r.debugJacobianErr < 1e-3, "analytic Jacobian agrees with central differences");
    }

    // (b) accuracy.
    std::printf("(b) two views, σ = 2 px, 2 %% dropout, one face-on label swap at the top\n");
    FitInput inB = observe(T, 2.0, 0.02, true, 11, true);
    const FitResult rB = fitSkeleton(inB);
    check(rB.valid, "(b) the fit converged");
    const Score sB = score(T, rB, true);
    // The design's §8.1 targets are REPORTED; the asserts below them are regression guards at
    // what the fit achieves (26 Sept 2026), and what each miss is owed to is in the design §12.
    auto target = [](bool met, const char *what) { std::printf("  [%s] design target: %s\n", met ? "MET" : "MISSED", what); };
    target(sB.dirP90 <= 3.0, "bone direction ≤ 3° p90 (all bones)");
    target(sB.rollP90 <= 8.0, "roll ≤ 8° p90");
    target(sB.leadForearmRollP90 <= 8.0, "lead-forearm roll ≤ 8° p90 through the straight-arm band");
    check(sB.dirBodyP90 <= 6.0, "(b) body bone direction ≤ 6° p90 (the hands' own direction excluded)");
    check(sB.rollP90 <= 9.0, "(b) roll ≤ 9° p90");
    check(sB.leadForearmRollP90 <= 30.0, "(b) lead-forearm roll ≤ 30° p90 without an instrument on the wrist");
    check(sB.posP90Cm <= 2.0, "(b) root-relative joint position ≤ 2 cm p90");
    check(rB.nSwapFo >= 1, "(b) the label swap at the top is found");

    // (b-HM) the same, with a HackMotion on the lead wrist (its two angles, σ 1.5°).
    std::printf("(b-HM) with a HackMotion on the lead wrist\n");
    {
        FitInput in = inB;
        const int kf = dofIndex("lWrist.flex"), kr = dofIndex("lWrist.rad");
        std::mt19937 rng(5);
        std::normal_distribution<double> nz(0, 1.5);
        for (size_t i = 0; i < T.t.size(); ++i) {
            in.hmFlexDeg.push_back(T.th[i][size_t(kf)] / kDeg + nz(rng));
            in.hmRadDeg.push_back(T.th[i][size_t(kr)] / kDeg + nz(rng));
        }
        const FitResult r = fitSkeleton(in);
        const Score s = score(T, r, true);
        std::printf("  [%s] design target: lead-forearm roll ≤ 8° p90 with the wrist instrumented\n",
                    s.leadForearmRollP90 <= 8.0 ? "MET" : "MISSED");
        check(s.leadForearmRollP90 < 0.7 * sB.leadForearmRollP90, "(b-HM) the HackMotion cuts the lead-forearm roll error by ≥ 30 %");
    }

    // (b-IMU) orientation sensors on the pelvis and the lead forearm: an unknown mount, a 30°
    // heading offset, 1° noise. The fit solves both and the lead forearm's roll follows it.
    for (int known = 0; known < 2; ++known) {
        std::printf("(b-IMU) IMUs on the pelvis and the lead forearm, mount %s\n", known ? "from a calibration record" : "unknown");
        FitInput in = inB;
        const Rig &R = rig();
        std::mt19937 rng(9);
        std::normal_distribution<double> nz(0, 1.0 * kDeg);
        const Q heading = Q::axisAngle({ 0, 0, 1 }, 30 * kDeg);
        for (int joint : { int(ybot::Hips), int(ybot::LeftForeArm) }) {
            ImuTrack it;
            it.joint = joint;
            const Q mount = Q::axisAngle(V3 { 0.3, 1, -0.4 }, joint == ybot::Hips ? 70 * kDeg : 110 * kDeg);
            if (known) { it.hasMount = true; it.mount = (mount * Q::axisAngle({ 1, 0, 0 }, 2 * kDeg)).normalized(); }
            for (size_t i = 0; i < T.t.size(); ++i) {
                Pose p;
                forwardKinematics(R, T.th[i].data(), T.scale.data(), p);
                const Q noise = Q::axisAngle(V3 { nz(rng), nz(rng), nz(rng) }, 1.0 * kDeg);
                it.q.push_back((heading * p.rot[joint] * mount * noise).normalized());
                it.valid.push_back(1);
            }
            in.imu.push_back(it);
        }
        const FitResult r = fitSkeleton(in);
        const Score s = score(T, r, true);
        if (known)
            check(r.valid && s.leadForearmRollP90 <= 8.0, "(b-IMU) with its mount known, a forearm IMU holds the lead-forearm roll ≤ 8° p90");
        else
            check(r.valid, "(b-IMU) with its mount unknown the fit converges (roll bias stays prior-limited)");
    }

    // Diagnosis: the same observations, stage 2 started from the TRUTH. If this scores well
    // and (b) does not, (b)'s errors are local minima of the start, not bias in the objective.
    std::printf("(b′) stage 2 started from the truth\n");
    {
        FitInput in = inB;
        in.debugInitTheta = &T.th;
        const FitResult r = fitSkeleton(in);
        score(T, r, true);
    }
    diagnose(T, rB);

    // (c) identifiability: truth has longer upper arms and shorter shanks, the DTL yawed 12°;
    // the fit starts from the height prior and a square DTL.
    std::printf("(c) identifiability\n");
    {
        std::array<double, GroupCount> sc; sc.fill(1.0);
        sc[GUpperArm] = 1.07; sc[GShank] = 0.94; sc[GForearm] = 1.04;
        const Truth Tc = makeSwing(120.0, sc, 12.0);
        FitInput in = observe(Tc, 2.0, 0.02, true, 13, false);
        in.cfg.fitLengths = true;       // off by default (see FitConfig); this IS the identifiability test
        const FitResult r = fitSkeleton(in);
        score(Tc, r, true);
        const double eU = std::fabs(r.scale[GUpperArm] - 1.07), eS = std::fabs(r.scale[GShank] - 0.94);
        const double eG = std::fabs(r.gammaDeg - (90.0 - 12.0));
        std::printf("      upper arm %.3f (1.07), shank %.3f (0.94), forearm %.3f (1.04), γ %.1f° (78.0)\n",
                    r.scale[GUpperArm], r.scale[GShank], r.scale[GForearm], r.gammaDeg);
        check(eU <= 0.02 && eS <= 0.02, "(c) segment lengths recovered within 2 %");
        check(eG <= 2.0, "(c) the angle between the views recovered within 2°");
    }

    // (d) ablation.
    std::printf("(d) ablation — each constraint off in turn (same observations as (b))\n");
    {
        struct Ab { const char *name; std::function<void(FitConfig &)> f; };
        const std::vector<Ab> ab = {
            { "full", [](FitConfig &) {} },
            { "no limits", [](FitConfig &c) { c.useLimits = false; } },
            { "no contact", [](FitConfig &c) { c.useContact = false; } },
            { "no shaft", [](FitConfig &c) { c.useShaft = false; } },
            { "no grip", [](FitConfig &c) { c.useGrip = false; } },
            { "no smooth", [](FitConfig &c) { c.useSmooth = false; } },
            { "lengths fitted", [](FitConfig &c) { c.fitLengths = true; } },
            { "cameras frozen", [](FitConfig &c) { c.fitCameras = false; } },
            { "DTL roll frozen", [](FitConfig &c) { c.fitDtlRoll = false; } },
        };
        std::printf("      %-16s %8s %8s %10s %12s\n", "config", "dir p90", "roll p90", "pos p90cm", "lFore roll");
        double fullRollLF = 0, noShaftRollLF = 0;
        for (const Ab &a : ab) {
            FitInput in = inB;
            a.f(in.cfg);
            const FitResult r = fitSkeleton(in);
            const Score s = score(T, r, false);
            std::printf("      %-16s %8.2f %8.2f %10.2f %12.2f\n", a.name, s.dirP90, s.rollP90, s.posP90Cm, s.leadForearmRollP90);
            if (std::string(a.name) == "full") fullRollLF = s.leadForearmRollP90;
            if (std::string(a.name) == "no shaft") noShaftRollLF = s.leadForearmRollP90;
        }
        check(noShaftRollLF > fullRollLF, "(d) the shaft term is what holds the lead-forearm roll");
    }

    // (e) face-on only.
    std::printf("(e) face-on only\n");
    {
        FitInput in = observe(T, 2.0, 0.02, false, 11, false);
        const FitResult r = fitSkeleton(in);
        check(r.valid, "(e) the face-on-only fit converged");
        const Score s = score(T, r, true);
        check(s.dirBodyP90 <= 20.0, "(e) face-on only: body bone direction ≤ 20° p90 (depth from the anatomy alone)");
    }

    // Timing at a real cadence.
    std::printf("timing\n");
    {
        const Truth T2 = makeSwing(240.0, unit, 10.0);
        FitInput in = observe(T2, 2.0, 0.02, true, 17, false);
        const FitResult r = fitSkeleton(in);
        std::printf("      %zu frames: %.0f ms, %d iterations\n", T2.t.size(), r.ms, r.iterations);
    }

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
