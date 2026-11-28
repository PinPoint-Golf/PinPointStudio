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

// COACHING PLAN — pure, header-only, pack-agnostic, no Qt-GUI.
//
// Home's Coaching tab, organised by swing stage from the SAME habit pool as Home's "Your swing"
// tab (swing_areas.h: AreaBand::Habit — assessable >= 3, weighted share >= 0.50, Wilson lower
// bound >= 0.30). Every habit on Your swing (and every area's Biggest fault when it is a habit)
// is accounted for in its swing stage here, ranked by Wilson lower bound (PrioritySummary::score).
//
// SETUP CHECKS ARE NOT SWING THOUGHTS. Static setup habits (P1) sit in their own "Before you swing"
// list to be set before motion begins; "Your focus" picks ONE dynamic (in-motion) drill group from
// Backswing & top, Downswing & impact, or Finish so a static setup habit never hijacks the golfer's
// single swing thought.
//
// GOLFER WORDS ONLY (DC-9, design_system/README.md): stage names use swing terms, never P-codes;
// notes between active habits use authored links gently ("often eases ...", "may follow from ...")
// and never claim causation ("because", "cause", "due to", "leads to").
//
// The pack is marshalled by coaching_plan_pack.h into CoachingConditionInfo, so this file — and its
// test, src/Analysis/tests/coaching_plan_test.cpp — needs no pack.

#include "frequency_words.h"
#include "swing_areas.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

enum class CoachingStage {
    Setup = 0,
    BackswingTop = 1,
    DownswingImpact = 2,
    Finish = 3,
};

inline QString coachingStageKey(CoachingStage s)
{
    switch (s) {
    case CoachingStage::Setup:           return QStringLiteral("setup");
    case CoachingStage::BackswingTop:    return QStringLiteral("backswingTop");
    case CoachingStage::DownswingImpact: return QStringLiteral("downswingImpact");
    case CoachingStage::Finish:          return QStringLiteral("finish");
    }
    return QStringLiteral("downswingImpact");
}

inline QString coachingStageLabel(CoachingStage s)
{
    switch (s) {
    case CoachingStage::Setup:           return QStringLiteral("Setup");
    case CoachingStage::BackswingTop:    return QStringLiteral("Backswing & top");
    case CoachingStage::DownswingImpact: return QStringLiteral("Downswing & impact");
    case CoachingStage::Finish:          return QStringLiteral("Finish");
    }
    return QStringLiteral("Downswing & impact");
}

// Map a condition's earliest swing position (1..10, 0 = unplaced) and area group into the four
// golfer-facing coaching stages.
inline CoachingStage coachingStageOf(int when, const QString &group)
{
    if (group == QLatin1String("setup") || when == 1) return CoachingStage::Setup;
    if (when >= 2 && when <= 4)                       return CoachingStage::BackswingTop;
    if (when >= 5 && when <= 7)                       return CoachingStage::DownswingImpact;
    if (when >= 8)                                    return CoachingStage::Finish;
    if (group == QLatin1String("arms") || group == QLatin1String("planePath"))
        return CoachingStage::BackswingTop;
    return CoachingStage::DownswingImpact;
}

inline QString coachingCapitalised(const QString &s)
{
    if (s.isEmpty()) return s;
    QString out = s;
    out[0] = out[0].toUpper();
    return out;
}

inline QString coachingLowerFirst(const QString &s)
{
    if (s.isEmpty()) return s;
    QString out = s;
    out[0] = out[0].toLower();
    return out;
}

// What the pack says about one condition, as far as the Coaching tab needs to know.
struct CoachingConditionInfo {
    QString       group;              // ConditionGroup token
    CoachingStage stage  = CoachingStage::DownswingImpact;
    int           when   = 0;         // earliest P-position (1..10), 0 = unplaced
    bool          counts = false;     // Fault or Setup, not held back (ConditionRole::isMovementPriority)
    QString       golfer;             // golfer phrase (fallback: label)
    QString       golferWell;         // ideal phrase
    QString       golferWhy;          // why it matters
    QString       drillId;            // first drill id in the registry, "" when none
    QString       drillLabel;
    QString       drillInstruction;
};

struct CoachingPlanOptions {
    AreaOptions         area;
    PriorityOptions     priority;
    FrequencyThresholds frequency;
    int                 trendMinSessions = 3;
    double              trendDelta       = 0.15;
    int                 maxLinkedNames   = 2;
};

struct CoachingHabitItem {
    QString           conditionId;
    QString           text;           // capitalised golfer phrase
    QString           golferRaw;      // original phrase for natural inline mentions
    CoachingStage     stage = CoachingStage::DownswingImpact;
    int               when  = 0;
    double            share = 0.0;    // recency-weighted fired / assessable
    double            score = 0.0;    // Wilson lower bound on weighted counts
    QString           frequency;      // frequencyWords(share)
    int               trend = 0;      // +1 growing, -1 easing, 0 steady
    std::vector<bool> sessions;       // per judged session, oldest first: seen on >= half
    int               sessionsListed = 0;
    bool              isFocus = false;
    QString           upstreamNote;   // e.g. "May follow from your hips swaying away", "" when none
};

struct CoachingFocus {
    bool                           present = false;
    QString                        title;
    QString                        stageKey;
    QString                        stageLabel;
    QStringList                    aimFor;
    std::vector<CoachingHabitItem> rightNow;
    QString                        why;
    QString                        practiseLabel;
    QString                        practise;
    QString                        reasonShort;
    QString                        focusConditionId;
    QStringList                    conditionIds;
};

struct CoachingStageGroup {
    CoachingStage                  stage = CoachingStage::BackswingTop;
    QString                        key;
    QString                        label;
    std::vector<CoachingHabitItem> items;
};

struct CoachingPlan {
    bool                            ready = false;   // true when sessions is non-empty
    std::vector<CoachingHabitItem>  setupChecks;     // static pre-shot checks (Setup stage)
    CoachingFocus                   focus;           // ONE dynamic swing thought (never Setup)
    std::vector<CoachingStageGroup> stages;          // in-motion stages: Backswing & top, Downswing & impact, Finish
};

namespace coaching_detail {

inline void sessionPipsAndTrend(const PrioritySummary &w, const CoachingPlanOptions &opt,
                                std::vector<bool> &pips, int &trend)
{
    pips.clear();
    trend = 0;
    std::vector<double> rates;
    for (const PriorityPoint &p : w.history) {
        if (p.assessable < opt.area.minAssessable) continue;
        pips.push_back(2 * p.fired >= p.assessable);
        rates.push_back(double(p.fired) / double(p.assessable));
    }
    if (pips.empty()) {
        for (const PriorityPoint &p : w.history) {
            if (p.assessable <= 0) continue;
            pips.push_back(2 * p.fired >= p.assessable);
            rates.push_back(double(p.fired) / double(p.assessable));
        }
    }
    const int len = int(rates.size());
    if (len >= opt.trendMinSessions && len >= 3) {
        double early = 0.0;
        for (int i = 0; i < len - 2; ++i) early += rates[size_t(i)];
        early /= double(len - 2);
        const double late = (rates[size_t(len - 2)] + rates[size_t(len - 1)]) / 2.0;
        if (late > early + opt.trendDelta)      trend = 1;
        else if (late < early - opt.trendDelta) trend = -1;
    }
}

inline bool habitBefore(const CoachingHabitItem &x, const CoachingHabitItem &y)
{
    if (x.score != y.score)                   return x.score > y.score;
    if (x.share != y.share)                   return x.share > y.share;
    if (x.sessionsListed != y.sessionsListed) return x.sessionsListed > y.sessionsListed;
    return x.conditionId < y.conditionId;
}

inline QString joinPhrases(const QStringList &phrases, const QString &conjunction)
{
    if (phrases.isEmpty()) return QString();
    if (phrases.size() == 1) return phrases.front();
    if (phrases.size() == 2) return phrases[0] + QLatin1Char(' ') + conjunction + QLatin1Char(' ') + phrases[1];
    QStringList head = phrases;
    const QString last = head.takeLast();
    return head.join(QStringLiteral(", ")) + QLatin1Char(' ') + conjunction + QLatin1Char(' ') + last;
}

} // namespace coaching_detail

// Build the golfer's stage-organised coaching plan from the session records.
inline CoachingPlan buildCoachingPlan(const std::vector<SessionPriorities> &sessions,
                                      const QHash<QString, CoachingConditionInfo> &info,
                                      const PriorityCauses &causes = PriorityCauses(),
                                      const CoachingPlanOptions &opt = CoachingPlanOptions())
{
    CoachingPlan plan;
    if (sessions.empty()) return plan;
    plan.ready = true;

    const std::vector<PrioritySummary> all = aggregateConditions(sessions, opt.priority);

    // Collect every counted Habit (exact same eligibility as swing_areas.h).
    QHash<QString, CoachingHabitItem> activeHabits;
    std::vector<CoachingHabitItem> setupHabits;
    std::vector<CoachingHabitItem> dynamicHabits;
    for (const PrioritySummary &w : all) {
        const auto it = info.constFind(w.id);
        if (it == info.constEnd() || !it->counts || !areaMeasured(w)) continue;
        if (areaBandOf(w, opt.area, opt.priority.wilsonZ) != AreaBand::Habit) continue;

        CoachingHabitItem item;
        item.conditionId    = w.id;
        item.golferRaw      = !it->golfer.isEmpty() ? it->golfer : (!w.name.isEmpty() ? w.name : w.id);
        item.text           = coachingCapitalised(item.golferRaw);
        item.stage          = it->stage;
        item.when           = it->when;
        item.share          = areaShare(w);
        item.score          = w.score;
        item.frequency      = frequencyWords(item.share, opt.frequency);
        item.sessionsListed = w.sessionsListed;
        coaching_detail::sessionPipsAndTrend(w, opt, item.sessions, item.trend);

        activeHabits.insert(item.conditionId, item);
        if (item.stage == CoachingStage::Setup) setupHabits.push_back(item);
        else                                    dynamicHabits.push_back(item);
    }

    std::sort(setupHabits.begin(), setupHabits.end(), coaching_detail::habitBefore);
    plan.setupChecks = std::move(setupHabits);

    QSet<QString> focusIds;
    if (!dynamicHabits.empty()) {
        // ── Choose ONE dynamic focus (grouped by drill; Setup never enters) ──
        struct DrillGroup {
            QString key;
            QString drillId;
            CoachingStage earliestStage = CoachingStage::Finish;
            int           earliestWhen  = 99;
            double        maxScore      = 0.0;
            std::vector<CoachingHabitItem> items;
        };
        std::vector<DrillGroup> groups;
        for (const CoachingHabitItem &item : dynamicHabits) {
            const CoachingConditionInfo &ci = info.value(item.conditionId);
            const QString key = ci.drillId.isEmpty() ? QStringLiteral("solo:") + item.conditionId : ci.drillId;
            auto git = std::find_if(groups.begin(), groups.end(), [&](const DrillGroup &g) { return g.key == key; });
            if (git == groups.end()) {
                groups.push_back(DrillGroup{ key, ci.drillId, item.stage, item.when > 0 ? item.when : 99, item.score, {} });
                git = groups.end() - 1;
            }
            if (int(item.stage) < int(git->earliestStage)) git->earliestStage = item.stage;
            if (item.when > 0 && item.when < git->earliestWhen) git->earliestWhen = item.when;
            git->maxScore = std::max(git->maxScore, item.score);
            git->items.push_back(item);
        }
        for (DrillGroup &g : groups)
            std::sort(g.items.begin(), g.items.end(), coaching_detail::habitBefore);

        std::sort(groups.begin(), groups.end(), [](const DrillGroup &a, const DrillGroup &b) {
            if (int(a.earliestStage) != int(b.earliestStage)) return int(a.earliestStage) < int(b.earliestStage);
            if (a.earliestWhen != b.earliestWhen)             return a.earliestWhen < b.earliestWhen;
            if (a.drillId.isEmpty() != b.drillId.isEmpty())   return !a.drillId.isEmpty();
            if (a.items.size() != b.items.size())             return a.items.size() > b.items.size();
            if (a.maxScore != b.maxScore)                     return a.maxScore > b.maxScore;
            return a.key < b.key;
        });

        if (!groups.empty()) {
            const DrillGroup &chosen = groups.front();
            CoachingFocus &f = plan.focus;
            f.present    = true;
            f.stageKey   = coachingStageKey(chosen.earliestStage);
            f.stageLabel = coachingStageLabel(chosen.earliestStage);

            QStringList saidPhrases, aim, whys;
            for (const CoachingHabitItem &it : chosen.items) {
                focusIds.insert(it.conditionId);
                f.conditionIds.append(it.conditionId);
                if (!saidPhrases.contains(it.text)) {
                    saidPhrases.append(it.text);
                    CoachingHabitItem copy = it;
                    copy.isFocus = true;
                    f.rightNow.push_back(std::move(copy));
                }
                const CoachingConditionInfo &ci = info.value(it.conditionId);
                const QString well = coachingCapitalised(ci.golferWell);
                if (!well.isEmpty() && !aim.contains(well)) aim.append(well);
                if (!ci.golferWhy.isEmpty() && !whys.contains(ci.golferWhy)) whys.append(ci.golferWhy);
            }
            f.aimFor = aim;
            f.why    = whys.join(QLatin1Char(' '));
            if (!f.rightNow.empty()) f.focusConditionId = f.rightNow.front().conditionId;

            const CoachingConditionInfo &firstInfo = info.value(chosen.items.front().conditionId);
            if (!chosen.drillId.isEmpty() && !firstInfo.drillLabel.isEmpty()) {
                f.title         = firstInfo.drillLabel;
                f.practiseLabel = firstInfo.drillLabel;
                f.practise      = firstInfo.drillInstruction;
            } else {
                f.title = !f.aimFor.isEmpty() ? f.aimFor.front() : chosen.items.front().text;
            }

            // Downstream active habits (in a later stage) linked from any focus habit by an authored edge.
            std::vector<const CoachingHabitItem *> downstream;
            QSet<QString> seenDown;
            for (const auto &edge : causes) {
                if (!focusIds.contains(edge.first) || focusIds.contains(edge.second)) continue;
                if (seenDown.contains(edge.second)) continue;
                const auto dit = activeHabits.constFind(edge.second);
                if (dit == activeHabits.constEnd()) continue;
                if (int(dit->stage) <= int(chosen.earliestStage)) continue;
                seenDown.insert(edge.second);
                downstream.push_back(&dit.value());
            }
            std::sort(downstream.begin(), downstream.end(), [](const CoachingHabitItem *a, const CoachingHabitItem *b) {
                return coaching_detail::habitBefore(*a, *b);
            });
            QStringList downstreamPhrases;
            for (const CoachingHabitItem *d : downstream) {
                if (downstreamPhrases.size() >= opt.maxLinkedNames) break;
                const QString p = coachingLowerFirst(d->golferRaw);
                if (!p.isEmpty() && !downstreamPhrases.contains(p)) downstreamPhrases.append(p);
            }

            QStringList parts;
            parts << QStringLiteral("Starts earliest in your swing");
            if (f.rightNow.size() >= 2)
                parts << QStringLiteral("covers %1 habits").arg(f.rightNow.size());
            if (!downstreamPhrases.isEmpty())
                parts << QStringLiteral("often eases %1").arg(coaching_detail::joinPhrases(downstreamPhrases, QStringLiteral("and")));
            f.reasonShort = parts.join(QStringLiteral(" · "));
        }
    }

    // ── Build all three in-motion stage groups (always shown, even when clean) ──
    const auto upstreamNoteFor = [&](const CoachingHabitItem &target) {
        std::vector<const CoachingHabitItem *> up;
        QSet<QString> seenUp;
        for (const auto &edge : causes) {
            if (edge.second != target.conditionId || edge.first == target.conditionId) continue;
            if (seenUp.contains(edge.first)) continue;
            const auto uit = activeHabits.constFind(edge.first);
            if (uit == activeHabits.constEnd()) continue;
            if (int(uit->stage) >= int(target.stage)) continue;
            if (focusIds.contains(target.conditionId) && focusIds.contains(edge.first)) continue;
            seenUp.insert(edge.first);
            up.push_back(&uit.value());
        }
        if (up.empty()) return QString();
        std::sort(up.begin(), up.end(), [](const CoachingHabitItem *a, const CoachingHabitItem *b) {
            if (int(a->stage) != int(b->stage)) return int(a->stage) < int(b->stage);
            return coaching_detail::habitBefore(*a, *b);
        });
        QStringList phrases;
        for (const CoachingHabitItem *u : up) {
            if (phrases.size() >= opt.maxLinkedNames) break;
            const QString p = coachingLowerFirst(u->golferRaw);
            if (!p.isEmpty() && !phrases.contains(p)) phrases.append(p);
        }
        return phrases.isEmpty()
            ? QString()
            : QStringLiteral("May follow from %1").arg(coaching_detail::joinPhrases(phrases, QStringLiteral("or")));
    };

    const std::vector<CoachingStage> order{
        CoachingStage::BackswingTop,
        CoachingStage::DownswingImpact,
        CoachingStage::Finish,
    };
    for (CoachingStage st : order) {
        CoachingStageGroup g;
        g.stage = st;
        g.key   = coachingStageKey(st);
        g.label = coachingStageLabel(st);
        for (const CoachingHabitItem &item : dynamicHabits) {
            if (item.stage != st) continue;
            CoachingHabitItem copy = item;
            copy.isFocus      = focusIds.contains(copy.conditionId);
            copy.upstreamNote = upstreamNoteFor(copy);
            g.items.push_back(std::move(copy));
        }
        std::sort(g.items.begin(), g.items.end(), coaching_detail::habitBefore);
        plan.stages.push_back(std::move(g));
    }

    return plan;
}

} // namespace pinpoint::analysis
