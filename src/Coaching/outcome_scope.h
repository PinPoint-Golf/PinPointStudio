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

// THE SHOT FILTER — pure, header-only. Which shots Home's Coaching, Your swing and Matchups read by what the
// ball did, factor by factor (docs/design/shot_outcomes_design.md §7.6): a strike, a direction and a flight
// (shot_factors.h), each "any" or one value. Like the club scope it only chooses which shots the reductions
// see; the rows are never re-graded.
//
// A shot is in a filter when it matches every factor chosen (AND). A factor not read on a shot fails any
// choice for that factor but Any. A factor's values are every one the shot shows, so a fat slice is under
// Fat and under Slice, and a push slice under Push slice, Slice and Push; a toe and a weak strike both. All
// three Any is All: every shot, judged or not.
//
// A key is "all", or "strike:thin|direction:pull|flight:any", the three parts in that order. A value is a
// displayId (shot_factors.h's table), so a left-hander's "slice" is the slice they see.
//
// A filter alone would make a fault that is on every shot look like a cause. So each fault is also set
// against the golfer's other judged shots: how often it fired on the chosen shots, and on the rest. The
// words use Tendencies' 15-point rule; they add a sentence and never re-rank or hide anything.
//
// Unit-tested standalone in src/Analysis/tests/outcome_scope_test.cpp.

#include "diagnostic_ledger.h"
#include "shot_factors.h"
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
// 4: the filter is three factors, strike, direction and flight (shot_factors.h), keyed
// "strike:…|direction:…|flight:…", so every stored key and count is another filter's.
// 5: the rows are the ledger's flight-checked ones (checkStrikesAgainstFlight): an overruled thin, top or fat or
// sky is NotAssessable, so the shots a strike choice holds and the measured counts change.
inline constexpr int kOutcomeScopeRuleVersion = 5;

inline const QString &shotFilterAllKey() { static const QString k = QStringLiteral("all"); return k; }
inline const QString &shotFilterAny()    { static const QString k = QStringLiteral("any"); return k; }

struct ShotFilter {
    QString strike    = shotFilterAny();      // "any" or a strike value ("thin", "solid")
    QString direction = shotFilterAny();      // "any" or a direction value ("pull", "straight", "fade")
    QString flight    = shotFilterAny();      // "any" or a flight value ("high", "sky")

    bool isAll() const { return strike == shotFilterAny() && direction == shotFilterAny() && flight == shotFilterAny(); }

    // "all", or the three parts in order.
    QString key() const
    {
        if (isAll()) return shotFilterAllKey();
        return QStringLiteral("strike:%1|direction:%2|flight:%3").arg(strike, direction, flight);
    }

    const QString &choice(const QString &factor) const
    {
        return factor == shotFactorStrike() ? strike : factor == shotFactorDirection() ? direction : flight;
    }

    // All holds every shot, judged or not. Otherwise every chosen factor must be read and show the value.
    bool contains(const ShotFactors &f) const
    {
        for (const QString &name : shotFactorNames()) {
            const QString &want = choice(name);
            if (want == shotFilterAny()) continue;
            const ShotFactor &part = f.factor(name);
            if (!part.read || !part.values.contains(want)) return false;
        }
        return true;
    }
};

inline bool shotFilterValueOk(const QString &v)
{
    if (v.isEmpty()) return false;
    for (const QChar c : v)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) return false;
    return true;
}

// The filter a key names. A part may be left out (it is Any); anything malformed, a factor named twice or
// one that isn't a factor, reads as All, so a stored key from an older build can never leave the tabs empty.
inline ShotFilter shotFilterFromKey(const QString &key)
{
    ShotFilter f;
    if (key.isEmpty() || key == shotFilterAllKey()) return f;
    QSet<QString> seen;
    for (const QString &part : key.split(QLatin1Char('|'))) {
        const qsizetype colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0) return ShotFilter{};
        const QString factor = part.left(colon), value = part.mid(colon + 1);
        if (!shotFactorNames().contains(factor) || seen.contains(factor) || !shotFilterValueOk(value)) return ShotFilter{};
        seen.insert(factor);
        if (factor == shotFactorStrike())         f.strike = value;
        else if (factor == shotFactorDirection()) f.direction = value;
        else                                      f.flight = value;
    }
    return f;
}

inline QString shotFilterKey(const ShotFilter &f) { return f.key(); }

// The words for a filter's shots, for a sentence that ends in "shots": "pulled, thin", "solid, straight",
// "high". Direction first, then strike, then flight, as a golfer says it. Each value's word is its
// adjective in shot_factors.h's table; a value the table doesn't hold takes its `labels` word, lower case,
// else its id. "" for All.
inline QString filterWords(const ShotFilter &f, const QHash<QString, QString> &labels = {})
{
    QStringList words;
    for (const QString &name : { shotFactorDirection(), shotFactorStrike(), shotFactorFlight() }) {
        const QString &v = f.choice(name);
        if (v == shotFilterAny()) continue;
        if (const ShotFactorValueInfo *info = shotFactorValueInfo(v)) words << info->adjective;
        else words << labels.value(v, v).toLower();
    }
    return words.join(QStringLiteral(", "));
}

// Every shot of one session read factor by factor, keyed by shotId; a shot that isn't judged is there too,
// with nothing read. The session's corrections, intents, evidence and frozen baseline are read exactly as
// reduceTendencies reads them. No club group is asked for: the club scope has already chosen the shots.
inline QHash<int, ShotFactors> judgedShotFactors(const TendencySessionInput &s, const ShotOutcomePack &pack,
                                                 const FlightNorms &flightNorms)
{
    QHash<int, ShotFactors> out;
    const ShotOutcomeSessionFacts facts = tendencySessionFacts(s);
    for (const ShotRecord &shot : s.session.shots)
        out.insert(shot.shotId, judgeTendencyShot(shot, s.session.corrections.value(shot.shotId), s.intents.value(shot.shotId),
                                                  tendencyUsualEntry(s, shot), pack, facts, flightNorms));
    return out;
}

// The shots a filter sees, in ledger order. A shot missing from `judged` has nothing read.
inline std::vector<ShotRecord> shotsInShotFilter(const std::vector<ShotRecord> &shots, const QHash<int, ShotFactors> &judged,
                                                 const ShotFilter &filter)
{
    if (filter.isAll()) return shots;
    std::vector<ShotRecord> out;
    for (const ShotRecord &s : shots)
        if (filter.contains(judged.value(s.shotId))) out.push_back(s);
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
// judged ones (at least one factor read) the filter holds, the other shots the judged ones it doesn't; a
// shot that isn't judged is in neither. A row counts only where it was assessed, so a shot that couldn't
// see the condition says nothing either way. Under All there is nothing to set the shots against, so it is
// empty.
inline QHash<QString, OutcomeContrast> outcomeContrasts(const std::vector<ShotRecord> &shots, const QHash<int, ShotFactors> &judged,
                                                        const ShotFilter &scope, const QSet<QString> &conditionIds)
{
    QHash<QString, OutcomeContrast> out;
    if (scope.isAll()) return out;
    for (const QString &id : conditionIds) out.insert(id, OutcomeContrast{});
    for (const ShotRecord &s : shots) {
        const auto jt = judged.constFind(s.shotId);
        if (jt == judged.constEnd() || !jt->judged()) continue;
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

// The line under a fault: "On your pulled, thin shots 34 of 38 · other shots 12 of 61", then words unless
// the fault is at least 15 points more common on the chosen shots, when the counts say it alone. `label` is
// the choice's words (filterWords: "pulled, thin"). Empty when no chosen shot assessed the fault.
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
