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

#include "session_traces.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace pinpoint::charts {

namespace {
bool isFinite(double x) { return std::isfinite(x); }
double tricube(double d) { const double a = 1.0 - std::pow(std::abs(d), 3.0); return std::pow(a, 3.0); }
}

std::vector<TraceCurve> alignedCurves(const SessionSnapshot &s, const SessionAlignment &al, const QString &key)
{
    std::vector<TraceCurve> out;
    out.reserve(s.shots.size());
    for (size_t i = 0; i < s.shots.size(); ++i) {
        const ShotSeries &sh = *s.shots[i];
        const ShotWarp *w = i < al.shots.size() ? &al.shots[i] : nullptr;
        if (!w || !w->aligned()) {
            TraceCurve e; e.ordinal = sh.ordinal; e.key = key; e.axis = TimeAxis::CanonicalMs;
            out.push_back(std::move(e));
            continue;
        }
        out.push_back(alignedCurve(shotCurve(sh, key), *w));
        out.back().ordinal = sh.ordinal;
        out.back().key = key;
    }
    return out;
}

std::vector<const TraceCurve *> curvesOf(const std::vector<TraceCurve> &curves, const std::vector<int> &ordinals)
{
    std::vector<const TraceCurve *> out;
    if (ordinals.empty()) {
        for (const TraceCurve &c : curves) if (!c.empty()) out.push_back(&c);
        return out;
    }
    for (int o : ordinals)
        for (const TraceCurve &c : curves)
            if (c.ordinal == o && !c.empty()) { out.push_back(&c); break; }
    return out;
}

// ── envelope ────────────────────────────────────────────────────────────────────────────────

SessionEnvelope envelope(const std::vector<const TraceCurve *> &curves, const std::vector<double> &grid, int minShots)
{
    SessionEnvelope e;
    e.t = grid;
    e.count = int(curves.size());
    const size_t G = grid.size();
    e.q1.assign(G, kNaN); e.med.assign(G, kNaN); e.q3.assign(G, kNaN); e.lo.assign(G, kNaN); e.hi.assign(G, kNaN);
    e.n.assign(G, 0);
    std::vector<double> xs;
    for (size_t i = 0; i < G; ++i) {
        xs.clear();
        for (const TraceCurve *c : curves) { const double v = valueAt(*c, grid[i]); if (isFinite(v)) xs.push_back(v); }
        e.n[i] = int(xs.size());
        if (int(xs.size()) < minShots) continue;
        e.q1[i]  = quantile7(xs, 0.25);
        e.med[i] = quantile7(xs, 0.5);
        e.q3[i]  = quantile7(xs, 0.75);
        e.lo[i]  = *std::min_element(xs.begin(), xs.end());
        e.hi[i]  = *std::max_element(xs.begin(), xs.end());
    }
    return e;
}

PartingPoint partAt(const SessionEnvelope &env, int nShots)
{
    PartingPoint p;
    const int need = std::max(3, int(std::ceil(0.8 * nShots)));
    const int n = int(env.t.size());
    const int a = int(std::floor(n * 0.1)), b = int(std::ceil(n * 0.9));
    int best = -1;
    double w = -1.0;
    for (int i = a; i < b && i < n; ++i) {
        if (env.n[size_t(i)] < need || !isFinite(env.q1[size_t(i)])) continue;
        const double d = env.q3[size_t(i)] - env.q1[size_t(i)];
        if (d > w) { w = d; best = i; }
    }
    if (best < 0)
        return p;
    p.found = true;
    p.tCanonMs = env.t[size_t(best)];
    p.iqr = w;
    p.q1 = env.q1[size_t(best)]; p.q3 = env.q3[size_t(best)]; p.med = env.med[size_t(best)];
    p.n = env.n[size_t(best)];
    return p;
}

// ── the group fit ───────────────────────────────────────────────────────────────────────────

std::vector<PooledSample> pooledSamples(const std::vector<TraceCurve> &curves, const std::vector<int> &ordinals)
{
    std::vector<PooledSample> out;
    for (const TraceCurve *c : curvesOf(curves, ordinals))
        for (size_t i = 0; i < c->t.size(); ++i)
            if (c->ok[i] && isFinite(c->raw[i])) out.push_back(PooledSample{ c->t[i], c->raw[i], c->ordinal });
    return out;
}

int GroupFit::drawnPoints() const
{
    return int(std::count_if(v.begin(), v.end(), [](double x) { return std::isfinite(x); }));
}

bool ShotCoverage::covers(double x) const
{
    for (const TimeSpan &r : runs)
        if (x >= r.t0 - edgeMs && x <= r.t1 + edgeMs) return true;
    return false;
}

namespace {
double medianSpacing(const std::vector<double> &t)
{
    std::vector<double> sp;
    for (size_t i = 1; i < t.size(); ++i) sp.push_back(t[i] - t[i - 1]);
    const double m = medianOf(sp);
    return (isFinite(m) && m > 0.0) ? m : 0.0;
}
}

ShotCoverage shotCoverage(const TraceCurve &c)
{
    ShotCoverage cov;
    cov.ordinal = c.ordinal;
    cov.runs = coverageRuns(c);
    cov.edgeMs = 0.5 * medianSpacing(c.t);
    return cov;
}

GroupFit pooledFit(std::vector<PooledSample> S, const std::vector<double> &grid, int groupSize, const FitOptions &o,
                   const std::vector<ShotCoverage> &coverageIn)
{
    GroupFit f;
    f.groupSize = groupSize;
    f.samplesPooled = int(S.size());
    f.t = grid;
    // gate rule: PP_GATE("rule.charts.groupMin")
    if (groupSize < o.minShots) {
        f.refusal = FitRefusal::GroupTooSmall;
        f.t.clear();
        return f;
    }
    const size_t G = grid.size();
    f.v.assign(G, kNaN); f.sd.assign(G, kNaN); f.se.assign(G, kNaN);
    f.nShots.assign(G, 0); f.nSamples.assign(G, 0);
    if (S.empty()) {
        f.refusal = FitRefusal::NoSamples;
        return f;
    }
    std::stable_sort(S.begin(), S.end(), [](const PooledSample &a, const PooledSample &b) { return a.tCanonMs < b.tCanonMs; });
    std::vector<double> ts(S.size());
    for (size_t i = 0; i < S.size(); ++i) ts[i] = S[i].tCanonMs;
    const auto lower = [&](double x) { return size_t(std::lower_bound(ts.begin(), ts.end(), x) - ts.begin()); };
    const int k = o.adaptiveK > 0 ? o.adaptiveK : 3 * groupSize;

    // Where each shot measured (the header's contribution rule). Derived from the samples when the
    // caller has no curves to give: runs broken at a gap wider than three median spacings.
    std::vector<ShotCoverage> coverage = coverageIn;
    if (o.requireCoverage && coverage.empty()) {
        std::map<int, std::vector<double>> byShot;
        for (const PooledSample &p : S) byShot[p.ordinal].push_back(p.tCanonMs);   // S is sorted
        for (const auto &[ord, t] : byShot) {
            ShotCoverage c;
            c.ordinal = ord;
            const double sp = medianSpacing(t);
            c.edgeMs = 0.5 * sp;
            const double gapMax = 3.0 * (sp > 0.0 ? sp : 1.0);
            size_t a = 0;
            for (size_t i = 1; i <= t.size(); ++i)
                if (i == t.size() || t[i] - t[i - 1] > gapMax) { c.runs.push_back(TimeSpan{ t[a], t[i - 1] }); a = i; }
            coverage.push_back(std::move(c));
        }
    }
    std::map<int, const ShotCoverage *> covOf;
    for (const ShotCoverage &c : coverage) covOf[c.ordinal] = &c;
    std::set<int> inAt;                 // the shots contributing at the current x
    const auto contributes = [&](int ordinal) { return !o.requireCoverage || inAt.count(ordinal) > 0; };

    std::set<int> shots;
    std::vector<double> ds;
    for (size_t gi = 0; gi < G; ++gi) {
        const double x = grid[gi];
        if (o.requireCoverage) {
            inAt.clear();
            for (const auto &[ord, c] : covOf) if (c->covers(x)) inAt.insert(ord);
        }
        double h = o.halfWidthMs;
        if (o.adaptive) {
            // B's bandwidth: the distance to the k-th nearest sample inside the widest window, ×1.05,
            // held to [min, max].
            const size_t a0 = lower(x - o.adaptiveMaxMs), b0 = lower(x + o.adaptiveMaxMs);
            ds.clear();
            for (size_t j = a0; j < b0; ++j) if (contributes(S[j].ordinal)) ds.push_back(std::abs(ts[j] - x));
            std::sort(ds.begin(), ds.end());
            h = std::max(o.adaptiveMinMs,
                         std::min(o.adaptiveMaxMs, int(ds.size()) >= k ? ds[size_t(k - 1)] * 1.05 : o.adaptiveMaxMs));
        }
        const size_t a = lower(x - h), b = lower(x + h);
        double sw = 0, swt = 0, swtt = 0, swv = 0, swtv = 0, sw2 = 0;
        int ns = 0;
        shots.clear();
        for (size_t j = a; j < b; ++j) {
            if (!contributes(S[j].ordinal)) continue;
            const double w = tricube((S[j].tCanonMs - x) / h);
            if (w <= 0) continue;
            const double tt = S[j].tCanonMs - x;
            sw += w; swt += w * tt; swtt += w * tt * tt; swv += w * S[j].value; swtv += w * tt * S[j].value; sw2 += w * w;
            shots.insert(S[j].ordinal);
            ++ns;
        }
        f.nShots[gi] = int(shots.size());
        f.nSamples[gi] = ns;
        // gate rule: PP_GATE("rule.charts.instantMin")
        if (int(shots.size()) < o.minShots || ns < o.minSamples || sw <= 0) continue;
        const double det = sw * swtt - swt * swt;
        double a0, b1;
        if (std::abs(det) < 1e-9) { a0 = swv / sw; b1 = 0; }
        else { a0 = (swtt * swv - swt * swtv) / det; b1 = (sw * swtv - swt * swv) / det; }
        double r2 = 0;
        for (size_t j = a; j < b; ++j) {
            if (!contributes(S[j].ordinal)) continue;
            const double w = tricube((S[j].tCanonMs - x) / h);
            if (w <= 0) continue;
            const double r = S[j].value - (a0 + b1 * (S[j].tCanonMs - x));
            r2 += w * r * r;
        }
        const double sd = std::sqrt(r2 / sw);
        f.v[gi] = a0; f.sd[gi] = sd; f.se[gi] = sd * std::sqrt(sw2) / sw;
    }
    return f;
}

GroupFit pooledFit(const std::vector<TraceCurve> &curves, const std::vector<int> &ordinals,
                   const std::vector<double> &grid, const FitOptions &o)
{
    std::vector<ShotCoverage> coverage;
    if (o.requireCoverage)
        for (const TraceCurve *c : curvesOf(curves, ordinals)) coverage.push_back(shotCoverage(*c));
    GroupFit f = pooledFit(pooledSamples(curves, ordinals), grid, int(ordinals.size()), o, coverage);
    f.ordinals = ordinals;
    return f;
}

// ── coverage ────────────────────────────────────────────────────────────────────────────────

std::vector<TimeSpan> coverageRuns(const TraceCurve &c)
{
    std::vector<TimeSpan> runs;
    const size_t n = c.t.size();
    if (!n)
        return runs;
    std::vector<double> sp;
    for (size_t i = 1; i < n; ++i) sp.push_back(c.t[i] - c.t[i - 1]);
    double m = medianOf(sp);
    if (!isFinite(m) || m == 0.0) m = 1.0;                    // the mock's `median(sp) || 1`
    const double gapMax = 3.0 * m;
    long start = -1;
    for (size_t i = 0; i < n; ++i) {
        const bool ok = c.ok[i] != 0;
        if (ok && start < 0) start = long(i);
        const bool brk = !ok || (i + 1 < n && c.t[i + 1] - c.t[i] > gapMax) || i == n - 1;
        if (start >= 0 && brk) {
            const long end = ok ? long(i) : long(i) - 1;
            if (end >= start) runs.push_back(TimeSpan{ c.t[size_t(start)], c.t[size_t(end)] });
            start = -1;
        }
    }
    return runs;
}

std::vector<TimeSpan> coverageHoles(const std::vector<TimeSpan> &runs, double t0, double t1)
{
    std::vector<TimeSpan> out;
    double cur = t0;
    for (const TimeSpan &r : runs) {
        if (r.t1 < t0 || r.t0 > t1) continue;
        if (r.t0 > cur + 0.5) out.push_back(TimeSpan{ cur, std::min(r.t0, t1) });
        cur = std::max(cur, r.t1);
    }
    if (cur < t1 - 0.5) out.push_back(TimeSpan{ cur, t1 });
    return out;
}

// ── shapes and typical shots ────────────────────────────────────────────────────────────────

ShapeSplit shapeSplit(const std::vector<std::vector<TraceCurve>> &perKey, double t0, double t1, int minEach)
{
    ShapeSplit out;
    if (perKey.empty())
        return out;
    // the shots taking part: those with a curve for at least one key
    std::vector<int> ords;
    std::vector<std::vector<const TraceCurve *>> cs(perKey.size());
    for (size_t i = 0; i < perKey[0].size(); ++i) {
        bool any = false;
        for (const auto &k : perKey) if (i < k.size() && !k[i].empty()) any = true;
        if (!any) continue;
        ords.push_back(perKey[0][i].ordinal);
        for (size_t k = 0; k < perKey.size(); ++k) cs[k].push_back(i < perKey[k].size() ? &perKey[k][i] : nullptr);
    }
    const size_t n = ords.size();
    // gate rule: PP_GATE("rule.charts.kindsMin")
    if (n < 6)
        return out;
    const std::vector<double> g = timeGrid(t0, t1, std::max(2.0, (t1 - t0) / 120.0));
    struct Per { std::vector<std::vector<double>> vals; double iqr; };
    std::vector<Per> per;
    for (size_t k = 0; k < perKey.size(); ++k) {
        Per p;
        p.vals.assign(n, std::vector<double>(g.size(), kNaN));
        for (size_t i = 0; i < n; ++i)
            if (cs[k][i] && !cs[k][i]->empty())
                for (size_t q = 0; q < g.size(); ++q) p.vals[i][q] = valueAt(*cs[k][i], g[q]);
        std::vector<double> w;
        std::vector<double> col(n);
        for (size_t q = 0; q < g.size(); ++q) {
            for (size_t i = 0; i < n; ++i) col[i] = p.vals[i][q];
            const double d = quantile7(col, 0.75) - quantile7(col, 0.25);
            if (isFinite(d)) w.push_back(d);
        }
        p.iqr = medianOf(w);
        if (!isFinite(p.iqr) || p.iqr == 0.0) p.iqr = 1.0;      // the mock's `|| 1`
        per.push_back(std::move(p));
    }
    std::vector<std::vector<double>> D(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            double acc = 0; int k = 0;
            for (const Per &p : per)
                for (size_t q = 0; q < g.size(); ++q) {
                    const double a = p.vals[i][q], b = p.vals[j][q];
                    if (isFinite(a) && isFinite(b)) { acc += std::abs(a - b) / p.iqr; ++k; }
                }
            D[i][j] = D[j][i] = k ? acc / k : 99.0;
        }
    // two medoids (the mock's twoMedoids: exhaustive, ties to the first medoid, first best kept)
    for (size_t a = 0; a < n; ++a)
        for (size_t b = a + 1; b < n; ++b) {
            double cost = 0;
            std::vector<int> ga, gb;
            for (size_t i = 0; i < n; ++i) {
                if (D[i][a] <= D[i][b]) { cost += D[i][a]; ga.push_back(ords[i]); }
                else                    { cost += D[i][b]; gb.push_back(ords[i]); }
            }
            if (int(ga.size()) < minEach || int(gb.size()) < minEach) continue;
            if (!out.found || cost < out.cost) {
                out.found = true; out.cost = cost; out.medoidA = ords[a]; out.medoidB = ords[b];
                out.groupA = ga; out.groupB = gb;
            }
        }
    return out;
}

namespace {
ShotScore scoreShots(const std::vector<std::vector<TraceCurve>> &perKey, double t0, double t1, int exclude, bool furthest)
{
    ShotScore best;
    if (perKey.empty())
        return best;
    const std::vector<double> grid = sessionGrid(t0, t1);
    std::vector<SessionEnvelope> env;
    std::vector<double> iqr;
    for (const auto &k : perKey) {
        env.push_back(envelope(curvesOf(k), grid));
        std::vector<double> w;
        for (size_t i = 0; i < grid.size(); ++i) w.push_back(env.back().q3[i] - env.back().q1[i]);
        iqr.push_back(medianOf(w));
    }
    double bv = furthest ? -1.0 : std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < perKey[0].size(); ++i) {
        const int ord = perKey[0][i].ordinal;
        if (ord == exclude) continue;
        double acc = 0; int k = 0;
        for (size_t key = 0; key < perKey.size(); ++key) {
            if (i >= perKey[key].size() || perKey[key][i].empty()) continue;
            const double g = meanGap(perKey[key][i], env[key].t, env[key].med, t0, t1);
            if (isFinite(g) && iqr[key] > 0) { acc += g / iqr[key]; ++k; }
        }
        if (k && (furthest ? acc / k > bv : acc / k < bv)) { bv = acc / k; best.ordinal = ord; best.score = bv; }
    }
    return best;
}
}

ShotScore typicalShot(const std::vector<std::vector<TraceCurve>> &perKey, double t0, double t1, int exclude)
{
    return scoreShots(perKey, t0, t1, exclude, false);
}

ShotScore leastTypicalShot(const std::vector<std::vector<TraceCurve>> &perKey, double t0, double t1, int exclude)
{
    return scoreShots(perKey, t0, t1, exclude, true);
}

// ── against a reference ─────────────────────────────────────────────────────────────────────

TraceGap gapAt(const TraceCurve &c, const std::vector<double> &refT, const std::vector<double> &refV, double t0, double t1)
{
    TraceGap best;
    for (size_t i = 0; i < refT.size() && i < refV.size(); ++i) {
        const double x = refT[i];
        if (x < t0 || x > t1 || !isFinite(refV[i])) continue;
        const double v = valueAt(c, x);
        if (!isFinite(v)) continue;
        const double d = v - refV[i];
        if (!best.found || std::abs(d) > std::abs(best.diff)) { best = TraceGap{ true, x, d, v, refV[i] }; }
    }
    return best;
}

std::vector<double> diffFrom(const TraceCurve &c, const std::vector<double> &refT, const std::vector<double> &refV)
{
    std::vector<double> out(refT.size(), kNaN);
    for (size_t i = 0; i < refT.size() && i < refV.size(); ++i) {
        const double a = valueAt(c, refT[i]);
        if (isFinite(a) && isFinite(refV[i])) out[i] = a - refV[i];
    }
    return out;
}

double meanGap(const TraceCurve &c, const std::vector<double> &refT, const std::vector<double> &refV, double t0, double t1)
{
    double s = 0; int k = 0;
    for (size_t i = 0; i < refT.size() && i < refV.size(); ++i) {
        if (refT[i] < t0 || refT[i] > t1 || !isFinite(refV[i])) continue;
        const double a = valueAt(c, refT[i]);
        if (!isFinite(a)) continue;
        s += std::abs(a - refV[i]); ++k;
    }
    return k ? s / k : kNaN;
}

} // namespace pinpoint::charts
