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

// THE CONSISTENCY TABLE, STORED (docs/design/consistency_design.md §3 Home, §9 step 7). Home's column reads
// a word per area across a golfer's sessions, and Home can't load swing documents: never on the GUI thread,
// and never many of them for one column. So each session keeps its table in <session>/consistency.json,
// written by the work-ons catch-up (work_ons_controller.h) under a fingerprint of the rule version, the
// content stamp and every swing document's name, size and mtime. A new shot, a re-analysis or a new rule
// moves the fingerprint, and the catch-up derives the table again by its one route.
//
// The file holds what Home needs and no more:
//   {schema, derivedFrom: {fingerprint, ruleVersion}, shots, sessionDate (ms since the epoch),
//    afterImpactMs, cells: [{area, column, word (consistencyWordToken)}]}
//
// deriveConsistencySummary is the same two layers the Consistency tab runs, over the same fields of the same
// documents (SessionSeriesModel::extractShot), so a stored table and the tab cannot disagree. It takes no
// ledger: neither layer reads a shot's diagnostics.

#include "../../Coaching/consistency_home.h"    // ConsistencyCell, kConsistencyRuleVersion

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <vector>

namespace pinpoint::charts {

struct ConsistencyEnv;

inline constexpr int kConsistencySummarySchema = 1;

struct ConsistencySummary {
    QString fingerprint;                // derivedFrom.fingerprint
    int     ruleVersion = 0;            // derivedFrom.ruleVersion
    int     shots = 0;                  // the shots the table was read from
    qint64  sessionDateMs = 0;          // when it was played: the first shot's clock, else the folder's date
    double  afterImpactMs = 0.0;        // P7-P8's length
    std::vector<ConsistencyCell> cells; // area, column and word only: the counts stay 0
    int     unread = 0;                 // swings whose document couldn't be read. Not stored: a table with any
                                        // never reaches the file, so it is 0 on everything read back
};

QString consistencySummaryPath(const QString &sessionDir);

// When a session with no table was played, for ordering the catch-up without opening a swing: the date
// its folder is named with, else the folder's mtime.
qint64 consistencyFolderDateMs(const QString &sessionDir);

QJsonObject consistencySummaryToJson(const ConsistencySummary &summary);

// False when the file is another schema, holds no cells, or names a cell the table does not have: a table
// that can't be read whole doesn't count, and the catch-up derives it again.
bool consistencySummaryFromJson(const QJsonObject &root, ConsistencySummary *out);

// One session's table, fingerprint and ruleVersion left for the caller. Any thread, with an env taken on
// the GUI thread (consistency_env.h). Reads each swing with SwingStore::loadAnalysis and lets it go before
// the next. A swing that can't be read is left out and counted in `unread`, whether that is a corrupt
// document or a share that dropped for a moment, which look the same from here. `documents` is how many
// swings the scan found a document for: fewer found now counts the shortfall as unread too, because a share
// dropping fails the stat before the read. So a table with `unread` set is true of what was read and must
// not be stored under a fingerprint that names every swing. False when no shot could be read, or when
// `cancel` went up (checked before each swing and each layer).
bool deriveConsistencySummary(const QString &sessionDir, int documents, const ConsistencyEnv &env,
                              const std::atomic<bool> *cancel, ConsistencySummary *out);

} // namespace pinpoint::charts
