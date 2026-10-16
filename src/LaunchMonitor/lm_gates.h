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

// THE LAUNCH MONITOR'S DROPPED FIELDS AS GATE RECORDS (trust stage 6, docs/design/trust_stage6_plan.md).
//
// The decision is made at import (gcquad_csv_parser.cpp: a no-data marker, a value outside the
// field's physical range, a face-to-path printed with neither of its inputs measured), long after —
// or without — any analysis. So the import keeps the list (LaunchMonitorReading::dropped), the swing
// stores it in its `launchMonitor` block as `dropped[]`, and the records are DERIVED FROM THAT STORED
// LIST here, every time analysis.gates is written (decision rule 1: a reused product's decision is
// recorded from what it stores, on every path). analysis.gates is rebuilt by each analysis, so a
// record written once at import would be lost on the first re-analysis; derived, it cannot be.
//
// A swing whose block has no `dropped` key was imported before the list was kept: it gets no
// records, and stays unexplained (Mark, 16 Oct 2026). GSPro readings drop nothing.
//
// Header-only: swing_doc.cpp and the tests include it with no source-list edits.

#include "launch_monitor_reading.h"
#include "../Analysis/gate_record.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace pinpoint::lm {

// The drop list as the `launchMonitor` block stores it: [{ key, code, raw?, value?, lo?, hi?, unit? }].
inline QJsonArray droppedFieldsToJson(const std::vector<DroppedField> &dropped)
{
    QJsonArray out;
    for (const DroppedField &d : dropped) {
        QJsonObject o{ { QStringLiteral("key"),  d.key },
                       { QStringLiteral("code"), d.code } };
        if (d.raw)   o.insert(QStringLiteral("raw"),   *d.raw);
        if (d.value) o.insert(QStringLiteral("value"), *d.value);
        if (d.lo)    o.insert(QStringLiteral("lo"),    *d.lo);
        if (d.hi)    o.insert(QStringLiteral("hi"),    *d.hi);
        if (!d.unit.isEmpty()) o.insert(QStringLiteral("unit"), d.unit);
        out.append(o);
    }
    return out;
}

// The readings we work out from the device's own numbers (gcquad_csv_parser.cpp "Values the device
// states implicitly"): a dropped ingredient takes its derived field down with it. A CASCADE IS ONE
// RECORD (decision rule 2): the derived key is listed in the first dropped ingredient's `covers`,
// and only when the block really lacks it.
struct LmDerivation { const char *derived; const char *a; const char *b; };
inline const std::vector<LmDerivation> &lmDerivations()
{
    static const std::vector<LmDerivation> d = {
        { "lm.smashFactor", "lm.ballSpeed",   "lm.clubheadSpeed" },
        { "lm.spinAxis",    "lm.sideSpin",    "lm.backSpin"      },
        { "lm.spinLoft",    "lm.dynamicLoft", "lm.attackAngle"   },
    };
    return d;
}

// The records the stored drop list stands for. Subject `lm.<field>` (the metric key the lm row would
// carry), effect refused. Empty when the block has no `dropped` list.
inline std::vector<analysis::GateRecord> lmGates(const QJsonObject &launchMonitorBlock)
{
    using analysis::GateEffect;
    std::vector<analysis::GateRecord> out;
    const QJsonArray dropped = launchMonitorBlock.value(QStringLiteral("dropped")).toArray();
    QSet<QString> covered;
    for (const QJsonValue &v : dropped) {
        const QJsonObject d = v.toObject();
        const QString key  = d.value(QStringLiteral("key")).toString();
        const QString code = d.value(QStringLiteral("code")).toString();
        if (!key.startsWith(QLatin1String("lm.")))
            continue;

        QString gate;
        QVariantMap ev;
        if (code == QLatin1String("noData")) {
            gate = PP_GATE("lm.noData");
            if (d.contains(QStringLiteral("raw"))) ev.insert(QStringLiteral("raw"), d.value(QStringLiteral("raw")).toDouble());
        } else if (code == QLatin1String("outOfRange")) {
            gate = PP_GATE("lm.outOfRange");
            if (d.contains(QStringLiteral("value"))) ev.insert(QStringLiteral("value"), d.value(QStringLiteral("value")).toDouble());
            if (d.contains(QStringLiteral("lo")))    ev.insert(QStringLiteral("lo"),    d.value(QStringLiteral("lo")).toDouble());
            if (d.contains(QStringLiteral("hi")))    ev.insert(QStringLiteral("hi"),    d.value(QStringLiteral("hi")).toDouble());
            if (d.contains(QStringLiteral("raw")))   ev.insert(QStringLiteral("raw"),   d.value(QStringLiteral("raw")).toDouble());
        } else if (code == QLatin1String("noInputs")) {
            gate = PP_GATE("lm.noInputs");
            if (d.contains(QStringLiteral("value"))) ev.insert(QStringLiteral("value"), d.value(QStringLiteral("value")).toDouble());
        } else {
            continue;                                   // a code this build does not know: no guess
        }
        const QString unit = d.value(QStringLiteral("unit")).toString();
        if (!unit.isEmpty()) ev.insert(QStringLiteral("unit"), unit);

        QStringList covers;
        for (const LmDerivation &dv : lmDerivations()) {
            const QString derived = QString::fromLatin1(dv.derived);
            if (key != QLatin1String(dv.a) && key != QLatin1String(dv.b)) continue;
            if (covered.contains(derived)) continue;
            if (launchMonitorBlock.contains(derived.mid(3))) continue;   // the block's raw names drop "lm."
            covers << derived;
            covered.insert(derived);
        }

        analysis::GateRecord r;
        r.gate = gate;
        r.subject = key;
        r.effect = GateEffect::Refused;
        r.evidence = ev;
        r.covers = covers;
        out.push_back(std::move(r));
    }
    return out;
}

// Is this serialised record one lmGates() makes? (Re-applying a reading replaces them.)
inline bool isLmGateRecord(const QJsonObject &rec)
{
    return rec.value(QStringLiteral("gate")).toString().startsWith(QLatin1String("lm."));
}

// analysis.gates with the launch monitor's records rebuilt from `launchMonitorBlock`: any prior lm.*
// records removed, the stored list's appended. Never accumulates. Leaves an analysis with no
// `gatesVersion` alone — it was analysed before records existed, and an array there would read as
// "no gate fired" for every camera reading on it.
inline void applyLmGates(QJsonObject &analysisObj, const QJsonObject &launchMonitorBlock)
{
    if (!analysisObj.contains(QStringLiteral("gatesVersion")))
        return;
    QJsonArray gates;
    for (const QJsonValue &v : analysisObj.value(QStringLiteral("gates")).toArray())
        if (!isLmGateRecord(v.toObject()))
            gates.append(v);
    for (const QJsonValue &v : analysis::gateRecordsToJson(lmGates(launchMonitorBlock)))
        gates.append(v);
    analysisObj.insert(QStringLiteral("gates"), gates);
}

} // namespace pinpoint::lm
