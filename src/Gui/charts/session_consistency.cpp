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

#include "session_consistency.h"

#include "session_curves.h"     // shotCurve, valueAt, quantile7, medianOf
#include "session_traces.h"     // alignedCurves

#include <QHash>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace pinpoint::charts {

namespace {

bool isFinite(double x) { return std::isfinite(x); }

bool anyFinite(const std::vector<double> &a) { return std::any_of(a.begin(), a.end(), isFinite); }

// Stamped at impact, but they time the backswing: read at P3-P4 and treated as resting on the P-ladder.
bool isBackswingTiming(const QString &key)
{
    return key == QLatin1String("tempoBackswing") || key == QLatin1String("tempoRatio");
}

// A series' phase sample: the first with a finite value, as session_relations reads one. Null if none.
const PhaseSample *phaseSampleOf(const MetricSeries *m, int phase)
{
    if (!m) return nullptr;
    for (const PhaseSample &p : m->phaseSamples)
        if (p.phase == phase && isFinite(p.value)) return &p;
    return nullptr;
}

// A launch-monitor reading: its phase sample at any phase.
double ballReading(const ShotSeries &shot, const QString &key)
{
    const MetricSeries *m = shot.metric(key);
    if (!m) return kNaN;
    for (const PhaseSample &p : m->phaseSamples)
        if (isFinite(p.value)) return p.value;
    return kNaN;
}

// The session's metrics, by key, with what the rules need to know across the shots.
struct KeyFacts {
    QString label, unit;
    bool    hasCurve = false;
};

std::map<QString, KeyFacts> metricKeys(const SessionSnapshot &s)
{
    std::map<QString, KeyFacts> keys;
    for (const auto &sh : s.shots)
        for (auto it = sh->metrics.constBegin(); it != sh->metrics.constEnd(); ++it) {
            if (it.key().startsWith(QLatin1String("lm."))) continue;
            KeyFacts &f = keys[it.key()];
            if (f.label.isEmpty()) f.label = it.value().label;
            if (f.unit.isEmpty()) f.unit = it.value().unit;
            f.hasCurve = f.hasCurve || it.value().hasCurve();
        }
    return keys;
}

// The median over the shots of the series' own σ: a span's own σ, and a position's without phase σ.
double seriesSigma(const SessionSnapshot &s, const QString &key)
{
    std::vector<double> sigmas;
    for (const auto &sh : s.shots)
        if (const MetricSeries *m = sh->metric(key)) sigmas.push_back(m->sigma);
    return medianOf(std::move(sigmas));
}

// The median over the shots with a value of the norm's σ for that shot's club.
double normSigmaOf(const ConsistencyReading &r, const std::vector<ConsistencyShot> &shots, const ConsistencyEnv &env)
{
    if (!env.normSigma) return kNaN;
    const int phase = kConsistencyPhases[r.column];
    QHash<QString, double> byClub;
    std::vector<double> sigmas;
    for (size_t i = 0; i < shots.size(); ++i) {
        bool has = false;
        for (int j = 0; j < r.samples && !has; ++j) has = isFinite(r.at(int(i), j));
        if (!has) continue;
        auto it = byClub.constFind(shots[i].club);
        if (it == byClub.constEnd()) it = byClub.insert(shots[i].club, env.normSigma(r.key, phase, shots[i].club));
        sigmas.push_back(it.value());
    }
    return medianOf(std::move(sigmas));
}

ConsistencyReading readingOf(const QString &key, const KeyFacts &facts, const QString &area, int column,
                             ConsistencyReadingKind kind, size_t shots)
{
    ConsistencyReading r;
    r.key = key; r.label = facts.label; r.unit = facts.unit; r.area = area;
    r.column = column;
    r.kind = kind;
    r.samples = kind == ConsistencyReadingKind::Span ? kConsistencySamplesPerSpan : 1;
    r.values.assign(shots * size_t(r.samples), kNaN);
    return r;
}

// The sample inside a span: tA, tB on any one axis.
double spanTime(double tA, double tB, int j)
{
    return tA + (tB - tA) * (double(j) + 0.5) / double(kConsistencySamplesPerSpan);
}

// The P-aligned reading of a span, between two canonical P-times. No reading when either has none.
bool sampleSpan(const std::vector<TraceCurve> &aligned, const SessionAlignment &al, ConsistencyReading &r)
{
    const CanonicalPhase *a = al.canonicalOf(kConsistencyPhases[r.column - 1]);
    const CanonicalPhase *b = al.canonicalOf(kConsistencyPhases[r.column]);
    if (!a || !b) return false;
    const size_t n = aligned.size();
    for (size_t i = 0; i < n; ++i)
        for (int j = 0; j < r.samples; ++j)
            r.values[i * size_t(r.samples) + size_t(j)] = valueAt(aligned[i], spanTime(a->tMs, b->tMs, j));
    return true;
}

// Column 7: the same real time after impact on every shot, on its own clock.
void sampleAfterImpact(const SessionSnapshot &s, const QString &key, double lengthMs, ConsistencyReading &r)
{
    for (size_t i = 0; i < s.shots.size(); ++i) {
        const TraceCurve own = shotCurve(*s.shots[i], key);
        for (int j = 0; j < r.samples; ++j)
            r.values[i * size_t(r.samples) + size_t(j)] = valueAt(own, spanTime(0.0, lengthMs, j));
    }
}

// The phase sample at the column's P. Its own σ is the median of those samples', else the series'.
ConsistencyReading positionReading(const SessionSnapshot &s, const QString &key, const KeyFacts &facts,
                                   const QString &area, int column, int phase)
{
    ConsistencyReading r = readingOf(key, facts, area, column, ConsistencyReadingKind::Position, s.shots.size());
    std::vector<double> sigmas;
    for (size_t i = 0; i < s.shots.size(); ++i)
        if (const PhaseSample *p = phaseSampleOf(s.shots[i]->metric(key), phase)) {
            r.values[i] = p->value;
            sigmas.push_back(p->sigma);
        }
    r.ownSigma = medianOf(std::move(sigmas));
    if (!isFinite(r.ownSigma)) r.ownSigma = seriesSigma(s, key);
    return r;
}

} // namespace

bool consistencyGuarded(const ShotSeries &shot, const ShotWarp *warp)
{
    const auto hasCurve = [](const MetricSeries &m) { return m.hasCurve(); };
    if (std::none_of(shot.metrics.cbegin(), shot.metrics.cend(), hasCurve)) return false;

    const auto throughP7 = [](int phase) {
        return std::find(kConsistencyPhases, kConsistencyPhases + kConsistencyAfterBallColumn, phase)
            != kConsistencyPhases + kConsistencyAfterBallColumn;
    };
    // gate rule: PP_GATE("rule.charts.consistencyLadderGuard")
    if (!warp || warp->route != AlignRoute::PAnchors) return true;
    const auto kept = std::count_if(warp->anchors.begin(), warp->anchors.end(),
                                    [&](const WarpAnchor &a) { return throughP7(a.phase); });
    if (kept < kConsistencyMinKeptAnchors) return true;
    return std::any_of(warp->considered.begin(), warp->considered.end(), [&](const GatedAnchor &g) {
        return (g.gate == AnchorGate::OutlierSegment || g.gate == AnchorGate::RunsBackwards) && throughP7(g.phase);
    });
}

ConsistencyInput consistencyInput(const SessionSnapshot &s, const SessionAlignment &al, const ConsistencyEnv &env)
{
    ConsistencyInput in;
    in.shots.reserve(s.shots.size());
    for (const auto &sh : s.shots) {
        ConsistencyShot shot;
        shot.ordinal = sh->ordinal;
        shot.club = sh->club;
        shot.guarded = consistencyGuarded(*sh, al.warpFor(sh->ordinal));
        shot.carry = ballReading(*sh, QStringLiteral("lm.carryDistance"));
        shot.offline = ballReading(*sh, QStringLiteral("lm.offline"));
        in.shots.push_back(std::move(shot));
    }

    const auto keep = [&](ConsistencyReading &&r) {
        if (!anyFinite(r.values)) return;
        r.normSigma = normSigmaOf(r, in.shots, env);
        r.afterBall = r.column == kConsistencyAfterBallColumn && env.afterBall && env.afterBall(r.key);
        in.readings.push_back(std::move(r));
    };

    const size_t N = s.shots.size();
    const std::map<QString, KeyFacts> keys = metricKeys(s);
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        const QString &key = it->first;
        const KeyFacts &facts = it->second;
        const QString area = env.areaOf ? env.areaOf(key) : QString();

        if (consistencyReadsAtImpact(area)) {
            keep(positionReading(s, key, facts, area, kConsistencyImpactColumn, phase::Impact));
            continue;
        }
        if (isBackswingTiming(key)) {
            keep(positionReading(s, key, facts, area, kConsistencyBackswingTimingColumn, phase::Impact));
            continue;
        }
        if (!facts.hasCurve) {
            for (int c = 0; c < kConsistencyColumns; ++c)
                keep(positionReading(s, key, facts, area, c, kConsistencyPhases[c]));
            continue;
        }

        keep(positionReading(s, key, facts, area, 0, phase::Address));
        const double ownSigma = seriesSigma(s, key);
        const std::vector<TraceCurve> aligned = alignedCurves(s, al, key);
        for (int c = 1; c < kConsistencyAfterBallColumn; ++c) {
            ConsistencyReading r = readingOf(key, facts, area, c, ConsistencyReadingKind::Span, N);
            r.ownSigma = ownSigma;
            if (sampleSpan(aligned, al, r)) keep(std::move(r));
        }
        ConsistencyReading r = readingOf(key, facts, area, kConsistencyAfterBallColumn, ConsistencyReadingKind::Span, N);
        r.ownSigma = ownSigma;
        sampleAfterImpact(s, key, in.afterImpactMs, r);
        keep(std::move(r));
    }
    return in;
}

// ── the rules ───────────────────────────────────────────────────────────────────────────────────

std::vector<double> consistencyResiduals(const std::vector<double> &values, int samples, const QStringList &clubs)
{
    std::vector<double> out(values.size(), kNaN);
    if (samples <= 0) return out;
    std::map<QString, std::vector<int>> groups;     // a blank label is a group of its own
    for (int i = 0; i < clubs.size(); ++i) groups[clubs.at(i)].push_back(i);
    std::vector<double> column;
    for (const auto &[club, rows] : groups) {
        if (int(rows.size()) < kConsistencyMinClubShots) continue;
        for (int j = 0; j < samples; ++j) {
            column.clear();
            for (int i : rows) column.push_back(values[size_t(i) * size_t(samples) + size_t(j)]);
            const double median = medianOf(column);     // the shot itself included
            for (int i : rows) {
                const size_t at = size_t(i) * size_t(samples) + size_t(j);
                out[at] = values[at] - median;
            }
        }
    }
    return out;
}

double consistencySpread(const std::vector<double> &values)
{
    if (int(std::count_if(values.begin(), values.end(), isFinite)) < kConsistencyMinSpreadShots) return kNaN;
    return (quantile7(values, 0.75) - quantile7(values, 0.25)) / kConsistencySpreadDivisor;
}

ConsistencyPoint consistencyStandOutOf(const std::vector<double> &residuals, int samples, int shot, double ownSigma)
{
    ConsistencyPoint weakest;
    bool found = false;
    const int n = samples > 0 ? int(residuals.size()) / samples : 0;
    std::vector<double> others;
    others.reserve(size_t(n));
    for (int j = 0; j < samples; ++j) {
        const double x = residuals[size_t(shot) * size_t(samples) + size_t(j)];
        if (!isFinite(x)) continue;
        others.clear();
        for (int i = 0; i < n; ++i) {
            const double v = residuals[size_t(i) * size_t(samples) + size_t(j)];
            if (i != shot && isFinite(v)) others.push_back(v);
        }
        // gate rule: PP_GATE("rule.charts.consistencyStandOut")
        if (int(others.size()) < kConsistencyMinOthers) continue;
        const double s = (quantile7(others, 0.75) - quantile7(others, 0.25)) / kConsistencySpreadDivisor;
        if (!(s > 0.0)) continue;
        const double median = medianOf(others);
        const double z = (x - median) / s;
        if (found && !(std::abs(z) < std::abs(weakest.z))) continue;
        found = true;
        weakest.z = z; weakest.dv = x - median; weakest.sample = j;
        if (std::abs(z) < kConsistencyStandOutZ) return {};     // the weakest point can only get weaker
    }
    if (!found) return {};
    weakest.standsOut = !(isFinite(ownSigma) && std::abs(weakest.dv) < kConsistencyStandOutSigma * ownSigma);
    return weakest;
}

std::vector<int> consistencyPermutation(int n, ConsistencySplitMix &rng)
{
    std::vector<int> perm(size_t(std::max(0, n)));
    for (int i = 0; i < n; ++i) perm[size_t(i)] = i;
    for (int i = n - 1; i >= 1; --i) {
        const int j = int(rng.next() % quint64(i + 1));
        std::swap(perm[size_t(i)], perm[size_t(j)]);
    }
    return perm;
}

namespace {

std::vector<double> sampleColumn(const std::vector<double> &rows, int samples, int j, int n)
{
    std::vector<double> c(size_t(n), kNaN);
    for (int i = 0; i < n; ++i) c[size_t(i)] = rows[size_t(i) * size_t(samples) + size_t(j)];
    return c;
}

// A reading's spread. False when it has too little to reach the table.
// Whether a reading reached the table: a position with 5 finite values, a span with a spread at 3 of its 5 points.
bool hasSpread(const ConsistencyReading &r, const std::vector<double> &residuals, int n)
{
    if (r.kind == ConsistencyReadingKind::Position)
        return int(std::count_if(r.values.begin(), r.values.end(), isFinite)) >= kConsistencyMinSpreadShots;
    int spreads = 0;
    for (int j = 0; j < r.samples; ++j) spreads += isFinite(consistencySpread(sampleColumn(residuals, r.samples, j, n))) ? 1 : 0;
    return spreads >= kConsistencyMinSpanSamples;
}

// A club's Tukey fences on one launch-monitor reading. Too few values and nothing is past them.
struct ConsistencyFence { double lo = -HUGE_VAL, hi = HUGE_VAL; };

ConsistencyFence fenceOf(const std::vector<double> &v)
{
    ConsistencyFence f;
    // gate rule: PP_GATE("rule.charts.consistencyBallFlag")
    if (int(std::count_if(v.begin(), v.end(), isFinite)) < kConsistencyMinBallShots) return f;
    const double q1 = quantile7(v, 0.25), q3 = quantile7(v, 0.75), iqr = q3 - q1;
    f.lo = q1 - kConsistencyFenceIqr * iqr;
    f.hi = q3 + kConsistencyFenceIqr * iqr;
    return f;
}

// Short and offline, club by club, on the Tukey fences of the club's own shots.
std::vector<ConsistencyBallFlag> ballFlags(const std::vector<ConsistencyShot> &shots)
{
    std::map<QString, std::vector<int>> groups;
    for (size_t i = 0; i < shots.size(); ++i) groups[shots[i].club].push_back(int(i));
    std::vector<QStringList> flags(shots.size());
    for (const auto &[club, rows] : groups) {
        std::vector<double> carry, offline;
        for (int i : rows) { carry.push_back(shots[size_t(i)].carry); offline.push_back(shots[size_t(i)].offline); }
        const ConsistencyFence c = fenceOf(carry), o = fenceOf(offline);
        for (int i : rows) {
            const ConsistencyShot &s = shots[size_t(i)];
            if (s.carry < c.lo) flags[size_t(i)] << QStringLiteral("short");
            if (s.offline < o.lo || s.offline > o.hi) flags[size_t(i)] << QStringLiteral("offline");
        }
    }
    std::vector<ConsistencyBallFlag> out;
    for (size_t i = 0; i < shots.size(); ++i)
        if (!flags[i].isEmpty()) out.push_back(ConsistencyBallFlag{ shots[i].ordinal, flags[i] });
    return out;
}

} // namespace

bool consistencyGuardCovers(const ConsistencyReading &reading)
{
    return reading.kind == ConsistencyReadingKind::Span || isBackswingTiming(reading.key);
}

ConsistencyResult consistencyResult(const ConsistencyInput &in)
{
    ConsistencyResult out;
    const int n = int(in.shots.size());
    QStringList clubs;
    for (const ConsistencyShot &s : in.shots) clubs << s.club;

    // The readings, residuals first: everything after them reads those.
    const int R = int(in.readings.size());
    std::vector<std::vector<double>> residuals(size_t(R), std::vector<double>{});
    for (int r = 0; r < R; ++r) {
        const ConsistencyReading &reading = in.readings[size_t(r)];
        residuals[size_t(r)] = consistencyResiduals(reading.values, reading.samples, clubs);
        if (!hasSpread(reading, residuals[size_t(r)], n)) continue;
        out.readings.push_back(ConsistencyJudged{ reading.key, reading.column, r });
    }

    // Whether each row of each reading stands out, in the shots it was recorded on. Only readings that
    // reached the table and whose area applies at the column: one with too little for a spread has too
    // little to stand out. The real stand-outs take these with the guard applied, the shuffles without
    // it (header: COST).
    std::vector<int> applicable;
    for (const ConsistencyJudged &j : out.readings)
        if (consistencyApplies(in.readings[size_t(j.reading)].area, j.column)) applicable.push_back(j.reading);
    std::vector<std::vector<ConsistencyPoint>> points(applicable.size());
    for (size_t a = 0; a < applicable.size(); ++a) {
        const ConsistencyReading &reading = in.readings[size_t(applicable[a])];
        points[a].resize(size_t(n));
        for (int i = 0; i < n; ++i)
            points[a][size_t(i)] = consistencyStandOutOf(residuals[size_t(applicable[a])], reading.samples, i,
                                                         reading.ownSigma);
    }

    for (int i = 0; i < n; ++i) {
        std::vector<ConsistencyStandOut> shot;      // one per key, the largest |z|
        for (size_t a = 0; a < applicable.size(); ++a) {
            const ConsistencyPoint &p = points[a][size_t(i)];
            if (!p.standsOut) continue;
            const ConsistencyReading &reading = in.readings[size_t(applicable[a])];
            if (in.shots[size_t(i)].guarded && consistencyGuardCovers(reading))
                continue;
            ConsistencyStandOut s{ in.shots[size_t(i)].ordinal, reading.key, reading.column, applicable[a],
                                   p.z, p.dv, p.sample };
            const auto same = std::find_if(shot.begin(), shot.end(),
                                           [&](const ConsistencyStandOut &o) { return o.key == reading.key; });
            if (same == shot.end()) shot.push_back(std::move(s));
            else if (std::abs(s.z) > std::abs(same->z)) *same = std::move(s);
        }
        std::sort(shot.begin(), shot.end(), [](const ConsistencyStandOut &a, const ConsistencyStandOut &b) {
            if (std::abs(a.z) != std::abs(b.z)) return std::abs(a.z) > std::abs(b.z);
            if (a.key != b.key) return a.key < b.key;
            return a.column < b.column;
        });
        out.standouts.insert(out.standouts.end(), shot.begin(), shot.end());
    }

    out.flags = ballFlags(in.shots);

    // The shuffle: every applicable reading draws, standing out or not, so the stream matches the tally's.
    if (n > 0) {
        ConsistencySplitMix rng(kConsistencyShuffleSeed);
        std::vector<int> stamped(size_t(n), -1), count(size_t(n), 0);
        std::vector<double> busiest;
        double total = 0.0;
        int stamp = 0;
        for (int s = 0; s < kConsistencyShuffles; ++s) {
            std::fill(count.begin(), count.end(), 0);
            for (size_t a = 0; a < applicable.size(); ++a) {
                const std::vector<int> perm = consistencyPermutation(n, rng);
                // Readings sorted by key, so a stamp per run of one key counts it once a shot.
                if (a == 0 || in.readings[size_t(applicable[a])].key != in.readings[size_t(applicable[a - 1])].key)
                    ++stamp;
                for (int i = 0; i < n; ++i) {
                    if (!points[a][size_t(perm[size_t(i)])].standsOut || stamped[size_t(i)] == stamp) continue;
                    stamped[size_t(i)] = stamp;
                    ++count[size_t(i)];
                }
            }
            int most = 0;
            for (int c : count) { total += c; most = std::max(most, c); }
            busiest.push_back(double(most));
        }
        out.shuffledMean = total / (double(kConsistencyShuffles) * double(n));
        out.p95 = quantile7(std::move(busiest), kConsistencyShuffleP95);
    }
    return out;
}

namespace {

// The launch-monitor rows' readings, in their order.
const struct { const char *key; bool ball; } kLmRowKeys[] = {
    { "lm.carryDistance", true },  { "lm.offline", true },     { "lm.launchAngle", true },
    { "lm.ballSpeed", true },      { "lm.spinRate", true },    { "lm.clubheadSpeed", false },
    { "lm.smashFactor", false },   { "lm.attackAngle", false }, { "lm.clubPath", false } };

} // namespace

std::vector<ConsistencyLmRow> consistencyLmRows(const SessionSnapshot &s, int ordinal)
{
    std::vector<ConsistencyLmRow> rows;
    const auto me = std::find_if(s.shots.begin(), s.shots.end(), [&](const auto &sh) { return sh->ordinal == ordinal; });
    if (me == s.shots.end()) return rows;
    for (const auto &k : kLmRowKeys) {
        const QString key = QLatin1String(k.key);
        const double value = ballReading(**me, key);
        if (!isFinite(value)) continue;
        std::vector<double> values(s.shots.size(), kNaN);
        for (size_t i = 0; i < s.shots.size(); ++i)
            if (s.shots[i]->club == (*me)->club) values[i] = ballReading(*s.shots[i], key);
        const ConsistencyFence f = fenceOf(values);     // the same rule as the flags, past either side of it
        if (value >= f.lo && value <= f.hi) continue;
        const MetricSeries *m = (*me)->metric(key);
        rows.push_back(ConsistencyLmRow{ key, m->label, m->unit, k.ball, value, value < f.lo ? -1 : 1, std::move(values) });
    }
    return rows;
}

ConsistencyComparable consistencyComparable(const SessionSnapshot &s, const ConsistencyInput &in,
                                            const ConsistencyResult &res, int shot)
{
    ConsistencyComparable out;
    const ConsistencyShot &me = in.shots[size_t(shot)];
    const int n = int(in.shots.size());
    QStringList clubs;
    for (const ConsistencyShot &sh : in.shots) clubs << sh.club;

    const auto onThisShot = [&](const ConsistencyReading &r) {
        for (int j = 0; j < r.samples; ++j) if (isFinite(r.at(shot, j))) return true;
        return false;
    };
    out.measured = std::any_of(in.readings.begin(), in.readings.end(), [&](const ConsistencyReading &r) {
        return !r.area.isEmpty() && onThisShot(r);
    });
    for (const ConsistencyJudged &judged : res.readings) {
        const ConsistencyReading &r = in.readings[size_t(judged.reading)];
        if (!consistencyApplies(r.area, judged.column) || !onThisShot(r)) continue;
        if (me.guarded && consistencyGuardCovers(r)) continue;
        const std::vector<double> residuals = consistencyResiduals(r.values, r.samples, clubs);
        for (int j = 0; j < r.samples && !out.swing; ++j) {
            if (!isFinite(residuals[size_t(shot) * size_t(r.samples) + size_t(j)])) continue;
            int others = 0;
            for (int i = 0; i < n; ++i)
                if (i != shot && isFinite(residuals[size_t(i) * size_t(r.samples) + size_t(j)])) ++others;
            out.swing = others >= kConsistencyMinOthers;
        }
        if (out.swing) break;
    }
    for (const auto &k : kLmRowKeys) {
        const QString key = QLatin1String(k.key);
        if (!isFinite(ballReading(*s.shots[size_t(shot)], key))) continue;
        out.launchMonitor = true;
        int values = 0;
        for (const auto &sh : s.shots)
            if (sh->club == me.club && isFinite(ballReading(*sh, key))) ++values;
        out.ball = out.ball || values >= kConsistencyMinBallShots;
    }
    return out;
}

} // namespace pinpoint::charts
