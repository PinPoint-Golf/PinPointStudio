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

#include "session_relations.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace pinpoint::charts {

namespace {

bool isFinite(double x) { return std::isfinite(x); }

std::vector<double> finiteOf(const std::vector<double> &a)
{
    std::vector<double> out;
    for (double x : a) if (isFinite(x)) out.push_back(x);
    return out;
}

double meanOf(const std::vector<double> &a)
{
    if (a.empty()) return kNaN;
    double s = 0; for (double x : a) s += x;
    return s / double(a.size());
}

double varianceOf(const std::vector<double> &a)
{
    if (a.size() < 2) return 0.0;
    const double m = meanOf(a);
    double s = 0; for (double x : a) s += (x - m) * (x - m);
    return s / double(a.size() - 1);
}

// The first finite value a series' phase samples carry at `phase` (the mock's atOf).
double valueAtPhase(const MetricSeries *m, int phase)
{
    if (!m) return kNaN;
    for (const PhaseSample &p : m->phaseSamples)
        if (p.phase == phase && isFinite(p.value)) return p.value;
    return kNaN;
}

// The ladder position of a phase, for "the latest sampled P-position" (non-ladder phases after it).
int ladderRank(int ph)
{
    const auto &l = pLadder();
    const auto it = std::find(l.begin(), l.end(), ph);
    return it == l.end() ? 100 + ph : int(it - l.begin());
}

// Robust standardised rows → the RMS distance between shots (Stats.js distances).
std::vector<std::vector<double>> distanceMatrix(const std::vector<const SessionReading *> &rows, size_t nShots)
{
    std::vector<std::vector<double>> Z;
    for (const SessionReading *r : rows) {
        const BoxStats &b = r->box;
        const double s = b.iqr > 0 ? b.iqr / 1.349 : 1.0;
        std::vector<double> z;
        for (double v : r->values) z.push_back(std::max(-2.5, std::min(2.5, (v - b.med) / s)));
        Z.push_back(std::move(z));
    }
    std::vector<std::vector<double>> D(nShots, std::vector<double>(nShots, 0.0));
    for (size_t i = 0; i < nShots; ++i)
        for (size_t j = 0; j < nShots; ++j) {
            double s = 0;
            for (const auto &z : Z) { const double d = z[i] - z[j]; s += d * d; }
            D[i][j] = std::sqrt(s / double(std::max<size_t>(1, Z.size())));
        }
    return D;
}

double silhouetteOf(const std::vector<std::vector<double>> &D, const std::vector<int> &lab)
{
    const size_t n = D.size();
    if (!n) return kNaN;
    double tot = 0;
    for (size_t i = 0; i < n; ++i) {
        double sums[2] = { 0, 0 }; int cnt[2] = { 0, 0 };
        for (size_t j = 0; j < n; ++j) if (j != i) { sums[lab[j]] += D[i][j]; cnt[lab[j]]++; }
        const double a = cnt[lab[i]] ? sums[lab[i]] / cnt[lab[i]] : 0.0;
        double b = std::numeric_limits<double>::infinity();
        for (int c = 0; c < 2; ++c) if (c != lab[i] && cnt[c]) b = std::min(b, sums[c] / cnt[c]);
        tot += (isFinite(b) && std::max(a, b) > 0) ? (b - a) / std::max(a, b) : 0.0;
    }
    return tot / double(n);
}

int medoidOf(const std::vector<std::vector<double>> &D, const std::vector<int> &members)
{
    int best = -1; double bs = std::numeric_limits<double>::infinity();
    for (int i : members) {
        double s = 0; for (int j : members) s += D[size_t(i)][size_t(j)];
        if (s < bs) { bs = s; best = i; }
    }
    return best;
}

struct GapSplit {
    bool   found = false;
    double gap = kNaN, score = kNaN, cut = kNaN, low = kNaN, high = kNaN;
    std::vector<int> lowIdx, highIdx;
};

GapSplit bestSplit(const std::vector<double> &values, int minSize)
{
    GapSplit best;
    std::vector<int> idx;
    for (size_t i = 0; i < values.size(); ++i) if (isFinite(values[i])) idx.push_back(int(i));
    if (int(idx.size()) < 2 * minSize) return best;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return values[size_t(a)] < values[size_t(b)]; });
    const BoxStats b = boxStats(values);
    const double s = b.iqr > 0 ? b.iqr : 1.0;
    for (int c = minSize; c <= int(idx.size()) - minSize; ++c) {
        const double lo = values[size_t(idx[size_t(c - 1)])], hi = values[size_t(idx[size_t(c)])];
        const double gap = hi - lo;
        if (!best.found || gap > best.gap) {
            best.found = true; best.gap = gap; best.score = gap / s; best.cut = (hi + lo) / 2;
            best.low = lo; best.high = hi;
            best.lowIdx.assign(idx.begin(), idx.begin() + c);
            best.highIdx.assign(idx.begin() + c, idx.end());
        }
    }
    return best;
}

std::vector<int> toOrdinals(std::vector<int> idx)
{
    std::sort(idx.begin(), idx.end());
    for (int &i : idx) i += 1;
    return idx;
}

} // namespace

// ── box ─────────────────────────────────────────────────────────────────────────────────────

// ── readings ────────────────────────────────────────────────────────────────────────────────

std::vector<SessionReading> sessionReadings(const SessionSnapshot &s, const ReadAtPhases &readAt, const QStringList &keyOrder)
{
    std::vector<SessionReading> out;
    QStringList keys = keyOrder;
    if (keys.isEmpty()) {
        QSet<QString> seen;
        for (const auto &sh : s.shots)
            for (const QString &k : sh->metricOrder)
                if (!seen.contains(k)) { seen.insert(k); keys << k; }
    }
    const size_t N = s.shots.size();
    for (const QString &key : keys) {
        const MetricSeries *first = nullptr;
        for (const auto &sh : s.shots) if ((first = sh->metric(key))) break;
        if (!first) continue;
        const auto avail = [&](int p) {
            for (const auto &sh : s.shots) if (isFinite(valueAtPhase(sh->metric(key), p))) return true;
            return false;
        };
        std::vector<int> phs;
        if (readAt) for (int p : readAt(key)) if (avail(p) && std::find(phs.begin(), phs.end(), p) == phs.end()) phs.push_back(p);
        if (phs.empty()) {
            std::vector<int> sampled;
            for (const auto &sh : s.shots)
                if (const MetricSeries *m = sh->metric(key))
                    for (const PhaseSample &p : m->phaseSamples)
                        if (isFinite(p.value) && std::find(sampled.begin(), sampled.end(), p.phase) == sampled.end())
                            sampled.push_back(p.phase);
            std::stable_sort(sampled.begin(), sampled.end(), [](int a, int b) { return ladderRank(a) < ladderRank(b); });
            if (std::find(sampled.begin(), sampled.end(), phase::Impact) != sampled.end()) phs = { phase::Impact };
            else if (!sampled.empty()) phs = { sampled.back() };
        }
        for (int ph : phs) {
            SessionReading r;
            r.key = key; r.phase = ph; r.id = key + QLatin1Char('@') + QString::number(ph);
            r.label = first->label; r.unit = first->unit;
            r.multi = phs.size() > 1;
            r.values.resize(N, kNaN);
            for (size_t i = 0; i < N; ++i) r.values[i] = valueAtPhase(s.shots[i]->metric(key), ph);
            r.box = boxStats(r.values);
            // gate rule: PP_GATE("rule.charts.boxMin")
            if (r.box.n < 3) continue;
            r.fired.assign(N, 0);
            for (auto it = s.conditions.constBegin(); it != s.conditions.constEnd(); ++it) {
                const ConditionInfo &ci = it.value();
                if (ci.metricKey != key) continue;
                if (!(ci.phase == ph || (ci.phase < 0 && phs.size() == 1))) continue;
                r.conditionIds << it.key();
                for (size_t i = 0; i < N; ++i)
                    if (s.shots[i]->diagnostics.fired.contains(it.key())) r.fired[i] = 1;
            }
            r.conditionIds.sort();
            out.push_back(std::move(r));
        }
    }
    return out;
}

ReadingPlace placeOf(const SessionReading &r, int shotIndex)
{
    ReadingPlace p;
    if (shotIndex < 0 || size_t(shotIndex) >= r.values.size()) return p;
    const double x = r.values[size_t(shotIndex)];
    if (!isFinite(x)) return p;
    const BoxStats &b = r.box;
    p.measured = true;
    p.value = x;
    const double sc = b.iqr > 0 ? b.iqr / 1.349 : kNaN;
    p.rz = isFinite(sc) ? (x - b.med) / sc : 0.0;
    std::vector<double> f = finiteOf(r.values);
    std::sort(f.begin(), f.end(), [](double a, double c) { return a > c; });
    const auto first = std::find(f.begin(), f.end(), x);
    const auto last = std::find(f.rbegin(), f.rend(), x);
    p.rankHi = int(first - f.begin()) + 1;
    p.rankLo = int(f.size()) - (int(f.size()) - 1 - int(last - f.rbegin()));
    p.side = x > b.q3 + 1.5 * b.iqr + 1e-9 ? 1 : x < b.q1 - 1.5 * b.iqr - 1e-9 ? -1 : 0;
    return p;
}

std::vector<int> apartFrom(const std::vector<SessionReading> &readings, int shotIndex)
{
    std::vector<int> idx;
    std::vector<double> rz(readings.size(), 0.0);
    for (size_t i = 0; i < readings.size(); ++i) {
        const ReadingPlace p = placeOf(readings[i], shotIndex);
        if (p.side != 0) { idx.push_back(int(i)); rz[i] = std::abs(p.rz); }
    }
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return rz[size_t(a)] > rz[size_t(b)]; });
    return idx;
}

// ── relations ───────────────────────────────────────────────────────────────────────────────

Correlation pearson(const std::vector<double> &xs, const std::vector<double> &ys)
{
    Correlation c;
    std::vector<double> px, py;
    for (size_t i = 0; i < std::min(xs.size(), ys.size()); ++i)
        if (isFinite(xs[i]) && isFinite(ys[i])) { px.push_back(xs[i]); py.push_back(ys[i]); }
    const int n = int(px.size());
    c.n = n;
    // gate rule: PP_GATE("rule.charts.correlationMin")
    if (n < 5) return c;
    double mx = 0, my = 0;
    for (int i = 0; i < n; ++i) { mx += px[size_t(i)]; my += py[size_t(i)]; }
    mx /= n; my /= n;
    double sxx = 0, syy = 0, sxy = 0;
    for (int i = 0; i < n; ++i) {
        const double a = px[size_t(i)] - mx, b = py[size_t(i)] - my;
        sxx += a * a; syy += b * b; sxy += a * b;
    }
    if (sxx <= 0 || syy <= 0) return c;
    c.defined = true;
    c.r = sxy / std::sqrt(sxx * syy);
    const double rc = std::max(-0.999999, std::min(0.999999, c.r));
    const double z = 0.5 * std::log((1 + rc) / (1 - rc)), se = 1 / std::sqrt(double(n - 3));
    c.lo = std::tanh(z - 1.96 * se);
    c.hi = std::tanh(z + 1.96 * se);
    c.slope = sxy / sxx;
    c.intercept = my - c.slope * mx;
    c.meanX = mx; c.meanY = my;
    c.clear = c.lo > 0 || c.hi < 0;
    return c;
}

double rCritical(int n) { return n > 3 ? std::tanh(1.96 / std::sqrt(double(n - 3))) : 1.0; }
int chanceCount(int tests) { return int(std::lround(tests * 0.05)); }

QString sameQuantityPartner(const QString &key)
{
    // The mock's catalogue (Bench.qml sameQuantity), keys checked against the 4 July session.
    static const QHash<QString, QString> pairs {
        { QStringLiteral("clubheadSpeed"),        QStringLiteral("clubAngularSpeed") },
        { QStringLiteral("clubAngularSpeed"),     QStringLiteral("clubheadSpeed") },
        { QStringLiteral("pelvisRotation"),       QStringLiteral("pelvisRotationSigned") },
        { QStringLiteral("pelvisRotationSigned"), QStringLiteral("pelvisRotation") },
        { QStringLiteral("thoraxRotation"),       QStringLiteral("thoraxRotationSigned") },
        { QStringLiteral("thoraxRotationSigned"), QStringLiteral("thoraxRotation") },
        { QStringLiteral("xFactor"),              QStringLiteral("xFactorStretch") },
        { QStringLiteral("xFactorStretch"),       QStringLiteral("xFactor") },
        { QStringLiteral("stanceWidth"),          QStringLiteral("stanceWidthMm") },
        { QStringLiteral("stanceWidthMm"),        QStringLiteral("stanceWidth") },
        { QStringLiteral("pelvisAngularSpeed"),   QStringLiteral("pelvisPeakTime") },
        { QStringLiteral("pelvisPeakTime"),       QStringLiteral("pelvisAngularSpeed") },
    };
    return pairs.value(key);
}

bool sameQuantityAs(const QString &a, const QString &b)
{
    return a == b || sameQuantityPartner(a) == b;
}

std::vector<CorrRow> correlate(const std::vector<SessionReading> &readings, int y)
{
    std::vector<CorrRow> out;
    if (y < 0 || size_t(y) >= readings.size()) return out;
    const SessionReading &Y = readings[size_t(y)];
    for (size_t i = 0; i < readings.size(); ++i) {
        if (int(i) == y) continue;
        const Correlation c = pearson(readings[i].values, Y.values);
        if (!c.defined) continue;
        out.push_back(CorrRow{ int(i), sameQuantityAs(readings[i].key, Y.key), c });
    }
    std::stable_sort(out.begin(), out.end(), [](const CorrRow &a, const CorrRow &b) { return std::abs(a.c.r) > std::abs(b.c.r); });
    return out;
}

Correlation pairOf(const SessionReading &x, const SessionReading &y) { return pearson(x.values, y.values); }

Correlation driftOf(const SessionReading &r)
{
    std::vector<double> order(r.values.size());
    std::iota(order.begin(), order.end(), 1.0);
    return pearson(order, r.values);
}

std::vector<CorrRow> drifts(const std::vector<SessionReading> &readings)
{
    std::vector<CorrRow> out;
    for (size_t i = 0; i < readings.size(); ++i) {
        const Correlation c = driftOf(readings[i]);
        if (c.defined) out.push_back(CorrRow{ int(i), false, c });
    }
    std::stable_sort(out.begin(), out.end(), [](const CorrRow &a, const CorrRow &b) { return std::abs(a.c.r) > std::abs(b.c.r); });
    return out;
}

// ── kinds ───────────────────────────────────────────────────────────────────────────────────

double cohenD(const std::vector<double> &a, const std::vector<double> &b)
{
    const std::vector<double> A = finiteOf(a), B = finiteOf(b);
    if (A.size() < 3 || B.size() < 3) return kNaN;
    const double sp = std::sqrt(((A.size() - 1) * varianceOf(A) + (B.size() - 1) * varianceOf(B)) / double(A.size() + B.size() - 2));
    if (!(sp > 0)) return kNaN;
    return (meanOf(A) - meanOf(B)) / sp;
}

double dCritical(int nA, int nB)
{
    if (nA <= 0 || nB <= 0) return kNaN;
    const int df = nA + nB - 2;
    const double t = 1.96 + 2.4 / std::max(1, df);
    return t * std::sqrt(1.0 / nA + 1.0 / nB);
}

SessionKinds findKinds(const std::vector<SessionReading> &rows, const KindsOptions &o)
{
    SessionKinds k;
    if (rows.empty()) return k;
    const size_t N = rows.front().values.size();
    k.minSize = std::max(o.minSize, int(std::ceil(double(N) * o.minShare)));

    std::vector<const SessionReading *> full;
    for (const SessionReading &r : rows)
        if (std::all_of(r.values.begin(), r.values.end(), [](double v) { return std::isfinite(v); }) && r.box.iqr > 0)
            full.push_back(&r);
    k.readingsUsed = int(full.size());
    const auto D = distanceMatrix(full, N);

    // k-medoids, k = 2, exhaustive
    double bestCost = kNaN;
    std::vector<int> bestLab;
    int ma = -1, mb = -1;
    for (size_t a = 0; a < N; ++a)
        for (size_t b = a + 1; b < N; ++b) {
            std::vector<int> lab(N);
            int cnt[2] = { 0, 0 };
            double cost = 0;
            for (size_t i = 0; i < N; ++i) {
                const int c = D[i][a] <= D[i][b] ? 0 : 1;
                lab[i] = c; cnt[c]++; cost += D[i][c ? b : a];
            }
            if (std::min(cnt[0], cnt[1]) < k.minSize) continue;
            if (!isFinite(bestCost) || cost < bestCost) { bestCost = cost; bestLab = lab; ma = int(a); mb = int(b); }
        }
    if (!bestLab.empty()) {
        k.silhouette = silhouetteOf(D, bestLab);
        std::vector<int> A, B;
        for (size_t i = 0; i < N; ++i) (bestLab[i] == 0 ? A : B).push_back(int(i));
        k.kmA = toOrdinals(A); k.kmB = toOrdinals(B);
        k.kmMedoidA = ma + 1; k.kmMedoidB = mb + 1;
    }

    // the cleanest single break
    GapSplit best;
    for (size_t i = 0; i < rows.size(); ++i) {
        const GapSplit sp = bestSplit(rows[i].values, k.minSize);
        if (sp.found && (!best.found || sp.score > best.score)) { best = sp; k.splitReading = int(i); }
    }
    if (best.found) {
        k.splitGap = best.gap; k.splitScore = best.score; k.splitCut = best.cut;
        k.splitLow = best.low; k.splitHigh = best.high;
    }

    std::vector<int> A, B;   // shot indices
    if (!bestLab.empty() && k.silhouette >= o.minSilhouette) {
        k.method = KindsMethod::KMedoids;
        for (size_t i = 0; i < N; ++i) (bestLab[i] == 0 ? A : B).push_back(int(i));
    } else if (best.found) {
        k.method = KindsMethod::GapSplit;
        A = best.highIdx; B = best.lowIdx;
    } else {
        return k;
    }
    std::sort(A.begin(), A.end()); std::sort(B.begin(), B.end());
    k.groupA = toOrdinals(A); k.groupB = toOrdinals(B);
    k.medoidA = medoidOf(D, A) + 1; k.medoidB = medoidOf(D, B) + 1;

    for (size_t i = 0; i < rows.size(); ++i) {
        std::vector<double> va, vb;
        for (int j : A) va.push_back(rows[i].values[size_t(j)]);
        for (int j : B) vb.push_back(rows[i].values[size_t(j)]);
        const double d = cohenD(va, vb);
        if (!isFinite(d)) continue;
        const std::vector<double> fa = finiteOf(va), fb = finiteOf(vb);
        k.separations.push_back(KindSeparation{ int(i), d, quantile7(fa, 0.5), quantile7(fb, 0.5), int(fa.size()), int(fb.size()) });
    }
    std::stable_sort(k.separations.begin(), k.separations.end(),
                     [](const KindSeparation &a, const KindSeparation &b) { return std::abs(a.d) > std::abs(b.d); });
    k.dCritical = dCritical(int(A.size()), int(B.size()));
    return k;
}

// ── facets ──────────────────────────────────────────────────────────────────────────────────

std::vector<SessionFacet> conditionFacets(const SessionSnapshot &s, int minEachSide)
{
    const int count = s.count();
    QStringList order;              // first-fired order (the mock's object-key order)
    QHash<QString, int> n;
    for (const auto &sh : s.shots)
        for (const QString &id : sh->diagnostics.fired) {
            if (!n.contains(id)) order << id;
            n[id] += 1;
        }
    std::vector<SessionFacet> out;
    for (const QString &id : order) {
        const int f = n.value(id);
        if (f < minEachSide || f > count - minEachSide) continue;
        SessionFacet facet;
        facet.kind = FacetKind::Condition; facet.id = id; facet.firedCount = f;
        FacetGroup with{ QStringLiteral("with"), {} }, without{ QStringLiteral("without"), {} };
        for (const auto &sh : s.shots) (sh->diagnostics.fired.contains(id) ? with : without).ordinals.push_back(sh->ordinal);
        facet.groups = { with, without };
        out.push_back(std::move(facet));
    }
    std::stable_sort(out.begin(), out.end(), [count](const SessionFacet &a, const SessionFacet &b) {
        const double da = std::abs(a.firedCount - count / 2.0), db = std::abs(b.firedCount - count / 2.0);
        if (da != db) return da < db;
        return a.firedCount > b.firedCount;
    });
    return out;
}

SessionFacet halfFacet(const SessionSnapshot &s)
{
    SessionFacet f;
    f.kind = FacetKind::Half;
    const int h = s.count() / 2;
    FacetGroup a{ QStringLiteral("first"), {} }, b{ QStringLiteral("second"), {} };
    for (const auto &sh : s.shots) (sh->ordinal <= h ? a : b).ordinals.push_back(sh->ordinal);
    f.groups = { a, b };
    return f;
}

std::optional<SessionFacet> clubFacet(const SessionSnapshot &s)
{
    SessionFacet f;
    f.kind = FacetKind::Club;
    for (const auto &sh : s.shots) {
        auto it = std::find_if(f.groups.begin(), f.groups.end(), [&](const FacetGroup &g) { return g.key == sh->club; });
        if (it == f.groups.end()) { f.groups.push_back(FacetGroup{ sh->club, {} }); it = f.groups.end() - 1; }
        it->ordinals.push_back(sh->ordinal);
    }
    if (f.groups.size() < 2) return std::nullopt;
    return f;
}

std::optional<SessionFacet> shapeFacet(const ShapeSplit &split)
{
    if (!split.found) return std::nullopt;
    SessionFacet f;
    f.kind = FacetKind::Shape;
    f.groups = { FacetGroup{ QStringLiteral("a"), split.groupA }, FacetGroup{ QStringLiteral("b"), split.groupB } };
    return f;
}

} // namespace pinpoint::charts
