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

// swing_themes.h — the machinery one piece at a time against values numpy gives (the random
// numbers, the order statistics, the eigensolver, varimax, the assignment), then the reduction on
// planted data: two themes found with the right members, session drift not mistaken for one, pure
// noise and too little data said honestly, threads and cancellation, layer 1's rules, "what you do
// well" (rule v1: in band by value, the gates, the family exclusions, the ranking), the wording,
// the view the home screen draws, the focus (rule v1: the groups by drill, their order, the words
// and "next on your list"), and the JSON round-trip. The real-ledger comparison with the
// Python reference is swing_themes_golden_test.cpp.

#include "../swing_themes.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace pinpoint::analysis;
using namespace pinpoint::analysis::themes;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// ── Builders ─────────────────────────────────────────────────────────────────────────────────────

static QString mid(int j) { return QStringLiteral("m_%1").arg(j, 2, 10, QLatin1Char('0')); }

// One session from a swings × measures block: each measure is read by its own condition, NaN is a
// row the capture could not assess.
static ThemeSessionInput sessionFrom(const QString &name, const std::vector<std::vector<double>> &rows)
{
    ThemeSessionInput s;
    s.name = name;
    int shot = 1;
    for (const std::vector<double> &vals : rows) {
        ShotRecord rec;
        rec.shotId = shot++;
        for (int j = 0; j < int(vals.size()); ++j) {
            ConditionRow r;
            r.conditionId = QStringLiteral("c_%1").arg(j);
            if (std::isfinite(vals[size_t(j)])) {
                r.state = vals[size_t(j)] > 1.0 ? ShotState::Fired : ShotState::Clean;
                r.drivingMeasureId = mid(j);
                r.value = vals[size_t(j)];
            } else {
                r.state = ShotState::NotAssessable;
            }
            rec.rows.push_back(std::move(r));
        }
        s.shots.push_back(std::move(rec));
    }
    return s;
}

// Planted: measures 0–3 ride factor A, 4–7 factor B, the rest are noise. Every session adds its
// own offset to every measure — camera / club / day drift the centring must take out.
static std::vector<ThemeSessionInput> planted(int sessions, int perSession, int p, bool factors, uint64_t seed,
                                              double missing = 0.04, double drift = 3.0)
{
    DetRng rng(seed);
    std::vector<ThemeSessionInput> out;
    for (int s = 0; s < sessions; ++s) {
        std::vector<std::vector<double>> rows;
        for (int i = 0; i < perSession; ++i) {
            const double fa = rng.normal(), fb = rng.normal();
            std::vector<double> v(static_cast<size_t>(p));
            for (int j = 0; j < p; ++j) {
                const double e = rng.normal();
                double x = e;
                if (factors && j < 4)      x = 0.85 * fa + 0.53 * e;
                else if (factors && j < 8) x = 0.85 * fb + 0.53 * e;
                x += drift * double(s) * (1.0 + 0.1 * double(j));
                if (rng.uniform() < missing) x = std::numeric_limits<double>::quiet_NaN();
                v[size_t(j)] = x;
            }
            rows.push_back(std::move(v));
        }
        // Reverse name order on purpose: the reduction must sort sessions by name itself.
        out.insert(out.begin(), sessionFrom(QStringLiteral("2026-0%1-01_Golfer").arg(s + 1), rows));
    }
    return out;
}

static QHash<QString, ThemeMeasureInfo> infoFor(int p)
{
    QHash<QString, ThemeMeasureInfo> h;
    for (int j = 0; j < p; ++j) {
        ThemeMeasureInfo m;
        m.id = mid(j);
        m.sign = j == 1 ? -1.0 : 1.0;           // one measure oriented low = fault
        m.twoSided = false;
        m.when = j < 4 ? 4 + j % 2 : j < 8 ? 6 + j % 2 : 0;
        m.metricKey = QStringLiteral("key%1").arg(j);
        h.insert(m.id, m);
    }
    return h;
}

static ThemePhrases phrasesFor()
{
    ThemePhrases ph;
    ph.condition = [](const QString &id) { return QStringLiteral("you do %1").arg(id); };
    ph.measure = [](const QString &id, bool high) {
        return QStringLiteral("your %1 goes %2").arg(id, high ? QStringLiteral("up") : QStringLiteral("down"));
    };
    ph.family = [](const QString &) { return QString(); };
    return ph;
}

// std::thread parallel-for: an atomic work counter over a few workers.
static ThemeParallelFor threadedFor(int workers)
{
    return [workers](int n, const std::function<void(int)> &body) {
        std::atomic<int> next{ 0 };
        std::vector<std::thread> pool;
        for (int w = 0; w < workers; ++w)
            pool.emplace_back([&]() { for (int i = next++; i < n; i = next++) body(i); });
        for (std::thread &t : pool) t.join();
    };
}

static QSet<int> memberSet(const SwingTheme &t)
{
    QSet<int> s;
    for (const ThemeMember &m : t.members) s.insert(m.measureId.mid(2).toInt());
    return s;
}

static bool banned(const QString &line)
{
    const QString l = line.toLower();
    return l.contains(QStringLiteral("because")) || l.contains(QStringLiteral("cause"))
        || l.contains(QStringLiteral("due to")) || l.contains(QStringLiteral("leads to"));
}

// Layer-1 session: one row per condition per shot from a per-condition plan (1 fired, 0 clean,
// -1 not assessable).
static ThemeSessionInput ledgerOf(const QString &name, const std::vector<std::pair<QString, std::vector<int>>> &plan)
{
    ThemeSessionInput s;
    s.name = name;
    size_t n = 0;
    for (const auto &c : plan) n = std::max(n, c.second.size());
    for (size_t i = 0; i < n; ++i) {
        ShotRecord rec;
        rec.shotId = int(i) + 1;
        for (const auto &c : plan) {
            ConditionRow r;
            r.conditionId = c.first;
            const int v = i < c.second.size() ? c.second[i] : -1;
            r.state = v == 1 ? ShotState::Fired : v == 0 ? ShotState::Clean : ShotState::NotAssessable;
            rec.rows.push_back(std::move(r));
        }
        s.shots.push_back(std::move(rec));
    }
    return s;
}

static std::vector<int> rate(int fired, int total)
{
    std::vector<int> v;
    for (int i = 0; i < total; ++i) v.push_back(i < fired ? 1 : 0);
    return v;
}

// A fault condition per id: prominence Common, detection Any, its own id as its one family.
static ThemeConditionInfo cond(const QString &id, int prominence = 3, int detection = kThemeDetectionAny,
                               QStringList families = {}, bool fault = true)
{
    ThemeConditionInfo c;
    c.id = id;
    c.fault = fault;
    c.detection = detection;
    c.prominence = prominence;
    c.families = families.isEmpty() ? QStringList{ id } : families;
    return c;
}

static QHash<QString, ThemeConditionInfo> faultsOf(const QStringList &ids)
{
    QHash<QString, ThemeConditionInfo> h;
    for (const QString &id : ids) h.insert(id, cond(id));
    return h;
}

// "What you do well" session: per condition a list of values read against one corridor (lo..hi,
// one shape); NaN is a row the capture could not assess. The state is set OPPOSITE to the value
// (an in-band row is marked fired) so a test passes only if the fired flag is never read.
struct WellPlan {
    QString id;
    std::vector<double> values;
    CorridorShape shape = CorridorShape::TwoSided;
    double lo = 0.0, hi = 10.0;
};

static ThemeSessionInput wellLedger(const QString &name, const std::vector<WellPlan> &plan)
{
    ThemeSessionInput s;
    s.name = name;
    size_t n = 0;
    for (const WellPlan &c : plan) n = std::max(n, c.values.size());
    for (size_t i = 0; i < n; ++i) {
        ShotRecord rec;
        rec.shotId = int(i) + 1;
        for (const WellPlan &c : plan) {
            ConditionRow r;
            r.conditionId = c.id;
            const double v = i < c.values.size() ? c.values[i] : std::numeric_limits<double>::quiet_NaN();
            if (std::isfinite(v)) {
                r.drivingMeasureId = QStringLiteral("m_") + c.id;
                r.value = v;
                r.corridorLo = c.lo;
                r.corridorHi = c.hi;
                r.corridorShape = c.shape;
                r.state = (v >= c.lo && v <= c.hi) ? ShotState::Fired : ShotState::Clean;
            } else {
                r.state = ShotState::NotAssessable;
            }
            rec.rows.push_back(std::move(r));
        }
        s.shots.push_back(std::move(rec));
    }
    return s;
}

// `good` values inside 0..10, then `total − good` outside it.
static std::vector<double> inBand(int good, int total)
{
    std::vector<double> v;
    for (int i = 0; i < total; ++i) v.push_back(i < good ? 5.0 : 20.0);
    return v;
}

// The do-well card capped at five, so the tests written against that cap still see every row.
static ThemeOptions fiveWell()
{
    ThemeOptions o;
    o.maxDoWell = 5;
    return o;
}

static const DoWellRow *findWell(const std::vector<DoWellRow> &rows, const char *id)
{
    for (const DoWellRow &r : rows) if (r.conditionId == QLatin1String(id)) return &r;
    return nullptr;
}

int main()
{
    std::printf("swing_themes_test\n");
    QStringList allLines;

    // ── Random numbers: mt19937_64 exactly, one seed per replicate ───────────────────────────────
    {
        DetRng g(5489);
        const uint64_t first = g.below(~0ULL);
        for (int i = 2; i < 10000; ++i) g.below(~0ULL);
        const uint64_t tenThousandth = g.below(~0ULL);
        check(first == 14514284786278117030ULL, "mt19937_64 default seed: the reference first output");
        check(tenThousandth == 9981545732273789042ULL, "mt19937_64 default seed: the reference 10000th output");
        check(themeSeedFor(kThemeSeedBase, kThemeStreamPA, 0) == 1814386492527797468ULL
              && themeSeedFor(kThemeSeedBase, kThemeStreamBoot, 499) == 1814386492528797970ULL,
              "seedFor(1, 0) and seedFor(2, 499) are the Python values");

        auto perm = [](uint64_t seed, int n) {
            std::vector<double> a;
            for (int i = 0; i < n; ++i) a.push_back(i);
            DetRng r(seed);
            fisherYates(a, r);
            return a;
        };
        const std::vector<double> p1 = perm(themeSeedFor(kThemeSeedBase, 1, 0), 10);
        check(p1 == std::vector<double>{ 0, 3, 7, 6, 4, 2, 5, 8, 1, 9 }, "Fisher–Yates: the permutation theme_rng gives for seedFor(1, 0)");
        check(perm(themeSeedFor(kThemeSeedBase, 2, 3), 7) == std::vector<double>{ 1, 3, 5, 2, 4, 0, 6 },
              "Fisher–Yates: and for seedFor(2, 3)");
        check(perm(themeSeedFor(kThemeSeedBase, 1, 0), 10) == p1, "Fisher–Yates: the same seed, the same shuffle");
    }

    // ── Order statistics, numpy's conventions ────────────────────────────────────────────────────
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        check(nanMedian({ 3.0, nan, 1.5, 7.25, -2.0, nan, 4.0 }) == 3.0, "nanmedian ignores NaN");
        check(std::isnan(nanMedian({ nan, nan })), "nanmedian of nothing is NaN");
        check(medianPropagating({ 4.0, 1.0, 3.0, 2.0 }) == 2.5, "median of an even count averages the middle two");
        check(std::isnan(medianPropagating({ 1.0, nan })), "median propagates NaN, as numpy does");
        const std::vector<double> y{ 0.91, 0.62, 0.88, 0.97, 0.73, 0.85, 0.99, 0.80, 0.66, 0.93, 0.71 };
        check(near(percentileLinear(y, 5), 0.64, 1e-15) && near(percentileLinear(y, 50), 0.85, 1e-15)
              && near(percentileLinear(y, 95), 0.98, 1e-15) && near(percentileLinear(y, 25.5), 0.721, 1e-15),
              "percentile 'linear' at 5 / 50 / 95 / 25.5 matches numpy");
        std::vector<double> z;
        for (int i = 0; i < 500; ++i) { const double t = double(i) / 499.0; z.push_back(t * t); }
        check(near(percentileLinear(z, 95), 0.9025001907622858, 1e-15) && near(percentileLinear(z, 5), 0.002500190762286095, 1e-15),
              "percentile over 500 values (the bootstrap's size) matches numpy");
        check(averageRanks({ 3, 1, 4, 1, 5, 9, 2, 6, 5, 3 }) == std::vector<double>{ 4.5, 1.5, 6, 1.5, 7.5, 10, 3, 9, 7.5, 4.5 },
              "average ranks: ties share the mean rank");
    }

    // ── Linear algebra ───────────────────────────────────────────────────────────────────────────
    {
        Matrix A(3, 3);
        A.v = { 2, -1, 0, -1, 2, -1, 0, -1, 2 };
        std::vector<double> w;
        Matrix V;
        symmetricEigen(A, w, V);
        const double r2 = std::sqrt(2.0);
        check(near(w[0], 2 + r2, 1e-13) && near(w[1], 2, 1e-13) && near(w[2], 2 - r2, 1e-13),
              "eigensolver: the 3×3 second-difference matrix, descending");
        double res = 0.0, orth = 0.0;
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                double s = 0.0, d = 0.0;
                for (int k = 0; k < 3; ++k) { s += A.at(i, k) * V.at(k, j); d += V.at(k, i) * V.at(k, j); }
                res = std::max(res, std::fabs(s - w[size_t(j)] * V.at(i, j)));
                orth = std::max(orth, std::fabs(d - (i == j ? 1.0 : 0.0)));
            }
        check(res < 1e-13 && orth < 1e-13, "eigensolver: A·v = λ·v and the vectors are orthonormal");

        Matrix B(3, 3);
        B.v = { 4, 1, 2, 1, 3, 0, 2, 0, 5 };
        std::vector<double> wb;
        Matrix VB;
        symmetricEigen(B, wb, VB);
        check(near(wb[0] + wb[1] + wb[2], 12.0, 1e-12) && wb[0] >= wb[1] && wb[1] >= wb[2],
              "eigensolver: the trace is kept and the order is descending");

        Matrix L(6, 2);
        L.v = { 0.8, 0.3, 0.7, 0.4, 0.75, 0.2, 0.2, 0.8, 0.3, 0.7, 0.25, 0.85 };
        const Matrix R = varimax(L, ThemeOptions{});
        const std::vector<double> want{ 0.8073623534547492, 0.2795818846492182, 0.7099347849833086, 0.3820897814266997,
                                        0.754838322989498, 0.18088423410126755, 0.22025677847308642, 0.7946615326896462,
                                        0.31768434694452674, 0.6921536359121647, 0.27151072686182715, 0.8433753169253664 };
        double dv = 0.0;
        for (size_t i = 0; i < want.size(); ++i) dv = std::max(dv, std::fabs(R.v[i] - want[i]));
        check(dv < 1e-12, "varimax: numpy's rotation of a two-factor loading matrix");

        std::vector<double> inv;
        check(invertSmall(2, { 4, 7, 2, 6 }, inv) && near(inv[0], 0.6, 1e-15) && near(inv[1], -0.7, 1e-15)
              && near(inv[2], -0.2, 1e-15) && near(inv[3], 0.4, 1e-15), "k×k inverse");
        check(!invertSmall(2, { 1, 2, 2, 4 }, inv), "a singular k×k matrix is refused");

        using P = std::vector<std::pair<int, int>>;
        check(hungarianMax({ 1, 2, 3, 2, 4, 6, 3, 6, 9.5 }, 3, 3) == P{ { 0, 0 }, { 1, 1 }, { 2, 2 } }, "Hungarian: square");
        check(hungarianMax({ 1, 0, 0, 0, 0, 1, 0, 1, 0 }, 3, 3) == P{ { 0, 0 }, { 2, 1 }, { 1, 2 } }, "Hungarian: a permutation, listed by column");
        check(hungarianMax({ 0.9, 0.1, 0.3, 0.2, 0.8, 0.7 }, 2, 3) == P{ { 0, 0 }, { 1, 1 } }, "Hungarian: more columns than rows");
        check(hungarianMax({ 0.9, 0.1, 0.95, 0.2, 0.3, 0.7 }, 3, 2) == P{ { 1, 0 }, { 2, 1 } }, "Hungarian: more rows than columns");
        check(hungarianMax({ 0.5, 0.5, 0.5, 0.5 }, 2, 2) == P{ { 0, 0 }, { 1, 1 } }, "Hungarian: an all-tie matrix resolves as the reference does");
    }

    // ── Planted: two themes over four sessions of thirty swings ──────────────────────────────────
    const std::vector<ThemeSessionInput> sessions = planted(4, 30, 10, true, 77);
    const QHash<QString, ThemeMeasureInfo> info = infoFor(10);
    const SwingThemes st = reduceSwingThemes(sessions, info, {});
    {
        check(st.enough && st.swings == 120 && st.sessions == 4, "enough data: 120 swings over 4 sessions");
        check(st.sessionNames.size() == 4 && st.sessionNames.front() == QStringLiteral("2026-01-01_Golfer"),
              "sessions are taken in name order, whatever order they arrive in");
        check(st.measures.size() == 10 && st.k == 2, "parallel analysis keeps exactly the two planted factors");
        check(st.eig.size() == 10 && st.paThreshold.size() == 10 && st.eig[1] > st.paThreshold[1] && st.eig[2] <= st.paThreshold[2],
              "the observed eigenvalues cross the shuffled threshold after the second");
        const QSet<int> a{ 0, 1, 2, 3 }, b{ 4, 5, 6, 7 };
        bool found = st.themes.size() == 2;
        for (const SwingTheme &t : st.themes) found = found && (memberSet(t) == a || memberSet(t) == b);
        check(found && memberSet(st.themes[0]) != memberSet(st.themes[1]), "each theme holds one planted group, and nothing else");
        bool tiers = true;
        for (const SwingTheme &t : st.themes) tiers = tiers && (t.tier == ThemeTier::Firm || t.tier == ThemeTier::Probably);
        check(tiers, "both planted themes are told firmly (Firm or Probably)");
        check(st.themes[0].tier == ThemeTier::Firm && st.themes[1].tier == ThemeTier::Firm, "with these loadings, both are Firm");
        bool noMix = true;
        for (const SwingTheme &t : st.themes) noMix = noMix && !(memberSet(t).contains(8) || memberSet(t).contains(9));
        check(noMix, "the session offsets — added to every measure — do not make a theme");

        // Orientation travels into the direction: m_01 is oriented low = fault.
        bool rawOk = true;
        for (const SwingTheme &t : st.themes)
            for (const ThemeMember &m : t.members)
                rawOk = rawOk && m.rawHigh == (m.loading * info.value(m.measureId).sign > 0);
        check(rawOk, "rawHigh is the loading times the orientation sign");
        const SwingTheme &ta = memberSet(st.themes[0]) == a ? st.themes[0] : st.themes[1];
        const SwingTheme &tb = memberSet(st.themes[0]) == a ? st.themes[1] : st.themes[0];
        check(ta.startsAt == 4 && tb.startsAt == 6, "startsAt is the earliest member's swing position");
        bool sorted = true;
        for (const SwingTheme &t : st.themes)
            for (size_t i = 1; i < t.members.size(); ++i)
                sorted = sorted && std::fabs(t.members[i - 1].loading) >= std::fabs(t.members[i].loading);
        check(sorted, "members are ordered by |loading|");
        check(ta.losoPerSession.size() == 4 && ta.sessionScores.size() == 4, "per-session vectors cover the kept sessions");
        // The trend is read UNCENTRED, so a drift that raises every measure session on session
        // reads as more of theme B (all four oriented +1) — which is why it is only ever "may be".
        check(tb.trend == 1 && tb.slope > 0.1 && tb.sessionScores[0] < tb.sessionScores[3],
              "session drift up the oriented scale reads as \"may be growing\"");
        // No drift and no change. Enough swings per session that the session means are steady:
        // at thirty, two noise-only themes here read slopes of 0.17 and 0.11 against the 0.1 gate.
        ThemeOptions quick;
        quick.boot = 20;
        quick.paShuffles = 50;
        const SwingThemes still = reduceSwingThemes(planted(4, 400, 10, true, 77, 0.04, 0.0), info, {}, quick);
        bool flat = still.themes.size() == 2;
        for (const SwingTheme &t : still.themes) flat = flat && t.trend == 0 && std::fabs(t.slope) < 0.1;
        check(flat, "a golfer with no drift and no change: \"no clear change yet\"");
        check(ta.emVsZeroFill > 0.95 && ta.clusterJaccard == 1.0, "zero-fill and the clustering both find the same groups");
        check(st.clusterLabels.size() == 10 && st.clusterLabels[0] == 1, "cluster labels are numbered by first appearance");
        check(near(st.varianceShare[0] + st.varianceShare[1], st.themes[0].varianceShare + st.themes[1].varianceShare, 1e-15)
              && st.varianceShare[0] >= st.varianceShare[1], "themes are ordered by variance share");

        const SwingSummaryLines lines = swingSummaryLines(st, phrasesFor());
        allLines << lines.seenMost << lines.together;
        check(lines.together.size() == 2 && lines.together[0].startsWith(QStringLiteral("On swings where your m_0")),
              "both themes are said, as co-movement");
    }

    // ── Threads and cancellation ─────────────────────────────────────────────────────────────────
    {
        const SwingThemes threaded = reduceSwingThemes(sessions, info, {}, ThemeOptions{}, threadedFor(4));
        check(toJson(threaded) == toJson(st), "a threaded run is identical to the serial one, to the bit");

        std::atomic<bool> stopNow{ true };
        const SwingThemes c0 = reduceSwingThemes(sessions, info, faultsOf({ QStringLiteral("c_0") }), ThemeOptions{}, {}, &stopNow);
        check(c0.cancelled && !c0.enough && c0.themes.empty() && c0.k == 0, "cancelled before it starts: nothing in layer 2");

        std::atomic<bool> stopLater{ false };
        std::atomic<int> ran{ 0 };
        const ThemeParallelFor counting = [&](int n, const std::function<void(int)> &body) {
            for (int i = 0; i < n; ++i) {
                body(i);
                if (++ran == 520) stopLater = true;     // inside the bootstrap
            }
        };
        const SwingThemes c1 = reduceSwingThemes(sessions, info, {}, ThemeOptions{}, counting, &stopLater);
        check(c1.cancelled && !c1.enough && c1.themes.empty(), "cancelled mid-bootstrap: the result says so and holds no themes");
        check(swingSummaryLines(c1, phrasesFor()).together.front().startsWith(QStringLiteral("Not yet")),
              "a cancelled result is never told as themes");
    }

    // ── Pure noise and too little data ───────────────────────────────────────────────────────────
    {
        const SwingThemes noise = reduceSwingThemes(planted(4, 30, 12, false, 99), infoFor(12), {});
        bool none = noise.enough;
        for (const SwingTheme &t : noise.themes) none = none && t.tier == ThemeTier::None;
        check(none && noise.k >= 1, "pure noise: a component is fitted, and no theme passes a tier");
        const SwingSummaryLines nl = swingSummaryLines(noise, phrasesFor());
        allLines << nl.together;
        check(nl.together == QStringList{ QStringLiteral("Nothing yet — your faults don't rise and fall together clearly enough to say.") },
              "pure noise: \"Nothing yet\"");

        const SwingThemes few = reduceSwingThemes(planted(2, 40, 10, true, 5), infoFor(10), {});
        check(!few.enough && few.themes.empty() && few.k == 0 && few.swings == 80 && few.sessions == 2,
              "two sessions: not enough, whatever the swing count");
        const SwingSummaryLines fl = swingSummaryLines(few, phrasesFor());
        allLines << fl.together;
        check(fl.together == QStringList{ QStringLiteral("Not yet — it takes about 60 swings over 3 sessions to see what goes "
                                                         "together (so far: 80 swings over 2 sessions).") },
              "too little data: \"Not yet\" with the counts so far");
        const SwingThemes fewSwings = reduceSwingThemes(planted(3, 19, 10, true, 6), infoFor(10), {});
        check(!fewSwings.enough && fewSwings.swings == 57 && fewSwings.sessions == 3, "three sessions but 57 swings: not enough");

        // A swing that read under half the well-covered measures is dropped and named.
        std::vector<ThemeSessionInput> holed = planted(3, 25, 10, true, 8, 0.0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        holed[0] = sessionFrom(holed[0].name, { { 1, nan, nan, nan, nan, nan, nan, 3, 4, 5 } });
        holed[0].shots.front().shotId = 42;
        const SwingThemes h = reduceSwingThemes(holed, infoFor(10), {});
        check(h.dropped.size() == 1 && h.dropped.front().second == 42 && h.swings == 50 && h.sessions == 2,
              "a capture hole is dropped, and its session no longer counts");
    }

    // ── Layer 1: what we see most ────────────────────────────────────────────────────────────────
    {
        const QHash<QString, ThemeConditionInfo> faults =
            faultsOf({ "a", "b", "c", "d", "e", "f", "g", "h", "seven", "late", "steady", "growing", "twoOnly", "same1", "same2" });
        const std::vector<ThemeSessionInput> ls = {
            ledgerOf("s1", { { "a", rate(10, 10) }, { "seven", rate(7, 7) }, { "late", rate(9, 10) }, { "steady", rate(8, 10) },
                             { "growing", rate(5, 10) }, { "setupOnly", rate(10, 10) } }),
            ledgerOf("s2", { { "a", rate(10, 10) }, { "seven", rate(7, 7) }, { "late", rate(9, 10) }, { "steady", rate(8, 10) },
                             { "growing", rate(5, 10) }, { "twoOnly", rate(6, 10) } }),
            ledgerOf("s3", { { "a", rate(5, 10) }, { "late", rate(6, 10) }, { "steady", rate(7, 10) }, { "growing", rate(9, 10) },
                             { "twoOnly", rate(4, 10) }, { "b", rate(3, 10) } }),
            ledgerOf("s4", { { "a", rate(7, 10) }, { "late", rate(5, 10) }, { "steady", rate(8, 10) }, { "growing", rate(9, 10) },
                             { "twoOnly", rate(8, 10) }, { "b", { 1, 1, 1, 1, -1, -1, -1, -1, -1, -1 } } }),
        };
        const std::vector<const ThemeSessionInput *> sp = sortedSessions(ls);
        const std::vector<SeenMostRow> rows = seenMost(sp, faults, ThemeOptions{});
        auto find = [&](const char *id) -> const SeenMostRow * {
            for (const SeenMostRow &r : rows) if (r.conditionId == QLatin1String(id)) return &r;
            return nullptr;
        };
        check(!find("seven"), "a session must judge a fault on 8 swings to count");
        check(!find("setupOnly"), "only Fault conditions are listed");
        check(!find("b"), "present only when the latest judged session saw it on half (four judged swings are not a session)");
        const SeenMostRow *a = find("a");
        const double wantA = (0.421875 * 1.0 + 0.5625 * 1.0 + 0.75 * 0.5 + 1.0 * 0.7) / (0.421875 + 0.5625 + 0.75 + 1.0);
        check(a && near(a->share, wantA, 1e-15) && a->sessionsJudged == 4 && a->sessionsSeen == 4,
              "share is recency-weighted, 0.75 per older session");
        check(a && a->trend == QStringLiteral("easing"), "late two under the earlier mean by more than 0.15: easing");
        check(a && a->pips == std::vector<bool>{ true, true, true, true }, "a needs-work pip per judged session, at half or more");
        check(find("twoOnly") && find("twoOnly")->pips == std::vector<bool>{ true, false, true },
              "needs-work pips follow the judged sessions in order (an unjudged one has none)");
        check(find("growing") && find("growing")->trend == QStringLiteral("growing"), "over by more than 0.15: growing");
        check(find("steady") && find("steady")->trend == QStringLiteral("steady"), "within 0.15: steady");
        check(find("late") && find("late")->sessionsSeen == 4 && find("late")->sessionsJudged == 4,
              "sessions seen counts the judged sessions at half or more");
        check(find("twoOnly") && find("twoOnly")->sessionsJudged == 3 && find("twoOnly")->sessionsSeen == 2,
              "a fault judged in three sessions counts three");
        bool ordered = true;
        for (size_t i = 1; i < rows.size(); ++i) ordered = ordered && rows[i - 1].share >= rows[i].share;
        check(ordered, "ranked by share");

        const std::vector<ThemeSessionInput> twoIn{ ledgerOf("s1", { { "a", rate(9, 10) } }), ledgerOf("s2", { { "a", rate(9, 10) } }) };
        check(seenMost(sortedSessions(twoIn), faults, ThemeOptions{}).front().trend.isEmpty(), "no trend under three judged sessions");

        // The lines: phrase dedupe, the cap of five, the template.
        std::vector<std::pair<QString, std::vector<int>>> many;
        for (const char *id : { "a", "b", "c", "d", "e", "f", "g", "same1", "same2" }) many.push_back({ QString::fromLatin1(id), rate(9, 10) });
        SwingThemes lt;
        lt.seenMost = seenMost(sortedSessions({ ledgerOf("s1", many) }), faults, ThemeOptions{});
        ThemePhrases ph;
        ph.condition = [](const QString &id) {
            if (id.startsWith(QStringLiteral("same"))) return QStringLiteral("you stand up through the ball");
            if (id == QStringLiteral("g")) return QString();
            return QStringLiteral("your %1 moves").arg(id);
        };
        const SwingSummaryLines L1 = swingSummaryLines(lt, ph);
        check(L1.seenMost.size() == 5, "at most five lines");
        check(L1.seenMost.front() == QStringLiteral("Your a moves — on almost every swing (1 of 1 sessions)"),
              "the template: capitalised phrase, how often, sessions");
        SwingThemes dd;
        dd.seenMost = seenMost(sortedSessions({ ledgerOf("s1", { { "same1", rate(9, 10) }, { "same2", rate(8, 10) }, { "g", rate(10, 10) } }) }),
                               faults, ThemeOptions{});
        const SwingSummaryLines L2 = swingSummaryLines(dd, ph);
        check(L2.seenMost == QStringList{ QStringLiteral("You stand up through the ball — on almost every swing (1 of 1 sessions)") },
              "a repeated phrase is told once, and a condition with no phrase is not told");
        check(themeHowOften(0.9) == QStringLiteral("on almost every swing") && themeHowOften(0.89) == QStringLiteral("on most swings")
              && themeHowOften(0.7) == QStringLiteral("on most swings") && themeHowOften(0.69) == QStringLiteral("on more than half your swings"),
              "how often: 0.9 and 0.7 are the boundaries");
        SwingThemes tr;
        tr.seenMost = { SeenMostRow{ "a", 0.8, 3, 4, "easing", {} }, SeenMostRow{ "b", 0.6, 2, 3, "steady", {} } };
        const SwingSummaryLines L3 = swingSummaryLines(tr, ph);
        check(L3.seenMost == QStringList{ QStringLiteral("Your a moves — on most swings (3 of 4 sessions; easing)"),
                                          QStringLiteral("Your b moves — on more than half your swings (2 of 3 sessions)") },
              "easing / growing are said; steady is not");
        check(swingSummaryLines(SwingThemes{}, ph).seenMost == QStringList{ QStringLiteral("Nothing shows up on most of your swings.") },
              "nothing present: says so");
        allLines << L1.seenMost << L2.seenMost << L3.seenMost;
    }

    // ── What you do well: in band BY VALUE ───────────────────────────────────────────────────────
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        auto row = [](double v, CorridorShape shape, double lo, double hi, ShotState state = ShotState::Clean) {
            ConditionRow r;
            r.conditionId = QStringLiteral("x");
            r.state = state;
            r.drivingMeasureId = QStringLiteral("m_x");
            r.value = v;
            r.corridorLo = lo;
            r.corridorHi = hi;
            r.corridorShape = shape;
            return r;
        };
        check(rowInBand(row(2.0, CorridorShape::Floor, 2.0, 5.0)) == 1 && rowInBand(row(1.99, CorridorShape::Floor, 2.0, 5.0)) == 0
              && rowInBand(row(99.0, CorridorShape::Floor, 2.0, 5.0)) == 1,
              "floor: in band at and above lo, however far above (the open side)");
        check(rowInBand(row(5.0, CorridorShape::Ceiling, 2.0, 5.0)) == 1 && rowInBand(row(5.01, CorridorShape::Ceiling, 2.0, 5.0)) == 0
              && rowInBand(row(-99.0, CorridorShape::Ceiling, 2.0, 5.0)) == 1,
              "ceiling: in band at and below hi, however far below");
        check(rowInBand(row(2.0, CorridorShape::TwoSided, 2.0, 5.0)) == 1 && rowInBand(row(5.0, CorridorShape::TwoSided, 2.0, 5.0)) == 1
              && rowInBand(row(1.99, CorridorShape::TwoSided, 2.0, 5.0)) == 0 && rowInBand(row(5.01, CorridorShape::TwoSided, 2.0, 5.0)) == 0,
              "two-sided: lo ≤ v ≤ hi, both edges inside");
        check(rowInBand(row(3.0, CorridorShape::Unknown, 2.0, 5.0)) == -1 && rowInBand(row(3.0, CorridorShape::None, 2.0, 5.0)) == -1,
              "an unknown or absent corridor shape: the row is not counted at all");
        check(rowInBand(row(3.0, CorridorShape::TwoSided, 2.0, 5.0, ShotState::NotAssessable)) == -1
              && rowInBand(row(nan, CorridorShape::TwoSided, 2.0, 5.0)) == -1,
              "not assessable, or a non-finite value: not counted");
        ConditionRow bare = row(3.0, CorridorShape::TwoSided, 2.0, 5.0);
        bare.drivingMeasureId.clear();
        check(rowInBand(bare) == -1, "a row with no readings is not counted");
        check(rowInBand(row(3.0, CorridorShape::TwoSided, 2.0, 5.0, ShotState::Fired)) == 1
              && rowInBand(row(9.0, CorridorShape::TwoSided, 2.0, 5.0, ShotState::Clean)) == 0,
              "the fired flag is never read: by value only");

        ConditionRow multi = row(100.0, CorridorShape::Unknown, 0.0, 0.0);   // driving fields ignored
        auto reading = [](double v, CorridorShape shape, double lo, double hi) {
            MeasureRow m;
            m.measureId = QStringLiteral("m_r");
            m.value = v;
            m.corridorLo = lo;
            m.corridorHi = hi;
            m.corridorShape = shape;
            return m;
        };
        multi.readings = { reading(3.0, CorridorShape::TwoSided, 2.0, 5.0), reading(7.0, CorridorShape::Floor, 6.0, 9.0) };
        check(rowInBand(multi) == 1, "several readings: in band when every one is, the driving fields not read");
        multi.readings.push_back(reading(10.0, CorridorShape::Ceiling, 0.0, 9.0));
        check(rowInBand(multi) == 0, "several readings: one outside its own corridor puts the row out");
        multi.readings.push_back(reading(1.0, CorridorShape::Unknown, 0.0, 9.0));
        check(rowInBand(multi) == -1, "several readings: one with an unknown shape uncounts the row, even after one outside");
    }

    // ── What you do well: the gates, the exclusions, the ranking ─────────────────────────────────
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const std::vector<double> unread(10, nan);
        QHash<QString, ThemeConditionInfo> ci = faultsOf({ "clean3", "few", "seven", "twenty", "thirty", "bigger", "latest", "lowShare",
                                                           "unknownMixed", "allUnknown" });
        ci.insert(QStringLiteral("conj"), cond(QStringLiteral("conj"), 3, kThemeDetectionAll));
        ci.insert(QStringLiteral("first"), cond(QStringLiteral("first"), 3, kThemeDetectionFirst));
        ci.insert(QStringLiteral("rare"), cond(QStringLiteral("rare"), 1));
        ci.insert(QStringLiteral("occasional"), cond(QStringLiteral("occasional"), 2));
        ci.insert(QStringLiteral("setup"), cond(QStringLiteral("setup"), 3, kThemeDetectionAny, {}, false));

        std::vector<ThemeSessionInput> ls = {
            wellLedger("s1", { { "clean3", inBand(10, 10) }, { "few", inBand(40, 40) }, { "seven", inBand(7, 7) }, { "twenty", inBand(10, 10) },
                               { "thirty", inBand(15, 15) }, { "bigger", inBand(16, 16) }, { "latest", inBand(10, 10) },
                               { "lowShare", inBand(8, 10) }, { "conj", inBand(15, 15) }, { "first", inBand(15, 15) },
                               { "rare", inBand(15, 15) }, { "occasional", inBand(15, 15) }, { "setup", inBand(15, 15) },
                               { "unmapped", inBand(15, 15) }, { "allUnknown", inBand(10, 10), CorridorShape::Unknown } }),
            wellLedger("s2", { { "clean3", inBand(10, 10) }, { "seven", inBand(7, 7) }, { "twenty", inBand(10, 10) },
                               { "thirty", inBand(15, 15) }, { "bigger", inBand(16, 16) }, { "latest", inBand(10, 10) },
                               { "lowShare", inBand(8, 10) }, { "conj", inBand(15, 15) }, { "first", inBand(15, 15) },
                               { "rare", inBand(15, 15) }, { "occasional", inBand(15, 15) }, { "setup", inBand(15, 15) },
                               { "unmapped", inBand(15, 15) }, { "unknownMixed", inBand(15, 15) },
                               { "allUnknown", inBand(10, 10), CorridorShape::Unknown } }),
            wellLedger("s3", { { "clean3", inBand(9, 10) }, { "seven", inBand(7, 7) }, { "latest", inBand(7, 10) }, { "lowShare", inBand(9, 10) },
                               { "few", unread }, { "unknownMixed", inBand(15, 15) }, { "allUnknown", inBand(10, 10), CorridorShape::Unknown } }),
        };
        // ...and ten more unknownMixed rows in s3, read against a corridor of unknown shape.
        for (const ShotRecord &r : wellLedger("x", { { "unknownMixed", std::vector<double>(10, 5.0), CorridorShape::Unknown } }).shots)
            ls[2].shots.push_back(r);

        const std::vector<DoWellRow> rows = doWell(sortedSessions(ls), ci, {}, ThemeOptions{});
        const DoWellRow *c3 = findWell(rows, "clean3");
        const double wantShare = (0.5625 * 10 / 10 + 0.75 * 10 / 10 + 1.0 * 9 / 10) / (0.5625 + 0.75 + 1.0);
        check(c3 && near(c3->share, wantShare, 1e-15) && c3->sessionsJudged == 3 && c3->swingsJudged == 30
              && c3->pips == std::vector<bool>{ true, true, true } && c3->prominence == 3,
              "kept: recency-weighted in-band share, judged sessions and swings, a pip per session");
        check(!findWell(rows, "few"), "one judged session is not enough, however many swings");
        check(!findWell(rows, "seven"), "a session must count 8 rows to be judged (three sessions of seven judge none)");
        check(!findWell(rows, "twenty"), "two judged sessions but 20 swings: under 30, not kept");
        check(findWell(rows, "thirty") && findWell(rows, "thirty")->swingsJudged == 30, "two sessions of fifteen: 30 swings, kept");
        check(!findWell(rows, "latest"), "the latest judged session under 0.8 drops it, though the weighted share is 0.87");
        check(!findWell(rows, "lowShare"), "a weighted share under 0.85 is not kept");
        const DoWellRow *um = findWell(rows, "unknownMixed");
        check(um && um->swingsJudged == 30 && um->sessionsJudged == 2, "rows read against an unknown shape are not counted");
        check(!findWell(rows, "allUnknown"), "a condition only ever read against unknown shapes is never judged");
        check(!findWell(rows, "conj") && findWell(rows, "first"), "an All conjunction is never praised; a First preference is");
        check(!findWell(rows, "rare") && findWell(rows, "occasional"), "prominence: Occasional and up only");
        check(!findWell(rows, "setup") && !findWell(rows, "unmapped"), "only fault conditions the pack knows");

        bool ranked = true;
        for (size_t i = 1; i < rows.size(); ++i) {
            const DoWellRow &a = rows[i - 1], &b = rows[i];
            ranked = ranked && (a.prominence > b.prominence
                                || (a.prominence == b.prominence && (a.share > b.share
                                    || (a.share == b.share && (a.swingsJudged > b.swingsJudged
                                        || (a.swingsJudged == b.swingsJudged && a.conditionId < b.conditionId))))));
        }
        check(ranked && rows.back().conditionId == QStringLiteral("occasional"), "ranked by prominence, share, judged swings, then id");
        QStringList order;
        for (const DoWellRow &r : rows) order.append(r.conditionId);
        check(order == QStringList{ "bigger", "first", "thirty", "unknownMixed", "clean3", "occasional" },
              "ties on prominence and share break on judged swings, then id");

        // Family exclusion in the reduction: against every needs-work row (not just the five
        // shown). The themes are not read here — that exclusion is the view's (below).
        QHash<QString, ThemeConditionInfo> fc;
        fc.insert(QStringLiteral("headStill"), cond(QStringLiteral("headStill"), 3, kThemeDetectionAny, { "headSway" }));
        fc.insert(QStringLiteral("swayFault"), cond(QStringLiteral("swayFault"), 3, kThemeDetectionAny, { "headSway", "other" }));
        fc.insert(QStringLiteral("armsWide"), cond(QStringLiteral("armsWide"), 3, kThemeDetectionAny, { "leadHandWidth" }));
        fc.insert(QStringLiteral("chest"), cond(QStringLiteral("chest"), 3, kThemeDetectionAny, { "m_rawChest" }));
        fc.insert(QStringLiteral("free"), cond(QStringLiteral("free"), 3, kThemeDetectionAny, { "untouched" }));
        fc.insert(QStringLiteral("quiet"), cond(QStringLiteral("quiet"), 3, kThemeDetectionAny, { "noneTier" }));
        fc.insert(QStringLiteral("unshown"), cond(QStringLiteral("unshown"), 3, kThemeDetectionAny, { "notSaid" }));
        std::vector<ThemeSessionInput> fs;
        for (const char *n : { "s1", "s2" })
            fs.push_back(wellLedger(QString::fromLatin1(n), { { "headStill", inBand(15, 15) }, { "armsWide", inBand(15, 15) },
                                                               { "chest", inBand(15, 15) }, { "free", inBand(15, 15) },
                                                               { "quiet", inBand(15, 15) }, { "unshown", inBand(15, 15) } }));
        SeenMostRow sway;
        sway.conditionId = QStringLiteral("swayFault");
        const std::vector<DoWellRow> fr = doWell(sortedSessions(fs), fc, { sway }, ThemeOptions{});
        check(!findWell(fr, "headStill"), "a family shared with a needs-work condition is not praised");
        check(fr.size() == 5 && findWell(fr, "armsWide") && findWell(fr, "armsWide")->families == QStringList{ "leadHandWidth" },
              "the reduction keeps the rest, each carrying its families for the view");

        // ...and in the view: against EVERY member of each theme the page SHOWS.
        SwingThemes fv;
        fv.enough = true;
        fv.doWell = fr;
        auto th = [](ThemeTier tier, double var, std::vector<ThemeMember> m) {
            SwingTheme t;
            t.tier = tier;
            t.varianceShare = var;
            t.members = std::move(m);
            return t;
        };
        auto mem = [](const char *id, double loading) { return ThemeMember{ QString::fromLatin1(id), loading, true, 4 }; };
        fv.themes = {
            // Shown: named by its two strongest members; m_width is a third, unnamed member.
            th(ThemeTier::Possibly, 0.30, { mem("m_a", 0.9), mem("m_b", 0.8), mem("m_width", 0.5), mem("m_rawChest", -0.45) }),
            th(ThemeTier::None, 0.50, { mem("m_quiet", 0.9), mem("m_quiet2", 0.8) }),            // no tier: never shown
            th(ThemeTier::Probably, 0.20, { mem("m_silent", 0.9), mem("m_notSaid", 0.8) }),        // no phrase: never shown
        };
        ThemePhrases fp;
        fp.well = [](const QString &id) { return QStringLiteral("your %1 is fine").arg(id); };
        fp.measure = [](const QString &id, bool) {
            return id == QStringLiteral("m_silent") || id == QStringLiteral("m_notSaid") ? QString() : QStringLiteral("%1 moves").arg(id);
        };
        fp.family = [](const QString &id) {
            if (id == QStringLiteral("m_width"))  return QStringLiteral("leadHandWidth");
            if (id == QStringLiteral("m_quiet"))  return QStringLiteral("noneTier");
            if (id == QStringLiteral("m_notSaid")) return QStringLiteral("notSaid");
            return QString();                                   // m_rawChest: no metricKey, its id
        };
        const SwingSummaryView fview = swingSummaryView(fv, fp);
        QStringList praised;
        for (const DoWellItem &i : fview.doWell) praised.append(i.text);
        check(fview.together.size() == 1, "one theme shown (the others have no tier or no phrase)");
        check(!praised.contains(QStringLiteral("Your armsWide is fine")),
              "a shown theme's member excludes its family, though the sentence does not name it");
        check(!praised.contains(QStringLiteral("Your chest is fine")), "a member with no metricKey is its own family");
        check(praised.contains(QStringLiteral("Your quiet is fine")), "a theme with no tier excludes nothing");
        check(praised.contains(QStringLiteral("Your unshown is fine")), "a tiered theme the page does not show excludes nothing");
        check(praised.contains(QStringLiteral("Your free is fine")) && praised.size() == 3, "an untouched family is praised");
        fv.enough = false;
        check(swingSummaryView(fv, fp, fiveWell()).doWell.size() == 5, "no themes shown (not enough data): nothing excluded by them");

        // The family root: a "Signed" metricKey is its unsigned quantity.
        check(themeFamilyRoot(QStringLiteral("pelvisRotationSigned")) == QStringLiteral("pelvisRotation")
              && themeFamilyRoot(QStringLiteral("pelvisRotation")) == QStringLiteral("pelvisRotation")
              && themeFamilyRoot(QStringLiteral("SignedOff")) == QStringLiteral("SignedOff") && themeFamilyRoot(QString()).isEmpty(),
              "family root: a trailing \"Signed\" is removed, nothing else");
        ThemePhrases sp;
        sp.measure = [](const QString &id, bool) { return QStringLiteral("your %1 turns").arg(id); };
        sp.family = [](const QString &id) {
            return themeFamilyRoot(id == QStringLiteral("hipRate") ? QStringLiteral("pelvisRotationSigned")
                                 : id == QStringLiteral("hipTurn") ? QStringLiteral("pelvisRotation") : QString());
        };
        sp.well = [](const QString &) { return QStringLiteral("your hips keep turning through impact"); };
        SwingThemes sg;
        sg.enough = true;
        sg.themes = { th(ThemeTier::Firm, 0.3, { mem("hipTurn", 0.9), mem("hipRate", 0.8), mem("chest", 0.7) }) };
        DoWellRow stall;
        stall.conditionId = QStringLiteral("hip_stall");
        stall.share = 0.95;
        stall.families = QStringList{ themeFamilyRoot(QStringLiteral("pelvisRotationSigned")) };
        sg.doWell = { stall };
        const SwingSummaryView sv = swingSummaryView(sg, sp);
        check(sv.together.size() == 1 && sv.together[0].first == QStringLiteral("When your hipTurn turns")
              && sv.together[0].second == QStringLiteral("your chest turns"),
              "a signed member is the same family as its unsigned one in a theme's sentence");
        check(sv.doWell.empty(), "praise read on a Signed key is excluded by a theme moving the unsigned one");

        // Through the reduction: every shot and session counted, and do-well beside layer 1.
        QHash<QString, ThemeConditionInfo> rc = faultsOf({ "fires", "clear" });
        rc[QStringLiteral("clear")].families = QStringList{ "fires" };     // same quantity as the fault
        rc.insert(QStringLiteral("solo"), cond(QStringLiteral("solo")));
        std::vector<ThemeSessionInput> rs;
        for (const char *n : { "s2", "s1" })
            rs.push_back(wellLedger(QString::fromLatin1(n), { { "fires", inBand(0, 16) }, { "clear", inBand(16, 16) }, { "solo", inBand(16, 16) } }));
        for (ThemeSessionInput &x : rs)     // honest states here: layer 1 reads them
            for (ShotRecord &shot : x.shots)
                for (ConditionRow &r : shot.rows) r.state = r.conditionId == QStringLiteral("fires") ? ShotState::Fired : ShotState::Clean;
        rs.push_back(ThemeSessionInput{ QStringLiteral("s0_empty"), {} });
        const SwingThemes red = reduceSwingThemes(rs, {}, rc);
        check(red.allSwings == 32 && red.allSessions == 2 && red.sessionNames.size() == 3,
              "every shot and every session holding one are counted (an empty session is not)");
        check(red.seenMost.size() == 1 && red.seenMost[0].conditionId == QStringLiteral("fires")
              && red.doWell.size() == 1 && red.doWell[0].conditionId == QStringLiteral("solo"),
              "the reduction: a needs-work fault's family keeps its sibling out of \"do well\"");
        std::atomic<bool> stopNow{ true };
        const SwingThemes cut = reduceSwingThemes(rs, {}, rc, ThemeOptions{}, {}, &stopNow);
        check(cut.cancelled && cut.doWell.size() == 1, "cancelled: layer 1 and \"do well\" are still filled");
    }

    // ── The view ─────────────────────────────────────────────────────────────────────────────────
    {
        ThemePhrases ph;
        ph.condition = [](const QString &id) {
            if (id == QStringLiteral("silent")) return QString();
            if (id.startsWith(QStringLiteral("dup"))) return QStringLiteral("you stand up through the ball");
            return QStringLiteral("your %1 moves").arg(id);
        };
        ph.well = [](const QString &id) {
            if (id == QStringLiteral("silent")) return QString();
            if (id.startsWith(QStringLiteral("dup"))) return QStringLiteral("you keep your arms wide");
            return QStringLiteral("your %1 stays put").arg(id);
        };
        ph.measure = [](const QString &id, bool high) {
            if (id == QStringLiteral("none")) return QString();
            return QStringLiteral("your %1 %2").arg(id, high ? QStringLiteral("rises") : QStringLiteral("drops"));
        };
        ph.family = [](const QString &id) { return id.startsWith(QStringLiteral("hip")) ? QStringLiteral("pelvisSway") : QString(); };

        SwingThemes one;
        one.allSwings = 1;
        one.allSessions = 1;
        check(swingSummaryView(one, ph).subtitle == QStringLiteral("From 1 swing over 1 session"), "subtitle: singular");
        one.allSwings = 2;
        check(swingSummaryView(one, ph).subtitle == QStringLiteral("From 2 swings over 1 session"), "subtitle: swings plural, session singular");
        one.allSwings = 135;
        one.allSessions = 5;
        check(swingSummaryView(one, ph).subtitle == QStringLiteral("From 135 swings over 5 sessions"), "subtitle: plural");
        check(swingSummaryView(SwingThemes{}, ph).subtitle == QStringLiteral("From 0 swings over 0 sessions"), "subtitle: nothing yet is plural");

        auto well = [](const char *id, double share, std::vector<bool> pips) {
            DoWellRow r;
            r.conditionId = QString::fromLatin1(id);
            r.share = share;
            r.pips = std::move(pips);
            return r;
        };
        SwingThemes v;
        v.enough = true;
        v.doWell = { well("a", 1.0, { true, true }), well("silent", 1.0, {}), well("dupA", 0.995, { true }), well("dupB", 0.99, {}),
                     well("b", 0.994, { false, true }), well("c", 0.9, {}), well("d", 0.899, {}), well("e", 0.86, {}), well("f", 0.86, {}) };
        check(swingSummaryView(v, ph).doWell.size() == 3, "do well: at most three by default");
        const SwingSummaryView vw = swingSummaryView(v, ph, fiveWell());
        check(vw.doWell.size() == 5, "do well: at most maxDoWell");
        check(vw.doWell.size() == 5 && vw.doWell[0].text == QStringLiteral("Your a stays put")
              && vw.doWell[0].caption == QStringLiteral("In the ideal range on every swing") && vw.doWell[0].pips == std::vector<bool>{ true, true },
              "do well: the capitalised golferWell phrase, its caption and pips");
        check(vw.doWell.size() == 5 && vw.doWell[1].text == QStringLiteral("You keep your arms wide")
              && vw.doWell[1].caption == QStringLiteral("In the ideal range on every swing")
              && vw.doWell[2].text == QStringLiteral("Your b stays put"),
              "do well: an empty phrase is skipped, a repeated one told once; 0.995 is \"every swing\"");
        check(vw.doWell.size() == 5 && vw.doWell[2].caption == QStringLiteral("In the ideal range on almost every swing")
              && vw.doWell[3].caption == QStringLiteral("In the ideal range on almost every swing")
              && vw.doWell[4].caption == QStringLiteral("In the ideal range on most swings"),
              "captions: 0.994 and 0.9 are \"almost every\", 0.899 is \"most\"");

        auto seen = [](const char *id, double share, const char *trend, std::vector<bool> pips) {
            SeenMostRow r;
            r.conditionId = QString::fromLatin1(id);
            r.share = share;
            r.trend = QString::fromLatin1(trend);
            r.pips = std::move(pips);
            return r;
        };
        v.seenMost = { seen("a", 0.95, "steady", { true, true }), seen("silent", 0.9, "", {}), seen("dupA", 0.9, "growing", { true }),
                       seen("dupB", 0.85, "", {}), seen("b", 0.7, "easing", {}), seen("c", 0.69, "", {}), seen("d", 0.6, "", {}),
                       seen("e", 0.55, "", {}) };
        const SwingSummaryView nw = swingSummaryView(v, ph);
        check(nw.needsWork.size() == 5 && nw.needsWork[0].text == QStringLiteral("Your a moves") && nw.needsWork[0].share == 0.95
              && nw.needsWork[0].frequency == QStringLiteral("almost every swing") && nw.needsWork[0].trend == 0
              && nw.needsWork[0].pips == std::vector<bool>{ true, true },
              "needs work: the capitalised golfer phrase, share, frequency, trend and pips");
        check(nw.needsWork.size() == 5 && nw.needsWork[1].text == QStringLiteral("You stand up through the ball") && nw.needsWork[1].trend == 1
              && nw.needsWork[2].text == QStringLiteral("Your b moves") && nw.needsWork[2].trend == -1
              && nw.needsWork[2].frequency == QStringLiteral("most swings")
              && nw.needsWork[3].frequency == QStringLiteral("more than half your swings")
              && nw.needsWork[4].text == QStringLiteral("Your d moves"),
              "needs work: skipping and dedupe, growing +1 / easing −1, 0.7 is \"most\", 0.69 \"more than half\", at most five");

        auto theme = [](ThemeTier tier, double var, int startsAt, int trend, std::vector<ThemeMember> m) {
            SwingTheme t;
            t.tier = tier;
            t.varianceShare = var;
            t.startsAt = startsAt;
            t.trend = trend;
            t.members = std::move(m);
            return t;
        };
        auto member = [](const char *id, double loading, bool high) { return ThemeMember{ QString::fromLatin1(id), loading, high, 0 }; };
        v.themes = {
            theme(ThemeTier::Possibly, 0.30, 1, 0, { member("head", 0.8, true) }),
            theme(ThemeTier::Firm, 0.10, 5, -1, { member("none", 0.9, true), member("chest", 0.7, false), member("knee", -0.6, true) }),
            theme(ThemeTier::Probably, 0.20, 0, 1, { member("hipA", 0.9, true), member("hipB", 0.8, true), member("arm", 0.7, true) }),
            theme(ThemeTier::Firm, 0.25, 10, 0, { member("none", 0.9, true) }),
            theme(ThemeTier::None, 0.90, 1, 0, { member("lost", 0.9, true), member("found", 0.8, true) }),
            theme(ThemeTier::Probably, 0.05, 7, 0, { member("wrist", 0.9, true), member("elbow", 0.8, false) }),
            theme(ThemeTier::Possibly, 0.04, 4, 0, { member("late", 0.9, true), member("later", 0.8, false) }),
        };
        const SwingSummaryView tv = swingSummaryView(v, ph);
        check(tv.together.size() == 4, "together: at most four, tier-less and phrase-less themes never shown");
        const TogetherItem *t0 = tv.together.size() == 4 ? &tv.together[0] : nullptr;
        check(t0 && t0->tier == QStringLiteral("firm") && t0->first == QStringLiteral("When your chest drops")
              && t0->second == QStringLiteral("your knee rises") && t0->startStop == 3
              && t0->startWords == QStringLiteral("starts coming down") && t0->trend == -1,
              "together: firm first; \"When A\" / \"B\" from the two strongest phrased members; P5 is \"coming down\"");
        check(tv.together.size() == 4 && tv.together[1].tier == QStringLiteral("probably")
              && tv.together[1].first == QStringLiteral("When your hipA rises") && tv.together[1].second == QStringLiteral("your arm rises")
              && tv.together[1].startStop == -1 && tv.together[1].startWords.isEmpty() && tv.together[1].trend == 1,
              "together: a family already said is skipped; an unplaced theme has no stop and no words");
        check(tv.together.size() == 4 && tv.together[2].first == QStringLiteral("When your wrist rises")
              && tv.together[2].startStop == 4 && tv.together[2].startWords == QStringLiteral("starts around impact"),
              "together: then by variance within a tier");
        check(tv.together.size() == 4 && tv.together[3].tier == QStringLiteral("possibly")
              && tv.together[3].first == QStringLiteral("Your head rises") && tv.together[3].second.isEmpty()
              && tv.together[3].startStop == 0 && tv.together[3].startWords == QStringLiteral("starts at address"),
              "together: a theme with one part is that part capitalised, with no second half");
        check(tv.note.isEmpty(), "note: empty when something goes together");

        const std::vector<int> stops{ themeStartStop(0), themeStartStop(1), themeStartStop(2), themeStartStop(3), themeStartStop(4),
                                      themeStartStop(5), themeStartStop(6), themeStartStop(7), themeStartStop(8), themeStartStop(9),
                                      themeStartStop(10) };
        check(stops == std::vector<int>{ -1, 0, 1, 1, 2, 3, 3, 4, 5, 5, 5 }, "timeline stops: P1 address, P2–3 back, P4 top, P5–6 down, P7 impact, P8+ finish");
        check(themeStartStopWords(0) == QStringLiteral("starts at address") && themeStartStopWords(1) == QStringLiteral("starts in the backswing")
              && themeStartStopWords(2) == QStringLiteral("starts at the top") && themeStartStopWords(3) == QStringLiteral("starts coming down")
              && themeStartStopWords(4) == QStringLiteral("starts around impact") && themeStartStopWords(5) == QStringLiteral("starts in the finish")
              && themeStartStopWords(-1).isEmpty(), "timeline words for every stop");

        SwingThemes quiet;
        quiet.enough = true;
        quiet.themes = { theme(ThemeTier::None, 0.5, 4, 0, { member("head", 0.9, true) }), theme(ThemeTier::Firm, 0.3, 4, 0, { member("none", 0.9, true) }) };
        check(swingSummaryView(quiet, ph).together.empty()
              && swingSummaryView(quiet, ph).note == QStringLiteral("Nothing yet — your faults don't rise and fall together clearly enough to say."),
              "note: \"Nothing yet\" when no theme can be said");
        SwingThemes thin = v;
        thin.enough = false;
        thin.swings = 40;
        thin.sessions = 2;
        check(swingSummaryView(thin, ph).together.empty()
              && swingSummaryView(thin, ph).note == QStringLiteral("Not yet — it takes about 60 swings over 3 sessions to see what goes "
                                                                   "together (so far: 40 swings over 2 sessions)."),
              "note: \"Not yet\" with the counts when there is too little data, whatever the themes say");

        QStringList said{ tv.subtitle, tv.note, quiet.enough ? swingSummaryView(quiet, ph).note : QString(), swingSummaryView(thin, ph).note };
        for (const DoWellItem &i : vw.doWell) said << i.text << i.caption;
        for (const NeedsWorkItem &i : nw.needsWork) said << i.text << i.frequency;
        for (const TogetherItem &i : tv.together) said << i.first + QLatin1Char(' ') + i.second << i.startWords;
        allLines << said;
    }

    // ── Layer 2 wording ──────────────────────────────────────────────────────────────────────────
    {
        auto theme = [](ThemeTier tier, double var, int startsAt, int trend, std::vector<ThemeMember> m) {
            SwingTheme t;
            t.tier = tier;
            t.varianceShare = var;
            t.startsAt = startsAt;
            t.trend = trend;
            t.members = std::move(m);
            return t;
        };
        auto member = [](const char *id, double loading, bool high) { return ThemeMember{ QString::fromLatin1(id), loading, high, 0 }; };
        ThemePhrases ph;
        ph.measure = [](const QString &id, bool high) {
            if (id == QStringLiteral("none")) return QString();
            if (id == QStringLiteral("dup")) return QStringLiteral("your hips slide");
            return QStringLiteral("%1 %2").arg(id, high ? QStringLiteral("rises") : QStringLiteral("drops"));
        };
        ph.family = [](const QString &id) { return id.startsWith(QStringLiteral("hip")) ? QStringLiteral("pelvisSway") : QString(); };

        SwingThemes w;
        w.enough = true;
        w.themes = {
            theme(ThemeTier::Possibly, 0.30, 1, 0, { member("head", 0.8, true) }),
            theme(ThemeTier::Firm, 0.10, 5, -1, { member("none", 0.9, true), member("chest", 0.7, false), member("knee", -0.6, true), member("arm", 0.5, true) }),
            theme(ThemeTier::Probably, 0.20, 0, 1, { member("hipA", 0.9, true), member("hipB", 0.8, true), member("dup", 0.7, true) }),
            theme(ThemeTier::Firm, 0.25, 10, 0, { member("dup", 0.9, true), member("dup", 0.8, true) }),
            theme(ThemeTier::None, 0.90, 1, 0, { member("lost", 0.9, true) }),
            theme(ThemeTier::Possibly, 0.05, 9, 0, { member("none", 0.9, true) }),
        };
        const SwingSummaryLines L = swingSummaryLines(w, ph);
        check(L.together.size() == 4, "at most four themes are said, and a tier-less or phrase-less one never is");
        check(L.together.value(0) == QStringLiteral("Your hips slide. It shows in the finish. (no clear change yet)"),
              "firm first, by variance; one phrase stands alone, capitalised; a repeated phrase is skipped");
        check(L.together.value(1) == QStringLiteral("On swings where chest drops, knee rises. It starts on the way down. (may be easing)"),
              "\"On swings where A, B\" from the two strongest phrased members, in the direction each moves");
        check(L.together.value(2) == QStringLiteral("Probably: On swings where hipA rises, your hips slide. (may be growing)"),
              "Probably: lead; a second member of a family already said is skipped; no start when unplaced");
        check(L.together.value(3) == QStringLiteral("Possibly: Head rises. It starts at address. (no clear change yet)"),
              "Possibly: lead");

        const QStringList starts{ themeStartWords(1), themeStartWords(2), themeStartWords(3), themeStartWords(4), themeStartWords(5),
                                  themeStartWords(6), themeStartWords(7), themeStartWords(8), themeStartWords(9), themeStartWords(10) };
        check(starts == QStringList{ "It starts at address", "It starts early in the backswing", "It starts in the backswing",
                                     "It starts in the backswing", "It starts on the way down", "It starts on the way down",
                                     "It starts around impact", "It starts after impact", "It starts in the follow-through",
                                     "It shows in the finish" } && themeStartWords(0).isEmpty(),
              "start words for every swing position");
        check(themeTrendWords(-1) == QStringLiteral("may be easing") && themeTrendWords(1) == QStringLiteral("may be growing")
              && themeTrendWords(0) == QStringLiteral("no clear change yet"), "trend words");
        allLines << L.together;

        bool clean = !allLines.isEmpty();
        for (const QString &line : allLines) clean = clean && !banned(line);
        check(clean, "no line says because / cause / due to / leads to");
    }

    // ── Your focus: the groups ───────────────────────────────────────────────────────────────────
    {
        auto info = [](const char *id, const char *drill, int when) {
            ThemeConditionInfo c = cond(QString::fromLatin1(id));
            c.drill = QString::fromLatin1(drill);
            c.when = when;
            return c;
        };
        auto seen = [](const char *id, double share) {
            SeenMostRow r;
            r.conditionId = QString::fromLatin1(id);
            r.share = share;
            return r;
        };
        auto keys = [](const std::vector<FocusGroup> &g) {
            QStringList k;
            for (const FocusGroup &x : g) k.append(x.key);
            return k;
        };
        QHash<QString, ThemeConditionInfo> ci;
        for (const ThemeConditionInfo &c : { info("e1", "", 0), info("b1", "", 3), info("a1", "drill.d1", 5), info("c1", "drill.d2", 3),
                                             info("a3", "drill.d1", 6), info("a2", "drill.d1", 3), info("f1", "drill.d3", 7) })
            ci.insert(c.id, c);
        // seenMost order (share descending), as the reduction hands it over.
        const std::vector<SeenMostRow> sm{ seen("e1", 0.99), seen("b1", 0.95), seen("a1", 0.9), seen("c1", 0.8),
                                           seen("a3", 0.6), seen("a2", 0.6), seen("f1", 0.55) };
        const std::vector<FocusGroup> fo = focusOrder(sm, ci);
        check(keys(fo) == QStringList{ "drill.d1", "drill.d2", "solo:b1", "drill.d3", "solo:e1" },
              "focus order: earliest first, a drill before none at the same position, unplaced (0) last");
        check(fo.size() == 5 && fo[0].drill == QStringLiteral("drill.d1") && fo[0].when == 3
              && fo[0].conditionIds == QStringList{ "a1", "a2", "a3" },
              "a drill's faults are one group, placed at its earliest fault; within it by share, then id");
        check(fo.size() == 5 && fo[2].drill.isEmpty() && fo[2].conditionIds == QStringList{ "b1" } && fo[4].when == 0,
              "a fault with no drill is a group of its own; an unplaced group keeps when 0");

        // Same position, both with a drill: more faults, then the higher top share, then key.
        QHash<QString, ThemeConditionInfo> ct;
        for (const ThemeConditionInfo &c : { info("g1", "drill.da", 4), info("h1", "drill.db", 4), info("i1", "drill.dc", 4),
                                             info("j1", "drill.dd", 4), info("j2", "drill.dd", 4), info("s2", "", 4), info("s1", "", 4) })
            ct.insert(c.id, c);
        const std::vector<FocusGroup> tie = focusOrder({ seen("s2", 0.9), seen("s1", 0.9), seen("h1", 0.8), seen("i1", 0.8), seen("g1", 0.7),
                                                         seen("j1", 0.5), seen("j2", 0.5) }, ct);
        check(keys(tie) == QStringList{ "drill.dd", "drill.db", "drill.dc", "drill.da", "solo:s1", "solo:s2" },
              "ties: more faults first, then the higher share, then key; solo groups after drills, by key on a tie");
        check(tie.size() == 6 && tie[0].conditionIds == QStringList{ "j1", "j2" }, "within a group, equal shares by id");
        check(focusOrder({}, ct).empty(), "nothing needs work: no groups");

        // Through the reduction: every needs-work fault grouped, cancelled or not.
        QHash<QString, ThemeConditionInfo> rc;
        rc.insert(QStringLiteral("x"), info("x", "drill.d", 6));
        rc.insert(QStringLiteral("y"), info("y", "drill.d", 4));
        rc.insert(QStringLiteral("z"), info("z", "", 2));
        std::vector<ThemeSessionInput> rs{ ledgerOf(QStringLiteral("s1"), { { "x", rate(10, 10) }, { "y", rate(9, 10) }, { "z", rate(2, 10) } }) };
        const SwingThemes red = reduceSwingThemes(rs, {}, rc);
        check(red.focusOrder.size() == 1 && red.focusOrder[0].key == QStringLiteral("drill.d") && red.focusOrder[0].when == 4
              && red.focusOrder[0].conditionIds == QStringList{ "x", "y" },
              "the reduction groups every needs-work fault (and only those)");
        std::atomic<bool> stopNow{ true };
        check(reduceSwingThemes(rs, {}, rc, ThemeOptions{}, {}, &stopNow).focusOrder.size() == 1, "cancelled: the focus groups are still filled");
    }

    // ── Your focus: the words ────────────────────────────────────────────────────────────────────
    {
        ThemePhrases ph;
        ph.condition = [](const QString &id) {
            if (id.startsWith(QStringLiteral("silent"))) return QString();
            if (id.startsWith(QStringLiteral("dup"))) return QStringLiteral("you dup it");
            return QStringLiteral("you do %1").arg(id);
        };
        ph.well = [](const QString &id) {
            if (id == QStringLiteral("k2")) return QString();
            if (id.startsWith(QStringLiteral("dup"))) return QStringLiteral("you keep it wide");
            return QStringLiteral("you keep %1").arg(id);
        };
        ph.why = [](const QString &id) {
            if (id.startsWith(QStringLiteral("dup"))) return QStringLiteral("Same why.");
            return QStringLiteral("Why %1.").arg(id);
        };
        ph.drill = [](const QString &id) { return ThemeDrill{ QStringLiteral("Label ") + id, QStringLiteral("Do ") + id }; };
        auto row = [](const char *id, double share, const char *trend = "", std::vector<bool> pips = {}) {
            SeenMostRow r;
            r.conditionId = QString::fromLatin1(id);
            r.share = share;
            r.trend = QString::fromLatin1(trend);
            r.pips = std::move(pips);
            return r;
        };
        auto group = [](const char *key, const char *drill, int when, QStringList ids) {
            return FocusGroup{ QString::fromLatin1(key), QString::fromLatin1(drill), when, std::move(ids) };
        };

        // The earliest group, with a drill and three faults.
        SwingThemes a;
        a.seenMost = { row("e1", 0.99), row("b1", 0.95), row("a1", 1.0, "growing", { false, true }), row("c1", 0.8, "easing"),
                       row("a2", 0.6), row("a3", 0.6), row("f1", 0.55), row("z1", 0.52) };
        a.focusOrder = { group("drill.d1", "drill.d1", 3, { "a1", "a2", "a3" }), group("drill.d2", "drill.d2", 3, { "c1" }),
                         group("solo:b1", "", 3, { "b1" }), group("drill.d3", "drill.d3", 7, { "f1" }), group("solo:e1", "", 0, { "e1" }),
                         group("solo:z1", "", 0, { "z1" }) };
        const SwingSummaryView av = swingSummaryView(a, ph);
        const FocusItem &f = av.focus;
        check(f.present && f.title == QStringLiteral("Label drill.d1") && f.practiseLabel == QStringLiteral("Label drill.d1")
              && f.practise == QStringLiteral("Do drill.d1"),
              "focus: the first group, titled by its drill, with the drill to practise");
        check(f.aimFor == QStringList{ "You keep a1", "You keep a2", "You keep a3" }, "focus: aim for each fault's capitalised golferWell");
        check(f.rightNow.size() == 3 && f.rightNow[0].text == QStringLiteral("You do a1") && f.rightNow[0].share == 1.0
              && f.rightNow[0].frequency == QStringLiteral("every swing") && f.rightNow[0].trend == 1
              && f.rightNow[0].pips == std::vector<bool>{ false, true } && f.rightNow[2].text == QStringLiteral("You do a3"),
              "focus: right now is each fault as a needs-work item (text, share, frequency, trend, pips), in group order");
        check(f.why == QStringLiteral("Why a1. Why a2. Why a3."), "focus: the golferWhy sentences joined by one space");
        check(f.reason == QStringLiteral("Picked first: it comes earliest in your swing and covers three of the things on your list."),
              "focus: the reason — earliest, and how many of the list it covers, in words");
        check(f.conditionIds == QStringList{ "a1", "a2", "a3" }, "focus: carries its group's faults");
        check(av.next.size() == 4 && av.next[0].text == QStringLiteral("You do c1") && av.next[0].trend == -1
              && av.next[0].frequency == QStringLiteral("most swings") && av.next[1].text == QStringLiteral("You do b1")
              && av.next[1].frequency == QStringLiteral("almost every swing") && av.next[2].text == QStringLiteral("You do f1")
              && av.next[3].text == QStringLiteral("You do e1"),
              "next: every other fault in group order, at most four (the fifth is not told)");

        // An earlier group that says nothing is passed over; then the reason is frequency, and
        // repeated phrases are told once — in the focus, across aim / why, and against next.
        SwingThemes b;
        b.seenMost = { row("silent1", 0.9), row("dup1", 0.8), row("dup3", 0.75), row("dup2", 0.7), row("k2", 0.6), row("m1", 0.6) };
        b.focusOrder = { group("drill.ds", "drill.ds", 1, { "silent1" }), group("drill.dk", "drill.dk", 4, { "dup1", "dup2", "k2" }),
                         group("solo:dup3", "", 5, { "dup3" }), group("solo:m1", "", 6, { "m1" }) };
        const SwingSummaryView bv = swingSummaryView(b, ph);
        check(bv.focus.present && bv.focus.title == QStringLiteral("Label drill.dk") && bv.focus.rightNow.size() == 2
              && bv.focus.rightNow[0].text == QStringLiteral("You dup it") && bv.focus.rightNow[1].text == QStringLiteral("You do k2"),
              "focus: a group with no phrase is passed over; a repeated phrase is one right-now item");
        check(bv.focus.aimFor == QStringList{ "You keep it wide" } && bv.focus.why == QStringLiteral("Same why. Why k2."),
              "focus: an empty or repeated aim is skipped, a repeated why told once");
        check(bv.focus.reason == QStringLiteral("Picked first: it shows up on the most of your swings and covers two of the things on your list."),
              "focus: not the earliest group, so the reason is how often it shows up");
        check(bv.next.size() == 1 && bv.next[0].text == QStringLiteral("You do m1"),
              "next: a phrase the focus already says is not told again; an empty phrase never");

        // A solo group: titled by its own golferWell, nothing to practise, one fault covers no list.
        SwingThemes c;
        c.seenMost = { row("q1", 0.7) };
        c.focusOrder = { group("solo:q1", "", 2, { "q1" }) };
        const SwingSummaryView cv = swingSummaryView(c, ph);
        check(cv.focus.present && cv.focus.title == QStringLiteral("You keep q1") && cv.focus.practiseLabel.isEmpty()
              && cv.focus.practise.isEmpty() && cv.focus.aimFor == QStringList{ "You keep q1" }
              && cv.focus.reason == QStringLiteral("Picked first: it comes earliest in your swing.") && cv.next.empty(),
              "solo focus: its golferWell as the title, no drill, and the reason without a count");

        // Ten faults under one drill: the count in digits.
        SwingThemes d;
        FocusGroup ten = group("drill.t", "drill.t", 0, {});
        for (int i = 0; i < 10; ++i) {
            const QString id = QStringLiteral("t%1").arg(i);
            d.seenMost.push_back(row("", 0.6));
            d.seenMost.back().conditionId = id;
            ten.conditionIds.append(id);
        }
        d.focusOrder = { ten };
        check(swingSummaryView(d, ph).focus.reason
                  == QStringLiteral("Picked first: it comes earliest in your swing and covers 10 of the things on your list."),
              "focus: an unplaced group alone is still the earliest; ten and up in digits");
        check(themeCountWords(2) == QStringLiteral("two") && themeCountWords(5) == QStringLiteral("five")
              && themeCountWords(9) == QStringLiteral("nine") && themeCountWords(10) == QStringLiteral("10"),
              "count words: two to nine, then digits");

        // Nothing to focus on.
        const SwingSummaryView none = swingSummaryView(SwingThemes{}, ph);
        check(!none.focus.present && none.focus.title.isEmpty() && none.focus.rightNow.empty() && none.next.empty(),
              "focus absent when nothing needs work");
        SwingThemes mute;
        mute.seenMost = { row("silent1", 0.9), row("silent2", 0.8) };
        mute.focusOrder = { group("solo:silent1", "", 4, { "silent1" }), group("solo:silent2", "", 5, { "silent2" }) };
        check(!swingSummaryView(mute, ph).focus.present && swingSummaryView(mute, ph).next.empty(),
              "focus absent when no needs-work fault has a phrase");

        // Frequency words, everywhere a fault is told.
        check(themeFrequency(1.0) == QStringLiteral("every swing") && themeFrequency(0.995) == QStringLiteral("every swing")
              && themeFrequency(0.994) == QStringLiteral("almost every swing") && themeFrequency(0.9) == QStringLiteral("almost every swing")
              && themeFrequency(0.7) == QStringLiteral("most swings") && themeFrequency(0.69) == QStringLiteral("more than half your swings"),
              "frequency: 0.995 and up is \"every swing\", then almost every, most, more than half");
        check(av.needsWork.size() == 5 && av.needsWork[0].frequency == QStringLiteral("almost every swing")
              && av.needsWork[2].frequency == QStringLiteral("every swing"),
              "needs work says \"every swing\" too");

        // The reason included: it says why the app picked this focus, and it is worded without
        // "because" so the page's one rule — no causal words anywhere — holds without exceptions.
        QStringList said{ f.title, f.why, bv.focus.why, f.reason, bv.focus.reason };
        said << f.aimFor;
        for (const NeedsWorkItem &i : f.rightNow) said << i.text << i.frequency;
        for (const NeedsWorkItem &i : av.next) said << i.text << i.frequency;
        bool clean = true;
        for (const QString &line : said) clean = clean && !banned(line);
        check(clean, "the focus, its reason and next say no because / cause / due to / leads to");
    }

    // ── JSON ─────────────────────────────────────────────────────────────────────────────────────
    {
        SwingThemes fj;
        fj.focusOrder = { FocusGroup{ QStringLiteral("drill.d1"), QStringLiteral("drill.d1"), 4, QStringList{ "a", "b" } },
                          FocusGroup{ QStringLiteral("solo:c"), QString(), 0, QStringList{ "c" } } };
        bool ok = false;
        const SwingThemes back = swingThemesFromJson(toJson(fj), &ok);
        check(ok && back.focusOrder.size() == 2 && back.focusOrder[0].key == QStringLiteral("drill.d1")
              && back.focusOrder[0].drill == QStringLiteral("drill.d1") && back.focusOrder[0].when == 4
              && back.focusOrder[0].conditionIds == QStringList{ "a", "b" } && back.focusOrder[1].drill.isEmpty()
              && back.focusOrder[1].when == 0 && toJson(back) == toJson(fj),
              "round-trip: the focus groups");
        QJsonObject rule2 = toJson(fj);
        rule2[QStringLiteral("ruleVersion")] = 2;
        bool ok2 = true;
        check(kThemeRuleVersion == 3 && swingThemesFromJson(rule2, &ok2).focusOrder.empty() && !ok2,
              "a rule-2 file (no focus groups) is refused, to be re-derived");
    }

    // ── JSON ─────────────────────────────────────────────────────────────────────────────────────
    {
        bool ok = false;
        const SwingThemes back = swingThemesFromJson(toJson(st), &ok);
        check(ok && toJson(back) == toJson(st), "round-trip: what is written is what is read");
        check(back.themes.size() == st.themes.size() && back.themes[0].tier == st.themes[0].tier
              && back.themes[0].members.size() == st.themes[0].members.size()
              && back.themes[0].members[0].measureId == st.themes[0].members[0].measureId
              && back.loadings == st.loadings && back.seenMost.size() == st.seenMost.size(),
              "round-trip: themes, members, loadings and layer 1");
        QJsonObject stale = toJson(st);
        stale[QStringLiteral("ruleVersion")] = kThemeRuleVersion + 1;
        bool ok2 = true;
        check(swingThemesFromJson(stale, &ok2).themes.empty() && !ok2, "another rule's file is refused, to be re-derived");
        SwingTheme nanTheme;
        nanTheme.bootP05 = std::numeric_limits<double>::quiet_NaN();
        SwingThemes withNan;
        withNan.themes.push_back(nanTheme);
        const SwingThemes nb = swingThemesFromJson(toJson(withNan), &ok);
        check(ok && std::isnan(nb.themes[0].bootP05), "a non-finite number survives as null");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
