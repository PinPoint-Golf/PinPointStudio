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

// THE GATES VIEW'S LIBRARY COUNT AND ITS DOOR TO THE SESSION INDEXES (platform refactor P-3b; information_model.md
// FILE-SESS-05 `gates`).
//
// HOW OFTEN EACH GATE FIRED ACROSS THE LIBRARY (trust_and_gates_design.md §7.5): a swing counts ONCE per gate however many
// records that gate left on it; a swing analysed before reasons were recorded (no `gatesVersion`) is counted apart, never
// as "no gate fired". THE COUNT IS A SUM OVER EVERY ATHLETE'S SESSION INDEX: each session's summary carries its own counts
// (its `gates` block, written by its SessionDiagnostics), read as SessionRepository::deferredSummary reads it. A session whose
// summary has no block yet (written before the block existed, or not written) is NOT SUMMARISED YET: only its swings are
// read one by one, by folder, as the whole library was before (corridor_grid_read.h readPhaseGrid), and the count says how
// many sessions were read that way.
//
// WHY A DOOR: ModelBrowser is a QML type of the module, which the QML suites compile without the domain model. The module
// holds only this header; the composition root (main.cpp) installs the app's source. With no source installed (a QML
// suite, a tool), or for a library that is not the athletes' (the source answers nullopt), there is no index: the count is
// the library walk, every swing read by folder (gate_count_reads.h).
//
// THREADS: the source runs on the GUI thread and reads no session-level file there: it takes the athletes' owners and hands
// back the read. The read — every athlete's SessionIndex, each entry's summary filled through deferredSummary
// (AthleteRepository::readIndexWithSummaries, worker-safe beside readActivity) — runs on the model browser's worker, as the
// library walk did.

#include "../../DomainModel/session_roll_up.h"   // SessionIndexSummary (header-only values)

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

#include <functional>
#include <optional>
#include <utility>

// The Gates view's count, held in memory per library.
struct ModelGateCounts {
    QString                            root;          // the library this describes
    bool                               valid = false; // a scan has completed for `root`
    bool                               capped = false;
    int                                swings = 0;     // swings counted (a summary's, or a folder read's)
    int                                notRecorded = 0;// of those, analysed before records existed
    QMap<int, int>                     byVersion;      // gatesVersion → swings stamped with it
    QHash<QString, int>                fired;          // gate id → swings it fired on
    QHash<QString, QMap<QString, int>> sessions;       // gate id → session folder name → swings
    // Where the counts came from (P-3b). `indexed` false: no index (no source, or not the athletes' library), the
    // library walk; then the two below are 0.
    bool                               indexed = false;
    int                                summarised = 0;        // sessions counted from their summary's `gates` block
    int                                readSwingBySwing = 0;  // sessions not summarised yet, their swings read by folder
    // Swings stamped at `since` or later — the ones a gate introduced at `since` could have fired on.
    int recordedSince(int since) const
    {
        int n = 0;
        for (auto it = byVersion.constBegin(); it != byVersion.constEnd(); ++it)
            if (it.key() >= since) n += it.value();
        return n;
    }
};

namespace pinpoint::diagmodel {

// One session of an athlete's index, as the count reads it.
struct GateCountSession {
    QString             session;   // its folder's name (the key ModelGateCounts::sessions uses)
    QString             folder;    // its folder, absolute: the swing-by-swing read, when not summarised
    SessionIndexSummary summary;   // its summary as read (`read` false: none could be read)
};

// The read of every session of the library's athletes, with each summary. Any thread (the model browser's worker).
using GateCountSessionsRead = std::function<QList<GateCountSession>()>;
// (libraryRoot) → that read; nullopt when the library is not the athletes' (no index to sum). GUI thread; reads no file.
using GateCountSource = std::function<std::optional<GateCountSessionsRead>(const QString &libraryRoot)>;

inline GateCountSource &gateCountSourceSlot()
{
    static GateCountSource source;
    return source;
}
// The composition root's half: main.cpp installs the app's source, once, before the QML loads.
inline void installGateCountSource(GateCountSource s) { gateCountSourceSlot() = std::move(s); }
inline const GateCountSource &gateCountSource() { return gateCountSourceSlot(); }

} // namespace pinpoint::diagmodel
