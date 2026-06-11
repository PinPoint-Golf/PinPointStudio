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

#include "shaft_track_assembly.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::analysis {
namespace {

constexpr double kPi  = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Wrap to (−π, π].
double wrapNear(double a)
{
    a = std::fmod(a + kPi, 2.0 * kPi);
    if (a <= 0.0)
        a += 2.0 * kPi;
    return a - kPi;
}

// Raw (sign/offset-free) projected image angle of ŝ rotated by q — the
// face-on orthographic convention documented on predictTheta().
double rawTheta(const QQuaternion &q, const cv::Vec3f &sHand)
{
    const QVector3D w = q.rotatedVector(QVector3D(sHand[0], sHand[1], sHand[2]));
    return std::atan2(-double(w.z()), double(w.x()));
}

// Circular mean of residuals (the closed-form δ* for a given ŝ/sign).
double circularMean(const std::vector<double> &a)
{
    double s = 0.0, c = 0.0;
    for (double v : a) { s += std::sin(v); c += std::cos(v); }
    return std::atan2(s, c);
}

struct FitEval {
    double offset   = 0.0;
    double residual = std::numeric_limits<double>::max();
};

FitEval evaluateFit(const std::vector<double> &thetaMeas,
                    const std::vector<double> &thetaRaw, double sign)
{
    std::vector<double> res;
    res.reserve(thetaMeas.size());
    for (size_t i = 0; i < thetaMeas.size(); ++i)
        res.push_back(wrapNear(thetaMeas[i] - sign * thetaRaw[i]));
    FitEval e;
    e.offset = circularMean(res);
    double ss = 0.0;
    for (double r : res) {
        const double d = wrapNear(r - e.offset);
        ss += d * d;
    }
    e.residual = std::sqrt(ss / double(res.size()));
    return e;
}

// Roughly uniform unit directions (Fibonacci sphere). Half-sphere suffices:
// ŝ and −ŝ give θ_raw shifted by π, which δ absorbs — but the full sphere is
// cheap and avoids reasoning about the degeneracy.
std::vector<cv::Vec3f> sphereDirections(int n)
{
    std::vector<cv::Vec3f> dirs;
    dirs.reserve(size_t(n));
    const double ga = kPi * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double z = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double a = ga * i;
        dirs.emplace_back(float(r * std::cos(a)), float(r * std::sin(a)), float(z));
    }
    return dirs;
}

// Local tangent-plane refinement grid around a direction.
std::vector<cv::Vec3f> localDirections(const cv::Vec3f &center, double radiusRad, int steps)
{
    const cv::Vec3f c = cv::normalize(center);
    cv::Vec3f u = std::abs(c[2]) < 0.9f ? c.cross(cv::Vec3f(0, 0, 1)) : c.cross(cv::Vec3f(1, 0, 0));
    u = cv::normalize(u);
    const cv::Vec3f v = c.cross(u);
    std::vector<cv::Vec3f> dirs;
    dirs.reserve(size_t((2 * steps + 1) * (2 * steps + 1)));
    for (int i = -steps; i <= steps; ++i)
        for (int j = -steps; j <= steps; ++j) {
            const double du = radiusRad * i / steps, dv = radiusRad * j / steps;
            dirs.push_back(cv::normalize(c + u * float(du) + v * float(dv)));
        }
    return dirs;
}

// 3-state constant-acceleration model: x = [θ, θ̇, θ̈], white-noise jerk.
cv::Matx33d transition(double dt)
{
    return { 1.0, dt, 0.5 * dt * dt,
             0.0, 1.0, dt,
             0.0, 0.0, 1.0 };
}

cv::Matx33d processNoise(double dt, double q)
{
    const double d2 = dt * dt, d3 = d2 * dt, d4 = d3 * dt, d5 = d4 * dt;
    return { q * d5 / 20.0, q * d4 / 8.0, q * d3 / 6.0,
             q * d4 / 8.0,  q * d3 / 3.0, q * d2 / 2.0,
             q * d3 / 6.0,  q * d2 / 2.0, q * dt };
}

// Scalar position update (H = [1 0 0]); the wrapped measurement is lifted by
// the caller against the prior, so z is already continuous-domain.
void scalarUpdate(cv::Vec3d &x, cv::Matx33d &P, double z, double r)
{
    const double s = P(0, 0) + r;
    const cv::Vec3d k(P(0, 0) / s, P(1, 0) / s, P(2, 0) / s);
    const double innov = z - x[0];
    x += k * innov;
    // (I − K H) P, H = e₀ᵀ
    cv::Matx33d ikh = cv::Matx33d::eye();
    ikh(0, 0) -= k[0]; ikh(1, 0) -= k[1]; ikh(2, 0) -= k[2];
    P = ikh * P;
}

} // namespace

// ---------------------------------------------------------------------------

double ShaftTrackAssembly::predictTheta(const ShaftInHandFit &fit, const QQuaternion &qHand)
{
    return wrapNear(fit.sign * rawTheta(qHand, fit.sHand) + fit.offsetRad);
}

ShaftInHandFit ShaftTrackAssembly::calibrateShaftInHand(const std::vector<ShaftFrameObs> &obs,
                                                        const AssemblyConfig &cfg)
{
    ShaftInHandFit fit;

    // Eligible frames: valid quaternion, a non-wedge best candidate, and a
    // slow wrapped rate against the previous eligible frame (waggle-speed
    // motion keeps the projection assumption honest and the blur low).
    std::vector<double>      thetaMeas;
    std::vector<QQuaternion> qs;
    double  lastTheta = 0.0;
    int64_t lastT     = 0;
    bool    haveLast  = false;
    for (const ShaftFrameObs &o : obs) {
        if (!o.qHandValid || o.candidates.empty() || o.candidates.front().wedge)
            continue;
        const double th = o.candidates.front().thetaRad;
        if (haveLast) {
            const double dt = double(o.t_us - lastT) * 1e-6;
            if (dt <= 0.0 || std::abs(wrapNear(th - lastTheta)) / dt > cfg.calibSlowRateRadS) {
                lastTheta = th; lastT = o.t_us;
                continue;
            }
        }
        thetaMeas.push_back(th);
        qs.push_back(o.qHand);
        lastTheta = th; lastT = o.t_us; haveLast = true;
    }
    if (int(thetaMeas.size()) < cfg.calibMinFrames)
        return fit;

    // Identifiability: the measured angles must span a real arc.
    double lo = thetaMeas.front(), hi = thetaMeas.front(), acc = thetaMeas.front();
    for (size_t i = 1; i < thetaMeas.size(); ++i) {
        acc += wrapNear(thetaMeas[i] - thetaMeas[i - 1]);
        lo = std::min(lo, acc); hi = std::max(hi, acc);
    }
    if (hi - lo < cfg.calibMinSpanRad)
        return fit;

    auto solveOver = [&](const std::vector<cv::Vec3f> &dirs,
                         cv::Vec3f &bestS, double &bestSign, FitEval &bestE) {
        std::vector<double> raw(thetaMeas.size());
        for (const cv::Vec3f &d : dirs) {
            for (size_t i = 0; i < qs.size(); ++i)
                raw[i] = rawTheta(qs[i], d);
            for (double sign : { 1.0, -1.0 }) {
                const FitEval e = evaluateFit(thetaMeas, raw, sign);
                if (e.residual < bestE.residual) {
                    bestE = e; bestS = d; bestSign = sign;
                }
            }
        }
    };

    cv::Vec3f bestS(1, 0, 0);
    double    bestSign = 1.0;
    FitEval   bestE;
    solveOver(sphereDirections(800), bestS, bestSign, bestE);            // ≈7° lattice
    solveOver(localDirections(bestS, 0.12, 4), bestS, bestSign, bestE);  // ±7° @ ~1.7°
    solveOver(localDirections(bestS, 0.03, 3), bestS, bestSign, bestE);  // ±1.7° @ ~0.6°

    if (bestE.residual > cfg.calibAcceptRad)
        return fit;   // honest failure — the channel stays disabled

    fit.ok          = true;
    fit.sHand       = bestS;
    fit.sign        = bestSign;
    fit.offsetRad   = bestE.offset;
    fit.residualRad = bestE.residual;
    fit.framesUsed  = int(thetaMeas.size());
    return fit;
}

std::vector<double> ShaftTrackAssembly::imuThetaChannel(const std::vector<ShaftFrameObs> &obs,
                                                        const ShaftInHandFit &fit)
{
    std::vector<double> ch(obs.size(), kNaN);
    if (!fit.ok)
        return ch;
    // Sequential unwrap — the channel is gap-free at frame rate, so wrapped
    // per-frame deltas never alias.
    double acc      = 0.0;
    bool   haveAcc  = false;
    for (size_t i = 0; i < obs.size(); ++i) {
        if (!obs[i].qHandValid)
            continue;
        const double th = predictTheta(fit, obs[i].qHand);
        if (!haveAcc) { acc = th; haveAcc = true; }
        else            acc += wrapNear(th - acc);
        ch[i] = acc;
    }
    return ch;
}

// ---------------------------------------------------------------------------

std::vector<int> ShaftTrackAssembly::associate(const std::vector<ShaftFrameObs> &obs,
                                               const std::vector<double> &imuTheta,
                                               const AssemblyConfig &cfg)
{
    const int n = int(obs.size());
    std::vector<int> selected(size_t(n), -1);
    if (n == 0)
        return selected;

    // DP node: candidate k at frame i, or the missing hypothesis (index K_i).
    // Nodes carry the last OBSERVED (θ, t, L, rate, imuθ) along their best
    // path so transitions across missing-gaps stay velocity-aware; missing
    // nodes forward that memory unchanged.
    struct Node {
        double  cost = std::numeric_limits<double>::max();
        int     prev = -1;       // state index in the previous frame
        bool    hasLast = false;
        double  lastTheta = 0.0, lastL = 0.0, lastImu = kNaN;
        int64_t lastT = 0;
        bool    hasRate = false;
        double  lastRate = 0.0;
    };

    auto imuAt = [&](size_t i) {
        return (i < imuTheta.size()) ? imuTheta[i] : kNaN;
    };

    std::vector<std::vector<Node>> dp(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        dp[size_t(i)].resize(obs[size_t(i)].candidates.size() + 1);

    auto nodeCost = [&](const ShaftFrameObs &o, size_t k) {
        const double best = o.candidates.front().score;
        const double frac = best > 0.f ? std::max(double(o.candidates[k].score) / best,
                                                  cfg.nodeScoreFloor)
                                       : cfg.nodeScoreFloor;
        return -std::log(frac);
    };

    // Transition cost from a node's last-observed memory into candidate k.
    auto transCost = [&](const Node &p, const ShaftFrameObs &o, size_t k, double imuHere) {
        if (!p.hasLast)
            return 0.0;
        const double dt = std::max(double(o.t_us - p.lastT) * 1e-6, 1e-4);
        double expected, sigma;
        if (!std::isnan(imuHere) && !std::isnan(p.lastImu)) {
            expected = imuHere - p.lastImu;   // unwrapped channel delta
            sigma    = cfg.transSigmaBaseRad + 0.5 * cfg.transAccSlackImu * dt * dt;
        } else {
            expected = p.hasRate ? p.lastRate * dt : 0.0;
            sigma    = cfg.transSigmaBaseRad + 0.5 * cfg.transAccSlackRadS2 * dt * dt
                     + (p.hasRate ? 0.0 : cfg.transNoRateExtraRad);
        }
        const double dTheta = wrapNear(o.candidates[k].thetaRad - p.lastTheta);
        const double devT   = wrapNear(dTheta - wrapNear(expected)) / sigma;

        const double L      = o.candidates[k].visibleLenPx;
        const double sigL   = cfg.lenSigmaFrac * std::max(p.lastL, L) + cfg.lenSigmaFloorPx;
        const double devL   = (L - p.lastL) / sigL;
        return devT * devT + 0.25 * devL * devL;
    };

    // Frame 0.
    {
        const ShaftFrameObs &o = obs[0];
        const size_t K = o.candidates.size();
        for (size_t k = 0; k < K; ++k) {
            Node &nd = dp[0][k];
            nd.cost = nodeCost(o, k);
            nd.hasLast = true;
            nd.lastTheta = o.candidates[k].thetaRad;
            nd.lastL = o.candidates[k].visibleLenPx;
            nd.lastT = o.t_us;
            nd.lastImu = imuAt(0);
        }
        dp[0][K].cost = cfg.missingPenalty;
    }

    for (int i = 1; i < n; ++i) {
        const ShaftFrameObs &o = obs[size_t(i)];
        const auto &prev = dp[size_t(i) - 1];
        const size_t K = o.candidates.size();
        const double imuHere = imuAt(size_t(i));

        for (size_t k = 0; k < K; ++k) {
            Node &nd = dp[size_t(i)][k];
            const double nc = nodeCost(o, k);
            for (int s = 0; s < int(prev.size()); ++s) {
                if (prev[size_t(s)].cost == std::numeric_limits<double>::max())
                    continue;
                const double c = prev[size_t(s)].cost + nc
                               + transCost(prev[size_t(s)], o, k, imuHere);
                if (c < nd.cost) { nd.cost = c; nd.prev = s; }
            }
            if (nd.prev >= 0) {
                const Node &p = prev[size_t(nd.prev)];
                nd.hasLast   = true;
                nd.lastTheta = o.candidates[k].thetaRad;
                nd.lastL     = o.candidates[k].visibleLenPx;
                nd.lastT     = o.t_us;
                nd.lastImu   = imuHere;
                if (p.hasLast) {
                    const double dt = std::max(double(o.t_us - p.lastT) * 1e-6, 1e-4);
                    nd.hasRate  = true;
                    nd.lastRate = wrapNear(o.candidates[k].thetaRad - p.lastTheta) / dt;
                }
            }
        }
        // Missing: forward the cheapest predecessor's memory unchanged.
        Node &miss = dp[size_t(i)][K];
        for (int s = 0; s < int(prev.size()); ++s) {
            if (prev[size_t(s)].cost == std::numeric_limits<double>::max())
                continue;
            const double c = prev[size_t(s)].cost + cfg.missingPenalty;
            if (c < miss.cost) {
                miss = prev[size_t(s)];
                miss.cost = c;
                miss.prev = s;
            }
        }
    }

    // Backtrack from the cheapest terminal state.
    int state = 0;
    {
        const auto &last = dp[size_t(n) - 1];
        double best = std::numeric_limits<double>::max();
        for (int s = 0; s < int(last.size()); ++s)
            if (last[size_t(s)].cost < best) { best = last[size_t(s)].cost; state = s; }
    }
    for (int i = n - 1; i >= 0; --i) {
        const size_t K = obs[size_t(i)].candidates.size();
        selected[size_t(i)] = (state < int(K)) ? state : -1;
        state = dp[size_t(i)][size_t(state)].prev;
        if (i > 0 && (state < 0 || state >= int(dp[size_t(i) - 1].size())))
            state = int(dp[size_t(i) - 1].size()) - 1;   // defensive: fall to missing
    }
    return selected;
}

// ---------------------------------------------------------------------------

ShaftTrack2D ShaftTrackAssembly::smooth(const std::vector<ShaftFrameObs> &obs,
                                        const std::vector<int> &selected,
                                        const std::vector<double> &imuTheta,
                                        double imuSigmaRad,
                                        int64_t spanStartUs, int64_t spanEndUs,
                                        const AssemblyConfig &cfg)
{
    ShaftTrack2D track;
    const int n = int(obs.size());
    if (n == 0)
        return track;

    auto imuAt = [&](size_t i) {
        return (i < imuTheta.size()) ? imuTheta[i] : kNaN;
    };

    // Find the first epoch with any measurement to initialise the filter.
    int first = -1;
    for (int i = 0; i < n && first < 0; ++i)
        if (selected[size_t(i)] >= 0 || !std::isnan(imuAt(size_t(i))))
            first = i;
    if (first < 0)
        return track;   // nothing measurable at all — invalid empty track

    const double imuR = std::pow(std::max(imuSigmaRad, cfg.imuSigmaFloorRad), 2.0);

    // Forward pass, storing priors and posteriors for RTS. The IMU channel is
    // re-anchored to the filter's domain by a constant per-run offset taken at
    // initialisation (its unwrap origin is arbitrary); each subsequent wrapped
    // lift happens against the prediction.
    struct Step {
        cv::Vec3d  xPrior, xPost;
        cv::Matx33d pPrior, pPost, F;
        uint8_t flags = 0;
    };
    std::vector<Step> steps(static_cast<size_t>(n - first));

    cv::Vec3d  x(0.0, 0.0, 0.0);
    cv::Matx33d P = cv::Matx33d::diag(cv::Vec3d(std::pow(0.17, 2), std::pow(100.0, 2),
                                                std::pow(1000.0, 2)));
    double imuAnchor = kNaN;   // imuTheta − filterθ at init
    {
        const int sel = selected[size_t(first)];
        if (sel >= 0)
            x[0] = obs[size_t(first)].candidates[size_t(sel)].thetaRad;
        else
            x[0] = imuAt(size_t(first));
        if (!std::isnan(imuAt(size_t(first))))
            imuAnchor = imuAt(size_t(first)) - x[0];
    }

    for (int i = first; i < n; ++i) {
        Step &st = steps[size_t(i - first)];
        if (i > first) {
            const double dt = std::max(double(obs[size_t(i)].t_us
                                              - obs[size_t(i) - 1].t_us) * 1e-6, 1e-4);
            st.F = transition(dt);
            x = st.F * x;
            P = st.F * P * st.F.t() + processNoise(dt, cfg.jerkPsd);
        } else {
            st.F = cv::Matx33d::eye();
        }
        st.xPrior = x;
        st.pPrior = P;

        const int sel = selected[size_t(i)];
        if (sel >= 0) {
            const ShaftCandidate &c = obs[size_t(i)].candidates[size_t(sel)];
            const double sig = std::max(double(c.sigmaThetaRad), cfg.visionSigmaFloorRad);
            const double z   = x[0] + wrapNear(double(c.thetaRad) - wrapNear(x[0]));
            scalarUpdate(x, P, z, sig * sig);
            st.flags |= ShaftMeasured;
            if (c.wedge)
                st.flags |= ShaftWedge;
        }
        const double imuHere = imuAt(size_t(i));
        if (!std::isnan(imuHere)) {
            if (std::isnan(imuAnchor))
                imuAnchor = imuHere - x[0];
            // The channel is already continuous: lift by the run anchor only.
            scalarUpdate(x, P, imuHere - imuAnchor, imuR);
            if (!(st.flags & ShaftMeasured))
                st.flags |= ShaftImuBridged;
        }
        if (!(st.flags & (ShaftMeasured | ShaftImuBridged)))
            st.flags |= ShaftCoasted;

        st.xPost = x;
        st.pPost = P;
    }

    // RTS backward smoother.
    const int m = int(steps.size());
    std::vector<cv::Vec3d>  xs(static_cast<size_t>(m));
    std::vector<cv::Matx33d> ps(static_cast<size_t>(m));
    xs[size_t(m) - 1] = steps[size_t(m) - 1].xPost;
    ps[size_t(m) - 1] = steps[size_t(m) - 1].pPost;
    for (int k = m - 2; k >= 0; --k) {
        const Step &nx = steps[size_t(k) + 1];
        const cv::Matx33d C = steps[size_t(k)].pPost * nx.F.t() * nx.pPrior.inv();
        xs[size_t(k)] = steps[size_t(k)].xPost + C * (xs[size_t(k) + 1] - nx.xPrior);
        ps[size_t(k)] = steps[size_t(k)].pPost
                      + C * (ps[size_t(k) + 1] - nx.pPrior) * C.t();
    }

    // Visible length: median-of-5 over measured values, hold-last between —
    // deliberately simple, θ is the precision channel (header note).
    std::vector<double> lenRaw(size_t(n), kNaN);
    for (int i = 0; i < n; ++i)
        if (selected[size_t(i)] >= 0)
            lenRaw[size_t(i)] = obs[size_t(i)].candidates[size_t(selected[size_t(i)])].visibleLenPx;

    track.samples.reserve(size_t(m));
    double lastLen = 0.0;
    int inSpan = 0, covered = 0;
    for (int i = first; i < n; ++i) {
        const Step &st = steps[size_t(i - first)];
        ShaftSample2D s;
        s.t_us         = obs[size_t(i)].t_us;
        s.gripPx       = QPointF(obs[size_t(i)].gripPx.x, obs[size_t(i)].gripPx.y);
        s.thetaRad     = xs[size_t(i - first)][0];
        s.thetaDotRadS = xs[size_t(i - first)][1];
        s.flags        = st.flags;

        if (!std::isnan(lenRaw[size_t(i)])) {
            std::vector<double> w;
            for (int j = std::max(0, i - 2); j <= std::min(n - 1, i + 2); ++j)
                if (!std::isnan(lenRaw[size_t(j)]))
                    w.push_back(lenRaw[size_t(j)]);
            std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
            lastLen = w[w.size() / 2];
        }
        s.visibleLenPx = lastLen;

        const int sel = selected[size_t(i)];
        if (sel >= 0) {
            const auto &h = obs[size_t(i)].candidates[size_t(sel)].headPx;
            s.headPx = QPointF(h.x, h.y);
        } else {
            s.headPx = QPointF(s.gripPx.x() + lastLen * std::cos(s.thetaRad),
                               s.gripPx.y() + lastLen * std::sin(s.thetaRad));
            s.flags |= ShaftHeadProjected;
        }

        const double sig = std::sqrt(std::max(ps[size_t(i - first)](0, 0), 0.0));
        s.conf = float(std::clamp(1.0 - sig / cfg.confSigmaRefRad, 0.0, 1.0));

        if (s.t_us >= spanStartUs && s.t_us <= spanEndUs) {
            ++inSpan;
            if (s.flags & (ShaftMeasured | ShaftImuBridged))
                ++covered;
        }
        track.samples.push_back(s);
    }

    track.coverage = inSpan > 0 ? float(covered) / float(inSpan) : 0.f;
    track.valid    = track.coverage >= float(cfg.coverageMin);
    return track;
}

// ---------------------------------------------------------------------------

ShaftTrack2D ShaftTrackAssembly::assemble(const std::vector<ShaftFrameObs> &obs,
                                          int64_t spanStartUs, int64_t spanEndUs,
                                          const AssemblyConfig &cfg,
                                          AssemblyTrace *trace)
{
    const ShaftInHandFit fit = calibrateShaftInHand(obs, cfg);
    const std::vector<double> imuTheta = imuThetaChannel(obs, fit);
    const std::vector<int> selected = associate(obs, imuTheta, cfg);
    if (trace) {
        trace->fit               = fit;
        trace->selected          = selected;
        trace->imuThetaUnwrapped = imuTheta;
    }

    ShaftTrack2D track = smooth(obs, selected, imuTheta,
                                fit.ok ? fit.residualRad : 0.0,
                                spanStartUs, spanEndUs, cfg);

    // Health metric: Pearson correlation of vision vs IMU angular rate over
    // consecutive measured pairs (computed anyway for the fusion layer's
    // temporal alignment; a low value flags a bad track in production).
    if (fit.ok) {
        std::vector<double> vv, vi;
        int prev = -1;
        for (int i = 0; i < int(obs.size()); ++i) {
            if (selected[size_t(i)] < 0 || std::isnan(imuTheta[size_t(i)]))
                continue;
            if (prev >= 0) {
                const double dt = double(obs[size_t(i)].t_us - obs[size_t(prev)].t_us) * 1e-6;
                if (dt > 0.0 && dt < 0.2) {
                    vv.push_back(wrapNear(obs[size_t(i)].candidates[size_t(selected[size_t(i)])].thetaRad
                                          - obs[size_t(prev)].candidates[size_t(selected[size_t(prev)])].thetaRad) / dt);
                    vi.push_back((imuTheta[size_t(i)] - imuTheta[size_t(prev)]) / dt);
                }
            }
            prev = i;
        }
        if (vv.size() >= 8) {
            double mv = 0, mi = 0;
            for (size_t k = 0; k < vv.size(); ++k) { mv += vv[k]; mi += vi[k]; }
            mv /= double(vv.size()); mi /= double(vi.size());
            double num = 0, dv = 0, di = 0;
            for (size_t k = 0; k < vv.size(); ++k) {
                num += (vv[k] - mv) * (vi[k] - mi);
                dv  += (vv[k] - mv) * (vv[k] - mv);
                di  += (vi[k] - mi) * (vi[k] - mi);
            }
            if (dv > 0 && di > 0)
                track.imuVisionCorr = float(num / std::sqrt(dv * di));
        }
    }
    return track;
}

} // namespace pinpoint::analysis
