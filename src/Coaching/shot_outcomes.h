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

// SHOT OUTCOMES — pure, header-only, no Qt-GUI. What happened on one shot, in the golfer's words
// (docs/design/shot_outcomes_design.md §4.1, §5): the labels, the phrase, the one-line story, the
// candidate reason and the carry waterfall, all from the shot's own ledger row.
//
// A shot fires findings; only a session diagnoses (§1 rule 1). So the driver named here is a CANDIDATE,
// and its strength word is read from the SESSION's link grades, which the caller passes in. Nothing in
// this file can raise a word above what the session has earned.
//
// The pack's facts arrive marshalled (shot_outcomes_pack.h), so this header knows nothing of
// CharacteristicPack and does its own short graph walk: causes, then the physics edges (fat -> short).
//
// Rows are never mirrored for a left-hander; the gloss map only changes the words (Contract D).
//
// Unit-tested standalone in src/Analysis/tests/shot_outcomes_test.cpp.

#include "club_distances.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

namespace pinpoint::analysis {

// CURVE'S SHARE OF CARRY. A tilted spin axis turns part of the ball's lift sideways, so a curving ball
// loses the height (and so the carry) that the upward component no longer holds: carry × (1 − cos axis).
// The standard approximation, and not tuned here: at 14° of spin axis it is about 3 % of carry.
inline constexpr double kCurveCarryLossFactor = 1.0;

// LAUNCH AND SPIN, past the window the rows were graded against. A first linear estimate, as a share of
// the usual carry per unit outside the window. The design names the part but gives no number; these are
// provisional and belong on the corpus.
inline constexpr double kCarryLossPerLaunchDegree = 0.008;   // 0.8 % of carry per degree out of window
inline constexpr double kCarryLossPer1000Rpm      = 0.030;   // 3 % of carry per 1000 rpm out of window

// Which layer of the model a condition sits in, for the link words (§5.1). Ball = outcomes; Club =
// delivery numbers; Swing = faults; Root = setup, capacity, equipment and intent.
enum class ShotConditionLayer { Ball, Club, Swing, Root };

// The words and place of one condition.
struct ShotConditionFacts {
    QString id;
    QString family;             // "strike", "direction", "flight"; "" for a non-outcome
    QString label;              // the author's name
    QString golfer;             // the golfer phrase, lower case
    QString golferShort;        // "Slice"
    QString golferAbbr;         // "SL"
    QString golferStory;        // "cut across it"
    QString playTo;             // written for a right-hander
    ShotConditionLayer layer = ShotConditionLayer::Swing;
    int     packOrder = 0;
};

// The pack, marshalled. `causes` and `physics` are directed (from -> to, in pack order); `excludes`
// holds each pair in both directions.
struct ShotOutcomePack {
    QHash<QString, ShotConditionFacts> conditions;
    QHash<QString, QStringList> causes;
    QHash<QString, QStringList> physics;
    QHash<QString, QStringList> excludes;
};

// The session's evidence, so a word on one shot never exceeds what the session earned.
struct ShotOutcomeSessionFacts {
    std::vector<LinkEvidence> links;
    QHash<QString, Tier>      tiers;
    QHash<QString, ClubFlightUsual> flightUsuals;   // by canonical club; empty = nothing is read from the flight
};

struct ShotOutcomeEvidence {
    QString measureId;
    double  value = 0.0;
};

struct ShotOutcomeLabel {
    QString id;                 // the ROW id
    QString displayId;          // the id it is glossed as for this golfer
    QString family;
    QString shortText;          // "Slice"
    QString golfer;             // "the ball slices, curving hard away from the target"
    QString abbr;               // "SL"
    QString story;              // "cut across it"
    bool    intended  = false;  // the shape the golfer meant to hit
    bool    corrected = false;  // the golfer's correction, not a detection
    bool    inferred  = false;  // read from the flight, not measured at the face (strike_from_flight.h)
    bool    absorbed  = false;  // part of an inferred strike's signature: said once, in producedLine
    bool    miss      = true;   // counts against the shot (never fade/draw, never intended)
    std::vector<ShotOutcomeEvidence> evidence;
};

struct ShotOutcomeDriver {
    QString id;                 // the ROW id; empty = no driver
    QString displayId;
    QString name;
    QString golfer;             // the golfer phrase, as a clause
    QString strength;           // "clear", "likely", "possible"; "" = not shown
    int     explains = 0;       // headline misses it reaches
    int     of       = 0;       // miss labels on the shot
};

struct ShotOutcomeLink {
    QString   fromId, toId;
    LinkGrade grade   = LinkGrade::Unanchored;     // Unanchored for a physics link, which is not graded
    bool      physics = false;
    QString   words;            // "because", "seen with", "maybe", "from the fat"
};

struct ShotCarryPart {
    QString id;                 // "speed", "strike", "curve", "launch_spin"
    QString name;               // "Club speed", "Fat strike", "Curve", "Launch / spin"
    double  loss = 0.0;         // yd
    QString detail;             // "79 vs your 82 mph"
    QString lineWords;          // "slower club speed", "from the fat"
    bool    modelled = false;   // our own estimate with no formula behind it in §5.3: launch / spin
};

// The carry waterfall against the frozen baseline. Empty (present == false) unless the shot is labelled
// short and has a usual to be short of.
struct ShotCarryBlock {
    bool    present = false;
    double  usual   = std::numeric_limits<double>::quiet_NaN();
    double  usualSd = std::numeric_limits<double>::quiet_NaN();
    double  actual  = std::numeric_limits<double>::quiet_NaN();
    double  shortBy = 0.0;
    QString provenance;         // "against your usual 162 ± 7 yd"
    QString line;               // "24 yd short of your usual 162: 16 from the fat, 6 slower club speed, 2 curve"
    std::vector<ShotCarryPart> parts;   // causal order
    double  residual = 0.0;
    bool    residualShown = false;      // residual >= 1 yd
    bool    scaled = false;             // a part was cut to fit the shortfall
};

struct ShotOutcome {
    bool    ready = false;      // the shot has been analysed
    bool    noReading = false;  // no launch monitor reading on this shot
    std::vector<ShotOutcomeLabel> labels;
    QString phrase;             // "Fat slice, 24 yd short"
    QString mixKey;             // "fat slice": the phrase without its quantity, lower case
    QString story;
    QString noReasonText;       // "No reason found" for a miss with no driver
    QString producedLine;       // "From the shank: low launch, push slice"
    QString strikeFlightNote;   // "Low on the face (−12 mm), flight normal": a measured strike the flight
                                // didn't bear out, its row overruled on the ledger (checkStrikesAgainstFlight),
                                // so no label; "" otherwise
    ShotOutcomeDriver driver;
    std::vector<ShotOutcomeLink> links;
    ShotCarryBlock carry;
    bool    clean = false;      // no miss label; never true without a reading
    std::vector<QString> cellLines;     // the shot-log cell: at most two lines
    QString cell;               // those lines joined by a space
};

struct ShotMix {
    QString line;               // "11 clean · 6 slice · 3 fat slice · 2 fat"
    int     corrections = 0;
    int     fromFlight  = 0;    // shots whose strike was read from the flight
};

// ── Handedness ──────────────────────────────────────────────────────────────

// The condition a left-hander's row should read as. Rows stay target-relative; a left-hander's ball
// going right is a hook and their out-to-in path is on the other side of zero.
inline QString glossedConditionId(const QString &id, const QString &handedness)
{
    if (handedness != QLatin1String("Left")) return id;
    static const QHash<QString, QString> kGloss = {
        { QStringLiteral("pull"),                QStringLiteral("push") },
        { QStringLiteral("push"),                QStringLiteral("pull") },
        { QStringLiteral("slice"),               QStringLiteral("hook") },
        { QStringLiteral("hook"),                QStringLiteral("slice") },
        { QStringLiteral("pull_hook"),           QStringLiteral("push_slice") },
        { QStringLiteral("push_slice"),          QStringLiteral("pull_hook") },
        { QStringLiteral("fade"),                QStringLiteral("draw") },
        { QStringLiteral("draw"),                QStringLiteral("fade") },
        { QStringLiteral("in_to_out_path"),      QStringLiteral("out_to_in_path") },
        { QStringLiteral("out_to_in_path"),      QStringLiteral("in_to_out_path") },
        { QStringLiteral("open_face_to_path"),   QStringLiteral("closed_face_to_path") },
        { QStringLiteral("closed_face_to_path"), QStringLiteral("open_face_to_path") },
    };
    return kGloss.value(id, id);
}

// The left/right words of a playTo line swapped, for a left-hander.
inline QString mirrorLeftRightWords(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\\b(left|right|Left|Right)\\b"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString w = m.captured(0);
        if (w == QLatin1String("left"))       out += QStringLiteral("right");
        else if (w == QLatin1String("right")) out += QStringLiteral("left");
        else if (w == QLatin1String("Left"))  out += QStringLiteral("Right");
        else                                  out += QStringLiteral("Left");
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

// The playTo line for this golfer: the glossed condition's words, mirrored for a left-hander.
inline QString outcomePlayTo(const ShotOutcomePack &pack, const QString &id, const QString &handedness)
{
    const QString display = glossedConditionId(id, handedness);
    const auto it = pack.conditions.constFind(display);
    if (it == pack.conditions.constEnd()) return QString();
    return handedness == QLatin1String("Left") ? mirrorLeftRightWords(it->playTo) : it->playTo;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

inline QString outcomeCapitalised(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

inline bool shotHasLmReading(const ShotRecord &s)
{
    return std::isfinite(s.carry) || std::isfinite(s.total) || std::isfinite(s.offline)
        || std::isfinite(s.ballSpeed) || std::isfinite(s.lmClubSpeed) || std::isfinite(s.smash);
}

inline const ShotConditionFacts *outcomeFacts(const ShotOutcomePack &pack, const QString &id)
{
    const auto it = pack.conditions.constFind(id);
    return it == pack.conditions.constEnd() ? nullptr : &*it;
}

// English word order only: where a family's word goes in the phrase ("Fat slice", never "Slice fat") and
// in the shot-log cell. It is not a ranking of how much a miss matters: a pull or a push costs a golfer as
// much as a fat or a thin. It must never be used to choose or to count a miss (Tendencies' usual miss, a
// shot mix): there the commonest miss decides (tendencyLeadMiss, tendencies.h).
inline int outcomeFamilyRank(const QString &family)
{
    if (family == QLatin1String("strike"))    return 0;
    if (family == QLatin1String("direction")) return 1;
    if (family == QLatin1String("flight"))    return 2;
    return 3;
}

inline bool isOutcomeCurveOnly(const QString &id)
{
    return id == QLatin1String("fade") || id == QLatin1String("draw");
}

// O's survivors after the excludes rule, in row order — the same walk as explain(), with the same
// keep-rule, so a shot and its session cannot disagree about which label stood.
inline QStringList resolveOutcomeExcludes(const QStringList &fired, const QHash<QString, const ConditionRow *> &rowOf,
                                          const ShotOutcomePack &pack)
{
    QStringList active = fired;
    for (const QString &a : fired) {
        if (!active.contains(a)) continue;
        const ConditionRow *ra = rowOf.value(a);
        for (const QString &b : pack.excludes.value(a)) {
            if (b == a || !active.contains(b)) continue;
            const ConditionRow *rb = rowOf.value(b);
            const bool keepA = keepFirstOfExcludingPair(ra->confidence, ra->firedSignalCount, a,
                                                        rb->confidence, rb->firedSignalCount, b);
            const QString dropped = keepA ? b : a;
            active.removeAll(dropped);
            if (dropped == a) break;
        }
    }
    return active;
}

// A condition's reading, as the rows that read it recorded it. NaN when no assessable row has it.
inline double shotMeasureValue(const ShotRecord &shot, const QString &measureId)
{
    for (const ConditionRow &r : shot.rows) {
        if (r.state == ShotState::NotAssessable) continue;
        if (const std::optional<MeasureRow> m = rowReadingOf(r, measureId); m && std::isfinite(m->value))
            return m->value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// How far a value sits outside its row's window, in the row's own unit; 0 when inside.
inline double rowExcess(const ConditionRow &r)
{
    if (!std::isfinite(r.value) || !std::isfinite(r.corridorLo) || !std::isfinite(r.corridorHi)) return 0.0;
    if (r.value > r.corridorHi) return r.value - r.corridorHi;
    if (r.value < r.corridorLo) return r.corridorLo - r.value;
    return 0.0;
}

// The plain words for a measured thin, top or fat the flight didn't bear out: "Low on the face (−12 mm),
// flight normal". The height is the row's own reading, to the whole millimetre; left out when no row has it.
inline QString measuredStrikeFlightNote(const ShotRecord &shot, const ConditionRow &row)
{
    const QString measureId = QStringLiteral("m_lmStrikeHeight");
    double height = std::numeric_limits<double>::quiet_NaN();
    if (const std::optional<MeasureRow> m = rowReadingOf(row, measureId); m && std::isfinite(m->value))
        height = m->value;
    else
        height = shotMeasureValue(shot, measureId);

    QString note = row.conditionId == QLatin1String("chunk") ? QStringLiteral("High on the face")
                                                             : QStringLiteral("Low on the face");
    if (std::isfinite(height)) {
        const long mm = std::lround(height);
        const QString sign = mm < 0 ? QStringLiteral("−") : mm > 0 ? QStringLiteral("+") : QString();
        note += QStringLiteral(" (") + sign + QString::number(std::labs(mm)) + QStringLiteral(" mm)");
    }
    return note + QStringLiteral(", flight normal");
}

// ── The walk ────────────────────────────────────────────────────────────────

struct ShotReachEdge {
    QString from;
    bool    physics = false;
};

struct ShotDriverReach {
    QHash<QString, int>           depth;
    QHash<QString, ShotReachEdge> parent;   // the edge each node was first reached over
};

// Everything `start` reaches: its swing causes first, then the physics edges, walked only through
// outcomes that actually happened on this shot. A link in the story has to have happened: a fat shot
// reaches "short" through the fat, and a shot that wasn't fat doesn't.
inline ShotDriverReach walkDriverReach(const QString &start, const ShotOutcomePack &pack, const QSet<QString> &onShot)
{
    ShotDriverReach reach;
    reach.depth.insert(start, 0);
    QStringList queue{ start };
    for (int head = 0; head < queue.size(); ++head) {
        const QString at = queue[head];
        for (const QString &next : pack.causes.value(at)) {
            if (reach.depth.contains(next)) continue;
            reach.depth.insert(next, reach.depth.value(at) + 1);
            reach.parent.insert(next, ShotReachEdge{ at, false });
            queue << next;
        }
    }

    QStringList physicsQueue;
    for (const QString &id : queue)
        if (onShot.contains(id)) physicsQueue << id;
    for (int head = 0; head < physicsQueue.size(); ++head) {
        const QString at = physicsQueue[head];
        for (const QString &next : pack.physics.value(at)) {
            if (!onShot.contains(next) || reach.depth.contains(next)) continue;
            reach.depth.insert(next, reach.depth.value(at) + 1);
            reach.parent.insert(next, ShotReachEdge{ at, true });
            physicsQueue << next;
        }
    }
    return reach;
}

struct ShotReachStep {
    QString from, to;
    bool    physics = false;
};

// The path from the walk's start to `target`, in order.
inline std::vector<ShotReachStep> driverPathTo(const ShotDriverReach &reach, const QString &target)
{
    std::vector<ShotReachStep> path;
    QString at = target;
    while (reach.parent.contains(at)) {
        const ShotReachEdge edge = reach.parent.value(at);
        path.push_back(ShotReachStep{ edge.from, at, edge.physics });
        at = edge.from;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

inline LinkGrade sessionLinkGrade(const std::vector<LinkEvidence> &links, const QString &from, const QString &to)
{
    const LinkEvidence *l = linkFor(links, from, to);
    return l ? l->grade : LinkGrade::Unanchored;
}

// §5.1: the words for one link in the driver's chain.
inline QString swingLinkWords(const ShotConditionFacts &from, const ShotConditionFacts &to, bool bothFired)
{
    if (to.layer == ShotConditionLayer::Ball && from.layer == ShotConditionLayer::Club)
        return bothFired ? QStringLiteral("because") : QStringLiteral("seen with");
    if (from.layer == ShotConditionLayer::Root) return QStringLiteral("maybe");
    return QStringLiteral("seen with");
}

// ── The carry waterfall (§5.3) ──────────────────────────────────────────────

// Empty for a club under kClubBaselineMinShots: a usual that has not been earned is not a yardstick. A
// part whose measure is missing is left out, and what it would have held stays in the residual. The parts
// can sum past what was actually lost, and a waterfall must foot, so each takes what is left in causal
// order: speed, strike, curve, then the modelled launch / spin part from whatever remains. `scaled` says
// a part was cut, and a part cut to nothing is dropped.
inline ShotCarryBlock composeCarryBlock(const ShotRecord &shot, const std::vector<ShotOutcomeLabel> &labels,
                                        const ClubBaselineEntry *usual)
{
    ShotCarryBlock b;
    const auto hasLabel = [&labels](const char *id) {
        return std::any_of(labels.begin(), labels.end(), [id](const ShotOutcomeLabel &l) { return l.id == QLatin1String(id); });
    };
    if (!usual || usual->n < kClubBaselineMinShots || !std::isfinite(shot.carry) || !std::isfinite(usual->carryMean)
        || !hasLabel("carry_deficit"))
        return b;
    const double shortBy = usual->carryMean - shot.carry;
    if (!(shortBy > 0.0)) return b;

    b.present = true;
    b.usual   = usual->carryMean;
    b.usualSd = usual->carrySd;
    b.actual  = shot.carry;
    b.shortBy = shortBy;
    b.provenance = QStringLiteral("against your usual ") + yardsMeanSdText(usual->carryMean, usual->carrySd);

    const bool haveSpeed = std::isfinite(shot.lmClubSpeed) && std::isfinite(usual->speedMean) && usual->speedMean > 0.0;
    const double speedRatio = haveSpeed ? shot.lmClubSpeed / usual->speedMean : 1.0;

    if (haveSpeed) {
        const double loss = usual->carryMean * (1.0 - speedRatio);
        if (loss > 0.0)
            b.parts.push_back(ShotCarryPart{ QStringLiteral("speed"), QStringLiteral("Club speed"), loss,
                                             lmFormat(shot.lmClubSpeed, 0) + QStringLiteral(" vs your ")
                                                 + lmFormat(usual->speedMean, 0) + QStringLiteral(" mph"),
                                             QStringLiteral("slower club speed") });
    }

    if (std::isfinite(shot.smash) && std::isfinite(usual->smashMean) && usual->smashMean > 0.0) {
        const double loss = usual->carryMean * speedRatio * (1.0 - shot.smash / usual->smashMean);
        if (loss > 0.0) {
            QString name = QStringLiteral("Strike");
            QString words = QStringLiteral("from the strike");
            for (const ShotOutcomeLabel &l : labels)
                if (l.family == QLatin1String("strike") && !l.shortText.isEmpty()) {
                    name  = l.shortText.toLower().endsWith(QStringLiteral("strike")) ? l.shortText
                                                                                     : l.shortText + QStringLiteral(" strike");
                    words = QStringLiteral("from the ") + l.shortText.toLower();
                    break;
                }
            b.parts.push_back(ShotCarryPart{ QStringLiteral("strike"), name, loss,
                                             lmFormat(shot.smash, 2) + QStringLiteral(" vs your ")
                                                 + lmFormat(usual->smashMean, 2) + QStringLiteral(" smash"),
                                             words });
        }
    }

    // A ball-only monitor can't split club speed from strike, but it can still say the ball came off slower.
    if (!haveSpeed && !std::isfinite(shot.smash) && std::isfinite(shot.ballSpeed)
        && std::isfinite(usual->ballSpeedMean) && usual->ballSpeedMean > 0.0) {
        const double loss = usual->carryMean * (1.0 - shot.ballSpeed / usual->ballSpeedMean);
        if (loss > 0.0)
            b.parts.push_back(ShotCarryPart{ QStringLiteral("ball_speed"), QStringLiteral("Ball speed"), loss,
                                             lmFormat(shot.ballSpeed, 0) + QStringLiteral(" vs your ")
                                                 + lmFormat(usual->ballSpeedMean, 0) + QStringLiteral(" mph"),
                                             QStringLiteral("slower ball speed") });
    }

    const double spinAxis = shotMeasureValue(shot, QStringLiteral("m_spinAxis"));
    if (std::isfinite(spinAxis)) {
        const double loss = shot.carry * kCurveCarryLossFactor * (1.0 - std::cos(spinAxis * M_PI / 180.0));
        if (loss > 0.0)
            b.parts.push_back(ShotCarryPart{ QStringLiteral("curve"), QStringLiteral("Curve"), loss, QString(),
                                             QStringLiteral("curve") });
    }

    double launchSpin = 0.0;
    for (const ShotOutcomeLabel &l : labels) {
        const ConditionRow *r = rowFor(shot, l.id);
        if (!r || r->state != ShotState::Fired) continue;
        if (l.id == QLatin1String("launch_low") || l.id == QLatin1String("launch_high"))
            launchSpin += usual->carryMean * kCarryLossPerLaunchDegree * rowExcess(*r);
        else if (l.id == QLatin1String("spin_excess") || l.id == QLatin1String("spin_deficit"))
            launchSpin += usual->carryMean * kCarryLossPer1000Rpm * rowExcess(*r) / 1000.0;
    }
    if (launchSpin > 0.0)
        b.parts.push_back(ShotCarryPart{ QStringLiteral("launch_spin"), QStringLiteral("Launch / spin"), launchSpin, QString(),
                                         QStringLiteral("launch and spin"), true });

    double remaining = shortBy;
    for (ShotCarryPart &p : b.parts) {
        if (p.loss > remaining) {
            p.loss = remaining;
            b.scaled = true;
        }
        remaining -= p.loss;
    }
    b.parts.erase(std::remove_if(b.parts.begin(), b.parts.end(), [](const ShotCarryPart &p) { return !(p.loss > 0.0); }),
                  b.parts.end());
    b.residual = std::max(0.0, remaining);
    b.residualShown = b.residual >= 1.0;

    std::vector<const ShotCarryPart *> largestFirst;
    for (const ShotCarryPart &p : b.parts) largestFirst.push_back(&p);
    std::stable_sort(largestFirst.begin(), largestFirst.end(),
                     [](const ShotCarryPart *x, const ShotCarryPart *y) { return x->loss > y->loss; });
    QStringList fragments;
    for (const ShotCarryPart *p : largestFirst)
        if (p->loss >= 0.5) fragments << lmFormat(p->loss, 0) + QLatin1Char(' ') + p->lineWords;

    b.line = yardsShortText(shortBy) + QStringLiteral(" of your usual ") + lmFormat(usual->carryMean, 0);
    if (!fragments.isEmpty()) b.line += QStringLiteral(": ") + fragments.join(QStringLiteral(", "));
    return b;
}

// ── The composer (§4.1, §5.2) ───────────────────────────────────────────────

inline QString joinOutcomeClauses(const QStringList &clauses)
{
    if (clauses.size() < 2) return clauses.isEmpty() ? QString() : clauses.first();
    return clauses.mid(0, clauses.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + clauses.last();
}

// What happened on one shot. `corrections` and `intent` are the shot's own (§4.4): a correction replaces
// a detected label for display and tallies, the row itself is never touched. `usual` is the session's
// frozen baseline entry for the shot's club, null when the club has no history.
inline ShotOutcome composeShotOutcome(const ShotRecord &shot, const std::vector<ShotCorrection> &corrections,
                                      const QString &intent, const ClubBaselineEntry *usual,
                                      const ShotOutcomePack &pack, const ShotOutcomeSessionFacts &session)
{
    ShotOutcome out;
    out.ready = !shot.rows.empty();
    if (!out.ready) return out;

    const bool hasLm = shotHasLmReading(shot);
    out.noReading = !hasLm;

    // A measured thin, top or fat has to agree with the flight (§3.7): a sticker placed off shifts a whole
    // session's strike heights. That is decided ONCE, on the ledger rows (checkStrikesAgainstFlight,
    // strike_from_flight.h): one the flight doesn't bear out is NotAssessable and `overruled`, so it is not
    // fired here and is never a miss anywhere a label counts (tendencies, the outcome filter, the mix, the
    // phrase, the story). The shot carries a plain note instead, from the overruled row's own reading. rowOf
    // keeps that row too, so a golfer's correction back to it still shows what was measured.
    QStringList fired;
    QHash<QString, const ConditionRow *> rowOf;
    bool directionAssessed = false;
    for (const ConditionRow &r : shot.rows) {
        const ShotConditionFacts *facts = outcomeFacts(pack, r.conditionId);
        if (!facts || facts->layer != ShotConditionLayer::Ball) continue;
        if (r.state != ShotState::NotAssessable && facts->family == QLatin1String("direction"))
            directionAssessed = true;
        if (r.state == ShotState::Fired) {
            fired << r.conditionId;
            rowOf.insert(r.conditionId, &r);
        } else if (r.overruled) {
            rowOf.insert(r.conditionId, &r);
            const bool measuredStrike = r.conditionId == QLatin1String("thin") || r.conditionId == QLatin1String("top")
                                     || r.conditionId == QLatin1String("chunk");
            if (measuredStrike && out.strikeFlightNote.isEmpty()) out.strikeFlightNote = measuredStrikeFlightNote(shot, r);
        }
    }

    // Sky is a FLIGHT fault (shot_factors.h, 10 Nov 2026): a High flight, an attack steeper than the club's usual
    // and a carry well short, on a driver, wood or hybrid (flightSaysSky). The ledger keeps a measured sky row
    // fired only where that rule holds, and the three factors name it (shotFactors), so it is no label here: the
    // phrase and the strike stay the strike's. A golfer's own correction to Sky still stands.
    fired.removeAll(QStringLiteral("sky"));

    // Step 1: excludes, then corrections, then intent. No reading means no word on where the ball went.
    QStringList active = resolveOutcomeExcludes(fired, rowOf, pack);
    // A fade or draw is the playable shape short of a slice or hook, so a shot cannot be read as both. The
    // ledger rows stay as they fired; only the labels are decided here.
    if (active.contains(QStringLiteral("slice")) || active.contains(QStringLiteral("hook"))
        || active.contains(QStringLiteral("push_slice")) || active.contains(QStringLiteral("pull_hook"))) {
        active.removeAll(QStringLiteral("fade"));
        active.removeAll(QStringLiteral("draw"));
    }
    if (!hasLm)
        active.erase(std::remove_if(active.begin(), active.end(), [&pack](const QString &id) {
            return outcomeFacts(pack, id)->family != QLatin1String("strike");
        }), active.end());

    // A strike the monitor couldn't measure, read from the flight. Ahead of the corrections, so "Not right?"
    // takes it off the same way as any other label.
    // Sky is not a strike any more: strikeFromFlight never names it (flightSaysSky does, for the flight factor).
    const QString flightStrike = hasLm ? strikeFromFlight(shot, session.flightUsuals) : QString();
    if (!flightStrike.isEmpty() && outcomeFacts(pack, flightStrike) && !active.contains(flightStrike))
        active << flightStrike;

    QSet<QString> correctedIds;
    for (const ShotCorrection &c : corrections) {
        if (!c.from.isEmpty()) active.removeAll(c.from);
        if (!c.to.isEmpty() && outcomeFacts(pack, c.to)) {
            if (!active.contains(c.to)) active << c.to;
            correctedIds.insert(c.to);
            // The golfer's word settles the family: what it excludes there goes, as the rule would have.
            const QString family = outcomeFacts(pack, c.to)->family;
            // A strike they named wins over the flight, and the "flight normal" note no longer applies.
            if (family == QLatin1String("strike")) out.strikeFlightNote.clear();
            for (const QString &other : pack.excludes.value(c.to))
                if (!correctedIds.contains(other) && active.contains(other) && outcomeFacts(pack, other)->family == family)
                    active.removeAll(other);
        }
    }

    const bool leftHanded = shot.handedness == QLatin1String("Left");
    const auto inGolfersHand = [leftHanded](const QString &words) {
        return leftHanded ? mirrorLeftRightWords(words) : words;
    };

    for (const QString &id : active) {
        const ShotConditionFacts *rowFacts = outcomeFacts(pack, id);
        const QString displayId = glossedConditionId(id, shot.handedness);
        const ShotConditionFacts *facts = outcomeFacts(pack, displayId);
        if (!facts) facts = rowFacts;

        ShotOutcomeLabel l;
        l.id        = id;
        l.displayId = displayId;
        l.family    = rowFacts->family;
        l.shortText = !facts->golferShort.isEmpty() ? facts->golferShort : facts->label;
        l.golfer    = inGolfersHand(facts->golfer);
        l.abbr      = facts->golferAbbr;
        l.story     = inGolfersHand(facts->golferStory);
        l.corrected = correctedIds.contains(id);
        l.intended  = !intent.isEmpty() && intent != QLatin1String("straight") && displayId == intent;
        l.miss      = !l.intended && !isOutcomeCurveOnly(id);
        l.inferred  = id == flightStrike && !l.corrected;
        if (const ConditionRow *r = rowOf.value(id))
            for (const MeasureRow &m : rowReadings(*r))
                l.evidence.push_back(ShotOutcomeEvidence{ m.measureId, m.value });
        if (l.inferred) {
            // What it was read from. A row can't say: the one that would have measured it was empty.
            l.evidence.push_back(ShotOutcomeEvidence{ QStringLiteral("m_lmLaunchAngle"), shot.launchAngle });
            if (id == QLatin1String("shank"))
                l.evidence.push_back(ShotOutcomeEvidence{ QStringLiteral("m_lmLaunchDirection"), shot.startDirection });
            else if (id != QLatin1String("thin"))
                l.evidence.push_back(ShotOutcomeEvidence{ QStringLiteral("m_smashFactor"), shot.smash });
            if (id == QLatin1String("chunk"))
                l.evidence.push_back(ShotOutcomeEvidence{ QStringLiteral("m_attackAngle"), shot.attackAngle });
        }
        out.labels.push_back(std::move(l));
    }
    // Word order for the phrase and the cell, nothing more: no count or choice reads the first label.
    std::stable_sort(out.labels.begin(), out.labels.end(), [&pack](const ShotOutcomeLabel &a, const ShotOutcomeLabel &b) {
        const int fa = outcomeFamilyRank(a.family), fb = outcomeFamilyRank(b.family);
        if (fa != fb) return fa < fb;
        return outcomeFacts(pack, a.id)->packOrder < outcomeFacts(pack, b.id)->packOrder;
    });

    QSet<QString> onShot;
    for (const ShotOutcomeLabel &l : out.labels) onShot.insert(l.id);

    // Step 2: what another label already accounts for leaves the headline but stays a label. A compound
    // miss also accounts for the two halves of itself.
    QSet<QString> implied;
    for (const ShotOutcomeLabel &l : out.labels) {
        const ShotDriverReach below = walkDriverReach(l.id, ShotOutcomePack{ pack.conditions, {}, pack.physics, {} }, onShot);
        for (auto it = below.depth.constBegin(); it != below.depth.constEnd(); ++it)
            if (it.key() != l.id) implied.insert(it.key());
    }
    if (onShot.contains(QStringLiteral("pull_hook"))) {
        implied.insert(QStringLiteral("pull"));
        implied.insert(QStringLiteral("hook"));
    }
    if (onShot.contains(QStringLiteral("push_slice"))) {
        implied.insert(QStringLiteral("push"));
        implied.insert(QStringLiteral("slice"));
    }

    // The flight an inferred shank or top was read from is that strike, not a second thing that went wrong.
    // It's said once, in producedLine, and a compound miss stands for its two halves there.
    const auto inferredLabel = std::find_if(out.labels.begin(), out.labels.end(), [](const ShotOutcomeLabel &l) { return l.inferred; });
    if (inferredLabel != out.labels.end()
        && (inferredLabel->id == QLatin1String("shank") || inferredLabel->id == QLatin1String("top"))) {
        const bool shank = inferredLabel->id == QLatin1String("shank");
        const QString strikeWord = inferredLabel->shortText.toLower();
        QStringList produced;
        for (ShotOutcomeLabel &l : out.labels) {
            if (l.id != QLatin1String("launch_low") && !(shank && l.family == QLatin1String("direction"))) continue;
            l.absorbed = true;
            implied.insert(l.id);
            const bool halfOfCompound = (onShot.contains(QStringLiteral("push_slice")) && (l.id == QLatin1String("push") || l.id == QLatin1String("slice")))
                                     || (onShot.contains(QStringLiteral("pull_hook")) && (l.id == QLatin1String("pull") || l.id == QLatin1String("hook")));
            if (!halfOfCompound) produced << l.shortText.toLower();
        }
        if (!produced.isEmpty())
            out.producedLine = QStringLiteral("From the ") + strikeWord + QStringLiteral(": ") + produced.join(QStringLiteral(", "));
    }

    std::vector<const ShotOutcomeLabel *> headlineMisses;
    int missCount = 0;
    for (const ShotOutcomeLabel &l : out.labels) {
        if (!l.miss) continue;
        ++missCount;
        if (!implied.contains(l.id)) headlineMisses.push_back(&l);
    }

    // Steps 3–5: the candidate that explains most of the headline.
    struct CandidateScore {
        QString id;
        int explains = 0, tier = 0, bestGrade = 0, depth = 0, packOrder = 0;
        bool hasGrade = false;
        LinkGrade weakest = LinkGrade::MovedTogether;
        bool clearEligible = false;
        ShotDriverReach reach;
    };
    std::optional<CandidateScore> best;
    QSet<QString> firedConditions;
    for (const ConditionRow &r : shot.rows)
        if (r.state == ShotState::Fired) firedConditions.insert(r.conditionId);

    for (const ConditionRow &r : shot.rows) {
        if (r.state != ShotState::Fired) continue;
        const ShotConditionFacts *facts = outcomeFacts(pack, r.conditionId);
        if (!facts || facts->layer == ShotConditionLayer::Ball) continue;

        CandidateScore score;
        score.id    = r.conditionId;
        score.tier  = int(session.tiers.value(r.conditionId, Tier::Clean));
        score.packOrder = facts->packOrder;
        score.reach = walkDriverReach(r.conditionId, pack, onShot);
        score.clearEligible = facts->layer == ShotConditionLayer::Club;

        for (const ShotOutcomeLabel *target : headlineMisses) {
            if (!score.reach.depth.contains(target->id)) continue;
            ++score.explains;
            score.depth = std::max(score.depth, score.reach.depth.value(target->id));
            int causeSteps = 0;
            for (const ShotReachStep &step : driverPathTo(score.reach, target->id)) {
                if (step.physics) continue;
                ++causeSteps;
                const LinkGrade g = sessionLinkGrade(session.links, step.from, step.to);
                score.weakest   = score.hasGrade ? std::min(score.weakest, g) : g;
                score.bestGrade = std::max(score.bestGrade, int(g));
                score.hasGrade  = true;
            }
            if (causeSteps != 1 || target->corrected) score.clearEligible = false;
        }
        if (score.explains == 0) continue;

        if (!best
            || std::tie(score.explains, score.tier, score.bestGrade, score.depth)
                   > std::tie(best->explains, best->tier, best->bestGrade, best->depth)
            || (std::tie(score.explains, score.tier, score.bestGrade, score.depth)
                    == std::tie(best->explains, best->tier, best->bestGrade, best->depth)
                && score.packOrder < best->packOrder))
            best = std::move(score);
    }

    if (best) {
        const ShotConditionFacts *rowFacts = outcomeFacts(pack, best->id);
        const QString displayId = glossedConditionId(best->id, shot.handedness);
        const ShotConditionFacts *facts = outcomeFacts(pack, displayId);
        if (!facts) facts = rowFacts;

        out.driver.id        = best->id;
        out.driver.displayId = displayId;
        out.driver.name      = facts->label;
        out.driver.golfer    = inGolfersHand(facts->golfer);
        out.driver.explains  = best->explains;
        out.driver.of        = missCount;

        // The word is the weakest link the session graded, and "clear" only replaces "likely": a direct
        // ball-from-club link that this shot measured and that the session already says is real.
        if (best->hasGrade) {
            switch (best->weakest) {
            case LinkGrade::MovedTogether:
            case LinkGrade::ConditionallyDependent:
                out.driver.strength = best->clearEligible ? QStringLiteral("clear") : QStringLiteral("likely");
                break;
            case LinkGrade::Coherent:
            case LinkGrade::PresentTogether:
                out.driver.strength = QStringLiteral("possible");
                break;
            case LinkGrade::Unanchored:
                break;
            }
        }

        QSet<QPair<QString, QString>> seen;
        for (const ShotOutcomeLabel *target : headlineMisses) {
            if (!best->reach.depth.contains(target->id)) continue;
            for (const ShotReachStep &step : driverPathTo(best->reach, target->id)) {
                if (step.physics || seen.contains({ step.from, step.to })) continue;
                seen.insert({ step.from, step.to });
                const ShotConditionFacts *f = outcomeFacts(pack, step.from);
                const ShotConditionFacts *t = outcomeFacts(pack, step.to);
                ShotOutcomeLink link;
                link.fromId = step.from;
                link.toId   = step.to;
                link.grade  = sessionLinkGrade(session.links, step.from, step.to);
                link.words  = (f && t)
                    ? swingLinkWords(*f, *t, firedConditions.contains(step.from) && firedConditions.contains(step.to))
                    : QStringLiteral("seen with");
                out.links.push_back(std::move(link));
            }
        }
    }
    for (const ShotOutcomeLabel &l : out.labels)
        for (const QString &to : pack.physics.value(l.id)) {
            if (!onShot.contains(to)) continue;
            ShotOutcomeLink link;
            link.fromId  = l.id;
            link.toId    = to;
            link.physics = true;
            link.words   = QStringLiteral("from the ") + l.shortText.toLower();
            out.links.push_back(std::move(link));
        }

    out.carry = composeCarryBlock(shot, out.labels, usual);

    // Phrase and story (§5.2). One strike word, the direction words, one flight word, then the quantity.
    QStringList head, tail, clauses;
    const ShotOutcomeLabel *soloHead = nullptr;
    int headCount = 0;
    const auto addClause = [&clauses](const ShotOutcomeLabel &l) {
        if (!l.story.isEmpty()) clauses << l.story;
    };
    bool haveStrike = false, haveFlight = false;
    bool haveCarry = false;
    QStringList cellStrike, cellDirection, cellFlight;
    for (const ShotOutcomeLabel &l : out.labels) {
        if (l.id == QLatin1String("carry_deficit")) { haveCarry = true; continue; }
        if (implied.contains(l.id)) continue;
        if (l.family == QLatin1String("strike")) {
            if (haveStrike) continue;
            haveStrike = true;
            head << l.shortText;
            if (!l.abbr.isEmpty()) cellStrike << l.abbr;
            addClause(l);
            ++headCount;
            soloHead = &l;
        } else if (l.family == QLatin1String("direction")) {
            head << l.shortText;
            if (!l.abbr.isEmpty()) cellDirection << l.abbr;
            addClause(l);
            ++headCount;
            soloHead = &l;
        } else {
            if (haveFlight) continue;
            haveFlight = true;
            tail << l.shortText.toLower();
            if (!l.abbr.isEmpty()) cellFlight << l.abbr;
            addClause(l);
        }
    }
    if (haveCarry) {
        const ShotOutcomeLabel *carry = nullptr;
        for (const ShotOutcomeLabel &l : out.labels)
            if (l.id == QLatin1String("carry_deficit")) carry = &l;
        if (!implied.contains(carry->id)) addClause(*carry);
        if (!haveFlight && !carry->abbr.isEmpty()) cellFlight << carry->abbr;
    }

    QString mix = head.join(QLatin1Char(' ')).toLower();
    if (!tail.isEmpty()) mix += (mix.isEmpty() ? QString() : QStringLiteral(", ")) + tail.join(QStringLiteral(", "));

    if (haveCarry) {
        const ShotOutcomeLabel *carry = nullptr;
        for (const ShotOutcomeLabel &l : out.labels)
            if (l.id == QLatin1String("carry_deficit")) carry = &l;
        tail << (out.carry.present ? yardsShortText(out.carry.shortBy) : carry->shortText.toLower());
        if (mix.isEmpty()) mix = QStringLiteral("short");
    }
    out.mixKey = mix;

    QStringList phraseParts;
    if (!head.isEmpty()) {
        QStringList lowered = head;
        for (int i = 1; i < lowered.size(); ++i) lowered[i] = lowered[i].toLower();
        phraseParts << lowered.join(QLatin1Char(' '));
    }
    phraseParts << tail;
    if (!phraseParts.isEmpty()) {
        phraseParts[0] = outcomeCapitalised(phraseParts[0]);
        out.phrase = phraseParts.join(QStringLiteral(", "));
        if (headCount == 1 && soloHead && soloHead->intended && tail.isEmpty())
            out.phrase += QStringLiteral(", as planned");
    }

    const bool anyMiss = missCount > 0;
    out.clean = hasLm && directionAssessed && !anyMiss;
    if (out.phrase.isEmpty()) {
        // A clean shot says only what was judged: "good" needs the strike and the curve to have been read.
        const bool strikeUnread = strikeHeightUnread(shot);
        const bool curveUnread  = strikeRowUnread(shot, "slice") && strikeRowUnread(shot, "hook");
        if (out.clean)
            out.phrase = strikeUnread && curveUnread ? QStringLiteral("Good flight · strike and curve not measured")
                       : strikeUnread                ? QStringLiteral("Good flight · strike not measured")
                       : curveUnread                 ? QStringLiteral("Good flight · curve not measured")
                                                     : QStringLiteral("Good shot");
        else
            out.phrase = hasLm ? QStringLiteral("Flight not judged") : QStringLiteral("No ball reading");
    }

    const QString physicsClause = joinOutcomeClauses(clauses);
    if (!out.driver.id.isEmpty()) {
        QString driverClause = out.driver.golfer;
        while (driverClause.endsWith(QLatin1Char('.'))) driverClause.chop(1);
        driverClause = outcomeCapitalised(driverClause);
        out.story = driverClause + (physicsClause.isEmpty() ? QString() : QStringLiteral(": ") + physicsClause) + QLatin1Char('.');
    } else if (!physicsClause.isEmpty()) {
        out.story = outcomeCapitalised(physicsClause) + QLatin1Char('.');
    }
    if (out.driver.id.isEmpty() && anyMiss) out.noReasonText = QStringLiteral("No reason found");

    // The shot-log cell: the strike on one line, the direction on the next, flight only if a line is free.
    QStringList cellLines;
    if (!cellStrike.isEmpty())    cellLines << cellStrike.join(QLatin1Char(' '));
    if (!cellDirection.isEmpty()) cellLines << cellDirection.join(QLatin1Char(' '));
    if (cellLines.size() < 2 && !cellFlight.isEmpty()) cellLines << cellFlight.first();
    for (const QString &line : cellLines) out.cellLines.push_back(line);
    out.cell = cellLines.join(QLatin1Char(' '));
    return out;
}

// The session's shot mix (§4.2): each shot counted once under its combination, clean first, the rest by
// count. A shot with no reading is counted apart rather than called anything it wasn't seen to be.
inline ShotMix composeSessionMix(const std::vector<ShotOutcome> &shots)
{
    ShotMix mix;
    int clean = 0, noReading = 0;
    QStringList order;
    QHash<QString, int> count;
    for (const ShotOutcome &s : shots) {
        if (std::any_of(s.labels.begin(), s.labels.end(), [](const ShotOutcomeLabel &l) { return l.corrected; }))
            ++mix.corrections;
        if (std::any_of(s.labels.begin(), s.labels.end(), [](const ShotOutcomeLabel &l) { return l.inferred; }))
            ++mix.fromFlight;
        if (!s.ready) continue;
        if (s.clean) { ++clean; continue; }
        if (s.mixKey.isEmpty()) { ++noReading; continue; }
        if (!count.contains(s.mixKey)) order << s.mixKey;
        ++count[s.mixKey];
    }
    std::stable_sort(order.begin(), order.end(), [&count](const QString &a, const QString &b) {
        return count.value(a) > count.value(b);
    });

    QStringList parts;
    if (clean > 0) parts << QStringLiteral("%1 clean").arg(clean);
    for (const QString &key : order) parts << QStringLiteral("%1 %2").arg(count.value(key)).arg(key);
    if (noReading > 0) parts << QStringLiteral("%1 no reading").arg(noReading);
    mix.line = parts.join(QStringLiteral(" · "));
    return mix;
}

// ── A club's shot mix, for the band bar ─────────────────────────────────────

inline constexpr int kShotMixMaxMisses = 3;     // named in the bar; the rest are "Other"

// One band of the bar. `kind` picks the tone: "good", "main" (the commonest miss) or "other".
struct ShotMixPart {
    QString key, label, kind;
    int     count = 0, percent = 0;
};

// "Good 50% · Thin 25% · Slice 25%". Percentages are of the judged shots and add up to 100; `notRead` is
// said in words after them and is not drawn.
struct ShotMixBar {
    int     judged = 0, notRead = 0;
    std::vector<ShotMixPart> parts;
    QString line;
};

// The shots of one club, each counted once: good, under its lead miss (the one of its misses commonest with
// that club, tendencyLeadMiss in tendencies.h), or not read.
struct ShotMixTally {
    int good = 0, notRead = 0;
    QHash<QString, int>     count;
    QHash<QString, QString> label;
    void addMiss(const QString &id, const QString &shortText)
    {
        ++count[id];
        label.insert(id, shortText);
    }
};

inline ShotMixBar composeShotMixBar(const ShotMixTally &tally)
{
    ShotMixBar bar;
    bar.notRead = tally.notRead;

    QStringList missIds = tally.count.keys();
    std::sort(missIds.begin(), missIds.end(), [&tally](const QString &a, const QString &b) {
        const int na = tally.count.value(a), nb = tally.count.value(b);
        return na != nb ? na > nb : a < b;
    });

    if (tally.good > 0) bar.parts.push_back(ShotMixPart{ QStringLiteral("good"), QStringLiteral("Good"), QStringLiteral("good"), tally.good, 0 });
    int otherCount = 0;
    for (int i = 0; i < missIds.size(); ++i) {
        if (i >= kShotMixMaxMisses) { otherCount += tally.count.value(missIds[i]); continue; }
        bar.parts.push_back(ShotMixPart{ missIds[i], tally.label.value(missIds[i]), i == 0 ? QStringLiteral("main") : QStringLiteral("other"),
                                         tally.count.value(missIds[i]), 0 });
    }
    if (otherCount > 0) bar.parts.push_back(ShotMixPart{ QStringLiteral("other"), QStringLiteral("Other"), QStringLiteral("other"), otherCount, 0 });

    for (const ShotMixPart &p : bar.parts) bar.judged += p.count;

    // Largest remainder, so the words always add up to 100.
    if (bar.judged > 0) {
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
    }

    QStringList words;
    for (const ShotMixPart &p : bar.parts) words << QStringLiteral("%1 %2%").arg(p.label).arg(p.percent);
    if (bar.notRead > 0) words << QStringLiteral("%1 not read").arg(bar.notRead);
    bar.line = words.join(QStringLiteral(" · "));
    return bar;
}

inline QJsonObject shotMixBarToJson(const ShotMixBar &bar)
{
    QJsonArray parts;
    for (const ShotMixPart &p : bar.parts)
        parts.append(QJsonObject{ { QStringLiteral("key"), p.key }, { QStringLiteral("label"), p.label }, { QStringLiteral("kind"), p.kind },
                                  { QStringLiteral("count"), p.count }, { QStringLiteral("percent"), p.percent } });
    return QJsonObject{ { QStringLiteral("judged"), bar.judged }, { QStringLiteral("notRead"), bar.notRead },
                        { QStringLiteral("line"), bar.line }, { QStringLiteral("parts"), parts } };
}

inline ShotMixBar shotMixBarFromJson(const QJsonObject &o)
{
    ShotMixBar bar;
    bar.judged  = o.value(QStringLiteral("judged")).toInt();
    bar.notRead = o.value(QStringLiteral("notRead")).toInt();
    bar.line    = o.value(QStringLiteral("line")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject p = v.toObject();
        bar.parts.push_back(ShotMixPart{ p.value(QStringLiteral("key")).toString(), p.value(QStringLiteral("label")).toString(),
                                         p.value(QStringLiteral("kind")).toString(), p.value(QStringLiteral("count")).toInt(),
                                         p.value(QStringLiteral("percent")).toInt() });
    }
    return bar;
}

} // namespace pinpoint::analysis
