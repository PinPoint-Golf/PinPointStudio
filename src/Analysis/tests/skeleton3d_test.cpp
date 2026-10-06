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
#include "../skeleton3d/club_plane_catalogue.h"
#include "../skeleton3d/skeleton3d_rig.h"
#include "../pose_schedule.h"   // bracketAt — the DTL pose bracket (step 4)

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
    // A SHOD foot, as the fit assumes by default (FitConfig::foot*LiftM): the pose model's toe and
    // heel keypoints sit up on the shoe, so the synthetic markers are lifted the same way.
    const FitConfig defaults;
    const auto shodLift = [&](int m) {
        return (m < 17 || m > 22) ? 0.0 : (m == 19 || m == 22) ? defaults.footHeelLiftM : defaults.footToeLiftM;
    };
    for (int m = 0; m < kMarkerCount; ++m) {
        const Marker &M = R.markers[size_t(m)];
        const V3 jit = M.fitOffset ? V3 { n01(rng), n01(rng), n01(rng) } * 0.008 : V3 {};
        T.off[size_t(m)] = M.offsetPrior + jit + M.upLocal * shodLift(m);
    }
    // A marker's height above the floor on a flat foot: the rig's, plus the shoe.
    const auto flatHeight = [&](int m) { return R.markers[size_t(m)].floorHeight + shodLift(m); };

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
                        base = std::min(base, f[size_t(3 * side + k)].z - flatHeight(17 + 3 * side + k));
                    std::vector<V3> tgt;
                    for (int k = 0; k < 3; ++k) {
                        V3 q = f[size_t(3 * side + k)];
                        q.z = base + flatHeight(17 + 3 * side + k);
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
            for (int k = 0; k < 6; ++k) zmin = std::min(zmin, f[size_t(k)].z - flatHeight(17 + k));
            th[2] += T.cam.zG - zmin;
            // …and BOTH feet on the floor: the whole legs take the difference.
            {
                std::vector<V3> tgt = footPts(th);
                for (int k = 0; k < 6; ++k) tgt[size_t(k)].z = T.cam.zG + flatHeight(17 + k);
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

// The lean rig's map (design §13.2 (A)), mirrored here so a truth can be made representable by it:
// one spine shared over Spine/Spine1/Spine2 by segment length, clavicles following the upper arm.
static std::vector<double> leanProject(const std::vector<double> &th, double kE = 0.10, double kP = 0.10)
{
    const Rig &R = rig();
    std::vector<double> o = th;
    const double l0 = R.restT[ybot::Spine1].norm(), l1 = R.restT[ybot::Spine2].norm(), l2 = R.restT[ybot::Neck].norm();
    const double w[3] = { l0 / (l0 + l1 + l2), l1 / (l0 + l1 + l2), l2 / (l0 + l1 + l2) };
    for (const char *ax : { "flex", "lat", "twist" }) {
        const int a = dofIndex((std::string("spine.") + ax).c_str()), b = dofIndex((std::string("spine1.") + ax).c_str()),
                  c = dofIndex((std::string("spine2.") + ax).c_str());
        const double total = th[size_t(a)] + th[size_t(b)] + th[size_t(c)];
        o[size_t(a)] = w[0] * total; o[size_t(b)] = w[1] * total; o[size_t(c)] = w[2] * total;
    }
    for (const char *sd : { "l", "r" }) {
        const std::string sdd = sd;
        o[size_t(dofIndex((sdd + "Clav.elev").c_str()))] = kE * th[size_t(dofIndex((sdd + "Arm.abd").c_str()))];
        o[size_t(dofIndex((sdd + "Clav.prot").c_str()))] = kP * th[size_t(dofIndex((sdd + "Arm.flex").c_str()))];
    }
    return o;
}

static FitInput observe(const Truth &T, double sigmaPx, double dropout, bool withDtl, unsigned seed,
                        bool labelSwapAtTop, bool grounded = false)
{
    const Rig &R = rig();
    FitInput in;
    in.t_us = T.t;
    in.foW = T.foW; in.foH = T.foH;
    in.dtlW = withDtl ? T.dtlW : 0; in.dtlH = withDtl ? T.dtlH : 0;
    in.addressUs = T.addressUs; in.topUs = T.topUs; in.impactUs = T.impactUs;
    in.leadIsLeft = true;
    in.heightM = (R.restHeadTopY + 0.02) * T.scale[GSpine];
    // The club. The suite's guards were set on a 0.95 m club that is NOT grounded at address (the
    // keyframed posture holds its head well off the floor), so the grounded-club term is pinned
    // off for them. `grounded` is the (G) section's golfer: his club is as long as it takes for its
    // head to rest groundedClubLiftM above the floor at address, and the term is left at its default.
    double clubToHead = 0.95 - 0.04;
    if (grounded) {
        size_t ia = 0;
        for (size_t i = 0; i < T.t.size(); ++i)
            if (std::llabs(T.t[i] - T.addressUs) < std::llabs(T.t[ia] - T.addressUs)) ia = i;
        Pose p;
        forwardKinematics(R, T.th[ia].data(), T.scale.data(), p);
        const V3 g = markerWorld(p, ybot::LeftHand, T.gripOff);
        const V3 d = p.rot[ybot::LeftHand].rotate(T.gripAxis);
        if (d.z < -0.2) clubToHead = (g.z - T.cam.zG - FitConfig().groundedClubLiftM) / -d.z;
    } else {
        in.cfg.groundedClubSigmaM = 0;
    }
    in.clubLengthM = clubToHead + 0.04;
    // The suite's guards were set on the per-frame 48-angle fit; the lean (L) and spline (S) sections
    // switch those on themselves. Pinned so the production defaults (§13.5–13.6) leave them where they were.
    in.cfg.leanRig = false; in.cfg.leanClavicles = true; in.cfg.splineBasis = false;
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
            if (projectPoint(T.cam, view, W, H, g + d * clubToHead, hu, hv) && u01(rng) < 0.5)
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
                                                      - T.cam.zG - R.markers[size_t(17 + k)].floorHeight
                                                      - ((k == 2 || k == 5) ? FitConfig{}.footHeelLiftM : FitConfig{}.footToeLiftM)));
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
    // SK3D_REPORT=1 also runs the fits that only PRINT — stage 2 from the truth, the full ablation
    // table, the lean model-mismatch rows, timing at 240 Hz. They assert nothing and were most of
    // this test's runtime, so the default run fits only what a check() reads.
    const bool report = std::getenv("SK3D_REPORT") != nullptr;
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

    // (b-thr) evaluate() on a thread team (analysis_dag_design.md step D): the frames' residuals run
    // on the team and the shared block is still summed in frame order, so the fit is the serial
    // fit's BITS — every angle, the cameras, the cost, the iteration count. One fit at whichever
    // setting (b) did not use: (b) runs the default (0 = auto, min(8, physical cores)), this one the
    // serial loop; with the default at 1 this one runs 4 threads. On the M4 (6 Oct 2026): 0 rad
    // apart, 1023 → 601 ms.
    {
        FitInput in = inB;
        in.cfg.evalThreads = inB.cfg.evalThreads == 1 ? 4 : 1;
        const FitResult r = fitSkeleton(in);
        double dTh = 0;
        bool same = r.theta.size() == rB.theta.size() && r.iterations == rB.iterations
                    && r.costFinal == rB.costFinal && r.cam.fF == rB.cam.fF && r.cam.pD == rB.cam.pD
                    && r.cam.cD.x == rB.cam.cD.x && r.cam.cD.y == rB.cam.cD.y && r.cam.cD.z == rB.cam.cD.z
                    && r.gripOffsetLocal == rB.gripOffsetLocal && r.nSwapFo == rB.nSwapFo;
        for (size_t i = 0; same && i < r.theta.size(); ++i)
            for (size_t k = 0; k < r.theta[i].size(); ++k) {
                dTh = std::max(dTh, std::fabs(r.theta[i][k] - rB.theta[i][k]));
                same = same && r.theta[i][k] == rB.theta[i][k];
            }
        std::printf("      evalThreads %d vs %d: cost %.17g vs %.17g, %d vs %d iterations, max |Δθ| %.3g rad, "
                    "%.0f vs %.0f ms\n", in.cfg.evalThreads, inB.cfg.evalThreads, r.costFinal, rB.costFinal,
                    r.iterations, rB.iterations, dTh, r.ms, rB.ms);
        check(same, "(b-thr) the serial and the threaded evaluate() give the identical fit, bit for bit");
    }

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
    if (report) {
        std::printf("(b′) stage 2 started from the truth\n");
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
            const bool full = std::string(a.name) == "full", noShaft = std::string(a.name) == "no shaft";
            if (!report && !full && !noShaft) continue;        // the rest of the table only prints
            FitInput in = inB;
            a.f(in.cfg);
            const FitResult r = full ? rB : fitSkeleton(in);   // "full" IS (b): same input, same config
            const Score s = score(T, r, false);
            std::printf("      %-16s %8.2f %8.2f %10.2f %12.2f\n", a.name, s.dirP90, s.rollP90, s.posP90Cm, s.leadForearmRollP90);
            if (full) fullRollLF = s.leadForearmRollP90;
            if (noShaft) noShaftRollLF = s.leadForearmRollP90;
        }
        check(noShaftRollLF > fullRollLF, "(d) the shaft term is what holds the lead-forearm roll");
    }

    // (e) face-on only.
    std::printf("(e) face-on only\n");
    const FitInput inFo = observe(T, 2.0, 0.02, false, 11, false);
    const FitResult rFo = fitSkeleton(inFo);      // (p-f) below is the same fit
    {
        const FitResult &r = rFo;
        check(r.valid, "(e) the face-on-only fit converged");
        const Score s = score(T, r, true);
        check(s.dirBodyP90 <= 20.0, "(e) face-on only: body bone direction ≤ 20° p90 (depth from the anatomy alone)");
    }

    // ── the club's depth branch where the DTL is blind (skeleton3d_shaft_branch_design.md §6.1) ──
    std::printf("depth branch — the DTL's shaft and clubhead hidden from impact + 60 ms\n");
    {
        const Rig &R = rig();
        // Angle between the fitted and the true shaft, on the frames `pick` selects.
        auto shaftErr = [&](const FitResult &r, const std::function<bool(size_t)> &pick) {
            std::vector<double> e;
            for (size_t i = 0; i < T.t.size() && i < r.shaftDir.size(); ++i) {
                if (!pick(i)) continue;
                Pose p;
                forwardKinematics(R, T.th[i].data(), T.scale.data(), p);
                const V3 d = p.rot[ybot::LeftHand].rotate(T.gripAxis).unit();
                e.push_back(std::acos(std::clamp(d.dot(r.shaftDir[i].unit()), -1.0, 1.0)) / kDeg);
            }
            return e;
        };
        auto med = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        const int64_t dropAfter = T.impactUs + 60000;
        auto blind = [&](size_t i) { return T.t[i] > dropAfter; };
        auto report = [&](const char *what, const FitResult &r) {
            const std::vector<double> e = shaftErr(r, blind), all = shaftErr(r, [](size_t) { return true; });
            std::printf("      %-34s blind shaft median %5.1f° p90 %5.1f° | all frames p90 %5.1f° | plane frames %d, runs %d, kept %d, %.0f ms (branch %.0f ms)\n",
                        what, med(e), p90(e), p90(all), r.nPlaneFrames, r.nBranchRuns, r.nBranchKept, r.ms, r.branchMs);
            return p90(e);
        };
        FitInput base = inB;
        base.cfg.debugDropDtlShaftAfterUs = 60000;

        // (p-a) from the fit's own start: the club stays on its true branch.
        const FitResult rA = fitSkeleton(base);
        std::printf("      planes: back %s n=(%.2f,%.2f,%.2f) %d frames %.1f° rms; down %s n=(%.2f,%.2f,%.2f) %d frames %.1f° rms\n",
                    rA.planeBack.source.c_str(), rA.planeBack.n.x, rA.planeBack.n.y, rA.planeBack.n.z, rA.planeBack.count, rA.planeBack.rmsDeg,
                    rA.planeDown.source.c_str(), rA.planeDown.n.x, rA.planeDown.n.y, rA.planeDown.n.z, rA.planeDown.count, rA.planeDown.rmsDeg);
        const double pA = report("(p-a) the fit's own start", rA);
        check(rA.valid && rA.nPlaneFrames > 0, "(p-a) the blind frames are held by the plane term");
        check(pA <= 8.0, "(p-a) the club stays on its true branch: blind shaft direction ≤ 8° p90");

        // (p-b) started on the MIRROR: the branch pass returns it.
        FitInput inMir = base;
        inMir.cfg.debugForceMirror = true;
        const FitResult rBm = fitSkeleton(inMir);
        const double pB = report("(p-b) forced onto the mirror", rBm);
        check(rBm.nBranchKept >= 1, "(p-b) the branch pass flips the mirrored club back…");
        check(pB <= 8.0, "(p-b) …onto its true branch: blind shaft direction ≤ 8° p90");
        int flagged = 0;
        for (uint8_t f : rBm.flags) flagged += (f & FlagShaftBranch) ? 1 : 0;
        check(flagged > 0, "(p-b) and flags the frames it flipped");
        if (std::getenv("SK3D_BRANCH_DEBUG")) {
            // Per blind frame: the truth's, the forced-mirror fit's and the final fit's out-of-plane
            // angles against the down plane, the error, and whether the frame was flipped back.
            FitInput noPass = inMir; noPass.cfg.branchPass = false;
            const FitResult rM = fitSkeleton(noPass);
            const V3 n = rBm.planeDown.n;
            for (size_t i = 0; i < T.t.size(); ++i) {
                if (!blind(i)) continue;
                Pose p;
                forwardKinematics(R, T.th[i].data(), T.scale.data(), p);
                const V3 d = p.rot[ybot::LeftHand].rotate(T.gripAxis).unit();
                auto oop = [&](const V3 &u) { return std::asin(std::clamp(u.unit().dot(n), -1.0, 1.0)) / kDeg; };
                auto err = [&](const V3 &u) { return std::acos(std::clamp(d.dot(u.unit()), -1.0, 1.0)) / kDeg; };
                std::printf("        t %.3f truth oop %+6.1f | mirrored %+6.1f err %5.1f | final %+6.1f err %5.1f %s\n", T.t[i] * 1e-6, oop(d),
                            oop(rM.shaftDir[i]), err(rM.shaftDir[i]), oop(rBm.shaftDir[i]), err(rBm.shaftDir[i]),
                            (rBm.flags[i] & FlagShaftBranch) ? "FLIPPED" : "");
            }
        }

        // (p-c) ablations from the mirror: neither, plane only (the barrier), both.
        {
            FitInput n0 = inMir; n0.cfg.usePlane = false; n0.cfg.branchPass = false;
            const double p0 = report("(p-c) mirror, no plane, no branch", fitSkeleton(n0));
            FitInput n1 = inMir; n1.cfg.branchPass = false;
            const double p1 = report("(p-c) mirror, plane only", fitSkeleton(n1));
            check(p0 > 30.0, "(p-c) without either, the mirrored club stays mirrored");
            check(p1 > 30.0, "(p-c) the plane alone cannot cross the barrier (it needs the branch pass)");
        }

        // (p-d) the prior's pull: on blind frames where the TRUE shaft is > 15° off the down plane.
        if (rA.planeDown.valid) {
            const V3 n = rA.planeDown.n;
            auto offPlane = [&](size_t i) {
                if (!blind(i)) return false;
                Pose p;
                forwardKinematics(R, T.th[i].data(), T.scale.data(), p);
                const V3 d = p.rot[ybot::LeftHand].rotate(T.gripAxis).unit();
                return std::fabs(std::asin(std::clamp(d.dot(n), -1.0, 1.0))) / kDeg > 15.0;
            };
            const std::vector<double> e = shaftErr(rA, offPlane);
            std::printf("      (p-d) truly off-plane blind frames (> 15°): %zu, shaft error median %.1f° p90 %.1f°\n", e.size(), med(e), p90(e));
        }

        // (p-e) parity: the DTL sees the shaft on EVERY frame ⇒ no blind frame, no term, the same fit.
        {
            FitInput full = inB;
            for (size_t i = 0; i < T.t.size(); ++i) {
                ViewObs &o = full.dtl[i];
                if (std::isfinite(o.shaftTheta)) continue;
                Pose p;
                forwardKinematics(R, T.th[i].data(), T.scale.data(), p);
                const V3 g = markerWorld(p, ybot::LeftHand, T.gripOff), d = p.rot[ybot::LeftHand].rotate(T.gripAxis);
                double u1, v1, u2, v2;
                if (projectPoint(T.cam, 1, T.dtlW, T.dtlH, g, u1, v1) && projectPoint(T.cam, 1, T.dtlW, T.dtlH, g + d * 0.9, u2, v2)) {
                    o.shaftTheta = std::atan2(v2 - v1, u2 - u1);
                    o.shaftSigma = 2.5 * kDeg;
                }
            }
            FitInput off = full; off.cfg.usePlane = false; off.cfg.branchPass = false;
            const FitResult ra = fitSkeleton(full), rb = fitSkeleton(off);
            double worst = 0;
            for (size_t i = 0; i < ra.theta.size(); ++i)
                for (size_t k = 0; k < ra.theta[i].size(); ++k) worst = std::max(worst, std::fabs(ra.theta[i][k] - rb.theta[i][k]));
            std::printf("      (p-e) DTL sees every frame: plane frames %d, worst DoF difference %.2e\n", ra.nPlaneFrames, worst);
            check(ra.nPlaneFrames == 0 && worst < 1e-9, "(p-e) with no blind frame the fit is unchanged");
        }

        // (p-f) face-on only: the catalogue plane.
        {
            FitInput foOff = inFo; foOff.cfg.usePlane = false; foOff.cfg.branchPass = false;
            const FitResult &ron = rFo;
            const FitResult roff = fitSkeleton(foOff);
            auto every = [](size_t) { return true; };
            const std::vector<double> eOn = shaftErr(ron, every), eOff = shaftErr(roff, every);
            std::printf("      (p-f) face-on only: catalogue %.1f° (%s), shaft p90 %.1f° → %.1f° (median %.1f° → %.1f°), kept %d\n",
                        ron.catalogueInclDeg, ron.catalogueUncalibrated ? "uncalibrated" : "calibrated",
                        p90(eOff), p90(eOn), med(eOff), med(eOn), ron.nBranchKept);
            check(ron.planeBack.source == "catalogue" && ron.planeDown.source == "catalogue", "(p-f) a face-on-only swing uses the catalogue plane");
            check(p90(eOn) <= p90(eOff) + 2.0, "(p-f) …and its shaft is no worse for it");
        }

        // The catalogue table.
        const CataloguePlane c7 = clubPlaneInclDeg(0.94), cw = clubPlaneInclDeg(0.89), cm = clubPlaneInclDeg(0.915),
                             cd = clubPlaneInclDeg(1.12), cu = clubPlaneInclDeg(0.0);
        check(std::fabs(c7.inclDeg - 60.3) < 1e-9 && c7.calibrated && std::fabs(cw.inclDeg - 62.8) < 1e-9 && cw.calibrated,
              "catalogue: the measured 7-iron and wedge planes, calibrated");
        check(std::fabs(cm.inclDeg - 61.55) < 1e-9 && std::fabs(cd.inclDeg - 50.0) < 1e-9 && !cd.calibrated && !cu.calibrated,
              "catalogue: interpolated between, typical (uncalibrated) beyond, a 7-iron when unknown");
    }

    // ── (L) the lean rig: 38 unknowns expanding to the rig's 48 (design §13.2 (A)) ──
    std::printf("(L) the lean rig\n");
    {
        Truth TL = T;
        for (auto &th : TL.th) th = leanProject(th);
        // Jacobian through θ = M·q.
        {
            FitInput in = observe(TL, 2.0, 0.02, true, 11, false);
            in.cfg.leanRig = true;
            in.cfg.debugJacobianFrames = 3;
            in.cfg.stage2Iters = 0;
            const FitResult r = fitSkeleton(in);
            std::printf("      lean Jacobian worst %.3e at %s\n", r.debugJacobianErr, r.debugJacobianWorst.c_str());
            check(r.debugJacobianErr < 1e-3, "(L) the analytic Jacobian through θ = M·q agrees with central differences");
        }
        // Started from the lean truth, a zero-iteration fit writes the truth back exactly: the map
        // and its projection are exact for any pose the lean rig can make.
        {
            FitInput in = observe(TL, 2.0, 0.02, true, 11, false);
            in.cfg.leanRig = true;
            in.debugInitTheta = &TL.th;
            in.cfg.stage2Iters = 0;
            in.cfg.usePlane = false; in.cfg.branchPass = false; in.cfg.useGrip = false;
            const FitResult r = fitSkeleton(in);
            double worst = 0;
            for (size_t i = 0; i < TL.th.size() && i < r.theta.size(); ++i)
                for (size_t k = 0; k < TL.th[i].size(); ++k) worst = std::max(worst, std::fabs(r.theta[i][k] - TL.th[i][k]));
            std::printf("      lean round trip: worst angle %.2e rad; rig angles written %zu\n", worst, r.theta.empty() ? 0 : r.theta[0].size());
            check(worst < 1e-9 && !r.theta.empty() && r.theta[0].size() == 48, "(L) θ = M·q reproduces a lean pose exactly, and all 48 angles are written");
        }
        if (std::getenv("SK3D_LEAN_EXPERIMENT")) {
            // Temporary: which part of the lean map causes the lead forearm to roll?
            struct V { const char *name; double kE, kP; int it; };
            for (const V &v : { V { "clavicles locked (0, 0)", 0.0, 0.0, 25 }, V { "gains 0.25, 60 stage-2 iters", 0.25, 0.25, 60 },
                                V { "gains 0.1", 0.1, 0.1, 25 } }) {
                Truth Tv = T;
                for (auto &th : Tv.th) th = leanProject(th, v.kE, v.kP);
                FitInput iv = observe(Tv, 2.0, 0.02, true, 11, true);
                iv.cfg.leanRig = true; iv.cfg.clavElevGain = v.kE; iv.cfg.clavProtGain = v.kP; iv.cfg.stage2Iters = v.it;
                const FitResult rv = fitSkeleton(iv);
                const Score sv = score(Tv, rv, false);
                std::printf("      EXPERIMENT %-30s body dir %.2f° roll %.2f° lFore roll %.2f° pos %.2f cm iters %d\n", v.name,
                            sv.dirBodyP90, sv.rollP90, sv.leadForearmRollP90, sv.posP90Cm, rv.iterations);
            }
        }
        // The (b) guards, on the lean truth.
        FitInput inL = observe(TL, 2.0, 0.02, true, 11, true);
        inL.cfg.leanRig = true;
        const FitResult rL = fitSkeleton(inL);
        check(rL.valid, "(L) the lean fit converged");
        std::printf("      lean fit on the lean truth:\n");
        const Score sL = score(TL, rL, true);
        // Judged against the 48-angle fit on the SAME truth (the plan's "not worse than v2"): the
        // absolute (b) guards were set for the 48-angle fit on a truth the lean rig cannot make.
        FitInput inF = inL; inF.cfg.leanRig = false;
        std::printf("      the 48-angle fit on the lean truth:\n");
        const Score sF = score(TL, fitSkeleton(inF), true);
        check(sL.dirBodyP90 <= sF.dirBodyP90 + 0.5, "(L) body bone direction no worse than the 48-angle fit (+0.5°)");
        check(sL.rollP90 <= sF.rollP90 + 2.0, "(L) roll no worse than the 48-angle fit (+2°; forearm/hand roll is unseen by either)");
        check(sL.leadForearmRollP90 <= 30.0, "(L) lead-forearm roll ≤ 30° p90");
        check(sL.posP90Cm <= 2.0, "(L) root-relative joint position ≤ 2 cm p90");
        if (report) {
            // The model mismatch: the lean fit on the ORIGINAL truth (uneven spine, free clavicle).
            FitInput inM = inB; inM.cfg.leanRig = true;
            std::printf("      lean fit on the ORIGINAL truth (the model-mismatch cost):\n");
            const Score sM = score(T, fitSkeleton(inM), true);
            std::printf("      summary — body dir p90: lean/lean %.2f°, 48/lean %.2f°, lean/original %.2f° (48/original %.2f°)\n",
                        sL.dirBodyP90, sF.dirBodyP90, sM.dirBodyP90, sB.dirBodyP90);
            // Face-on only, lean vs 48, on the lean truth.
            FitInput foL = observe(TL, 2.0, 0.02, false, 11, false);
            FitInput fo48 = foL;
            foL.cfg.leanRig = true;
            std::printf("      face-on only — lean:\n");
            const Score fL = score(TL, fitSkeleton(foL), true);
            std::printf("      face-on only — 48:\n");
            const Score f48 = score(TL, fitSkeleton(fo48), true);
            std::printf("      face-on only body dir p90: lean %.2f° vs 48 %.2f°\n", fL.dirBodyP90, f48.dirBodyP90);
        }
    }

    // ── (S) spline trajectories on the lean rig (design §13.2 (B)) ──
    std::printf("(S) spline trajectories\n");
    {
        const Rig &R = rig();
        Truth TL = T;
        for (auto &th : TL.th) th = leanProject(th);
        // The club's peak angular speed through impact (rad/s), fit vs truth: the risk that knots
        // round the fast peaks (§13.4).
        auto clubPeak = [&](const std::vector<V3> &dirs) {
            double pk = 0;
            for (size_t i = 1; i < dirs.size() && i < TL.t.size(); ++i) {
                if (std::llabs(TL.t[i] - TL.impactUs) > 80000) continue;
                const double dt = (TL.t[i] - TL.t[i - 1]) * 1e-6;
                pk = std::max(pk, std::acos(std::clamp(dirs[i].unit().dot(dirs[i - 1].unit()), -1.0, 1.0)) / dt);
            }
            return pk;
        };
        std::vector<V3> truthDirs;
        for (size_t i = 0; i < TL.t.size(); ++i) {
            Pose p;
            forwardKinematics(R, TL.th[i].data(), TL.scale.data(), p);
            truthDirs.push_back(p.rot[ybot::LeftHand].rotate(TL.gripAxis).unit());
        }
        // Round trip: the truth as a spline, written back, stage 2 not run.
        {
            FitInput in = observe(TL, 2.0, 0.02, true, 11, false);
            in.cfg.leanRig = true; in.cfg.splineBasis = true;
            in.debugInitTheta = &TL.th;
            in.cfg.stage2Iters = 0; in.cfg.usePlane = false; in.cfg.branchPass = false; in.cfg.useGrip = false;
            const FitResult r = fitSkeleton(in);
            double worst = 0;
            size_t wi = 0, wk = 0;
            std::vector<double> all;
            for (size_t i = 0; i < TL.th.size() && i < r.theta.size(); ++i)
                for (size_t k = 0; k < TL.th[i].size(); ++k) {
                    const double e = std::fabs(r.theta[i][k] - TL.th[i][k]);
                    all.push_back(e / kDeg);
                    if (e > worst) { worst = e; wi = i; wk = k; }
                }
            std::printf("      spline round trip of the truth: p90 %.3f°, worst %.2f° (%s at %.3f s), unknowns %d (frames %zu)\n",
                        p90(all), worst / kDeg, rig().dofs[wk].name, TL.t[wi] * 1e-6, r.nUnknowns, TL.t.size());
            // The generator's truth is only C¹ (smoothstep keys, per-frame leg IK): a worst single
            // angle is where it kinks. The knots carry the swing when the p90 is well under a degree.
            check(p90(all) < 0.5, "(S) the knots carry the truth: round trip p90 < 0.5°");
        }
        // Jacobian (frame level, through θ = M·q) with the spline state.
        {
            FitInput in = observe(TL, 2.0, 0.02, true, 11, false);
            in.cfg.leanRig = true; in.cfg.splineBasis = true;
            in.cfg.debugJacobianFrames = 3; in.cfg.stage2Iters = 0;
            const FitResult r = fitSkeleton(in);
            check(r.debugJacobianErr < 1e-3, "(S) the analytic Jacobian agrees with central differences");
        }
        FitInput inS = observe(TL, 2.0, 0.02, true, 11, true);
        inS.cfg.leanRig = true;
        FitInput inN = inS;                       // lean, per-frame
        inS.cfg.splineBasis = true;
        const FitResult rS = fitSkeleton(inS), rN = fitSkeleton(inN);
        check(rS.valid, "(S) the spline fit converged");
        std::printf("      lean + spline:\n");
        const Score sS = score(TL, rS, true);
        std::printf("      lean, per frame:\n");
        const Score sN = score(TL, rN, true);
        std::printf("      unknowns: lean + spline %d, lean per-frame %d\n", rS.nUnknowns, rN.nUnknowns);
        check(rS.nUnknowns < rN.nUnknowns / 2, "(S) splines at least halve the unknowns");
        check(sS.dirBodyP90 <= sN.dirBodyP90 + 0.5, "(S) body bone direction no worse than per-frame (+0.5°)");
        check(sS.rollP90 <= sN.rollP90 + 2.0, "(S) roll no worse than per-frame (+2°)");
        check(sS.posP90Cm <= sN.posP90Cm + 0.3, "(S) joint position no worse than per-frame (+0.3 cm)");
        const double pkT = clubPeak(truthDirs), pkS = clubPeak(rS.shaftDir), pkN = clubPeak(rN.shaftDir);
        std::printf("      club peak angular speed through impact: truth %.1f rad/s, spline %.1f (%.0f%%), per-frame %.1f (%.0f%%)\n",
                    pkT, pkS, 100 * pkS / pkT, pkN, 100 * pkN / pkT);
        check(pkS >= 0.9 * pkT, "(S) the knots keep ≥ 90 % of the club's true peak speed through impact");
    }

    // ── (C) a session pool's values are held fixed ──
    std::printf("(C) a fixed session calib\n");
    {
        SkeletonCalib cb;
        cb.hasScale = true; cb.scale.fill(1.0); cb.scale[GUpperArm] = 1.05;
        cb.clubToHeadM = 0.88;
        FitInput in = inB;
        in.fixedCalib = &cb;
        const FitResult r = fitSkeleton(in);
        std::printf("      upper-arm scale %.4f (fixed 1.05), club %.4f m (fixed 0.92)\n", r.scale[GUpperArm], r.clubLengthM);
        check(r.valid && r.calibFixed && std::fabs(r.scale[GUpperArm] - 1.05) < 1e-12 && std::fabs(r.clubLengthM - 0.92) < 1e-12,
              "(C) a session calib's values are held fixed through the fit");
    }

    // ── (G) the grounded club levels the world ─────────────────────────────────────────────────
    // A face-on camera pitched p used to tilt the fitted world by ~p/2: nothing measures gravity,
    // the planted feet are too short a baseline, and the pitch prior pulls to level. A club
    // grounded at address touches the floor half a metre in front of the feet, and that is a
    // baseline. Same golfer, same observations, the term off and on.
    std::printf("(G) the grounded club levels the world: face-on camera pitched 6°\n");
    {
        Truth Tg = makeSwing(120.0, unit, 10.0);
        Tg.cam.pF = 6 * kDeg;
        FitInput off = observe(Tg, 2.0, 0.02, true, 11, false, true);
        const double clubM = off.clubLengthM;
        off.cfg.groundedClubSigmaM = 0;
        const FitInput on = observe(Tg, 2.0, 0.02, true, 11, false, true);
        const FitResult rOff = fitSkeleton(off), rOn = fitSkeleton(on);
        const double eOff = std::fabs(rOff.cam.pF - Tg.cam.pF) / kDeg, eOn = std::fabs(rOn.cam.pF - Tg.cam.pF) / kDeg;
        std::printf("      club %.3f m; face-on pitch error: term off %.2f°, on %.2f°; floor off %.3f, on %.3f (truth %.3f)\n",
                    clubM, eOff, eOn, rOff.cam.zG, rOn.cam.zG, Tg.cam.zG);
        check(rOff.valid && rOn.valid, "(G) both fits converged");
        check(eOff > 1.5, "(G) without it the world tilts with the camera's pitch");
        check(eOn < 1.0, "(G) with it the face-on pitch is recovered within 1°");
        check(std::fabs(rOn.cam.zG - Tg.cam.zG) < 0.02, "(G) …and the floor within 2 cm");
    }

    // (DS) A thinned DTL pass (pose_inference_performance_plan.md step 4, schedule A at 150 fps):
    // stride 4 through the address hold (26.7 ms gaps), stride 2 in the backswing (13.3 ms), every
    // frame from the downswing. The face-on poses stride 4 before the downswing too, on the other
    // phase. With the fixed 12 ms bracket / 6 ms nearest rule no DTL frame pairs with ANY face-on
    // instant inside address ± 150 ms — the fit's DTL initialiser finds no hips there and drops the
    // camera (4 of 21 corpus swings on 6 Oct). With the bracket following the DTL's local spacing
    // every reference instant interpolates and the camera stays.
    std::printf("(DS) a schedule-thinned DTL track: 27 ms address gaps, 13 ms backswing gaps\n");
    {
        using pinpoint::analysis::BracketRule;
        using pinpoint::analysis::bracketAt;
        const Truth Ts = makeSwing(150.0, unit, 10.0);
        const FitInput full = observe(Ts, 2.0, 0.0, true, 11, false);
        std::vector<size_t> foIdx, dtIdx;
        for (size_t i = 0; i < Ts.t.size(); ++i) {
            const int64_t t = Ts.t[i];
            if (t >= 900000 || i % 4 == 0) foIdx.push_back(i);
            if (t >= 900000 || (t >= 460000 ? i % 2 == 1 : i % 4 == 2)) dtIdx.push_back(i);
        }
        auto build = [&](const BracketRule &rule, int &refInstants, int &refWithHips) {
            FitInput in = full;
            in.t_us.clear(); in.fo.clear(); in.dtl.clear(); in.footContact.clear();
            refInstants = refWithHips = 0;
            for (size_t i : foIdx) {
                const int64_t t = Ts.t[i];
                in.t_us.push_back(t);
                in.fo.push_back(full.fo[i]);
                in.footContact.push_back(full.footContact[i]);
                ViewObs vd;
                const pinpoint::analysis::Bracket br =
                    bracketAt(dtIdx.size(), [&](size_t k) { return Ts.t[dtIdx[k]]; }, t, rule);
                if (br.ok) {
                    const ViewObs &A = full.dtl[dtIdx[br.a]], &B = full.dtl[dtIdx[br.b]];
                    for (int m = 0; m < kMarkerCount; ++m) {
                        const KpObs &a = A.kp[size_t(m)], &b = B.kp[size_t(m)];
                        if (a.sigma <= 0 || b.sigma <= 0) continue;
                        vd.kp[size_t(m)] = { a.u + br.w * (b.u - a.u), a.v + br.w * (b.v - a.v), std::max(a.sigma, b.sigma) };
                    }
                    const ViewObs &nb = br.w < 0.5 ? A : B;
                    vd.shaftTheta = nb.shaftTheta; vd.shaftSigma = nb.shaftSigma;
                }
                if (std::llabs(t - Ts.addressUs) <= 150000) {
                    ++refInstants;
                    if (vd.kp[11].sigma > 0 && vd.kp[12].sigma > 0) ++refWithHips;
                }
                in.dtl.push_back(vd);
            }
            return in;
        };
        BracketRule fixedRule, localRule;
        localRule.localGap = true;
        int nRefF = 0, hipsF = 0, nRefL = 0, hipsL = 0;
        const FitInput inF = build(fixedRule, nRefF, hipsF);
        const FitInput inL = build(localRule, nRefL, hipsL);
        std::printf("      %zu face-on instants, %zu DTL frames; address ± 150 ms: %d instants, DTL hips at %d (12 ms rule) / %d (local gap)\n",
                    foIdx.size(), dtIdx.size(), nRefF, hipsF, hipsL);
        check(nRefF >= 3 && hipsF == 0, "(DS) the fixed 12 / 6 ms rule pairs no DTL frame at the address instants");
        check(nRefL >= 3 && hipsL == nRefL, "(DS) the local-gap bracket gives DTL hips at every address instant");
        const FitResult rF = fitSkeleton(inF), rL = fitSkeleton(inL);
        std::printf("      fixed rule: valid %d, DTL used %d (\"%s\"); local gap: valid %d, DTL used %d, reprojection %.2f / %.2f px\n",
                    int(rF.valid), int(rF.dtlUsed), rF.dtlDropReason.c_str(), int(rL.valid), int(rL.dtlUsed),
                    rL.reprojMedPxFo, rL.reprojMedPxDtl);
        check(!rF.dtlUsed && !rF.dtlDropReason.empty(), "(DS) fixed rule: the fit drops the DTL camera and says why");
        check(rL.valid && rL.dtlUsed && rL.dtlDropReason.empty(), "(DS) local gap: the fit keeps the DTL camera");
    }

    // Timing at a real cadence.
    if (report) {
        std::printf("timing\n");
        const Truth T2 = makeSwing(240.0, unit, 10.0);
        FitInput in = observe(T2, 2.0, 0.02, true, 17, false);
        const FitResult r = fitSkeleton(in);
        std::printf("      %zu frames: %.0f ms, %d iterations\n", T2.t.size(), r.ms, r.iterations);
    }

    std::printf("=== %s (%d failure%s) ===\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
