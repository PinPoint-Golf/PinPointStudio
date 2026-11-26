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

#include "phase_grid_store.h"

#include "swing_store.h"   // no FFmpeg: the store is its own library
#include "../Core/atomic_write.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace pinpoint::PhaseGridStore {

using analysis::PhaseGridConfig;
using analysis::PhaseGridMsWindow;
using analysis::SwingPhaseGrid;

QString sidecarPath(const QString &swingDir)
{
    return QDir(swingDir).filePath(QStringLiteral("swing_phasegrid.json"));
}

bool write(const QString &swingDir, const SwingPhaseGrid &grid, qint64 sourceSize, qint64 sourceMtimeMs)
{
    return pinpoint::atomicWrite(sidecarPath(swingDir),
                                 QJsonDocument(analysis::savePhaseGrid(grid, sourceSize, sourceMtimeMs)).toJson(QJsonDocument::Compact),
                                 pinpoint::AtomicWriteOptions::intoExistingFolder())
        .ok;
}

SwingPhaseGrid read(const QString &swingDir, SidecarPolicy policy, const PhaseGridConfig &cfg,
                    SwingStore::LazyDocument *parsed)
{
    SwingPhaseGrid grid;

    // The guard keys on whichever document the swing holds (swing.ppsw, or a JSON-era swing.json):
    // its size and mtime move on every rewrite, including the one that converts a swing.
    const SwingStore::DocInfo docInfo = SwingStore::info(swingDir);
    if (!docInfo.exists())
        return grid;

    const qint64 size    = docInfo.size;
    const qint64 mtimeMs = docInfo.mtimeMs;
    PhaseGridConfig buildCfg = cfg;   // widened below to the union of millisecond spans

    // The cheap path.
    const QString sidePath = sidecarPath(swingDir);
    if (QFile::exists(sidePath)) {
        QFile sf(sidePath);
        if (sf.open(QIODevice::ReadOnly)) {
            bool ok = false;
            std::vector<PhaseGridMsWindow> held;
            SwingPhaseGrid cached = analysis::loadPhaseGrid(QJsonDocument::fromJson(sf.readAll()).object(),
                                                            size, mtimeMs, &ok, cfg, &held);
            if (ok) {
                cached.swingDir = swingDir;
                return cached;
            }
            // Refused for want of a millisecond span (or stale): rebuild with the UNION of what it held
            // and what this reader asks for, so the other pack's reader is not refused next time. A
            // caller that may not fat-parse gets the current grid it holds (only the span is missing).
            if (!mayParse(policy) && !cached.isEmpty()) {
                cached.swingDir = swingDir;
                return cached;
            }
            buildCfg = analysis::withMsWindows(cfg, held);
        }
    }

    // The caller asked never to fat-parse (CachedOnly). Report nothing rather than stall — an unindexed
    // swing renders as "not yet read", which is honest and recoverable, where a one-second freeze per
    // swing is neither.
    if (!mayParse(policy))
        return grid;

    // The shared parse when the caller has one (the summary reader may already have paid for it).
    SwingStore::LazyDocument own(swingDir);
    SwingStore::LazyDocument &doc = parsed ? *parsed : own;
    const QJsonObject &root = doc.root();
    if (root.isEmpty())
        return grid;

    // The grid and the swing's identity fields, off the document (the sampler's pure half).
    grid = analysis::phaseGridOfDocument(root, buildCfg);
    grid.swingDir  = swingDir;
    grid.sessionId = QFileInfo(QFileInfo(swingDir).absolutePath()).fileName();

    // The guard written is the size/mtime read at the top of this function. A shared parse can be
    // older than that (another reader paid for it before a rewrite), and a rewrite can land between
    // any parse and this write: either way the grid is returned but never indexed under that guard.
    if (!doc.stillCurrent() || doc.parsedInfo().size != size || doc.parsedInfo().mtimeMs != mtimeMs)
        return grid;

    // ParseNoWrite (refactor 3.7-0): the same grid, never indexed — the caller leaves the session's files to
    // their one writer (the parallel run's firing reader, §4.9 "in memory only").
    if (!mayWriteSidecar(policy))
        return grid;

    // Best-effort: a library on read-only media still browses, it just re-parses each time. Into the
    // swing folder, which must already be there (a trashed folder is not re-made).
    (void)write(swingDir, grid, size, mtimeMs);
    return grid;
}

SwingPhaseGrid readForMeasures(const QString &swingDir, const std::vector<analysis::Measure> &measures,
                               SidecarPolicy policy, const PhaseGridConfig &cfg, SwingStore::LazyDocument *parsed)
{
    SwingPhaseGrid grid = read(swingDir, policy, analysis::withMsWindows(cfg, analysis::msWindowsOf(measures)), parsed);
    if (grid.swingDir.isEmpty())
        grid.swingDir = swingDir;
    return grid;
}

analysis::WristInput readWristInput(const QString &swingDir, const SwingPhaseGrid &grid, SwingStore::LazyDocument *parsed)
{
    bool any = false;
    for (const analysis::MetricPhaseGrid &m : grid.metrics)
        if (analysis::dofForMetricKey(m.key)) { any = true; break; }
    if (!any) return {};
    if (parsed && parsed->loaded())
        return analysis::wristInputOf(parsed->root().value(QStringLiteral("analysis")).toObject());
    return analysis::wristInputOf(SwingStore::loadAnalysis(swingDir).value(QStringLiteral("analysis")).toObject());
}

} // namespace pinpoint::PhaseGridStore
