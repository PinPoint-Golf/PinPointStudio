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

// kf3_smoother.h — the 3-state [p, v, a] scalar Kalman filter with a white-jerk process model,
// a variable dt per step and an RTS backward pass. Moved here verbatim from pose_smoother.cpp
// (2026-10-24) so the club rate filter (club_rate_filter.cpp) runs the SAME arithmetic the pose
// smoother does rather than a second copy of it. Its own namespace, because Vec3 / Mat3 are
// common names and a second definition anywhere in pinpoint::analysis would be an ODR violation.
// The one addition is rts()'s optional velocity output, which the club filter reads as θ̇.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace pinpoint::kf3 {

// ── 3×3 / 3-vector helpers for the KF (F = constant-accel transition) ─────────
// Hand-rolled like the club's Mat2 (clubhead_track.cpp), one dimension up. Kept
// deliberately dumb (row-major fixed arrays, no aliasing tricks) — the smoother
// runs offline over 34 scalar filters, correctness beats cleverness.
struct Vec3 { double v[3]; };
struct Mat3 { double m[3][3]; };

inline Mat3 mul(const Mat3 &A, const Mat3 &B)
{
    Mat3 R{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += A.m[i][k] * B.m[k][j];
            R.m[i][j] = s;
        }
    return R;
}
inline Vec3 mul(const Mat3 &A, const Vec3 &x)
{
    Vec3 r{};
    for (int i = 0; i < 3; ++i)
        r.v[i] = A.m[i][0] * x.v[0] + A.m[i][1] * x.v[1] + A.m[i][2] * x.v[2];
    return r;
}
inline Mat3 transpose(const Mat3 &A)
{
    Mat3 R{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) R.m[i][j] = A.m[j][i];
    return R;
}
// Adjugate / determinant inverse; singular ⇒ zero matrix (harmless — only reached
// from the RTS pass where Pp is a positive-definite predicted covariance).
inline Mat3 inv(const Mat3 &A)
{
    const double a = A.m[0][0], b = A.m[0][1], c = A.m[0][2];
    const double d = A.m[1][0], e = A.m[1][1], f = A.m[1][2];
    const double g = A.m[2][0], h = A.m[2][1], i = A.m[2][2];
    const double A00 =  (e * i - f * h);
    const double A01 = -(b * i - c * h);
    const double A02 =  (b * f - c * e);
    const double A10 = -(d * i - f * g);
    const double A11 =  (a * i - c * g);
    const double A12 = -(a * f - c * d);
    const double A20 =  (d * h - e * g);
    const double A21 = -(a * h - b * g);
    const double A22 =  (a * e - b * d);
    const double det = a * A00 + b * A10 + c * A20;
    Mat3 R{};
    if (std::abs(det) < 1e-30) return R;
    const double id = 1.0 / det;
    R.m[0][0] = A00 * id; R.m[0][1] = A01 * id; R.m[0][2] = A02 * id;
    R.m[1][0] = A10 * id; R.m[1][1] = A11 * id; R.m[1][2] = A12 * id;
    R.m[2][0] = A20 * id; R.m[2][1] = A21 * id; R.m[2][2] = A22 * id;
    return R;
}

// Constant-acceleration transition F(dt) = [[1,dt,dt²/2],[0,1,dt],[0,0,1]].
inline Mat3 makeF(double dt)
{
    Mat3 F{};
    F.m[0][0] = 1.0; F.m[0][1] = dt;  F.m[0][2] = 0.5 * dt * dt;
    F.m[1][0] = 0.0; F.m[1][1] = 1.0; F.m[1][2] = dt;
    F.m[2][0] = 0.0; F.m[2][1] = 0.0; F.m[2][2] = 1.0;
    return F;
}
// Standard discrete white-noise-JERK process covariance, q = σ_jerk²:
//   Q = q·[[dt⁵/20, dt⁴/8, dt³/6],[dt⁴/8, dt³/3, dt²/2],[dt³/6, dt²/2, dt]].
//
// σ_jerk derivation (PoseSmootherConfig::sigmaJerk = 2e5 px/s³): swept on two
// synthetic probes at 1080p, σ_meas ≈ 4 px. (1) A STATIONARY noisy point isolates
// the noise-averaging window from curvature bias — Neff = (σ_in/σ_out)², window =
// Neff·dt:  1e5→42 ms@150fps / 50 ms@30fps;  2e5→33 / 42 ms;  3e5→26 / 38 ms. The
// spec target is ≈40–60 ms. (2) A swept ARC at increasing tip speed checks the 3σ
// gate: at 8000 px/s (well beyond a face-on wrist's ~2000–4800 px/s) σ_jerk=1e5
// rejected so many measurements that segments collapsed (half the track fell to
// Off); 2e5 held (0 Off) at a <0.2 px residual cost on realistic speeds. 2e5 is
// the robust knee — its window still lands in-band at the sparse phases (where
// noise averaging matters) and tightens automatically in the dense impact burst.
//
// ── the window as a FUNCTION of σ_jerk (what the legs scale buys) ─────────────
// The measurements above are three points on a law, and phase 4.1 needs the law
// to choose a legsJerkScale. Treat the filter as its steady-state Wiener
// equivalent: a 3rd-order integrated-white-noise signal (spectrum q/ω⁶) observed
// with noise of spectral density σ_m²·dt gives H(ω) = 1/(1 + σ_m²·dt·ω⁶/q), i.e. a
// cutoff at
//        ω_c = (q / (σ_m²·dt))^(1/6) = (σ_jerk² / (σ_m²·dt))^(1/6),
// so the effective window is
//        T = 1/ω_c ∝ σ_jerk^(−1/3)  at fixed dt,   and  ∝ dt^(1/6) at fixed σ_jerk.
// Both are confirmed by the sweep above: 1e5→2e5 predicts 42/2^(1/3) = 33.3 ms
// against a measured 33; 150→30 fps predicts 33·5^(1/6) = 43.2 ms against a
// measured 42. (3e5 is the loose one — predicted 29 ms, measured 26 — so the law
// is a chooser of sweep points, not a substitute for measuring one.)
//
// On a STATIONARY point the residual is pure noise averaging, σ_out = σ_in/√(T/dt),
// so a jerk scale s multiplies the window by s^(−1/3) and the residual σ by
// s^(+1/6). For the legs group (metric_presentation_honesty.md §5.4, targeting an
// 80–100 ms hip window):
//        s = 0.1  → window ×2.15 (≈71 ms @150 fps), residual σ ×0.68
//        s = 0.05 → window ×2.71 (≈90 ms @150 fps), residual σ ×0.61
// pose_smoother.h::legWindowMsForJerkScale is this arithmetic, and both ratios are
// asserted in pose_smoother_test.cpp. What the law does NOT tell you is what a
// longer window costs a real hip excursion — that is what the 4.2 corpus sweep
// measures, and why the default stays 1.0 until it has.
inline Mat3 makeQ(double dt, double q)
{
    const double dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt3 * dt, dt5 = dt4 * dt;
    Mat3 Q{};
    Q.m[0][0] = q * dt5 / 20.0; Q.m[0][1] = q * dt4 / 8.0; Q.m[0][2] = q * dt3 / 6.0;
    Q.m[1][0] = q * dt4 / 8.0;  Q.m[1][1] = q * dt3 / 3.0; Q.m[1][2] = q * dt2 / 2.0;
    Q.m[2][0] = q * dt3 / 6.0;  Q.m[2][1] = q * dt2 / 2.0; Q.m[2][2] = q * dt;
    return Q;
}

// ── Kf3: 3-state [p,v,a] scalar Kalman with white-jerk Q + variable dt ─────────
// The exact structural analogue of clubhead_track.cpp's HeadKf1D — one derivative
// higher and with a per-step dt. predict()/commit() are split (vs HeadKf1D's fused
// step()) so the driver can gate BOTH axes of a keypoint before committing either,
// keeping x and y in lock-step for the shared per-keypoint segmentation. The RTS
// gain uses the STORED predicted covariance (hist), same as the club.
class Kf3 {
public:
    Kf3(double q, double gate2, double p0, double v0, double a0)
        : m_q(q), m_gate2(gate2), m_initP0(p0), m_initV0(v0), m_initA0(a0) {}

    void init(double p)
    {
        m_x = { p, 0.0, 0.0 };
        m_P = Mat3{};
        m_P.m[0][0] = m_initP0; m_P.m[1][1] = m_initV0; m_P.m[2][2] = m_initA0;
        m_hist.clear();
        m_hist.push_back({ m_x, m_P, m_x, m_P, 0.0 });   // init: prediction == posterior
    }

    // Predict INTO the next step (records the prediction + dt for commit()/rts()).
    //
    // qScale (phase 5) multiplies the process-noise variance q FOR THIS STEP ONLY:
    // Q(dt, m_q·qScale). commit() and rts() are deliberately untouched — both read
    // the STORED Pp and dt of each step, so a per-step Q is already in the RTS
    // arithmetic and the backward pass needs to know nothing about the policy that
    // chose the scale. qScale = 1.0 (the default, and every step when adapt.mode is
    // off) leaves m_q · 1.0 == m_q bit-for-bit — ×1.0 is exact in IEEE-754, the same
    // argument the per-group static scales ship on.
    void predict(double dt, double qScale = 1.0)
    {
        m_dt = dt;
        const Mat3 F = makeF(dt);
        m_xp = mul(F, m_x);
        m_Pp = makeQ(dt, m_q * qScale);
        const Mat3 FP = mul(F, m_P);
        const Mat3 FPFt = mul(FP, transpose(F));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) m_Pp.m[i][j] += FPFt.m[i][j];
    }

    // 3σ Mahalanobis innovation gate on the pending prediction (H = [1,0,0]).
    bool gatePass(double z, double R) const
    {
        const double S = m_Pp.m[0][0] + R;
        const double innov = z - m_xp.v[0];
        return innov * innov <= m_gate2 * S;
    }

    // The gate's OWN statistic, normalised: innov²/S on the pending prediction. This
    // is what the phase-5 innov policy reads, computed from the same S, the same
    // innovation and the same H as gatePass one line above.
    //
    // ⚠ It is a SEPARATE function on purpose. Rewriting gatePass as
    // `normInnov(z, R) <= m_gate2` would put a division into the accept decision and
    // could flip a borderline gate on the last bit — and the accept decision drives
    // segmentation, so that would not be byte-identical when the policy is OFF.
    // A degenerate S (≤ 0 — only reachable with a zero R and a collapsed Pp) means the
    // innovation carries NO information about how surprising this step was. Return a
    // huge value, not 0: the policy clamps it to scale 1.0, i.e. today's window, which
    // is the safe direction. Returning 0 would ask for MAXIMUM smoothing exactly where
    // the filter's own covariance has gone unphysical.
    double normInnov(double z, double R) const
    {
        const double S = m_Pp.m[0][0] + R;
        const double innov = z - m_xp.v[0];
        return (S > 0.0) ? (innov * innov) / S : 1e300;
    }

    // Finalize the step: accepted ⇒ scalar update from the pending prediction;
    // else coast (posterior == prediction). Pushes the history entry either way.
    void commit(bool accepted, double z, double R)
    {
        if (accepted) {
            const double S = m_Pp.m[0][0] + R;
            const double innov = z - m_xp.v[0];
            const double K0 = m_Pp.m[0][0] / S;   // K = Pp[:,0] / S
            const double K1 = m_Pp.m[1][0] / S;
            const double K2 = m_Pp.m[2][0] / S;
            m_x.v[0] = m_xp.v[0] + K0 * innov;
            m_x.v[1] = m_xp.v[1] + K1 * innov;
            m_x.v[2] = m_xp.v[2] + K2 * innov;
            const double K[3] = { K0, K1, K2 };
            for (int r = 0; r < 3; ++r)                // P = Pp − K·Pp[0,:]
                for (int c = 0; c < 3; ++c)
                    m_P.m[r][c] = m_Pp.m[r][c] - K[r] * m_Pp.m[0][c];
        } else {
            m_x = m_xp;
            m_P = m_Pp;
        }
        m_hist.push_back({ m_x, m_P, m_xp, m_Pp, m_dt });
    }

    std::size_t size() const { return m_hist.size(); }

    // The pending prediction's position (after predict(), before commit()) — the club filter
    // brings an angle reading onto the sheet of the prediction before it is weighed.
    double predictedP() const { return m_xp.v[0]; }

    // One draw of the whole smoothed trajectory from the posterior (forward-filter backward-sample):
    // x_N ~ N(x_N|N, P_N|N), then x_k ~ N(x_k + J(x_{k+1} − xp_{k+1}), P_k − J·Pp_{k+1}·Jᵀ) with
    // J = P_k·Fᵀ·Pp_{k+1}⁻¹ — the RTS gain, sampled rather than averaged. The draws' spread is the
    // RTS variance, and consecutive steps are correlated as the model says they are, so a rate read
    // off a draw carries the posterior's own uncertainty. Deterministic for a given rng state.
    template <class Rng>
    void sample(Rng &rng, std::vector<double> &pOut, std::vector<double> &vOut) const
    {
        const int n = int(m_hist.size());
        pOut.assign(std::size_t(std::max(0, n)), 0.0);
        vOut.assign(std::size_t(std::max(0, n)), 0.0);
        if (n == 0) return;
        auto draw = [&rng](const Vec3 &mean, const Mat3 &cov) {
            // Cholesky of a 3×3 PSD matrix; a non-positive pivot (a collapsed direction) is
            // taken as zero rather than failing the draw.
            double L[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j <= i; ++j) {
                    double s = 0.5 * (cov.m[i][j] + cov.m[j][i]);
                    for (int k = 0; k < j; ++k) s -= L[i][k] * L[j][k];
                    if (i == j) L[i][i] = s > 0.0 ? std::sqrt(s) : 0.0;
                    else        L[i][j] = L[j][j] > 0.0 ? s / L[j][j] : 0.0;
                }
            const double z[3] = { rng.normal(), rng.normal(), rng.normal() };
            Vec3 x = mean;
            for (int i = 0; i < 3; ++i)
                for (int k = 0; k <= i; ++k) x.v[i] += L[i][k] * z[k];
            return x;
        };
        Vec3 next = draw(m_hist[std::size_t(n - 1)].x, m_hist[std::size_t(n - 1)].P);
        pOut[std::size_t(n - 1)] = next.v[0]; vOut[std::size_t(n - 1)] = next.v[1];
        for (int k = n - 2; k >= 0; --k) {
            const Hist &h  = m_hist[std::size_t(k)];
            const Hist &h1 = m_hist[std::size_t(k + 1)];
            const Mat3 J = mul(mul(h.P, transpose(makeF(h1.dt))), inv(h1.Pp));
            Vec3 dx{};
            for (int i = 0; i < 3; ++i) dx.v[i] = next.v[i] - h1.xp.v[i];
            const Vec3 corr = mul(J, dx);
            Vec3 mean = h.x;
            for (int i = 0; i < 3; ++i) mean.v[i] += corr.v[i];
            const Mat3 JPJ = mul(mul(J, h1.Pp), transpose(J));
            Mat3 cov{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) cov.m[i][j] = h.P.m[i][j] - JPJ.m[i][j];
            next = draw(mean, cov);
            pOut[std::size_t(k)] = next.v[0]; vOut[std::size_t(k)] = next.v[1];
        }
    }

    void trimTail(int n)
    {
        const int keep = std::max(0, int(m_hist.size()) - std::max(0, n));
        m_hist.resize(std::size_t(keep));
    }

    // RTS smooth: pOut[k] = smoothed position, varOut[k] = its posterior variance.
    // aOut (phase 5, optional) additionally returns the smoothed ACCELERATION of the
    // same state (px/s²) — the RTS already carries the full [p,v,a] state, so this
    // costs a copy of one element per step and changes no arithmetic. Nothing new is
    // persisted; it feeds the accel policy's pass-1 read and nothing else.
    //
    // vOut / vVarOut (2026-10-24, optional) return the smoothed VELOCITY and its posterior
    // variance the same way — the club rate filter's θ̇ and σθ̇. Same arithmetic, two more copies.
    void rts(std::vector<double> &pOut, std::vector<double> &varOut,
             std::vector<double> *aOut = nullptr, std::vector<double> *vOut = nullptr,
             std::vector<double> *vVarOut = nullptr) const
    {
        const int n = int(m_hist.size());
        pOut.assign(n, 0.0); varOut.assign(n, 0.0);
        if (aOut) aOut->assign(std::size_t(std::max(0, n)), 0.0);
        if (vOut) vOut->assign(std::size_t(std::max(0, n)), 0.0);
        if (vVarOut) vVarOut->assign(std::size_t(std::max(0, n)), 0.0);
        if (n == 0) return;
        std::vector<Vec3> x(n);
        std::vector<Mat3> P(n);
        for (int k = 0; k < n; ++k) { x[k] = m_hist[k].x; P[k] = m_hist[k].P; }
        for (int k = n - 2; k >= 0; --k) {
            const Hist &h1 = m_hist[k + 1];            // predicted INTO k+1 (from posterior at k)
            const Mat3 Ft = transpose(makeF(h1.dt));
            const Mat3 C = mul(mul(P[k], Ft), inv(h1.Pp));
            Vec3 dx{};
            for (int i = 0; i < 3; ++i) dx.v[i] = x[k + 1].v[i] - h1.xp.v[i];
            const Vec3 corr = mul(C, dx);
            for (int i = 0; i < 3; ++i) x[k].v[i] += corr.v[i];
            Mat3 D{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) D.m[i][j] = P[k + 1].m[i][j] - h1.Pp.m[i][j];
            const Mat3 upd = mul(mul(C, D), transpose(C));
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) P[k].m[i][j] += upd.m[i][j];
        }
        for (int k = 0; k < n; ++k) {
            pOut[k] = x[k].v[0]; varOut[k] = P[k].m[0][0];
            if (aOut) (*aOut)[std::size_t(k)] = x[k].v[2];
            if (vOut) (*vOut)[std::size_t(k)] = x[k].v[1];
            if (vVarOut) (*vVarOut)[std::size_t(k)] = P[k].m[1][1];
        }
    }

private:
    double m_q, m_gate2, m_initP0, m_initV0, m_initA0;
    double m_dt = 0.0;
    Vec3 m_x{};
    Mat3 m_P{};
    Vec3 m_xp{};        // pending prediction (predict → commit)
    Mat3 m_Pp{};
    struct Hist {
        Vec3 x; Mat3 P;     // posterior at step k
        Vec3 xp; Mat3 Pp;   // prediction INTO step k (P_{k|k-1})
        double dt;          // transition dt used to reach step k
    };
    std::vector<Hist> m_hist;
};

} // namespace pinpoint::kf3
