/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE SESSION'S TYPED RESULTS (refactor step 3.5-ii; build/refactor/step-3/3.5/FIELD-LIST.md, accepted by Mark on
// 16 Nov 2026 as DEC-173's typed results; DEC-192: the summary grows by FIELD-LIST §3's in-memory types).
//
// WHAT IT IS. The value types of what a session concludes as a whole — its work-ons, what it could and could not
// say about every habit, the evidence for each shot's "because", per club the usual flight, the carry today against
// the frozen usual, the factor mix and the club baseline window (D7), the reasons, and per shot its outcome and
// three factors — and their JSON, the shape of work_ons.json (FILE-SESS-05). IDS AND NUMBERS ONLY (R-DER-9): every
// golfer-facing sentence is the words module's (until step 3.7, the thin adapter's), never written here.
//
// COMPOSES THE EXISTING TYPES (FIELD-LIST §0; audit C-10): SessionWorkOns, WorkOnEntry, WorkOnReasons, RootBecause,
// WorkOnEvidence (work_ons.h), TendencySessionEvidence (tendencies.h), ShotMixBar (shot_outcomes.h), ClubFlightUsual
// (strike_from_flight.h), ClubSessionPool (club_distances.h), ShotOutcome's id and number fields, ShotFactors. New
// structs only where none exists. The root is NOT called SessionSummary (pinpoint::SessionSummary is the session
// list's row, src/Gui/review/session_summary.h).
//
// THE FILE (FILE-SESS-05). Today's keys, unchanged (schemaVersion 3, ruleVersion, sessionId … evidence,
// outcomeEvidence, derivedFrom), and DEC-192's: `resultsVersion` (1), `shots`, `usuals`, `carryToday`, `clubMix`,
// `clubWindows` (D7: DEC-172), `reasons`, `because`, `shotMix`. An older file without them still loads: each is
// empty and `hasGrowth` is false. THE TOLERANT READ (FIELD-LIST F12; Mark, 16 Nov): `Lenient` reads a file of an
// older rule or schema as it stands (the report's "Earlier today" does); a NEWER schema is never read as ours.
//
// Names (G-08: the frozen usual and the session's own usual are not both "usual"): `ClubFlightUsual` is THIS
// session's median flight per club (GL-25); `ClubCarryToday::baseline*` is the FROZEN club baseline of earlier days
// (FILE-SESS-04). G-09: `ClubFactorMix` counts EVERY value a shot shows (a factor's parts may add up to more than its
// shots), which Tendencies' bar does not (each shot once): two rules, two names (`countsEveryValue`).
//
// Rules ring (src/Coaching): values and JSON only.

#include "club_distances.h"
#include "shot_factors.h"
#include "shot_outcomes.h"
#include "strike_from_flight.h"
#include "tendencies.h"
#include "work_ons.h"
#include "../Diagnostics/measures/club_baseline_provider.h"

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// THE LINK GRADE IS STORED AS ITS POSITION (FIELD-LIST F10): outcomeEvidence.grades, because[].links, shots[].outcome
// .links and diagnostics.json's derived outcomes all write int(LinkGrade). Reordering the enum would silently change
// every stored file, so its positions are pinned here (and checked by session_diagnostics_test, RJe).
static_assert(int(LinkGrade::Unanchored) == 0 && int(LinkGrade::PresentTogether) == 1 && int(LinkGrade::Coherent) == 2
                  && int(LinkGrade::ConditionallyDependent) == 3 && int(LinkGrade::MovedTogether) == 4,
              "LinkGrade is persisted by position: never reorder it (FIELD-LIST F10)");

// DEC-192's keys' own version (today's keys keep kWorkOnSchemaVersion / kWorkOnRuleVersion).
// 1 (16 Nov 2026, refactor 3.5-ii): shots, usuals, carryToday, clubMix, clubWindows, reasons, because, shotMix.
inline constexpr int kSessionResultsVersion = 1;

// ── FIELD-LIST §3.1: one club's carry today against the FROZEN usual ─────────────────────────────────────────────
struct ClubCarryToday {
    QString clubId;                                                   // canonical
    double  mean = std::numeric_limits<double>::quiet_NaN();          // full swings, mishits left out; NaN: "-"
    double  sd   = std::numeric_limits<double>::quiet_NaN();          // NaN below the floor
    int     n    = 0;
    // The frozen club baseline (FILE-SESS-04), only when EARNED (ClubBaselineEntry::earned: B-22, G-08); NaN / 0 else.
    double  baselineMean = std::numeric_limits<double>::quiet_NaN();
    double  baselineSd   = std::numeric_limits<double>::quiet_NaN();
    int     baselineN    = 0;
};

// ── FIELD-LIST §3.2: one club's shots by factor (G-09: every value a shot shows is counted) ──────────────────────
struct ClubFactorMix {
    QString    clubId;
    int        judged = 0;
    ShotMixBar strike, direction, flight;   // ShotMixBar's `line`/part labels are words: never written (R-DER-9)
    static constexpr bool countsEveryValue = true;
};

// ── FIELD-LIST §3.5 ───────────────────────────────────────────────────────────────────────────────────────────────
struct SessionShotMix {
    int corrections = 0;   // labels the golfer corrected
    int fromFlight  = 0;   // strikes read from the ball flight
};

// ── FIELD-LIST §3.6–3.8: the reasons ──────────────────────────────────────────────────────────────────────────────
struct ReasonProduced {
    QString mixKey;        // the miss kind, as ShotOutcome::mixKey ("fat slice")
    int     count = 0;
};
struct ReasonTick {
    ShotState state = ShotState::NotAssessable;
    int       shotId = 0;
};
struct SessionReason {
    QString conditionId;            // empty: the unexplained row ("no reason found"), last
    int     shots = 0;              // bad shots it was behind
    std::vector<ReasonProduced> produced;
    double  cost = std::numeric_limits<double>::quiet_NaN();   // carry lost, yd, against an EARNED usual only (G-08)
    std::optional<Tier> tier;       // the habit's tier this session; none for the unexplained row
    QString strength;               // clear / likely / possible / ""
    bool    chainConfirmed = false;
    int     trend = 0;              // +1 growing, 0, −1 easing
    std::vector<ReasonTick> ticks;  // one per shot, strike order
};

// ── J-14 (b): a shot's "because" is the SESSION'S (its links and tiers), held with the reasons ────────────────────
// The driver and graded links composeShotOutcome names when it is handed the session's evidence; the shot's own
// outcome (its derived data, layer 3) carries none. A screen joins the two by shotId.
struct ShotBecause {
    int                          shotId = 0;
    ShotOutcomeDriver            driver;   // ids and numbers only are written (id, displayId, strength, explains, of)
    std::vector<ShotOutcomeLink> links;    // fromId, toId, grade, physics are written; `words` never
};

// ── DEC-192: per shot, its outcome and three factors (the shot's derived data as the session holds it) ───────────
struct ShotResult {
    int         shotId = 0;
    QString     club;        // canonical, the effective club the session pooled it under
    ShotOutcome outcome;     // ids and numbers only are written (R-DER-9)
    ShotFactors factors;
};

// D7 (DEC-172): one club's baseline window from this session — its qualifying shots and mishits, in strike order.
// The type is today's pool (club_distances.h); `folder` and `day` are the session's own and are not written.
using ClubWindow = ClubSessionPool;

// ── FIELD-LIST §1.8 ───────────────────────────────────────────────────────────────────────────────────────────────
struct SummaryProvenance {
    QString fingerprint;     // read by the catch-up's freshness check (today WorkOnsController's own hash)
    QString contentStamp;
    QString gradePolicy;
    double  writtenAtMs = 0.0;
};

// ── FIELD-LIST §1.1: the root ─────────────────────────────────────────────────────────────────────────────────────
struct SessionResults {
    SessionWorkOns          workOns;          // today's keys §1.1–1.5 (sessionId … evidence)
    TendencySessionEvidence outcomeEvidence;  // §1.6
    SummaryProvenance       derivedFrom;      // §1.8
    // DEC-192's growth (FIELD-LIST §3), so the athlete level reads summaries only (DEC-92, R-DER-3):
    bool                                hasGrowth = false;   // false: an older file, read without them
    std::vector<ShotResult>             shots;               // strike order
    QHash<QString, ClubFlightUsual>     usuals;              // GL-25, by canonical club (G-10: stored, not recomputed)
    std::vector<ClubCarryToday>         carryToday;          // clubs in first-hit order
    std::vector<ClubFactorMix>          clubMix;             // clubs in first-hit order; a club with none judged left out
    std::vector<ClubWindow>             clubWindows;         // D7, clubs in first-hit order
    std::vector<SessionReason>          reasons;             // most shots first, the unexplained last
    std::vector<ShotBecause>            because;             // J-14 (b), strike order
    SessionShotMix                      shotMix;
};

// ── JSON ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace session_results_detail {

inline QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }   // NaN → null
inline double numOf(const QJsonValue &v)
{
    return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
}
inline QString tierKey(Tier t)
{
    return t == Tier::Pattern ? QStringLiteral("pattern") : t == Tier::Watching ? QStringLiteral("watching")
                                                                                 : QStringLiteral("clean");
}
inline Tier tierOfKey(const QString &s)
{
    return s == QLatin1String("pattern") ? Tier::Pattern : s == QLatin1String("watching") ? Tier::Watching : Tier::Clean;
}

} // namespace session_results_detail

inline QJsonObject clubFlightUsualToJson(const ClubFlightUsual &u)
{
    using session_results_detail::num;
    return QJsonObject{ { QStringLiteral("launch"), num(u.launch) }, { QStringLiteral("smash"), num(u.smash) },
                        { QStringLiteral("attack"), num(u.attack) }, { QStringLiteral("carry"), num(u.carry) } };
}
inline ClubFlightUsual clubFlightUsualFromJson(const QJsonObject &o)
{
    using session_results_detail::numOf;
    ClubFlightUsual u;
    u.launch = numOf(o.value(QStringLiteral("launch")));
    u.smash  = numOf(o.value(QStringLiteral("smash")));
    u.attack = numOf(o.value(QStringLiteral("attack")));
    u.carry  = numOf(o.value(QStringLiteral("carry")));
    return u;
}

inline QJsonObject shotFactorToJson(const ShotFactor &f)
{
    return QJsonObject{ { QStringLiteral("read"), f.read },
                        { QStringLiteral("values"), QJsonArray::fromStringList(f.values) },
                        { QStringLiteral("faults"), QJsonArray::fromStringList(f.faults) },
                        { QStringLiteral("method"), f.method },
                        { QStringLiteral("rawValue"), session_results_detail::num(f.rawValue) } };
}
inline ShotFactor shotFactorFromJson(const QJsonObject &o)
{
    ShotFactor f;
    f.read = o.value(QStringLiteral("read")).toBool();
    for (const QJsonValue &v : o.value(QStringLiteral("values")).toArray()) f.values << v.toString();
    for (const QJsonValue &v : o.value(QStringLiteral("faults")).toArray()) f.faults << v.toString();
    f.method   = o.value(QStringLiteral("method")).toString();
    f.rawValue = session_results_detail::numOf(o.value(QStringLiteral("rawValue")));
    return f;
}
inline QJsonObject shotFactorsToJson(const ShotFactors &f)
{
    return QJsonObject{ { QStringLiteral("strike"), shotFactorToJson(f.strike) },
                        { QStringLiteral("direction"), shotFactorToJson(f.direction) },
                        { QStringLiteral("flight"), shotFactorToJson(f.flight) } };
}
inline ShotFactors shotFactorsFromJson(const QJsonObject &o)
{
    ShotFactors f;
    f.strike    = shotFactorFromJson(o.value(QStringLiteral("strike")).toObject());
    f.direction = shotFactorFromJson(o.value(QStringLiteral("direction")).toObject());
    f.flight    = shotFactorFromJson(o.value(QStringLiteral("flight")).toObject());
    return f;
}

inline QJsonObject shotOutcomeDriverToJson(const ShotOutcomeDriver &d)
{
    return QJsonObject{ { QStringLiteral("id"), d.id }, { QStringLiteral("displayId"), d.displayId },
                        { QStringLiteral("strength"), d.strength }, { QStringLiteral("explains"), d.explains },
                        { QStringLiteral("of"), d.of } };
}
inline ShotOutcomeDriver shotOutcomeDriverFromJson(const QJsonObject &o)
{
    ShotOutcomeDriver d;
    d.id        = o.value(QStringLiteral("id")).toString();
    d.displayId = o.value(QStringLiteral("displayId")).toString();
    d.strength  = o.value(QStringLiteral("strength")).toString();
    d.explains  = o.value(QStringLiteral("explains")).toInt();
    d.of        = o.value(QStringLiteral("of")).toInt();
    return d;
}
inline QJsonArray shotOutcomeLinksToJson(const std::vector<ShotOutcomeLink> &links)
{
    QJsonArray out;
    for (const ShotOutcomeLink &l : links)
        out.append(QJsonObject{ { QStringLiteral("fromId"), l.fromId }, { QStringLiteral("toId"), l.toId },
                                { QStringLiteral("grade"), int(l.grade) },   // the enum's number, exactly (F10)
                                { QStringLiteral("physics"), l.physics } });
    return out;
}
inline std::vector<ShotOutcomeLink> shotOutcomeLinksFromJson(const QJsonArray &a)
{
    std::vector<ShotOutcomeLink> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        ShotOutcomeLink l;
        l.fromId  = o.value(QStringLiteral("fromId")).toString();
        l.toId    = o.value(QStringLiteral("toId")).toString();
        l.grade   = LinkGrade(o.value(QStringLiteral("grade")).toInt());
        l.physics = o.value(QStringLiteral("physics")).toBool();
        out.push_back(std::move(l));
    }
    return out;
}

// A shot's outcome, IDS AND NUMBERS ONLY (R-DER-9): its labels (row id, display id, family, the flags, the readings
// that fired them), its driver and links (the session's "because", when composed with it), its carry waterfall's
// numbers, ready / noReading / clean and the mix key. Every sentence (phrase, story, golfer, short, abbr, lines,
// provenance, detail) is left out; read back they are empty.
inline QJsonObject shotOutcomeFactsToJson(const ShotOutcome &o)
{
    using session_results_detail::num;
    QJsonArray labels;
    for (const ShotOutcomeLabel &l : o.labels) {
        QJsonArray ev;
        for (const ShotOutcomeEvidence &e : l.evidence)
            ev.append(QJsonObject{ { QStringLiteral("measureId"), e.measureId }, { QStringLiteral("value"), num(e.value) } });
        labels.append(QJsonObject{
            { QStringLiteral("id"), l.id }, { QStringLiteral("displayId"), l.displayId },
            { QStringLiteral("family"), l.family }, { QStringLiteral("intended"), l.intended },
            { QStringLiteral("corrected"), l.corrected }, { QStringLiteral("inferred"), l.inferred },
            { QStringLiteral("absorbed"), l.absorbed }, { QStringLiteral("miss"), l.miss },
            { QStringLiteral("evidence"), ev } });
    }
    QJsonObject out{
        { QStringLiteral("ready"), o.ready }, { QStringLiteral("noReading"), o.noReading },
        { QStringLiteral("clean"), o.clean }, { QStringLiteral("mixKey"), o.mixKey },
        { QStringLiteral("labels"), labels },
        { QStringLiteral("driver"), shotOutcomeDriverToJson(o.driver) },
        { QStringLiteral("links"), shotOutcomeLinksToJson(o.links) },
    };
    if (o.carry.present) {
        QJsonArray parts;
        for (const ShotCarryPart &p : o.carry.parts)
            parts.append(QJsonObject{ { QStringLiteral("id"), p.id }, { QStringLiteral("loss"), num(p.loss) },
                                      { QStringLiteral("modelled"), p.modelled } });
        out.insert(QStringLiteral("carry"), QJsonObject{
            { QStringLiteral("usual"), num(o.carry.usual) }, { QStringLiteral("usualSd"), num(o.carry.usualSd) },
            { QStringLiteral("actual"), num(o.carry.actual) }, { QStringLiteral("shortBy"), num(o.carry.shortBy) },
            { QStringLiteral("parts"), parts }, { QStringLiteral("residual"), num(o.carry.residual) },
            { QStringLiteral("residualShown"), o.carry.residualShown }, { QStringLiteral("scaled"), o.carry.scaled } });
    }
    return out;
}
inline ShotOutcome shotOutcomeFactsFromJson(const QJsonObject &o)
{
    using session_results_detail::numOf;
    ShotOutcome out;
    out.ready     = o.value(QStringLiteral("ready")).toBool();
    out.noReading = o.value(QStringLiteral("noReading")).toBool();
    out.clean     = o.value(QStringLiteral("clean")).toBool();
    out.mixKey    = o.value(QStringLiteral("mixKey")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("labels")).toArray()) {
        const QJsonObject lo = v.toObject();
        ShotOutcomeLabel l;
        l.id        = lo.value(QStringLiteral("id")).toString();
        l.displayId = lo.value(QStringLiteral("displayId")).toString();
        l.family    = lo.value(QStringLiteral("family")).toString();
        l.intended  = lo.value(QStringLiteral("intended")).toBool();
        l.corrected = lo.value(QStringLiteral("corrected")).toBool();
        l.inferred  = lo.value(QStringLiteral("inferred")).toBool();
        l.absorbed  = lo.value(QStringLiteral("absorbed")).toBool();
        l.miss      = lo.value(QStringLiteral("miss")).toBool(true);
        for (const QJsonValue &e : lo.value(QStringLiteral("evidence")).toArray())
            l.evidence.push_back(ShotOutcomeEvidence{ e.toObject().value(QStringLiteral("measureId")).toString(),
                                                      numOf(e.toObject().value(QStringLiteral("value"))) });
        out.labels.push_back(std::move(l));
    }
    out.driver = shotOutcomeDriverFromJson(o.value(QStringLiteral("driver")).toObject());
    out.links  = shotOutcomeLinksFromJson(o.value(QStringLiteral("links")).toArray());
    if (o.contains(QStringLiteral("carry"))) {
        const QJsonObject c = o.value(QStringLiteral("carry")).toObject();
        out.carry.present       = true;
        out.carry.usual         = numOf(c.value(QStringLiteral("usual")));
        out.carry.usualSd       = numOf(c.value(QStringLiteral("usualSd")));
        out.carry.actual        = numOf(c.value(QStringLiteral("actual")));
        out.carry.shortBy       = numOf(c.value(QStringLiteral("shortBy")));
        out.carry.residual      = numOf(c.value(QStringLiteral("residual")));
        out.carry.residualShown = c.value(QStringLiteral("residualShown")).toBool();
        out.carry.scaled        = c.value(QStringLiteral("scaled")).toBool();
        for (const QJsonValue &v : c.value(QStringLiteral("parts")).toArray()) {
            ShotCarryPart p;
            p.id       = v.toObject().value(QStringLiteral("id")).toString();
            p.loss     = numOf(v.toObject().value(QStringLiteral("loss")));
            p.modelled = v.toObject().value(QStringLiteral("modelled")).toBool();
            out.carry.parts.push_back(std::move(p));
        }
    }
    return out;
}

// A club's window (D7): its shots' LM values by key (carry, total, offline, ball speed, club speed, smash — today's
// pool, FIELD-LIST §5.2 point 2: total and offline kept), P4 shaft angle and position; its mishits by position.
inline QJsonObject clubWindowToJson(const ClubWindow &w)
{
    using session_results_detail::num;
    QJsonArray shots, mishits;
    for (const ClubPoolShot &s : w.shots) {
        QJsonObject lm;
        for (auto it = s.lm.constBegin(); it != s.lm.constEnd(); ++it) lm.insert(it.key(), num(it.value()));
        shots.append(QJsonObject{ { QStringLiteral("position"), s.position },
                                  { QStringLiteral("shaftAngleP4"), num(s.shaftAngleP4) },
                                  { QStringLiteral("lm"), lm } });
    }
    for (const ClubPoolMishit &m : w.mishits)
        mishits.append(QJsonObject{ { QStringLiteral("position"), m.position }, { QStringLiteral("kind"), m.kind } });
    return QJsonObject{ { QStringLiteral("clubId"), w.club }, { QStringLiteral("shots"), shots },
                        { QStringLiteral("mishits"), mishits } };
}
inline ClubWindow clubWindowFromJson(const QJsonObject &o)
{
    ClubWindow w;
    w.club = o.value(QStringLiteral("clubId")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("shots")).toArray()) {
        const QJsonObject so = v.toObject();
        ClubPoolShot s;
        s.position     = so.value(QStringLiteral("position")).toInt();
        s.shaftAngleP4 = session_results_detail::numOf(so.value(QStringLiteral("shaftAngleP4")));
        const QJsonObject lm = so.value(QStringLiteral("lm")).toObject();
        for (auto it = lm.constBegin(); it != lm.constEnd(); ++it)
            if (it.value().isDouble()) s.lm.insert(it.key(), it.value().toDouble());
        w.shots.push_back(std::move(s));
    }
    for (const QJsonValue &v : o.value(QStringLiteral("mishits")).toArray())
        w.mishits.push_back(ClubPoolMishit{ v.toObject().value(QStringLiteral("position")).toInt(),
                                            v.toObject().value(QStringLiteral("kind")).toString() });
    return w;
}

inline QJsonObject shotMixBarFactsToJson(const ShotMixBar &bar)
{
    QJsonArray parts;
    for (const ShotMixPart &p : bar.parts)
        parts.append(QJsonObject{ { QStringLiteral("key"), p.key }, { QStringLiteral("kind"), p.kind },
                                  { QStringLiteral("count"), p.count }, { QStringLiteral("percent"), p.percent } });
    return QJsonObject{ { QStringLiteral("judged"), bar.judged }, { QStringLiteral("notRead"), bar.notRead },
                        { QStringLiteral("parts"), parts } };
}
inline ShotMixBar shotMixBarFactsFromJson(const QJsonObject &o)
{
    ShotMixBar bar;
    bar.judged  = o.value(QStringLiteral("judged")).toInt();
    bar.notRead = o.value(QStringLiteral("notRead")).toInt();
    for (const QJsonValue &v : o.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject p = v.toObject();
        ShotMixPart part;
        part.key     = p.value(QStringLiteral("key")).toString();
        part.kind    = p.value(QStringLiteral("kind")).toString();
        part.count   = p.value(QStringLiteral("count")).toInt();
        part.percent = p.value(QStringLiteral("percent")).toInt();
        bar.parts.push_back(std::move(part));
    }
    return bar;
}

inline QString reasonTickStateKey(ShotState s)
{
    return s == ShotState::Fired ? QStringLiteral("fired") : s == ShotState::Clean ? QStringLiteral("clean")
                                                                                     : QStringLiteral("notAssessable");
}

inline QJsonObject sessionResultsToJson(const SessionResults &r)
{
    using session_results_detail::num;
    QJsonObject root = toJson(r.workOns);
    root.insert(QStringLiteral("outcomeEvidence"), tendencyEvidenceToJson(r.outcomeEvidence));
    root.insert(QStringLiteral("derivedFrom"), QJsonObject{
        { QStringLiteral("fingerprint"), r.derivedFrom.fingerprint },
        { QStringLiteral("contentStamp"), r.derivedFrom.contentStamp },
        { QStringLiteral("gradePolicy"), r.derivedFrom.gradePolicy },
        { QStringLiteral("writtenAtMs"), r.derivedFrom.writtenAtMs } });

    // DEC-192's growth.
    root.insert(QStringLiteral("resultsVersion"), kSessionResultsVersion);
    QJsonArray shots, carry, mix, windows, reasons, because;
    for (const ShotResult &s : r.shots)
        shots.append(QJsonObject{ { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("clubId"), s.club },
                                  { QStringLiteral("outcome"), shotOutcomeFactsToJson(s.outcome) },
                                  { QStringLiteral("factors"), shotFactorsToJson(s.factors) } });
    QJsonObject usuals;
    for (auto it = r.usuals.constBegin(); it != r.usuals.constEnd(); ++it)
        usuals.insert(it.key(), clubFlightUsualToJson(it.value()));
    for (const ClubCarryToday &c : r.carryToday)
        carry.append(QJsonObject{ { QStringLiteral("clubId"), c.clubId }, { QStringLiteral("mean"), num(c.mean) },
                                  { QStringLiteral("sd"), num(c.sd) }, { QStringLiteral("n"), c.n },
                                  { QStringLiteral("baselineMean"), num(c.baselineMean) },
                                  { QStringLiteral("baselineSd"), num(c.baselineSd) },
                                  { QStringLiteral("baselineN"), c.baselineN } });
    for (const ClubFactorMix &m : r.clubMix)
        mix.append(QJsonObject{ { QStringLiteral("clubId"), m.clubId }, { QStringLiteral("judged"), m.judged },
                                { QStringLiteral("strike"), shotMixBarFactsToJson(m.strike) },
                                { QStringLiteral("direction"), shotMixBarFactsToJson(m.direction) },
                                { QStringLiteral("flight"), shotMixBarFactsToJson(m.flight) } });
    for (const ClubWindow &w : r.clubWindows) windows.append(clubWindowToJson(w));
    for (const SessionReason &x : r.reasons) {
        QJsonArray produced, ticks;
        for (const ReasonProduced &p : x.produced)
            produced.append(QJsonObject{ { QStringLiteral("mixKey"), p.mixKey }, { QStringLiteral("count"), p.count } });
        for (const ReasonTick &t : x.ticks)
            ticks.append(QJsonObject{ { QStringLiteral("state"), reasonTickStateKey(t.state) },
                                      { QStringLiteral("shotId"), t.shotId } });
        QJsonObject o{ { QStringLiteral("conditionId"), x.conditionId }, { QStringLiteral("shots"), x.shots },
                       { QStringLiteral("produced"), produced }, { QStringLiteral("cost"), num(x.cost) },
                       { QStringLiteral("strength"), x.strength }, { QStringLiteral("chainConfirmed"), x.chainConfirmed },
                       { QStringLiteral("trend"), x.trend }, { QStringLiteral("ticks"), ticks } };
        if (x.tier) o.insert(QStringLiteral("tier"), session_results_detail::tierKey(*x.tier));
        reasons.append(o);
    }
    for (const ShotBecause &b : r.because)
        because.append(QJsonObject{ { QStringLiteral("shotId"), b.shotId },
                                    { QStringLiteral("driver"), shotOutcomeDriverToJson(b.driver) },
                                    { QStringLiteral("links"), shotOutcomeLinksToJson(b.links) } });
    root.insert(QStringLiteral("shots"), shots);
    root.insert(QStringLiteral("usuals"), usuals);
    root.insert(QStringLiteral("carryToday"), carry);
    root.insert(QStringLiteral("clubMix"), mix);
    root.insert(QStringLiteral("clubWindows"), windows);
    root.insert(QStringLiteral("reasons"), reasons);
    root.insert(QStringLiteral("because"), because);
    root.insert(QStringLiteral("shotMix"), QJsonObject{ { QStringLiteral("corrections"), r.shotMix.corrections },
                                                        { QStringLiteral("fromFlight"), r.shotMix.fromFlight } });
    return root;
}

// How an older file is read (FIELD-LIST F12).
enum class SessionResultsRead {
    Strict,    // today's rule: a file of another schema or rule is not this build's answer (ok = false, empty)
    Lenient,   // the report's "Earlier today": an older rule's or schema's file read as it stands; a NEWER schema never
};

inline SessionResults sessionResultsFromJson(const QJsonObject &root, SessionResultsRead mode = SessionResultsRead::Strict,
                                             bool *ok = nullptr)
{
    using session_results_detail::numOf;
    SessionResults r;
    QJsonObject src = root;
    if (mode == SessionResultsRead::Lenient && !src.isEmpty()
        && src.value(QStringLiteral("schemaVersion")).toInt(0) <= kWorkOnSchemaVersion) {
        src[QStringLiteral("schemaVersion")] = kWorkOnSchemaVersion;
        src[QStringLiteral("ruleVersion")]   = kWorkOnRuleVersion;
    }
    bool current = false;
    r.workOns = sessionWorkOnsFromJson(src, &current);
    if (ok) *ok = current;
    if (!current) return SessionResults{};
    r.outcomeEvidence = tendencyEvidenceFromJson(src.value(QStringLiteral("outcomeEvidence")).toObject());
    const QJsonObject from = src.value(QStringLiteral("derivedFrom")).toObject();
    r.derivedFrom.fingerprint  = from.value(QStringLiteral("fingerprint")).toString();
    r.derivedFrom.contentStamp = from.value(QStringLiteral("contentStamp")).toString();
    r.derivedFrom.gradePolicy  = from.value(QStringLiteral("gradePolicy")).toString();
    r.derivedFrom.writtenAtMs  = from.value(QStringLiteral("writtenAtMs")).toDouble();

    // DEC-192's growth: absent in an older file (each left empty), never guessed.
    const int version = src.value(QStringLiteral("resultsVersion")).toInt(0);
    if (version <= 0 || version > kSessionResultsVersion)
        return r;   // older (none), or a newer growth this build cannot read: today's keys only
    r.hasGrowth = true;
    for (const QJsonValue &v : src.value(QStringLiteral("shots")).toArray()) {
        const QJsonObject o = v.toObject();
        ShotResult s;
        s.shotId  = o.value(QStringLiteral("shotId")).toInt();
        s.club    = o.value(QStringLiteral("clubId")).toString();
        s.outcome = shotOutcomeFactsFromJson(o.value(QStringLiteral("outcome")).toObject());
        s.factors = shotFactorsFromJson(o.value(QStringLiteral("factors")).toObject());
        r.shots.push_back(std::move(s));
    }
    const QJsonObject usuals = src.value(QStringLiteral("usuals")).toObject();
    for (auto it = usuals.constBegin(); it != usuals.constEnd(); ++it)
        r.usuals.insert(it.key(), clubFlightUsualFromJson(it.value().toObject()));
    for (const QJsonValue &v : src.value(QStringLiteral("carryToday")).toArray()) {
        const QJsonObject o = v.toObject();
        ClubCarryToday c;
        c.clubId       = o.value(QStringLiteral("clubId")).toString();
        c.mean         = numOf(o.value(QStringLiteral("mean")));
        c.sd           = numOf(o.value(QStringLiteral("sd")));
        c.n            = o.value(QStringLiteral("n")).toInt();
        c.baselineMean = numOf(o.value(QStringLiteral("baselineMean")));
        c.baselineSd   = numOf(o.value(QStringLiteral("baselineSd")));
        c.baselineN    = o.value(QStringLiteral("baselineN")).toInt();
        r.carryToday.push_back(std::move(c));
    }
    for (const QJsonValue &v : src.value(QStringLiteral("clubMix")).toArray()) {
        const QJsonObject o = v.toObject();
        ClubFactorMix m;
        m.clubId    = o.value(QStringLiteral("clubId")).toString();
        m.judged    = o.value(QStringLiteral("judged")).toInt();
        m.strike    = shotMixBarFactsFromJson(o.value(QStringLiteral("strike")).toObject());
        m.direction = shotMixBarFactsFromJson(o.value(QStringLiteral("direction")).toObject());
        m.flight    = shotMixBarFactsFromJson(o.value(QStringLiteral("flight")).toObject());
        r.clubMix.push_back(std::move(m));
    }
    for (const QJsonValue &v : src.value(QStringLiteral("clubWindows")).toArray())
        r.clubWindows.push_back(clubWindowFromJson(v.toObject()));
    for (const QJsonValue &v : src.value(QStringLiteral("reasons")).toArray()) {
        const QJsonObject o = v.toObject();
        SessionReason x;
        x.conditionId = o.value(QStringLiteral("conditionId")).toString();
        x.shots       = o.value(QStringLiteral("shots")).toInt();
        for (const QJsonValue &p : o.value(QStringLiteral("produced")).toArray())
            x.produced.push_back(ReasonProduced{ p.toObject().value(QStringLiteral("mixKey")).toString(),
                                                 p.toObject().value(QStringLiteral("count")).toInt() });
        x.cost = numOf(o.value(QStringLiteral("cost")));
        if (o.contains(QStringLiteral("tier")))
            x.tier = session_results_detail::tierOfKey(o.value(QStringLiteral("tier")).toString());
        x.strength       = o.value(QStringLiteral("strength")).toString();
        x.chainConfirmed = o.value(QStringLiteral("chainConfirmed")).toBool();
        x.trend          = o.value(QStringLiteral("trend")).toInt();
        for (const QJsonValue &t : o.value(QStringLiteral("ticks")).toArray())
            x.ticks.push_back(ReasonTick{ shotStateFromString(t.toObject().value(QStringLiteral("state")).toString()),
                                          t.toObject().value(QStringLiteral("shotId")).toInt() });
        r.reasons.push_back(std::move(x));
    }
    for (const QJsonValue &v : src.value(QStringLiteral("because")).toArray()) {
        const QJsonObject o = v.toObject();
        ShotBecause b;
        b.shotId = o.value(QStringLiteral("shotId")).toInt();
        b.driver = shotOutcomeDriverFromJson(o.value(QStringLiteral("driver")).toObject());
        b.links  = shotOutcomeLinksFromJson(o.value(QStringLiteral("links")).toArray());
        r.because.push_back(std::move(b));
    }
    const QJsonObject mix = src.value(QStringLiteral("shotMix")).toObject();
    r.shotMix.corrections = mix.value(QStringLiteral("corrections")).toInt();
    r.shotMix.fromFlight  = mix.value(QStringLiteral("fromFlight")).toInt();
    return r;
}

// ── FIELD-LIST §2: diagnostics.json's session block (FILE-SESS-03), today's keys exactly ─────────────────────────
// The session's diagnosis state, as the old model's sessionBlock() writes it; SessionDiagnostics writes it the same
// (refactor 3.5-ii review 1: no key change), with its own `unread` (DEC-191) and `derived` (DEC-135) beside it.
// `sessionDir`, `sessionId`, `shotCount`, `gradePolicy`, `writtenAtMs` are read by nobody (FIELD-LIST §4.2 D3): kept.

inline QString sessionStageKey(Stage s)
{
    switch (s) {
    case Stage::Cold:        return QStringLiteral("cold");
    case Stage::Forming:     return QStringLiteral("forming");
    case Stage::Established: return QStringLiteral("established");
    case Stage::Closing:     break;
    }
    return QStringLiteral("closing");
}
inline Stage sessionStageOfKey(const QString &s)
{
    if (s == QLatin1String("forming"))     return Stage::Forming;
    if (s == QLatin1String("established")) return Stage::Established;
    if (s == QLatin1String("closing"))     return Stage::Closing;
    return Stage::Cold;
}

// FIELD-LIST §2.2: which version of a swing file was graded.
struct GradedFrom {
    qint64  size = 0;
    qint64  mtimeMs = 0;
    QString content;      // the stamp it was graded under
};

// FIELD-LIST §2.1.
struct SessionDiagnosisState {
    QString             sessionDir;            // the session folder's path (read by nobody)
    QString             sessionId;             // the folder's NAME (F1; read by nobody)
    bool                closed = false;        // the session ended (written from the Session's status: J-24)
    Stage               stage = Stage::Cold;
    bool                reachedEstablished = false;
    int                 shotCount = 0;         // read by nobody
    QString             gradePolicy;           // read by nobody
    QList<int>          lmShots;               // shots with a launch-monitor reading
    QMap<int, QString>  swingDirs;             // shot number → its swing folder
    QMap<int, GradedFrom> gradedFrom;
    double              writtenAtMs = 0.0;     // read by nobody
    bool                hasStage = false;      // read: the block held a `stage`
};

inline QJsonObject sessionDiagnosisStateToJson(const SessionDiagnosisState &st)
{
    QList<int> lm = st.lmShots;
    std::sort(lm.begin(), lm.end());
    QJsonArray lmArray;
    for (const int id : std::as_const(lm)) lmArray.append(id);
    QJsonObject dirs, graded;
    for (auto it = st.swingDirs.constBegin(); it != st.swingDirs.constEnd(); ++it) dirs.insert(QString::number(it.key()), it.value());
    for (auto it = st.gradedFrom.constBegin(); it != st.gradedFrom.constEnd(); ++it)
        graded.insert(QString::number(it.key()), QJsonObject{ { QStringLiteral("size"), double(it->size) },
                                                              { QStringLiteral("mtimeMs"), double(it->mtimeMs) },
                                                              { QStringLiteral("content"), it->content } });
    return QJsonObject{
        { QStringLiteral("sessionDir"), st.sessionDir }, { QStringLiteral("sessionId"), st.sessionId },
        { QStringLiteral("closed"), st.closed }, { QStringLiteral("stage"), sessionStageKey(st.stage) },
        { QStringLiteral("reachedEstablished"), st.reachedEstablished }, { QStringLiteral("shotCount"), st.shotCount },
        { QStringLiteral("gradePolicy"), st.gradePolicy }, { QStringLiteral("lmShots"), lmArray },
        { QStringLiteral("swingDirs"), dirs }, { QStringLiteral("gradedFrom"), graded },
        { QStringLiteral("writtenAtMs"), st.writtenAtMs } };
}

// Read as the old model's activation reads it: an absent `stage` is the first stage, an absent `reachedEstablished`
// is "the stage is established", an absent stamp is 0 (stale).
inline SessionDiagnosisState sessionDiagnosisStateFromJson(const QJsonObject &o)
{
    SessionDiagnosisState st;
    st.sessionDir  = o.value(QStringLiteral("sessionDir")).toString();
    st.sessionId   = o.value(QStringLiteral("sessionId")).toString();
    st.closed      = o.value(QStringLiteral("closed")).toBool(false);
    st.hasStage    = o.contains(QStringLiteral("stage"));
    st.stage       = sessionStageOfKey(o.value(QStringLiteral("stage")).toString());
    st.reachedEstablished = o.value(QStringLiteral("reachedEstablished")).toBool(st.stage == Stage::Established);
    st.shotCount   = o.value(QStringLiteral("shotCount")).toInt();
    st.gradePolicy = o.value(QStringLiteral("gradePolicy")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("lmShots")).toArray()) st.lmShots.append(v.toInt());
    const QJsonObject dirs = o.value(QStringLiteral("swingDirs")).toObject();
    for (auto it = dirs.constBegin(); it != dirs.constEnd(); ++it) st.swingDirs.insert(it.key().toInt(), it.value().toString());
    const QJsonObject graded = o.value(QStringLiteral("gradedFrom")).toObject();
    for (auto it = graded.constBegin(); it != graded.constEnd(); ++it) {
        const QJsonObject g = it.value().toObject();
        st.gradedFrom.insert(it.key().toInt(), GradedFrom{ qint64(g.value(QStringLiteral("size")).toDouble()),
                                                           qint64(g.value(QStringLiteral("mtimeMs")).toDouble()),
                                                           g.value(QStringLiteral("content")).toString() });
    }
    st.writtenAtMs = o.value(QStringLiteral("writtenAtMs")).toDouble();
    return st;
}

// ── D7 (DEC-172): the club baseline from the predecessors' SUMMARIES, no ledger read ──────────────────────────────

// One earlier session as its summary gives it: its place, and its club windows.
struct SummaryClubWindows {
    SessionPlace            place;
    std::vector<ClubWindow> windows;
};

// The usual numbers as of a session on `day`, from the summaries of the athlete's sessions: strictly earlier days
// only, newest sessions and shots first up to the caps — clubBaselineAsOf's rule over the same pools, read from the
// summaries instead of re-pooled from the ledgers (DEC-172). A club with no qualifying shot has no entry.
inline QHash<QString, ClubBaselineEntry> clubBaselineFromSummaries(const std::vector<SummaryClubWindows> &sessions,
                                                                   const QDate &day)
{
    QHash<QString, ClubBaselineEntry> out;
    if (!day.isValid()) return out;
    std::vector<const SummaryClubWindows *> earlier;
    for (const SummaryClubWindows &s : sessions)
        if (s.place.day.isValid() && s.place.day < day) earlier.push_back(&s);
    std::stable_sort(earlier.begin(), earlier.end(), [](const SummaryClubWindows *a, const SummaryClubWindows *b) {
        return sessionPlaceBefore(a->place, b->place);
    });
    QHash<QString, std::vector<ClubSessionPool>> byClub;
    QStringList order;
    for (const SummaryClubWindows *s : earlier)
        for (ClubSessionPool p : s->windows) {
            p.folder = s->place.folder;
            p.day    = s->place.day;
            if (!byClub.contains(p.club)) order << p.club;
            byClub[p.club].push_back(std::move(p));
        }
    for (const QString &club : std::as_const(order)) {
        const ClubDistanceWindow window = clubWindow(club, byClub.value(club));
        if (window.shotCount > 0) out.insert(club, clubBaselineEntryFromWindow(window));
    }
    return out;
}

} // namespace pinpoint::analysis
