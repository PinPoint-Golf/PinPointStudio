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

// THE SESSION SUMMARY'S GATE COUNTS (platform refactor P-3b; information_model.md FILE-SESS-05 `gates`). What the model
// browser's Gates view counts of this session, so it reads the session's summary (the athlete level's SessionIndex
// summaries) instead of every swing's phase grid:
//   swings       the swings whose phase grid read (a swing whose analysis does not read is not a swing here, as the
//                library walk had it — but a document that reads to an empty root is read, so it counts, under
//                notRecorded, where the walk dropped it: an EDGE, logged for the Solution Architect);
//   notRecorded  of those, the swings analysed before gate records existed (gatesVersion 0);
//   byVersion    the swings per gatesVersion they were analysed under (the ones with records);
//   fired        per gate id, the swings (their numbers, ascending) whose records name it, each swing once per gate.
// Ids and numbers only (R-DER-9). Absent in a summary written before the block existed: `has` false, and a reader says
// the session is not summarised yet (the Gates view reads that session's swings one by one instead).
//
// THE RULES THAT MAKE IT are here too, pure: shotGateRecordOf (one swing's grid's `gatesVersion` and gate records → its
// record) and sessionGatesOf (the fold). Qt Core only: the summary's readers (session_roll_up.h) include this, so it names
// the gate record (gate_record.h), never the phase grid's header. The session's own SessionDiagnostics makes the block, off the grid each shot's fire read (or the reduction's lane
// read for a result loaded from the file), and its summary's one write carries it.
//
// Rules ring (src/Diagnostics/derived): values and JSON only.

#include "../../Vocabulary/Analysis/gate_record.h"   // GateRecord (Qt Core only)

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace pinpoint::analysis {

// One swing's gate record, as its phase grid carries it.
struct ShotGateRecord {
    int         shotId       = 0;   // the swing number
    int         gatesVersion = 0;   // 0: analysed before records existed
    QStringList gates;              // the gate ids its records name, each once, sorted
};

struct SessionGates {
    bool                      has         = false;   // false: a summary written before the block existed
    int                       swings      = 0;
    int                       notRecorded = 0;
    QMap<int, int>            byVersion;             // gatesVersion → swings (only versions above 0)
    QMap<QString, QList<int>> fired;                 // gate id → swing numbers, ascending
};

// `gatesVersion` and `gates` as the swing's phase grid carries them (SwingPhaseGrid::gatesVersion, ::gates).
inline ShotGateRecord shotGateRecordOf(int gatesVersion, const std::vector<GateRecord> &gates, int shotId)
{
    ShotGateRecord r;
    r.shotId       = shotId;
    r.gatesVersion = gatesVersion;
    if (gatesVersion > 0)
        for (const GateRecord &g : gates)
            if (!g.gate.isEmpty() && !r.gates.contains(g.gate)) r.gates.append(g.gate);
    r.gates.sort();
    return r;
}

// THE FOLD, over the swings whose grid read (strike order or any: the result is ordered).
inline SessionGates sessionGatesOf(const std::vector<ShotGateRecord> &shots)
{
    SessionGates s;
    s.has = true;
    for (const ShotGateRecord &r : shots) {
        ++s.swings;
        if (r.gatesVersion <= 0) { ++s.notRecorded; continue; }
        s.byVersion[r.gatesVersion] += 1;
        for (const QString &g : r.gates) {
            QList<int> &on = s.fired[g];
            if (!on.contains(r.shotId)) on.append(r.shotId);
        }
    }
    for (QList<int> &on : s.fired) std::sort(on.begin(), on.end());
    return s;
}

// ── JSON (FILE-SESS-05's `gates`) ─────────────────────────────────────────────────────────────────────────────────

inline QJsonObject sessionGatesToJson(const SessionGates &g)
{
    QJsonObject byVersion, fired;
    for (auto it = g.byVersion.constBegin(); it != g.byVersion.constEnd(); ++it)
        byVersion.insert(QString::number(it.key()), it.value());
    for (auto it = g.fired.constBegin(); it != g.fired.constEnd(); ++it) {
        QJsonArray on;
        for (const int n : it.value()) on.append(n);
        fired.insert(it.key(), on);
    }
    return QJsonObject{ { QStringLiteral("swings"), g.swings }, { QStringLiteral("notRecorded"), g.notRecorded },
                        { QStringLiteral("byVersion"), byVersion }, { QStringLiteral("fired"), fired } };
}

// `v` is the `gates` value; an absent one (a summary written before the block) reads as `has` false.
inline SessionGates sessionGatesFromJson(const QJsonValue &v)
{
    SessionGates g;
    if (!v.isObject()) return g;
    const QJsonObject o = v.toObject();
    g.has         = true;
    g.swings      = o.value(QStringLiteral("swings")).toInt();
    g.notRecorded = o.value(QStringLiteral("notRecorded")).toInt();
    const QJsonObject byVersion = o.value(QStringLiteral("byVersion")).toObject();
    for (auto it = byVersion.constBegin(); it != byVersion.constEnd(); ++it) {
        bool ok = false;
        const int version = it.key().toInt(&ok);
        if (ok && version > 0 && it.value().toInt() > 0) g.byVersion.insert(version, it.value().toInt());
    }
    const QJsonObject fired = o.value(QStringLiteral("fired")).toObject();
    for (auto it = fired.constBegin(); it != fired.constEnd(); ++it) {
        QList<int> on;
        for (const QJsonValue &n : it.value().toArray())
            if (n.toInt() > 0 && !on.contains(n.toInt())) on.append(n.toInt());
        std::sort(on.begin(), on.end());
        if (!on.isEmpty()) g.fired.insert(it.key(), on);
    }
    return g;
}

} // namespace pinpoint::analysis
