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

// TENDENCIES — pure, header-only. What a golfer's misses add up to across sessions
// (docs/design/shot_outcomes_design.md §7.1). One row per (club group, swing length): the usual miss,
// how often, what it is seen with, and which way it is going.
//
// Every shot is RE-COMPOSED here from its ledger row, the session's stored evidence block and the pack's
// adjacency (composeShotOutcome, the same composer as the panel), so a correction a golfer makes takes
// effect at the next reduction without re-deriving a session. Rows are never mirrored; the composer's
// words are (Contract D).
//
// WHICH SHOTS COUNT: past the positional warm-ups, straight intent, assessable (a launch monitor reading,
// and the ball's shape judged). A shot with a data warning that withheld nothing still counts: a broken
// recording's rows are NotAssessable, so it falls out as not assessable.
//
// The JSON (<athlete>/tendencies.json) carries a rule and schema version; another version is not read.
//
// Unit-tested standalone in src/Analysis/tests/tendencies_test.cpp.

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

inline constexpr int kTendencyRuleVersion   = 4;    // 2: a left-hander's chain is glossed, and the item says whose hand; 3: a building row says what has shown up so far; 4: each row carries its shot mix
inline constexpr int kTendencySchemaVersion = 1;

// §7.1 and D5.
struct TendencyOptions {
    int    minSessions     = 2;     // sessions with ≥ minSessionShots in the key
    int    minSessionShots = 8;
    int    minShots        = 15;
    double minShare        = 0.25;  // recency-weighted
    double recency         = 0.75;  // each older session weighs this much less
    double coOccur         = 0.5;   // a second family on this share of the shots: "often with"
    double trendDelta      = 0.15;  // the 15-point rule
    int    trendMinJudged  = 3;
    int    recentSessions  = 5;     // what "your last ... shots" means on Home
    int    warmUpShots     = kDiagWarmUpShots;
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

// The club group's label, and what a sentence calls its shots.
inline int tendencyClubRank(const QString &group)
{
    static const QStringList kOrder = { QStringLiteral("driver"), QStringLiteral("woods"), QStringLiteral("irons"),
                                        QStringLiteral("wedges") };
    const int i = int(kOrder.indexOf(group));
    return i < 0 ? int(kOrder.size()) : i;
}

inline QString tendencyClubLabel(const QString &group)
{
    if (group == QLatin1String("driver")) return QStringLiteral("Driver");
    if (group == QLatin1String("woods"))  return QStringLiteral("Woods and hybrids");
    if (group == QLatin1String("irons"))  return QStringLiteral("Irons");
    if (group == QLatin1String("wedges")) return QStringLiteral("Wedges");
    return group;
}

inline QString tendencyClubShotWord(const QString &group)
{
    if (group == QLatin1String("driver")) return QStringLiteral("driver");
    if (group == QLatin1String("woods"))  return QStringLiteral("wood and hybrid");
    if (group == QLatin1String("irons"))  return QStringLiteral("iron");
    if (group == QLatin1String("wedges")) return QStringLiteral("wedge");
    return group;
}

inline QString tendencyClubPluralWord(const QString &group)
{
    if (group == QLatin1String("driver")) return QStringLiteral("driver");
    if (group == QLatin1String("woods"))  return QStringLiteral("woods and hybrids");
    if (group == QLatin1String("irons"))  return QStringLiteral("irons");
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

struct TendencyItem {
    QString key;            // "irons|full"
    QString clubGroup;      // "irons"
    QString club;           // "Irons"
    QString length;         // "full", "partial"
    QString handedness;     // the golfer's, off the documents: "Left", "Right", ""
    QString missId;         // the ROW id (for the left-hander's gloss, the display text below has it applied)
    QString miss;           // "Fat, often with a slice"; "No usual miss"
    QString missFamily;
    double  share = 0.0;    // 0..1, recency-weighted
    QString frequency;      // "fairly often"
    bool    building   = true;
    bool    noUsualMiss = false;
    int     sessions = 0;   // sessions with a counted shot in this key
    int     shots = 0;
    QString seenWithId;
    QString seenWith;       // golfer words
    QString strength;       // "likely", "possible", ""
    int     confirmedSessions = 0;
    int     trend = 0;      // +1 growing, -1 easing, 0
    QString trendWords;     // "→ steady", "↓ easing", "new", ""
    std::vector<TendencyChainStep> chain;       // the miss first, then what leads to it
    QString alsoSeen;
    QString earlyNote;                          // a building row: says it's early, and what would earn it
    ShotMixBar mix;                             // every counted shot with this club: good, or its primary miss
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
    QString tendencyKey;        // the earned tendency it feeds; "" for none
    QString tendencyText;
    QString onTheBall;          // "On the ball — your slice with driver."; "" unless earned or confirmed
};

struct TendencySet {
    int rule   = kTendencyRuleVersion;
    int schema = kTendencySchemaVersion;
    std::vector<TendencyItem>   items;
    std::vector<TendencyOnBall> onBall;
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

// The miss labels a tendency can name: what no other label on the shot accounts for (§4.1 step 2).
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

namespace tendency_detail {

struct JudgedShot {
    int     session = 0;
    QString key, group;
    QString handedness;
    QString primaryId, primaryShort, primaryFamily, primaryGolfer;
    QHash<QString, QString> others;     // id -> short text, the misses in another family
    QString driverId, driverText;
    bool    confirmed = false;
    QString mixKey;
    double  loss = std::numeric_limits<double>::quiet_NaN();
    QStringList firedConditions;
};

inline double weightedShare(const std::vector<std::pair<int, int>> &perSession, double recency)
{
    // (misses, shots) per session, oldest first; each older session counts recency^k.
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

} // namespace tendency_detail

// `clubGroupOf` maps a stored club to "driver", "woods", "irons", "wedges", or "" for a club left out (a
// putter, an undeclared club). `cancel`, polled between sessions, leaves `.cancelled` set.
inline TendencySet reduceTendencies(const std::vector<TendencySessionInput> &input, const ShotOutcomePack &pack,
                                    const std::function<QString(const QString &)> &clubGroupOf,
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

    // Every shot that counts, composed from its row, corrections and intent; misses and clean ones alike.
    std::vector<JudgedShot> judged;
    QSet<int> sessionsCounted;
    for (int si = 0; si < int(sessions.size()); ++si) {
        if (cancel && cancel->load()) { out.cancelled = true; return out; }
        const TendencySessionInput &s = *sessions[size_t(si)];
        const ShotOutcomeSessionFacts facts{ s.evidence.links, s.evidence.tiers, sessionFlightUsuals(s.session.shots) };
        for (int pos = 0; pos < int(s.session.shots.size()); ++pos) {
            if (pos < opt.warmUpShots) continue;
            const ShotRecord &shot = s.session.shots[size_t(pos)];
            const QString intent = s.intents.value(shot.shotId);
            if (!intent.isEmpty() && intent != QLatin1String("straight")) continue;
            const QString group = clubGroupOf ? clubGroupOf(shot.club) : QString();
            if (group.isEmpty()) continue;

            const auto baselineIt = s.baseline.constFind(canonicalClubId(shot.club));
            const ClubBaselineEntry *usual = baselineIt == s.baseline.constEnd() ? nullptr : &*baselineIt;
            const ShotOutcome o = composeShotOutcome(shot, s.session.corrections.value(shot.shotId), intent, usual, pack, facts);
            if (!o.ready || o.noReading) continue;
            const std::vector<const ShotOutcomeLabel *> misses = tendencyMissLabels(o, pack);
            if (!o.clean && misses.empty()) continue;      // the ball's shape was not judged

            JudgedShot j;
            j.session = si;
            j.group   = group;
            j.handedness = shot.handedness;
            j.key     = group + QLatin1Char('|') + (shot.swingLength == QLatin1String("partial") ? QStringLiteral("partial") : QStringLiteral("full"));
            if (!misses.empty()) {
                const ShotOutcomeLabel *primary = misses.front();
                j.primaryId     = primary->id;
                j.primaryShort  = primary->shortText;
                j.primaryFamily = primary->family;
                j.primaryGolfer = primary->golfer;
                for (const ShotOutcomeLabel *m : misses)
                    if (m != primary && m->family != primary->family) j.others.insert(m->id, m->shortText);
            }
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

    // ── Each key ──
    QHash<QString, std::vector<const JudgedShot *>> byKey;
    for (const JudgedShot &j : judged) byKey[j.key].push_back(&j);

    QStringList keys = byKey.keys();
    std::sort(keys.begin(), keys.end(), [&byKey](const QString &a, const QString &b) {
        const int ra = tendencyClubRank(byKey[a].front()->group), rb = tendencyClubRank(byKey[b].front()->group);
        if (ra != rb) return ra < rb;
        return a.endsWith(QLatin1String("full")) && !b.endsWith(QLatin1String("full"));
    });

    for (const QString &key : keys) {
        const std::vector<const JudgedShot *> &shots = byKey[key];

        // The sessions with shots in this key, oldest first.
        QList<int> keySessions;
        for (const JudgedShot *j : shots) if (!keySessions.contains(j->session)) keySessions << j->session;
        std::sort(keySessions.begin(), keySessions.end());
        QHash<int, int> shotsIn;
        for (const JudgedShot *j : shots) ++shotsIn[j->session];

        QList<int> judgedSessions;
        for (int si : keySessions) if (shotsIn.value(si) >= opt.minSessionShots) judgedSessions << si;
        const bool enoughShots = int(judgedSessions.size()) >= opt.minSessions && int(shots.size()) >= opt.minShots;

        // Which primary miss weighs most, recency-weighted over the key's sessions.
        QStringList primaries;
        for (const JudgedShot *j : shots) if (!j->primaryId.isEmpty() && !primaries.contains(j->primaryId)) primaries << j->primaryId;
        const auto shareOf = [&](const QString &id) {
            std::vector<std::pair<int, int>> perSession;
            for (int si : keySessions) {
                int hit = 0;
                for (const JudgedShot *j : shots) if (j->session == si && j->primaryId == id) ++hit;
                perSession.push_back({ hit, shotsIn.value(si) });
            }
            return weightedShare(perSession, opt.recency);
        };
        QString missId;
        double bestShare = -1.0;
        int bestCount = 0;
        std::sort(primaries.begin(), primaries.end());
        for (const QString &id : primaries) {
            const double sh = shareOf(id);
            const int n = int(std::count_if(shots.begin(), shots.end(), [&id](const JudgedShot *j) { return j->primaryId == id; }));
            if (sh > bestShare || (sh == bestShare && n > bestCount)) { bestShare = sh; bestCount = n; missId = id; }
        }

        TendencyItem item;
        item.key       = key;
        item.clubGroup = shots.front()->group;
        item.club      = tendencyClubLabel(item.clubGroup);
        item.length    = key.endsWith(QLatin1String("partial")) ? QStringLiteral("partial") : QStringLiteral("full");
        item.sessions  = int(keySessions.size());
        item.shots     = int(shots.size());
        for (auto it = shots.rbegin(); it != shots.rend(); ++it)     // newest first: an old row may not say
            if (!(*it)->handedness.isEmpty()) { item.handedness = (*it)->handedness; break; }
        item.missId    = missId;
        item.share     = std::max(bestShare, 0.0);
        item.frequency = tendencyFrequencyWords(item.share);
        ShotMixTally mixTally;
        for (const JudgedShot *j : shots) {
            if (j->primaryId.isEmpty()) ++mixTally.good;
            else mixTally.addMiss(j->primaryId, j->primaryShort);
        }
        item.mix = composeShotMixBar(mixTally);

        // The reasons that were the headline driver of a miss, most often first.
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

        // The chain, the miss first and up to the reason, in golfer words. The walk is over the rows; the
        // words are the golfer's, so each step is glossed (Contract D). `settled` is false on a building
        // row: every link says "seen with", because nothing has earned "because" yet.
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

        // ── Not enough history for a verdict: what has shown up so far, said as that ──
        if (!enoughShots) {
            item.building = true;
            if (missId.isEmpty()) {
                item.miss = QStringLiteral("-");
            } else {
                std::vector<const JudgedShot *> missShots;
                for (const JudgedShot *j : shots) if (j->primaryId == missId) missShots.push_back(j);
                item.miss = missShots.front()->primaryShort;
                item.missFamily = missShots.front()->primaryFamily;

                const MissReasons reasons = reasonsBehind(missShots);
                const QString top = reasons.ids.isEmpty() ? QString() : reasons.ids.first();
                item.chain = chainTo(missId, top, reasons.text.value(top), item.handedness, false);
                item.earlyNote = QStringLiteral("%1 It becomes a tendency after %2 sessions of %3 or more shots.")
                                     .arg(top.isEmpty() ? QStringLiteral("Early days: no reason has stood out yet.")
                                                        : QStringLiteral("Early days: this is what has shown up so far."))
                                     .arg(opt.minSessions).arg(opt.minSessionShots);
            }
            out.items.push_back(std::move(item));
            continue;
        }

        // ── A usual miss, or the honest absence of one ──
        item.building = false;
        if (missId.isEmpty() || bestShare < opt.minShare) {
            item.noUsualMiss = true;
            item.miss = QStringLiteral("No usual miss");
            item.missId.clear();
            out.items.push_back(std::move(item));
            continue;
        }

        std::vector<const JudgedShot *> missShots;
        for (const JudgedShot *j : shots) if (j->primaryId == missId) missShots.push_back(j);
        const JudgedShot *first = missShots.front();
        item.missFamily = first->primaryFamily;
        item.miss = first->primaryShort;

        // "Fat, often with a slice": the second family on at least half of these shots.
        QHash<QString, int> otherCount;
        QHash<QString, QString> otherShort;
        for (const JudgedShot *j : missShots)
            for (auto it = j->others.constBegin(); it != j->others.constEnd(); ++it) {
                ++otherCount[it.key()];
                otherShort.insert(it.key(), it.value());
            }
        QStringList otherIds = otherCount.keys();
        std::sort(otherIds.begin(), otherIds.end());
        QString bestOther;
        for (const QString &id : otherIds)
            if (bestOther.isEmpty() || otherCount.value(id) > otherCount.value(bestOther)) bestOther = id;
        if (!bestOther.isEmpty() && double(otherCount.value(bestOther)) >= opt.coOccur * double(missShots.size())) {
            const QString w = otherShort.value(bestOther).toLower();
            item.miss += QStringLiteral(", often with %1 %2").arg(tendencyArticle(w), w);
        }

        // Seen with: the reason most often the headline driver of this miss.
        const MissReasons reasons = reasonsBehind(missShots);
        const QHash<QString, int> &driverCount = reasons.count;
        const QHash<QString, QSet<int>> &confirmedIn = reasons.confirmedIn;
        const QHash<QString, QString> &driverText = reasons.text;
        const int drivenShots = reasons.driven;
        const QStringList &driverIds = reasons.ids;
        if (!driverIds.isEmpty()) {
            item.seenWithId = driverIds.first();
            item.seenWith   = driverText.value(item.seenWithId);
            item.confirmedSessions = int(confirmedIn.value(item.seenWithId).size());
            item.strength   = item.confirmedSessions >= 2 ? QStringLiteral("likely") : QStringLiteral("possible");
            if (driverIds.size() > 1) {
                const QString &second = driverIds.at(1);
                if (driverCount.value(second) >= 2 && double(driverCount.value(second)) >= 0.2 * double(drivenShots))
                    item.alsoSeen = QStringLiteral("Also seen: %1.").arg(driverText.value(second).toLower());
            }
        }

        // The trend: the last two judged sessions against the rest; under three judged sessions there
        // is no trend to read, only whether the miss was present throughout.
        if (int(judgedSessions.size()) >= opt.trendMinJudged) {
            const int split = int(judgedSessions.size()) - 2;
            int lateHit = 0, lateAll = 0, earlyHit = 0, earlyAll = 0;
            for (int idx = 0; idx < judgedSessions.size(); ++idx) {
                const int si = judgedSessions.at(idx);
                int hit = 0;
                for (const JudgedShot *j : missShots) if (j->session == si) ++hit;
                if (idx >= split) { lateHit += hit; lateAll += shotsIn.value(si); }
                else              { earlyHit += hit; earlyAll += shotsIn.value(si); }
            }
            const double diff = double(lateHit) / std::max(1, lateAll) - double(earlyHit) / std::max(1, earlyAll);
            item.trend = diff >= opt.trendDelta ? 1 : diff <= -opt.trendDelta ? -1 : 0;
            item.trendWords = item.trend > 0 ? QStringLiteral("↑ growing")
                            : item.trend < 0 ? QStringLiteral("↓ easing") : QStringLiteral("→ steady");
        } else {
            bool present = true, latestOver = false;
            for (int si : judgedSessions) {
                int hit = 0;
                for (const JudgedShot *j : missShots) if (j->session == si) ++hit;
                const bool over = double(hit) / std::max(1, shotsIn.value(si)) >= opt.minShare;
                if (!over) present = false;
                if (si == judgedSessions.last()) latestOver = over;
            }
            // Present throughout is steady, and only a miss that showed up in the latest session is new;
            // one that was there and has gone from the latest session is on its way out.
            item.trend = (present || latestOver) ? 0 : -1;
            item.trendWords = present    ? QStringLiteral("→ steady")
                            : latestOver ? QStringLiteral("new") : QStringLiteral("↓ easing");
        }

        item.chain = chainTo(item.missId, item.seenWithId, item.seenWith, item.handedness, true);

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
        if (!j.primaryId.isEmpty()) ++badInGroup[j.group];
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

        // The tendency it feeds, and the line for Home's focus and list.
        for (const TendencyItem &item : out.items) {
            if (item.building || item.noUsualMiss || item.seenWithId != id) continue;
            ball.tendencyKey  = item.key;
            ball.tendencyText = QStringLiteral("Feeds your “%1: %2” tendency").arg(item.club, item.miss.toLower());
            if (item.confirmedSessions >= 2 && ball.onTheBall.isEmpty()) {
                const QString missWord = item.miss.section(QLatin1Char(','), 0, 0).toLower();
                ball.onTheBall = QStringLiteral("On the ball — your %1 with %2%3.")
                                     .arg(missWord, tendencyClubPluralWord(item.clubGroup),
                                          item.length == QLatin1String("partial") ? QStringLiteral(" on part swings") : QString());
            }
            break;
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

inline QJsonObject tendencySetToJson(const TendencySet &s)
{
    QJsonArray items;
    for (const TendencyItem &it : s.items) {
        QJsonArray chain;
        for (const TendencyChainStep &c : it.chain)
            chain.append(QJsonObject{ { QStringLiteral("text"), c.text }, { QStringLiteral("words"), c.words } });
        items.append(QJsonObject{
            { QStringLiteral("key"), it.key },
            { QStringLiteral("clubGroup"), it.clubGroup },
            { QStringLiteral("club"), it.club },
            { QStringLiteral("length"), it.length },
            { QStringLiteral("handedness"), it.handedness },
            { QStringLiteral("missId"), it.missId },
            { QStringLiteral("miss"), it.miss },
            { QStringLiteral("missFamily"), it.missFamily },
            { QStringLiteral("share"), it.share },
            { QStringLiteral("frequency"), it.frequency },
            { QStringLiteral("building"), it.building },
            { QStringLiteral("noUsualMiss"), it.noUsualMiss },
            { QStringLiteral("sessions"), it.sessions },
            { QStringLiteral("shots"), it.shots },
            { QStringLiteral("seenWithId"), it.seenWithId },
            { QStringLiteral("seenWith"), it.seenWith },
            { QStringLiteral("strength"), it.strength },
            { QStringLiteral("confirmedSessions"), it.confirmedSessions },
            { QStringLiteral("trend"), it.trend },
            { QStringLiteral("trendWords"), it.trendWords },
            { QStringLiteral("chain"), chain },
            { QStringLiteral("alsoSeen"), it.alsoSeen },
            { QStringLiteral("earlyNote"), it.earlyNote },
            { QStringLiteral("mix"), shotMixBarToJson(it.mix) },
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
            { QStringLiteral("tendencyText"), b.tendencyText },
            { QStringLiteral("onTheBall"), b.onTheBall },
        });
    }
    return QJsonObject{
        { QStringLiteral("rule"), s.rule },
        { QStringLiteral("schema"), s.schema },
        { QStringLiteral("countedShots"), s.countedShots },
        { QStringLiteral("countedSessions"), s.countedSessions },
        { QStringLiteral("tendencies"), items },
        { QStringLiteral("onBall"), onBall },
        { QStringLiteral("clubDistances"), s.clubDistances },
    };
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
        it.club       = str(o, "club");
        it.length     = str(o, "length");
        it.handedness = str(o, "handedness");
        it.missId     = str(o, "missId");
        it.miss       = str(o, "miss");
        it.missFamily = str(o, "missFamily");
        it.share      = o.value(QStringLiteral("share")).toDouble();
        it.frequency  = str(o, "frequency");
        it.building   = o.value(QStringLiteral("building")).toBool();
        it.noUsualMiss = o.value(QStringLiteral("noUsualMiss")).toBool();
        it.sessions   = o.value(QStringLiteral("sessions")).toInt();
        it.shots      = o.value(QStringLiteral("shots")).toInt();
        it.seenWithId = str(o, "seenWithId");
        it.seenWith   = str(o, "seenWith");
        it.strength   = str(o, "strength");
        it.confirmedSessions = o.value(QStringLiteral("confirmedSessions")).toInt();
        it.trend      = o.value(QStringLiteral("trend")).toInt();
        it.trendWords = str(o, "trendWords");
        it.alsoSeen   = str(o, "alsoSeen");
        it.earlyNote  = str(o, "earlyNote");
        it.mix        = shotMixBarFromJson(o.value(QStringLiteral("mix")).toObject());
        for (const QJsonValue &c : o.value(QStringLiteral("chain")).toArray())
            it.chain.push_back(TendencyChainStep{ str(c.toObject(), "text"), str(c.toObject(), "words") });
        s.items.push_back(std::move(it));
    }
    for (const QJsonValue &v : root.value(QStringLiteral("onBall")).toArray()) {
        const QJsonObject o = v.toObject();
        TendencyOnBall b;
        b.conditionId  = str(o, "conditionId");
        b.text         = str(o, "text");
        b.costWords    = str(o, "costWords");
        b.tendencyKey  = str(o, "tendencyKey");
        b.tendencyText = str(o, "tendencyText");
        b.onTheBall    = str(o, "onTheBall");
        for (const QJsonValue &p : o.value(QStringLiteral("produced")).toArray())
            b.produced.push_back(TendencyProduced{ str(p.toObject(), "phrase"), p.toObject().value(QStringLiteral("count")).toInt() });
        s.onBall.push_back(std::move(b));
    }
    return s;
}

} // namespace pinpoint::analysis
