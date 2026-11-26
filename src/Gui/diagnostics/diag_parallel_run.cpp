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

#include "../../DomainModel/athlete.h"   // DiagnosticsDoors
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../Export/firing_reader_store.h"
#include "../../Diagnostics/derived/session_results.h"
#include "../../Diagnostics/derived/strike_from_flight.h"
#include "../../Vocabulary/Club/club_vocabulary.h"
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

QJsonObject workOnsJson(const SessionPriorities &w)
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

} // namespace

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
    for (const ShotResult &s : r.shots) {
        const KeptShot *k = d.kept(s.shotId);
        if (!k) continue;
        const ShotRecord rec = d.checkedRows(s.shotId);   // the shot's flight check as its owner holds it (refactor 6.3a)
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

} // namespace pinpoint::parallel
