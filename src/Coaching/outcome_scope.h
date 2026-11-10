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

// OUTCOME SCOPE — pure, header-only. Which shots Home's Coaching, Your swing and Matchups read by what the
// ball did: every shot, the good ones, or the ones carrying one miss (docs/design/shot_outcomes_design.md
// §7.6). Like the club scope it only chooses which shots the reductions see; the rows are never re-graded.
//
// A shot is in a choice when the Tendencies rule judges it (judgeTendencyShot in tendencies.h, the one
// helper both read through, so the filter and Tendencies can never disagree about a shot). Good is a clean
// shot. A miss is any shot showing it, so a fat slice is under Fat and under Slice, and a push slice is
// under Push slice, Slice and Push: a combined label brings its two halves. Otherwise a miss another label
// implies (the short a fat strike explains) folds in exactly as Tendencies folds it. The choices' counts can
// so add up to more than the shots. A shot that isn't judged is in no choice but All.
//
// A key is "all", "good" or "miss:slice". The miss is the label as glossed for the golfer's hand
// (displayId), so a left-hander's "Slice" is the slice they see, though the row under it is a hook.
//
// A filter alone would make a fault that is on every shot look like a cause. So each fault is also set
// against the golfer's other judged shots: how often it fired on the chosen shots, and on the rest. The
// words use Tendencies' 15-point rule; they add a sentence and never re-rank or hide anything.
//
// Unit-tested standalone in src/Analysis/tests/outcome_scope_test.cpp.

#include "diagnostic_ledger.h"
#include "tendencies.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace pinpoint::analysis {

// 2: the stored block also counts the chosen shots with the swing measured (a fault row assessed), which the
// evidence line says and Coaching's "too few measured" note reads, so a version-1 file re-reduces.
// 3: a measured thin, top or fat is a miss only when the flight agrees (measuredStrikeFlightAgrees), so the
// shots a miss choice holds change.
inline constexpr int kOutcomeScopeRuleVersion = 3;

inline const QString &outcomeScopeAllKey()  { static const QString k = QStringLiteral("all");  return k; }
inline const QString &outcomeScopeGoodKey() { static const QString k = QStringLiteral("good"); return k; }
inline QString outcomeScopeMissKey(const QString &displayId) { return QStringLiteral("miss:") + displayId; }

struct OutcomeScope {
    QString key = outcomeScopeAllKey();
    bool    good = false;
    QString missId;                     // a displayId ("slice"); empty unless a miss is chosen

    bool isAll() const { return !good && missId.isEmpty(); }

    // All holds every shot, judged or not. Good and a miss hold only judged shots.
    bool contains(const ShotJudgement &j) const
    {
        if (isAll()) return true;
        if (!j.judged) return false;
        return good ? j.good : j.missIds.contains(missId);
    }
};

// The scope a key names. Anything else, a miss with no id or one that isn't an id, reads as All, so a
// stored key from an older build can never leave the tabs empty.
inline OutcomeScope outcomeScopeFromKey(const QString &key)
{
    OutcomeScope scope;
    if (key == outcomeScopeGoodKey()) {
        scope.key  = key;
        scope.good = true;
        return scope;
    }
    static const QString kMiss = QStringLiteral("miss:");
    if (!key.startsWith(kMiss)) return scope;
    const QString id = key.mid(kMiss.size());
    if (id.isEmpty()) return scope;
    for (const QChar c : id)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) return scope;
    scope.key    = outcomeScopeMissKey(id);
    scope.missId = id;
    return scope;
}

// Every shot of one session judged, keyed by shotId; a shot that isn't judged is there too, as not judged.
// The session's corrections, intents, evidence and frozen baseline are read exactly as reduceTendencies
// reads them. No club group is asked for: the club scope has already chosen the shots.
inline QHash<int, ShotJudgement> judgedShotOutcomes(const TendencySessionInput &s, const ShotOutcomePack &pack)
{
    QHash<int, ShotJudgement> out;
    const ShotOutcomeSessionFacts facts = tendencySessionFacts(s);
    for (const ShotRecord &shot : s.session.shots)
        out.insert(shot.shotId, judgeTendencyShot(shot, s.session.corrections.value(shot.shotId), s.intents.value(shot.shotId),
                                                  tendencyUsualEntry(s, shot), pack, facts));
    return out;
}

// The shots a scope sees, in ledger order. A shot missing from `judged` is not judged.
inline std::vector<ShotRecord> shotsInOutcomeScope(const std::vector<ShotRecord> &shots, const QHash<int, ShotJudgement> &judged,
                                                   const OutcomeScope &scope)
{
    if (scope.isAll()) return shots;
    std::vector<ShotRecord> out;
    for (const ShotRecord &s : shots)
        if (scope.contains(judged.value(s.shotId))) out.push_back(s);
    return out;
}

// One condition on the chosen shots against the golfer's other judged shots: `a` fired of `n1` that
// assessed it, `b` of `n0`. Adds up across sessions.
struct OutcomeContrast {
    int a = 0, n1 = 0, b = 0, n0 = 0;

    OutcomeContrast &operator+=(const OutcomeContrast &o)
    {
        a += o.a; n1 += o.n1; b += o.b; n0 += o.n0;
        return *this;
    }
};

// Each condition in `conditionIds`, over `shots` (already in the club scope). The chosen shots are the
// judged ones the scope holds, the other shots the judged ones it doesn't; a shot that isn't judged is in
// neither. A row counts only where it was assessed, so a shot that couldn't see the condition says
// nothing either way. Under All there is nothing to set the shots against, so it is empty.
inline QHash<QString, OutcomeContrast> outcomeContrasts(const std::vector<ShotRecord> &shots, const QHash<int, ShotJudgement> &judged,
                                                        const OutcomeScope &scope, const QSet<QString> &conditionIds)
{
    QHash<QString, OutcomeContrast> out;
    if (scope.isAll()) return out;
    for (const QString &id : conditionIds) out.insert(id, OutcomeContrast{});
    for (const ShotRecord &s : shots) {
        const auto jt = judged.constFind(s.shotId);
        if (jt == judged.constEnd() || !jt->judged) continue;
        const bool chosen = scope.contains(*jt);
        for (const ConditionRow &r : s.rows) {
            if (r.state == ShotState::NotAssessable || !conditionIds.contains(r.conditionId)) continue;
            OutcomeContrast &c = out[r.conditionId];
            const int fired = r.state == ShotState::Fired ? 1 : 0;
            if (chosen) { ++c.n1; c.a += fired; }
            else        { ++c.n0; c.b += fired; }
        }
    }
    return out;
}

// The line under a fault: "On your slice shots 34 of 38 · other shots 12 of 61", then words unless the
// fault is at least 15 points more common on the chosen shots, when the counts say it alone. `label` is
// the choice's short label ("Slice", "Good"). Empty when no chosen shot assessed the fault.
inline QString outcomeContrastLine(const OutcomeContrast &c, const QString &label, const TendencyOptions &opt = {})
{
    if (c.n1 <= 0) return QString();
    const QString counts = QStringLiteral("On your %1 shots %2 of %3 · other shots %4 of %5")
                               .arg(label.toLower(), QString::number(c.a), QString::number(c.n1),
                                    QString::number(c.b), QString::number(c.n0));
    if (c.n0 < opt.minSessionShots) return counts + QStringLiteral(" — too few other shots to compare");

    // A hair under the rule, so exactly 15 points (0.35 against 0.20, say) is not lost to rounding.
    constexpr double kRounding = 1e-9;
    const double p1 = double(c.a) / double(c.n1), p0 = double(c.b) / double(c.n0);
    if (p1 - p0 >= opt.trendDelta - kRounding) return counts;
    if (p0 - p1 >= opt.trendDelta - kRounding) return counts + QStringLiteral(" — less often than on your other shots");
    return counts + QStringLiteral(" — just as often on your other shots");
}

} // namespace pinpoint::analysis
