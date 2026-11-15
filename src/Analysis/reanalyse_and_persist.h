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

// THE ONE RE-ANALYSIS WRITE-BACK (refactor step 1.4; step 1 design §4.7, A-09).
//
//   reanalyseAndPersist(repo, reader, shot, PersistOptions)
//       a launch-monitor-only shot: its launch monitor rows derived again (nothing to analyse);
//       any other: re-analyse it (reanalyzeShot, read through `reader`) → persistReanalysis
//   persistReanalysis(repo, shot, result, PersistOptions)
//       the write half alone: RecordingRepository::writeAnalysis with the analysis and BOTH
//       integrity verdicts the pass reached (a verdict it could not reach is withdrawn), the
//       repository's empty-document guard, and PersistOptions::savePose (Save pose keypoints)
//
// The in-app ReanalysisController runs the analysis on a worker and calls persistReanalysis on the
// GUI thread, as it always has — so a rating or a launch monitor reading written on the GUI thread
// meanwhile is never lost to a write-back that read the document before it. SwingLab's --write-back
// calls reanalyseAndPersist (its --save-pose fills PersistOptions::savePose). The analysis half runs on
// the analysis service (reanalyzeShot, reading through the RecordingReader → AnalysisService, steps 1.5
// and 2.4); the write half stays this one.
//
// Keyed by the shot's ShotRef (step 2.4; IM-S2-2: DEC-160), as the repository and the reader are: a
// caller holding a folder translates it once, at its edge (RecordingRepository::shotOfFolder).
//
// Application ring (src/Analysis): names the repository interface, never the store.

#include <QString>

#include "swing_reanalyzer.h"   // ReanalyzeResult, ReanalyzeOptions
#include "../DomainModel/recording_repository.h"

namespace pinpoint::analysis {

struct PersistedReanalysis {
    bool        deviceOnly = false;   // a launch-monitor-only shot: its rows, not an analysis
    bool        analysed   = false;   // a camera / IMU shot was re-analysed (false: it failed first)
    QString     analysisError;        // why it failed to analyse (analysed == false, !deviceOnly)
    WriteResult write;                // the write-back; Failed with no error when nothing analysed
    int         score = 0;            // the new analysis' score (analysed)

    bool ok() const { return (deviceOnly || analysed) && write.ok(); }
};

// The write half, by the shot's ShotRef (step 2.4). `result` null = a launch-monitor-only shot (its
// rows derived again).
PersistedReanalysis persistReanalysis(RecordingRepository &repo, const ShotRef &shot,
                                      const ReanalyzeResult *result, const PersistOptions &options);

// Both halves, on the calling thread: the shot read through `reader` (reanalyzeShot), written through
// `repo`. `reanalyse` is what re-analysis runs with (the app's defaults unless a tool says otherwise).
PersistedReanalysis reanalyseAndPersist(RecordingRepository &repo, RecordingReader &reader, const ShotRef &shot,
                                        const PersistOptions &options,
                                        const ReanalyzeOptions &reanalyse = {});


} // namespace pinpoint::analysis
