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

// TENDENCIES — pure, header-only. What a golfer's shots add up to across sessions
// (docs/design/shot_outcomes_design.md §7.1). One row per (club, swing length), and in it three factors,
// the shot's STRIKE, DIRECTION and FLIGHT (shot_factors.h): each one's usual, how often, what its fault is
// seen with, and which way it is going. A shot is all three at once, so no factor outranks another.
//
// Every shot is RE-COMPOSED here from its ledger row, the session's stored evidence block and the pack's
// adjacency (composeShotOutcome, the same composer as the panel), so a correction a golfer makes takes
// effect at the next reduction without re-deriving a session. Rows are never mirrored; the composer's
// words are (Contract D).
//
// WHICH SHOTS COUNT: a shot with at least one factor read (ShotFactors::judged), an intended shape among
// them (its Fade or Draw is a direction value). A shot with a data warning that withheld nothing still
// counts: a broken recording's rows are NotAssessable, so it falls out as not read. The rule is
// judgeTendencyShot, which Home's shot filter (outcome_scope.h) reads shots through too.
//
// EACH FACTOR'S USUAL is its most common FAULT when that fault earns (over the shots the factor was read
// on: at least 2 sessions of 8 or more, 15 shots, and a recency-weighted share of 25 %), else its most
// common value that is not a fault (Solid, Straight, Fade, Mid…) by plain count, as its bar counts, said
// plainly. A building factor says what has shown up so far: its most common fault if that is on 25 % or
// more of its shots (recency-weighted), else its most common value. Seen with, Also seen, the chain, Play to it, the trend and the work-on belong
// to a factor's fault. Every value a shot shows counts (the halves of a combined miss among them: a push
// slice is a slice and a push too); where a shot can be in one place only (a factor's bar) it goes under
// whichever of its values is commonest with the key (tendencyLeadMiss).
//
// The JSON (<athlete>/tendencies.json) carries a rule and schema version; another version is not read.
//
// Unit-tested standalone in src/Analysis/tests/tendencies_test.cpp.

#include "shot_factors.h"
#include "shot_outcomes.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int kTendencyRuleVersion   = 13;   // 2: a left-hander's chain is glossed, and the item says whose hand; 3: a building row says what has shown up so far; 4: each row carries its shot mix; 9: each club's judged shots counted by outcome (clubOutcomes), Home's outcome filter's choices; 10: the usual miss is the most common, whatever its family; 11: a measured thin, top or fat counts only when the flight agrees (measuredStrikeFlightAgrees); 12: three factors per row, strike, direction and flight (shot_factors.h), and each club's shots counted by factor tuple; 13: a factor's usual value is the most common by plain count (faults stay recency-weighted), and a building factor names a fault only at minShare or more
inline constexpr int kTendencySchemaVersion = 1;

// §7.1 and D5.
struct TendencyOptions {
    int    minSessions     = 2;     // sessions with ≥ minSessionShots in the key
    int    minSessionShots = 8;
    int    minShots        = 15;
    double minShare        = 0.25;  // recency-weighted
    double recency         = 0.75;  // each older session weighs this much less
    double trendDelta      = 0.15;  // the 15-point rule
    int    trendMinJudged  = 3;
    int    recentSessions  = 5;     // what "your last ... shots" means on Home
};

// ── The session's evidence, stored per condition (work_ons.json `outcomeEvidence`) ──────────────────

struct TendencySessionEvidence {
    QHash<QString, Tier>      tiers;            // Pattern and Watching only; absent is Clean
    std::vector<LinkEvidence> links;            // graded links; absent is Unanchored
    QSet<QString>             chainConfirmed;   // conditions with a confirmed chain to the ball
};

// Conditions with a chain to an outcome made entirely of links the session's statistics confirmed
// (the A4 test: Fisher or a declared split). Walked backwards over the confirmed links only, from every
// outcome in the pack.
inline QSet<QString> chainConfirmedConditions(const ShotOutcomePack &pack, const std::vector<LinkEvidence> &links)
{
    QHash<QString, QStringList> parents;
    for (const LinkEvidence &l : links)
        if (l.grade >= LinkGrade::ConditionallyDependent) parents[l.to] << l.from;

    QSet<QString> confirmed;
    QStringList queue;
    for (auto it = pack.conditions.constBegin(); it != pack.conditions.constEnd(); ++it)
        if (it->layer == ShotConditionLayer::Ball) queue << it.key();
    for (int head = 0; head < queue.size(); ++head) {
        const QString at = queue[head];
        for (const QString &p : parents.value(at))
            if (!confirmed.contains(p)) { confirmed.insert(p); queue << p; }
    }
    return confirmed;
}

inline QJsonObject tendencyEvidenceToJson(const TendencySessionEvidence &e)
{
    QJsonObject tiers;
    for (auto it = e.tiers.constBegin(); it != e.tiers.constEnd(); ++it)
        tiers.insert(it.key(), it.value() == Tier::Pattern ? QStringLiteral("pattern") : QStringLiteral("watching"));
    QJsonArray links;
    for (const LinkEvidence &l : e.links)
        links.append(QJsonObject{ { QStringLiteral("from"), l.from }, { QStringLiteral("to"), l.to },
                                  { QStringLiteral("grade"), int(l.grade) } });
    QStringList confirmed(e.chainConfirmed.begin(), e.chainConfirmed.end());
    confirmed.sort();
    return QJsonObject{ { QStringLiteral("tiers"), tiers }, { QStringLiteral("grades"), links },
                        { QStringLiteral("chainConfirmed"), QJsonArray::fromStringList(confirmed) } };
}

inline TendencySessionEvidence tendencyEvidenceFromJson(const QJsonObject &o)
{
    TendencySessionEvidence e;
    const QJsonObject tiers = o.value(QStringLiteral("tiers")).toObject();
    for (auto it = tiers.constBegin(); it != tiers.constEnd(); ++it)
        e.tiers.insert(it.key(), it.value().toString() == QLatin1String("pattern") ? Tier::Pattern : Tier::Watching);
    for (const QJsonValue &v : o.value(QStringLiteral("grades")).toArray()) {
        const QJsonObject l = v.toObject();
        LinkEvidence ev;
        ev.from  = l.value(QStringLiteral("from")).toString();
        ev.to    = l.value(QStringLiteral("to")).toString();
        ev.grade = LinkGrade(l.value(QStringLiteral("grade")).toInt());
        e.links.push_back(std::move(ev));
    }
    for (const QJsonValue &v : o.value(QStringLiteral("chainConfirmed")).toArray())
        e.chainConfirmed.insert(v.toString());
    return e;
}

// diagnostics.json `corrections`: { "<shotId>": [ { family, from, to }, ... ] }.
inline QJsonObject shotCorrectionsToJson(const QHash<int, std::vector<ShotCorrection>> &corrections)
{
    QJsonObject out;
    for (auto it = corrections.constBegin(); it != corrections.constEnd(); ++it) {
        QJsonArray list;
        for (const ShotCorrection &c : it.value())
            list.append(QJsonObject{ { QStringLiteral("family"), c.family }, { QStringLiteral("from"), c.from },
                                     { QStringLiteral("to"), c.to } });
        out.insert(QString::number(it.key()), list);
    }
    return out;
}

inline QHash<int, std::vector<ShotCorrection>> shotCorrectionsFromJson(const QJsonObject &o)
{
    QHash<int, std::vector<ShotCorrection>> out;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        std::vector<ShotCorrection> list;
        for (const QJsonValue &v : it.value().toArray()) {
            const QJsonObject c = v.toObject();
            list.push_back(ShotCorrection{ c.value(QStringLiteral("family")).toString(),
                                           c.value(QStringLiteral("from")).toString(),
                                           c.value(QStringLiteral("to")).toString() });
        }
        if (!list.empty()) out.insert(it.key().toInt(), std::move(list));
    }
    return out;
}

// diagnostics.json `shotIntent`: { "<shotId>": "fade" }. "straight" appears only to override a run set
// by setIntentFrom.
inline QHash<int, QString> shotIntentsFromJson(const QJsonObject &o)
{
    QHash<int, QString> out;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!it.value().toString().isEmpty()) out.insert(it.key().toInt(), it.value().toString());
    return out;
}

// The envelope's intent-from run, resolved onto shots: a shot's own entry wins.
inline QHash<int, QString> resolvedShotIntents(QHash<int, QString> explicitIntents, const std::vector<ShotRecord> &shots,
                                               int intentFromShot, const QString &intentFromShape)
{
    if (intentFromShot < 0 || intentFromShape.isEmpty()) return explicitIntents;
    for (const ShotRecord &s : shots)
        if (s.shotId >= intentFromShot && !explicitIntents.contains(s.shotId))
            explicitIntents.insert(s.shotId, intentFromShape);
    return explicitIntents;
}

// ── Shared arithmetic ───────────────────────────────────────────────────────────────────────────────

// How far short of the usual this shot carried, 0 when it went past it; NaN when either number is missing.
inline double shotCarryLoss(const ShotRecord &shot, const ClubBaselineEntry *usual)
{
    if (!usual || !std::isfinite(shot.carry) || !std::isfinite(usual->carryMean))
        return std::numeric_limits<double>::quiet_NaN();
    return std::max(0.0, usual->carryMean - shot.carry);
}

// themeFrequency() floors at "more than half"; a tendency earns at a quarter, so it needs words below
// that. The bands from half up use themeFrequency's thresholds and its words.
inline QString tendencyFrequencyWords(double share)
{
    if (share >= 0.995) return QStringLiteral("every swing");
    if (share >= 0.9)   return QStringLiteral("almost every swing");
    if (share >= 0.7)   return QStringLiteral("most swings");
    if (share >= 0.5)  return QStringLiteral("more than half your swings");
    if (share >= 0.4)  return QStringLiteral("fairly often");
    if (share >= 0.25) return QStringLiteral("sometimes");
    return QStringLiteral("rarely");
}

// Irons split by how they are played: long 1-4, mid 5-7, short 8 to the pitching wedge. An iron with no number
// goes in the middle. The pitching wedge is read here, by name, because the context tree holds it under wedges.
inline QString tendencyIronGroup(const QString &club)
{
    const QString c = club.trimmed().toUpper();
    if (c.contains(QLatin1String("PITCH"))) return QStringLiteral("short_irons");
    int digits = 0;
    while (digits < c.size() && c.at(digits).isDigit()) ++digits;
    const int number = digits > 0 ? c.left(digits).toInt() : -1;
    if (number > 0 && number <= 4) return QStringLiteral("long_irons");
    if (number >= 8)               return QStringLiteral("short_irons");
    return QStringLiteral("mid_irons");
}

// The club group's label, and what a sentence calls its shots.
inline int tendencyClubRank(const QString &group)
{
    static const QStringList kOrder = { QStringLiteral("driver"), QStringLiteral("woods"), QStringLiteral("long_irons"),
                                        QStringLiteral("mid_irons"), QStringLiteral("short_irons"), QStringLiteral("wedges") };
    const int i = int(kOrder.indexOf(group));
    return i < 0 ? int(kOrder.size()) : i;
}

inline QString tendencyClubLabel(const QString &group)
{
    if (group == QLatin1String("driver")) return QStringLiteral("Driver");
    if (group == QLatin1String("woods"))  return QStringLiteral("Woods and hybrids");
    if (group == QLatin1String("long_irons"))  return QStringLiteral("Long irons");
    if (group == QLatin1String("mid_irons"))   return QStringLiteral("Mid irons");
    if (group == QLatin1String("short_irons")) return QStringLiteral("Short irons");
    if (group == QLatin1String("wedges")) return QStringLiteral("Wedges");
    return group;
}

inline QString tendencyClubShotWord(const QString &group)
{
    if (group == QLatin1String("driver")) return QStringLiteral("driver");
    if (group == QLatin1String("woods"))  return QStringLiteral("wood and hybrid");
    if (group == QLatin1String("long_irons"))  return QStringLiteral("long iron");
    if (group == QLatin1String("mid_irons"))   return QStringLiteral("mid iron");
    if (group == QLatin1String("short_irons")) return QStringLiteral("short iron");
    if (group == QLatin1String("wedges")) return QStringLiteral("wedge");
    return group;
}

inline QString tendencyClubPluralWord(const QString &group)
{
    if (group == QLatin1String("driver")) return QStringLiteral("driver");
    if (group == QLatin1String("woods"))  return QStringLiteral("woods and hybrids");
    if (group == QLatin1String("long_irons"))  return QStringLiteral("long irons");
    if (group == QLatin1String("mid_irons"))   return QStringLiteral("mid irons");
    if (group == QLatin1String("short_irons")) return QStringLiteral("short irons");
    if (group == QLatin1String("wedges")) return QStringLiteral("wedges");
    return group;
}

inline QString tendencyPluralWord(const QString &word)
{
    if (word.endsWith(QLatin1String("sh")) || word.endsWith(QLatin1String("ch")) || word.endsWith(QLatin1Char('s')))
        return word + QStringLiteral("es");
    return word + QLatin1Char('s');
}

inline QString tendencyArticle(const QString &word)
{
    static const QString kVowels = QStringLiteral("aeiou");
    return (!word.isEmpty() && kVowels.contains(word.at(0).toLower())) ? QStringLiteral("an") : QStringLiteral("a");
}

// ── The structure ───────────────────────────────────────────────────────────────────────────────────

struct TendencySessionInput {
    ClubDistanceSession session;        // folder, day, shots, corrections
    QHash<int, QString>                 intents;        // resolved onto shots (resolvedShotIntents)
    TendencySessionEvidence             evidence;       // from work_ons.json
    QHash<QString, ClubBaselineEntry>   baseline;       // this session's frozen sidecar; keys canonical
};

struct TendencyChainStep {
    QString text;       // golfer words
    QString words;      // the link word to the next step; "" on the last
};

// One factor of a row: its usual, and for a fault everything that belongs to it.
struct TendencyFactor {
    QString factor;             // "strike", "direction", "flight"
    QString value;              // the usual, a displayId ("pull", "solid", "mid"); "" with nothing to name
    QString rowId;              // the ROW id of a fault (the left-hander's gloss undone), for its chain and play-to
    QString label;              // "Pull", "Solid", "Mid"; "-" when nothing can be named
    bool    fault = false;      // the usual is a fault: earned, or (building) the commonest so far at ≥ minShare; never a value
    double  share = 0.0;        // 0..1 of the shots this factor was read on: a fault's recency-weighted, a value's plain count
    QString frequency;          // "fairly often"
    bool    building = true;    // not enough history in this factor for a verdict
    int     sessions = 0;       // sessions with a shot this factor was read on
    int     shots = 0;          // shots it was read on
    QString seenWithId;         // a fault's reason, most often the headline driver of its shots
    QString seenWith;           // golfer words
    QString strength;           // "likely", "possible", ""
    int     confirmedSessions = 0;
    int     trend = 0;          // +1 growing, -1 easing, 0
    QString trendWords;         // "→ steady", "↓ easing", "new", ""
    QString workOn;             // the condition to practise for an earned fault (its seen-with); the controller adds its status
    ShotMixBar mix;             // every shot this factor was read on, once, under its commonest value with the key
    std::vector<TendencyChainStep> chain;       // the fault first, then what leads to it
    QString alsoSeen;
    QString earlyNote;          // a building fault: says it's early, and what would earn it
    QString playTo;             // an earned fault's play-to line, in the golfer's hand
};

struct TendencyItem {
    QString key;            // "7 IRON|full"
    QString clubGroup;      // "mid_irons"; the row's filter
    QString clubId;         // "7 IRON"
    QString club;           // "7 iron"
    QString length;         // "full", "partial"
    QString handedness;     // the golfer's, off the documents: "Left", "Right", ""
    int     sessions = 0;   // sessions with a counted shot in this key
    int     shots = 0;
    TendencyFactor strike, direction, flight;

    const TendencyFactor &factor(const QString &name) const
    {
        return name == shotFactorStrike() ? strike : name == shotFactorDirection() ? direction : flight;
    }
    TendencyFactor &factor(const QString &name)
    {
        return name == shotFactorStrike() ? strike : name == shotFactorDirection() ? direction : flight;
    }
};

struct TendencyProduced {
    QString phrase;
    int     count = 0;
};

// What one condition does to the ball (§7.3, §7.4). Only for a condition the log linked to a result.
struct TendencyOnBall {
    QString conditionId;
    QString text;               // "Behind 7 of your last 13 bad iron shots."
    std::vector<TendencyProduced> produced;
    QString costWords;          // "About 14 yd lost each time it happens"; "" with no carry to compare
    QString tendencyKey;        // the row whose earned fault it feeds (club|length); "" for none
    QString factor;             // which of the row's factors: "strike", "direction", "flight"; "" for none
    QString value;              // that factor's fault, a displayId ("pull"); "" for none
    QString tendencyText;
    QString onTheBall;          // "On the ball — your pull with 7 iron."; "" unless earned or confirmed
};

// One combination of factor values among a club's judged shots, and how many shots had it. Each list is
// that factor's values (displayIds, so a left-hander's are the ones they see); empty when it wasn't read.
struct TendencyFactorTuple {
    QStringList strike, direction, flight;
    int         shots = 0;
};

// Every judged shot with one club, by its factors: Home's shot filter counts its choices from these (a
// choice holds a tuple when every factor it names is among that factor's values).
struct TendencyClubOutcomes {
    QString clubId;             // canonical ("7 IRON")
    int     judged = 0;
    std::vector<TendencyFactorTuple> tuples;    // each distinct combination once; most shots first
};

struct TendencySet {
    int rule   = kTendencyRuleVersion;
    int schema = kTendencySchemaVersion;
    std::vector<TendencyItem>   items;
    std::vector<TendencyOnBall> onBall;
    std::vector<TendencyClubOutcomes> clubOutcomes;     // the bag's order
    int     countedShots = 0;
    int     countedSessions = 0;
    QJsonArray clubDistances;       // the "Your distances" rows, already in their stored shape
    bool    cancelled = false;
};

// ── One club's distances row, as stored and shown ───────────────────────────────────────────────────

inline QJsonObject clubDistanceItemToJson(const ClubDistanceItem &it)
{
    QJsonObject o;
    const auto putNumber = [&o](const char *key, double v) { if (std::isfinite(v)) o[QLatin1String(key)] = v; };
    o[QStringLiteral("club")]      = it.club;
    o[QStringLiteral("label")]     = it.label;
    putNumber("carryMean", it.carryMean);
    putNumber("carrySd", it.carrySd);
    o[QStringLiteral("carryText")] = it.carryText;
    QJsonObject band;
    if (std::isfinite(it.band.lo))   band[QStringLiteral("lo")]   = it.band.lo;
    if (std::isfinite(it.band.hi))   band[QStringLiteral("hi")]   = it.band.hi;
    if (std::isfinite(it.band.mean)) band[QStringLiteral("mean")] = it.band.mean;
    o[QStringLiteral("band")] = band;
    putNumber("totalMean", it.totalMean);
    putNumber("totalSd", it.totalSd);
    putNumber("offlineSd", it.offlineSd);
    putNumber("speedMean", it.speedMean);
    putNumber("speedSd", it.speedSd);
    putNumber("ballSpeedMean", it.ballSpeedMean);
    putNumber("ballSpeedSd", it.ballSpeedSd);
    putNumber("smashMean", it.smashMean);
    putNumber("smashSd", it.smashSd);
    o[QStringLiteral("shots")]    = it.shots;
    o[QStringLiteral("sessions")] = it.sessions;
    o[QStringLiteral("mishits")]  = QJsonObject{ { QStringLiteral("chunk"), it.mishits.chunk },
                                                 { QStringLiteral("thin"),  it.mishits.thin },
                                                 { QStringLiteral("top"),   it.mishits.top },
                                                 { QStringLiteral("sky"),   it.mishits.sky },
                                                 { QStringLiteral("shank"), it.mishits.shank },
                                                 { QStringLiteral("short"), it.mishits.veryShort } };
    QStringList kinds;
    const auto addKind = [&kinds](const char *word, int n) { if (n > 0) kinds << QStringLiteral("%1 %2").arg(QLatin1String(word)).arg(n); };
    addKind("fat", it.mishits.chunk);
    addKind("thin", it.mishits.thin);
    addKind("topped", it.mishits.top);
    addKind("skied", it.mishits.sky);
    addKind("shanked", it.mishits.shank);
    addKind("far short", it.mishits.veryShort);
    o[QStringLiteral("mishitsText")] = it.mishits.total() > 0
        ? QStringLiteral("%1 mishits left out (%2)").arg(it.mishits.total()).arg(kinds.join(QStringLiteral(", ")))
        : QString();
    o[QStringLiteral("fewShots")]     = it.fewShots;
    o[QStringLiteral("fewShotsText")] = it.fewShots ? QStringLiteral("few shots yet") : QString();
    o[QStringLiteral("trend")]        = it.trend;
    o[QStringLiteral("trendWords")]   = it.trendWords;
    o[QStringLiteral("gapWith")]      = it.gapWith;
    putNumber("gapPrev", it.gapPrev);
    putNumber("gapNext", it.gapNext);
    QStringList gaps;
    if (std::isfinite(it.gapPrev)) gaps << QStringLiteral("to %1: %2").arg(it.gapPrevClub, yardsText(it.gapPrev));
    if (std::isfinite(it.gapNext)) gaps << QStringLiteral("to %1: %2").arg(it.gapNextClub, yardsText(it.gapNext));
    o[QStringLiteral("gapsText")] = gaps.isEmpty() ? QString() : QStringLiteral("Gap ") + gaps.join(QStringLiteral(" · "));
    putNumber("shortBelow", it.shortBelow);
    o[QStringLiteral("shortBelowText")] = std::isfinite(it.shortBelow)
        ? QStringLiteral("Carry under %1 — your average less 1.5 × your spread. Slower club speed counts: "
                         "the shot is judged on where it went.").arg(yardsText(it.shortBelow))
        : QString();
    QJsonArray perSession;
    for (const ClubDistanceSessionPoint &p : it.perSession) {
        QJsonObject s;
        s[QStringLiteral("date")] = p.date;
        if (std::isfinite(p.mean)) s[QStringLiteral("mean")] = p.mean;
        if (std::isfinite(p.sd))   s[QStringLiteral("sd")]   = p.sd;
        s[QStringLiteral("n")] = p.n;
        perSession.append(s);
    }
    o[QStringLiteral("perSession")] = perSession;
    return o;
}

// ── The reduction ───────────────────────────────────────────────────────────────────────────────────

// The one value to file a shot under where it can be in one place only (a row's bar), and the usual of a
// factor: of `missIds`, the one with the biggest `weight` among the key's shots (Tendencies: its
// recency-weighted share; one session: how many shots show it), then the most shots (`count`), then the id.
// No family ranks above another. On an exact tie a combined miss stands over its own halves: the same
// shots, named whole (a push slice, not a push). "" for none.
inline QString tendencyLeadMiss(const QStringList &missIds, const QHash<QString, double> &weight,
                                const QHash<QString, int> &count)
{
    QStringList tied;
    double bestWeight = 0.0;
    int bestCount = 0;
    for (const QString &id : missIds) {
        if (tied.contains(id)) continue;
        const double w = weight.value(id);
        const int n = count.value(id);
        if (tied.isEmpty() || w > bestWeight || (w == bestWeight && n > bestCount)) {
            tied = QStringList{ id };
            bestWeight = w;
            bestCount = n;
        } else if (w == bestWeight && n == bestCount) {
            tied << id;
        }
    }
    QString lead;
    for (const QString &id : tied) {
        const bool halfOfATie = std::any_of(tied.begin(), tied.end(), [&id](const QString &other) {
            return tendencyMissHalves(other).contains(id);
        });
        if (!halfOfATie && (lead.isEmpty() || id < lead)) lead = id;
    }
    return lead;
}

// ── Which shots count ───────────────────────────────────────────────────────────────────────────────

// The session's facts every shot of it is composed with.
inline ShotOutcomeSessionFacts tendencySessionFacts(const TendencySessionInput &s)
{
    return ShotOutcomeSessionFacts{ s.evidence.links, s.evidence.tiers, sessionFlightUsuals(s.session.shots) };
}

// The session's frozen usual for the shot's club, null when the club has none.
inline const ClubBaselineEntry *tendencyUsualEntry(const TendencySessionInput &s, const ShotRecord &shot)
{
    const auto it = s.baseline.constFind(canonicalClubId(shot.club));
    return it == s.baseline.constEnd() ? nullptr : &*it;
}

// One shot as the Tendencies rule judges it: composed from its row, corrections and intent, then its three
// factors (shotFactors). A shot counts when at least one factor was read (ShotFactors::judged). Tendencies and
// Home's shot filter both read a shot through here, so they can never disagree about a shot. The club group
// is the caller's to check. `outcome`, when given, receives the composed shot.
inline ShotFactors judgeTendencyShot(const ShotRecord &shot, const std::vector<ShotCorrection> &corrections,
                                     const QString &intent, const ClubBaselineEntry *usual,
                                     const ShotOutcomePack &pack, const ShotOutcomeSessionFacts &facts,
                                     const FlightNorms &norms, ShotOutcome *outcome = nullptr)
{
    ShotOutcome local;
    ShotOutcome &o = outcome ? *outcome : local;
    o = composeShotOutcome(shot, corrections, intent, usual, pack, facts);
    return shotFactors(shot, o, pack, norms, facts.flightUsuals);
}

namespace tendency_detail {

struct JudgedShot {
    int     session = 0;
    QString key, group, clubId;
    QString handedness;
    ShotFactors factors;
    QString driverId, driverText;
    bool    confirmed = false;
    QString mixKey;
    double  loss = std::numeric_limits<double>::quiet_NaN();
    QStringList firedConditions;
};

inline double weightedShare(const std::vector<std::pair<int, int>> &perSession, double recency)
{
    // (hits, shots) per session, oldest first; each older session counts recency^k.
    double num = 0.0, den = 0.0, w = 1.0;
    for (auto it = perSession.rbegin(); it != perSession.rend(); ++it) {
        num += w * double(it->first);
        den += w * double(it->second);
        w *= recency;
    }
    return den > 0.0 ? num / den : 0.0;
}

inline QString stripClause(QString s)
{
    while (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return outcomeCapitalised(s);
}

// One judged shot added to its club's tuples.
inline void tallyClubOutcomes(QHash<QString, TendencyClubOutcomes> &byClub, const QString &clubId, const ShotFactors &f)
{
    TendencyClubOutcomes &c = byClub[clubId];
    c.clubId = clubId;
    ++c.judged;
    const QStringList strike    = f.strike.read ? f.strike.values : QStringList();
    const QStringList direction = f.direction.read ? f.direction.values : QStringList();
    const QStringList flight    = f.flight.read ? f.flight.values : QStringList();
    auto it = std::find_if(c.tuples.begin(), c.tuples.end(), [&](const TendencyFactorTuple &t) {
        return t.strike == strike && t.direction == direction && t.flight == flight;
    });
    if (it == c.tuples.end()) {
        c.tuples.push_back(TendencyFactorTuple{ strike, direction, flight, 0 });
        it = c.tuples.end() - 1;
    }
    ++it->shots;
}

// The counts in the bag's order (a club the bag doesn't know goes last), each club's tuples most shots first.
inline std::vector<TendencyClubOutcomes> orderedClubOutcomes(const QHash<QString, TendencyClubOutcomes> &byClub)
{
    QStringList clubs = byClub.keys();
    const QStringList &bag = clubVocabulary();
    const auto bagRank = [&bag](const QString &c) { const int i = int(bag.indexOf(c)); return i < 0 ? int(bag.size()) : i; };
    std::sort(clubs.begin(), clubs.end(), [&bagRank](const QString &a, const QString &b) {
        const int ra = bagRank(a), rb = bagRank(b);
        return ra != rb ? ra < rb : a < b;
    });
    const auto tupleKey = [](const TendencyFactorTuple &t) {
        return t.strike.join(QLatin1Char(',')) + QLatin1Char('|') + t.direction.join(QLatin1Char(',')) + QLatin1Char('|')
             + t.flight.join(QLatin1Char(','));
    };
    std::vector<TendencyClubOutcomes> out;
    for (const QString &club : clubs) {
        TendencyClubOutcomes c = byClub.value(club);
        std::sort(c.tuples.begin(), c.tuples.end(), [&tupleKey](const TendencyFactorTuple &a, const TendencyFactorTuple &b) {
            return a.shots != b.shots ? a.shots > b.shots : tupleKey(a) < tupleKey(b);
        });
        out.push_back(std::move(c));
    }
    return out;
}

} // namespace tendency_detail

// `clubGroupOf` maps a stored club to "driver", "woods", "long_irons", "mid_irons", "short_irons", "wedges", or "" for a club left out (a
// putter, an undeclared club). `flightNorms` is the table a shot's height is judged against (shippedFlightNorms;
// an empty one reads no shot's flight). `cancel`, polled between sessions, leaves `.cancelled` set.
inline TendencySet reduceTendencies(const std::vector<TendencySessionInput> &input, const ShotOutcomePack &pack,
                                    const std::function<QString(const QString &)> &clubGroupOf,
                                    const FlightNorms &flightNorms,
                                    const TendencyOptions &opt = {}, const std::atomic<bool> *cancel = nullptr)
{
    using namespace tendency_detail;

    TendencySet out;

    std::vector<const TendencySessionInput *> sessions;
    sessions.reserve(input.size());
    for (const TendencySessionInput &s : input) sessions.push_back(&s);
    std::stable_sort(sessions.begin(), sessions.end(), [](const TendencySessionInput *a, const TendencySessionInput *b) {
        return a->session.day != b->session.day ? a->session.day < b->session.day : a->session.folder < b->session.folder;
    });

    // Every shot that counts, composed from its row, corrections and intent, and read factor by factor.
    std::vector<JudgedShot> judged;
    QSet<int> sessionsCounted;
    QHash<QString, TendencyClubOutcomes> outcomesByClub;
    for (int si = 0; si < int(sessions.size()); ++si) {
        if (cancel && cancel->load()) { out.cancelled = true; return out; }
        const TendencySessionInput &s = *sessions[size_t(si)];
        const ShotOutcomeSessionFacts facts = tendencySessionFacts(s);
        for (const ShotRecord &shot : s.session.shots) {
            const QString group = clubGroupOf ? clubGroupOf(shot.club) : QString();
            if (group.isEmpty()) continue;

            const ClubBaselineEntry *usual = tendencyUsualEntry(s, shot);
            ShotOutcome o;
            const ShotFactors factors = judgeTendencyShot(shot, s.session.corrections.value(shot.shotId), s.intents.value(shot.shotId),
                                                          usual, pack, facts, flightNorms, &o);
            if (!factors.judged()) continue;
            tallyClubOutcomes(outcomesByClub, canonicalClubId(shot.club), factors);

            JudgedShot j;
            j.session = si;
            j.group   = group;
            j.clubId  = canonicalClubId(shot.club);
            j.handedness = shot.handedness;
            j.key     = j.clubId + QLatin1Char('|') + (shot.swingLength == QLatin1String("partial") ? QStringLiteral("partial") : QStringLiteral("full"));
            j.factors = factors;
            j.driverId   = o.driver.id;
            j.driverText = stripClause(o.driver.golfer);
            j.confirmed  = !o.driver.id.isEmpty() && s.evidence.chainConfirmed.contains(o.driver.id);
            j.mixKey     = o.mixKey;
            j.loss       = shotCarryLoss(shot, usual);
            for (const ConditionRow &r : shot.rows) {
                if (r.state != ShotState::Fired) continue;
                const ShotConditionFacts *f = outcomeFacts(pack, r.conditionId);
                if (f && f->layer != ShotConditionLayer::Ball) j.firedConditions << r.conditionId;
            }
            sessionsCounted.insert(si);
            judged.push_back(std::move(j));
        }
    }
    out.countedShots    = int(judged.size());
    out.countedSessions = int(sessionsCounted.size());
    out.clubOutcomes    = orderedClubOutcomes(outcomesByClub);

    // The reasons that were the headline driver of a fault's shots, most often first.
    struct MissReasons {
        QHash<QString, int>        count;
        QHash<QString, QSet<int>>  confirmedIn;
        QHash<QString, QString>    text;
        int                        driven = 0;
        QStringList                ids;
    };
    const auto reasonsBehind = [](const std::vector<const JudgedShot *> &missShots) {
        MissReasons r;
        for (const JudgedShot *j : missShots) {
            if (j->driverId.isEmpty()) continue;
            ++r.driven;
            ++r.count[j->driverId];
            r.text.insert(j->driverId, j->driverText);
            if (j->confirmed) r.confirmedIn[j->driverId].insert(j->session);
        }
        r.ids = r.count.keys();
        std::sort(r.ids.begin(), r.ids.end(), [&r](const QString &a, const QString &b) {
            const int na = r.count.value(a), nb = r.count.value(b);
            return na != nb ? na > nb : a < b;
        });
        return r;
    };

    // The chain, the fault first and up to the reason, in golfer words. The walk is over the rows; the
    // words are the golfer's, so each step is glossed (Contract D). `settled` is false on a building
    // factor: every link says "seen with", because nothing has earned "because" yet.
    const auto chainTo = [&pack](const QString &miss, const QString &reasonId, const QString &reasonText,
                                 const QString &hand, bool settled) {
        const auto wordsFor = [&pack, &hand](const QString &id) { return outcomeFacts(pack, glossedConditionId(id, hand)); };
        std::vector<TendencyChainStep> chain;
        const ShotConditionFacts *missFacts = wordsFor(miss);
        if (!reasonId.isEmpty()) {
            const ShotDriverReach reach = walkDriverReach(reasonId, pack, QSet<QString>{ miss });
            const std::vector<ShotReachStep> path = driverPathTo(reach, miss);
            for (int i = int(path.size()) - 1; i >= 0; --i) {
                const ShotReachStep &step = path[size_t(i)];
                const ShotConditionFacts *to = wordsFor(step.to);
                const ShotConditionFacts *from = wordsFor(step.from);
                const QString text = to ? stripClause(to->golfer) : step.to;
                const QString words = (settled && to && from) ? swingLinkWords(*from, *to, true) : QStringLiteral("seen with");
                chain.push_back(TendencyChainStep{ text, words });
            }
            if (!path.empty()) {
                const ShotConditionFacts *root = wordsFor(path.front().from);
                chain.push_back(TendencyChainStep{ root ? stripClause(root->golfer) : path.front().from, QString() });
            }
        }
        if (chain.empty() && missFacts) {
            const bool nameTheReason = !settled && !reasonText.isEmpty();
            chain.push_back(TendencyChainStep{ stripClause(missFacts->golfer), nameTheReason ? QStringLiteral("seen with") : QString() });
            if (nameTheReason) chain.push_back(TendencyChainStep{ reasonText, QString() });
        }
        return chain;
    };

    // ── Each key ──
    QHash<QString, std::vector<const JudgedShot *>> byKey;
    for (const JudgedShot &j : judged) byKey[j.key].push_back(&j);

    QStringList keys = byKey.keys();
    std::sort(keys.begin(), keys.end(), [&byKey](const QString &a, const QString &b) {
        const JudgedShot *ja = byKey[a].front(), *jb = byKey[b].front();
        const int ra = tendencyClubRank(ja->group), rb = tendencyClubRank(jb->group);
        if (ra != rb) return ra < rb;
        if (ja->clubId != jb->clubId) {             // the bag's own order within a group, longest club first
            const QStringList &order = clubVocabulary();
            const auto rank = [&order](const QString &c) { const int i = int(order.indexOf(c)); return i < 0 ? int(order.size()) : i; };
            const int ca = rank(ja->clubId), cb = rank(jb->clubId);
            return ca != cb ? ca < cb : ja->clubId < jb->clubId;
        }
        return a.endsWith(QLatin1String("full")) && !b.endsWith(QLatin1String("full"));
    });

    for (const QString &key : keys) {
        const std::vector<const JudgedShot *> &keyShots = byKey[key];

        TendencyItem item;
        item.key       = key;
        item.clubGroup = keyShots.front()->group;
        item.clubId    = keyShots.front()->clubId;
        item.club      = clubDistanceLabel(item.clubId);
        item.length    = key.endsWith(QLatin1String("partial")) ? QStringLiteral("partial") : QStringLiteral("full");
        item.shots     = int(keyShots.size());
        {
            QSet<int> in;
            for (const JudgedShot *j : keyShots) in.insert(j->session);
            item.sessions = int(in.size());
        }
        for (auto it = keyShots.rbegin(); it != keyShots.rend(); ++it)     // newest first: an old row may not say
            if (!(*it)->handedness.isEmpty()) { item.handedness = (*it)->handedness; break; }

        for (const QString &name : shotFactorNames()) {
            TendencyFactor &tf = item.factor(name);
            tf.factor = name;

            // The shots this factor was read on; the earn rule and every share are over them.
            std::vector<const JudgedShot *> shots;
            for (const JudgedShot *j : keyShots) if (j->factors.factor(name).read) shots.push_back(j);
            tf.shots = int(shots.size());
            if (shots.empty()) {
                tf.building = false;
                tf.label = QStringLiteral("-");
                tf.mix = composeFactorMixBar(ShotFactorTally{ 0, int(keyShots.size()), {}, {}, {} });
                continue;
            }

            QList<int> keySessions;
            for (const JudgedShot *j : shots) if (!keySessions.contains(j->session)) keySessions << j->session;
            std::sort(keySessions.begin(), keySessions.end());
            QHash<int, int> shotsIn;
            for (const JudgedShot *j : shots) ++shotsIn[j->session];
            tf.sessions = int(keySessions.size());

            QList<int> judgedSessions;
            for (int si : keySessions) if (shotsIn.value(si) >= opt.minSessionShots) judgedSessions << si;
            const bool enoughShots = int(judgedSessions.size()) >= opt.minSessions && int(shots.size()) >= opt.minShots;

            // Every value the factor's shots show, each on its recency-weighted share of them. A fault is
            // what a shot carries as one (a pull, a sky); Solid, Straight, Fade, Mid… are values.
            QStringList faultIds, valueIds;
            QHash<QString, int> count;
            for (const JudgedShot *j : shots) {
                const ShotFactor &f = j->factors.factor(name);
                for (const QString &id : f.values) {
                    if (!count.contains(id)) (f.faults.contains(id) ? faultIds : valueIds) << id;
                    ++count[id];
                }
            }
            std::sort(faultIds.begin(), faultIds.end());
            std::sort(valueIds.begin(), valueIds.end());
            QHash<QString, double> share;
            for (const QString &id : faultIds + valueIds) {
                std::vector<std::pair<int, int>> perSession;
                for (int si : keySessions) {
                    int hit = 0;
                    for (const JudgedShot *j : shots) if (j->session == si && j->factors.factor(name).values.contains(id)) ++hit;
                    perSession.push_back({ hit, shotsIn.value(si) });
                }
                share.insert(id, weightedShare(perSession, opt.recency));
            }
            const QString leadFault = tendencyLeadMiss(faultIds, share, count);
            const auto labelOf = [&pack](const QString &id) { return shotFactorValueLabel(pack, id); };

            // The bar: each shot once, under whichever of its values is commonest with this key.
            ShotFactorTally tally;
            tally.read = int(shots.size());
            tally.notRead = int(keyShots.size() - shots.size());
            for (const JudgedShot *j : shots) {
                const ShotFactor &f = j->factors.factor(name);
                const QString lead = tendencyLeadMiss(f.values, share, count);
                tally.add(lead, labelOf(lead), f.faults.contains(lead));
            }
            tf.mix = composeFactorMixBar(tally);

            // ── The usual: the one place a factor's value, fault and building are chosen ──
            //
            // FAULTS WEIGHTED, VALUES COUNTED (Mark, 10 Nov 2026). A fault is a habit forming or fading, so
            // which fault is usual, and whether it earns, keeps the recency-weighted share above. A value
            // (Solid, Straight, Fade, Draw, Low, Mid, High) is only what the shots were, so it is the one the
            // most shots show, by plain count, as the bar counts: the usual never contradicts the bar beside
            // it (a 7 iron's bar of Mid 58 % · Low 42 % reads Mid, not a recency-weighted Low). A tie goes to
            // the table's order (shotFactorValueRank). Its share and frequency words are that same count over
            // the shots read.
            QString leadValue;
            int leadValueCount = 0;
            for (const QString &id : valueIds) {
                const int n = count.value(id);
                if (leadValue.isEmpty() || n > leadValueCount
                    || (n == leadValueCount && shotFactorValueRank(id) < shotFactorValueRank(leadValue))) {
                    leadValue = id;
                    leadValueCount = n;
                }
            }
            const auto useValue = [&](const QString &id, bool fault) {
                tf.value = id;
                tf.fault = fault;
                tf.label = id.isEmpty() ? QStringLiteral("-") : labelOf(id);
                tf.share = id.isEmpty() ? 0.0
                         : fault        ? share.value(id)
                                        : double(count.value(id)) / double(shots.size());
                tf.frequency = id.isEmpty() ? QString() : tendencyFrequencyWords(tf.share);
                tf.rowId = fault ? glossedConditionId(id, item.handedness) : QString();
            };
            std::vector<const JudgedShot *> faultShots;
            if (!leadFault.isEmpty())
                for (const JudgedShot *j : shots) if (j->factors.factor(name).faults.contains(leadFault)) faultShots.push_back(j);

            // THE BUILDING FLOOR (Mark, 10 Nov 2026). Early on, one fat shot in 29 is not "what has shown up
            // so far"; it is a stray. So a fault is shown at all only when it is on at least minShare of the
            // factor's read shots (recency-weighted, as the earn rule measures share): an earned fault needs
            // that and its history, a building one that alone. Under it the factor shows its most common value
            // (counted, as above), building or not, and never in a fault tone.
            const bool faultShows = !leadFault.isEmpty() && share.value(leadFault) >= opt.minShare;
            tf.building = !enoughShots;
            if (!faultShows) {
                useValue(leadValue, false);
                continue;
            }

            // ── Not enough history for a verdict: what has shown up so far, said as that ──
            if (!enoughShots) {
                useValue(leadFault, true);
                const MissReasons reasons = reasonsBehind(faultShots);
                const QString top = reasons.ids.isEmpty() ? QString() : reasons.ids.first();
                tf.chain = chainTo(tf.rowId, top, reasons.text.value(top), item.handedness, false);
                tf.earlyNote = QStringLiteral("%1 It becomes a tendency after %2 sessions of %3 or more shots.")
                                   .arg(top.isEmpty() ? QStringLiteral("Early days: no reason has stood out yet.")
                                                      : QStringLiteral("Early days: this is what has shown up so far."))
                                   .arg(opt.minSessions).arg(opt.minSessionShots);
                continue;
            }

            // ── A usual fault, earned ──
            useValue(leadFault, true);

            // Seen with: the reason most often the headline driver of this fault's shots.
            const MissReasons reasons = reasonsBehind(faultShots);
            if (!reasons.ids.isEmpty()) {
                tf.seenWithId = reasons.ids.first();
                tf.seenWith   = reasons.text.value(tf.seenWithId);
                tf.workOn     = tf.seenWithId;
                tf.confirmedSessions = int(reasons.confirmedIn.value(tf.seenWithId).size());
                tf.strength   = tf.confirmedSessions >= 2 ? QStringLiteral("likely") : QStringLiteral("possible");
                if (reasons.ids.size() > 1) {
                    const QString &second = reasons.ids.at(1);
                    if (reasons.count.value(second) >= 2 && double(reasons.count.value(second)) >= 0.2 * double(reasons.driven))
                        tf.alsoSeen = QStringLiteral("Also seen: %1.").arg(reasons.text.value(second).toLower());
                }
            }

            // The trend: the last two judged sessions against the rest; under three judged sessions there
            // is no trend to read, only whether the fault was present throughout.
            if (int(judgedSessions.size()) >= opt.trendMinJudged) {
                const int split = int(judgedSessions.size()) - 2;
                int lateHit = 0, lateAll = 0, earlyHit = 0, earlyAll = 0;
                for (int idx = 0; idx < judgedSessions.size(); ++idx) {
                    const int si = judgedSessions.at(idx);
                    int hit = 0;
                    for (const JudgedShot *j : faultShots) if (j->session == si) ++hit;
                    if (idx >= split) { lateHit += hit; lateAll += shotsIn.value(si); }
                    else              { earlyHit += hit; earlyAll += shotsIn.value(si); }
                }
                const double diff = double(lateHit) / std::max(1, lateAll) - double(earlyHit) / std::max(1, earlyAll);
                tf.trend = diff >= opt.trendDelta ? 1 : diff <= -opt.trendDelta ? -1 : 0;
                tf.trendWords = tf.trend > 0 ? QStringLiteral("↑ growing")
                              : tf.trend < 0 ? QStringLiteral("↓ easing") : QStringLiteral("→ steady");
            } else {
                bool present = true, latestOver = false;
                for (int si : judgedSessions) {
                    int hit = 0;
                    for (const JudgedShot *j : faultShots) if (j->session == si) ++hit;
                    const bool over = double(hit) / std::max(1, shotsIn.value(si)) >= opt.minShare;
                    if (!over) present = false;
                    if (si == judgedSessions.last()) latestOver = over;
                }
                // Present throughout is steady, and only a fault that showed up in the latest session is new;
                // one that was there and has gone from the latest session is on its way out.
                tf.trend = (present || latestOver) ? 0 : -1;
                tf.trendWords = present    ? QStringLiteral("→ steady")
                              : latestOver ? QStringLiteral("new") : QStringLiteral("↓ easing");
            }

            tf.chain  = chainTo(tf.rowId, tf.seenWithId, tf.seenWith, item.handedness, true);
            tf.playTo = outcomePlayTo(pack, tf.rowId, item.handedness);
        }

        out.items.push_back(std::move(item));
    }

    // ── Each condition's reach onto the ball, over the recent sessions ──
    const int firstRecent = std::max(0, int(sessions.size()) - opt.recentSessions);
    struct ConditionTally {
        int firings = 0, headline = 0, confirmedHeadline = 0;
        QHash<QString, int> byGroup;
        QHash<QString, int> byPhrase;
        QStringList phraseOrder;
        double lossSum = 0.0;
        int    lossN = 0;
    };
    QHash<QString, ConditionTally> tallies;
    QHash<QString, int> badInGroup;
    for (const JudgedShot &j : judged) {
        if (j.session < firstRecent) continue;
        if (j.factors.anyFault()) ++badInGroup[j.group];
        for (const QString &c : j.firedConditions) ++tallies[c].firings;
        if (j.driverId.isEmpty()) continue;
        ConditionTally &t = tallies[j.driverId];
        ++t.headline;
        if (j.confirmed) ++t.confirmedHeadline;
        ++t.byGroup[j.group];
        if (!j.mixKey.isEmpty()) {
            if (!t.byPhrase.contains(j.mixKey)) t.phraseOrder << j.mixKey;
            ++t.byPhrase[j.mixKey];
        }
        if (std::isfinite(j.loss)) { t.lossSum += j.loss; ++t.lossN; }
    }

    QStringList conditionIds = tallies.keys();
    std::sort(conditionIds.begin(), conditionIds.end());
    for (const QString &id : conditionIds) {
        const ConditionTally &t = tallies[id];
        if (t.headline == 0) continue;

        QStringList groups = t.byGroup.keys();
        std::sort(groups.begin(), groups.end(), [&t](const QString &a, const QString &b) {
            const int na = t.byGroup.value(a), nb = t.byGroup.value(b);
            return na != nb ? na > nb : tendencyClubRank(a) < tendencyClubRank(b);
        });
        const QString group = groups.first();
        const int inGroup = t.byGroup.value(group), bad = badInGroup.value(group);

        TendencyOnBall ball;
        ball.conditionId = id;
        const QString lead = t.confirmedHeadline > 0 ? QStringLiteral("Behind") : QStringLiteral("Seen on");
        ball.text = QStringLiteral("%1 %2 of your last %3 bad %4 shots.").arg(lead).arg(inGroup).arg(bad).arg(tendencyClubShotWord(group));

        QStringList phrases = t.phraseOrder;
        std::stable_sort(phrases.begin(), phrases.end(), [&t](const QString &a, const QString &b) {
            return t.byPhrase.value(a) > t.byPhrase.value(b);
        });
        for (const QString &p : phrases) ball.produced.push_back(TendencyProduced{ outcomeCapitalised(p), t.byPhrase.value(p) });

        if (t.lossN > 0) {
            const double each = t.lossSum / double(t.lossN);
            if (each >= 1.0) ball.costWords = QStringLiteral("About %1 lost each time it happens").arg(yardsText(each));
        }

        // The earned fault it feeds, keyed on (club, factor): the first row and factor seen with it, one
        // confirmed in two sessions before any other, and the line for Home's focus and list.
        const TendencyItem *fedItem = nullptr;
        const TendencyFactor *fed = nullptr;
        for (const TendencyItem &item : out.items) {
            for (const QString &name : shotFactorNames()) {
                const TendencyFactor &tf = item.factor(name);
                if (tf.building || !tf.fault || tf.seenWithId != id) continue;
                if (!fed || (tf.confirmedSessions >= 2 && fed->confirmedSessions < 2)) { fedItem = &item; fed = &tf; }
            }
            if (fed && fed->confirmedSessions >= 2) break;
        }
        if (fed) {
            ball.tendencyKey  = fedItem->key;
            ball.factor       = fed->factor;
            ball.value        = fed->value;
            ball.tendencyText = QStringLiteral("Feeds your “%1: %2” tendency").arg(fedItem->club, fed->label.toLower());
            if (fed->confirmedSessions >= 2)
                ball.onTheBall = QStringLiteral("On the ball — your %1 with %2%3.")
                                     .arg(fed->label.toLower(), fedItem->club.toLower(),
                                          fedItem->length == QLatin1String("partial") ? QStringLiteral(" on part swings") : QString());
        }
        // ... or, with no earned tendency confirmed, a confirmed share of this condition's own firings.
        if (ball.onTheBall.isEmpty() && t.firings > 0
            && double(t.confirmedHeadline) / double(t.firings) >= opt.minShare && !phrases.isEmpty()) {
            const double ofBad = bad > 0 ? double(inGroup) / double(bad) : 0.0;
            QString line = QStringLiteral("On the ball — behind %1 your %2 %3 shots")
                               .arg(ofBad >= 0.5 ? QStringLiteral("most of") : QStringLiteral("some of"),
                                    phrases.first(), tendencyClubShotWord(group));
            if (phrases.size() > 1)
                line += QStringLiteral(", and some of the %1").arg(tendencyPluralWord(phrases.at(1)));
            ball.onTheBall = line + QLatin1Char('.');
        }
        out.onBall.push_back(std::move(ball));
    }

    return out;
}

// ── JSON ─────────────────────────────────────────────────────────────────────────────────────────────

inline QJsonObject tendencyFactorToJson(const TendencyFactor &f)
{
    QJsonArray chain;
    for (const TendencyChainStep &c : f.chain)
        chain.append(QJsonObject{ { QStringLiteral("text"), c.text }, { QStringLiteral("words"), c.words } });
    return QJsonObject{
        { QStringLiteral("factor"), f.factor },
        { QStringLiteral("value"), f.value },
        { QStringLiteral("rowId"), f.rowId },
        { QStringLiteral("label"), f.label },
        { QStringLiteral("fault"), f.fault },
        { QStringLiteral("share"), f.share },
        { QStringLiteral("frequency"), f.frequency },
        { QStringLiteral("building"), f.building },
        { QStringLiteral("sessions"), f.sessions },
        { QStringLiteral("shots"), f.shots },
        { QStringLiteral("seenWithId"), f.seenWithId },
        { QStringLiteral("seenWith"), f.seenWith },
        { QStringLiteral("strength"), f.strength },
        { QStringLiteral("confirmedSessions"), f.confirmedSessions },
        { QStringLiteral("trend"), f.trend },
        { QStringLiteral("trendWords"), f.trendWords },
        { QStringLiteral("workOn"), f.workOn },
        { QStringLiteral("mix"), shotMixBarToJson(f.mix) },
        { QStringLiteral("chain"), chain },
        { QStringLiteral("alsoSeen"), f.alsoSeen },
        { QStringLiteral("earlyNote"), f.earlyNote },
        { QStringLiteral("playTo"), f.playTo },
    };
}

inline TendencyFactor tendencyFactorFromJson(const QJsonObject &o)
{
    const auto str = [&o](const char *k) { return o.value(QLatin1String(k)).toString(); };
    TendencyFactor f;
    f.factor     = str("factor");
    f.value      = str("value");
    f.rowId      = str("rowId");
    f.label      = str("label");
    f.fault      = o.value(QStringLiteral("fault")).toBool();
    f.share      = o.value(QStringLiteral("share")).toDouble();
    f.frequency  = str("frequency");
    f.building   = o.value(QStringLiteral("building")).toBool();
    f.sessions   = o.value(QStringLiteral("sessions")).toInt();
    f.shots      = o.value(QStringLiteral("shots")).toInt();
    f.seenWithId = str("seenWithId");
    f.seenWith   = str("seenWith");
    f.strength   = str("strength");
    f.confirmedSessions = o.value(QStringLiteral("confirmedSessions")).toInt();
    f.trend      = o.value(QStringLiteral("trend")).toInt();
    f.trendWords = str("trendWords");
    f.workOn     = str("workOn");
    f.mix        = shotMixBarFromJson(o.value(QStringLiteral("mix")).toObject());
    for (const QJsonValue &c : o.value(QStringLiteral("chain")).toArray())
        f.chain.push_back(TendencyChainStep{ c.toObject().value(QStringLiteral("text")).toString(),
                                             c.toObject().value(QStringLiteral("words")).toString() });
    f.alsoSeen   = str("alsoSeen");
    f.earlyNote  = str("earlyNote");
    f.playTo     = str("playTo");
    return f;
}

inline QJsonObject tendencySetToJson(const TendencySet &s)
{
    QJsonArray items;
    for (const TendencyItem &it : s.items) {
        items.append(QJsonObject{
            { QStringLiteral("key"), it.key },
            { QStringLiteral("clubGroup"), it.clubGroup },
            { QStringLiteral("clubId"), it.clubId },
            { QStringLiteral("club"), it.club },
            { QStringLiteral("length"), it.length },
            { QStringLiteral("handedness"), it.handedness },
            { QStringLiteral("sessions"), it.sessions },
            { QStringLiteral("shots"), it.shots },
            { QStringLiteral("strike"), tendencyFactorToJson(it.strike) },
            { QStringLiteral("direction"), tendencyFactorToJson(it.direction) },
            { QStringLiteral("flight"), tendencyFactorToJson(it.flight) },
        });
    }
    QJsonArray onBall;
    for (const TendencyOnBall &b : s.onBall) {
        QJsonArray produced;
        for (const TendencyProduced &p : b.produced)
            produced.append(QJsonObject{ { QStringLiteral("phrase"), p.phrase }, { QStringLiteral("count"), p.count } });
        onBall.append(QJsonObject{
            { QStringLiteral("conditionId"), b.conditionId },
            { QStringLiteral("text"), b.text },
            { QStringLiteral("produced"), produced },
            { QStringLiteral("costWords"), b.costWords },
            { QStringLiteral("tendencyKey"), b.tendencyKey },
            { QStringLiteral("factor"), b.factor },
            { QStringLiteral("value"), b.value },
            { QStringLiteral("tendencyText"), b.tendencyText },
            { QStringLiteral("onTheBall"), b.onTheBall },
        });
    }
    QJsonArray clubOutcomes;
    for (const TendencyClubOutcomes &c : s.clubOutcomes) {
        QJsonArray tuples;
        for (const TendencyFactorTuple &t : c.tuples)
            tuples.append(QJsonObject{ { QStringLiteral("strike"), QJsonArray::fromStringList(t.strike) },
                                       { QStringLiteral("direction"), QJsonArray::fromStringList(t.direction) },
                                       { QStringLiteral("flight"), QJsonArray::fromStringList(t.flight) },
                                       { QStringLiteral("shots"), t.shots } });
        clubOutcomes.append(QJsonObject{
            { QStringLiteral("clubId"), c.clubId },
            { QStringLiteral("judged"), c.judged },
            { QStringLiteral("tuples"), tuples },
        });
    }
    return QJsonObject{
        { QStringLiteral("rule"), s.rule },
        { QStringLiteral("schema"), s.schema },
        { QStringLiteral("countedShots"), s.countedShots },
        { QStringLiteral("countedSessions"), s.countedSessions },
        { QStringLiteral("tendencies"), items },
        { QStringLiteral("onBall"), onBall },
        { QStringLiteral("clubOutcomes"), clubOutcomes },
        { QStringLiteral("clubDistances"), s.clubDistances },
    };
}

// Tendencies as of one session (the golfer report): sessions on an earlier day, and those of the same day up to and
// including it by folder order. Undated folders have no place in the order, so one counts only as itself.
inline bool tendencySessionCounts(const QString &folder, const QString &asOfFolder)
{
    if (folder == asOfFolder) return true;
    const QDate day = sessionDayOfFolder(folder), asOf = sessionDayOfFolder(asOfFolder);
    if (!day.isValid() || !asOf.isValid()) return false;
    return day != asOf ? day < asOf : folder < asOfFolder;
}

// The usual distance a Tendencies row shows: the club's full swing carry as stored. A part swing has none.
inline QString tendencyUsualDistance(const QString &clubId, const QString &length, const QJsonArray &clubDistances)
{
    if (length != QLatin1String("full")) return QString();
    for (const QJsonValue &v : clubDistances) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("club")).toString() == clubId) return o.value(QStringLiteral("carryText")).toString();
    }
    return QString();
}

// `*ok` is false for another rule or schema: its numbers are not to be shown as ours.
inline TendencySet tendencySetFromJson(const QJsonObject &root, bool *ok = nullptr)
{
    TendencySet s;
    s.rule   = root.value(QStringLiteral("rule")).toInt(-1);
    s.schema = root.value(QStringLiteral("schema")).toInt(-1);
    const bool good = s.rule == kTendencyRuleVersion && s.schema == kTendencySchemaVersion;
    if (ok) *ok = good;
    if (!good) return s;

    s.countedShots    = root.value(QStringLiteral("countedShots")).toInt();
    s.countedSessions = root.value(QStringLiteral("countedSessions")).toInt();
    s.clubDistances   = root.value(QStringLiteral("clubDistances")).toArray();

    const auto str = [](const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toString(); };
    for (const QJsonValue &v : root.value(QStringLiteral("tendencies")).toArray()) {
        const QJsonObject o = v.toObject();
        TendencyItem it;
        it.key        = str(o, "key");
        it.clubGroup  = str(o, "clubGroup");
        it.clubId     = str(o, "clubId");
        it.club       = str(o, "club");
        it.length     = str(o, "length");
        it.handedness = str(o, "handedness");
        it.sessions   = o.value(QStringLiteral("sessions")).toInt();
        it.shots      = o.value(QStringLiteral("shots")).toInt();
        it.strike     = tendencyFactorFromJson(o.value(QStringLiteral("strike")).toObject());
        it.direction  = tendencyFactorFromJson(o.value(QStringLiteral("direction")).toObject());
        it.flight     = tendencyFactorFromJson(o.value(QStringLiteral("flight")).toObject());
        s.items.push_back(std::move(it));
    }
    for (const QJsonValue &v : root.value(QStringLiteral("onBall")).toArray()) {
        const QJsonObject o = v.toObject();
        TendencyOnBall b;
        b.conditionId  = str(o, "conditionId");
        b.text         = str(o, "text");
        b.costWords    = str(o, "costWords");
        b.tendencyKey  = str(o, "tendencyKey");
        b.factor       = str(o, "factor");
        b.value        = str(o, "value");
        b.tendencyText = str(o, "tendencyText");
        b.onTheBall    = str(o, "onTheBall");
        for (const QJsonValue &p : o.value(QStringLiteral("produced")).toArray())
            b.produced.push_back(TendencyProduced{ str(p.toObject(), "phrase"), p.toObject().value(QStringLiteral("count")).toInt() });
        s.onBall.push_back(std::move(b));
    }
    const auto strings = [](const QJsonValue &v) {
        QStringList out;
        for (const QJsonValue &x : v.toArray()) out << x.toString();
        return out;
    };
    for (const QJsonValue &v : root.value(QStringLiteral("clubOutcomes")).toArray()) {
        const QJsonObject o = v.toObject();
        TendencyClubOutcomes c;
        c.clubId = str(o, "clubId");
        c.judged = o.value(QStringLiteral("judged")).toInt();
        for (const QJsonValue &tv : o.value(QStringLiteral("tuples")).toArray()) {
            const QJsonObject t = tv.toObject();
            c.tuples.push_back(TendencyFactorTuple{ strings(t.value(QStringLiteral("strike"))), strings(t.value(QStringLiteral("direction"))),
                                                    strings(t.value(QStringLiteral("flight"))), t.value(QStringLiteral("shots")).toInt() });
        }
        s.clubOutcomes.push_back(std::move(c));
    }
    return s;
}

} // namespace pinpoint::analysis
