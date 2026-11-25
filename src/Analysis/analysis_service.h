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

// THE ANALYSIS SERVICE (refactor step 1.5; step 1 design §4.7; governance/artefacts/information_model.md
// §2 "The Recording contract", R-REC-4: analysis only ever runs on a Recording).
//
//   analyse(recording, effective, options)  the ONE job builder (buildAnalysisJob) → the
//                                           production analyzer → the analysis, and the two
//                                           integrity verdicts of the Recording (integrityFor)
//   analyseJob(recording, job)              the same run for a job a tool edited after the builder
//                                           (SwingLab's --bind, --trunk-calib-from, an injected club,
//                                           re-analysis' reuse gate): the job must have come from
//                                           buildAnalysisJob on this Recording
//   integrity(recording)                    the verdicts alone: a capture that ran no analysis
//   load(source)                            a stored shot's Recording, read through the RecordingSource
//                                           its Shot handed out (recording_source.h; refactor step 3.4b-ii:
//                                           "they ask the Shot", 04 step 3 item 8)
//
// Called by the live capture (ShotProcessor, on its analysis worker), re-analysis
// (reanalyzeShot, through the shot's RecordingSource: the app's ReanalysisController and the one
// write-back, reanalyseAndPersist)
// and SwingLab (swinglab_run --out / --doc-blocks / --write-back). So a shot analysed live and the
// same shot re-analysed are run, and judged, by the same code.
//
// IT HOLDS NO DATA and writes nothing: its result goes back to the caller, which persists it (the Shot's
// applyAnalysis in the app). Stateless, so any thread may call it; it reads only through the RecordingSource it is
// handed, never a reader or a repository of its own (P11).
// The analyzer call is wrapped: a throwing stage degrades to { ok=false, error }.
//
// Application ring (src/Analysis).

#include <QString>

#include "shot_analyzer.h"             // ShotAnalysisResult, ShotAnalysisJob
#include "analysis_job_builder.h"      // RunOptions, buildAnalysisJob
#include "effective_context.h"
#include "recording_integrity.h"       // IntegrityVerdicts, integrityFor
#include "../Vocabulary/Recording/recording.h"
#include "../DomainModel/recording_source.h"   // RecordingSource: how a stored shot is read (refactor step 3.4b-ii)

namespace pinpoint::analysis {

struct AnalysedRecording {
    ShotAnalysisResult analysis;      // ok / error / score / metrics / trace / detail
    IntegrityVerdicts  integrity;     // the Recording's verdicts, whether or not the analysis succeeded
};

class AnalysisService {
public:
    AnalysisService() = default;

    AnalysedRecording analyse(const pinpoint::recording::Recording &recording,
                              const EffectiveContext &effective, const RunOptions &options) const;
    AnalysedRecording analyseJob(const pinpoint::recording::Recording &recording,
                                 const ShotAnalysisJob &job) const;
    IntegrityVerdicts integrity(const pinpoint::recording::Recording &recording) const;

    // ok=false with the reason when the source reads nothing (no shot, no reader, its session unpublished).
    pinpoint::LoadedRecording load(const pinpoint::RecordingSource &source,
                                   const pinpoint::RecordingLoadOptions &options = {}) const;
};

} // namespace pinpoint::analysis
