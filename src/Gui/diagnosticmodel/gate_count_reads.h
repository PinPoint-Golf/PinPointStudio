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

// THE GATES VIEW'S COUNT, MADE (platform refactor P-3b; gate_counts.h says what it is). Off the GUI thread, on the model
// browser's worker:
//   gateCountsOfIndex   the sum over every athlete's session index: each summarised session's `gates` block added as it
//                       stands; a session not summarised yet read swing by swing (and counted as read that way);
//   gateCountsByWalk    no index (no source installed, or a library that is not the athletes'): every swing of the library
//                       read by folder, the whole count as before P-3b.
// The swing-by-swing read is the sampler's folder forwarder (measure_sample.h readPhaseGrid), `writeSidecar` true: a stale
// sidecar (a re-analysed swing) is rebuilt, so a folder read counts the records the swing holds now.
//
// Header-only: model_browser.cpp and session_gates_test include it, each linking the sampler, the grid's door and SwingPaths.

#include "gate_counts.h"
#include "../../Diagnostics/measures/measure_sample.h"   // readPhaseGrid: the folder forwarder (FILE-SHOT-07's recorded exception)
#include "../../Export/swing_paths.h"                    // the one swing listing

#include <QSet>

namespace pinpoint::diagmodel {

inline constexpr int kGateCountMaxFolderSwings = 20000;   // a cap on the swings counted by folder, reported when it bites

// One swing read by folder, added. False: its analysis does not read (not a swing).
inline bool addSwingByFolder(ModelGateCounts &c, const QString &session, const QString &swingDir)
{
    const analysis::SwingPhaseGrid grid = analysis::readPhaseGrid(swingDir, /*writeSidecar*/ true);
    if (grid.swingDir.isEmpty()) return false;   // no readable analysis: not a swing
    ++c.swings;
    if (grid.gatesVersion <= 0) { ++c.notRecorded; return true; }
    c.byVersion[grid.gatesVersion] += 1;
    QSet<QString> once;                          // a swing counts once per gate
    for (const analysis::GateRecord &r : grid.gates) once.insert(r.gate);
    for (const QString &g : once) {
        c.fired[g] += 1;
        c.sessions[g][session] += 1;
    }
    return true;
}

// One session's summary block, added as it stands.
inline void addSessionGates(ModelGateCounts &c, const QString &session, const analysis::SessionGates &g)
{
    c.swings      += g.swings;
    c.notRecorded += g.notRecorded;
    for (auto it = g.byVersion.constBegin(); it != g.byVersion.constEnd(); ++it) c.byVersion[it.key()] += it.value();
    for (auto it = g.fired.constBegin(); it != g.fired.constEnd(); ++it) {
        const int n = int(it.value().size());
        if (n <= 0) continue;
        c.fired[it.key()] += n;
        c.sessions[it.key()][session] += n;
    }
}

// THE SUM OVER THE SESSION INDEXES, their summaries already read (GateCountSessionsRead). Any thread.
inline ModelGateCounts gateCountsOfIndex(const QString &root, const QList<GateCountSession> &sessions,
                                         int maxFolderSwings = kGateCountMaxFolderSwings)
{
    ModelGateCounts c;
    c.root    = root;
    c.indexed = true;
    int folderSwings = 0;
    for (const GateCountSession &s : sessions) {
        const SessionIndexSummary &sum = s.summary;
        if (sum.gates.has) {
            addSessionGates(c, s.session, sum.gates);
            ++c.summarised;
            continue;
        }
        // NOT SUMMARISED YET: today's per-swing read, for this session only.
        ++c.readSwingBySwing;
        for (const QString &swing : SwingPaths::swingFolders(s.folder)) {
            if (folderSwings >= maxFolderSwings) { c.capped = true; break; }
            if (addSwingByFolder(c, s.session, s.folder + QLatin1Char('/') + swing)) ++folderSwings;
        }
    }
    c.valid = true;
    return c;
}

// NO INDEX: the library walk, every swing by folder.
inline ModelGateCounts gateCountsByWalk(const QString &root, int maxFolderSwings = kGateCountMaxFolderSwings)
{
    ModelGateCounts c;
    c.root = root;
    // The library's listing is SwingPaths' (refactor step 3.9; THE one swing listing, 2.7b).
    SwingPaths::visitLibrarySwings(root, [&c, maxFolderSwings](const QString &session, const QString &swingDir) {
        if (c.swings >= maxFolderSwings) {
            c.capped = true;
            return false;
        }
        addSwingByFolder(c, session, swingDir);
        return true;
    });
    c.valid = true;
    return c;
}

} // namespace pinpoint::diagmodel
