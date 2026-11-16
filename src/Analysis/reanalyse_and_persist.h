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

// THE ONE RE-ANALYSIS WRITE-BACK (refactor step 1.4; step 1 design §4.7, A-09), WRITTEN BY THE SHOT (refactor step
// 3.4b-ii; DEC-103: applyAnalysis is the Shot's command, EVT-02).
//
//   persistReanalysis(write, result, PersistOptions)
//       the write half's one rule: what a re-analysis persists — the analysis and BOTH integrity verdicts the
//       pass reached (a verdict it could not reach is withdrawn), or with no result a launch-monitor-only shot's
//       rows derived again — handed to `write`, the shot's write-back. In the app `write` is the Shot's
//       applyAnalysis (through `app`, the door); a tool's is its repository's writeAnalysis (R-REC-3's tools).
//       The empty-document guard and PersistOptions::savePose (Save pose keypoints) are the repository's, as
//       before.
//   reanalyseAndPersist(write, source, deviceOnly, PersistOptions, ReanalyzeOptions)
//       both halves on the calling thread: a launch-monitor-only shot (`deviceOnly`, its kind) has nothing to
//       analyse, so its rows are derived again; any other is re-analysed, read through its RecordingSource
//       (reanalyzeShot), then persisted as above
//
// The in-app ReanalysisController runs the analysis on a worker and persists it on the GUI thread, as it always has
// — so a rating or a launch monitor reading written on the GUI thread meanwhile is never lost to a write-back that
// read the document before it. SwingLab's --write-back calls reanalyseAndPersist over its own repository (its
// --save-pose fills PersistOptions::savePose).
//
// Names no repository (P11): the write is the caller's. Application ring (src/Analysis).

#include <QString>

#include <functional>

#include "swing_reanalyzer.h"   // ReanalyzeResult, ReanalyzeOptions; RecordingSource
#include "../DomainModel/shot_command_values.h"   // the Shot's command values: AnalysisOutcome, PersistOptions, WriteResult

namespace pinpoint::analysis {

struct PersistedReanalysis {
    bool        deviceOnly = false;   // a launch-monitor-only shot: its rows, not an analysis
    bool        analysed   = false;   // a camera / IMU shot was re-analysed (false: it failed first)
    QString     analysisError;        // why it failed to analyse (analysed == false, !deviceOnly)
    WriteResult write;                // the write-back; Failed with no error when nothing analysed
    int         score = 0;            // the new analysis' score (analysed)

    bool ok() const { return (deviceOnly || analysed) && write.ok(); }
};

// The shot's write-back: what the outcome is written through (Shot::applyAnalysis in the app).
using AnalysisWrite = std::function<WriteResult(const AnalysisOutcome &outcome, const PersistOptions &options)>;

// The write half. `result` null = a launch-monitor-only shot (its rows derived again).
PersistedReanalysis persistReanalysis(const AnalysisWrite &write, const ReanalyzeResult *result,
                                      const PersistOptions &options);

// Both halves, on the calling thread: the shot read through `source` (reanalyzeShot), written through `write`.
// `reanalyse` is what re-analysis runs with (the app's defaults unless a tool says otherwise).
PersistedReanalysis reanalyseAndPersist(const AnalysisWrite &write, const RecordingSource &source, bool deviceOnly,
                                        const PersistOptions &options, const ReanalyzeOptions &reanalyse = {});

} // namespace pinpoint::analysis
