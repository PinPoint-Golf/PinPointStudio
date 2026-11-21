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

#include "diag_parallel_run.h"

#include "session_diagnostics_model.h"
#include "../../DomainModel/athlete.h"   // DiagnosticsDoors
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../Export/firing_reader_store.h"
#include "../../Coaching/session_results.h"
#include "../../Coaching/strike_from_flight.h"
#include "../../Core/club_vocabulary.h"
#include "../../Core/pp_debug.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QMetaObject>
#include <QPointer>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pinpoint::parallel {

using namespace pinpoint::analysis;

namespace {

QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }

QString tierWord(Tier t)
{
    return t == Tier::Pattern ? QStringLiteral("pattern") : t == Tier::Watching ? QStringLiteral("watching")
                                                                                 : QStringLiteral("clean");
}
// The reasons' tier as the old model's map spells it (tierTag): "clean all session" for Clean, "" for none.
QString reasonTierWord(const std::optional<Tier> &t)
{
    if (!t) return QString();
    return *t == Tier::Clean ? QStringLiteral("clean all session") : tierWord(*t);
}

QJsonObject rowJson(const ConditionRow &r)
{
    QJsonArray readings;
    for (const MeasureRow &m : r.readings)
        readings.append(QJsonObject{
            { QStringLiteral("measureId"), m.measureId }, { QStringLiteral("signalId"), m.signalId },
            { QStringLiteral("value"), num(m.value) }, { QStringLiteral("corridorLo"), num(m.corridorLo) },
            { QStringLiteral("corridorHi"), num(m.corridorHi) }, { QStringLiteral("z"), num(m.z) },
            { QStringLiteral("corridorShape"), corridorShapeToString(m.corridorShape) },
            { QStringLiteral("firingSide"), firingSideToString(m.firingSide) },
            { QStringLiteral("faultAt"), num(m.faultAt) }, { QStringLiteral("fired"), m.fired } });
    return QJsonObject{
        { QStringLiteral("conditionId"), r.conditionId }, { QStringLiteral("state"), shotStateToString(r.state) },
        { QStringLiteral("confidence"), num(double(r.confidence)) }, { QStringLiteral("direction"), r.direction },
        { QStringLiteral("drivingMeasureId"), r.drivingMeasureId }, { QStringLiteral("value"), num(r.value) },
        { QStringLiteral("corridorLo"), num(r.corridorLo) }, { QStringLiteral("corridorHi"), num(r.corridorHi) },
        { QStringLiteral("z"), num(r.z) }, { QStringLiteral("corridorShape"), corridorShapeToString(r.corridorShape) },
        { QStringLiteral("firingSide"), firingSideToString(r.firingSide) }, { QStringLiteral("faultAt"), num(r.faultAt) },
        { QStringLiteral("allOf"), r.allOf }, { QStringLiteral("contextId"), r.contextId },
        { QStringLiteral("contextInferred"), r.contextInferred }, { QStringLiteral("material"), r.material },
        { QStringLiteral("sharp"), r.sharp }, { QStringLiteral("notAssessableReason"), r.notAssessableReason },
        { QStringLiteral("notJudgedGate"), r.notJudgedGate }, { QStringLiteral("notLiveStatus"), r.notLiveStatus },
        { QStringLiteral("pFire"), num(double(r.pFire)) }, { QStringLiteral("grossRisk"), num(double(r.grossRisk)) },
        { QStringLiteral("quantified"), r.quantified }, { QStringLiteral("firedSignalCount"), r.firedSignalCount },
        { QStringLiteral("overruled"), r.overruled }, { QStringLiteral("readings"), readings } };
}

QJsonObject shotJson(const ShotRecord &s, const ShotOutcome &outcome, const ShotFactors &factors)
{
    std::vector<const ConditionRow *> rows;
    for (const ConditionRow &r : s.rows) rows.push_back(&r);
    std::stable_sort(rows.begin(), rows.end(), [](const ConditionRow *a, const ConditionRow *b) {
        return a->conditionId < b->conditionId;
    });
    QJsonArray rowList;
    for (const ConditionRow *r : rows) rowList.append(rowJson(*r));
    return QJsonObject{
        { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("club"), s.club },
        { QStringLiteral("contextId"), s.contextId }, { QStringLiteral("dataWarning"), s.dataWarning },
        { QStringLiteral("handedness"), s.handedness }, { QStringLiteral("swingLength"), s.swingLength },
        { QStringLiteral("rows"), rowList }, { QStringLiteral("outcome"), shotOutcomeFactsToJson(outcome) },
        { QStringLiteral("factors"), shotFactorsToJson(factors) } };
}

QJsonArray conditionsJson(const std::vector<ConditionLedger> &ledgers)
{
    std::vector<const ConditionLedger *> sorted;
    for (const ConditionLedger &l : ledgers) sorted.push_back(&l);
    std::stable_sort(sorted.begin(), sorted.end(), [](const ConditionLedger *a, const ConditionLedger *b) { return a->id < b->id; });
    QJsonArray out;
    for (const ConditionLedger *l : sorted)
        out.append(QJsonObject{
            { QStringLiteral("id"), l->id }, { QStringLiteral("drivingMeasureId"), l->drivingMeasureId },
            { QStringLiteral("tier"), tierWord(l->tier) }, { QStringLiteral("assessable"), l->assessable },
            { QStringLiteral("fired"), l->fired }, { QStringLiteral("wilsonLower"), num(l->wilsonLower) },
            { QStringLiteral("pPattern"), num(l->pPattern) }, { QStringLiteral("recurrence"), l->recurrence },
            { QStringLiteral("firingExcess"), num(l->firingExcess) }, { QStringLiteral("medianExcess"), num(l->medianExcess) } });
    return out;
}

QJsonArray patternSet(const std::vector<ConditionLedger> &ledgers)
{
    QStringList ids;
    for (const ConditionLedger &l : ledgers)
        if (l.tier == Tier::Pattern) ids << l.id;
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

QJsonObject workOnsJson(const SessionWorkOns &w)
{
    QJsonObject o = toJson(w);
    QJsonArray entries;
    for (const QJsonValue &v : o.value(QStringLiteral("workOns")).toArray()) {
        QJsonObject e = v.toObject();
        e.remove(QStringLiteral("name"));   // a pack label (words), the adapter's
        entries.append(e);
    }
    o.insert(QStringLiteral("workOns"), entries);
    return o;
}

QJsonObject usualsJson(const QHash<QString, ClubFlightUsual> &usuals)
{
    QJsonObject o;
    for (auto it = usuals.constBegin(); it != usuals.constEnd(); ++it) o.insert(it.key(), clubFlightUsualToJson(it.value()));
    return o;
}

QJsonObject reasonJson(const QString &id, int shots, const QJsonArray &produced, double cost, const QString &tier,
                       const QString &strength, bool chainConfirmed, int trend, const QJsonArray &ticks)
{
    return QJsonObject{ { QStringLiteral("conditionId"), id }, { QStringLiteral("shots"), shots },
                        { QStringLiteral("produced"), produced }, { QStringLiteral("cost"), num(cost) },
                        { QStringLiteral("tier"), tier }, { QStringLiteral("strength"), strength },
                        { QStringLiteral("chainConfirmed"), chainConfirmed }, { QStringLiteral("trend"), trend },
                        { QStringLiteral("ticks"), ticks } };
}

QJsonObject carryJson(const QString &club, double mean, double sd, int n, double bMean, double bSd, int bN)
{
    return QJsonObject{ { QStringLiteral("clubId"), club }, { QStringLiteral("mean"), num(mean) },
                        { QStringLiteral("sd"), num(sd) }, { QStringLiteral("n"), n },
                        { QStringLiteral("baselineMean"), num(bMean) }, { QStringLiteral("baselineSd"), num(bSd) },
                        { QStringLiteral("baselineN"), bN } };
}

double mapNum(const QVariantMap &m, const char *key)
{
    const QVariant v = m.value(QLatin1String(key));
    return v.isValid() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

// ── The old model's form ──────────────────────────────────────────────────────────────────────────────────────────
QJsonObject formOf(const SessionDiagnosticsModel &m, bool withStamp)
{
    QJsonObject out;
    out.insert(QStringLiteral("stage"), m.stage());
    out.insert(QStringLiteral("reachedEstablished"), m.reachedEstablished());
    out.insert(QStringLiteral("shotCount"), int(m.shotRecords().size()));
    out.insert(QStringLiteral("patternSet"), patternSet(m.ledgerRows()));
    if (withStamp) out.insert(QStringLiteral("stamp"), m.contentStamp());
    out.insert(QStringLiteral("conditions"), conditionsJson(m.ledgerRows()));

    QJsonArray shots;
    std::vector<ShotOutcome> composed;
    for (const ShotRecord &s : m.shotRecords()) {
        composed.push_back(m.composedOutcomeOf(s.shotId));
        shots.append(shotJson(s, composed.back(), m.shotFactorsOf(s.shotId)));
    }
    out.insert(QStringLiteral("shots"), shots);
    const ShotMix mix = composeSessionMix(composed);
    out.insert(QStringLiteral("shotMix"), QJsonObject{ { QStringLiteral("corrections"), mix.corrections },
                                                       { QStringLiteral("fromFlight"), mix.fromFlight } });

    QJsonArray clubMix;
    for (const QVariant &v : m.clubMix()) {
        const QVariantMap c = v.toMap();
        clubMix.append(QJsonObject{
            { QStringLiteral("clubId"), c.value(QStringLiteral("clubId")).toString() },
            { QStringLiteral("judged"), c.value(QStringLiteral("judged")).toInt() },
            { QStringLiteral("strike"), shotMixBarFactsToJson(shotMixBarFromJson(QJsonObject::fromVariantMap(c.value(QStringLiteral("strike")).toMap()))) },
            { QStringLiteral("direction"), shotMixBarFactsToJson(shotMixBarFromJson(QJsonObject::fromVariantMap(c.value(QStringLiteral("direction")).toMap()))) },
            { QStringLiteral("flight"), shotMixBarFactsToJson(shotMixBarFromJson(QJsonObject::fromVariantMap(c.value(QStringLiteral("flight")).toMap()))) } });
    }
    out.insert(QStringLiteral("clubMix"), clubMix);

    QJsonArray carry;
    for (const QVariant &v : m.carryToday()) {
        const QVariantMap c = v.toMap();
        carry.append(carryJson(c.value(QStringLiteral("clubId")).toString(), mapNum(c, "mean"), mapNum(c, "sd"),
                               c.value(QStringLiteral("n")).toInt(), mapNum(c, "usualMean"), mapNum(c, "usualSd"),
                               c.value(QStringLiteral("usualN")).toInt()));
    }
    out.insert(QStringLiteral("carryToday"), carry);

    QJsonArray reasons;
    for (const QVariant &v : m.reasons()) {
        const QVariantMap r = v.toMap();
        QJsonArray produced, ticks;
        for (const QVariant &p : r.value(QStringLiteral("produced")).toList())
            produced.append(QJsonObject{ { QStringLiteral("phrase"), p.toMap().value(QStringLiteral("phrase")).toString() },
                                         { QStringLiteral("count"), p.toMap().value(QStringLiteral("count")).toInt() } });
        for (const QVariant &t : r.value(QStringLiteral("ticks")).toList())
            ticks.append(QJsonObject{ { QStringLiteral("state"), t.toMap().value(QStringLiteral("state")).toString() },
                                      { QStringLiteral("shotId"), t.toMap().value(QStringLiteral("shotId")).toInt() } });
        reasons.append(reasonJson(r.value(QStringLiteral("conditionId")).toString(), r.value(QStringLiteral("shots")).toInt(),
                                  produced, mapNum(r, "cost"), r.value(QStringLiteral("tier")).toString(),
                                  r.value(QStringLiteral("strength")).toString(),
                                  r.value(QStringLiteral("chainConfirmed")).toBool(), r.value(QStringLiteral("trend")).toInt(),
                                  ticks));
    }
    out.insert(QStringLiteral("reasons"), reasons);
    out.insert(QStringLiteral("workOns"), workOnsJson(m.sessionWorkOns()));
    out.insert(QStringLiteral("outcomeEvidence"), tendencyEvidenceToJson(m.outcomeEvidence()));
    out.insert(QStringLiteral("usuals"), usualsJson(sessionFlightUsuals(m.shotRecords())));   // the old model's, recomputed (G-10)
    return out;
}

// ── The new owner's form ──────────────────────────────────────────────────────────────────────────────────────────
QJsonObject formOf(const SessionDiagnostics &d, bool withStamp)
{
    const SessionDiagnosis *dg = d.diagnosis();
    if (!dg || !d.snapshot()) return {};
    const SessionResults &r = dg->results;
    QJsonObject out;
    out.insert(QStringLiteral("stage"), [&] {
        switch (dg->stage) {
        case Stage::Cold:        return QStringLiteral("cold");
        case Stage::Forming:     return QStringLiteral("forming");
        case Stage::Established: return QStringLiteral("established");
        case Stage::Closing:     break;
        }
        return QStringLiteral("closing");
    }());
    out.insert(QStringLiteral("reachedEstablished"), dg->reachedEstablished);
    out.insert(QStringLiteral("shotCount"), int(r.shots.size()));
    out.insert(QStringLiteral("patternSet"), patternSet(dg->ledgers));
    if (withStamp) out.insert(QStringLiteral("stamp"), d.findingsStamp());
    out.insert(QStringLiteral("conditions"), conditionsJson(dg->ledgers));

    // Each shot: its rows as the file holds them, its OWN outcome with the session's "because" joined (J-14 (b)).
    QHash<int, const ShotBecause *> because;
    for (const ShotBecause &b : r.because) because.insert(b.shotId, &b);
    QJsonArray shots;
    const FlightNorms &flight = d.snapshot()->inputs.flightNorms;
    for (const ShotResult &s : r.shots) {
        const KeptShot *k = d.kept(s.shotId);
        if (!k) continue;
        ShotRecord rec = k->findings;
        checkShotAgainstFlight(rec, d.usuals(), flight);
        ShotOutcome o = s.outcome;
        if (const ShotBecause *b = because.value(s.shotId)) {
            o.driver = b->driver;
            o.links  = b->links;
        }
        shots.append(shotJson(rec, o, s.factors));
    }
    out.insert(QStringLiteral("shots"), shots);
    out.insert(QStringLiteral("shotMix"), QJsonObject{ { QStringLiteral("corrections"), r.shotMix.corrections },
                                                       { QStringLiteral("fromFlight"), r.shotMix.fromFlight } });
    QJsonArray clubMix;
    for (const ClubFactorMix &m : r.clubMix)
        clubMix.append(QJsonObject{ { QStringLiteral("clubId"), m.clubId }, { QStringLiteral("judged"), m.judged },
                                    { QStringLiteral("strike"), shotMixBarFactsToJson(m.strike) },
                                    { QStringLiteral("direction"), shotMixBarFactsToJson(m.direction) },
                                    { QStringLiteral("flight"), shotMixBarFactsToJson(m.flight) } });
    out.insert(QStringLiteral("clubMix"), clubMix);
    QJsonArray carry;
    for (const ClubCarryToday &c : r.carryToday)
        carry.append(carryJson(c.clubId, c.mean, c.sd, c.n, c.baselineMean, c.baselineSd, c.baselineN));
    out.insert(QStringLiteral("carryToday"), carry);
    QJsonArray reasons;
    for (const SessionReason &x : r.reasons) {
        QJsonArray produced, ticks;
        for (const ReasonProduced &p : x.produced)
            produced.append(QJsonObject{ { QStringLiteral("phrase"), outcomeCapitalised(p.mixKey) },
                                         { QStringLiteral("count"), p.count } });
        for (const ReasonTick &t : x.ticks)
            ticks.append(QJsonObject{ { QStringLiteral("state"), reasonTickStateKey(t.state) },
                                      { QStringLiteral("shotId"), t.shotId } });
        reasons.append(reasonJson(x.conditionId, x.shots, produced, x.cost, reasonTierWord(x.tier), x.strength,
                                  x.chainConfirmed, x.trend, ticks));
    }
    out.insert(QStringLiteral("reasons"), reasons);
    out.insert(QStringLiteral("workOns"), workOnsJson(r.workOns));
    out.insert(QStringLiteral("outcomeEvidence"), tendencyEvidenceToJson(r.outcomeEvidence));
    out.insert(QStringLiteral("usuals"), usualsJson(r.usuals));
    return out;
}

// ── The difference ───────────────────────────────────────────────────────────────────────────────────────────────
namespace {

QString idOf(const QJsonObject &o)
{
    for (const char *k : { "shotId", "conditionId", "id", "clubId", "from" })
        if (o.contains(QLatin1String(k))) {
            const QJsonValue v = o.value(QLatin1String(k));
            QString id = v.isDouble() ? QString::number(v.toDouble()) : v.toString();
            if (o.contains(QStringLiteral("to"))) id += QStringLiteral("→") + o.value(QStringLiteral("to")).toString();
            return QString::fromLatin1(k) + QLatin1Char('=') + id;
        }
    return QString();
}

QString shortText(const QJsonValue &v)
{
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 10);
    if (v.isString()) return QLatin1Char('"') + v.toString().left(40) + QLatin1Char('"');
    if (v.isBool())   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isNull() || v.isUndefined()) return QStringLiteral("null");
    if (v.isArray())  return QStringLiteral("[%1]").arg(v.toArray().size());
    return QStringLiteral("{…}");
}

void diffInto(const QString &path, const QJsonValue &a, const QJsonValue &b, double rtol, double atol, QStringList &out)
{
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        if (x == y) return;
        const bool whole = std::floor(x) == x && std::floor(y) == y;
        if (!whole && std::fabs(x - y) <= std::max(rtol * std::max(std::fabs(x), std::fabs(y)), atol)) return;
        out << path + QStringLiteral(": ") + shortText(a) + QStringLiteral(" → ") + shortText(b);
        return;
    }
    if (a.type() != b.type()) {
        out << path + QStringLiteral(": ") + shortText(a) + QStringLiteral(" → ") + shortText(b);
        return;
    }
    if (a.isObject()) {
        const QJsonObject x = a.toObject(), y = b.toObject();
        QSet<QString> keys;
        for (auto it = x.begin(); it != x.end(); ++it) keys.insert(it.key());
        for (auto it = y.begin(); it != y.end(); ++it) keys.insert(it.key());
        QStringList sorted(keys.begin(), keys.end());
        sorted.sort();
        for (const QString &k : sorted) {
            if (!x.contains(k)) { out << path + QLatin1Char('.') + k + QStringLiteral(": (none) → ") + shortText(y.value(k)); continue; }
            if (!y.contains(k)) { out << path + QLatin1Char('.') + k + QStringLiteral(": ") + shortText(x.value(k)) + QStringLiteral(" → (none)"); continue; }
            diffInto(path + QLatin1Char('.') + k, x.value(k), y.value(k), rtol, atol, out);
        }
        return;
    }
    if (a.isArray()) {
        const QJsonArray x = a.toArray(), y = b.toArray();
        // Objects with an identity are matched by it; anything else by position.
        bool byId = !x.isEmpty() || !y.isEmpty();
        QHash<QString, int> ix, iy;
        for (int i = 0; byId && i < x.size(); ++i) { const QString id = x[i].isObject() ? idOf(x[i].toObject()) : QString(); if (id.isEmpty() || ix.contains(id)) byId = false; else ix.insert(id, i); }
        for (int i = 0; byId && i < y.size(); ++i) { const QString id = y[i].isObject() ? idOf(y[i].toObject()) : QString(); if (id.isEmpty() || iy.contains(id)) byId = false; else iy.insert(id, i); }
        if (byId) {
            for (int i = 0; i < x.size(); ++i) {
                const QString id = idOf(x[i].toObject());
                if (!iy.contains(id)) { out << path + QLatin1Char('[') + id + QStringLiteral("]: only in the old model"); continue; }
                diffInto(path + QLatin1Char('[') + id + QLatin1Char(']'), x[i], y[iy.value(id)], rtol, atol, out);
            }
            for (int i = 0; i < y.size(); ++i) {
                const QString id = idOf(y[i].toObject());
                if (!ix.contains(id)) out << path + QLatin1Char('[') + id + QStringLiteral("]: only in the new owner");
            }
            return;
        }
        if (x.size() != y.size()) out << path + QStringLiteral(": %1 items → %2").arg(x.size()).arg(y.size());
        for (int i = 0; i < std::min(x.size(), y.size()); ++i)
            diffInto(path + QStringLiteral("[%1]").arg(i), x[i], y[i], rtol, atol, out);
        return;
    }
    if (a != b) out << path + QStringLiteral(": ") + shortText(a) + QStringLiteral(" → ") + shortText(b);
}

std::function<Session *(const QString &)> g_sessionAt;
std::function<Session *(const QString &)> g_loadedAt;
QHash<QString, QString> g_lastLine;   // per session folder: the line last written (written again only on a change)
QHash<QString, int>     g_lines;         // per session folder: lines written (test seam)
QHash<QString, int>     g_comparisons;   // per session folder: comparisons run (test seam)

// THE SESSIONS BEING RECORDED HERE THAT HAVE NOT BEEN COMPARED YET (3.7-0 round 2, review F4): the hook skips a
// comparison while a session is recorded here, so a session that goes away before it is compared (released, its
// athlete or library switched, the app quit) gets ONE line saying so — a missing line must never read as a pass.
// One entry and two connections per such session; nothing is compared on the way out.
struct Uncompared {
    QString                 name;
    bool                    ended = false;   // EVT-09 seen: it ended, but no comparison landed
    QMetaObject::Connection destroyed, endedConn;
};
QHash<QString, Uncompared> g_uncompared;   // by the old model's session folder

void writeLine(const QString &folder, const QString &line, const QString &measured, bool warn)
{
    if (g_lastLine.value(folder) == line) return;
    g_lastLine.insert(folder, line);
    ++g_lines[folder];
    if (warn) ppWarn() << "[DiagParallel]" << qPrintable(line + measured);
    else      ppInfo() << "[DiagParallel]" << qPrintable(line + measured);
}

void forgetUncompared(const QString &folder)
{
    const auto it = g_uncompared.find(folder);
    if (it == g_uncompared.end()) return;
    QObject::disconnect(it->destroyed);
    QObject::disconnect(it->endedConn);
    g_uncompared.erase(it);
}

// Said once, when the session goes uncompared; no comparison here (the objects may be going).
void sayNotCompared(const QString &folder)
{
    const auto it = g_uncompared.find(folder);
    if (it == g_uncompared.end()) return;
    const QString line = it->ended
        ? QStringLiteral("%1 not compared: the session ended, but no comparison landed before it went").arg(it->name)
        : QStringLiteral("%1 not compared: the session was not ended").arg(it->name);
    forgetUncompared(folder);
    writeLine(folder, line, QString(), true);
}

void noteUncompared(const QString &folder, Session *s)
{
    if (g_uncompared.contains(folder)) return;
    Uncompared u;
    u.name      = QFileInfo(folder).fileName();
    u.destroyed = QObject::connect(s, &QObject::destroyed, [folder] { sayNotCompared(folder); });
    u.endedConn = QObject::connect(s, &Session::sessionEnded, s, [folder] {
        if (const auto it = g_uncompared.find(folder); it != g_uncompared.end()) it->ended = true;
    });
    g_uncompared.insert(folder, u);
}

QSet<int> shotIdsOf(const SessionDiagnosticsModel &m)
{
    QSet<int> ids;
    for (const ShotRecord &s : m.shotRecords()) ids.insert(s.shotId);
    return ids;
}
QSet<int> shotIdsOf(const SessionDiagnostics &d)
{
    const QList<int> kept = d.keptShots();
    return QSet<int>(kept.begin(), kept.end());
}

void compareAndLog(const SessionDiagnosticsModel &m, const SessionDiagnostics &d)
{
    const QString name = QFileInfo(m.sessionDir()).fileName();
    const QStringList diffs = differences(formOf(m, false), formOf(d, false));
    QString line = QStringLiteral("%1 shots %2: ").arg(name).arg(m.shotRecords().size());
    if (diffs.isEmpty()) {
        line += QStringLiteral("0 differences");
    } else {
        line += QStringLiteral("%1 difference(s): ").arg(diffs.size()) + diffs.mid(0, 6).join(QStringLiteral("; "));
        if (diffs.size() > 6) line += QStringLiteral("; …");
    }
    const QString measured = QStringLiteral(" (new owner: reduce %1 ms, %2 asks, %3 reductions)")
                                 .arg(d.lastReductionMs()).arg(d.asks().size()).arg(d.counts().reductions);
    ++g_comparisons[m.sessionDir()];
    forgetUncompared(m.sessionDir());
    writeLine(m.sessionDir(), line, measured, !diffs.isEmpty());
}

// The seams' lookup: the folder as the old model holds it, or the same folder spelled otherwise.
template <typename T>
T byFolder(const QHash<QString, T> &h, const QString &folder)
{
    if (const auto it = h.find(folder); it != h.end()) return *it;
    const QString clean = QDir::cleanPath(folder);
    for (auto it = h.begin(); it != h.end(); ++it)
        if (QDir::cleanPath(it.key()) == clean) return *it;
    return T{};
}

} // namespace

QStringList differences(const QJsonValue &oldForm, const QJsonValue &newForm, double rtol, double atol)
{
    QStringList out;
    diffInto(QStringLiteral("$"), oldForm, newForm, rtol, atol, out);
    return out;
}

DiagnosticsDoors doors(WorkPool *pool, WorkReceiver *receiver, RuleData *ruleData,
                       const SessionRepository &sessions, RecordingRepository &recordings)
{
    DiagnosticsDoors d;
    d.pool     = pool;
    d.receiver = receiver;
    d.ruleData = ruleData;
    // IN MEMORY ONLY (§4.9): no diagnostics file (persist false) and no sidecar either — ParseNoWrite: the reader
    // parses a miss or a stale sidecar and writes nothing (3.7-0; before it, false meant "never parse": D22).
    d.reader   = std::make_shared<SwingStoreFiringReader>(sessions, recordings, /*writeSidecars=*/false);
    d.persist  = false;
    d.attach   = &SessionDiagnostics::attach;
    return d;
}

bool enabledByEnvironment()
{
    return qEnvironmentVariable("PINPOINT_DIAG_PARALLEL") == QLatin1String("1");
}

void install(std::function<Session *(const QString &folder)> sessionAt,
             std::function<Session *(const QString &folder)> loadedAt)
{
    g_sessionAt = std::move(sessionAt);
    g_loadedAt  = std::move(loadedAt);
    ppInfo() << "[DiagParallel] on (PINPOINT_DIAG_PARALLEL=1): every old diagnostics model still made (the catch-up's, the"
             << "report's) is compared with its session's owner; one line per session below";
    SessionDiagnosticsModel::setParallelHook([](const SessionDiagnosticsModel &model) {
        if (!g_sessionAt || !g_loadedAt) return;
        const bool sweep = model.baselineAuthorityName() == QLatin1String("always");   // the catch-up's model
        Session *s = sweep ? g_loadedAt(model.sessionDir()) : g_sessionAt(model.sessionDir());
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        if (!d && sweep) return;   // an ended session no one holds: not loaded for a comparison
        if (!d) {
            const QString line = QStringLiteral("%1: no new owner (a session no golfer owns has none: N1)")
                                     .arg(QFileInfo(model.sessionDir()).fileName());
            writeLine(model.sessionDir(), line, QString(), false);
            return;
        }
        // O(1) WHILE BEING RECORDED HERE (3.7-0; D22-STALL-NOTES §4 A1): no comparison while this process records the
        // session (started or resumed here, not ended) — it is compared at its End. Both facts are in memory (the
        // Session's own flag and the repository's record); no document is touched. A session another process left in
        // progress (a crash, another machine on a shared library) is NOT being recorded here, and is compared as on
        // HEAD (review F1). A session that goes away before it is compared says so in one line (F4).
        if (s->runningHere() && s->inProgress()) {
            noteUncompared(model.sessionDir(), s);
            return;
        }
        // LIKE WITH LIKE (3.7-0): the club baseline the old model froze (at End, after the new owner loaded) is read
        // by the new owner too — only when the file is there (review F3: an absent file never wipes the baseline the
        // owner computed in memory at first need). A read only (the run stays in memory); when it moved the numbers,
        // every shot fires again and the comparison below waits for that diagnosis (the retry path).
        d->reloadClubBaseline(/*keepWhenAbsent=*/true);
        if (d->idle() && d->diagnosis() && shotIdsOf(model) == shotIdsOf(*d)) {
            compareAndLog(model, *d);
            return;
        }
        // Not there yet: compared when the new owner's diagnosis of the same shots lands (at most 20 tries, then left
        // to the old model's next settle).
        QPointer<SessionDiagnosticsModel> pm(const_cast<SessionDiagnosticsModel *>(&model));
        QPointer<SessionDiagnostics>      pd(d);
        auto conn  = std::make_shared<QMetaObject::Connection>();
        auto tries = std::make_shared<int>(0);
        *conn = QObject::connect(d, &SessionDiagnostics::diagnosisApplied, d, [pm, pd, conn, tries] {
            if (!pm || !pd || ++*tries > 20) { QObject::disconnect(*conn); return; }
            if (!pd->idle() || shotIdsOf(*pm) != shotIdsOf(*pd)) return;
            QObject::disconnect(*conn);
            compareAndLog(*pm, *pd);
        });
    });
}

void uninstall()
{
    SessionDiagnosticsModel::setParallelHook({});
    g_sessionAt = {};
    g_loadedAt  = {};
    // The app quitting (or a test ending) with a session recorded here never compared: one line each (F4).
    for (const QString &folder : g_uncompared.keys()) sayNotCompared(folder);
}

int     comparisonsOf(const QString &sessionFolder) { return byFolder(g_comparisons, sessionFolder); }
int     linesOf(const QString &sessionFolder) { return byFolder(g_lines, sessionFolder); }
QString lastLineOf(const QString &sessionFolder) { return byFolder(g_lastLine, sessionFolder); }

} // namespace pinpoint::parallel

namespace pinpoint {

// THE APP'S DOORS (refactor step 3.7-a; the header): the owner writes diagnostics.json and the shots' sidecars, never
// work_ons.json (WorkOnsController's until 3.7-e).
DiagnosticsDoors appDiagnosticsDoors(WorkPool *pool, WorkReceiver *receiver, RuleData *ruleData,
                                     const SessionRepository &sessions, RecordingRepository &recordings)
{
    DiagnosticsDoors d;
    d.pool         = pool;
    d.receiver     = receiver;
    d.ruleData     = ruleData;
    d.reader       = std::make_shared<SwingStoreFiringReader>(sessions, recordings);   // parses a miss, writes the sidecar
    d.persist      = true;
    d.writeSummary = false;
    d.attach       = &SessionDiagnostics::attach;
    return d;
}

} // namespace pinpoint
