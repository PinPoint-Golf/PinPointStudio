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

#include "club_rate_filter.h"

#include <algorithm>
#include <cmath>

namespace pinpoint::analysis {

namespace {

constexpr double kPiCrf    = 3.14159265358979323846;
constexpr double kDegToRad = kPiCrf / 180.0;
// σθ̈ at the start of a segment: a club turns over at the top and is struck at impact, so the
// acceleration there is not known to better than a few hundred rad/s².
constexpr double kInitAccelRadS2 = 300.0;
// A rate below this is "not moving" for the wobble ratio's denominator (°/s).
constexpr double kWobbleFloorDps = 200.0;
// Fewer readings than this in a segment and it does not run: four is the least that pins a
// position, a rate and an acceleration with one to spare.
constexpr int kMinObs = 4;

struct Event {
    int64_t t   = 0;
    int     obs = -1;     // index into the segment's readings, or −1 for an output tick
    size_t  tick = 0;
};

struct SegmentRun {
    std::shared_ptr<ClubFilterSegment> seg;
    std::vector<double> p, var, v, vvar;   // per kf step, after RTS
    std::vector<int>    obsStep;           // per reading: its step
    std::vector<double> z;                 // per reading: the angle on the sheet it was weighed on
    int    gated = 0, downweighted = 0;
    double nisSum = 0.0;
    int    nisN = 0;
};

// One segment: pass 1 gated, then robustIters Huber-reweighted passes, each a forward filter and
// an RTS pass over the readings and the output ticks merged in time.
SegmentRun runSegment(const std::vector<ClubFilterObs> &obs, const std::vector<int64_t> &tickT,
                      const std::vector<size_t> &ticks, double initRateRadS, const ClubRateFilterConfig &cfg)
{
    SegmentRun out;
    std::vector<Event> ev;
    ev.reserve(obs.size() + ticks.size());
    for (int i = 0; i < int(obs.size()); ++i) ev.push_back({ obs[size_t(i)].t_us, i, 0 });
    for (size_t k : ticks) ev.push_back({ tickT[k], -1, k });
    // Readings before ticks at one instant, so a tick on a reading's time reports the update.
    std::stable_sort(ev.begin(), ev.end(), [](const Event &a, const Event &b) {
        if (a.t != b.t) return a.t < b.t;
        return (a.obs >= 0) && (b.obs < 0);
    });

    const double jerk  = cfg.jerkDps3 * kDegToRad;
    const double q     = jerk * jerk;
    const double floor = std::max(cfg.sigmaFloorDeg, 1e-3) * kDegToRad;
    std::vector<double> sigma(obs.size()), w(obs.size(), 1.0);
    for (size_t i = 0; i < obs.size(); ++i) sigma[i] = std::max(obs[i].sigmaRad, floor);

    const int passes = 1 + std::max(0, cfg.robustIters);
    for (int pass = 0; pass < passes; ++pass) {
        const bool gate = pass == 0 && cfg.gateSigma > 0.0;
        const double gate2 = gate ? cfg.gateSigma * cfg.gateSigma : 1e300;
        // The first reading seeds θ; θ̇ starts at zero at the top (the club turns over there) with
        // initRate as its σ, and the acceleration is free.
        const ClubFilterObs &first = obs.front();
        auto seg = std::make_shared<ClubFilterSegment>();
        seg->kf = kf3::Kf3(q, gate2, sigma.front() * sigma.front(), initRateRadS * initRateRadS,
                           kInitAccelRadS2 * kInitAccelRadS2);
        kf3::Kf3 &kf = seg->kf;
        kf.init(first.thetaRad);
        out.obsStep.assign(obs.size(), -1);
        out.z.assign(obs.size(), 0.0);
        if (pass == 0) out.gated = 0;   // only pass 1 gates; the count is pass 1's
        out.nisSum = 0.0; out.nisN = 0;
        int64_t tPrev = first.t_us;
        for (const Event &e : ev) {
            kf.predict(double(e.t - tPrev) * 1e-6);
            tPrev = e.t;
            if (e.obs >= 0) {
                const ClubFilterObs &o = obs[size_t(e.obs)];
                const double xp = kf.predictedP();
                const double z  = xp + std::remainder(o.thetaRad - xp, 2.0 * kPiCrf);
                const double R  = sigma[size_t(e.obs)] * sigma[size_t(e.obs)] / w[size_t(e.obs)];
                const bool ok   = kf.gatePass(z, R);
                if (!ok) ++out.gated;
                else { out.nisSum += kf.normInnov(z, R); ++out.nisN; }
                kf.commit(ok, z, R);
                out.z[size_t(e.obs)]       = z;
                out.obsStep[size_t(e.obs)] = int(kf.size()) - 1;
            } else {
                kf.commit(false, 0.0, 0.0);
                seg->tick.push_back(e.tick);
                seg->step.push_back(int(kf.size()) - 1);
            }
        }
        kf.rts(out.p, out.var, nullptr, &out.v, &out.vvar);
        // Huber weights for the next pass from this pass's smoothed residuals, in σ.
        out.downweighted = 0;
        for (size_t i = 0; i < obs.size(); ++i) {
            const int s = out.obsStep[i];
            const double u = std::fabs(out.z[i] - out.p[size_t(s)]) / sigma[i];
            w[i] = (u <= cfg.robustK || cfg.robustK <= 0.0) ? 1.0 : cfg.robustK / u;
            if (w[i] < 1.0) ++out.downweighted;
        }
        out.seg = seg;
    }
    return out;
}

} // namespace

int countHumps(const std::vector<double> &v, double frac, double prom)
{
    // A hump is a maximum (the window's ends count: a rate still rising at impact is one hump)
    // that stands above frac of the window's top AND rises at least prom of the top above the
    // higher of the two troughs separating it from taller neighbours — so ripples on a flat top
    // are not humps, and a dip-and-recover of a few hundred °/s is.
    const size_t n = v.size();
    if (n == 0) return 0;
    const double top = *std::max_element(v.begin(), v.end());
    if (!(top > 0.0)) return 0;
    int count = 0;
    for (size_t i = 0; i < n; ++i) {
        const bool leftOk  = i == 0     || v[i] > v[i - 1];
        const bool rightOk = i + 1 == n || v[i] >= v[i + 1];
        if (!leftOk || !rightOk || v[i] <= frac * top) continue;
        double lmin = v[i], rmin = v[i];
        for (size_t j = i; j-- > 0;)        { if (v[j] > v[i]) break; lmin = std::min(lmin, v[j]); }
        for (size_t j = i + 1; j < n; ++j)  { if (v[j] > v[i]) break; rmin = std::min(rmin, v[j]); }
        // A side that runs to the window's end without a taller neighbour does not bound it.
        bool lEnd = true, rEnd = true;
        for (size_t j = i; j-- > 0;)        if (v[j] > v[i]) { lEnd = false; break; }
        for (size_t j = i + 1; j < n; ++j)  if (v[j] > v[i]) { rEnd = false; break; }
        const double base = (lEnd && rEnd) ? std::min(lmin, rmin)
                          : lEnd ? rmin : rEnd ? lmin : std::max(lmin, rmin);
        if (v[i] - base >= prom * top) ++count;
    }
    return count;
}

ClubRateFilterResult runClubRateFilter(const std::vector<int64_t> &tickT,
                                       const std::vector<ClubFilterObs> &obsIn,
                                       int64_t tTop, int64_t tImpact, int64_t tEnd,
                                       const ClubRateFilterConfig &cfg)
{
    ClubRateFilterResult res;
    const size_t nT = tickT.size();
    res.covered.assign(nT, 0);
    res.thetaRad.assign(nT, 0.0);
    res.thetaDotRadS.assign(nT, 0.0);
    res.sigmaThetaRad.assign(nT, 0.0);
    res.sigmaThetaDotRadS.assign(nT, 0.0);
    if (!cfg.enabled || tImpact <= tTop || obsIn.empty() || nT == 0) return res;

    std::vector<ClubFilterObs> obs = obsIn;
    std::stable_sort(obs.begin(), obs.end(),
                     [](const ClubFilterObs &a, const ClubFilterObs &b) { return a.t_us < b.t_us; });

    struct Plan { int64_t obsFrom, obsTo, outFrom, outTo; bool outFromOpen; double initRate; bool down; };
    const Plan plans[2] = {
        { tTop - cfg.leadInUs, tImpact, tTop, tImpact, false, cfg.initRateDps * kDegToRad, true },
        { tImpact, tEnd, tImpact, tEnd, true, cfg.initRatePostDps * kDegToRad, false },
    };
    double resSq = 0.0; int resN = 0;
    double nisSum = 0.0; int nisN = 0;
    for (const Plan &pl : plans) {
        if (pl.obsTo <= pl.obsFrom) continue;
        std::vector<ClubFilterObs> so;
        for (const ClubFilterObs &o : obs)
            if (o.t_us >= pl.obsFrom && o.t_us <= pl.obsTo) so.push_back(o);
        if (int(so.size()) < kMinObs) continue;
        std::vector<size_t> ticks;
        for (size_t k = 0; k < nT; ++k) {
            const int64_t t = tickT[k];
            if ((pl.outFromOpen ? t > pl.outFrom : t >= pl.outFrom) && t <= pl.outTo
                && t >= so.front().t_us) ticks.push_back(k);
        }
        if (ticks.empty()) continue;
        const SegmentRun run = runSegment(so, tickT, ticks, pl.initRate, cfg);
        for (size_t j = 0; j < run.seg->tick.size(); ++j) {
            const size_t k = run.seg->tick[j];
            const size_t s = size_t(run.seg->step[j]);
            res.covered[k]           = 1;
            res.thetaRad[k]          = run.p[s];
            res.thetaDotRadS[k]      = run.v[s];
            res.sigmaThetaRad[k]     = std::sqrt(std::max(0.0, run.var[s]));
            res.sigmaThetaDotRadS[k] = std::sqrt(std::max(0.0, run.vvar[s]));
        }
        res.segments.push_back(run.seg);
        res.stats.nEvidence     += int(so.size());
        res.stats.nGated        += run.gated;
        res.stats.nDownweighted += run.downweighted;
        nisSum += run.nisSum; nisN += run.nisN;

        if (!pl.down) continue;
        // ── the downswing diagnostic: how far the FRAMES sat off the filtered track ──────────
        // Raw rate = the difference of consecutive frame readings (the rate the old KS series
        // drew, near enough); filtered rate = the smoothed θ̇ averaged over the same two steps.
        std::vector<std::pair<int64_t, double>> rawRate;   // (mid time, |raw| °/s)
        double wobSq = 0.0; int wobN = 0;
        int prev = -1;
        for (int i = 0; i < int(so.size()); ++i) {
            if (so[size_t(i)].kind != ClubFilterObs::Frame || so[size_t(i)].t_us < tTop) continue;
            const size_t s = size_t(run.obsStep[size_t(i)]);
            const double r = run.z[size_t(i)] - run.p[s];
            resSq += r * r; ++resN;
            if (prev >= 0) {
                const double dt = double(so[size_t(i)].t_us - so[size_t(prev)].t_us) * 1e-6;
                if (dt > 0.0) {
                    const double raw  = (run.z[size_t(i)] - run.z[size_t(prev)]) / dt;
                    const double filt = 0.5 * (run.v[s] + run.v[size_t(run.obsStep[size_t(prev)])]);
                    const double den  = std::max(std::fabs(filt), kWobbleFloorDps * kDegToRad);
                    wobSq += ((raw - filt) / den) * ((raw - filt) / den); ++wobN;
                    rawRate.push_back({ (so[size_t(i)].t_us + so[size_t(prev)].t_us) / 2,
                                        std::fabs(raw) / kDegToRad });
                }
            }
            prev = i;
        }
        if (wobN > 0) res.stats.rateWobble = std::sqrt(wobSq / wobN);
        std::vector<double> rawLast, filtLast;
        for (const auto &rr : rawRate)
            if (rr.first >= tImpact - 250000) rawLast.push_back(rr.second);
        for (size_t j = 0; j < run.seg->tick.size(); ++j) {
            const size_t k = run.seg->tick[j];
            if (tickT[k] >= tImpact - 250000)
                filtLast.push_back(std::fabs(run.v[size_t(run.seg->step[j])]) / kDegToRad);
        }
        if (!rawLast.empty())  res.stats.humpsRaw      = countHumps(rawLast);
        if (!filtLast.empty()) res.stats.humpsFiltered = countHumps(filtLast);
    }
    res.stats.ran = !res.segments.empty();
    if (resN > 0) res.stats.residualRmsDeg = std::sqrt(resSq / resN) / kDegToRad;
    if (nisN > 0) res.stats.nisMean = nisSum / nisN;
    return res;
}

} // namespace pinpoint::analysis
