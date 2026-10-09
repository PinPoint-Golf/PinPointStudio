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

// SWING THEMES — pure, header-only, no Qt-GUI. The home screen's plain-language summary of ONE
// golfer's swing, reduced from every session's ledger (docs/design/home_themes_design.md; the
// prototype it ports is tools/themes/theme_pca.py, theme_data.py and theme_summary.py).
//
//   reduceSwingThemes()   every session's ledger -> the two layers below, "what you do well" and
//                         the focus groups, as numbers.
//   swingSummaryView()    those numbers -> what the home screen draws, in the golfer's own words
//                         (the pack's `golfer`, `golferWell`, `golferWhy`, `golferHigh` and
//                         `golferLow`, and the drill registry's labels and instructions): a
//                         subtitle, YOUR FOCUS and "next on your list", the "do well" and "needs
//                         work" cards with a pip per session, the "goes together" pairs with their
//                         timeline stop, and a note.
//   swingSummaryLines()   the same words as flat sentences (the older form; kept for the probe).
//
// TWO LAYERS, KEPT APART ON PURPOSE, because a fault that is present and faults that move together
// are different facts:
//
//   1. WHAT WE SEE MOST — the faults present on most swings: fired / assessed per condition per
//      session, a session counting only when it judged the fault on enough swings, ranked by a
//      recency-weighted share. Works from one session.
//   2. WHAT GOES TOGETHER — groups of measures that rise and fall together from swing to swing once
//      each session's own level is taken out: PCA on the correlation matrix, components kept by
//      parallel analysis, varimax-rotated. A fault seen on EVERY swing at a steady size never
//      varies with anything, so it can never form a theme however big it is — which is why layer 1
//      is not redundant with this one.
//
// Beside them, WHAT YOU DO WELL — the faults this golfer is reliably clear of. Judged BY VALUE
// against each reading's own corridor (floor v ≥ lo, ceiling v ≤ hi, two-sided lo ≤ v ≤ hi; a row
// whose corridor shape is unknown is not counted at all), never by the fired flag, so a condition
// that does not fire only because it could not be graded is never praised. Only faults read by
// alternative signals (detection Any / First — an All conjunction has no one reading that is the
// condition) and common enough to matter (prominence Occasional and up); at least two judged
// sessions and thirty judged swings; a recency-weighted in-band share of 0.85 with the latest
// session at 0.8. And never about a quantity the other two layers talk about: a condition whose
// FAMILY (its signals' first measures' metricKey roots, themeFamilyRoot) is a needs-work fault's
// is not praised (the reduction), nor one that a member of a theme the page SHOWS moves (the
// view, swingSummaryView) — "your head stays centred" beside "your head sways further going
// back" would be the page contradicting itself.
//
// The rules the reduction is built on:
//
//   1. CO-MOVEMENT, NEVER CAUSE. A theme says "on swings where A, B" and no more. Nothing here reads
//      the pack's causal edges, and the wording refuses because / cause / due to / leads to. Which
//      of two co-moving faults drives the other is a question swing-to-swing variation cannot
//      answer; declared-focus and drill sessions are what will.
//   2. PER GOLFER. Every theme is found in this golfer's own swings, centred per session (median,
//      pooled MAD) so camera, club and day-to-day drift cannot make a theme. Nothing is pooled
//      across golfers and nothing authored is assumed.
//   3. TOLD ONLY AS FIRMLY AS IT HOLDS. Each theme is refitted on 500 within-session bootstrap
//      resamples and once with each session left out, matched back by the Hungarian method on
//      Tucker congruence. "Firm" holds on every check, "Probably" and "Possibly" are said so, and
//      anything weaker is not shown. Below about 60 swings over 3 sessions layer 2 says "not yet".
//   4. EVERY NUMBER IS INJECTED (ThemeOptions), the result is stamped with kThemeRuleVersion, and
//      the random draws are mt19937_64 from one seed per replicate (DetRng), so a replicate gives
//      the same answer on any thread, in any order, on any platform — and the same answer as the
//      Python reference, which the golden test (swing_themes_golden_test.cpp) holds it to.
//
// YOUR FOCUS (rule v1) is ONE thing to work on, drawn from the needs-work faults (every one, not
// just the five the card shows). Faults that share a drill — the first of a condition's `drills`
// the shipped registry holds — are one group, because one practice covers them; a fault with none
// is a group of its own. The group that starts EARLIEST in the swing leads (a fault at the top
// shapes everything after it, never the other way round), then one with a drill to practise, then
// the one covering most faults, then the most frequent. Everything else the golfer needs to work
// on is "next on your list", in the same order. The order is stored (SwingThemes::focusOrder);
// the words are the view's.
//
// Like the work-ons, the result is a PURE FUNCTION OF THE LEDGERS: re-analysing or deleting a
// session re-derives it, nothing accumulates. The pack is never seen here — the caller marshals
// orientation, swing position and phrases (src/Diagnostics/swing_themes_pack.h).
//
// The linear algebra is hand-written and small (tens of measures): a Householder + implicit-QL
// symmetric eigensolver, a one-sided Jacobi SVD for varimax's k×k polar factor, Gauss–Jordan for
// the k×k score weights, the Kuhn–Munkres assignment. The EM imputation dominates the cost (one
// eigen-decomposition per iteration, up to 200 iterations, per bootstrap fit), so its loop works in
// preallocated buffers and the replicates run through an injected parallel-for.
//
// Unit-tested standalone in src/Analysis/tests/swing_themes_test.cpp; held to the Python reference
// on real ledgers in src/Analysis/tests/swing_themes_golden_test.cpp.

#include "det_rng.h"
#include "diagnostic_ledger.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

// The persisted shape, and the rule that filled it — two numbers for the reason work_ons.h gives.
// 2: "what you do well" (doWell), each needs-work row's per-session pips, every shot and session.
// Rule 3: the focus groups (focusOrder) — a file without them is re-derived, not read.
inline constexpr int kThemeSchemaVersion = 2;
inline constexpr int kThemeRuleVersion   = 3;

// One seed per replicate, from a base and a stream, so replicates are independent of each other
// and of the order they run in. Unsigned arithmetic wraps, which is the "mod 2^64" the Python
// reference spells out.
inline constexpr uint64_t kThemeSeedBase    = 20261009ULL;
inline constexpr uint64_t kThemeStreamPA    = 1;
inline constexpr uint64_t kThemeStreamBoot  = 2;

inline uint64_t themeSeedFor(uint64_t base, uint64_t stream, uint64_t index)
{
    return (base * 1000003ULL + stream) * 1000003ULL + index;
}

// Every number the reduction and the wording use. The defaults are the design's.
struct ThemeOptions {
    // ── The table ──
    double minColumnCoverage = 0.6;     // a measure read on fewer swings than this is not a column
    double minRowCoverage    = 0.5;     // a swing reading fewer of those columns is a capture hole
    double madScale          = 1.4826;  // MAD -> sigma
    double winsor            = 4.0;     // robust z clip, so one mis-tracked swing cannot make a theme
    double trendClip         = 12.0;    // the uncentred projection's clip (3 × winsor)

    // ── Imputation, correlation, retention ──
    int    emIters    = 200;
    double emTol      = 1e-6;
    int    minPairs   = 10;             // pairwise-complete correlations need this many swings
    uint64_t seed     = kThemeSeedBase;
    int    paShuffles = 500;
    double paPercentile = 95.0;

    // ── Rotation ──
    double varimaxGamma = 1.0;
    int    varimaxIters = 500;
    double varimaxTol   = 1e-8;

    // ── Stability and the tiers (design table) ──
    int    boot               = 500;
    double bootPercentile     = 5.0;
    double firmBootP05        = 0.84;
    double firmLosoMin        = 0.85;
    double probablyBootMedian = 0.75;
    double probablyLosoMedian = 0.9;
    double possiblyBootMedian = 0.7;
    double possiblyLosoMedian = 0.85;

    // ── Description ──
    double loadMin    = 0.4;            // a member loads at least this much
    double clusterCut = 0.7;            // UPGMA merges while the mean 1−|ρ| is at most this
    double trendSlope = 0.1;            // session-score slope that reads as a change
    // ...and only when it stands this many standard errors clear of zero, the error taken from the
    // sessions' own scatter about the line. Session means carry camera, club and day drift far
    // larger than their sampling error (±0.8 within-SD on Mark's five sessions), so a slope gate
    // alone reads that drift as a trend; this asks whether the sessions LINE UP. Under three
    // sessions there is no scatter to judge by, and no trend.
    double trendMinT  = 2.0;

    // ── Minimum data for layer 2 ──
    int minSwings   = 60;
    int minSessions = 3;

    // ── Layer 1 ──
    int    minAssessed       = 8;       // a session counts for a fault once it judged this many
    double presentShare      = 0.5;     // ...and the latest such session saw it on this share
    double recency           = 0.75;    // each older session weighs this much less
    double seenTrendDelta    = 0.15;
    int    seenTrendMinJudged = 3;

    // ── What you do well (rule v1). Sessions are judged on minAssessed rows and weighted by
    //    recency, as layer 1's are. ──
    int    wellMinProminence = 2;       // Occasional (Rare 0 .. Ubiquitous 4) and up
    int    wellMinSessions   = 2;       // judged sessions ...
    int    wellMinSwings     = 30;      // ... and judged swings over them
    double wellShare         = 0.85;    // recency-weighted in-band share
    double wellSession       = 0.8;     // the latest judged session's in-band share; each session's pip

    // ── Wording ──
    int    maxSeenMost   = 5;           // also the "needs work" card
    int    maxDoWell     = 3;
    int    maxNext       = 4;           // "next on your list", after the focus
    int    maxTogether   = 4;
    int    maxThemeParts = 2;
    double everySwing    = 0.995;       // "In the ideal range on every swing"
    double almostEvery   = 0.9;
    double mostSwings    = 0.7;
};

// What the reduction needs to know about one measure, marshalled from the pack by the caller.
//   sign      +1 / −1 so that + means MORE OF THE FAULT once multiplied in
//   twoSided  neither tail is the fault; the raw sign was kept (reported, never used to gate)
//   when      the latest swing position the measure reads, 1..10 (P1..P10); 0 = unknown
struct ThemeMeasureInfo {
    QString id;
    double  sign     = 1.0;
    bool    twoSided = true;
    int     when     = 0;
    QString metricKey;
};

// What "what we see most" and "what you do well" need to know about one condition, marshalled
// from the pack by the caller (themeConditionInfo in swing_themes_pack.h).
//   fault       pack kind Fault: the only conditions either list may name
//   detection   kThemeDetectionAny / All / First (an All conjunction is never praised)
//   prominence  Rare 0, Uncommon 1, Occasional 2, Common 3, Ubiquitous 4
//   families    the metricKey root (or the measure id when it has none) of the FIRST measure of
//               each detectedBy signal, each once — the quantities the condition is about
//   drill       the first of the condition's `drills` the shipped drill registry holds, "" = none
//   when        the EARLIEST swing position (1..10) any of those first measures is read at
//               (ThemeMeasureInfo::when; 0s ignored), 0 = none placed
// A metricKey's FAMILY ROOT: the key with a trailing "Signed" removed. The metric catalogue
// (src/Metrics/metric_catalogue_manifest.cpp) describes pelvisRotationSigned as "the same physical
// quantity as Pelvis rotation", carrying only the sign the magnitude throws away — so a fault read
// on one and praise read on the other ("your hips keep turning through impact" beside "your hips
// haven't turned enough by impact") are about one thing. Every family here goes through this:
// conditions' families, theme members' families, and a theme sentence's family de-dup.
inline QString themeFamilyRoot(const QString &metricKey)
{
    static const QString signedSuffix = QStringLiteral("Signed");
    return metricKey.endsWith(signedSuffix) ? metricKey.chopped(signedSuffix.size()) : metricKey;
}

inline constexpr int kThemeDetectionAny   = 0;
inline constexpr int kThemeDetectionAll   = 1;
inline constexpr int kThemeDetectionFirst = 2;

struct ThemeConditionInfo {
    QString     id;
    bool        fault      = false;
    int         detection  = kThemeDetectionAny;
    int         prominence = 0;
    QStringList families;
    QString     drill;
    int         when       = 0;
};

struct ThemeSessionInput {
    QString                 name;       // the session directory name; sessions are taken in name order
    std::vector<ShotRecord> shots;      // ledger order
};

// Runs body(0..n−1), in any order and on any threads. Empty = serial. Each replicate writes only
// its own slot, so the result does not depend on how this schedules them.
using ThemeParallelFor = std::function<void(int n, const std::function<void(int)> &body)>;

enum class ThemeTier { None, Possibly, Probably, Firm };

inline QString themeTierToString(ThemeTier t)
{
    switch (t) {
    case ThemeTier::Firm:     return QStringLiteral("firm");
    case ThemeTier::Probably: return QStringLiteral("probably");
    case ThemeTier::Possibly: return QStringLiteral("possibly");
    case ThemeTier::None:     break;
    }
    return QString();
}

inline ThemeTier themeTierFromString(const QString &s)
{
    if (s == QStringLiteral("firm"))     return ThemeTier::Firm;
    if (s == QStringLiteral("probably")) return ThemeTier::Probably;
    if (s == QStringLiteral("possibly")) return ThemeTier::Possibly;
    return ThemeTier::None;
}

struct ThemeMember {
    QString measureId;
    double  loading = 0.0;      // rotated, on the oriented scale
    bool    rawHigh = true;     // the theme pushes the RAW reading up (picks golferHigh)
    int     when    = 0;
};

struct SwingTheme {
    int    index         = 0;   // 1-based, in variance order
    double varianceShare = 0.0;
    std::vector<ThemeMember> members;   // by −|loading|
    int    startsAt      = 0;   // earliest member position, 0 = none known
    ThemeTier tier       = ThemeTier::None;

    double bootMedian = 0.0, bootP05 = 0.0;
    double losoMin    = 0.0, losoMedian = 0.0;
    std::vector<double> losoPerSession;     // kept sessions, in order
    // Recorded, never gating: the theme against a zero-fill fit, and against the clustering.
    double emVsZeroFill   = 0.0;
    double clusterJaccard = 0.0;

    std::vector<double> sessionScores;      // kept sessions, in within-session SDs
    double slope = 0.0;                     // per session
    double slopeSe = 0.0;                   // from the sessions' scatter about the line
    int    trend = 0;                       // +1 may be growing, −1 may be easing, 0 no clear change
};

struct SeenMostRow {
    QString conditionId;
    double  share          = 0.0;   // recency-weighted firing share over the judged sessions
    int     sessionsSeen   = 0;
    int     sessionsJudged = 0;
    QString trend;                  // "easing" / "growing" / "steady", or "" under 3 judged sessions
    std::vector<bool> pips;         // per judged session, in order: fired on presentShare or more
};

// One fault this golfer is reliably clear of ("what you do well"), clear of every needs-work
// family; the view still drops it when a theme it shows touches `families`, then caps.
struct DoWellRow {
    QString conditionId;
    double  share          = 0.0;   // recency-weighted in-band share over the judged sessions
    int     prominence     = 0;
    int     sessionsJudged = 0;
    int     swingsJudged   = 0;     // in-band-counted rows over the judged sessions
    std::vector<bool> pips;         // per judged session, in order: in band on wellSession or more
    QStringList families;           // the condition's (ThemeConditionInfo::families)
};

// One group of needs-work faults the focus is chosen from (rule v1): a drill's faults together,
// or one fault with no drill alone.
//   key           the drill id, or "solo:" + the condition id
//   drill         the drill id, "" for a solo group
//   when          the earliest swing position of its faults (1..10), 0 = none placed (ranked
//                 after P10)
//   conditionIds  by share (descending), then id
struct FocusGroup {
    QString     key;
    QString     drill;
    int         when = 0;
    QStringList conditionIds;
};

// A group with no known swing position ranks after every placed one.
inline constexpr int kThemeFocusUnplaced = 11;

struct SwingThemes {
    bool enough    = false;     // layer 2 had the minimum data and ran to the end
    bool cancelled = false;     // stopped by the caller; nothing in layer 2 is meaningful

    int allSwings   = 0;        // every shot in the ledgers
    int allSessions = 0;        // sessions holding at least one shot
    int swings   = 0;           // after the sparse-swing drop
    int sessions = 0;           // sessions with at least one kept swing
    QStringList sessionNames;   // every input session, in name order
    QStringList keptSessionNames;   // the sessions above with a kept swing (per-session vectors)
    std::vector<std::pair<QString, int>> dropped;   // (session, shotId) of sparse swings

    QStringList measures;       // kept columns, in column order
    int k = 0;
    std::vector<double> eig;            // observed, descending, full length
    std::vector<double> paThreshold;    // parallel-analysis percentile, full length
    std::vector<double> varianceShare;  // per theme
    std::vector<double> loadings;       // measures.size() × k, row-major
    std::vector<int>    clusterLabels;  // per kept measure, 1-based

    std::vector<SwingTheme>  themes;
    std::vector<SeenMostRow> seenMost;
    std::vector<DoWellRow>   doWell;    // rule v1 after the needs-work exclusion, ranked, uncapped
    std::vector<FocusGroup>  focusOrder;    // every needs-work fault, grouped and ranked (focus rule v1)
};

// ════════════════════════════════════════════════════════════════════════════════════════════════
// The machinery, in its own namespace so its small names cannot collide with the rest of
// pinpoint::analysis (one namespace, one definition — an ODR collision there is silent).
// Public because the tests and the golden test pin each step on its own.
// ════════════════════════════════════════════════════════════════════════════════════════════════
namespace themes {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline bool cancelled(const std::atomic<bool> *c) { return c && c->load(std::memory_order_relaxed); }

// A NaN-aware matrix, row-major.
struct Matrix {
    int rows = 0, cols = 0;
    std::vector<double> v;

    Matrix() = default;
    Matrix(int r, int c, double fill = 0.0) : rows(r), cols(c), v(size_t(r) * size_t(c), fill) {}
    double       &at(int i, int j)       { return v[size_t(i) * size_t(cols) + size_t(j)]; }
    double        at(int i, int j) const { return v[size_t(i) * size_t(cols) + size_t(j)]; }
    double       *row(int i)             { return v.data() + size_t(i) * size_t(cols); }
    const double *row(int i) const       { return v.data() + size_t(i) * size_t(cols); }
};

// ── Order statistics (numpy's conventions) ───────────────────────────────────────────────────────

// numpy.median over the finite values; NaN when there are none. Even counts average the middle two.
inline double nanMedian(std::vector<double> x)
{
    x.erase(std::remove_if(x.begin(), x.end(), [](double d) { return !std::isfinite(d); }), x.end());
    if (x.empty()) return kNaN;
    std::sort(x.begin(), x.end());
    const size_t n = x.size();
    return n % 2 ? x[n / 2] : (x[n / 2 - 1] + x[n / 2]) / 2.0;
}

// numpy.median / numpy.min: NaN as soon as any value is NaN (numpy propagates it).
inline double medianPropagating(std::vector<double> x)
{
    if (x.empty()) return kNaN;
    for (double d : x) if (std::isnan(d)) return kNaN;
    std::sort(x.begin(), x.end());
    const size_t n = x.size();
    return n % 2 ? x[n / 2] : (x[n / 2 - 1] + x[n / 2]) / 2.0;
}

inline double minPropagating(const std::vector<double> &x)
{
    if (x.empty()) return kNaN;
    double m = x[0];
    for (double d : x) { if (std::isnan(d)) return kNaN; m = std::min(m, d); }
    return m;
}

// numpy.percentile(x, q), method 'linear': virtual index (n−1)·q/100, and numpy's _lerp, which
// interpolates from the far end when the fraction is at least a half. NaN propagates.
inline double percentileLinear(std::vector<double> x, double q)
{
    if (x.empty()) return kNaN;
    for (double d : x) if (std::isnan(d)) return kNaN;
    std::sort(x.begin(), x.end());
    const int n = int(x.size());
    const double vi = double(n - 1) * (q / 100.0);
    if (vi >= double(n - 1)) return x[size_t(n - 1)];
    if (vi < 0.0) return x[0];
    const double lo = std::floor(vi);
    const double g  = vi - lo;
    const double a  = x[size_t(lo)], b = x[size_t(lo) + 1];
    const double d  = b - a;
    return g >= 0.5 ? b - d * (1.0 - g) : a + d * g;
}

// Average ranks, 1-based; ties share the mean rank (scipy.stats.rankdata 'average').
inline std::vector<double> averageRanks(const std::vector<double> &x)
{
    const int n = int(x.size());
    std::vector<int> o(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) o[size_t(i)] = i;
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return x[size_t(a)] < x[size_t(b)]; });
    std::vector<double> r(static_cast<size_t>(n));
    int i = 0;
    while (i < n) {
        int j = i;
        while (j + 1 < n && x[size_t(o[size_t(j + 1)])] == x[size_t(o[size_t(i)])]) ++j;
        const double rank = double(i + j) / 2.0 + 1.0;
        for (int t = i; t <= j; ++t) r[size_t(o[size_t(t)])] = rank;
        i = j + 1;
    }
    return r;
}

// ── Symmetric eigen-decomposition ────────────────────────────────────────────────────────────────
//
// Householder tridiagonalisation then implicit QL (the EISPACK tred2/tql2 pair, as JAMA publishes
// it). Chosen over cyclic Jacobi for speed and nothing else: both are accurate to ~1e-13 here, but
// at 46 measures this is ~7× faster at -O2 and ~12× at -O0, and the EM loop runs one per iteration
// — about a hundred thousand per reduction.

struct EigenWork {
    std::vector<double> d, e, vs;
    std::vector<int>    order;
};

// `a` (n×n, symmetric, row-major) is replaced by the eigenvectors IN COLUMNS; `w` receives the
// eigenvalues. Both sorted DESCENDING. A non-finite input yields NaN throughout.
inline void symmetricEigen(int n, double *a, double *w, EigenWork &ws)
{
    if (n <= 0) return;
    for (int i = 0; i < n * n; ++i) {
        if (!std::isfinite(a[i])) {
            for (int j = 0; j < n * n; ++j) a[j] = kNaN;
            for (int j = 0; j < n; ++j) w[j] = kNaN;
            return;
        }
    }
    ws.d.resize(static_cast<size_t>(n));
    ws.e.resize(static_cast<size_t>(n));
    double *d = ws.d.data(), *e = ws.e.data();
    double *V = a;
#define PP_THEME_V(i, j) V[(i) * n + (j)]

    // tred2 — Householder reduction to tridiagonal form, accumulating the transform.
    for (int j = 0; j < n; ++j) d[j] = PP_THEME_V(n - 1, j);
    for (int i = n - 1; i > 0; --i) {
        double scale = 0.0, h = 0.0;
        for (int k = 0; k < i; ++k) scale += std::fabs(d[k]);
        if (scale == 0.0) {
            e[i] = d[i - 1];
            for (int j = 0; j < i; ++j) {
                d[j] = PP_THEME_V(i - 1, j);
                PP_THEME_V(i, j) = 0.0;
                PP_THEME_V(j, i) = 0.0;
            }
        } else {
            for (int k = 0; k < i; ++k) { d[k] /= scale; h += d[k] * d[k]; }
            double f = d[i - 1];
            double g = std::sqrt(h);
            if (f > 0) g = -g;
            e[i] = scale * g;
            h -= f * g;
            d[i - 1] = f - g;
            for (int j = 0; j < i; ++j) e[j] = 0.0;
            for (int j = 0; j < i; ++j) {
                f = d[j];
                PP_THEME_V(j, i) = f;
                g = e[j] + PP_THEME_V(j, j) * f;
                for (int k = j + 1; k <= i - 1; ++k) {
                    g += PP_THEME_V(k, j) * d[k];
                    e[k] += PP_THEME_V(k, j) * f;
                }
                e[j] = g;
            }
            f = 0.0;
            for (int j = 0; j < i; ++j) { e[j] /= h; f += e[j] * d[j]; }
            const double hh = f / (h + h);
            for (int j = 0; j < i; ++j) e[j] -= hh * d[j];
            for (int j = 0; j < i; ++j) {
                f = d[j];
                g = e[j];
                for (int k = j; k <= i - 1; ++k) PP_THEME_V(k, j) -= (f * e[k] + g * d[k]);
                d[j] = PP_THEME_V(i - 1, j);
                PP_THEME_V(i, j) = 0.0;
            }
        }
        d[i] = h;
    }
    for (int i = 0; i < n - 1; ++i) {
        PP_THEME_V(n - 1, i) = PP_THEME_V(i, i);
        PP_THEME_V(i, i) = 1.0;
        const double h = d[i + 1];
        if (h != 0.0) {
            for (int k = 0; k <= i; ++k) d[k] = PP_THEME_V(k, i + 1) / h;
            for (int j = 0; j <= i; ++j) {
                double g = 0.0;
                for (int k = 0; k <= i; ++k) g += PP_THEME_V(k, i + 1) * PP_THEME_V(k, j);
                for (int k = 0; k <= i; ++k) PP_THEME_V(k, j) -= g * d[k];
            }
        }
        for (int k = 0; k <= i; ++k) PP_THEME_V(k, i + 1) = 0.0;
    }
    for (int j = 0; j < n; ++j) { d[j] = PP_THEME_V(n - 1, j); PP_THEME_V(n - 1, j) = 0.0; }
    PP_THEME_V(n - 1, n - 1) = 1.0;
    e[0] = 0.0;

    // tql2 — implicit QL on the tridiagonal, rotating the accumulated transform.
    for (int i = 1; i < n; ++i) e[i - 1] = e[i];
    e[n - 1] = 0.0;
    double f = 0.0, tst1 = 0.0;
    const double eps = std::ldexp(1.0, -52);
    for (int l = 0; l < n; ++l) {
        tst1 = std::max(tst1, std::fabs(d[l]) + std::fabs(e[l]));
        int m = l;
        while (m < n - 1) {
            if (std::fabs(e[m]) <= eps * tst1) break;
            ++m;
        }
        if (m > l) {
            int guard = 0;
            do {
                double g = d[l];
                double p = (d[l + 1] - g) / (2.0 * e[l]);
                double r = std::hypot(p, 1.0);
                if (p < 0) r = -r;
                d[l]     = e[l] / (p + r);
                d[l + 1] = e[l] * (p + r);
                const double dl1 = d[l + 1];
                double h = g - d[l];
                for (int i = l + 2; i < n; ++i) d[i] -= h;
                f += h;
                p = d[m];
                double c = 1.0, c2 = c, c3 = c;
                const double el1 = e[l + 1];
                double s = 0.0, s2 = 0.0;
                for (int i = m - 1; i >= l; --i) {
                    c3 = c2;
                    c2 = c;
                    s2 = s;
                    g = c * e[i];
                    h = c * p;
                    r = std::hypot(p, e[i]);
                    e[i + 1] = s * r;
                    s = e[i] / r;
                    c = p / r;
                    p = c * d[i] - s * g;
                    d[i + 1] = h + s * (c * g + s * d[i]);
                    for (int k = 0; k < n; ++k) {
                        double *row = V + k * n;
                        h = row[i + 1];
                        row[i + 1] = s * row[i] + c * h;
                        row[i]     = c * row[i] - s * h;
                    }
                }
                p = -s * s2 * c3 * el1 * e[l] / dl1;
                e[l] = s * p;
                d[l] = c * p;
            } while (std::fabs(e[l]) > eps * tst1 && ++guard < 64);
        }
        d[l] = d[l] + f;
        e[l] = 0.0;
    }
#undef PP_THEME_V

    // Descending, the vectors following their values.
    ws.order.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) ws.order[size_t(i)] = i;
    std::stable_sort(ws.order.begin(), ws.order.end(), [&](int x, int y) { return d[x] > d[y]; });
    ws.vs.assign(a, a + size_t(n) * size_t(n));
    for (int j = 0; j < n; ++j) {
        const int src = ws.order[size_t(j)];
        w[j] = d[src];
        for (int i = 0; i < n; ++i) a[i * n + j] = ws.vs[size_t(i) * size_t(n) + size_t(src)];
    }
}

// Convenience: eigenvalues (descending) and vectors (columns) of a symmetric p×p matrix.
inline void symmetricEigen(const Matrix &A, std::vector<double> &w, Matrix &V)
{
    EigenWork ws;
    V = A;
    w.assign(size_t(A.rows), 0.0);
    symmetricEigen(A.rows, V.v.data(), w.data(), ws);
}

// ── Small dense helpers ──────────────────────────────────────────────────────────────────────────

// The polar factor U·Vᵀ of a k×k matrix and the sum of its singular values — what varimax takes
// from numpy.linalg.svd. One-sided (Hestenes) Jacobi on the columns: accurate for any conditioning,
// and k is the number of themes, so cost is nothing. `g` is k×k row-major.
inline void polarFactor(int k, const std::vector<double> &g, std::vector<double> &R, double &sumS)
{
    std::vector<double> U = g;                       // rotated in place into Û·Σ
    std::vector<double> V(size_t(k) * size_t(k), 0.0);
    for (int i = 0; i < k; ++i) V[size_t(i) * size_t(k) + size_t(i)] = 1.0;
    auto u = [&](int r, int c) -> double & { return U[size_t(r) * size_t(k) + size_t(c)]; };
    auto v = [&](int r, int c) -> double & { return V[size_t(r) * size_t(k) + size_t(c)]; };

    for (int sweep = 0; sweep < 80; ++sweep) {
        bool rotated = false;
        for (int i = 0; i < k - 1; ++i) {
            for (int j = i + 1; j < k; ++j) {
                double alpha = 0.0, beta = 0.0, gamma = 0.0;
                for (int r = 0; r < k; ++r) {
                    alpha += u(r, i) * u(r, i);
                    beta  += u(r, j) * u(r, j);
                    gamma += u(r, i) * u(r, j);
                }
                if (gamma == 0.0 || std::fabs(gamma) <= 1e-15 * std::sqrt(alpha * beta)) continue;
                rotated = true;
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double t = (zeta >= 0 ? 1.0 : -1.0) / (std::fabs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = c * t;
                for (int r = 0; r < k; ++r) {
                    const double ui = u(r, i), uj = u(r, j);
                    u(r, i) = c * ui - s * uj;
                    u(r, j) = s * ui + c * uj;
                    const double vi = v(r, i), vj = v(r, j);
                    v(r, i) = c * vi - s * vj;
                    v(r, j) = s * vi + c * vj;
                }
            }
        }
        if (!rotated) break;
    }

    // Singular values are the column norms; Û is the normalised columns.
    sumS = 0.0;
    std::vector<bool> zero(size_t(k), false);
    for (int j = 0; j < k; ++j) {
        double s2 = 0.0;
        for (int r = 0; r < k; ++r) s2 += u(r, j) * u(r, j);
        const double s = std::sqrt(s2);
        sumS += s;
        if (s > 0.0) for (int r = 0; r < k; ++r) u(r, j) /= s;
        else         zero[size_t(j)] = true;
    }
    // A rank-deficient G leaves columns of Û undetermined; complete them to an orthonormal basis
    // so R stays a rotation (any completion is a valid polar factor).
    for (int j = 0; j < k; ++j) {
        if (!zero[size_t(j)]) continue;
        for (int cand = 0; cand < k; ++cand) {
            std::vector<double> x(size_t(k), 0.0);
            x[size_t(cand)] = 1.0;
            for (int o = 0; o < k; ++o) {
                if (o == j || zero[size_t(o)]) continue;
                double dot = 0.0;
                for (int r = 0; r < k; ++r) dot += u(r, o) * x[size_t(r)];
                for (int r = 0; r < k; ++r) x[size_t(r)] -= dot * u(r, o);
            }
            double nn = 0.0;
            for (double t : x) nn += t * t;
            nn = std::sqrt(nn);
            if (nn > 0.5) {
                for (int r = 0; r < k; ++r) u(r, j) = x[size_t(r)] / nn;
                zero[size_t(j)] = false;
                break;
            }
        }
    }
    R.assign(size_t(k) * size_t(k), 0.0);
    for (int a = 0; a < k; ++a)
        for (int b = 0; b < k; ++b) {
            double s = 0.0;
            for (int j = 0; j < k; ++j) s += u(a, j) * v(b, j);
            R[size_t(a) * size_t(k) + size_t(b)] = s;
        }
}

// Inverse of a k×k matrix (row-major) by Gauss–Jordan with partial pivoting. False when singular.
inline bool invertSmall(int k, std::vector<double> a, std::vector<double> &inv)
{
    inv.assign(size_t(k) * size_t(k), 0.0);
    for (int i = 0; i < k; ++i) inv[size_t(i) * size_t(k) + size_t(i)] = 1.0;
    auto A = [&](int r, int c) -> double & { return a[size_t(r) * size_t(k) + size_t(c)]; };
    auto I = [&](int r, int c) -> double & { return inv[size_t(r) * size_t(k) + size_t(c)]; };
    for (int col = 0; col < k; ++col) {
        int piv = col;
        for (int r = col + 1; r < k; ++r) if (std::fabs(A(r, col)) > std::fabs(A(piv, col))) piv = r;
        if (!(std::fabs(A(piv, col)) > 0.0)) return false;
        if (piv != col)
            for (int c = 0; c < k; ++c) { std::swap(A(piv, c), A(col, c)); std::swap(I(piv, c), I(col, c)); }
        const double p = A(col, col);
        for (int c = 0; c < k; ++c) { A(col, c) /= p; I(col, c) /= p; }
        for (int r = 0; r < k; ++r) {
            if (r == col) continue;
            const double f = A(r, col);
            if (f == 0.0) continue;
            for (int c = 0; c < k; ++c) { A(r, c) -= f * A(col, c); I(r, c) -= f * I(col, c); }
        }
    }
    return true;
}

// Maximum-weight assignment of rows to columns (Kuhn–Munkres, the potentials formulation),
// rectangular allowed. A line-for-line port of theme_pca.hungarian_max, ties included: the result
// lists (row, col) in column order. `a` is n0×m0 row-major.
inline std::vector<std::pair<int, int>> hungarianMax(const std::vector<double> &a, int n0, int m0)
{
    const int n = std::max(n0, m0);
    std::vector<double> cost(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n0; ++i)
        for (int j = 0; j < m0; ++j) cost[size_t(i) * size_t(n) + size_t(j)] = -a[size_t(i) * size_t(m0) + size_t(j)];
    const double INF = 1e18;
    std::vector<double> u(size_t(n) + 1, 0.0), v(size_t(n) + 1, 0.0), minv(size_t(n) + 1);
    std::vector<int> p(size_t(n) + 1, 0), way(size_t(n) + 1, 0);
    std::vector<char> used(size_t(n) + 1);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::fill(minv.begin(), minv.end(), INF);
        std::fill(used.begin(), used.end(), char(0));
        while (true) {
            used[size_t(j0)] = 1;
            const int i0 = p[size_t(j0)];
            double delta = INF;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[size_t(j)]) continue;
                const double cur = cost[size_t(i0 - 1) * size_t(n) + size_t(j - 1)] - u[size_t(i0)] - v[size_t(j)];
                if (cur < minv[size_t(j)]) { minv[size_t(j)] = cur; way[size_t(j)] = j0; }
                if (minv[size_t(j)] < delta) { delta = minv[size_t(j)]; j1 = j; }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[size_t(j)]) { u[size_t(p[size_t(j)])] += delta; v[size_t(j)] -= delta; }
                else                 minv[size_t(j)] -= delta;
            }
            j0 = j1;
            if (p[size_t(j0)] == 0) break;
        }
        while (j0) {
            const int j1 = way[size_t(j0)];
            p[size_t(j0)] = p[size_t(j1)];
            j0 = j1;
        }
    }
    std::vector<std::pair<int, int>> out;
    for (int j = 1; j <= n; ++j)
        if (p[size_t(j)] - 1 < n0 && j - 1 < m0) out.push_back({ p[size_t(j)] - 1, j - 1 });
    return out;
}

// Tucker congruence of column a of A and column b of B (both p rows).
inline double congruence(const Matrix &A, int a, const Matrix &B, int b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (int i = 0; i < A.rows; ++i) {
        const double x = A.at(i, a), y = B.at(i, b);
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    return ab / std::sqrt(aa * bb);
}

// Best assignment of L's columns to Lref's; |congruence| per Lref column, NaN where unassigned.
inline std::vector<double> matchLoadings(const Matrix &Lref, const Matrix &L)
{
    const int k1 = Lref.cols, k2 = L.cols;
    std::vector<double> A(size_t(k1) * size_t(k2));
    for (int i = 0; i < k1; ++i)
        for (int j = 0; j < k2; ++j) A[size_t(i) * size_t(k2) + size_t(j)] = std::fabs(congruence(Lref, i, L, j));
    std::vector<double> out(size_t(k1), kNaN);
    for (const auto &ij : hungarianMax(A, k1, k2))
        out[size_t(ij.first)] = A[size_t(ij.first) * size_t(k2) + size_t(ij.second)];
    return out;
}

// numpy.nan_to_num: NaN -> 0, ±inf -> ±DBL_MAX.
inline double nanToNum(double x)
{
    if (std::isnan(x)) return 0.0;
    if (std::isinf(x)) return x > 0 ? std::numeric_limits<double>::max() : -std::numeric_limits<double>::max();
    return x;
}

// ── Correlations ─────────────────────────────────────────────────────────────────────────────────

// numpy.corrcoef(F, rowvar=False) then nan_to_num: complete data, n−1 covariance, divided by each
// SD in turn, clipped to ±1. A constant column gives 0 throughout, its diagonal included.
inline Matrix corrComplete(const Matrix &F)
{
    const int n = F.rows, p = F.cols;
    std::vector<double> avg(size_t(p), 0.0);
    for (int i = 0; i < n; ++i) {
        const double *r = F.row(i);
        for (int j = 0; j < p; ++j) avg[size_t(j)] += r[j];
    }
    for (int j = 0; j < p; ++j) avg[size_t(j)] /= double(n);
    Matrix X(n, p);
    for (int i = 0; i < n; ++i) {
        const double *r = F.row(i);
        double *x = X.row(i);
        for (int j = 0; j < p; ++j) x[j] = r[j] - avg[size_t(j)];
    }
    Matrix C(p, p, 0.0);
    for (int i = 0; i < n; ++i) {
        const double *x = X.row(i);
        for (int a = 0; a < p; ++a) {
            const double xa = x[a];
            double *c = C.row(a);
            for (int b = a; b < p; ++b) c[b] += xa * x[b];
        }
    }
    const double fact = 1.0 / double(n - 1);
    for (int a = 0; a < p; ++a)
        for (int b = a; b < p; ++b) { C.at(a, b) *= fact; C.at(b, a) = C.at(a, b); }
    std::vector<double> sd(static_cast<size_t>(p));
    for (int j = 0; j < p; ++j) sd[size_t(j)] = std::sqrt(C.at(j, j));
    for (int a = 0; a < p; ++a)
        for (int b = 0; b < p; ++b) {
            double c = C.at(a, b) / sd[size_t(a)];
            c /= sd[size_t(b)];
            if (c > 1.0) c = 1.0;
            else if (c < -1.0) c = -1.0;
            C.at(a, b) = nanToNum(c);
        }
    return C;
}

// Pearson over the swings where both measures were read — no imputation — exactly theme_pca's
// n / sx / sxx / sxy formula. Under `minPairs` shared swings the pair is 0; the diagonal is 1.
inline Matrix corrPairwise(const Matrix &Z, int minPairs)
{
    const int n = Z.rows, p = Z.cols;
    std::vector<double> N(size_t(p) * size_t(p), 0.0), SX(N), SXX(N), SXY(N);
    std::vector<double> x(static_cast<size_t>(p)), m(static_cast<size_t>(p));
    for (int r = 0; r < n; ++r) {
        const double *z = Z.row(r);
        for (int j = 0; j < p; ++j) {
            const bool ok = std::isfinite(z[j]);
            m[size_t(j)] = ok ? 1.0 : 0.0;
            x[size_t(j)] = ok ? z[j] : 0.0;
        }
        for (int i = 0; i < p; ++i) {
            const double xi = x[size_t(i)], mi = m[size_t(i)], xx = xi * xi;
            double *nr = N.data() + size_t(i) * size_t(p), *sxr = SX.data() + size_t(i) * size_t(p);
            double *sxxr = SXX.data() + size_t(i) * size_t(p), *sxyr = SXY.data() + size_t(i) * size_t(p);
            const double *mm = m.data(), *xv = x.data();
            for (int j = 0; j < p; ++j) {
                nr[j]   += mi * mm[j];
                sxr[j]  += xi * mm[j];
                sxxr[j] += xx * mm[j];
                sxyr[j] += xi * xv[j];
            }
        }
    }
    Matrix C(p, p, 0.0);
    for (int i = 0; i < p; ++i)
        for (int j = 0; j < p; ++j) {
            const size_t ij = size_t(i) * size_t(p) + size_t(j), ji = size_t(j) * size_t(p) + size_t(i);
            const double nij = N[ij];
            const double num = nij * SXY[ij] - SX[ij] * SX[ji];
            const double den = std::sqrt(std::max(nij * SXX[ij] - SX[ij] * SX[ij], 0.0)
                                         * std::max(nij * SXX[ji] - SX[ji] * SX[ji], 0.0));
            double c = num / den;
            if (nij < double(minPairs)) c = 0.0;
            C.at(i, j) = nanToNum(c);
        }
    for (int i = 0; i < p; ++i) C.at(i, i) = 1.0;
    return C;
}

// Spearman ρ over pairwise-complete swings (average ranks), as theme_pca.spearman_pairwise.
inline Matrix spearmanPairwise(const Matrix &Z, int minPairs)
{
    const int n = Z.rows, p = Z.cols;
    Matrix R(p, p, 0.0);
    for (int i = 0; i < p; ++i) R.at(i, i) = 1.0;
    std::vector<double> a, b;
    for (int i = 0; i < p; ++i)
        for (int j = i + 1; j < p; ++j) {
            a.clear();
            b.clear();
            for (int r = 0; r < n; ++r)
                if (std::isfinite(Z.at(r, i)) && std::isfinite(Z.at(r, j))) { a.push_back(Z.at(r, i)); b.push_back(Z.at(r, j)); }
            if (int(a.size()) < minPairs) continue;
            const std::vector<double> ra = averageRanks(a), rb = averageRanks(b);
            const int m = int(ra.size());
            double ma = 0.0, mb = 0.0;
            for (int t = 0; t < m; ++t) { ma += ra[size_t(t)]; mb += rb[size_t(t)]; }
            ma /= double(m);
            mb /= double(m);
            double sab = 0.0, saa = 0.0, sbb = 0.0;
            for (int t = 0; t < m; ++t) {
                const double da = ra[size_t(t)] - ma, db = rb[size_t(t)] - mb;
                sab += da * db;
                saa += da * da;
                sbb += db * db;
            }
            const double f = 1.0 / double(m - 1);
            double c = (sab * f) / std::sqrt(saa * f);
            c /= std::sqrt(sbb * f);
            c = std::clamp(c, -1.0, 1.0);
            R.at(i, j) = R.at(j, i) = nanToNum(c);
        }
    return R;
}

// UPGMA on a distance matrix, merging while the closest pair of groups (mean distance, first pair
// on ties) is at most `cut`. Labels are 1-based, in final-group order.
inline std::vector<int> averageLinkageCut(const Matrix &D, double cut)
{
    std::vector<std::vector<int>> groups;
    for (int i = 0; i < D.rows; ++i) groups.push_back({ i });
    while (groups.size() > 1) {
        double best = std::numeric_limits<double>::infinity();
        int bi = -1, bj = -1;
        for (int a = 0; a < int(groups.size()); ++a)
            for (int b = a + 1; b < int(groups.size()); ++b) {
                double s = 0.0;
                for (int x : groups[size_t(a)]) for (int y : groups[size_t(b)]) s += D.at(x, y);
                const double d = s / double(groups[size_t(a)].size() * groups[size_t(b)].size());
                if (d < best) { best = d; bi = a; bj = b; }
            }
        if (bi < 0 || best > cut) break;
        groups[size_t(bi)].insert(groups[size_t(bi)].end(), groups[size_t(bj)].begin(), groups[size_t(bj)].end());
        groups.erase(groups.begin() + bj);
    }
    std::vector<int> lab(size_t(D.rows), 0);
    for (size_t g = 0; g < groups.size(); ++g)
        for (int m : groups[g]) lab[size_t(m)] = int(g) + 1;
    return lab;
}

// ── Imputation and the fit ───────────────────────────────────────────────────────────────────────

// EM-PCA: fill each missing cell from a rank-k reconstruction of the column-standardised matrix,
// re-fit, until no fill moves by `emTol`, or `emIters` passes. The reconstruction S·V_k·V_kᵀ (V_k
// the top-k eigenvectors of SᵀS) is the truncated SVD's U_k·s_k·V_kᵀ without the SVD. Nothing is
// warm-started: every pass is the same map the reference iterates, so both reach the same point.
inline Matrix emImpute(const Matrix &Z, int k, const ThemeOptions &opt)
{
    const int n = Z.rows, p = Z.cols;
    Matrix F(n, p);
    std::vector<int> missCell;                          // flat index, row-major
    std::vector<int> rowSlot(size_t(n), -1), missRows;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < p; ++j) {
            const double z = Z.at(i, j);
            if (std::isfinite(z)) { F.at(i, j) = z; continue; }
            F.at(i, j) = 0.0;
            missCell.push_back(i * p + j);
            if (rowSlot[size_t(i)] < 0) { rowSlot[size_t(i)] = int(missRows.size()); missRows.push_back(i); }
        }
    if (missCell.empty() || n == 0 || p == 0) return F;
    k = std::clamp(k, 1, p);

    std::vector<double> mu(static_cast<size_t>(p)), sd(static_cast<size_t>(p)), S(size_t(n) * size_t(p)), C(size_t(p) * size_t(p)),
                        w(static_cast<size_t>(p)), T(missRows.size() * size_t(k));
    EigenWork ws;
    double *f = F.v.data();
    double *s = S.data(), *c = C.data(), *t = T.data(), *m = mu.data(), *sdv = sd.data();
    const double invN = double(n);

    for (int it = 0; it < opt.emIters; ++it) {
        // Column mean and population SD, rows summed in order as numpy's axis-0 reduction does.
        for (int j = 0; j < p; ++j) { m[j] = 0.0; sdv[j] = 0.0; }
        for (int i = 0; i < n; ++i) { const double *fr = f + i * p; for (int j = 0; j < p; ++j) m[j] += fr[j]; }
        for (int j = 0; j < p; ++j) m[j] /= invN;
        for (int i = 0; i < n; ++i) {
            const double *fr = f + i * p;
            for (int j = 0; j < p; ++j) { const double d = fr[j] - m[j]; sdv[j] += d * d; }
        }
        for (int j = 0; j < p; ++j) { sdv[j] = std::sqrt(sdv[j] / invN); if (sdv[j] == 0.0) sdv[j] = 1.0; }
        for (int i = 0; i < n; ++i) {
            const double *fr = f + i * p;
            double *sr = s + i * p;
            for (int j = 0; j < p; ++j) sr[j] = (fr[j] - m[j]) / sdv[j];
        }

        // SᵀS once, upper triangle then mirrored.
        for (int q = 0; q < p * p; ++q) c[q] = 0.0;
        for (int i = 0; i < n; ++i) {
            const double *sr = s + i * p;
            for (int a = 0; a < p; ++a) {
                const double sa = sr[a];
                if (sa == 0.0) continue;
                double *cr = c + a * p;
                for (int b = a; b < p; ++b) cr[b] += sa * sr[b];
            }
        }
        for (int a = 0; a < p; ++a) for (int b = 0; b < a; ++b) c[a * p + b] = c[b * p + a];
        symmetricEigen(p, c, w.data(), ws);             // c now holds the eigenvectors, by column

        // Reconstruct only where something is missing: T = S·V_k on those rows, then T·V_kᵀ.
        for (size_t r = 0; r < missRows.size(); ++r) {
            const double *sr = s + missRows[r] * p;
            double *tr = t + r * size_t(k);
            for (int l = 0; l < k; ++l) tr[l] = 0.0;
            for (int j = 0; j < p; ++j) {
                const double sj = sr[j];
                const double *vr = c + j * p;
                for (int l = 0; l < k; ++l) tr[l] += sj * vr[l];
            }
        }
        double change = 0.0;
        for (int cell : missCell) {
            const int i = cell / p, j = cell % p;
            const double *tr = t + size_t(rowSlot[size_t(i)]) * size_t(k);
            const double *vr = c + j * p;
            double r = 0.0;
            for (int l = 0; l < k; ++l) r += tr[l] * vr[l];
            const double val = r * sdv[j] + m[j];
            change = std::max(change, std::fabs(val - f[cell]));
            f[cell] = val;
        }
        if (change < opt.emTol) break;
    }
    return F;
}

// Kaiser-normalised varimax — theme_pca.varimax's loop exactly: R is updated BEFORE the
// convergence test, so the last rotation found is the one applied.
inline Matrix varimax(const Matrix &L, const ThemeOptions &opt)
{
    const int p = L.rows, k = L.cols;
    std::vector<double> h(static_cast<size_t>(p));
    Matrix A(p, k);
    for (int i = 0; i < p; ++i) {
        double s = 0.0;
        for (int j = 0; j < k; ++j) s += L.at(i, j) * L.at(i, j);
        h[size_t(i)] = std::sqrt(s);
        if (h[size_t(i)] == 0.0) h[size_t(i)] = 1.0;
        for (int j = 0; j < k; ++j) A.at(i, j) = L.at(i, j) / h[size_t(i)];
    }
    std::vector<double> R(size_t(k) * size_t(k), 0.0), G(size_t(k) * size_t(k)), colSq(static_cast<size_t>(k)), Rn;
    for (int j = 0; j < k; ++j) R[size_t(j) * size_t(k) + size_t(j)] = 1.0;
    Matrix B(p, k), T(p, k);
    double d = 0.0;
    for (int it = 0; it < opt.varimaxIters; ++it) {
        for (int i = 0; i < p; ++i)
            for (int j = 0; j < k; ++j) {
                double s = 0.0;
                for (int l = 0; l < k; ++l) s += A.at(i, l) * R[size_t(l) * size_t(k) + size_t(j)];
                B.at(i, j) = s;
            }
        for (int j = 0; j < k; ++j) {
            double s = 0.0;
            for (int i = 0; i < p; ++i) s += B.at(i, j) * B.at(i, j);
            colSq[size_t(j)] = s;
        }
        const double gp = opt.varimaxGamma / double(p);
        for (int i = 0; i < p; ++i)
            for (int j = 0; j < k; ++j) {
                const double b = B.at(i, j);
                T.at(i, j) = b * b * b - (gp * b) * colSq[size_t(j)];
            }
        for (int a = 0; a < k; ++a)
            for (int b = 0; b < k; ++b) {
                double s = 0.0;
                for (int i = 0; i < p; ++i) s += A.at(i, a) * T.at(i, b);
                G[size_t(a) * size_t(k) + size_t(b)] = s;
            }
        double dNew = 0.0;
        polarFactor(k, G, Rn, dNew);
        R = Rn;
        if (dNew < d * (1.0 + opt.varimaxTol)) break;
        d = dNew;
    }
    Matrix out(p, k);
    for (int i = 0; i < p; ++i)
        for (int j = 0; j < k; ++j) {
            double s = 0.0;
            for (int l = 0; l < k; ++l) s += A.at(i, l) * R[size_t(l) * size_t(k) + size_t(j)];
            out.at(i, j) = s * h[size_t(i)];
        }
    return out;
}

struct Fit {
    int k = 0;
    Matrix L;                       // p × k, rotated, signed, ordered by variance
    std::vector<double> eig;        // of corr(F), descending
    std::vector<double> varshare;   // per column, ΣL² / p
    Matrix F;                       // the completed matrix
};

// theme_pca.fit at a fixed k: impute (EM or zero), eigen-decompose corr(F), keep k loadings,
// varimax when k > 1, sign each column so its largest |loading| (first on ties) is positive, and
// order the columns by ΣL² descending.
inline Fit fitThemes(const Matrix &Z, int k, bool em, const ThemeOptions &opt)
{
    Fit out;
    const int p = Z.cols;
    k = std::clamp(k, 1, std::max(1, p));
    out.k = k;
    if (em) {
        out.F = emImpute(Z, k, opt);
    } else {
        out.F = Z;
        for (double &x : out.F.v) if (!std::isfinite(x)) x = 0.0;
    }
    Matrix V;
    symmetricEigen(corrComplete(out.F), out.eig, V);
    Matrix L(p, k);
    for (int j = 0; j < k; ++j) {
        const double sc = std::sqrt(std::max(out.eig[size_t(j)], 0.0));
        for (int i = 0; i < p; ++i) L.at(i, j) = V.at(i, j) * sc;
    }
    if (k > 1) L = varimax(L, opt);
    for (int j = 0; j < k; ++j) {
        int arg = 0;
        for (int i = 1; i < p; ++i) if (std::fabs(L.at(i, j)) > std::fabs(L.at(arg, j))) arg = i;
        if (L.at(arg, j) < 0) for (int i = 0; i < p; ++i) L.at(i, j) = -L.at(i, j);
    }
    std::vector<double> ss(size_t(k), 0.0);
    for (int j = 0; j < k; ++j) for (int i = 0; i < p; ++i) ss[size_t(j)] += L.at(i, j) * L.at(i, j);
    // numpy's argsort(ss)[::-1]: descending, and on a tie the LATER column first.
    std::vector<int> o(static_cast<size_t>(k));
    for (int j = 0; j < k; ++j) o[size_t(j)] = j;
    std::sort(o.begin(), o.end(), [&](int a, int b) {
        if (ss[size_t(a)] != ss[size_t(b)]) return ss[size_t(a)] > ss[size_t(b)];
        return a > b;
    });
    out.L = Matrix(p, k);
    out.varshare.resize(static_cast<size_t>(k));
    for (int j = 0; j < k; ++j) {
        const int src = o[size_t(j)];
        for (int i = 0; i < p; ++i) out.L.at(i, j) = L.at(i, src);
        out.varshare[size_t(j)] = ss[size_t(src)] / double(p);
    }
    return out;
}

// Runs body over 0..n−1 through the injected parallel-for (serial when empty), skipping work once
// cancelled. Every replicate writes only its own slot.
inline void runReplicates(const ThemeParallelFor &pf, int n, const std::function<void(int)> &body,
                          const std::atomic<bool> *cancel)
{
    if (n <= 0) return;
    const std::function<void(int)> guarded = [&](int i) { if (!cancelled(cancel)) body(i); };
    if (pf) { pf(n, guarded); return; }
    for (int i = 0; i < n && !cancelled(cancel); ++i) body(i);
}

// Fisher–Yates with one DetRng: i from n−1 down to 1, j = below(i+1).
inline void fisherYates(std::vector<double> &a, DetRng &rng)
{
    for (int i = int(a.size()) - 1; i >= 1; --i) {
        const int j = int(rng.below(uint64_t(i) + 1));
        std::swap(a[size_t(i)], a[size_t(j)]);
    }
}

struct ParallelAnalysis {
    int k = 1;
    std::vector<double> eig;        // observed, pairwise-complete, descending
    std::vector<double> threshold;  // percentile of the shuffled eigenvalues, per index
};

// Parallel analysis on PAIRWISE-COMPLETE correlations, so imputed cells cannot lend structure to
// the eigenvalues that decide k. Shuffle i permutes a copy of each column in turn (NaNs travel)
// with one generator seeded for (STREAM_PA, i).
inline ParallelAnalysis parallelAnalysis(const Matrix &Z, const ThemeOptions &opt,
                                         const ThemeParallelFor &pf = {}, const std::atomic<bool> *cancel = nullptr)
{
    ParallelAnalysis out;
    const int n = Z.rows, p = Z.cols, reps = std::max(0, opt.paShuffles);
    Matrix V;
    symmetricEigen(corrPairwise(Z, opt.minPairs), out.eig, V);
    std::vector<double> sims(size_t(reps) * size_t(p), kNaN);
    runReplicates(pf, reps, [&](int r) {
        DetRng rng(themeSeedFor(opt.seed, kThemeStreamPA, uint64_t(r)));
        Matrix P(n, p);
        std::vector<double> col(static_cast<size_t>(n));
        for (int j = 0; j < p; ++j) {
            for (int i = 0; i < n; ++i) col[size_t(i)] = Z.at(i, j);
            fisherYates(col, rng);
            for (int i = 0; i < n; ++i) P.at(i, j) = col[size_t(i)];
        }
        Matrix C = corrPairwise(P, opt.minPairs);
        EigenWork ws;
        symmetricEigen(p, C.v.data(), sims.data() + size_t(r) * size_t(p), ws);
    }, cancel);
    out.threshold.assign(size_t(p), kNaN);
    std::vector<double> colv(static_cast<size_t>(reps));
    for (int j = 0; j < p; ++j) {
        for (int r = 0; r < reps; ++r) colv[size_t(r)] = sims[size_t(r) * size_t(p) + size_t(j)];
        out.threshold[size_t(j)] = percentileLinear(colv, opt.paPercentile);
    }
    int k = 0;
    while (k < p && out.eig[size_t(k)] > out.threshold[size_t(k)]) ++k;
    out.k = std::max(k, 1);
    return out;
}

// ── The swing × measure table ────────────────────────────────────────────────────────────────────

struct Table {
    Matrix values;                  // swings × measures, NaN where not read
    std::vector<int> session;       // session index (into sessionNames) per swing
    std::vector<int> shotId;
    QStringList sessionNames;
    QStringList measures;           // sorted
};

// One row per shot, sessions in the order given; one column per measure any row read. A row the
// capture could not assess contributes nothing; otherwise every reading behind it (rowReadings),
// the first finite value per measure winning.
inline Table buildTable(const std::vector<const ThemeSessionInput *> &sessions)
{
    Table t;
    std::vector<QHash<QString, double>> cells;
    QSet<QString> seen;
    for (int si = 0; si < int(sessions.size()); ++si) {
        const ThemeSessionInput &s = *sessions[size_t(si)];
        t.sessionNames.append(s.name);
        for (const ShotRecord &shot : s.shots) {
            QHash<QString, double> rec;
            for (const ConditionRow &row : shot.rows) {
                if (row.state == ShotState::NotAssessable) continue;
                for (const MeasureRow &m : rowReadings(row)) {
                    if (m.measureId.isEmpty() || !std::isfinite(m.value) || rec.contains(m.measureId)) continue;
                    rec.insert(m.measureId, m.value);
                    seen.insert(m.measureId);
                }
            }
            cells.push_back(std::move(rec));
            t.session.push_back(si);
            t.shotId.push_back(shot.shotId);
        }
    }
    t.measures = QStringList(seen.begin(), seen.end());
    std::sort(t.measures.begin(), t.measures.end());
    QHash<QString, int> col;
    for (int j = 0; j < t.measures.size(); ++j) col.insert(t.measures[j], j);
    t.values = Matrix(int(cells.size()), int(t.measures.size()), kNaN);
    for (int i = 0; i < int(cells.size()); ++i)
        for (auto it = cells[size_t(i)].cbegin(); it != cells[size_t(i)].cend(); ++it)
            t.values.at(i, col.value(it.key())) = it.value();
    return t;
}

// Drop the swings that read under minRowCoverage of the well-covered measures (coverage over ALL
// swings). One such swing in 2026-10-05 alone made the first component when kept.
inline Table dropSparseSwings(const Table &t, const ThemeOptions &opt, std::vector<std::pair<QString, int>> *dropped = nullptr)
{
    const int n = t.values.rows, m = t.values.cols;
    std::vector<int> wellCovered;
    for (int j = 0; j < m && n > 0; ++j) {
        int c = 0;
        for (int i = 0; i < n; ++i) c += std::isfinite(t.values.at(i, j)) ? 1 : 0;
        if (double(c) / double(n) >= opt.minColumnCoverage) wellCovered.push_back(j);
    }
    Table out;
    out.sessionNames = t.sessionNames;
    out.measures     = t.measures;
    std::vector<int> keep;
    for (int i = 0; i < n; ++i) {
        int c = 0;
        for (int j : wellCovered) c += std::isfinite(t.values.at(i, j)) ? 1 : 0;
        // No well-covered column at all is numpy's mean of nothing: NaN, which keeps nothing.
        const bool ok = !wellCovered.empty() && double(c) / double(wellCovered.size()) >= opt.minRowCoverage;
        if (ok) keep.push_back(i);
        else if (dropped) dropped->push_back({ t.sessionNames[t.session[size_t(i)]], t.shotId[size_t(i)] });
    }
    out.values = Matrix(int(keep.size()), m);
    for (int r = 0; r < int(keep.size()); ++r) {
        const int i = keep[size_t(r)];
        for (int j = 0; j < m; ++j) out.values.at(r, j) = t.values.at(i, j);
        out.session.push_back(t.session[size_t(i)]);
        out.shotId.push_back(t.shotId[size_t(i)]);
    }
    return out;
}

struct Prepared {
    Matrix Z;                       // oriented, session-centred, robust-scaled, winsorised; kept cols
    QStringList measures;           // kept columns
    std::vector<double> sign;       // per kept column
    Matrix Xo;                      // oriented raw values, kept cols (for the uncentred trend)
    std::vector<double> spread;     // pooled within-session spread, kept cols
    std::vector<std::vector<int>> rowsOf;   // per PRESENT session, its rows ascending
    std::vector<int> presentSessions;       // session indices with a kept swing, ascending
};

// theme_pca.prepare: orient, centre each session on its median, scale by the pooled MAD×1.4826
// (≤ 0 is NaN), keep the columns read on enough swings that still vary, winsorise.
inline Prepared prepare(const Table &t, const QHash<QString, ThemeMeasureInfo> &info, const ThemeOptions &opt)
{
    Prepared out;
    const int n = t.values.rows, m = t.values.cols;
    Matrix X = t.values;
    std::vector<double> sign(static_cast<size_t>(m));
    for (int j = 0; j < m; ++j) sign[size_t(j)] = info.value(t.measures[j]).sign;
    for (int i = 0; i < n; ++i) for (int j = 0; j < m; ++j) X.at(i, j) *= sign[size_t(j)];

    for (int i = 0; i < n; ++i) {
        const int s = t.session[size_t(i)];
        if (std::find(out.presentSessions.begin(), out.presentSessions.end(), s) == out.presentSessions.end())
            out.presentSessions.push_back(s);
    }
    std::sort(out.presentSessions.begin(), out.presentSessions.end());
    out.rowsOf.resize(out.presentSessions.size());
    for (int i = 0; i < n; ++i) {
        const auto it = std::find(out.presentSessions.begin(), out.presentSessions.end(), t.session[size_t(i)]);
        out.rowsOf[size_t(it - out.presentSessions.begin())].push_back(i);
    }

    // Centre per session.
    Matrix dev(n, m, kNaN);
    std::vector<double> buf;
    for (const std::vector<int> &rows : out.rowsOf)
        for (int j = 0; j < m; ++j) {
            buf.clear();
            for (int i : rows) buf.push_back(X.at(i, j));
            const double c = nanMedian(buf);
            for (int i : rows) dev.at(i, j) = X.at(i, j) - c;
        }
    std::vector<double> spread(static_cast<size_t>(m));
    for (int j = 0; j < m; ++j) {
        buf.clear();
        for (int i = 0; i < n; ++i) buf.push_back(std::fabs(dev.at(i, j)));
        spread[size_t(j)] = opt.madScale * nanMedian(buf);
    }

    std::vector<int> keep;
    for (int j = 0; j < m; ++j) {
        int cov = 0;
        for (int i = 0; i < n; ++i) cov += std::isfinite(X.at(i, j)) ? 1 : 0;
        const double sp = spread[size_t(j)] > 0 ? spread[size_t(j)] : kNaN;
        // nanstd of the scaled column: finite and above zero.
        double sum = 0.0;
        int cnt = 0;
        for (int i = 0; i < n; ++i) { const double z = dev.at(i, j) / sp; if (std::isfinite(z)) { sum += z; ++cnt; } }
        double sd = kNaN;
        if (cnt > 0) {
            const double mean = sum / double(cnt);
            double ss = 0.0;
            for (int i = 0; i < n; ++i) { const double z = dev.at(i, j) / sp; if (std::isfinite(z)) ss += (z - mean) * (z - mean); }
            sd = std::sqrt(ss / double(cnt));
        }
        if (n > 0 && double(cov) / double(n) >= opt.minColumnCoverage && std::isfinite(sd) && sd > 0) keep.push_back(j);
    }

    const int p = int(keep.size());
    out.Z = Matrix(n, p);
    out.Xo = Matrix(n, p);
    for (int c = 0; c < p; ++c) {
        const int j = keep[size_t(c)];
        out.measures.append(t.measures[j]);
        out.sign.push_back(sign[size_t(j)]);
        out.spread.push_back(spread[size_t(j)]);
        for (int i = 0; i < n; ++i) {
            double z = dev.at(i, j) / spread[size_t(j)];
            if (z > opt.winsor) z = opt.winsor;
            else if (z < -opt.winsor) z = -opt.winsor;
            out.Z.at(i, c) = z;
            out.Xo.at(i, c) = X.at(i, j);
        }
    }
    return out;
}

// ── Layer 1 ──────────────────────────────────────────────────────────────────────────────────────

// theme_summary.whats_off: per session per condition fired / assessed; only `fault` conditions; a
// session judges a fault on minAssessed swings; present when the latest judged session saw it on
// presentShare; ranked by the recency-weighted share (stable — ties keep first-seen order).
inline std::vector<SeenMostRow> seenMost(const std::vector<const ThemeSessionInput *> &sessions,
                                         const QHash<QString, ThemeConditionInfo> &conditions, const ThemeOptions &opt)
{
    struct Judged { int fired = 0, assessed = 0; };
    std::vector<QString> order;
    QHash<QString, std::vector<Judged>> per;
    for (const ThemeSessionInput *s : sessions) {
        std::vector<QString> sOrder;
        QHash<QString, Judged> acc;
        for (const ShotRecord &shot : s->shots)
            for (const ConditionRow &r : shot.rows) {
                if (r.state == ShotState::NotAssessable) continue;
                if (!acc.contains(r.conditionId)) sOrder.push_back(r.conditionId);
                Judged &j = acc[r.conditionId];
                ++j.assessed;
                if (r.state == ShotState::Fired) ++j.fired;
            }
        for (const QString &id : sOrder) {
            if (!per.contains(id)) order.push_back(id);
            per[id].push_back(acc.value(id));
        }
    }

    std::vector<SeenMostRow> out;
    for (const QString &id : order) {
        if (!conditions.value(id).fault) continue;
        std::vector<Judged> judged;
        std::vector<double> rate;
        for (const Judged &j : per.value(id))
            if (j.assessed >= opt.minAssessed) {
                judged.push_back(j);
                rate.push_back(double(j.fired) / double(j.assessed));
            }
        if (rate.empty() || rate.back() < opt.presentShare) continue;
        const int len = int(rate.size());
        double num = 0.0, den = 0.0;
        for (int i = 0; i < len; ++i) {
            // (w·fired)/assessed, in the reference's order, so a share on a wording boundary
            // lands on the same side of it.
            const double w = std::pow(opt.recency, double(len - 1 - i));
            num += w * double(judged[size_t(i)].fired) / double(judged[size_t(i)].assessed);
            den += w;
        }
        SeenMostRow row;
        row.conditionId    = id;
        row.share          = num / den;
        row.sessionsJudged = len;
        for (double r : rate) {
            if (r >= opt.presentShare) ++row.sessionsSeen;
            row.pips.push_back(r >= opt.presentShare);
        }
        if (len >= opt.seenTrendMinJudged) {
            double early = 0.0;
            for (int i = 0; i < len - 2; ++i) early += rate[size_t(i)];
            early /= double(len - 2);
            const double late = (rate[size_t(len - 2)] + rate[size_t(len - 1)]) / 2.0;
            row.trend = late < early - opt.seenTrendDelta ? QStringLiteral("easing")
                      : late > early + opt.seenTrendDelta ? QStringLiteral("growing")
                      :                                     QStringLiteral("steady");
        }
        out.push_back(std::move(row));
    }
    std::stable_sort(out.begin(), out.end(), [](const SeenMostRow &a, const SeenMostRow &b) { return a.share > b.share; });
    return out;
}

// ── What you do well ─────────────────────────────────────────────────────────────────────────────

// theme_summary.row_in_band: 1 when every reading behind the row (rowReadings) is inside its own
// corridor BY VALUE, 0 when one is outside, −1 when the row is not counted at all — not
// assessable, no readings, or a reading with a non-finite value or a corridor shape that is not
// floor / ceiling / two-sided (None, Unknown). The fired flag is never read.
inline int rowInBand(const ConditionRow &r)
{
    if (r.state == ShotState::NotAssessable) return -1;
    const std::vector<MeasureRow> rs = rowReadings(r);
    if (rs.empty()) return -1;
    bool inside = true;
    for (const MeasureRow &m : rs) {
        if (!std::isfinite(m.value)) return -1;
        switch (m.corridorShape) {
        case CorridorShape::Floor:    inside = inside && m.value >= m.corridorLo; break;
        case CorridorShape::Ceiling:  inside = inside && m.value <= m.corridorHi; break;
        case CorridorShape::TwoSided: inside = inside && m.corridorLo <= m.value && m.value <= m.corridorHi; break;
        case CorridorShape::None:
        case CorridorShape::Unknown:  return -1;
        }
    }
    return inside ? 1 : 0;
}

// theme_summary.do_well, rule v1. Per session per condition: good = rows in band, counted = rows
// counted. Candidates are faults, detection Any / First, prominence wellMinProminence and up; a
// session is judged on minAssessed counted rows; wellMinSessions judged sessions and wellMinSwings
// rows over them; kept when the recency-weighted in-band share reaches wellShare and the latest
// judged session's reaches wellSession, and the condition's families touch no needs-work
// condition's (every row of `seen`, not just the five the view shows). Ranked by prominence,
// share, judged swings (descending), then id. Uncapped. The themes are not read here: which of
// them touch a row depends on which the page SHOWS, so that exclusion is swingSummaryView's.
inline std::vector<DoWellRow> doWell(const std::vector<const ThemeSessionInput *> &sessions,
                                     const QHash<QString, ThemeConditionInfo> &conditions,
                                     const std::vector<SeenMostRow> &seen, const ThemeOptions &opt)
{
    QSet<QString> banned;
    for (const SeenMostRow &r : seen)
        for (const QString &f : conditions.value(r.conditionId).families) banned.insert(f);

    struct Counted { int good = 0, counted = 0; };
    std::vector<QString> order;
    QHash<QString, std::vector<Counted>> per;
    for (const ThemeSessionInput *s : sessions) {
        std::vector<QString> sOrder;
        QHash<QString, Counted> acc;
        for (const ShotRecord &shot : s->shots)
            for (const ConditionRow &r : shot.rows) {
                const int in = rowInBand(r);
                if (in < 0) continue;
                if (!acc.contains(r.conditionId)) sOrder.push_back(r.conditionId);
                Counted &c = acc[r.conditionId];
                ++c.counted;
                c.good += in;
            }
        for (const QString &id : sOrder) {
            if (!per.contains(id)) order.push_back(id);
            per[id].push_back(acc.value(id));
        }
    }

    std::vector<DoWellRow> out;
    for (const QString &id : order) {
        const auto ci = conditions.constFind(id);
        if (ci == conditions.constEnd() || !ci->fault || ci->detection == kThemeDetectionAll
            || ci->prominence < opt.wellMinProminence)
            continue;
        std::vector<Counted> judged;
        int swings = 0;
        for (const Counted &c : per.value(id))
            if (c.counted >= opt.minAssessed) { judged.push_back(c); swings += c.counted; }
        if (int(judged.size()) < opt.wellMinSessions || swings < opt.wellMinSwings) continue;
        const int len = int(judged.size());
        double num = 0.0, den = 0.0;
        for (int i = 0; i < len; ++i) {
            // (w·good)/counted, in the reference's order, as layer 1 does.
            const double w = std::pow(opt.recency, double(len - 1 - i));
            num += w * double(judged[size_t(i)].good) / double(judged[size_t(i)].counted);
            den += w;
        }
        const double share = num / den;
        const double latest = double(judged.back().good) / double(judged.back().counted);
        if (share < opt.wellShare || latest < opt.wellSession) continue;
        bool touches = false;
        for (const QString &f : ci->families) touches = touches || banned.contains(f);
        if (touches) continue;
        DoWellRow row;
        row.conditionId    = id;
        row.share          = share;
        row.prominence     = ci->prominence;
        row.sessionsJudged = len;
        row.swingsJudged   = swings;
        for (const Counted &c : judged) row.pips.push_back(double(c.good) / double(c.counted) >= opt.wellSession);
        row.families       = ci->families;
        out.push_back(std::move(row));
    }
    std::sort(out.begin(), out.end(), [](const DoWellRow &a, const DoWellRow &b) {
        if (a.prominence != b.prominence)     return a.prominence > b.prominence;
        if (a.share != b.share)               return a.share > b.share;
        if (a.swingsJudged != b.swingsJudged) return a.swingsJudged > b.swingsJudged;
        return a.conditionId < b.conditionId;
    });
    return out;
}

// ── Your focus ───────────────────────────────────────────────────────────────────────────────────

// theme_summary.focus_order, rule v1. Every needs-work fault (all of `seen`) joins the group of
// its drill (key = the drill id) or a group of its own ("solo:" + id). A group's `when` is the
// earliest placed position of its faults (0 = none). Groups ranked: when ascending (0 as
// kThemeFocusUnplaced), a drill before none, more faults, the higher top share, then key; within a
// group, share descending, then id.
inline std::vector<FocusGroup> focusOrder(const std::vector<SeenMostRow> &seen,
                                          const QHash<QString, ThemeConditionInfo> &conditions)
{
    struct Fault { QString id; double share; int when; };
    struct Group { FocusGroup g; std::vector<Fault> faults; double maxShare = 0.0; };
    std::vector<Group> groups;
    for (const SeenMostRow &r : seen) {
        const ThemeConditionInfo ci = conditions.value(r.conditionId);
        const QString key = ci.drill.isEmpty() ? QStringLiteral("solo:") + r.conditionId : ci.drill;
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group &x) { return x.g.key == key; });
        if (it == groups.end()) {
            groups.push_back(Group{});
            it = groups.end() - 1;
            it->g.key   = key;
            it->g.drill = ci.drill;
            it->maxShare = r.share;
        }
        it->faults.push_back({ r.conditionId, r.share, ci.when });
        it->maxShare = std::max(it->maxShare, r.share);
    }
    for (Group &x : groups) {
        std::sort(x.faults.begin(), x.faults.end(), [](const Fault &a, const Fault &b) {
            if (a.share != b.share) return a.share > b.share;
            return a.id < b.id;
        });
        for (const Fault &f : x.faults) {
            x.g.conditionIds.append(f.id);
            if (f.when > 0 && (x.g.when == 0 || f.when < x.g.when)) x.g.when = f.when;
        }
    }
    const auto placed = [](int when) { return when > 0 ? when : kThemeFocusUnplaced; };
    std::sort(groups.begin(), groups.end(), [&](const Group &a, const Group &b) {
        if (placed(a.g.when) != placed(b.g.when))          return placed(a.g.when) < placed(b.g.when);
        if (a.g.drill.isEmpty() != b.g.drill.isEmpty())    return !a.g.drill.isEmpty();
        if (a.faults.size() != b.faults.size())            return a.faults.size() > b.faults.size();
        if (a.maxShare != b.maxShare)                      return a.maxShare > b.maxShare;
        return a.g.key < b.g.key;
    });
    std::vector<FocusGroup> out;
    for (Group &x : groups) out.push_back(std::move(x.g));
    return out;
}

inline ThemeTier tierOf(double bootMedian, double bootP05, double losoMin, double losoMedian, const ThemeOptions &opt)
{
    if (bootP05 >= opt.firmBootP05 && losoMin >= opt.firmLosoMin) return ThemeTier::Firm;
    if (bootMedian >= opt.probablyBootMedian && losoMedian >= opt.probablyLosoMedian) return ThemeTier::Probably;
    if (bootMedian >= opt.possiblyBootMedian && losoMedian >= opt.possiblyLosoMedian) return ThemeTier::Possibly;
    return ThemeTier::None;
}

inline std::vector<const ThemeSessionInput *> sortedSessions(const std::vector<ThemeSessionInput> &sessions)
{
    std::vector<const ThemeSessionInput *> out;
    for (const ThemeSessionInput &s : sessions) out.push_back(&s);
    std::stable_sort(out.begin(), out.end(), [](const ThemeSessionInput *a, const ThemeSessionInput *b) { return a->name < b->name; });
    return out;
}

} // namespace themes

// ════════════════════════════════════════════════════════════════════════════════════════════════
// The reduction
// ════════════════════════════════════════════════════════════════════════════════════════════════

namespace themes {

// The two layers; reduceSwingThemes() below adds "what you do well", which reads both.
inline SwingThemes reduceLayers(const std::vector<const ThemeSessionInput *> &sorted,
                                const QHash<QString, ThemeMeasureInfo> &measures,
                                const QHash<QString, ThemeConditionInfo> &conditions,
                                const ThemeOptions &opt, const ThemeParallelFor &pf,
                                const std::atomic<bool> *cancel)
{
    SwingThemes out;
    for (const ThemeSessionInput *s : sorted) out.sessionNames.append(s->name);

    // Layer 1, from every shot of every session.
    out.seenMost = themes::seenMost(sorted, conditions, opt);

    // Layer 2.
    const Table t = dropSparseSwings(buildTable(sorted), opt, &out.dropped);
    const Prepared pr = prepare(t, measures, opt);
    out.swings   = t.values.rows;
    out.sessions = int(pr.presentSessions.size());
    for (int s : pr.presentSessions) out.keptSessionNames.append(t.sessionNames[s]);
    const auto stop = [&]() {
        out.enough = false;
        out.cancelled = true;
        out.k = 0;
        out.eig.clear(); out.paThreshold.clear(); out.varianceShare.clear(); out.loadings.clear();
        out.clusterLabels.clear(); out.themes.clear();
        return out;
    };
    if (cancelled(cancel)) return stop();
    if (out.swings < opt.minSwings || out.sessions < opt.minSessions) return out;
    out.enough   = true;
    out.measures = pr.measures;
    const Matrix &Z = pr.Z;
    const int p = Z.cols, S = out.sessions;
    if (p < 2) return out;          // nothing can move together with fewer than two measures

    // Retention, then the fit everything else is measured against.
    const ParallelAnalysis pa = parallelAnalysis(Z, opt, pf, cancel);
    if (cancelled(cancel)) return stop();
    const int k = pa.k;
    out.k = k;
    out.eig = pa.eig;
    out.paThreshold = pa.threshold;
    const Fit main = fitThemes(Z, k, true, opt);
    const Matrix &L = main.L;
    out.varianceShare = main.varshare;
    out.loadings = L.v;
    if (cancelled(cancel)) return stop();

    // Recorded only: zero-fill instead of EM.
    const std::vector<double> emVsZero = matchLoadings(L, fitThemes(Z, k, false, opt).L);

    // Bootstrap, swings resampled within each session; one seed per replicate.
    const int B = std::max(0, opt.boot);
    std::vector<double> boot(size_t(B) * size_t(k), kNaN);
    runReplicates(pf, B, [&](int b) {
        DetRng rng(themeSeedFor(opt.seed, kThemeStreamBoot, uint64_t(b)));
        std::vector<int> idx;
        idx.reserve(size_t(Z.rows));
        for (const std::vector<int> &g : pr.rowsOf)
            for (size_t r = 0; r < g.size(); ++r) idx.push_back(g[size_t(rng.below(g.size()))]);
        Matrix Zb(int(idx.size()), p);
        for (int r = 0; r < int(idx.size()); ++r)
            std::copy(Z.row(idx[size_t(r)]), Z.row(idx[size_t(r)]) + p, Zb.row(r));
        const std::vector<double> c = matchLoadings(L, fitThemes(Zb, k, true, opt).L);
        std::copy(c.begin(), c.end(), boot.begin() + std::ptrdiff_t(size_t(b) * size_t(k)));
    }, cancel);
    if (cancelled(cancel)) return stop();

    // Leave one session out.
    std::vector<double> loso(size_t(S) * size_t(k), kNaN);
    runReplicates(pf, S, [&](int s) {
        std::vector<int> rows;
        for (int o = 0; o < S; ++o) if (o != s) rows.insert(rows.end(), pr.rowsOf[size_t(o)].begin(), pr.rowsOf[size_t(o)].end());
        std::sort(rows.begin(), rows.end());
        Matrix Zl(int(rows.size()), p);
        for (int r = 0; r < int(rows.size()); ++r)
            std::copy(Z.row(rows[size_t(r)]), Z.row(rows[size_t(r)]) + p, Zl.row(r));
        const std::vector<double> c = matchLoadings(L, fitThemes(Zl, k, true, opt).L);
        std::copy(c.begin(), c.end(), loso.begin() + std::ptrdiff_t(size_t(s) * size_t(k)));
    }, cancel);
    if (cancelled(cancel)) return stop();

    // The second method: average linkage on 1 − |Spearman ρ|. Recorded, never gating.
    {
        Matrix R = spearmanPairwise(Z, opt.minPairs);
        Matrix D(p, p);
        for (int i = 0; i < p; ++i) for (int j = 0; j < p; ++j) D.at(i, j) = i == j ? 0.0 : 1.0 - std::fabs(R.at(i, j));
        // Renumbered by first appearance along the measures, so the labels do not depend on the
        // order the merges happened to leave the groups in.
        std::vector<int> raw = averageLinkageCut(D, opt.clusterCut), seen;
        for (int c : raw) {
            auto it = std::find(seen.begin(), seen.end(), c);
            if (it == seen.end()) { seen.push_back(c); out.clusterLabels.push_back(int(seen.size())); }
            else                  out.clusterLabels.push_back(int(it - seen.begin()) + 1);
        }
    }
    std::vector<int> clusterOrder;                          // labels by first appearance
    std::vector<std::vector<int>> clusterMembers;
    for (int i = 0; i < p; ++i) {
        const int c = out.clusterLabels[size_t(i)];
        auto it = std::find(clusterOrder.begin(), clusterOrder.end(), c);
        if (it == clusterOrder.end()) { clusterOrder.push_back(c); clusterMembers.push_back({ i }); }
        else clusterMembers[size_t(it - clusterOrder.begin())].push_back(i);
    }

    // Session trend: scores through W = L (LᵀL)⁻¹, the uncentred projection over the within-session
    // spread of the centred one.
    std::vector<double> LtL(size_t(k) * size_t(k), 0.0), LtLi;
    for (int a = 0; a < k; ++a)
        for (int b = 0; b < k; ++b) {
            double s = 0.0;
            for (int i = 0; i < p; ++i) s += L.at(i, a) * L.at(i, b);
            LtL[size_t(a) * size_t(k) + size_t(b)] = s;
        }
    if (!invertSmall(k, LtL, LtLi)) LtLi.assign(size_t(k) * size_t(k), kNaN);
    Matrix W(p, k);
    for (int i = 0; i < p; ++i)
        for (int j = 0; j < k; ++j) {
            double s = 0.0;
            for (int l = 0; l < k; ++l) s += L.at(i, l) * LtLi[size_t(l) * size_t(k) + size_t(j)];
            W.at(i, j) = s;
        }
    const int n = Z.rows;
    std::vector<double> gmed(static_cast<size_t>(p));
    {
        std::vector<double> buf(static_cast<size_t>(n));
        for (int j = 0; j < p; ++j) {
            for (int i = 0; i < n; ++i) buf[size_t(i)] = pr.Xo.at(i, j);
            gmed[size_t(j)] = nanMedian(buf);
        }
    }
    Matrix Su(n, k, 0.0), Sc(n, k, 0.0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < p; ++j) {
            double zu = (pr.Xo.at(i, j) - gmed[size_t(j)]) / pr.spread[size_t(j)];
            if (zu > opt.trendClip) zu = opt.trendClip;
            else if (zu < -opt.trendClip) zu = -opt.trendClip;
            if (!std::isfinite(zu)) zu = 0.0;
            const double f = main.F.at(i, j);
            for (int l = 0; l < k; ++l) { Su.at(i, l) += zu * W.at(j, l); Sc.at(i, l) += f * W.at(j, l); }
        }

    for (int j = 0; j < k; ++j) {
        SwingTheme th;
        th.index = j + 1;
        th.varianceShare = main.varshare[size_t(j)];

        // Members, by −|loading|.
        std::vector<int> o(static_cast<size_t>(p));
        for (int i = 0; i < p; ++i) o[size_t(i)] = i;
        std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return std::fabs(L.at(a, j)) > std::fabs(L.at(b, j)); });
        for (int i : o) {
            if (std::fabs(L.at(i, j)) < opt.loadMin) break;
            ThemeMember m;
            m.measureId = pr.measures[i];
            m.loading   = L.at(i, j);
            m.rawHigh   = L.at(i, j) * pr.sign[size_t(i)] > 0;
            m.when      = measures.value(m.measureId).when;
            th.members.push_back(m);
            if (m.when > 0 && (th.startsAt == 0 || m.when < th.startsAt)) th.startsAt = m.when;
        }

        // Stability.
        std::vector<double> bj(static_cast<size_t>(B)), lj(static_cast<size_t>(S));
        for (int b = 0; b < B; ++b) bj[size_t(b)] = boot[size_t(b) * size_t(k) + size_t(j)];
        for (int s = 0; s < S; ++s) lj[size_t(s)] = loso[size_t(s) * size_t(k) + size_t(j)];
        th.bootMedian = medianPropagating(bj);
        th.bootP05    = percentileLinear(bj, opt.bootPercentile);
        th.losoMin    = minPropagating(lj);
        th.losoMedian = medianPropagating(lj);
        th.losoPerSession = lj;
        th.tier = tierOf(th.bootMedian, th.bootP05, th.losoMin, th.losoMedian, opt);
        th.emVsZeroFill = emVsZero[size_t(j)];

        // Cluster agreement: the best multi-member cluster (first on ties) against the members.
        {
            QSet<int> mem;
            for (const ThemeMember &m : th.members) mem.insert(int(pr.measures.indexOf(m.measureId)));
            double best = -1.0, bestJ = 0.0;
            bool any = false;
            for (const std::vector<int> &cl : clusterMembers) {
                if (cl.size() < 2) continue;
                QSet<int> c(cl.begin(), cl.end());
                const int inter = int((c & mem).size());
                const int uni   = int((c | mem).size());
                const double jac = double(inter) / double(std::max(1, uni));
                if (!any || jac > best) { best = jac; bestJ = jac; any = true; }
            }
            th.clusterJaccard = any ? bestJ : 0.0;
        }

        // Session scores and their slope.
        double within = 0.0;
        for (const std::vector<int> &rows : pr.rowsOf) {
            double mean = 0.0;
            for (int i : rows) mean += Sc.at(i, j);
            mean /= double(rows.size());
            double var = 0.0;
            for (int i : rows) var += (Sc.at(i, j) - mean) * (Sc.at(i, j) - mean);
            within += var / double(rows.size());
        }
        const double withinSd = std::sqrt(within / double(S));
        for (const std::vector<int> &rows : pr.rowsOf) {
            double mean = 0.0;
            for (int i : rows) mean += Su.at(i, j);
            mean /= double(rows.size());
            th.sessionScores.push_back(mean / withinSd);
        }
        double xm = 0.0, ym = 0.0;
        for (int s = 0; s < S; ++s) { xm += double(s); ym += th.sessionScores[size_t(s)]; }
        xm /= double(S);
        ym /= double(S);
        double sxy = 0.0, sxx = 0.0;
        for (int s = 0; s < S; ++s) {
            sxy += (double(s) - xm) * (th.sessionScores[size_t(s)] - ym);
            sxx += (double(s) - xm) * (double(s) - xm);
        }
        th.slope = sxx > 0 ? sxy / sxx : 0.0;
        double ssr = 0.0;
        for (int s = 0; s < S; ++s) {
            const double r = th.sessionScores[size_t(s)] - ym - th.slope * (double(s) - xm);
            ssr += r * r;
        }
        th.slopeSe = (S > 2 && sxx > 0) ? std::sqrt(ssr / double(S - 2) / sxx)
                                        : std::numeric_limits<double>::infinity();
        const bool lined = std::abs(th.slope) >= opt.trendMinT * th.slopeSe;
        th.trend = !lined ? 0 : th.slope > opt.trendSlope ? 1 : th.slope < -opt.trendSlope ? -1 : 0;
        out.themes.push_back(std::move(th));
    }
    if (cancelled(cancel)) return stop();
    return out;
}

} // namespace themes

// `measures` answers for every measure id (one it does not hold is oriented +1, two-sided, position
// unknown, its own family); `conditions` answers for every condition id (one it does not hold is
// not a fault, so neither listed nor praised). Cancelling returns enough == false and cancelled ==
// true with layer 1, "what you do well" and the focus groups filled.
inline SwingThemes reduceSwingThemes(const std::vector<ThemeSessionInput> &sessions,
                                     const QHash<QString, ThemeMeasureInfo> &measures,
                                     const QHash<QString, ThemeConditionInfo> &conditions,
                                     const ThemeOptions &opt = {}, const ThemeParallelFor &pf = {},
                                     const std::atomic<bool> *cancel = nullptr)
{
    const std::vector<const ThemeSessionInput *> sorted = themes::sortedSessions(sessions);
    SwingThemes out = themes::reduceLayers(sorted, measures, conditions, opt, pf, cancel);
    for (const ThemeSessionInput *s : sorted) {
        out.allSwings += int(s->shots.size());
        if (!s->shots.empty()) ++out.allSessions;
    }
    out.doWell = themes::doWell(sorted, conditions, out.seenMost, opt);
    out.focusOrder = themes::focusOrder(out.seenMost, conditions);
    return out;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
// The words
// ════════════════════════════════════════════════════════════════════════════════════════════════

// The golfer's phrases, from the pack (swing_themes_pack.h): a condition's `golfer` (the fault)
// and `golferWell` (its absence, said as what the golfer DOES), and a measure's `golferHigh` /
// `golferLow`. Empty means "no phrase", and such a row is not told. `family` is the measure's
// metricKey root (themeFamilyRoot) — one quantity read at several moments, signed or not — so a
// sentence does not name the same quantity twice ("your hips slide …, your hips move …"), and the
// view knows which quantities a shown theme moves. Empty = no metricKey. `why` is a condition's
// `golferWhy` (what the fault costs the golfer's shots, one sentence); `drill` answers a drill id
// with the registry's label and instruction (empty when it holds no such drill).
struct ThemeDrill {
    QString label;
    QString instruction;
};

struct ThemePhrases {
    std::function<QString(const QString &conditionId)>            condition;
    std::function<QString(const QString &measureId, bool high)>   measure;
    std::function<QString(const QString &measureId)>              family;
    std::function<QString(const QString &conditionId)>            well;
    std::function<QString(const QString &conditionId)>            why;
    std::function<ThemeDrill(const QString &drillId)>             drill;
};

struct SwingSummaryLines {
    QStringList seenMost;   // "What we see most"
    QStringList together;   // "What goes together in your swing"
};

inline QString themeCapitalised(const QString &s)
{
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
}

inline QString themeHowOften(double share, const ThemeOptions &opt = {})
{
    if (share >= opt.almostEvery) return QStringLiteral("on almost every swing");
    if (share >= opt.mostSwings)  return QStringLiteral("on most swings");
    return QStringLiteral("on more than half your swings");
}

// Where a theme starts, from its earliest member's swing position.
inline QString themeStartWords(int startsAt)
{
    switch (startsAt) {
    case 1:  return QStringLiteral("It starts at address");
    case 2:  return QStringLiteral("It starts early in the backswing");
    case 3:
    case 4:  return QStringLiteral("It starts in the backswing");
    case 5:
    case 6:  return QStringLiteral("It starts on the way down");
    case 7:  return QStringLiteral("It starts around impact");
    case 8:  return QStringLiteral("It starts after impact");
    case 9:  return QStringLiteral("It starts in the follow-through");
    case 10: return QStringLiteral("It shows in the finish");
    default: break;
    }
    return QString();
}

// Session means carry camera drift, so a trend is only ever worded as a possibility.
inline QString themeTrendWords(int trend)
{
    if (trend < 0) return QStringLiteral("may be easing");
    if (trend > 0) return QStringLiteral("may be growing");
    return QStringLiteral("no clear change yet");
}

// The phrases a theme is said with: the strongest members' (by −|loading|), each in the direction
// the theme pushes its raw value; empty and repeated phrases skipped, and so is a second member of
// a family already said. At most maxThemeParts.
inline QStringList themeParts(const SwingTheme &t, const ThemePhrases &ph, const ThemeOptions &opt = {})
{
    QStringList parts, families;
    for (const ThemeMember &m : t.members) {
        const QString p   = ph.measure ? ph.measure(m.measureId, m.rawHigh) : QString();
        const QString fam = ph.family ? ph.family(m.measureId) : QString();
        if (p.isEmpty() || parts.contains(p) || (!fam.isEmpty() && families.contains(fam))) continue;
        parts.append(p);
        if (!fam.isEmpty()) families.append(fam);
        if (parts.size() >= opt.maxThemeParts) break;
    }
    return parts;
}

// "On swings where {a}, {b}" from themeParts(); one phrase stands alone. CO-MOVEMENT, NOT CAUSE:
// this says what the swings show.
inline QString themeSentence(const SwingTheme &t, const ThemePhrases &ph, const ThemeOptions &opt = {})
{
    const QStringList parts = themeParts(t, ph, opt);
    if (parts.isEmpty()) return QString();
    if (parts.size() == 1) return themeCapitalised(parts.front());
    return QStringLiteral("On swings where %1, %2").arg(parts[0], parts.mid(1).join(QStringLiteral(" and ")));
}

inline SwingSummaryLines swingSummaryLines(const SwingThemes &st, const ThemePhrases &ph, const ThemeOptions &opt = {})
{
    SwingSummaryLines out;

    // What we see most.
    QStringList told;
    for (const SeenMostRow &r : st.seenMost) {
        if (int(out.seenMost.size()) >= opt.maxSeenMost) break;
        const QString phrase = ph.condition ? ph.condition(r.conditionId) : QString();
        if (phrase.isEmpty() || told.contains(phrase)) continue;
        told.append(phrase);
        QString line = QStringLiteral("%1 — %2 (%3 of %4 sessions")
                           .arg(themeCapitalised(phrase), themeHowOften(r.share, opt))
                           .arg(r.sessionsSeen).arg(r.sessionsJudged);
        if (r.trend == QStringLiteral("easing") || r.trend == QStringLiteral("growing"))
            line += QStringLiteral("; ") + r.trend;
        line += QLatin1Char(')');
        out.seenMost.append(line);
    }
    if (out.seenMost.isEmpty()) out.seenMost.append(QStringLiteral("Nothing shows up on most of your swings."));

    // What goes together.
    if (!st.enough) {
        out.together.append(QStringLiteral("Not yet — it takes about %1 swings over %2 sessions to see what goes "
                                           "together (so far: %3 swings over %4 sessions).")
                                .arg(opt.minSwings).arg(opt.minSessions).arg(st.swings).arg(st.sessions));
        return out;
    }
    const auto rank = [](ThemeTier t) { return t == ThemeTier::Firm ? 0 : t == ThemeTier::Probably ? 1 : 2; };
    std::vector<const SwingTheme *> shown;
    for (const SwingTheme &t : st.themes) if (t.tier != ThemeTier::None) shown.push_back(&t);
    std::stable_sort(shown.begin(), shown.end(), [&](const SwingTheme *a, const SwingTheme *b) {
        if (rank(a->tier) != rank(b->tier)) return rank(a->tier) < rank(b->tier);
        return a->varianceShare > b->varianceShare;
    });
    for (const SwingTheme *t : shown) {
        if (int(out.together.size()) >= opt.maxTogether) break;
        const QString s = themeSentence(*t, ph, opt);
        if (s.isEmpty()) continue;
        const QString lead = t->tier == ThemeTier::Probably ? QStringLiteral("Probably: ")
                           : t->tier == ThemeTier::Possibly ? QStringLiteral("Possibly: ") : QString();
        QString line = lead + s + QLatin1Char('.');
        const QString start = themeStartWords(t->startsAt);
        if (!start.isEmpty()) line += QLatin1Char(' ') + start + QLatin1Char('.');
        line += QStringLiteral(" (") + themeTrendWords(t->trend) + QLatin1Char(')');
        out.together.append(line);
    }
    if (out.together.isEmpty())
        out.together.append(QStringLiteral("Nothing yet — your faults don't rise and fall together clearly enough to say."));
    return out;
}

// ── The view ─────────────────────────────────────────────────────────────────────────────────────
//
// What the home screen draws (theme_summary.summary_view, word for word):
//
//   subtitle    "From N swings over M sessions" — every shot in the ledgers, every session holding
//               one ("1 swing", "1 session").
//   focus       the first focus group (SwingThemes::focusOrder) whose faults say at least one
//               `golfer` phrase: the drill's label as the title (a solo fault's capitalised
//               `golferWell`); what to aim for (each fault's capitalised `golferWell`); what happens
//               right now (each fault as a needs-work item); why it matters (the `golferWhy`
//               sentences, joined by a space); the drill to practise (label and instruction, "" for
//               a solo group); and the reason it was chosen — "Picked first: it comes earliest in
//               your swing" when no group starts earlier, else "Picked first: it shows up on the
//               most of your swings", then " and covers N of the things on your list" when it says
//               two or more (N in words to nine), then ".". present = false when nothing needs work.
//   next        every other needs-work fault, group by group in focus order, a phrase told once
//               (the focus's included), at most maxNext.
//   doWell      at most maxDoWell: the capitalised `golferWell`, a caption from the share, a pip per
//               judged session. A row whose families touch ANY member of a theme shown below (its
//               family, else its id — every member, not only the two named) is dropped; then an
//               empty or already-said phrase is skipped.
//   needsWork   at most maxSeenMost: the capitalised `golfer`, the share and its frequency words,
//               the trend (+1 growing, −1 easing, 0) and a pip per judged session. Same skipping.
//               Kept for compatibility: the home shows the focus and "next" instead.
//   together    at most maxTogether themes with a tier, firm → probably → possibly, then by variance:
//               two parts as first = "When " + A, second = B; one part as first = A capitalised,
//               second = ""; none, not shown. The timeline stop the theme starts at, its words, the
//               trend.
//   note        "" when anything goes together; else "Not yet — …" (too little data) or
//               "Nothing yet — …" (no theme holds).

struct DoWellItem {
    QString text;               // "Your lead heel stays down going back"
    QString caption;            // "In the ideal range on almost every swing"
    std::vector<bool> pips;     // per judged session, oldest first
};

struct NeedsWorkItem {
    QString text;               // "You stand up through the ball"
    double  share = 0.0;
    QString frequency;          // "every swing" / "almost every swing" / "most swings" / "more than half your swings"
    int     trend = 0;          // +1 growing, −1 easing, 0 steady or too few sessions
    std::vector<bool> pips;     // per judged session, oldest first
};

struct FocusItem {
    bool present = false;
    QString title;                          // drill label; for a solo group: capitalised golferWell of its fault
    QStringList aimFor;                     // capitalised golferWell per fault in the group (skip empty / dup)
    std::vector<NeedsWorkItem> rightNow;    // the group's faults, as needsWork items (skip empty / dup phrase)
    QString why;                            // the group's golferWhy sentences joined by one space (skip dup)
    QString practiseLabel;                  // drill label ("" for solo)
    QString practise;                       // drill instruction ("" for solo)
    QString reason;                         // "Picked first: it comes earliest in your swing and covers two of …."
    QStringList conditionIds;               // the group's, in its order
};

struct TogetherItem {
    QString tier;               // "firm" / "probably" / "possibly"
    QString first;              // "When your hips slide …", or the one part, capitalised
    QString second;             // "your lead knee caves in …", or ""
    int     startStop = -1;     // 0 address, 1 back, 2 top, 3 down, 4 impact, 5 finish; −1 unplaced
    QString startWords;         // "starts at the top", or "" when unplaced
    int     trend = 0;          // +1 may be growing, −1 may be easing, 0 no clear change
};

struct SwingSummaryView {
    QString subtitle;
    std::vector<DoWellItem>    doWell;
    std::vector<NeedsWorkItem> needsWork;
    std::vector<TogetherItem>  together;
    QString note;
    FocusItem                  focus;
    std::vector<NeedsWorkItem> next;
};

// Swing position (1..10) -> the timeline's six stops; 0 (unplaced) -> −1.
inline int themeStartStop(int startsAt)
{
    if (startsAt <= 0) return -1;
    if (startsAt <= 1) return 0;
    if (startsAt <= 3) return 1;
    if (startsAt == 4) return 2;
    if (startsAt <= 6) return 3;
    if (startsAt == 7) return 4;
    return 5;
}

inline QString themeStartStopWords(int stop)
{
    switch (stop) {
    case 0: return QStringLiteral("starts at address");
    case 1: return QStringLiteral("starts in the backswing");
    case 2: return QStringLiteral("starts at the top");
    case 3: return QStringLiteral("starts coming down");
    case 4: return QStringLiteral("starts around impact");
    case 5: return QStringLiteral("starts in the finish");
    default: break;
    }
    return QString();
}

inline QString themeWellCaption(double share, const ThemeOptions &opt = {})
{
    if (share >= opt.everySwing)  return QStringLiteral("In the ideal range on every swing");
    if (share >= opt.almostEvery) return QStringLiteral("In the ideal range on almost every swing");
    return QStringLiteral("In the ideal range on most swings");
}

inline QString themeFrequency(double share, const ThemeOptions &opt = {})
{
    if (share >= opt.everySwing)  return QStringLiteral("every swing");
    if (share >= opt.almostEvery) return QStringLiteral("almost every swing");
    if (share >= opt.mostSwings)  return QStringLiteral("most swings");
    return QStringLiteral("more than half your swings");
}

// "two" .. "nine", else digits — how many of the list the focus covers.
inline QString themeCountWords(int n)
{
    static const char *const words[] = { "two", "three", "four", "five", "six", "seven", "eight", "nine" };
    return n >= 2 && n <= 9 ? QString::fromLatin1(words[n - 2]) : QString::number(n);
}

namespace themes {

inline NeedsWorkItem needsWorkItem(const SeenMostRow &r, const QString &phrase, const ThemeOptions &opt)
{
    const int trend = r.trend == QStringLiteral("growing") ? 1 : r.trend == QStringLiteral("easing") ? -1 : 0;
    return { themeCapitalised(phrase), r.share, themeFrequency(r.share, opt), trend, r.pips };
}

// The focus and "next on your list" (theme_summary.focus_view), from the stored groups.
inline void focusView(const SwingThemes &st, const ThemePhrases &ph, const ThemeOptions &opt, SwingSummaryView &v)
{
    QHash<QString, const SeenMostRow *> rows;
    for (const SeenMostRow &r : st.seenMost) rows.insert(r.conditionId, &r);
    const auto phraseOf = [&](const QString &id) { return ph.condition ? ph.condition(id) : QString(); };

    const FocusGroup *chosen = nullptr;
    QStringList told;                           // the phrases said so far, focus first
    for (const FocusGroup &g : st.focusOrder) {
        QStringList said;
        std::vector<NeedsWorkItem> right;
        for (const QString &id : g.conditionIds) {
            const QString phrase = phraseOf(id);
            const SeenMostRow *r = rows.value(id);
            if (phrase.isEmpty() || said.contains(phrase) || !r) continue;
            said.append(phrase);
            right.push_back(needsWorkItem(*r, phrase, opt));
        }
        if (!right.empty()) {
            chosen = &g;
            told = said;
            v.focus.rightNow = std::move(right);
            break;
        }
    }

    if (chosen) {
        FocusItem &f = v.focus;
        f.present = true;
        f.conditionIds = chosen->conditionIds;
        QStringList aim, whys;
        for (const QString &id : chosen->conditionIds) {
            const QString well = ph.well ? ph.well(id) : QString();
            if (!well.isEmpty() && !aim.contains(well)) aim.append(well);
            const QString why = ph.why ? ph.why(id) : QString();
            if (!why.isEmpty() && !whys.contains(why)) whys.append(why);
        }
        for (const QString &a : aim) f.aimFor.append(themeCapitalised(a));
        f.why = whys.join(QLatin1Char(' '));
        if (!chosen->drill.isEmpty()) {
            const ThemeDrill d = ph.drill ? ph.drill(chosen->drill) : ThemeDrill{};
            f.title         = d.label;
            f.practiseLabel = d.label;
            f.practise      = d.instruction;
        } else {
            f.title = themeCapitalised(ph.well && !chosen->conditionIds.isEmpty() ? ph.well(chosen->conditionIds.front()) : QString());
        }
        const auto placed = [](int when) { return when > 0 ? when : kThemeFocusUnplaced; };
        int earliest = kThemeFocusUnplaced;
        for (const FocusGroup &g : st.focusOrder) earliest = std::min(earliest, placed(g.when));
        f.reason = placed(chosen->when) == earliest ? QStringLiteral("Picked first: it comes earliest in your swing")
                                                    : QStringLiteral("Picked first: it shows up on the most of your swings");
        const int n = int(f.rightNow.size());
        if (n >= 2) f.reason += QStringLiteral(" and covers %1 of the things on your list").arg(themeCountWords(n));
        f.reason += QLatin1Char('.');
    }

    for (const FocusGroup &g : st.focusOrder) {
        if (chosen && g.key == chosen->key) continue;
        for (const QString &id : g.conditionIds) {
            if (int(v.next.size()) >= opt.maxNext) break;
            const QString phrase = phraseOf(id);
            const SeenMostRow *r = rows.value(id);
            if (phrase.isEmpty() || told.contains(phrase) || !r) continue;
            told.append(phrase);
            v.next.push_back(needsWorkItem(*r, phrase, opt));
        }
    }
}

} // namespace themes

inline SwingSummaryView swingSummaryView(const SwingThemes &st, const ThemePhrases &ph, const ThemeOptions &opt = {})
{
    SwingSummaryView v;
    v.subtitle = QStringLiteral("From %1 %2 over %3 %4")
                     .arg(st.allSwings).arg(st.allSwings == 1 ? QStringLiteral("swing") : QStringLiteral("swings"))
                     .arg(st.allSessions).arg(st.allSessions == 1 ? QStringLiteral("session") : QStringLiteral("sessions"));

    std::vector<const SwingTheme *> shown;      // the themes the page says, in order
    if (st.enough) {
        const auto rank = [](ThemeTier t) { return t == ThemeTier::Firm ? 0 : t == ThemeTier::Probably ? 1 : 2; };
        std::vector<const SwingTheme *> tiered;
        for (const SwingTheme &t : st.themes) if (t.tier != ThemeTier::None) tiered.push_back(&t);
        std::stable_sort(tiered.begin(), tiered.end(), [&](const SwingTheme *a, const SwingTheme *b) {
            if (rank(a->tier) != rank(b->tier)) return rank(a->tier) < rank(b->tier);
            return a->varianceShare > b->varianceShare;
        });
        for (const SwingTheme *t : tiered) {
            if (int(v.together.size()) >= opt.maxTogether) break;
            const QStringList parts = themeParts(*t, ph, opt);
            if (parts.isEmpty()) continue;
            shown.push_back(t);
            TogetherItem item;
            item.tier       = themeTierToString(t->tier);
            item.first      = parts.size() > 1 ? QStringLiteral("When ") + parts[0] : themeCapitalised(parts[0]);
            item.second     = parts.size() > 1 ? parts[1] : QString();
            item.startStop  = themeStartStop(t->startsAt);
            item.startWords = themeStartStopWords(item.startStop);
            item.trend      = t->trend;
            v.together.push_back(std::move(item));
        }
    }

    // A quantity a shown theme moves is not praised — every member of it, named or not.
    QSet<QString> moved;
    for (const SwingTheme *t : shown)
        for (const ThemeMember &m : t->members) {
            const QString fam = ph.family ? ph.family(m.measureId) : QString();
            moved.insert(fam.isEmpty() ? m.measureId : fam);
        }
    QStringList told;
    for (const DoWellRow &r : st.doWell) {
        if (int(v.doWell.size()) >= opt.maxDoWell) break;
        bool touches = false;
        for (const QString &f : r.families) touches = touches || moved.contains(f);
        if (touches) continue;
        const QString phrase = ph.well ? ph.well(r.conditionId) : QString();
        if (phrase.isEmpty() || told.contains(phrase)) continue;
        told.append(phrase);
        v.doWell.push_back({ themeCapitalised(phrase), themeWellCaption(r.share, opt), r.pips });
    }

    told.clear();
    for (const SeenMostRow &r : st.seenMost) {
        if (int(v.needsWork.size()) >= opt.maxSeenMost) break;
        const QString phrase = ph.condition ? ph.condition(r.conditionId) : QString();
        if (phrase.isEmpty() || told.contains(phrase)) continue;
        told.append(phrase);
        v.needsWork.push_back(themes::needsWorkItem(r, phrase, opt));
    }

    themes::focusView(st, ph, opt, v);

    if (v.together.empty())
        v.note = st.enough ? QStringLiteral("Nothing yet — your faults don't rise and fall together clearly enough to say.")
                           : QStringLiteral("Not yet — it takes about %1 swings over %2 sessions to see what goes "
                                            "together (so far: %3 swings over %4 sessions).")
                                 .arg(opt.minSwings).arg(opt.minSessions).arg(st.swings).arg(st.sessions);
    return v;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
// Serialisation
// ════════════════════════════════════════════════════════════════════════════════════════════════
//
// A stamped derivation, like work_ons.json: the ledgers stay the evidence, and a file from another
// schema or rule is re-derived rather than read.

namespace themes {
inline QJsonValue jsonNumber(double x) { return std::isfinite(x) ? QJsonValue(x) : QJsonValue(QJsonValue::Null); }
inline double numberFrom(const QJsonValue &v) { return v.isDouble() ? v.toDouble() : kNaN; }
inline QJsonArray jsonNumbers(const std::vector<double> &v)
{
    QJsonArray a;
    for (double x : v) a.append(jsonNumber(x));
    return a;
}
inline std::vector<double> numbersFrom(const QJsonValue &v)
{
    std::vector<double> out;
    for (const QJsonValue &x : v.toArray()) out.push_back(numberFrom(x));
    return out;
}
inline QJsonArray jsonBools(const std::vector<bool> &v)
{
    QJsonArray a;
    for (bool x : v) a.append(x);
    return a;
}
inline std::vector<bool> boolsFrom(const QJsonValue &v)
{
    std::vector<bool> out;
    for (const QJsonValue &x : v.toArray()) out.push_back(x.toBool());
    return out;
}
} // namespace themes

inline QJsonObject toJson(const SwingThemes &st)
{
    using namespace themes;
    QJsonObject root;
    root[QStringLiteral("schemaVersion")]    = kThemeSchemaVersion;
    root[QStringLiteral("ruleVersion")]      = kThemeRuleVersion;
    root[QStringLiteral("enough")]           = st.enough;
    root[QStringLiteral("cancelled")]        = st.cancelled;
    root[QStringLiteral("allSwings")]        = st.allSwings;
    root[QStringLiteral("allSessions")]      = st.allSessions;
    root[QStringLiteral("swings")]           = st.swings;
    root[QStringLiteral("sessions")]         = st.sessions;
    root[QStringLiteral("sessionNames")]     = QJsonArray::fromStringList(st.sessionNames);
    root[QStringLiteral("keptSessionNames")] = QJsonArray::fromStringList(st.keptSessionNames);
    QJsonArray dropped;
    for (const auto &d : st.dropped) dropped.append(QJsonArray{ d.first, d.second });
    root[QStringLiteral("dropped")]       = dropped;
    root[QStringLiteral("measures")]      = QJsonArray::fromStringList(st.measures);
    root[QStringLiteral("k")]             = st.k;
    root[QStringLiteral("eig")]           = jsonNumbers(st.eig);
    root[QStringLiteral("paThreshold")]   = jsonNumbers(st.paThreshold);
    root[QStringLiteral("varianceShare")] = jsonNumbers(st.varianceShare);
    QJsonArray loadings;
    for (int i = 0; st.k > 0 && i < st.measures.size(); ++i) {
        const auto first = st.loadings.begin() + std::ptrdiff_t(size_t(i) * size_t(st.k));
        if (st.loadings.end() - first < st.k) break;
        loadings.append(jsonNumbers(std::vector<double>(first, first + st.k)));
    }
    root[QStringLiteral("loadings")] = loadings;
    QJsonArray labels;
    for (int c : st.clusterLabels) labels.append(c);
    root[QStringLiteral("clusterLabels")] = labels;

    QJsonArray themesArr;
    for (const SwingTheme &t : st.themes) {
        QJsonObject o;
        o[QStringLiteral("index")]         = t.index;
        o[QStringLiteral("varianceShare")] = jsonNumber(t.varianceShare);
        QJsonArray members;
        for (const ThemeMember &m : t.members) {
            QJsonObject mo;
            mo[QStringLiteral("measure")] = m.measureId;
            mo[QStringLiteral("loading")] = jsonNumber(m.loading);
            mo[QStringLiteral("rawHigh")] = m.rawHigh;
            mo[QStringLiteral("when")]    = m.when;
            members.append(mo);
        }
        o[QStringLiteral("members")]        = members;
        o[QStringLiteral("startsAt")]       = t.startsAt;
        o[QStringLiteral("tier")]           = t.tier == ThemeTier::None ? QJsonValue(QJsonValue::Null)
                                                                         : QJsonValue(themeTierToString(t.tier));
        o[QStringLiteral("bootMedian")]     = jsonNumber(t.bootMedian);
        o[QStringLiteral("bootP05")]        = jsonNumber(t.bootP05);
        o[QStringLiteral("losoMin")]        = jsonNumber(t.losoMin);
        o[QStringLiteral("losoMedian")]     = jsonNumber(t.losoMedian);
        o[QStringLiteral("losoPerSession")] = jsonNumbers(t.losoPerSession);
        o[QStringLiteral("emVsZeroFill")]   = jsonNumber(t.emVsZeroFill);
        o[QStringLiteral("clusterJaccard")] = jsonNumber(t.clusterJaccard);
        o[QStringLiteral("sessionScores")]  = jsonNumbers(t.sessionScores);
        o[QStringLiteral("slope")]          = jsonNumber(t.slope);
        o[QStringLiteral("slopeSe")]        = jsonNumber(t.slopeSe);
        o[QStringLiteral("trend")]          = t.trend;
        themesArr.append(o);
    }
    root[QStringLiteral("themes")] = themesArr;

    QJsonArray seen;
    for (const SeenMostRow &r : st.seenMost) {
        QJsonObject o;
        o[QStringLiteral("id")]             = r.conditionId;
        o[QStringLiteral("share")]          = jsonNumber(r.share);
        o[QStringLiteral("sessionsSeen")]   = r.sessionsSeen;
        o[QStringLiteral("sessionsJudged")] = r.sessionsJudged;
        o[QStringLiteral("trend")]          = r.trend;
        o[QStringLiteral("pips")]           = jsonBools(r.pips);
        seen.append(o);
    }
    root[QStringLiteral("seenMost")] = seen;

    QJsonArray well;
    for (const DoWellRow &r : st.doWell) {
        QJsonObject o;
        o[QStringLiteral("id")]             = r.conditionId;
        o[QStringLiteral("share")]          = jsonNumber(r.share);
        o[QStringLiteral("prominence")]     = r.prominence;
        o[QStringLiteral("sessionsJudged")] = r.sessionsJudged;
        o[QStringLiteral("swingsJudged")]   = r.swingsJudged;
        o[QStringLiteral("pips")]           = jsonBools(r.pips);
        o[QStringLiteral("families")]       = QJsonArray::fromStringList(r.families);
        well.append(o);
    }
    root[QStringLiteral("doWell")] = well;

    QJsonArray focus;
    for (const FocusGroup &g : st.focusOrder) {
        QJsonObject o;
        o[QStringLiteral("key")]          = g.key;
        o[QStringLiteral("drill")]        = g.drill;
        o[QStringLiteral("when")]         = g.when;
        o[QStringLiteral("conditionIds")] = QJsonArray::fromStringList(g.conditionIds);
        focus.append(o);
    }
    root[QStringLiteral("focusOrder")] = focus;
    return root;
}

// `ok` is false for a file from another schema or rule: its answer is not this build's answer.
inline SwingThemes swingThemesFromJson(const QJsonObject &root, bool *ok = nullptr)
{
    using namespace themes;
    SwingThemes st;
    const bool current = root.value(QStringLiteral("schemaVersion")).toInt() == kThemeSchemaVersion
                      && root.value(QStringLiteral("ruleVersion")).toInt() == kThemeRuleVersion;
    if (ok) *ok = current;
    if (!current) return st;

    const auto strings = [](const QJsonValue &v) {
        QStringList l;
        for (const QJsonValue &x : v.toArray()) l.append(x.toString());
        return l;
    };
    st.enough           = root.value(QStringLiteral("enough")).toBool();
    st.cancelled        = root.value(QStringLiteral("cancelled")).toBool();
    st.allSwings        = root.value(QStringLiteral("allSwings")).toInt();
    st.allSessions      = root.value(QStringLiteral("allSessions")).toInt();
    st.swings           = root.value(QStringLiteral("swings")).toInt();
    st.sessions         = root.value(QStringLiteral("sessions")).toInt();
    st.sessionNames     = strings(root.value(QStringLiteral("sessionNames")));
    st.keptSessionNames = strings(root.value(QStringLiteral("keptSessionNames")));
    for (const QJsonValue &d : root.value(QStringLiteral("dropped")).toArray()) {
        const QJsonArray a = d.toArray();
        st.dropped.push_back({ a.at(0).toString(), a.at(1).toInt() });
    }
    st.measures      = strings(root.value(QStringLiteral("measures")));
    st.k             = root.value(QStringLiteral("k")).toInt();
    st.eig           = numbersFrom(root.value(QStringLiteral("eig")));
    st.paThreshold   = numbersFrom(root.value(QStringLiteral("paThreshold")));
    st.varianceShare = numbersFrom(root.value(QStringLiteral("varianceShare")));
    for (const QJsonValue &r : root.value(QStringLiteral("loadings")).toArray())
        for (double x : numbersFrom(r)) st.loadings.push_back(x);
    for (const QJsonValue &c : root.value(QStringLiteral("clusterLabels")).toArray()) st.clusterLabels.push_back(c.toInt());

    for (const QJsonValue &v : root.value(QStringLiteral("themes")).toArray()) {
        const QJsonObject o = v.toObject();
        SwingTheme t;
        t.index         = o.value(QStringLiteral("index")).toInt();
        t.varianceShare = numberFrom(o.value(QStringLiteral("varianceShare")));
        for (const QJsonValue &mv : o.value(QStringLiteral("members")).toArray()) {
            const QJsonObject mo = mv.toObject();
            ThemeMember m;
            m.measureId = mo.value(QStringLiteral("measure")).toString();
            m.loading   = numberFrom(mo.value(QStringLiteral("loading")));
            m.rawHigh   = mo.value(QStringLiteral("rawHigh")).toBool(true);
            m.when      = mo.value(QStringLiteral("when")).toInt();
            t.members.push_back(m);
        }
        t.startsAt       = o.value(QStringLiteral("startsAt")).toInt();
        t.tier           = themeTierFromString(o.value(QStringLiteral("tier")).toString());
        t.bootMedian     = numberFrom(o.value(QStringLiteral("bootMedian")));
        t.bootP05        = numberFrom(o.value(QStringLiteral("bootP05")));
        t.losoMin        = numberFrom(o.value(QStringLiteral("losoMin")));
        t.losoMedian     = numberFrom(o.value(QStringLiteral("losoMedian")));
        t.losoPerSession = numbersFrom(o.value(QStringLiteral("losoPerSession")));
        t.emVsZeroFill   = numberFrom(o.value(QStringLiteral("emVsZeroFill")));
        t.clusterJaccard = numberFrom(o.value(QStringLiteral("clusterJaccard")));
        t.sessionScores  = numbersFrom(o.value(QStringLiteral("sessionScores")));
        t.slope          = numberFrom(o.value(QStringLiteral("slope")));
        t.slopeSe        = numberFrom(o.value(QStringLiteral("slopeSe")));
        t.trend          = o.value(QStringLiteral("trend")).toInt();
        st.themes.push_back(std::move(t));
    }
    for (const QJsonValue &v : root.value(QStringLiteral("seenMost")).toArray()) {
        const QJsonObject o = v.toObject();
        SeenMostRow r;
        r.conditionId    = o.value(QStringLiteral("id")).toString();
        r.share          = numberFrom(o.value(QStringLiteral("share")));
        r.sessionsSeen   = o.value(QStringLiteral("sessionsSeen")).toInt();
        r.sessionsJudged = o.value(QStringLiteral("sessionsJudged")).toInt();
        r.trend          = o.value(QStringLiteral("trend")).toString();
        r.pips           = boolsFrom(o.value(QStringLiteral("pips")));
        st.seenMost.push_back(std::move(r));
    }
    for (const QJsonValue &v : root.value(QStringLiteral("doWell")).toArray()) {
        const QJsonObject o = v.toObject();
        DoWellRow r;
        r.conditionId    = o.value(QStringLiteral("id")).toString();
        r.share          = numberFrom(o.value(QStringLiteral("share")));
        r.prominence     = o.value(QStringLiteral("prominence")).toInt();
        r.sessionsJudged = o.value(QStringLiteral("sessionsJudged")).toInt();
        r.swingsJudged   = o.value(QStringLiteral("swingsJudged")).toInt();
        r.pips           = boolsFrom(o.value(QStringLiteral("pips")));
        for (const QJsonValue &f : o.value(QStringLiteral("families")).toArray()) r.families.append(f.toString());
        st.doWell.push_back(std::move(r));
    }
    for (const QJsonValue &v : root.value(QStringLiteral("focusOrder")).toArray()) {
        const QJsonObject o = v.toObject();
        FocusGroup g;
        g.key          = o.value(QStringLiteral("key")).toString();
        g.drill        = o.value(QStringLiteral("drill")).toString();
        g.when         = o.value(QStringLiteral("when")).toInt();
        g.conditionIds = strings(o.value(QStringLiteral("conditionIds")));
        st.focusOrder.push_back(std::move(g));
    }
    return st;
}

} // namespace pinpoint::analysis
