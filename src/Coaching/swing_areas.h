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

// SWING AREAS — pure, header-only, pack-agnostic. Home's "Your swing" tab: one row per area of the
// swing (setup, posture, lateral motion, …), each rated by HOW MANY OF ITS FAULTS ARE HABITS
// (docs/design/ux_refactor_away_from_4_modes.md §5.2, D1 decided 28 Oct: habit counts, no 0–100 score).
//
// THE RULE. Per condition, `share` is the work-ons aggregate's recency-weighted fired / assessable —
// swings, not sessions, each session's swings fading 0.75 per substantive session since. Four bands:
//
//   habit        share ≥ 0.5           the Coaching layer's own bar, "more than half your swings" —
//                                      the threshold "Right now" uses, so the two tabs cannot disagree
//   occasional   0.15 ≤ share < 0.5
//   clean        share < 0.15 AND the Wilson upper bound on the weighted counts < 0.35 — evidently
//                rare, not merely unseen so far
//   unclear      everything else measured: rare so far, on too few swings to say so
//
// An area reads "4 habits · 12 clean". Every number traces to a named fault; one habit and four read
// differently (the worst-condition rule floored both); a clean condition cannot dilute a habit (a
// mean would). TREND is habits gained or cleared against the same counts without the newest
// substantive session. Dropping that session also re-ages every older one (the fade counts
// substantive sessions since), so the trend mixes the new evidence with a re-weighting; it is small
// and it is said in the tooltip.
//
// WHAT COUNTS: Movement and Delivery conditions, under sessionWorkOnsForPack's class rule (held back
// → Excluded). Ball flight is all Outcome, so there its Outcome conditions count instead — and it is
// shown at all only with LAUNCH-MONITOR evidence: a ball-flight condition every one of whose
// detecting measures is an `lm.` key, assessed. "Any ball-flight condition assessed" is not that
// test, because `top` and `sky` are also judged from the camera.
//
// The pack is marshalled by swing_areas_pack.h into AreaConditionInfo, so this file — and its test,
// src/Analysis/tests/swing_areas_test.cpp — needs no pack.

#include "work_ons.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

// The ConditionGroup tokens (characteristic.cpp kGroups), as the record and the pack spell them.
inline const QString &kAreaBallFlight()
{
    static const QString s = QStringLiteral("ballFlight");
    return s;
}

// ADDRESS TO FINISH — the order a coach walks a swing, and the last tie-break between areas. Not the
// ConditionGroup enum's order (which files arms & club before sequence for the editor): the body's
// sequence comes before what the arms and club do with it. Ball flight last: an outcome is where an
// explanation ends.
inline const QStringList &areaGroupOrder()
{
    static const QStringList v{ QStringLiteral("setup"),       QStringLiteral("posture"),
                                QStringLiteral("lateral"),     QStringLiteral("sequence"),
                                QStringLiteral("armsAndClub"), QStringLiteral("release"),
                                QStringLiteral("impact"),      QStringLiteral("finish"),
                                kAreaBallFlight() };
    return v;
}

// What the pack says about one condition, as far as an area needs to know.
struct AreaConditionInfo {
    QString group;      // ConditionGroup token
    QString label;      // the pack's label (coach term)
    QString golfer;     // the golfer's phrase; "" when the pack has none
    QString unit;       // the unit of the condition's FIRST detecting measure — the fallback below
    // Counted toward its area (areaConditionCounts): Movement or Delivery, not held back; in ball
    // flight, its Outcome conditions.
    bool    counts = false;
    // Every detecting measure reads a launch-monitor key (`lm.`) and nothing else. Ball flight's
    // gate: such a condition assessed means a launch monitor was there.
    bool    lmOnly = false;
    // Every detecting measure's unit, by measure id — a multi-signal condition quotes the unit of the
    // measure that DROVE the reading (latestEvidence.measureId), not the first one authored.
    QHash<QString, QString> unitByMeasure;
    // The tail of each detecting measure this condition watches, "high" or "low" (its signals'
    // direction). A condition is one tail of one measure: cupped_at_top and bowed_at_top read the
    // same P4 wrist angle from opposite ends. Absent when the signals watch the measure from both
    // ends, or only through a ratio or an order test — then the condition is graded on both. The
    // session panel's rows take the same side (firing_side.h conditionFiringSide).
    QHash<QString, QString> tailByMeasure;
};

// The counting rule, isolated so it is testable without a pack. `cls` is workOnClassFor()'s answer
// (held back is already Excluded there); `held` is passed again because an Outcome is Excluded
// whether held or not, and a held ball-flight reading must not count either.
inline bool areaConditionCounts(const QString &group, WorkOnClass cls, bool outcome, bool held)
{
    if (held) return false;
    if (cls == WorkOnClass::Movement || cls == WorkOnClass::Delivery) return true;
    return group == kAreaBallFlight() && outcome;
}

// Every number injected, like WorkOnOptions.
inline constexpr double kAreaHabitShare      = 0.5;    // "more than half your swings" (Coaching's presentShare)
inline constexpr double kAreaOccasionalShare = 0.15;
inline constexpr double kAreaCleanUpper      = 0.35;   // clean also needs the Wilson UPPER bound under this
inline constexpr int    kAreaMaxItems        = 5;      // the default; Home lists every item (WorkOnsController)

struct AreaOptions {
    double habitShare      = kAreaHabitShare;
    double occasionalShare = kAreaOccasionalShare;
    double cleanUpper      = kAreaCleanUpper;      // weighted counts, z = WorkOnOptions::wilsonZ (kDiagWilsonZ)
    int    maxItems        = kAreaMaxItems;
};

enum class AreaBand { Habit, Occasional, Clean, Unclear };

inline QString areaBandToString(AreaBand b)
{
    switch (b) {
    case AreaBand::Habit:      return QStringLiteral("habit");
    case AreaBand::Occasional: return QStringLiteral("occasional");
    case AreaBand::Clean:      return QStringLiteral("clean");
    case AreaBand::Unclear:    break;
    }
    return QStringLiteral("unclear");
}

// The share the bands are cut on: recency-weighted fired over recency-weighted assessable. 0 when
// nothing was assessed (a caller only bands measured conditions).
inline double areaShare(const WorkOnSummary &w)
{
    return w.weightedAssessable > 0.0 ? w.weightedFired / w.weightedAssessable : 0.0;
}

// The band, its boundaries as written: exactly habitShare is a habit, exactly occasionalShare is
// occasional, and under it the condition is clean only when the evidence says rare, not "not yet".
inline AreaBand areaBandOf(const WorkOnSummary &w, const AreaOptions &opt = AreaOptions(),
                           double wilsonZ = kDiagWilsonZ)
{
    const double share = areaShare(w);
    if (share >= opt.habitShare)      return AreaBand::Habit;
    if (share >= opt.occasionalShare) return AreaBand::Occasional;
    if (wilsonUpperBound(w.weightedFired, w.weightedAssessable, wilsonZ) < opt.cleanUpper)
        return AreaBand::Clean;
    return AreaBand::Unclear;
}

// One condition in an area.
struct AreaItem {
    QString  conditionId, label, golfer;
    QString  measureId;          // latestEvidence's driving measure; the newest listing's when absent
    QString  corridorShape;      // corridorShapeToString, "" when !hasTypical
    QString  unit;
    AreaBand band  = AreaBand::Unclear;
    double   share = 0.0;        // recency-weighted fired / assessable — what the band is cut on
    int      fired = 0, assessable = 0;   // UNWEIGHTED swingsFired / swingsAssessable: what a golfer can count
    // From latestEvidence — the newest session that assessed it — the median of ALL its assessed
    // readings and the corridor they were graded against (D9). Absent under a mixed-club session, a
    // composite, or a schema-2 record.
    bool     hasTypical = false;
    double   typical    = 0.0;
    double   corridorLo = 0.0, corridorHi = 0.0;
    // The tail of the corridor this condition watches on measureId, "high" or "low"; "" for both
    // (AreaConditionInfo::tailByMeasure). The typical value is graded on this side only, so a
    // cupped wrist never reads as a bowed one's watch.
    QString  tail;
    // On the golfer's work-ons list: "active", "easing", or "new" (Active and listed ONLY in the newest
    // substantive session). "" when never listed or cleared — the every-condition aggregate's status
    // for a never-listed pattern reads Active, so this is derived only when sessionsListed > 0.
    QString  workOn;
    int      sessionsListed = 0;
};

struct AreaSummary {
    QString group;               // ConditionGroup token
    // Any counted condition with an assessed swing. Ball flight: any counted lmOnly condition.
    bool    measurable = false;
    int     measured = 0, habits = 0, occasional = 0, clean = 0, unclear = 0;   // counted conditions
    // habits now − habits without the newest substantive session; 0 when !hasTrend. hasTrend needs
    // ≥ 2 substantive sessions and a measurable area.
    int     habitTrend = 0;
    bool    hasTrend   = false;
    QString topFaultId;          // highest-share Habit or Occasional condition (item order); "" when none
    // Counted conditions on the golfer's work-ons list (workOn non-empty: active, easing or new), and
    // of those the new ones. Over the whole area, not just the items shown.
    int     workOnsActive = 0, workOnsNew = 0;
    // Up to maxItems: habits first, then occasional, then the rest (clean and unclear, all under the
    // occasional bar); by share descending within each, ties by sessionsListed descending, then id.
    std::vector<AreaItem> items;
};

// ── The counts, shared by now and the trend's "before" ──────────────────────────

struct AreaCounts {
    bool   measurable = false;
    int    measured = 0, habits = 0, occasional = 0, clean = 0, unclear = 0;
    double worstShare = 0.0;
};

// A counted condition is MEASURED when some swing assessed it. (The every-condition aggregate only
// holds ids some session assessed, so this is a guard, not a filter.)
inline bool areaMeasured(const WorkOnSummary &w) { return w.swingsAssessable > 0 && w.weightedAssessable > 0.0; }

inline QHash<QString, AreaCounts> areaCountsOf(const std::vector<WorkOnSummary> &conditions,
                                               const QHash<QString, AreaConditionInfo> &info,
                                               const AreaOptions &opt, double wilsonZ)
{
    QHash<QString, AreaCounts> out;
    for (const WorkOnSummary &w : conditions) {
        const auto it = info.constFind(w.id);
        if (it == info.constEnd() || !it->counts || !areaMeasured(w)) continue;
        AreaCounts &c = out[it->group];
        if (it->group != kAreaBallFlight() || it->lmOnly) c.measurable = true;
        ++c.measured;
        switch (areaBandOf(w, opt, wilsonZ)) {
        case AreaBand::Habit:      ++c.habits;     break;
        case AreaBand::Occasional: ++c.occasional; break;
        case AreaBand::Clean:      ++c.clean;      break;
        case AreaBand::Unclear:    ++c.unclear;    break;
        }
        c.worstShare = std::max(c.worstShare, areaShare(w));
    }
    return out;
}

// ── The areas ───────────────────────────────────────────────────────────────────
//
// `sessions` in any order (sorted here as the aggregate sorts them). `info` holds every condition the
// pack authors; an id it does not hold is not in any area. Returned in display order: measurable areas
// first — most habits, then most occasional, then the worst share, then address to finish — then the
// unmeasurable ones in address-to-finish order. The eight body areas are always returned; ball flight
// only when measurable.
inline std::vector<AreaSummary> summariseAreas(const std::vector<SessionWorkOns> &sessions,
                                               const QHash<QString, AreaConditionInfo> &info,
                                               const AreaOptions &opt = AreaOptions(),
                                               const WorkOnOptions &wopt = WorkOnOptions())
{
    std::vector<SessionWorkOns> sorted = sessions;
    std::stable_sort(sorted.begin(), sorted.end(), [](const SessionWorkOns &a, const SessionWorkOns &b) {
        if (a.startMs != b.startMs) return a.startMs < b.startMs;
        return a.sessionId < b.sessionId;
    });
    int substantive = 0, newest = -1;     // the newest SUBSTANTIVE session, the same one for every area
    for (int i = 0; i < int(sorted.size()); ++i)
        if (sorted[size_t(i)].substantive()) { ++substantive; newest = i; }
    const bool trendable = substantive >= 2;

    // Before: the same counts without the newest substantive session. Computed first, so `sorted`
    // can be handed to the main aggregate afterwards.
    QHash<QString, AreaCounts> before;
    if (trendable) {
        std::vector<SessionWorkOns> without = sorted;
        without.erase(without.begin() + newest);
        before = areaCountsOf(aggregateConditions(std::move(without), wopt), info, opt, wopt.wilsonZ);
    }
    // The aggregate re-sorts a sorted copy, which a stable sort leaves alone: history[i] is sorted[i].
    const std::vector<WorkOnSummary> now = aggregateConditions(sorted, wopt);
    const QHash<QString, AreaCounts> counts = areaCountsOf(now, info, opt, wopt.wilsonZ);

    // Bucket the counted, measured conditions by area.
    QHash<QString, std::vector<const WorkOnSummary *>> byGroup;
    for (const WorkOnSummary &w : now) {
        const auto it = info.constFind(w.id);
        if (it == info.constEnd() || !it->counts || !areaMeasured(w)) continue;
        byGroup[it->group].push_back(&w);
    }

    const QStringList &order = areaGroupOrder();
    std::vector<AreaSummary> out;
    for (const QString &group : order) {
        const AreaCounts c = counts.value(group);
        if (group == kAreaBallFlight() && !c.measurable) continue;   // no launch monitor, no row

        AreaSummary a;
        a.group      = group;
        a.measurable = c.measurable;
        a.measured   = c.measured;
        a.habits     = c.habits;
        a.occasional = c.occasional;
        a.clean      = c.clean;
        a.unclear    = c.unclear;
        a.hasTrend   = trendable && c.measurable;
        a.habitTrend = a.hasTrend ? c.habits - before.value(group).habits : 0;

        std::vector<AreaItem> items;
        for (const WorkOnSummary *w : byGroup.value(group)) {
            const AreaConditionInfo ci = info.value(w->id);
            AreaItem it;
            it.conditionId    = w->id;
            it.label          = !ci.label.isEmpty() ? ci.label : !w->name.isEmpty() ? w->name : w->id;
            it.golfer         = ci.golfer;
            it.band           = areaBandOf(*w, opt, wopt.wilsonZ);
            it.share          = areaShare(*w);
            it.fired          = w->swingsFired;
            it.assessable     = w->swingsAssessable;
            it.sessionsListed = w->sessionsListed;
            it.measureId = (w->hasLatestEvidence && !w->latestEvidence.measureId.isEmpty())
                               ? w->latestEvidence.measureId
                               : (w->hasLatest ? w->latest.measureId : QString());
            it.unit = ci.unitByMeasure.value(it.measureId, ci.unit);
            it.tail = ci.tailByMeasure.value(it.measureId);
            if (w->hasLatestEvidence && w->latestEvidence.hasTypical) {
                it.hasTypical    = true;
                it.typical       = w->latestEvidence.typicalValue;
                it.corridorLo    = w->latestEvidence.corridorLo;
                it.corridorHi    = w->latestEvidence.corridorHi;
                it.corridorShape = corridorShapeToString(w->latestEvidence.corridorShape);
            }
            if (w->sessionsListed > 0) {
                switch (w->status) {
                case WorkOnStatus::Cleared: break;
                case WorkOnStatus::Easing:  it.workOn = QStringLiteral("easing"); break;
                case WorkOnStatus::Active: {
                    const bool onlyNewest = w->sessionsListed == 1 && newest >= 0
                                         && newest < int(w->history.size()) && w->history[size_t(newest)].listed;
                    it.workOn = onlyNewest ? QStringLiteral("new") : QStringLiteral("active");
                    break;
                }
                }
            }
            if (!it.workOn.isEmpty()) ++a.workOnsActive;
            if (it.workOn == QStringLiteral("new")) ++a.workOnsNew;
            items.push_back(std::move(it));
        }

        const auto rank = [](AreaBand b) { return b == AreaBand::Habit ? 0 : b == AreaBand::Occasional ? 1 : 2; };
        std::sort(items.begin(), items.end(), [&rank](const AreaItem &x, const AreaItem &y) {
            if (rank(x.band) != rank(y.band))       return rank(x.band) < rank(y.band);
            if (x.share != y.share)                 return x.share > y.share;
            if (x.sessionsListed != y.sessionsListed) return x.sessionsListed > y.sessionsListed;
            return x.conditionId < y.conditionId;
        });
        if (!items.empty() && (items.front().band == AreaBand::Habit || items.front().band == AreaBand::Occasional))
            a.topFaultId = items.front().conditionId;
        if (int(items.size()) > std::max(opt.maxItems, 0)) items.resize(size_t(std::max(opt.maxItems, 0)));
        a.items = std::move(items);
        out.push_back(std::move(a));
    }

    // Display order. The fixed order is the last key, and unmeasurable areas tie on every other one.
    std::stable_sort(out.begin(), out.end(), [&order, &counts](const AreaSummary &x, const AreaSummary &y) {
        if (x.measurable != y.measurable) return x.measurable;
        if (x.measurable) {
            if (x.habits != y.habits)         return x.habits > y.habits;
            if (x.occasional != y.occasional) return x.occasional > y.occasional;
            const double wx = counts.value(x.group).worstShare, wy = counts.value(y.group).worstShare;
            if (wx != wy)                     return wx > wy;
        }
        return order.indexOf(x.group) < order.indexOf(y.group);
    });
    return out;
}

} // namespace pinpoint::analysis
