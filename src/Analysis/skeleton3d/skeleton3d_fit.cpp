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

// skeleton3d fit — see skeleton3d_fit.h and docs/design/swing_3d_viz_design.md §3.
//
// ── The solve ────────────────────────────────────────────────────────────────
// Unknowns: per frame, the rig's DoFs (skeleton3d_rig.h, 48); shared, the length
// scales, the surface-marker offsets, the camera geometry, the floor, the grip, the
// planted-foot anchors, IMU mounts/headings and the HackMotion zero offsets.
//
// Levenberg–Marquardt on the normal equations. The frame block is BLOCK-BANDED: frames
// only meet through the second-difference smoothness term, so frame t couples to t±1
// and t±2 and nothing else. It is factored by a block-banded Cholesky (48×48 dense
// blocks, bandwidth 2); the shared block is eliminated by a Schur complement. That is
// linear in the frame count — no general sparse solver, no fill beyond the band.
//
// ── The Jacobian ────────────────────────────────────────────────────────────
// Exact and cheap because every DoF is a scalar about a known axis: a point p on the
// chain moves  axisW × (p − pivot)  per radian of a rotation DoF, and along the axis for
// a root translation. Length scales translate every descendant by rot(parent)·restT.
// Only camera parameters (few, and only in the projection) are differenced numerically.
//
// ── Two stages ───────────────────────────────────────────────────────────────
// 1. Fit the rig to a WEAK-PERSPECTIVE TRIANGULATION of the keypoints (3-D targets),
//    everything shared held fixed. Convex enough to start anywhere near address.
// 2. The real objective: reprojection in each view through perspective cameras
//    (robust), shaft angles, both hands on one club, planted feet, smoothness, limits,
//    priors, IMUs, HackMotion — shared unknowns free.

#include "skeleton3d_fit.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <numeric>

#include <Eigen/Cholesky>
#include <Eigen/Dense>

namespace pinpoint::skeleton3d {

namespace {

using Eigen::MatrixXd;
using Eigen::VectorXd;
using Mat3X = Eigen::Matrix<double, 3, Eigen::Dynamic>;

inline Eigen::Vector3d E(const V3 &v) { return { v.x, v.y, v.z }; }
inline V3 F(const Eigen::Vector3d &v) { return { v.x(), v.y(), v.z() }; }

double median(std::vector<double> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return kNaN;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

double percentile(std::vector<double> v, double q)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

double wrapPi(double a)
{
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

Q rotvecToQ(const V3 &r)
{
    const double a = r.norm();
    return a < 1e-12 ? Q {} : Q::axisAngle(r, a);
}

V3 qToRotvec(const Q &qin)
{
    Q q = qin.normalized();
    if (q.w < 0) q = { -q.w, -q.x, -q.y, -q.z };
    const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-12) return { 2 * q.x, 2 * q.y, 2 * q.z };
    const double a = 2.0 * std::atan2(s, q.w);
    return V3 { q.x, q.y, q.z } * (a / s);
}

// ── cameras ──────────────────────────────────────────────────────────────────
struct CamFrame { V3 c, right, down, fwd; double f; double cx, cy; };

CamFrame camFrame(const Cameras &cm, int view, int W, int H)
{
    CamFrame cf;
    cf.cx = 0.5 * W;
    cf.cy = 0.5 * H;
    if (view == 0) {
        const double p = cm.pF;
        cf.c = {};
        cf.fwd = { 0, std::cos(p), -std::sin(p) };
        const V3 up { 0, std::sin(p), std::cos(p) };
        cf.right = { 1, 0, 0 };
        cf.down = -up;
        cf.f = cm.fF;
    } else {
        const V3 f0 { std::cos(cm.psiD), std::sin(cm.psiD), 0 };
        const V3 Z { 0, 0, 1 };
        cf.c = cm.cD;
        cf.fwd = f0 * std::cos(cm.pD) - Z * std::sin(cm.pD);
        const V3 up = f0 * std::sin(cm.pD) + Z * std::cos(cm.pD);
        cf.right = f0.cross(Z);
        cf.down = -up;
        cf.f = cm.fD;
    }
    // Roll about the optical axis: a camera tilted on its mount does not see verticals as vertical.
    const double r = view == 0 ? cm.rF : cm.rD;
    if (r != 0.0) {
        const V3 R0 = cf.right, D0 = cf.down;
        cf.right = R0 * std::cos(r) + D0 * std::sin(r);
        cf.down = D0 * std::cos(r) - R0 * std::sin(r);
    }
    return cf;
}

// Projection + ∂(u,v)/∂p. False when the point is not in front of the camera.
bool project(const CamFrame &cf, const V3 &p, double &u, double &v, Eigen::Matrix<double, 2, 3> *J)
{
    const V3 rel = p - cf.c;
    const double x = rel.dot(cf.right), y = rel.dot(cf.down), z = rel.dot(cf.fwd);
    if (z < 0.05) return false;
    u = cf.cx + cf.f * x / z;
    v = cf.cy + cf.f * y / z;
    if (J) {
        const double iz = 1.0 / z, iz2 = iz * iz;
        for (int k = 0; k < 3; ++k) {
            (*J)(0, k) = cf.f * (cf.right[k] * iz - x * cf.fwd[k] * iz2);
            (*J)(1, k) = cf.f * (cf.down[k] * iz - y * cf.fwd[k] * iz2);
        }
    }
    return true;
}

// ── shared parameter layout ──────────────────────────────────────────────────
struct Layout {
    int iScale = 0, iOff = 0, iSym = 0, iCam = 0, iZg = 0, iGrip = 0, iClub = 0, iAnchor = 0, iImu = 0, iHm = 0, n = 0;
    std::array<int, kMarkerCount> offSlot {};
    int nImu = 0;
};

constexpr int kCamParams = 10;  // fF pF cDx cDy cDz psiD pD fD rF rD

Cameras camFromSv(const VectorXd &sv, const Layout &L)
{
    Cameras c;
    c.fF = sv[L.iCam + 0]; c.pF = sv[L.iCam + 1];
    c.cD = { sv[L.iCam + 2], sv[L.iCam + 3], sv[L.iCam + 4] };
    c.psiD = sv[L.iCam + 5]; c.pD = sv[L.iCam + 6]; c.fD = sv[L.iCam + 7];
    c.rF = sv[L.iCam + 8]; c.rD = sv[L.iCam + 9];
    c.zG = sv[L.iZg];
    return c;
}

// A point that rides on the chain, with its derivatives: 3×nd over the frame's DoFs,
// 3×ns over the shared unknowns.
struct PJ {
    V3 p;
    Mat3X dth, ds;
};

struct Problem {
    const FitInput &in;
    const Rig &rig;
    FitConfig cfg;
    int T = 0, nd = 0;
    bool hasDtl = false;
    Layout L;
    std::vector<ViewObs> fo, dtl;           // working copies (label swaps are applied here)
    std::vector<uint8_t> swapFo, swapDtl;
    std::vector<double> dtS;                // per frame: seconds to the next frame
    // Stage-1 targets.
    std::vector<std::array<V3, kMarkerCount>> tgt;
    std::vector<std::array<V3, kMarkerCount>> tgtSig;   // per-axis σ; x ≤ 0 = no target
    // Priors.
    VectorXd svPrior, svSigma;              // svSigma ≤ 0 ⇒ no prior
    std::vector<uint8_t> svFree;            // per shared index: in the solve
    bool stage1 = true;
    // HackMotion / IMU frame data present.
    bool hasHm = false;
    // The fast-motion window (smoothness loosened).
    int64_t fastFrom = 0, fastTo = 0;
    double s0 = 1.0;                        // global body scale
    double clubToHeadM = 0.9;               // lead grip point → clubhead centre (m)
    // Per-frame DoF priors: (θ_a − θ_b − mean) / σ, b = −1 for an absolute prior.
    struct DofPrior { int a, b; double mean, sigma; };
    std::vector<DofPrior> dofPriors;

    explicit Problem(const FitInput &i) : in(i), rig(skeleton3d::rig()), cfg(i.cfg) {}
};

struct State {
    std::vector<VectorXd> th;
    VectorXd sv;
};

struct Lin {
    std::vector<MatrixXd> D, U1, U2, B;
    std::vector<VectorXd> gf;
    MatrixXd C;
    VectorXd gs;
    double cost = 0;
};

// Row accumulator for one frame.
struct Rows {
    MatrixXd Jf, Js;
    VectorXd r;
    int n = 0;
    void reset(int maxRows, int nd, int ns)
    {
        if (Jf.rows() < maxRows || Jf.cols() != nd) Jf.resize(maxRows, nd);
        if (Js.rows() < maxRows || Js.cols() != ns) Js.resize(maxRows, ns);
        if (r.size() < maxRows) r.resize(maxRows);
        Jf.setZero(); Js.setZero(); r.setZero();
        n = 0;
    }
    int add()
    {
        if (n >= r.size()) {
            const int m = int(r.size()) * 2 + 16;
            Jf.conservativeResize(m, Eigen::NoChange); Jf.bottomRows(m - n).setZero();
            Js.conservativeResize(m, Eigen::NoChange); Js.bottomRows(m - n).setZero();
            r.conservativeResize(m); r.tail(m - n).setZero();
        }
        return n++;
    }
};

// ── point Jacobians ──────────────────────────────────────────────────────────
void markerPJ(const Problem &P, const Pose &pose, const VectorXd &sv, int joint, const V3 &offLocal,
              int offSharedIndex, PJ &out)
{
    const Rig &R = P.rig;
    out.p = markerWorld(pose, joint, offLocal);
    out.dth.setZero(3, P.nd);
    out.ds.setZero(3, P.L.n);
    const uint64_t chain = R.ancestorsOrSelf[joint];
    for (int k = 0; k < P.nd; ++k) {
        const Dof &d = R.dofs[size_t(k)];
        if (!((chain >> d.joint) & 1u)) continue;
        const V3 g = d.kind == DofKind::RootTrans ? pose.dofAxisW[size_t(k)]
                                                   : pose.dofAxisW[size_t(k)].cross(out.p - pose.dofPivotW[size_t(k)]);
        out.dth.col(k) = E(g);
    }
    (void)sv;
    for (int i = joint; i >= 0; i = R.parent[i]) {
        const int par = R.parent[i];
        if (par < 0) break;
        const int g = jointGroup(i);
        if (g < 0) continue;
        out.ds.col(P.L.iScale + g) += E(pose.rot[par].rotate(R.restT[i]));
    }
    if (offSharedIndex >= 0)
        for (int c = 0; c < 3; ++c)
            out.ds.col(offSharedIndex + c) = E(pose.rot[joint].rotate(V3 { c == 0 ? 1.0 : 0, c == 1 ? 1.0 : 0, c == 2 ? 1.0 : 0 }));
}

// A world direction d = rot(joint)·a — derivatives over DoFs (axis × d).
void dirDerivs(const Problem &P, const Pose &pose, int joint, const V3 &d, Mat3X &dth)
{
    dth.setZero(3, P.nd);
    const uint64_t chain = P.rig.ancestorsOrSelf[joint];
    for (int k = 0; k < P.nd; ++k) {
        const Dof &df = P.rig.dofs[size_t(k)];
        if (!((chain >> df.joint) & 1u) || df.kind == DofKind::RootTrans) continue;
        dth.col(k) = E(pose.dofAxisW[size_t(k)].cross(d));
    }
}

V3 markerOffset(const Problem &P, const VectorXd &sv, int m)
{
    const Marker &M = P.rig.markers[size_t(m)];
    const int slot = P.L.offSlot[size_t(m)];
    V3 o = slot < 0 ? M.offsetPrior * P.s0 : V3 { sv[slot], sv[slot + 1], sv[slot + 2] };
    if (M.symGroup >= 0)
        o += M.latLocal * (M.side * sv[P.L.iSym + 2 * M.symGroup]) + M.upLocal * sv[P.L.iSym + 2 * M.symGroup + 1];
    return o;
}

double scaleOf(const VectorXd &sv, const Layout &L, int g) { return sv[L.iScale + g]; }

// ── the residuals of ONE frame ───────────────────────────────────────────────
// Returns the frame's robust cost; fills rows when `rows` is non-null.
double frameResiduals(const Problem &P, const State &S, int t, Rows *rows)
{
    const Rig &R = P.rig;
    const VectorXd &th = S.th[size_t(t)];
    const VectorXd &sv = S.sv;
    std::array<double, GroupCount> sc {};
    for (int g = 0; g < GroupCount; ++g) sc[size_t(g)] = scaleOf(sv, P.L, g);
    Pose pose;
    forwardKinematics(R, th.data(), sc.data(), pose);
    const Cameras cam = camFromSv(sv, P.L);
    const double c2 = P.cfg.cauchyC * P.cfg.cauchyC;
    double cost = 0;
    PJ pj;

    // Numeric camera derivatives: the perturbed camera frames, built once per frame.
    struct CamPert { int idx; double h; CamFrame plus, minus; };
    std::array<std::vector<CamPert>, 2> pert;
    if (rows && P.cfg.fitCameras && !P.stage1) {
        for (int view = 0; view < (P.hasDtl ? 2 : 1); ++view) {
            const int W = view == 0 ? P.in.foW : P.in.dtlW, H = view == 0 ? P.in.foH : P.in.dtlH;
            static const std::vector<int> foCams { 0, 1, 8 }, dtlCams { 2, 3, 4, 5, 6, 7, 9 };
            for (int c : view == 0 ? foCams : dtlCams) {
                const int idx = P.L.iCam + c;
                if (!P.svFree[size_t(idx)]) continue;
                const bool angle = c == 1 || c == 5 || c == 6 || c == 8 || c == 9;
                const double h = angle ? 1e-5 : std::max(1e-5, std::fabs(sv[idx]) * 1e-6);
                VectorXd sp = sv, sm = sv;
                sp[idx] += h; sm[idx] -= h;
                pert[size_t(view)].push_back({ idx, h, camFrame(camFromSv(sp, P.L), view, W, H),
                                               camFrame(camFromSv(sm, P.L), view, W, H) });
            }
        }
    }
    auto addCamNumeric = [&](int row, int view, const std::function<double(const CamFrame &)> &fn, double weight) {
        for (const CamPert &cp : pert[size_t(view)]) {
            const double fp = fn(cp.plus), fm = fn(cp.minus);
            if (std::isfinite(fp) && std::isfinite(fm)) rows->Js(row, cp.idx) += weight * (fp - fm) / (2 * cp.h);
        }
    };

    if (P.stage1) {
        // 3-D targets.
        for (int m = 0; m < kMarkerCount; ++m) {
            const V3 &sg = P.tgtSig[size_t(t)][size_t(m)];
            if (sg.x <= 0) continue;
            const Marker &M = R.markers[size_t(m)];
            const V3 off = markerOffset(P, sv, m);
            if (rows) markerPJ(P, pose, sv, M.joint, off, -1, pj);
            else pj.p = markerWorld(pose, M.joint, off);
            const V3 d = pj.p - P.tgt[size_t(t)][size_t(m)];
            for (int a = 0; a < 3; ++a) {
                const double s = a == 0 ? sg.x : a == 1 ? sg.y : sg.z;
                const double r = d[a] / s;
                cost += r * r;
                if (rows) {
                    const int row = rows->add();
                    rows->r[row] = r;
                    rows->Jf.row(row) = pj.dth.row(a) / s;
                }
            }
        }
    } else {
        // ── reprojection, each view ──
        const int nViews = P.hasDtl ? 2 : 1;
        for (int view = 0; view < nViews; ++view) {
            const ViewObs &ob = view == 0 ? P.fo[size_t(t)] : P.dtl[size_t(t)];
            const int W = view == 0 ? P.in.foW : P.in.dtlW, H = view == 0 ? P.in.foH : P.in.dtlH;
            const CamFrame cf = camFrame(cam, view, W, H);
            for (int m = 0; m < kMarkerCount; ++m) {
                const KpObs &o = ob.kp[size_t(m)];
                if (o.sigma <= 0) continue;
                const Marker &M = R.markers[size_t(m)];
                const V3 off = markerOffset(P, sv, m);
                if (rows) {
                    markerPJ(P, pose, sv, M.joint, off, P.L.offSlot[size_t(m)], pj);
                    if (M.symGroup >= 0) {
                        pj.ds.col(P.L.iSym + 2 * M.symGroup) += E(pose.rot[M.joint].rotate(M.latLocal * M.side));
                        pj.ds.col(P.L.iSym + 2 * M.symGroup + 1) += E(pose.rot[M.joint].rotate(M.upLocal));
                    }
                } else {
                    pj.p = markerWorld(pose, M.joint, off);
                }
                double u, v;
                Eigen::Matrix<double, 2, 3> Jp;
                if (!project(cf, pj.p, u, v, rows ? &Jp : nullptr)) { cost += c2 * 10.0; continue; }
                const double ru = (u - o.u) / o.sigma, rv = (v - o.v) / o.sigma;
                const double s2 = ru * ru + rv * rv;
                cost += c2 * std::log1p(s2 / c2);
                if (!rows) continue;
                const double w = std::sqrt(1.0 / (1.0 + s2 / c2));
                for (int a = 0; a < 2; ++a) {
                    const int row = rows->add();
                    rows->r[row] = w * (a == 0 ? ru : rv);
                    rows->Jf.row(row) = w / o.sigma * (Jp.row(a) * pj.dth);
                    rows->Js.row(row) = w / o.sigma * (Jp.row(a) * pj.ds);
                    const V3 pp = pj.p;
                    addCamNumeric(row, view, [&](const CamFrame &c) {
                        double uu, vv;
                        if (!project(c, pp, uu, vv, nullptr)) return kNaN;
                        return a == 0 ? uu : vv;
                    }, w / o.sigma);
                }
            }

            // ── shaft image angle ──
            if (P.cfg.useShaft && std::isfinite(ob.shaftTheta) && ob.shaftSigma > 0) {
                const int side = P.in.leadIsLeft ? 0 : 1;
                const int hand = P.in.leadIsLeft ? ybot::LeftHand : ybot::RightHand;
                (void)side;
                const V3 gOff { sv[P.L.iGrip + 3], sv[P.L.iGrip + 4], sv[P.L.iGrip + 5] };
                const V3 aRaw { sv[P.L.iGrip + 0], sv[P.L.iGrip + 1], sv[P.L.iGrip + 2] };
                const double an = std::max(1e-6, aRaw.norm());
                const V3 ah = aRaw * (1.0 / an);
                PJ g;
                markerPJ(P, pose, sv, hand, gOff, P.L.iGrip + 3, g);
                const V3 d = pose.rot[hand].rotate(ah);
                constexpr double kLen = 0.4;
                const V3 h = g.p + d * kLen;
                double ug, vg, uh, vh;
                Eigen::Matrix<double, 2, 3> Jg, Jh;
                if (project(cf, g.p, ug, vg, &Jg) && project(cf, h, uh, vh, &Jh)) {
                    const double du = uh - ug, dv = vh - vg, n2 = du * du + dv * dv;
                    if (n2 > 1.0) {
                        const double thp = std::atan2(dv, du);
                        const double r0 = wrapPi(thp - ob.shaftTheta) / ob.shaftSigma;
                        const double s2 = r0 * r0;
                        cost += c2 * std::log1p(s2 / c2);
                        if (rows) {
                            const double w = std::sqrt(1.0 / (1.0 + s2 / c2));
                            // ∂θ/∂(du,dv)
                            const double tu = -dv / n2, tv = du / n2;
                            Eigen::RowVector3d dThdH = tu * Jh.row(0) + tv * Jh.row(1);
                            Eigen::RowVector3d dThdG = -(tu * Jg.row(0) + tv * Jg.row(1));
                            Mat3X dd;
                            dirDerivs(P, pose, hand, d, dd);
                            const int row = rows->add();
                            rows->r[row] = w * r0;
                            rows->Jf.row(row) = w / ob.shaftSigma * ((dThdG + dThdH) * g.dth + kLen * dThdH * dd);
                            Eigen::RowVectorXd js = (dThdG + dThdH) * g.ds;
                            // ∂d/∂a (normalised axis)
                            const Eigen::Matrix3d Rh = [&] {
                                Eigen::Matrix3d m;
                                for (int c = 0; c < 3; ++c)
                                    m.col(c) = E(pose.rot[hand].rotate(V3 { c == 0 ? 1.0 : 0, c == 1 ? 1.0 : 0, c == 2 ? 1.0 : 0 }));
                                return m;
                            }();
                            const Eigen::Vector3d aE = E(ah);
                            const Eigen::Matrix3d dA = Rh * (Eigen::Matrix3d::Identity() - aE * aE.transpose()) / an;
                            js.segment(P.L.iGrip, 3) += kLen * dThdH * dA;
                            rows->Js.row(row) = w / ob.shaftSigma * js;
                            const V3 gp = g.p;
                            const double thObs = ob.shaftTheta;
                            addCamNumeric(row, view, [&](const CamFrame &f2) {
                                double a1, b1, a2, b2;
                                if (!project(f2, gp, a1, b1, nullptr) || !project(f2, h, a2, b2, nullptr)) return kNaN;
                                return wrapPi(std::atan2(b2 - b1, a2 - a1) - thObs);
                            }, w / ob.shaftSigma);
                        }
                    }
                }
            }
        }

        // ── the measured clubhead ──
        for (int view = 0; view < nViews && P.cfg.useClubhead; ++view) {
            const ViewObs &ob = view == 0 ? P.fo[size_t(t)] : P.dtl[size_t(t)];
            if (ob.headSigma <= 0) continue;
            const int W = view == 0 ? P.in.foW : P.in.dtlW, H = view == 0 ? P.in.foH : P.in.dtlH;
            const CamFrame cf = camFrame(cam, view, W, H);
            const int hand = P.in.leadIsLeft ? ybot::LeftHand : ybot::RightHand;
            const V3 gOff { sv[P.L.iGrip + 3], sv[P.L.iGrip + 4], sv[P.L.iGrip + 5] };
            const V3 aRaw { sv[P.L.iGrip + 0], sv[P.L.iGrip + 1], sv[P.L.iGrip + 2] };
            const double an = std::max(1e-6, aRaw.norm());
            const V3 ah = aRaw * (1.0 / an);
            const double Lh = sv[P.L.iClub];
            PJ g;
            markerPJ(P, pose, sv, hand, gOff, P.L.iGrip + 3, g);
            const V3 d = pose.rot[hand].rotate(ah);
            const V3 hp = g.p + d * Lh;
            double u, v;
            Eigen::Matrix<double, 2, 3> Jp;
            if (!project(cf, hp, u, v, &Jp)) continue;
            const double ru = (u - ob.headU) / ob.headSigma, rv = (v - ob.headV) / ob.headSigma;
            const double s2 = ru * ru + rv * rv;
            cost += c2 * std::log1p(s2 / c2);
            if (!rows) continue;
            const double w = std::sqrt(1.0 / (1.0 + s2 / c2));
            Mat3X dd;
            dirDerivs(P, pose, hand, d, dd);
            Eigen::Matrix3d Rh;
            for (int c = 0; c < 3; ++c)
                Rh.col(c) = E(pose.rot[hand].rotate(V3 { c == 0 ? 1.0 : 0, c == 1 ? 1.0 : 0, c == 2 ? 1.0 : 0 }));
            const Eigen::Vector3d aE = E(ah);
            const Eigen::Matrix3d dA = Rh * (Eigen::Matrix3d::Identity() - aE * aE.transpose()) / an;
            const Mat3X dth = g.dth + Lh * dd;
            Mat3X ds = g.ds;
            ds.middleCols(P.L.iGrip, 3) += Lh * dA;
            for (int a = 0; a < 2; ++a) {
                const int row = rows->add();
                rows->r[row] = w * (a == 0 ? ru : rv);
                rows->Jf.row(row) = w / ob.headSigma * (Jp.row(a) * dth);
                rows->Js.row(row) = w / ob.headSigma * (Jp.row(a) * ds);
                rows->Js(row, P.L.iClub) += w / ob.headSigma * Jp.row(a).dot(E(d));
                addCamNumeric(row, view, [&](const CamFrame &c) {
                    double uu, vv;
                    if (!project(c, hp, uu, vv, nullptr)) return kNaN;
                    return a == 0 ? uu : vv;
                }, w / ob.headSigma);
            }
        }

        const int64_t tus = P.in.t_us[size_t(t)];
        // ── both hands on one club ──
        if (P.cfg.useGrip && tus >= P.in.addressUs - 50000 && tus <= P.in.impactUs + 60000) {
            const int lead = P.in.leadIsLeft ? ybot::LeftHand : ybot::RightHand;
            const int trail = P.in.leadIsLeft ? ybot::RightHand : ybot::LeftHand;
            const V3 gOff { sv[P.L.iGrip + 3], sv[P.L.iGrip + 4], sv[P.L.iGrip + 5] };
            const V3 tOff { sv[P.L.iGrip + 6], sv[P.L.iGrip + 7], sv[P.L.iGrip + 8] };
            const V3 aRaw { sv[P.L.iGrip + 0], sv[P.L.iGrip + 1], sv[P.L.iGrip + 2] };
            const double an = std::max(1e-6, aRaw.norm());
            const V3 ah = aRaw * (1.0 / an);
            PJ g, q;
            markerPJ(P, pose, sv, lead, gOff, P.L.iGrip + 3, g);
            markerPJ(P, pose, sv, trail, tOff, P.L.iGrip + 6, q);
            const V3 d = pose.rot[lead].rotate(ah);
            const V3 qg = q.p - g.p;
            const double along = qg.dot(d);
            const V3 e = qg - d * along;
            const double s = P.cfg.gripSigmaM;
            for (int a = 0; a < 3; ++a) cost += (e[a] / s) * (e[a] / s);
            if (rows) {
                const Eigen::Vector3d dE = E(d);
                const Eigen::Matrix3d Pm = Eigen::Matrix3d::Identity() - dE * dE.transpose();
                const Eigen::Matrix3d dEdd = -(along * Eigen::Matrix3d::Identity() + dE * E(qg).transpose());
                Mat3X dd;
                dirDerivs(P, pose, lead, d, dd);
                Eigen::Matrix3d Rh;
                for (int c = 0; c < 3; ++c)
                    Rh.col(c) = E(pose.rot[lead].rotate(V3 { c == 0 ? 1.0 : 0, c == 1 ? 1.0 : 0, c == 2 ? 1.0 : 0 }));
                const Eigen::Vector3d aE = E(ah);
                const Eigen::Matrix3d dA = Rh * (Eigen::Matrix3d::Identity() - aE * aE.transpose()) / an;
                const MatrixXd jf = Pm * (q.dth - g.dth) + dEdd * dd;
                MatrixXd js = Pm * (q.ds - g.ds);
                js.middleCols(P.L.iGrip, 3) += dEdd * dA;
                for (int a = 0; a < 3; ++a) {
                    const int row = rows->add();
                    rows->r[row] = e[a] / s;
                    rows->Jf.row(row) = jf.row(a) / s;
                    rows->Js.row(row) = js.row(a) / s;
                }
            }
        }

        // ── planted feet ──
        if (P.cfg.useContact && t < int(P.in.footContact.size())) {
            for (int f = 0; f < 6; ++f) {
                if (!P.in.footContact[size_t(t)][size_t(f)]) continue;
                const int m = 17 + f;
                const Marker &M = R.markers[size_t(m)];
                const V3 off = markerOffset(P, sv, m);
                if (rows) markerPJ(P, pose, sv, M.joint, off, P.L.offSlot[size_t(m)], pj);
                else pj.p = markerWorld(pose, M.joint, off);
                const double s = P.cfg.contactSigmaM;
                // A planted marker does not MOVE: one anchor (x, y, z) per marker for the swing. No
                // horizontal floor is asserted — "all three at their flat-foot heights" is a claim
                // the rig's ankles cannot always meet, and with the DTL free to roll it tilted the
                // whole world to make it true. The floor is read off the anchors afterwards.
                const double res[3] = { (pj.p.x - sv[P.L.iAnchor + 3 * f]) / s,
                                        (pj.p.y - sv[P.L.iAnchor + 3 * f + 1]) / s,
                                        (pj.p.z - sv[P.L.iAnchor + 3 * f + 2]) / s };
                for (int a = 0; a < 3; ++a) {
                    cost += res[a] * res[a];
                    if (!rows) continue;
                    const int row = rows->add();
                    rows->r[row] = res[a];
                    rows->Jf.row(row) = pj.dth.row(a) / s;
                    rows->Js.row(row) = pj.ds.row(a) / s;
                    const int col = P.L.iAnchor + 3 * f + a;
                    rows->Js(row, col) -= 1.0 / s;
                }
            }
        }

        // ── IMUs ──
        if (P.cfg.useImu) {
            for (int i = 0; i < int(P.in.imu.size()); ++i) {
                const ImuTrack &it = P.in.imu[size_t(i)];
                if (it.joint < 0 || t >= int(it.q.size()) || !it.valid[size_t(t)]) continue;
                const int base = P.L.iImu + 4 * i;
                const V3 mv { sv[base], sv[base + 1], sv[base + 2] };
                const double psi = sv[base + 3];
                const Q Mq = rotvecToQ(mv);
                const Q Rz = Q::axisAngle({ 0, 0, 1 }, psi);
                const Q Qi = it.q[size_t(t)].normalized();
                const Q Eq = Qi.conj() * Rz * pose.rot[it.joint] * Mq;
                const V3 r = qToRotvec(Eq);
                const double s = P.cfg.imuSigmaDeg * kDeg;
                for (int a = 0; a < 3; ++a) cost += (r[a] / s) * (r[a] / s);
                if (!rows) continue;
                // A rotation δ about world axis a turns E into Rot(b, δ)·E with b = (Qi⁻¹·Rz)·a; the
                // derivative of log(·) there is differenced EXACTLY (a small-E approximation stalls
                // the solve when the heading offset is large).
                const Q pre = Qi.conj() * Rz;
                auto dLog = [&](const V3 &b) {
                    constexpr double h = 1e-6;
                    const V3 rp = qToRotvec(Q::axisAngle(b, h) * Eq), rm = qToRotvec(Q::axisAngle(b, -h) * Eq);
                    return E((rp - rm) * (0.5 / h));
                };
                const uint64_t chain = R.ancestorsOrSelf[it.joint];
                Mat3X dth = Mat3X::Zero(3, P.nd);
                for (int k = 0; k < P.nd; ++k) {
                    const Dof &df = R.dofs[size_t(k)];
                    if (!((chain >> df.joint) & 1u) || df.kind == DofKind::RootTrans) continue;
                    dth.col(k) = dLog(pre.rotate(pose.dofAxisW[size_t(k)]));
                }
                Mat3X ds = Mat3X::Zero(3, P.L.n);
                ds.col(base + 3) = dLog(Qi.conj().rotate({ 0, 0, 1 }));
                for (int c = 0; c < 3; ++c) {
                    V3 mp = mv;
                    const double h = 1e-6;
                    if (c == 0) mp.x += h; else if (c == 1) mp.y += h; else mp.z += h;
                    const V3 rp = qToRotvec(Qi.conj() * Rz * pose.rot[it.joint] * rotvecToQ(mp));
                    ds.col(base + c) = E((rp - r) * (1.0 / h));
                }
                for (int a = 0; a < 3; ++a) {
                    const int row = rows->add();
                    rows->r[row] = r[a] / s;
                    rows->Jf.row(row) = dth.row(a) / s;
                    rows->Js.row(row) = ds.row(a) / s;
                }
            }
        }

        // ── HackMotion lead wrist ──
        if (P.cfg.useHm && P.hasHm && t < int(P.in.hmFlexDeg.size())) {
            const int hand = P.in.leadIsLeft ? ybot::LeftHand : ybot::RightHand;
            const int k0 = R.firstDof[hand];
            const double s = P.cfg.hmSigmaDeg * kDeg;
            const double obs[2] = { P.in.hmFlexDeg[size_t(t)], t < int(P.in.hmRadDeg.size()) ? P.in.hmRadDeg[size_t(t)] : kNaN };
            const double sign[2] = { P.cfg.hmFlexSign, P.cfg.hmRadSign };
            for (int a = 0; a < 2; ++a) {
                if (!std::isfinite(obs[a])) continue;
                const double r = (th[k0 + a] - sign[a] * obs[a] * kDeg - sv[P.L.iHm + a]) / s;
                cost += r * r;
                if (!rows) continue;
                const int row = rows->add();
                rows->r[row] = r;
                rows->Jf(row, k0 + a) = 1.0 / s;
                rows->Js(row, P.L.iHm + a) = -1.0 / s;
            }
        }
    }
    return cost;
}

// ── smoothness, limits, priors: the cross-frame and shared-only terms ────────
double smoothSigma(const Problem &P, int t, int k)
{
    const Dof &d = P.rig.dofs[size_t(k)];
    double s = d.kind == DofKind::RootTrans ? P.cfg.smoothAccRootM : P.cfg.smoothAccRad;
    const int64_t tus = P.in.t_us[size_t(t)];
    if (tus >= P.fastFrom && tus <= P.fastTo) s *= P.cfg.fastFactor;
    return s;
}

double globalTerms(const Problem &P, const State &S, Lin *lin)
{
    double cost = 0;
    const int T = P.T, nd = P.nd;
    // Smoothness: second difference with the true (non-uniform) spacing.
    if (P.cfg.useSmooth && T >= 3) {
        for (int t = 1; t + 1 < T; ++t) {
            const double d0 = std::max(1e-4, (P.in.t_us[size_t(t)] - P.in.t_us[size_t(t - 1)]) * 1e-6);
            const double d1 = std::max(1e-4, (P.in.t_us[size_t(t + 1)] - P.in.t_us[size_t(t)]) * 1e-6);
            const double cm = 2.0 / (d0 * (d0 + d1)), c0 = -2.0 / (d0 * d1), cp = 2.0 / (d1 * (d0 + d1));
            for (int k = 0; k < nd; ++k) {
                const double s = smoothSigma(P, t, k);
                const double a = cm / s, b = c0 / s, c = cp / s;
                const double r = a * S.th[size_t(t - 1)][k] + b * S.th[size_t(t)][k] + c * S.th[size_t(t + 1)][k];
                cost += r * r;
                if (!lin) continue;
                lin->D[size_t(t - 1)](k, k) += a * a;
                lin->D[size_t(t)](k, k) += b * b;
                lin->D[size_t(t + 1)](k, k) += c * c;
                lin->U1[size_t(t - 1)](k, k) += a * b;
                lin->U1[size_t(t)](k, k) += b * c;
                lin->U2[size_t(t - 1)](k, k) += a * c;
                lin->gf[size_t(t - 1)][k] += a * r;
                lin->gf[size_t(t)][k] += b * r;
                lin->gf[size_t(t + 1)][k] += c * r;
            }
        }
    }
    // Posture priors.
    for (int t = 0; t < T; ++t)
        for (const Problem::DofPrior &dp : P.dofPriors) {
            const double va = S.th[size_t(t)][dp.a], vb = dp.b >= 0 ? S.th[size_t(t)][dp.b] : 0.0;
            const double r = (va - vb - dp.mean) / dp.sigma;
            cost += r * r;
            if (!lin) continue;
            const double w = 1.0 / (dp.sigma * dp.sigma);
            lin->D[size_t(t)](dp.a, dp.a) += w;
            lin->gf[size_t(t)][dp.a] += r / dp.sigma;
            if (dp.b >= 0) {
                lin->D[size_t(t)](dp.b, dp.b) += w;
                lin->D[size_t(t)](dp.a, dp.b) -= w;
                lin->D[size_t(t)](dp.b, dp.a) -= w;
                lin->gf[size_t(t)][dp.b] -= r / dp.sigma;
            }
        }
    // Limits.
    if (P.cfg.useLimits) {
        const double s = P.cfg.limitSigmaDeg * kDeg;
        for (int t = 0; t < T; ++t)
            for (int k = 0; k < nd; ++k) {
                const Dof &d = P.rig.dofs[size_t(k)];
                if (d.lo >= d.hi) continue;
                const double v = S.th[size_t(t)][k];
                const double e = v < d.lo ? v - d.lo : v > d.hi ? v - d.hi : 0.0;
                if (e == 0.0) continue;
                const double r = e / s;
                cost += r * r;
                if (!lin) continue;
                lin->D[size_t(t)](k, k) += 1.0 / (s * s);
                lin->gf[size_t(t)][k] += r / s;
            }
    }
    // Priors on the shared unknowns.
    if (!P.stage1) {
        for (int i = 0; i < P.L.n; ++i) {
            if (!P.svFree[size_t(i)] || P.svSigma[i] <= 0) continue;
            const double r = (S.sv[i] - P.svPrior[i]) / P.svSigma[i];
            cost += r * r;
            if (!lin) continue;
            lin->C(i, i) += 1.0 / (P.svSigma[i] * P.svSigma[i]);
            lin->gs[i] += r / P.svSigma[i];
        }
        // The overall body scale against the height: the mean log scale of the seen groups.
        if (P.cfg.fitLengths && P.cfg.globalScaleSigma > 0) {
            static const int groups[] = { GPelvis, GSpine, GHead, GClavicle, GUpperArm, GForearm, GThigh, GShank };
            const int ng = int(sizeof(groups) / sizeof(groups[0]));
            double m = 0;
            for (int g : groups) m += std::log(std::max(1e-6, S.sv[P.L.iScale + g] / P.s0));
            m /= ng;
            const double r = m / P.cfg.globalScaleSigma;
            cost += r * r;
            if (lin) {
                Eigen::VectorXd j = Eigen::VectorXd::Zero(P.L.n);
                for (int g : groups) j[P.L.iScale + g] = 1.0 / (ng * S.sv[P.L.iScale + g] * P.cfg.globalScaleSigma);
                lin->C.noalias() += j * j.transpose();
                lin->gs.noalias() += j * r;
            }
        }
        // |grip axis| = 1.
        const V3 a { S.sv[P.L.iGrip], S.sv[P.L.iGrip + 1], S.sv[P.L.iGrip + 2] };
        const double n = a.norm(), s = 0.05, r = (n - 1.0) / s;
        cost += r * r;
        if (lin && n > 1e-9 && P.svFree[size_t(P.L.iGrip)]) {
            const Eigen::Vector3d jr = E(a) / (n * s);
            lin->C.block<3, 3>(P.L.iGrip, P.L.iGrip) += jr * jr.transpose();
            lin->gs.segment<3>(P.L.iGrip) += jr * r;
        }
    }
    return cost;
}

double evaluate(const Problem &P, const State &S, Lin *lin)
{
    const int T = P.T, nd = P.nd, ns = P.L.n;
    if (lin) {
        lin->D.assign(size_t(T), MatrixXd::Zero(nd, nd));
        lin->U1.assign(size_t(T), MatrixXd::Zero(nd, nd));
        lin->U2.assign(size_t(T), MatrixXd::Zero(nd, nd));
        lin->B.assign(size_t(T), MatrixXd::Zero(nd, ns));
        lin->gf.assign(size_t(T), VectorXd::Zero(nd));
        lin->C = MatrixXd::Zero(ns, ns);
        lin->gs = VectorXd::Zero(ns);
    }
    double cost = 0;
    Rows rows;
    for (int t = 0; t < T; ++t) {
        if (lin) rows.reset(160, nd, ns);
        cost += frameResiduals(P, S, t, lin ? &rows : nullptr);
        if (!lin || rows.n == 0) continue;
        const auto Jf = rows.Jf.topRows(rows.n);
        const auto Js = rows.Js.topRows(rows.n);
        const auto r = rows.r.head(rows.n);
        lin->D[size_t(t)].noalias() += Jf.transpose() * Jf;
        lin->gf[size_t(t)].noalias() += Jf.transpose() * r;
        if (!P.stage1) {
            lin->B[size_t(t)].noalias() += Jf.transpose() * Js;
            lin->C.noalias() += Js.transpose() * Js;
            lin->gs.noalias() += Js.transpose() * r;
        }
    }
    cost += globalTerms(P, S, lin);
    if (lin) lin->cost = cost;
    return cost;
}

// ── the block-banded Cholesky + Schur solve ─────────────────────────────────
bool solve(const Problem &P, const Lin &lin, double lambda, std::vector<VectorXd> &dth, VectorXd &dsv)
{
    const int T = P.T, nd = P.nd;
    std::vector<int> freeIdx;
    if (!P.stage1)
        for (int i = 0; i < P.L.n; ++i) if (P.svFree[size_t(i)]) freeIdx.push_back(i);
    const int nf = int(freeIdx.size());

    std::vector<MatrixXd> Ltt(static_cast<size_t>(T)), L1(static_cast<size_t>(T)), L2(static_cast<size_t>(T));
    for (int t = 0; t < T; ++t) {
        MatrixXd S = lin.D[size_t(t)];
        for (int k = 0; k < nd; ++k) S(k, k) = S(k, k) * (1.0 + lambda) + 1e-9;
        if (t >= 1) S.noalias() -= L1[size_t(t - 1)] * L1[size_t(t - 1)].transpose();
        if (t >= 2) S.noalias() -= L2[size_t(t - 2)] * L2[size_t(t - 2)].transpose();
        Eigen::LLT<MatrixXd> llt(S);
        if (llt.info() != Eigen::Success) return false;
        Ltt[size_t(t)] = llt.matrixL();
        const auto Lt = Ltt[size_t(t)].triangularView<Eigen::Lower>();
        if (t + 1 < T) {
            MatrixXd M = lin.U1[size_t(t)].transpose();
            if (t >= 1) M.noalias() -= L2[size_t(t - 1)] * L1[size_t(t - 1)].transpose();
            L1[size_t(t)] = Lt.solve(M.transpose()).transpose();
        }
        if (t + 2 < T) L2[size_t(t)] = Lt.solve(MatrixXd(lin.U2[size_t(t)])).transpose();
    }
    // A⁻¹ applied to a per-frame multi-column right-hand side, in place.
    auto applyInv = [&](std::vector<MatrixXd> &R) {
        for (int t = 0; t < T; ++t) {
            if (t >= 1) R[size_t(t)].noalias() -= L1[size_t(t - 1)] * R[size_t(t - 1)];
            if (t >= 2) R[size_t(t)].noalias() -= L2[size_t(t - 2)] * R[size_t(t - 2)];
            Ltt[size_t(t)].triangularView<Eigen::Lower>().solveInPlace(R[size_t(t)]);
        }
        for (int t = T - 1; t >= 0; --t) {
            if (t + 1 < T) R[size_t(t)].noalias() -= L1[size_t(t)].transpose() * R[size_t(t + 1)];
            if (t + 2 < T) R[size_t(t)].noalias() -= L2[size_t(t)].transpose() * R[size_t(t + 2)];
            Ltt[size_t(t)].transpose().triangularView<Eigen::Upper>().solveInPlace(R[size_t(t)]);
        }
    };
    // Columns: [ −g_f | B_free ].
    std::vector<MatrixXd> R(static_cast<size_t>(T));
    for (int t = 0; t < T; ++t) {
        R[size_t(t)].resize(nd, 1 + nf);
        R[size_t(t)].col(0) = -lin.gf[size_t(t)];
        for (int j = 0; j < nf; ++j) R[size_t(t)].col(1 + j) = lin.B[size_t(t)].col(freeIdx[size_t(j)]);
    }
    std::vector<MatrixXd> Bf;
    if (nf > 0) {
        Bf.resize(size_t(T));
        for (int t = 0; t < T; ++t) Bf[size_t(t)] = R[size_t(t)].rightCols(nf);
    }
    applyInv(R);
    dsv = VectorXd::Zero(P.L.n);
    VectorXd ds;
    if (nf > 0) {
        MatrixXd Sc(nf, nf);
        VectorXd rs(nf);
        for (int a = 0; a < nf; ++a) {
            rs[a] = -lin.gs[freeIdx[size_t(a)]];
            for (int b = 0; b < nf; ++b) Sc(a, b) = lin.C(freeIdx[size_t(a)], freeIdx[size_t(b)]);
            Sc(a, a) = Sc(a, a) * (1.0 + lambda) + 1e-9;
        }
        for (int t = 0; t < T; ++t) {
            Sc.noalias() -= Bf[size_t(t)].transpose() * R[size_t(t)].rightCols(nf);
            rs.noalias() -= Bf[size_t(t)].transpose() * R[size_t(t)].col(0);
        }
        Eigen::LDLT<MatrixXd> ldlt(Sc);
        if (ldlt.info() != Eigen::Success) return false;
        ds = ldlt.solve(rs);
        if (!ds.allFinite()) return false;
        for (int a = 0; a < nf; ++a) dsv[freeIdx[size_t(a)]] = ds[a];
    }
    dth.resize(size_t(T));
    for (int t = 0; t < T; ++t) {
        dth[size_t(t)] = R[size_t(t)].col(0);
        if (nf > 0) dth[size_t(t)].noalias() -= R[size_t(t)].rightCols(nf) * ds;
        if (!dth[size_t(t)].allFinite()) return false;
    }
    return true;
}

// Levenberg–Marquardt. Returns iterations run.
int levenbergMarquardt(Problem &P, State &S, int maxIter, double &costOut,
                       const std::function<void(int)> &perIteration = {})
{
    double lambda = 1e-3;
    Lin lin;
    double cost = evaluate(P, S, &lin);
    int it = 0;
    for (; it < maxIter; ++it) {
        if (perIteration) {
            perIteration(it);
            cost = evaluate(P, S, &lin);
        }
        bool accepted = false;
        for (int tries = 0; tries < 8 && !accepted; ++tries) {
            std::vector<VectorXd> dth;
            VectorXd dsv;
            if (!solve(P, lin, lambda, dth, dsv)) { lambda *= 10; continue; }
            State N = S;
            for (int t = 0; t < P.T; ++t) N.th[size_t(t)] += dth[size_t(t)];
            N.sv += dsv;
            const double nc = evaluate(P, N, nullptr);
            if (std::isfinite(nc) && nc < cost) {
                const double rel = (cost - nc) / std::max(1e-12, cost);
                S = std::move(N);
                lambda = std::max(1e-7, lambda / 3.0);
                accepted = true;
                cost = evaluate(P, S, &lin);
                if (rel < 1e-5) { costOut = cost; return it + 1; }
            } else {
                lambda *= 4.0;
            }
        }
        if (!accepted) break;
    }
    costOut = cost;
    return it;
}

// ── a canned address posture: the stage-1 start, and the face-on-only depth template ──
std::vector<double> addressPosture(const Rig &R)
{
    std::vector<double> th(size_t(R.dofCount()), 0.0);
    auto set = [&](const char *name, double deg) {
        for (int k = 0; k < R.dofCount(); ++k)
            if (std::string(R.dofs[size_t(k)].name) == name) th[size_t(k)] = deg * kDeg;
    };
    set("root.pitch", 20);
    for (const char *n : { "spine.flex", "spine1.flex", "spine2.flex" }) set(n, 8);
    for (const char *n : { "lHip.flex", "rHip.flex" }) set(n, 30);
    for (const char *n : { "lKnee.flex", "rKnee.flex" }) set(n, 20);
    for (const char *n : { "lAnkle.dorsi", "rAnkle.dorsi" }) set(n, 10);
    for (const char *n : { "lArm.flex", "rArm.flex" }) set(n, 35);
    for (const char *n : { "lArm.abd", "rArm.abd" }) set(n, -12);
    for (const char *n : { "lElbow.flex", "rElbow.flex" }) set(n, 10);
    return th;
}

} // namespace

bool projectPoint(const Cameras &c, int view, int W, int H, const V3 &p, double &u, double &v)
{
    return project(camFrame(c, view, W, H), p, u, v, nullptr);
}

FitResult fitSkeleton(const FitInput &in)
{
    const auto t0 = std::chrono::steady_clock::now();
    FitResult res;
    Problem P(in);
    const Rig &R = P.rig;
    P.T = int(in.t_us.size());
    P.nd = R.dofCount();
    if (!in.cfg.enabled) { res.reason = "disabled"; return res; }
    if (P.T < 10 || int(in.fo.size()) != P.T || in.foW <= 0 || in.foH <= 0) {
        res.reason = "fewer than 10 face-on frames";
        return res;
    }
    P.hasDtl = in.cfg.useDtl && int(in.dtl.size()) == P.T && in.dtlW > 0 && in.dtlH > 0;
    P.fo = in.fo;
    if (P.hasDtl) P.dtl = in.dtl;
    P.swapFo.assign(size_t(P.T), 0);
    P.swapDtl.assign(size_t(P.T), 0);
    P.fastFrom = in.topUs - 50000;
    P.fastTo = in.impactUs + 60000;
    for (double v : in.hmFlexDeg) if (std::isfinite(v)) { P.hasHm = true; break; }
    const int T = P.T;

    // ── address reference frames ──
    std::vector<int> ref;
    for (int t = 0; t < T; ++t)
        if (std::llabs(in.t_us[size_t(t)] - in.addressUs) <= 150000) ref.push_back(t);
    if (ref.size() < 3) { ref.clear(); for (int t = 0; t < std::min(T, 12); ++t) ref.push_back(t); }

    auto obsMid = [](const ViewObs &o, int a, int b, double &u, double &v) {
        const KpObs &A = o.kp[size_t(a)], &B = o.kp[size_t(b)];
        if (A.sigma <= 0 || B.sigma <= 0) return false;
        u = 0.5 * (A.u + B.u); v = 0.5 * (A.v + B.v);
        return true;
    };

    // ── a mirrored face-on source: the golfer's left must image RIGHT ──
    {
        std::vector<double> d;
        for (int t : ref)
            for (auto pr : { std::pair { 5, 6 }, std::pair { 11, 12 } }) {
                const KpObs &l = P.fo[size_t(t)].kp[size_t(pr.first)], &r = P.fo[size_t(t)].kp[size_t(pr.second)];
                if (l.sigma > 0 && r.sigma > 0) d.push_back(l.u - r.u);
            }
        if (median(d) < 0) {
            res.foMirrored = true;
            for (ViewObs &o : P.fo) {
                for (KpObs &k : o.kp) if (k.sigma > 0) k.u = in.foW - k.u;
                if (std::isfinite(o.shaftTheta)) o.shaftTheta = kPi - o.shaftTheta;
            }
        }
    }

    // ── scale ──
    const double s0 = in.heightM > 0.5 ? in.heightM / (R.restHeadTopY + 0.02) : 1.0;
    P.s0 = s0;
    // The lead hand's grip point sits ~4 cm below the butt; the head's centre is where the
    // club's length ends.
    P.clubToHeadM = (in.clubLengthM > 0.5 ? in.clubLengthM : 0.95) - 0.04;
    {
        auto idx = [&](const char *n) {
            for (int k = 0; k < R.dofCount(); ++k) if (std::string(R.dofs[size_t(k)].name) == n) return k;
            return -1;
        };
        const double cs = in.cfg.spineCoupleSigmaDeg * kDeg;
        for (const char *ax : { "flex", "lat", "twist" }) {
            const int a = idx((std::string("spine.") + ax).c_str()), b = idx((std::string("spine1.") + ax).c_str()),
                      c = idx((std::string("spine2.") + ax).c_str());
            P.dofPriors.push_back({ a, b, 0.0, cs });
            P.dofPriors.push_back({ b, c, 0.0, cs });
        }
        for (const char *n : { "spine.flex", "spine1.flex", "spine2.flex" })
            P.dofPriors.push_back({ idx(n), -1, 0.0, in.cfg.spineFlexSigmaDeg * kDeg });
        for (const char *n : { "lWrist.flex", "lWrist.rad", "rWrist.flex", "rWrist.rad" })
            P.dofPriors.push_back({ idx(n), -1, 0.0, in.cfg.wristSigmaDeg * kDeg });
        for (const char *n : { "lForearm.pron", "rForearm.pron" })
            P.dofPriors.push_back({ idx(n), -1, 0.0, in.cfg.pronationSigmaDeg * kDeg });
        for (const char *n : { "lClav.elev", "lClav.prot", "rClav.elev", "rClav.prot" })
            P.dofPriors.push_back({ idx(n), -1, 0.0, in.cfg.clavicleSigmaDeg * kDeg });
        for (const char *n : { "lArm.rot", "rArm.rot" })
            P.dofPriors.push_back({ idx(n), -1, 0.0, in.cfg.armRotSigmaDeg * kDeg });
    }
    res.scaleGlobal = s0;
    res.scaleSource = in.heightM > 0.5 ? "height" : "default";
    const double visH = (R.restEyeY - R.restAnkleY) * s0;

    // Image scale (px per metre) at the golfer. ⚠ NOT from the eye-to-ankle height: at ADDRESS
    // the golfer is bent at the hips and knees, and reading that height as standing height
    // under-read the scale by 20–30 % on the corpus — which the focal prior then locked in and
    // the lengths inflated 1.4× to absorb. The LEGS (hip → knee → ankle, each side) are nearly
    // straight and nearly square to both view rays at address, so their pixel length over the
    // model's thigh + shank is the ruler; the height extent is only the fallback.
    const double legM = (R.restT[ybot::LeftLeg].norm() + R.restT[ybot::LeftFoot].norm()) * s0;
    auto scalePx = [&](const std::vector<ViewObs> &V) {
        std::vector<double> legs, ext;
        for (int t : ref) {
            const ViewObs &o = V[size_t(t)];
            for (auto tri : { std::array<int, 3> { 11, 13, 15 }, std::array<int, 3> { 12, 14, 16 } }) {
                const KpObs &h = o.kp[size_t(tri[0])], &k = o.kp[size_t(tri[1])], &a = o.kp[size_t(tri[2])];
                if (h.sigma > 0 && k.sigma > 0 && a.sigma > 0)
                    legs.push_back(std::hypot(k.u - h.u, k.v - h.v) + std::hypot(a.u - k.u, a.v - k.v));
            }
            double ue, ve, ua, va;
            if (obsMid(o, 1, 2, ue, ve) && obsMid(o, 15, 16, ua, va)) ext.push_back(va - ve);
        }
        const double l = median(legs);
        if (std::isfinite(l) && l > 20) return l / legM;
        const double e = median(ext);
        return std::isfinite(e) && e > 20 ? e / visH : kNaN;
    };
    const double sF = scalePx(P.fo);                      // px per metre, face-on
    if (!(sF > 0)) { res.reason = "no address legs or height extent in the face-on view"; return res; }
    const double dF0 = in.cfg.faceOnDistanceM * s0;
    Cameras cam0;
    cam0.fF = sF * dF0;
    cam0.pF = 0;

    // Face-on hip midpoint at address, in the world (camera at the origin).
    auto foXZ = [&](double u, double v, double &X, double &Z) {
        X = (u - 0.5 * in.foW) / sF;
        Z = -(v - 0.5 * in.foH) / sF;
    };
    std::vector<double> hu, hv;
    for (int t : ref) { double u, v; if (obsMid(P.fo[size_t(t)], 11, 12, u, v)) { hu.push_back(u); hv.push_back(v); } }
    if (hu.empty()) { res.reason = "no address hips in the face-on view"; return res; }
    double Xh0, Zh0;
    foXZ(median(hu), median(hv), Xh0, Zh0);
    const double Yh0 = dF0;

    // ── the DTL camera: which way it looks (toes image-right ⇒ it looks +X), and where ──
    double sD = kNaN, uD0 = 0, vD0 = 0;
    V3 rD { 0, -1, 0 };
    if (P.hasDtl) {
        const double sD0 = scalePx(P.dtl);
        std::vector<double> du, dv, tdir;
        for (int t : ref) {
            double u, v;
            if (obsMid(P.dtl[size_t(t)], 11, 12, u, v)) { du.push_back(u); dv.push_back(v); }
            for (auto pr : { std::pair { 17, 19 }, std::pair { 20, 22 } }) {
                const KpObs &toe = P.dtl[size_t(t)].kp[size_t(pr.first)], &heel = P.dtl[size_t(t)].kp[size_t(pr.second)];
                if (toe.sigma > 0 && heel.sigma > 0) tdir.push_back(toe.u - heel.u);
            }
        }
        if (!(sD0 > 0) || du.empty()) {
            P.hasDtl = false;
        } else {
            sD = sD0;
            uD0 = median(du); vD0 = median(dv);
            const double tmed = median(tdir);
            cam0.psiD = (std::isfinite(tmed) && tmed < 0) ? kPi : 0.0;
            const V3 f0 { std::cos(cam0.psiD), std::sin(cam0.psiD), 0 };
            rD = f0.cross({ 0, 0, 1 });
            const double D0 = in.cfg.dtlDistanceM * s0;
            const V3 hip0 { Xh0, Yh0, Zh0 };
            V3 axisPt = hip0 - rD * ((uD0 - 0.5 * in.dtlW) / sD);
            axisPt.z = Zh0 - (0.5 * in.dtlH - vD0) / sD;
            cam0.cD = axisPt - f0 * D0;
            cam0.pD = 0;
            cam0.fD = sD * D0;
        }
    }
    res.dtlUsed = P.hasDtl;

    // ── weak-perspective triangulation → stage-1 targets ──
    std::vector<double> tmpl(size_t(P.nd), 0.0);
    Pose tmplPose;
    {
        std::array<double, GroupCount> sc; sc.fill(s0);
        const std::vector<double> ap = addressPosture(R);
        forwardKinematics(R, ap.data(), sc.data(), tmplPose);
    }
    const V3 tmplHip = (markerWorld(tmplPose, ybot::LeftUpLeg, {}) + markerWorld(tmplPose, ybot::RightUpLeg, {})) * 0.5;
    P.tgt.assign(size_t(T), {});
    P.tgtSig.assign(size_t(T), {});
    for (int t = 0; t < T; ++t)
        for (int m = 0; m < kMarkerCount; ++m) {
            const KpObs &o = P.fo[size_t(t)].kp[size_t(m)];
            P.tgtSig[size_t(t)][size_t(m)] = { -1, -1, -1 };
            if (o.sigma <= 0) continue;
            double X, Z;
            foXZ(o.u, o.v, X, Z);
            double Y = Yh0 + (markerWorld(tmplPose, R.markers[size_t(m)].joint, R.markers[size_t(m)].offsetPrior).y - tmplHip.y);
            double sy = 0.25 * s0;
            if (P.hasDtl) {
                const KpObs &d = P.dtl[size_t(t)].kp[size_t(m)];
                if (d.sigma > 0) {
                    Y = Yh0 + rD.y * (d.u - uD0) / sD;
                    const double Zd = Zh0 - (d.v - vD0) / sD;
                    Z = 0.5 * (Z + Zd);
                    sy = 0.04 * s0;
                }
            }
            P.tgt[size_t(t)][size_t(m)] = { X, Y, Z };
            P.tgtSig[size_t(t)][size_t(m)] = { 0.03 * s0, sy, 0.03 * s0 };
        }

    // Raw triangulation length CV (the baseline the rigid model corrects).
    {
        const std::array<std::tuple<int, int, int>, 8> seg = { std::tuple { GUpperArm, 5, 7 }, { GUpperArm, 6, 8 },
            { GForearm, 7, 9 }, { GForearm, 8, 10 }, { GThigh, 11, 13 }, { GThigh, 12, 14 },
            { GShank, 13, 15 }, { GShank, 14, 16 } };
        res.rawLengthCv.fill(kNaN);
        std::array<std::vector<double>, GroupCount> L;
        for (int t = 0; t < T && P.hasDtl; ++t)
            for (const auto &[g, a, b] : seg) {
                if (P.tgtSig[size_t(t)][size_t(a)].y > 0.05 * s0 || P.tgtSig[size_t(t)][size_t(b)].y > 0.05 * s0) continue;
                if (P.tgtSig[size_t(t)][size_t(a)].x <= 0 || P.tgtSig[size_t(t)][size_t(b)].x <= 0) continue;
                L[size_t(g)].push_back((P.tgt[size_t(t)][size_t(a)] - P.tgt[size_t(t)][size_t(b)]).norm());
            }
        for (int g = 0; g < GroupCount; ++g) {
            const auto &v = L[size_t(g)];
            if (v.size() < 10) continue;
            const double mu = std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
            double ss = 0;
            for (double x : v) ss += (x - mu) * (x - mu);
            res.rawLengthCv[size_t(g)] = std::sqrt(ss / double(v.size())) / mu;
        }
    }

    // ── shared layout ──
    Layout &L = P.L;
    int n = 0;
    L.iScale = n; n += GroupCount;
    L.iOff = n;
    for (int m = 0; m < kMarkerCount; ++m) {
        L.offSlot[size_t(m)] = R.markers[size_t(m)].fitOffset ? n : -1;
        if (R.markers[size_t(m)].fitOffset) n += 3;
    }
    L.iSym = n; n += 2 * kSymGroups;
    L.iCam = n; n += kCamParams;
    L.iZg = n; n += 1;
    L.iGrip = n; n += 9;
    L.iClub = n; n += 1;
    L.iAnchor = n; n += 18;
    L.iImu = n; n += 4 * int(in.imu.size());
    L.iHm = n; n += 2;
    L.n = n;

    State S;
    S.sv = VectorXd::Zero(n);
    P.svPrior = VectorXd::Zero(n);
    P.svSigma = VectorXd::Constant(n, -1);
    P.svFree.assign(size_t(n), 0);
    for (int g = 0; g < GroupCount; ++g) {
        S.sv[L.iScale + g] = s0;
        P.svPrior[L.iScale + g] = s0;
        P.svSigma[L.iScale + g] = in.cfg.lengthSigma * s0;
        P.svFree[size_t(L.iScale + g)] = in.cfg.fitLengths;
    }
    for (int m = 0; m < kMarkerCount; ++m) {
        const int sl = L.offSlot[size_t(m)];
        if (sl < 0) continue;
        const V3 &o = R.markers[size_t(m)].offsetPrior;
        for (int c = 0; c < 3; ++c) {
            S.sv[sl + c] = P.svPrior[sl + c] = o[c] * s0;
            P.svSigma[sl + c] = R.markers[size_t(m)].sigmaM;
            P.svFree[size_t(sl + c)] = 1;
        }
    }
    for (int c = 0; c < 2 * kSymGroups; ++c) {
        P.svSigma[L.iSym + c] = 0.03 * s0;
        P.svFree[size_t(L.iSym + c)] = 1;
    }
    const double camInit[kCamParams] = { cam0.fF, cam0.pF, cam0.cD.x, cam0.cD.y, cam0.cD.z, cam0.psiD, cam0.pD, cam0.fD, 0, 0 };
    // The face-on pitch prior is TIGHT (4°): with the DTL free to roll, a tilt of the whole world
    // is seen only through the planted feet, which are too short a baseline to hold it — the
    // synthetic swing drifted 7° with a 10° prior here. "The face-on camera is near level" is the
    // same convention club3d's fusion makes.
    const double camSig[kCamParams] = { 0.3 * cam0.fF, 4 * kDeg, 1.0, 1.0, 1.0, 20 * kDeg, 15 * kDeg,
                                        0.3 * std::max(1.0, cam0.fD), 5 * kDeg, 5 * kDeg };
    for (int c = 0; c < kCamParams; ++c) {
        S.sv[L.iCam + c] = P.svPrior[L.iCam + c] = camInit[c];
        P.svSigma[L.iCam + c] = camSig[c];
        // Focal lengths carry a TIGHT prior: with the golfer's size fixed by the lengths, focal
        // and distance are the same image to first order (a gauge only the golfer's own depth
        // extent breaks, weakly). Frozen outright they leave a few px of perspective misfit.
        // ⚠ The face-on camera's roll stays 0: that camera DEFINES the world's level, and its roll
        // trades freely against the golfer's lean (synthetic: body directions 2.5° → 8.7° p90 with
        // it free). The DTL roll relative to it is seen — both views see the same verticals — and
        // on the corpus it is real: the DTL camera sits ~10° rolled on its mount.
        const bool foParam = c < 2;
        P.svFree[size_t(L.iCam + c)] = in.cfg.fitCameras && c != 8 && (c != 9 || in.cfg.fitDtlRoll)
                                       && (foParam || P.hasDtl);
    }
    // Floor: the median heel height at address, from the targets.
    {
        std::vector<double> z;
        for (int t : ref)
            for (int m : { 19, 22 })
                if (P.tgtSig[size_t(t)][size_t(m)].x > 0) z.push_back(P.tgt[size_t(t)][size_t(m)].z);
        if (z.empty())
            for (int t : ref)
                for (int m : { 15, 16 })
                    if (P.tgtSig[size_t(t)][size_t(m)].x > 0) z.push_back(P.tgt[size_t(t)][size_t(m)].z - 0.09 * s0);
        S.sv[L.iZg] = z.empty() ? Zh0 - 0.9 * s0 : median(z);
        P.svFree[size_t(L.iZg)] = 0;    // derived from the anchors after the solve
    }
    // Grip priors (hand-local): the grip point near the palm's centre; the shaft leaving
    // the lead hand DISTALLY and toward the little finger (a diagonal hold, ~40° off the
    // hand's long axis). The wrist angles and this axis trade off; the prior is what
    // splits them unless a HackMotion is worn (design §3.5, "what it cannot fix").
    {
        const int ls = in.leadIsLeft ? 0 : 1, ts = 1 - ls;
        const V3 bone { 0, 1, 0 };
        const V3 lead = (bone * std::cos(40 * kDeg) - R.thumbLocal[size_t(ls)] * std::sin(40 * kDeg)).unit();
        const V3 gOff = (bone * 0.08 + R.palmLocal[size_t(ls)] * 0.02) * s0;
        const V3 tOff = (bone * 0.08 + R.palmLocal[size_t(ts)] * 0.02) * s0;
        const double pri[9] = { lead.x, lead.y, lead.z, gOff.x, gOff.y, gOff.z, tOff.x, tOff.y, tOff.z };
        for (int c = 0; c < 9; ++c) {
            S.sv[L.iGrip + c] = P.svPrior[L.iGrip + c] = pri[c];
            P.svSigma[L.iGrip + c] = c < 3 ? 0.35 : 0.03 * s0;
            P.svFree[size_t(L.iGrip + c)] = in.cfg.useShaft || in.cfg.useGrip;
        }
    }
    // Grip → clubhead distance: a shared unknown with the club record as its prior. The
    // measured heads see it (both views, ~0.9 m of lever), and a club record that is only the
    // driver default (job.clubLengthM) must not be allowed to roll the forearms to fit it.
    S.sv[L.iClub] = P.svPrior[L.iClub] = P.clubToHeadM;
    P.svSigma[L.iClub] = in.clubLengthM > 0.5 ? 0.08 : 0.15;
    P.svFree[size_t(L.iClub)] = in.cfg.useClubhead;
    for (int c = 0; c < 18; ++c) P.svFree[size_t(L.iAnchor + c)] = in.cfg.useContact;
    for (int i = 0; i < int(in.imu.size()); ++i)
        for (int c = 0; c < 4; ++c) P.svFree[size_t(L.iImu + 4 * i + c)] = in.cfg.useImu;
    for (int c = 0; c < 2; ++c) {
        P.svFree[size_t(L.iHm + c)] = in.cfg.useHm && P.hasHm;
        P.svSigma[L.iHm + c] = 15 * kDeg;
    }

    // ── stage 1: fit the rig to the triangulation ──
    {
        const std::vector<double> ap = addressPosture(R);
        const V3 tmplRoot = tmplPose.pos[ybot::Hips];
        S.th.assign(size_t(T), VectorXd::Zero(P.nd));
        V3 lastHip { Xh0, Yh0, Zh0 };
        for (int t = 0; t < T; ++t) {
            VectorXd &th = S.th[size_t(t)];
            for (int k = 0; k < P.nd; ++k) th[k] = ap[size_t(k)];
            const V3 &a = P.tgtSig[size_t(t)][11], &b = P.tgtSig[size_t(t)][12];
            if (a.x > 0 && b.x > 0) lastHip = (P.tgt[size_t(t)][11] + P.tgt[size_t(t)][12]) * 0.5;
            const V3 root = lastHip + (tmplRoot - tmplHip);
            th[0] = root.x; th[1] = root.y; th[2] = root.z;
        }
        P.stage1 = true;
        double c1 = 0;
        if (in.debugInitTheta && int(in.debugInitTheta->size()) == T) {
            for (int t = 0; t < T; ++t)
                for (int k = 0; k < P.nd; ++k) S.th[size_t(t)][k] = (*in.debugInitTheta)[size_t(t)][size_t(k)];
        } else {
            res.iterations += levenbergMarquardt(P, S, in.cfg.stage1Iters, c1);
        }
    }

    // Anchors + IMU mounts from the stage-1 pose.
    std::vector<Pose> poses(static_cast<size_t>(T));
    auto poseAll = [&]() {
        std::array<double, GroupCount> sc {};
        for (int g = 0; g < GroupCount; ++g) sc[size_t(g)] = S.sv[L.iScale + g];
        for (int t = 0; t < T; ++t) forwardKinematics(R, S.th[size_t(t)].data(), sc.data(), poses[size_t(t)]);
    };
    poseAll();
    {
        const int ta = ref[ref.size() / 2];
        for (int f = 0; f < 6; ++f) {
            const int m = 17 + f;
            const V3 p = markerWorld(poses[size_t(ta)], R.markers[size_t(m)].joint, markerOffset(P, S.sv, m));
            S.sv[L.iAnchor + 3 * f] = p.x;
            S.sv[L.iAnchor + 3 * f + 1] = p.y;
            S.sv[L.iAnchor + 3 * f + 2] = p.z;
        }
        for (int i = 0; i < int(in.imu.size()); ++i) {
            const ImuTrack &it = in.imu[size_t(i)];
            if (it.hasMount) {
                // Known mount: a tight prior on it, and the heading solved from the first frame.
                const V3 m = qToRotvec(it.mount);
                for (int c = 0; c < 3; ++c) {
                    S.sv[L.iImu + 4 * i + c] = P.svPrior[L.iImu + 4 * i + c] = m[c];
                    P.svSigma[L.iImu + 4 * i + c] = 3 * kDeg;
                }
                for (int t = 0; t < T && t < int(it.q.size()); ++t)
                    if (it.valid[size_t(t)] && it.joint >= 0) {
                        const Q h = it.q[size_t(t)] * (poses[size_t(t)].rot[it.joint] * it.mount).conj();
                        const V3 fwd = h.rotate({ 1, 0, 0 });
                        S.sv[L.iImu + 4 * i + 3] = std::atan2(fwd.y, fwd.x);
                        break;
                    }
                continue;
            }
            for (int t = 0; t < T && t < int(it.q.size()); ++t)
                if (it.valid[size_t(t)] && it.joint >= 0) {
                    const V3 m = qToRotvec(poses[size_t(t)].rot[it.joint].conj() * it.q[size_t(t)]);
                    S.sv[L.iImu + 4 * i] = m.x; S.sv[L.iImu + 4 * i + 1] = m.y; S.sv[L.iImu + 4 * i + 2] = m.z;
                    break;
                }
        }
    }

    // ── stage 2: the real objective ──
    P.stage1 = false;
    // Label swaps: test the mirrored assignment of every L/R pair, per view and frame,
    // against the current body. A rigid body makes the wrong labelling expensive.
    auto swapPass = [&](int) {
        if (!in.cfg.labelSwap) return;
        poseAll();
        const Cameras cm = camFromSv(S.sv, L);
        const double c2 = in.cfg.cauchyC * in.cfg.cauchyC;
        for (int view = 0; view < (P.hasDtl ? 2 : 1); ++view) {
            const int W = view == 0 ? in.foW : in.dtlW, H = view == 0 ? in.foH : in.dtlH;
            const CamFrame cf = camFrame(cm, view, W, H);
            std::vector<ViewObs> &V = view == 0 ? P.fo : P.dtl;
            std::vector<uint8_t> &sw = view == 0 ? P.swapFo : P.swapDtl;
            for (int t = 0; t < T; ++t) {
                std::array<double, kMarkerCount> pu {}, pv {};
                std::array<bool, kMarkerCount> ok {};
                for (int m = 0; m < kMarkerCount; ++m)
                    ok[size_t(m)] = project(cf, markerWorld(poses[size_t(t)], R.markers[size_t(m)].joint, markerOffset(P, S.sv, m)),
                                            pu[size_t(m)], pv[size_t(m)], nullptr);
                auto cost = [&](bool swapped) {
                    double c = 0;
                    for (int m = 1; m < kMarkerCount; ++m) {
                        const int src = swapped && R.markers[size_t(m)].mirror >= 0 ? R.markers[size_t(m)].mirror : m;
                        const KpObs &o = V[size_t(t)].kp[size_t(src)];
                        if (o.sigma <= 0 || !ok[size_t(m)]) continue;
                        const double ru = (pu[size_t(m)] - o.u) / o.sigma, rv = (pv[size_t(m)] - o.v) / o.sigma;
                        c += c2 * std::log1p((ru * ru + rv * rv) / c2);
                    }
                    return c;
                };
                const double c0 = cost(false), c1 = cost(true);
                if (c1 < 0.6 * c0 && c0 - c1 > 20.0) {
                    std::array<KpObs, kMarkerCount> k = V[size_t(t)].kp;
                    for (int m = 1; m < kMarkerCount; ++m)
                        if (R.markers[size_t(m)].mirror >= 0) V[size_t(t)].kp[size_t(m)] = k[size_t(R.markers[size_t(m)].mirror)];
                    sw[size_t(t)] ^= 1;
                }
            }
        }
    };
    double cost2 = 0;
    if (in.cfg.debugJacobianFrames > 0) {
        // Robust loss off, so a row IS the residual's derivative.
        const FitConfig saved = P.cfg;
        P.cfg.cauchyC = 1e9;
        double worst = 0;
        std::string where;
        for (int f = 0; f < in.cfg.debugJacobianFrames; ++f) {
            const int t = int((long(f) * 7919 + T / 3) % T);
            Rows A;
            A.reset(160, P.nd, L.n);
            frameResiduals(P, S, t, &A);
            auto resid = [&](const State &X) {
                Rows B;
                B.reset(160, P.nd, L.n);
                frameResiduals(P, X, t, &B);
                return VectorXd(B.r.head(B.n));
            };
            auto check = [&](const VectorXd &num, int col, bool shared, const std::string &name) {
                for (int i = 0; i < A.n && i < num.size(); ++i) {
                    const double an = shared ? A.Js(i, col) : A.Jf(i, col);
                    const double e = std::fabs(an - num[i]) / (1.0 + std::fabs(num[i]));
                    if (e > worst) { worst = e; where = name + " row " + std::to_string(i) + " frame " + std::to_string(t); }
                }
            };
            for (int k = 0; k < P.nd; ++k) {
                State Sp = S, Sm = S;
                const double h = 1e-6;
                Sp.th[size_t(t)][k] += h; Sm.th[size_t(t)][k] -= h;
                check((resid(Sp) - resid(Sm)) / (2 * h), k, false, R.dofs[size_t(k)].name);
            }
            for (int i = 0; i < L.n; ++i) {
                if (!P.svFree[size_t(i)]) continue;
                State Sp = S, Sm = S;
                const double h = std::max(1e-6, std::fabs(S.sv[i]) * 1e-6);
                Sp.sv[i] += h; Sm.sv[i] -= h;
                check((resid(Sp) - resid(Sm)) / (2 * h), i, true, "shared " + std::to_string(i));
            }
        }
        res.debugJacobianErr = worst;
        res.debugJacobianWorst = where;
        P.cfg = saved;
    }
    res.costInit = evaluate(P, S, nullptr);
    // The grip ("both hands on one club") is switched on only once the rest has settled: it
    // couples the two arms through the club, and from the stage-1 start that coupling pulls
    // the lead forearm into a rolled minimum (synthetic: 108° p90 with it on from the start,
    // 20° when it joins a settled fit).
    const bool gripWanted = P.cfg.useGrip;
    P.cfg.useGrip = false;
    res.iterations += levenbergMarquardt(P, S, in.cfg.stage2Iters, cost2, [&](int it) {
        if (it == 0 || it == 4 || it == 10) swapPass(it);
    });
    if (gripWanted) {
        P.cfg.useGrip = true;
        res.iterations += levenbergMarquardt(P, S, std::max(4, in.cfg.stage2Iters / 2), cost2);
    }
    res.costFinal = cost2;
    poseAll();

    // ── outputs ──
    // The floor: the planted markers' anchors, less their flat-foot heights (median).
    if (in.cfg.useContact) {
        std::vector<double> z;
        for (int f = 0; f < 6; ++f) z.push_back(S.sv[L.iAnchor + 3 * f + 2] - R.markers[size_t(17 + f)].floorHeight * s0);
        S.sv[L.iZg] = median(z);
    }
    res.cam = camFromSv(S.sv, L);
    for (int g = 0; g < GroupCount; ++g) res.scale[size_t(g)] = S.sv[L.iScale + g];
    for (int m = 0; m < kMarkerCount; ++m) {
        const V3 o = markerOffset(P, S.sv, m);
        res.markerOffset[size_t(m)] = { o.x, o.y, o.z };
    }
    res.clubLengthM = S.sv[L.iClub] + 0.04;
    for (int c = 0; c < 3; ++c) {
        res.gripAxisLocal[size_t(c)] = S.sv[L.iGrip + c];
        res.gripOffsetLocal[size_t(c)] = S.sv[L.iGrip + 3 + c];
        res.trailGripOffsetLocal[size_t(c)] = S.sv[L.iGrip + 6 + c];
    }
    {
        const double an = std::max(1e-9, std::sqrt(res.gripAxisLocal[0] * res.gripAxisLocal[0]
                                                    + res.gripAxisLocal[1] * res.gripAxisLocal[1]
                                                    + res.gripAxisLocal[2] * res.gripAxisLocal[2]));
        for (double &v : res.gripAxisLocal) v /= an;
    }
    res.t_us = in.t_us;
    res.theta.resize(size_t(T));
    res.joints.resize(size_t(T));
    res.tier.resize(size_t(T));
    res.sigmaM.resize(size_t(T));
    res.flags.assign(size_t(T), 0);
    res.grip.resize(size_t(T));
    res.shaftDir.resize(size_t(T));
    res.shaftTier.assign(size_t(T), 0);

    // Uncertainty: the frame's own block of the final normal matrix (conservative: no
    // cross-frame information), propagated to each joint's position.
    Lin lin;
    evaluate(P, S, &lin);
    const int leadHand = in.leadIsLeft ? ybot::LeftHand : ybot::RightHand;
    std::vector<double> errF, errD, slip;
    const Cameras cm = res.cam;
    // Joint → the markers that witness it.
    auto witnesses = [&](int j) -> std::vector<int> {
        using namespace ybot;
        switch (j) {
        case Hips: return { 11, 12 };
        case Spine: case Spine1: case Spine2: case Neck: return { 5, 6, 11, 12 };
        case Head: case HeadTop_End: return { 0, 1, 2, 3, 4 };
        case LeftShoulder: case LeftArm: return { 5 };
        case RightShoulder: case RightArm: return { 6 };
        case LeftForeArm: return { 7 };
        case RightForeArm: return { 8 };
        case LeftHand: case LeftHandMiddle1: return { 9 };
        case RightHand: case RightHandMiddle1: return { 10 };
        case LeftUpLeg: return { 11 };
        case RightUpLeg: return { 12 };
        case LeftLeg: return { 13 };
        case RightLeg: return { 14 };
        case LeftFoot: case LeftToeBase: case LeftToe_End: return { 15, 17, 18, 19 };
        default: return { 16, 20, 21, 22 };
        }
    };
    std::array<std::vector<int>, Rig::N> wit;
    for (int j = 0; j < Rig::N; ++j) wit[size_t(j)] = witnesses(j);
    auto seenIn = [&](const std::vector<ViewObs> &V, int t, int j) {
        for (int m : wit[size_t(j)]) if (V[size_t(t)].kp[size_t(m)].sigma > 0) return true;
        return false;
    };
    for (int t = 0; t < T; ++t) {
        const Pose &pz = poses[size_t(t)];
        res.theta[size_t(t)].assign(S.th[size_t(t)].data(), S.th[size_t(t)].data() + P.nd);
        MatrixXd Dt = lin.D[size_t(t)];
        for (int k = 0; k < P.nd; ++k) Dt(k, k) += 1e-9;
        const MatrixXd Sig = Dt.ldlt().solve(MatrixXd::Identity(P.nd, P.nd));
        PJ pj;
        for (int j = 0; j < Rig::N; ++j) {
            res.joints[size_t(t)][size_t(j)] = pz.pos[j];
            markerPJ(P, pz, S.sv, j, {}, -1, pj);
            const double var = (pj.dth * Sig * pj.dth.transpose()).trace() / 3.0;
            const double sig = std::sqrt(std::max(0.0, var));
            res.sigmaM[size_t(t)][size_t(j)] = float(sig);
            // Seen in which views, within ±100 ms?
            int views = 0;
            for (int view = 0; view < (P.hasDtl ? 2 : 1); ++view) {
                const std::vector<ViewObs> &V = view == 0 ? P.fo : P.dtl;
                bool seen = false;
                for (int u = t; u >= 0 && in.t_us[size_t(t)] - in.t_us[size_t(u)] <= 100000 && !seen; --u) seen = seenIn(V, u, j);
                for (int u = t + 1; u < T && in.t_us[size_t(u)] - in.t_us[size_t(t)] <= 100000 && !seen; ++u) seen = seenIn(V, u, j);
                const bool now = seenIn(V, t, j);
                if (now) views += 2; else if (seen) views += 1;
            }
            uint8_t tier;
            if (views == 0) tier = TierAbsent;
            else if (views >= 4 && sig <= in.cfg.measuredSigmaM * s0) tier = TierMeasured;
            else if (sig <= in.cfg.constrainedSigmaM * s0) tier = TierConstrained;
            else tier = TierInferred;
            res.tier[size_t(t)][size_t(j)] = tier;
        }
        // Flags.
        uint8_t fl = 0;
        if (P.swapFo[size_t(t)]) { fl |= FlagSwapFo; ++res.nSwapFo; }
        if (P.hasDtl && P.swapDtl[size_t(t)]) { fl |= FlagSwapDtl; ++res.nSwapDtl; }
        for (int k = 0; k < P.nd; ++k) {
            const Dof &d = R.dofs[size_t(k)];
            if (d.lo < d.hi && (S.th[size_t(t)][k] < d.lo - 2 * kDeg || S.th[size_t(t)][k] > d.hi + 2 * kDeg)) {
                fl |= FlagLimitHeld;
                break;
            }
        }
        if (fl & FlagLimitHeld) ++res.nLimitHeld;
        res.flags[size_t(t)] = fl;
        // Grip + shaft.
        const V3 gOff { res.gripOffsetLocal[0], res.gripOffsetLocal[1], res.gripOffsetLocal[2] };
        const V3 ga { res.gripAxisLocal[0], res.gripAxisLocal[1], res.gripAxisLocal[2] };
        res.grip[size_t(t)] = markerWorld(pz, leadHand, gOff);
        res.shaftDir[size_t(t)] = pz.rot[leadHand].rotate(ga).unit();
        int nv = std::isfinite(P.fo[size_t(t)].shaftTheta) ? 1 : 0;
        if (P.hasDtl && std::isfinite(P.dtl[size_t(t)].shaftTheta)) ++nv;
        res.shaftTier[size_t(t)] = uint8_t(nv == 2 ? 3 : nv == 1 ? 2 : 1);
        // Reprojection errors.
        for (int view = 0; view < (P.hasDtl ? 2 : 1); ++view) {
            const std::vector<ViewObs> &V = view == 0 ? P.fo : P.dtl;
            const CamFrame cf = camFrame(cm, view, view == 0 ? in.foW : in.dtlW, view == 0 ? in.foH : in.dtlH);
            for (int m = 0; m < kMarkerCount; ++m) {
                const KpObs &o = V[size_t(t)].kp[size_t(m)];
                if (o.sigma <= 0) continue;
                double u, v;
                if (!project(cf, markerWorld(pz, R.markers[size_t(m)].joint, markerOffset(P, S.sv, m)), u, v, nullptr)) continue;
                (view == 0 ? errF : errD).push_back(std::hypot(u - o.u, v - o.v));
            }
        }
        if (t < int(in.footContact.size()))
            for (int f = 0; f < 6; ++f) {
                if (!in.footContact[size_t(t)][size_t(f)]) continue;
                const V3 p = markerWorld(pz, R.markers[size_t(17 + f)].joint, markerOffset(P, S.sv, 17 + f));
                slip.push_back(1000.0 * std::hypot(p.x - S.sv[L.iAnchor + 3 * f], p.y - S.sv[L.iAnchor + 3 * f + 1]));
            }
    }
    res.reprojMedPxFo = median(errF);
    res.reprojMedPxDtl = median(errD);
    res.footSlipP90Mm = percentile(slip, 0.9);

    // γ and r at the golfer.
    if (P.hasDtl) {
        res.gammaDeg = std::acos(std::clamp(std::fabs(std::sin(cm.psiD)), 0.0, 1.0)) / kDeg;
        const int ta = ref[ref.size() / 2];
        const V3 hip = poses[size_t(ta)].pos[ybot::Hips];
        const CamFrame cfF = camFrame(cm, 0, in.foW, in.foH), cfD = camFrame(cm, 1, in.dtlW, in.dtlH);
        const double zF = (hip - cfF.c).dot(cfF.fwd), zD = (hip - cfD.c).dot(cfD.fwd);
        if (zF > 0 && zD > 0) res.rRatio = (cm.fD / zD) / (cm.fF / zF);
    }

    // Display frame: the ball at address on the floor, else the mid-heels; the stance
    // axis trail heel → lead heel.
    {
        const V3 lh { S.sv[L.iAnchor + 3 * 2], S.sv[L.iAnchor + 3 * 2 + 1], cm.zG };
        const V3 rh { S.sv[L.iAnchor + 3 * 5], S.sv[L.iAnchor + 3 * 5 + 1], cm.zG };
        const V3 leadHeel = in.leadIsLeft ? lh : rh, trailHeel = in.leadIsLeft ? rh : lh;
        const V3 ax = leadHeel - trailHeel;
        res.stanceYawRad = std::atan2(ax.y, ax.x);
        res.displayOrigin = (lh + rh) * 0.5;
        if (in.ballU >= 0 && in.ballV >= 0) {
            const CamFrame cf = camFrame(cm, 0, in.foW, in.foH);
            double bu = in.ballU;
            if (res.foMirrored) bu = in.foW - bu;
            const V3 ray = (cf.fwd + cf.right * ((bu - cf.cx) / cf.f) + cf.down * ((in.ballV - cf.cy) / cf.f)).unit();
            const double zb = cm.zG + 0.02135;
            if (std::fabs(ray.z) > 1e-6) {
                const double s = (zb - cf.c.z) / ray.z;
                if (s > 0) {
                    res.ballWorld = cf.c + ray * s;
                    res.ballValid = true;
                    res.displayOrigin = { res.ballWorld.x, res.ballWorld.y, cm.zG };
                }
            }
        }
    }

    res.valid = std::isfinite(res.costFinal);
    if (!res.valid) res.reason = "the solve did not converge to a finite cost";
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

} // namespace pinpoint::skeleton3d
