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

// SHOT FACTORS — pure, header-only. A shot's outcome is three things at once, not one miss: how it was
// STRUCK, which DIRECTION it went and how it FLEW (docs/design/shot_outcomes_design.md §7, Mark, 10 Nov
// 2026: "it's not one or the other, it is all 3"). Each factor has a value on every shot that has what it
// needs, and is "not read" otherwise:
//
//   Strike     Solid · Thin · Fat · Top · Shank · Toe · Heel · Weak strike     faults: all but Solid
//   Direction  Straight · Fade · Draw · Pull · Push · Slice · Hook · Push slice · Pull hook
//                                                                              faults: Pull … Pull hook
//   Flight     Low · Mid · High · Sky                                          fault:  Sky only
//
// STRIKE is the strike-family labels the composer stood up (composeShotOutcome, from the ledger's checked rows,
// so a measured thin, top or fat the flight didn't bear out is already gone, §3.7); Solid when the strike was
// assessed (a strike row read, the flight's overruled one too, or a smash) and no strike label stood. A shot can carry several strike faults (a toe and a weak
// strike). DIRECTION is the direction-family labels, a combined one bringing its two halves
// (tendencyShotMisses); Fade and Draw are values, never faults, an intended shape among them; Straight when
// the direction was assessed and nothing stood. FLIGHT is how high the ball flew for its club, on a
// speed-normalised reading (docs/design/ball-flight-classification.md, flight_norms.json): the landing angle,
// else apex over carry, else the driver's apex against a speed-matched reference (classifyFlight), never on
// launch and never by handicap; a reading the monitor didn't give is modelled from the launch and spin
// (lm_flight_path.h), and says so. Sky replaces High, and only for a driver, a fairway wood or a hybrid: High,
// an attack at least 2° steeper than the golfer's usual for the club that session, and a carry at most
// 85 % of the usual (sessionFlightUsuals): flightSaysSky, the one Sky the ledger and the usual carry read too.
// The flight table, classifyFlight and the Sky rule live in strike_from_flight.h, beside the strike rules.
//
// Ids are as glossed for the golfer's hand (displayId), so a left-hander's Slice is the slice they see.
// Spin, ball speed and a short carry stay on the shot (labels, the carry waterfall) and are not factors.
//
// Unit-tested standalone in src/Analysis/tests/shot_factors_test.cpp.

#include "shot_outcomes.h"
#include "../Core/club_vocabulary.h"
#include "../LaunchMonitor/lm_flight_path.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// ── A shot's misses, every one (moved here from tendencies.h, unchanged) ─────────────────────────────

// The miss labels a tendency can name: what no other label on the shot accounts for (§4.1 step 2). They come
// in the labels' word order (strike, direction, flight), which must never matter to a count or a choice:
// every miss counts the same, and the commonest decides (tendencyLeadMiss).
inline std::vector<const ShotOutcomeLabel *> tendencyMissLabels(const ShotOutcome &o, const ShotOutcomePack &pack)
{
    std::vector<const ShotOutcomeLabel *> out;
    const auto implied = [&o, &pack](const ShotOutcomeLabel &l) {
        for (const ShotOutcomeLabel &other : o.labels) {
            if (other.id == l.id) continue;
            if (pack.physics.value(other.id).contains(l.id)) return true;
            if ((other.id == QLatin1String("pull_hook")  && (l.id == QLatin1String("pull") || l.id == QLatin1String("hook")))
                || (other.id == QLatin1String("push_slice") && (l.id == QLatin1String("push") || l.id == QLatin1String("slice"))))
                return true;
        }
        return false;
    };
    for (const ShotOutcomeLabel &l : o.labels)
        if (l.miss && !implied(l)) out.push_back(&l);
    return out;
}

// The two halves of a combined miss, as ROW ids: a push slice is a slice and a push, a pull hook a hook and a
// pull. Any other miss has none.
inline QStringList tendencyMissHalves(const QString &rowId)
{
    if (rowId == QLatin1String("push_slice")) return { QStringLiteral("slice"), QStringLiteral("push") };
    if (rowId == QLatin1String("pull_hook"))  return { QStringLiteral("hook"), QStringLiteral("pull") };
    return {};
}

// Two ROW ids that are the same miss seen another way: the same id, or a combined miss and one of its halves.
inline bool tendencySameMiss(const QString &a, const QString &b)
{
    return a == b || tendencyMissHalves(a).contains(b) || tendencyMissHalves(b).contains(a);
}

// One miss a shot shows.
struct TendencyShotMiss {
    QString id;                 // the ROW id
    QString displayId;          // as glossed for the golfer's hand
    QString family;             // the row's family
    QString shortText;          // the golfer's words, glossed ("Slice")
};

// Every label in `named`, each followed by the halves of a combined one, once each. The halves are glossed
// like the label, so a left-hander's push slice row (their pull hook) brings their hook and their pull. The
// order is only the labels' word order; every one here counts the same.
inline std::vector<TendencyShotMiss> tendencyShotMisses(const std::vector<const ShotOutcomeLabel *> &named,
                                                        const ShotOutcomePack &pack, const QString &handedness)
{
    std::vector<TendencyShotMiss> out;
    const auto add = [&out](TendencyShotMiss m) {
        if (std::none_of(out.begin(), out.end(), [&m](const TendencyShotMiss &o) { return o.id == m.id; }))
            out.push_back(std::move(m));
    };
    for (const ShotOutcomeLabel *l : named) {
        add(TendencyShotMiss{ l->id, l->displayId, l->family, l->shortText });
        for (const QString &half : tendencyMissHalves(l->id)) {
            const QString displayId = glossedConditionId(half, handedness);
            const ShotConditionFacts *row = outcomeFacts(pack, half);
            const ShotConditionFacts *f = outcomeFacts(pack, displayId);
            if (!f) f = row;
            add(TendencyShotMiss{ half, displayId, row ? row->family : QString(),
                                  !f ? displayId : !f->golferShort.isEmpty() ? f->golferShort : f->label });
        }
    }
    return out;
}

// ── The values, in one table ─────────────────────────────────────────────────────────────────────────

inline const QString &shotFactorStrike()    { static const QString k = QStringLiteral("strike");    return k; }
inline const QString &shotFactorDirection() { static const QString k = QStringLiteral("direction"); return k; }
inline const QString &shotFactorFlight()    { static const QString k = QStringLiteral("flight");    return k; }

// One value a factor can take. `label` is the word a value with no pack condition is shown by (an outcome
// id shows its pack's golferShort, shotFactorValueLabel); `adjective` is what a sentence calls the shots
// ("your pulled, thin shots", filterWords in outcome_scope.h). The order within a factor is the order its
// values are listed and drawn in.
//
// `watch` marks a value that is not a fault but not where a golfer wants to be: amber where a fault is red
// (Mark, 10 Nov 2026: "mid and high are both acceptable, but low is not great but not a fault, if we were
// using RAG ratings it would be Amber"). Only Low, today. A watch value is still a value: it never earns,
// counts or filters as a fault.
//
// `side` is which way a direction value goes, "right" (push, slice, fade…) or "left" (pull, hook, draw…), for a
// right-hander: a left-hander's ids are already their own words (glossed), so the slice side comes first either
// way. "" for Straight and every other factor. The tendency flow (tendency_flow.h) orders its direction column by it.
struct ShotFactorValueInfo {
    QString id, factor, label, adjective;
    bool    fault = false;
    bool    watch = false;
    QString side;
};

inline const std::vector<ShotFactorValueInfo> &shotFactorValueTable()
{
    static const std::vector<ShotFactorValueInfo> k{
        { QStringLiteral("solid"),         shotFactorStrike(),    QStringLiteral("Solid"),       QStringLiteral("solid"),         false },
        { QStringLiteral("thin"),          shotFactorStrike(),    QStringLiteral("Thin"),        QStringLiteral("thin"),          true },
        { QStringLiteral("chunk"),         shotFactorStrike(),    QStringLiteral("Fat"),         QStringLiteral("fat"),           true },
        { QStringLiteral("top"),           shotFactorStrike(),    QStringLiteral("Top"),         QStringLiteral("topped"),        true },
        { QStringLiteral("shank"),         shotFactorStrike(),    QStringLiteral("Shank"),       QStringLiteral("shanked"),       true },
        { QStringLiteral("strike_toe"),    shotFactorStrike(),    QStringLiteral("Toe"),         QStringLiteral("toe"),           true },
        { QStringLiteral("strike_heel"),   shotFactorStrike(),    QStringLiteral("Heel"),        QStringLiteral("heel"),          true },
        { QStringLiteral("smash_deficit"), shotFactorStrike(),    QStringLiteral("Weak strike"), QStringLiteral("weakly struck"), true },
        { QStringLiteral("straight"),      shotFactorDirection(), QStringLiteral("Straight"),    QStringLiteral("straight"),      false },
        { QStringLiteral("fade"),          shotFactorDirection(), QStringLiteral("Fade"),        QStringLiteral("faded"),         false, false, QStringLiteral("right") },
        { QStringLiteral("draw"),          shotFactorDirection(), QStringLiteral("Draw"),        QStringLiteral("drawn"),         false, false, QStringLiteral("left") },
        { QStringLiteral("pull"),          shotFactorDirection(), QStringLiteral("Pull"),        QStringLiteral("pulled"),        true, false, QStringLiteral("left") },
        { QStringLiteral("push"),          shotFactorDirection(), QStringLiteral("Push"),        QStringLiteral("pushed"),        true, false, QStringLiteral("right") },
        { QStringLiteral("slice"),         shotFactorDirection(), QStringLiteral("Slice"),       QStringLiteral("sliced"),        true, false, QStringLiteral("right") },
        { QStringLiteral("hook"),          shotFactorDirection(), QStringLiteral("Hook"),        QStringLiteral("hooked"),        true, false, QStringLiteral("left") },
        { QStringLiteral("push_slice"),    shotFactorDirection(), QStringLiteral("Push slice"),  QStringLiteral("push-sliced"),   true, false, QStringLiteral("right") },
        { QStringLiteral("pull_hook"),     shotFactorDirection(), QStringLiteral("Pull hook"),   QStringLiteral("pull-hooked"),   true, false, QStringLiteral("left") },
        { QStringLiteral("low"),           shotFactorFlight(),    QStringLiteral("Low"),         QStringLiteral("low"),           false, true },
        { QStringLiteral("mid"),           shotFactorFlight(),    QStringLiteral("Mid"),         QStringLiteral("mid-height"),    false },
        { QStringLiteral("high"),          shotFactorFlight(),    QStringLiteral("High"),        QStringLiteral("high"),          false },
        { QStringLiteral("sky"),           shotFactorFlight(),    QStringLiteral("Sky"),         QStringLiteral("skied"),         true },
    };
    return k;
}

inline const ShotFactorValueInfo *shotFactorValueInfo(const QString &id)
{
    for (const ShotFactorValueInfo &v : shotFactorValueTable())
        if (v.id == id) return &v;
    return nullptr;
}

// A direction value's side, "right", "left" or "": see ShotFactorValueInfo.
inline QString shotFactorValueSide(const QString &id)
{
    const ShotFactorValueInfo *v = shotFactorValueInfo(id);
    return v ? v->side : QString();
}

// The values on a factor's good path: neither a fault nor one to watch (Solid; Straight, Fade, Draw; Mid, High).
// The tendency flow's ideal path is the three together (Mark, 10 Nov 2026).
inline QSet<QString> shotFactorGoodValues(const QString &factor)
{
    QSet<QString> out;
    for (const ShotFactorValueInfo &v : shotFactorValueTable())
        if (v.factor == factor && !v.fault && !v.watch) out.insert(v.id);
    return out;
}

// A value to watch (amber), not a fault: see ShotFactorValueInfo.
inline bool shotFactorValueWatch(const QString &id)
{
    const ShotFactorValueInfo *v = shotFactorValueInfo(id);
    return v && v->watch && !v->fault;
}

// Where a value sits in its factor's list; values the table doesn't hold go last.
inline int shotFactorValueRank(const QString &id)
{
    const std::vector<ShotFactorValueInfo> &t = shotFactorValueTable();
    for (int i = 0; i < int(t.size()); ++i)
        if (t[size_t(i)].id == id) return i;
    return int(t.size());
}

// The golfer's word for a value: an outcome's own short words from the pack (its golferShort, glossed ids
// being the pack's ids too), else the table's word ("Solid", "Straight", "Low", "Mid", "High"), else the id.
inline QString shotFactorValueLabel(const ShotOutcomePack &pack, const QString &id)
{
    if (const ShotConditionFacts *f = outcomeFacts(pack, id))
        return !f->golferShort.isEmpty() ? f->golferShort : f->label;
    if (const ShotFactorValueInfo *v = shotFactorValueInfo(id)) return v->label;
    return id;
}

// ── The factors of one shot ──────────────────────────────────────────────────────────────────────────

struct ShotFactor {
    bool        read = false;   // the shot had what this factor needs
    QStringList values;         // displayIds, at least one when read ("solid"; "pull", "fade"; "high")
    QStringList faults;         // the values that are faults, in the same order
    // Flight only (spec §5): which method decided ("landing", "apex_carry", "landing (modelled)", "apex";
    // "golfer" for the golfer's own Sky) and the reading it used (degrees, apex/carry, apex/reference); "" and
    // NaN otherwise. A "(modelled)" method was read off the flight model, not the monitor.
    QString     method;
    double      rawValue = std::numeric_limits<double>::quiet_NaN();
};

struct ShotFactors {
    ShotFactor strike, direction, flight;

    bool judged() const { return strike.read || direction.read || flight.read; }
    bool anyFault() const { return !strike.faults.isEmpty() || !direction.faults.isEmpty() || !flight.faults.isEmpty(); }
    const ShotFactor &factor(const QString &name) const
    {
        return name == shotFactorStrike() ? strike : name == shotFactorDirection() ? direction : flight;
    }
};

inline const QStringList &shotFactorNames()
{
    static const QStringList k{ shotFactorStrike(), shotFactorDirection(), shotFactorFlight() };
    return k;
}

// The three factors of one composed shot. `outcome` is composeShotOutcome's for this shot (its corrections,
// intent and session already applied); `norms` the flight table (shippedFlightNorms); `usuals` the session's
// sessionFlightUsuals, which Sky is judged against.
inline ShotFactors shotFactors(const ShotRecord &shot, const ShotOutcome &outcome, const ShotOutcomePack &pack,
                               const FlightNorms &norms, const QHash<QString, ClubFlightUsual> &usuals)
{
    ShotFactors f;
    if (!outcome.ready) return f;

    const auto familyOf = [&pack](const QString &id) {
        const ShotConditionFacts *facts = outcomeFacts(pack, id);
        return facts && facts->layer == ShotConditionLayer::Ball ? facts->family : QString();
    };
    bool strikeAssessed = std::isfinite(shot.smash), directionAssessed = false;
    for (const ConditionRow &r : shot.rows) {
        // A row the flight overruled was read: the monitor gave its height, the flight just disagreed.
        if (r.state == ShotState::NotAssessable && !r.overruled) continue;
        const QString family = familyOf(r.conditionId);
        if (family == shotFactorStrike()) strikeAssessed = true;
        else if (family == shotFactorDirection()) directionAssessed = true;
    }

    // ── Strike: every strike label that stood is a fault; none and assessed is Solid ──
    for (const ShotOutcomeLabel &l : outcome.labels) {
        if (l.family != shotFactorStrike()) continue;
        strikeAssessed = true;          // an inferred or corrected strike was read too
        if (l.miss && !f.strike.faults.contains(l.displayId)) f.strike.faults << l.displayId;
    }
    if (strikeAssessed) {
        f.strike.read = true;
        f.strike.values = f.strike.faults.isEmpty() ? QStringList{ QStringLiteral("solid") } : f.strike.faults;
    }

    // ── Direction: the labels and a combined one's halves; Fade and Draw are values; none is Straight ──
    // No reading means no word on where the ball went (the composer keeps no direction label then). The
    // line an inferred shank was read from is the shank's, not a second thing that went wrong (the composer
    // absorbs it), so a shank's direction is not read.
    std::vector<const ShotOutcomeLabel *> directionLabels, directionMisses;
    bool absorbed = false;
    for (const ShotOutcomeLabel &l : outcome.labels) {
        if (l.family != shotFactorDirection()) continue;
        if (l.absorbed) { absorbed = true; continue; }
        directionLabels.push_back(&l);
        if (l.miss) directionMisses.push_back(&l);
    }
    if (!absorbed && (!directionLabels.empty() || (directionAssessed && !outcome.noReading))) {
        f.direction.read = true;
        for (const TendencyShotMiss &m : tendencyShotMisses(directionMisses, pack, shot.handedness))
            f.direction.faults << m.displayId;
        for (const TendencyShotMiss &m : tendencyShotMisses(directionLabels, pack, shot.handedness))
            f.direction.values << m.displayId;
        if (f.direction.values.isEmpty()) f.direction.values << QStringLiteral("straight");
    }

    // ── Flight: how high, by the first method the shot can use (classifyFlight); Sky replaces High ──
    // A golfer's own word that it was skied stands as it is.
    const bool correctedSky = std::any_of(outcome.labels.begin(), outcome.labels.end(), [](const ShotOutcomeLabel &l) {
        return l.id == QLatin1String("sky") && l.corrected;
    });
    if (correctedSky) {
        f.flight.read = true;
        f.flight.values = f.flight.faults = QStringList{ QStringLiteral("sky") };
        f.flight.method = QStringLiteral("golfer");
        return f;
    }
    // The landing angle and the apex are readings of their own (shotHasLmReading doesn't count them), so
    // the flight asks only for what its method reads.
    const std::optional<FlightReading> reading = classifyFlight(shot, norms);
    if (!reading) return f;
    f.flight.read = true;
    f.flight.method = reading->method;
    f.flight.rawValue = reading->rawValue;
    QString value = reading->value;
    // flightSaysSky's rule, on the reading already in hand.
    if (value == QLatin1String("high") && flightSkied(shot, usuals)) value = QStringLiteral("sky");
    f.flight.values << value;
    if (value == QLatin1String("sky")) f.flight.faults << value;
    return f;
}

// ── One factor's mix, for the band bar ───────────────────────────────────────────────────────────────

// The shots of one club (or one key) by one factor's value. `read` is the shots the factor was read on;
// a shot can be counted under more than one value (a toe and a weak strike), so the counts can add up to
// more than `read`.
struct ShotFactorTally {
    int read = 0, notRead = 0;
    QHash<QString, int>     count;
    QHash<QString, QString> label;
    QSet<QString>           faults;
    void add(const QString &id, const QString &words, bool fault)
    {
        ++count[id];
        label.insert(id, words);
        if (fault) faults.insert(id);
    }
};

// The bar for one factor: its values that are not faults in the table's order ("good" for Solid and
// Straight, "value" for a shape or a height), then its faults most shots first ("main" the commonest,
// "other" the rest, past kShotMixMaxMisses folded into "Other"). Percentages are of the shots read: when
// each shot is counted once they add up to 100 (largest remainder); when one can be in two places they are
// each its own share and need not.
inline ShotMixBar composeFactorMixBar(const ShotFactorTally &t)
{
    ShotMixBar bar;
    bar.notRead = t.notRead;
    bar.judged  = t.read;

    QStringList values, faults;
    for (auto it = t.count.constBegin(); it != t.count.constEnd(); ++it)
        (t.faults.contains(it.key()) ? faults : values) << it.key();
    std::sort(values.begin(), values.end(), [](const QString &a, const QString &b) {
        const int ra = shotFactorValueRank(a), rb = shotFactorValueRank(b);
        return ra != rb ? ra < rb : a < b;
    });
    std::sort(faults.begin(), faults.end(), [&t](const QString &a, const QString &b) {
        const int na = t.count.value(a), nb = t.count.value(b);
        return na != nb ? na > nb : a < b;
    });
    for (const QString &id : values) {
        const bool good = id == QLatin1String("solid") || id == QLatin1String("straight");
        // A value to watch (Low) is its own kind, so every bar draws it amber (Mark, 10 Nov 2026).
        const QString kind = good ? QStringLiteral("good") : shotFactorValueWatch(id) ? QStringLiteral("watch") : QStringLiteral("value");
        bar.parts.push_back(ShotMixPart{ id, t.label.value(id, id), kind, t.count.value(id), 0 });
    }
    int otherCount = 0;
    for (int i = 0; i < int(faults.size()); ++i) {
        if (i >= kShotMixMaxMisses) { otherCount += t.count.value(faults[i]); continue; }
        bar.parts.push_back(ShotMixPart{ faults[i], t.label.value(faults[i], faults[i]),
                                         i == 0 ? QStringLiteral("main") : QStringLiteral("other"), t.count.value(faults[i]), 0 });
    }
    if (otherCount > 0) bar.parts.push_back(ShotMixPart{ QStringLiteral("other"), QStringLiteral("Other"), QStringLiteral("other"), otherCount, 0 });

    int sum = 0;
    for (const ShotMixPart &p : bar.parts) sum += p.count;
    if (bar.judged > 0 && sum == bar.judged) {
        std::vector<std::pair<int, int>> byRemainder;      // (remainder, index)
        int given = 0;
        for (int i = 0; i < int(bar.parts.size()); ++i) {
            const int scaled = bar.parts[size_t(i)].count * 100;
            bar.parts[size_t(i)].percent = scaled / bar.judged;
            given += bar.parts[size_t(i)].percent;
            byRemainder.push_back({ scaled % bar.judged, i });
        }
        std::stable_sort(byRemainder.begin(), byRemainder.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (int i = 0; i < 100 - given; ++i) ++bar.parts[size_t(byRemainder[size_t(i)].second)].percent;
    } else if (bar.judged > 0) {
        for (ShotMixPart &p : bar.parts) p.percent = int(std::lround(100.0 * double(p.count) / double(bar.judged)));
    }

    QStringList words;
    for (const ShotMixPart &p : bar.parts) words << QStringLiteral("%1 %2%").arg(p.label).arg(p.percent);
    if (bar.notRead > 0) words << QStringLiteral("%1 not read").arg(bar.notRead);
    bar.line = words.join(QStringLiteral(" · "));
    return bar;
}

} // namespace pinpoint::analysis
