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

// DIRECTION B, "THE WORKBENCH" — every metric as a variable read at its instant
// (docs/design/charts_panel_traces_relations.md §2.3, §2.4; ported from variant_b/lib/Stats.js and
// Bench.qml, whose numbers the golden tests pin).
//
// A READING is one metric at one P-position: the value its card shows (the series' phaseSamples), one
// per shot, NaN where the shot did not measure it. The P-positions a key is read at come from the
// CALLER (ReadAtPhases), because ChartMetrics.cardSpecFor.readAt is the catalogue's and is corrected in
// step 2 (design §7: it defaults to impact for address-only readings); this file never calls ChartMetrics.
// Of the phases the caller names, those no shot sampled are dropped; when none is left, the reading falls
// back to impact if any shot sampled it there, else the latest sampled P-position (the mock's rule: "a
// reading taken relative to address is zero there by definition").
//
// Readings are session-level: nothing here depends on which shot is picked. ReadingPlace says where ONE
// shot sits (rank, side, robust z), so the panel asks for it per picked shot.
//
// ── THE STATISTICS, AND WHY THESE ────────────────────────────────────────────────────────────
//   · Box: type-7 quartiles, Tukey whiskers (the most extreme values within 1.5 IQR of the box, never
//     inside it), outliers past them. `side` ±1 = past a whisker: "apart from the session".
//   · r: Pearson over the shots both readings have, n ≥ 5; undefined (not NaN, not a crash) when either
//     reading is constant. The 95 % interval is Fisher's z ± 1.96/√(n−3); `clear` = it excludes 0.
//     rCritical(n) = tanh(1.96/√(n−3)) is the |r| chance reaches — ".51 at n = 15". chanceCount(m) =
//     round(0.05 m) is how many of m readings would clear by chance alone.
//   · Drift: r against shot order.
//   · Kinds: k-medoids (k = 2, exhaustive over medoid pairs, each kind ≥ max(3, ⌈25 % of shots⌉)) on the
//     readings every shot has (and that vary), each standardised robustly (median, IQR/1.349, held to
//     ±2.5), RMS distance. The silhouette says how real the kinds are; below 0.25 there is no structure
//     (D7) and the kinds are the cleanest single break instead: the widest gap, in IQRs, between
//     consecutive sorted shots of any one reading, each side ≥ the minimum size — labelled a SPLIT, not
//     kinds. 4 July: silhouette .02, so the split: an 8 mph clubhead-speed gap, 6 faster, 9 slower.
//   · Separation: Cohen's d (pooled sd) of every reading between the two groups, against dCritical =
//     (1.96 + 2.4/df)·√(1/nA + 1/nB), the d chance gives at these sizes (Words.qml).
//   · Facets: a condition that fired on SOME shots, ≥ 3 each side (one that fires on every shot can never
//     split a session), session halves, shapes, and club when the session is mixed.
//
// Kinds' CURVES are not computed here: they are session_traces.h pooledFit over each kind's ordinals —
// the one group-fit engine.

#include "session_series_types.h"
#include "session_traces.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace pinpoint::charts {

// ── box statistics ──────────────────────────────────────────────────────────────────────────

// The one implementation, shared with Session diagnostics' strips: ../typeset/box_stats.h
using pinpoint::box::BoxStats;
using pinpoint::box::boxStats;

// ── readings ────────────────────────────────────────────────────────────────────────────────

using ReadAtPhases = std::function<std::vector<int>(const QString &key)>;

struct SessionReading {
    QString id;                         // "key@phase"
    QString key;
    int     phase = -1;
    QString label, unit;                // the series' own
    std::vector<double>  values;        // one per shot (index = ordinal − 1), NaN = not measured
    BoxStats box;
    bool    multi = false;              // the key is read at more than one P-position
    std::vector<uint8_t> fired;         // per shot: a condition linked to this reading fired
    std::vector<uint8_t> refused;       // per shot: an estimate a gate refused, shown with a caution (Charts fills it)
    std::vector<double>  refusedSigma;  // its ± per shot (NaN = not characterised)
    std::vector<uint8_t> clean;         // per shot: a linked condition graded it inside its corridor (Charts fills it)
    std::vector<double>  firedZ;        // per shot: the largest |z| a fired link read, in the ledger's own unit (Charts)
    QStringList conditionIds;           // the linked conditions
};

// Every reading the session's shots produced. Keys in `keyOrder` (empty = each shot's metricOrder,
// first seen first); a reading measured on fewer than 3 shots is not a reading.
// Fired comes from the condition link (ConditionInfo::metricKey / phase): a condition with phase −1
// links to the key's reading only when the key is read at a single P-position.
std::vector<SessionReading> sessionReadings(const SessionSnapshot &session, const ReadAtPhases &readAt,
                                            const QStringList &keyOrder = {});

// Where one shot sits in a reading.
struct ReadingPlace {
    bool   measured = false;
    double value = kNaN;
    double rz = 0.0;                    // (value − median) / (IQR/1.349); 0 when unmeasured or no spread
    int    rankHi = 0;                  // 1 = highest of the session; 0 = unmeasured
    int    rankLo = 0;                  // 1 = lowest
    int    side = 0;                    // +1 past the upper whisker, −1 past the lower, 0 inside
};
ReadingPlace placeOf(const SessionReading &reading, int shotIndex);

// The readings that put a shot past the session's whiskers, most extreme first (indices into readings).
std::vector<int> apartFrom(const std::vector<SessionReading> &readings, int shotIndex);

// ── relations ───────────────────────────────────────────────────────────────────────────────

struct Correlation {
    bool   defined = false;             // false: n < 5 or a constant reading
    int    n = 0;
    double r = kNaN, lo = kNaN, hi = kNaN;            // r and its 95 % Fisher interval
    double slope = kNaN, intercept = kNaN;            // least squares, y per x, in units
    double meanX = kNaN, meanY = kNaN;
    bool   clear = false;               // the interval excludes 0
};
Correlation pearson(const std::vector<double> &xs, const std::vector<double> &ys);
double rCritical(int n);
int    chanceCount(int tests);

// The same quantity measured another way: such a pair is labelled, not ranked as a finding.
bool    sameQuantityAs(const QString &keyA, const QString &keyB);
QString sameQuantityPartner(const QString &key);

struct CorrRow {
    int  reading = -1;                  // index into readings
    bool same = false;
    Correlation c;
};
// Every other reading against readings[y], defined ones only, ranked by |r|.
std::vector<CorrRow> correlate(const std::vector<SessionReading> &readings, int y);
Correlation pairOf(const SessionReading &x, const SessionReading &y);
// r against shot order (1…N).
Correlation driftOf(const SessionReading &reading);
std::vector<CorrRow> drifts(const std::vector<SessionReading> &readings);   // ranked by |r|

// ── kinds ───────────────────────────────────────────────────────────────────────────────────

enum class KindsMethod { None, KMedoids, GapSplit };

struct KindsOptions {
    double minSilhouette = 0.25;        // below it: no structure (D7)
    double minShare = 0.25;             // each kind ≥ max(minSize, ⌈share × shots⌉)
    int    minSize = 3;
};

struct KindSeparation {
    int    reading = -1;
    double d = kNaN, medA = kNaN, medB = kNaN;
    int    nA = 0, nB = 0;
};

struct SessionKinds {
    KindsMethod method = KindsMethod::None;
    double silhouette = kNaN;           // the k-medoid kinds', NaN when none could be formed
    int    readingsUsed = 0;            // readings every shot has, with spread
    int    minSize = 0;
    std::vector<int> kmA, kmB;          // the k-medoid kinds (ordinals), even when not used
    int    kmMedoidA = 0, kmMedoidB = 0;
    int    splitReading = -1;           // the gap split's reading (index), −1 none
    double splitGap = kNaN, splitScore = kNaN, splitCut = kNaN, splitLow = kNaN, splitHigh = kNaN;
    std::vector<int> groupA, groupB;    // THE kinds used (ordinals). Split: A = the high side.
    int    medoidA = 0, medoidB = 0;    // each kind's most typical shot (ordinal)
    std::vector<KindSeparation> separations;   // every reading, ranked by |d|
    double dCritical = kNaN;            // for |groupA| vs |groupB|
};
SessionKinds findKinds(const std::vector<SessionReading> &readings, const KindsOptions &options = {});

double cohenD(const std::vector<double> &a, const std::vector<double> &b);   // NaN when a side < 3
double dCritical(int nA, int nB);

// ── facets ──────────────────────────────────────────────────────────────────────────────────

enum class FacetKind { Condition, Half, Shape, Club };

struct FacetGroup {
    QString key;                        // "with" / "without", "first" / "second", "a" / "b", the club
    std::vector<int> ordinals;
};
struct SessionFacet {
    FacetKind kind = FacetKind::Condition;
    QString   id;                       // the condition id (Condition), else empty
    int       firedCount = 0;           // Condition: shots it fired on
    std::vector<FacetGroup> groups;
};
// Conditions that fired on some shots, ≥ minEachSide fired and clean, those nearest an even split first.
std::vector<SessionFacet> conditionFacets(const SessionSnapshot &session, int minEachSide = 3);
SessionFacet halfFacet(const SessionSnapshot &session);
std::optional<SessionFacet> clubFacet(const SessionSnapshot &session);       // only when mixed
std::optional<SessionFacet> shapeFacet(const ShapeSplit &split);

} // namespace pinpoint::charts
