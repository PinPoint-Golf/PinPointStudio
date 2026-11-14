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

// THE ANALYSIS SERVICE (refactor step 1.5; step 1 design §4.7; docs/developer/information_model.md
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
//   load(swingDir)                          a stored shot's Recording, through the RecordingReader
//                                           the service was given (src/DomainModel/recording_reader.h)
//
// Called by the live capture (ShotProcessor, on its analysis worker), re-analysis
// (reanalyzeSwingDir: the app's ReanalysisController and the one write-back, reanalyseAndPersist)
// and SwingLab (swinglab_run --out / --doc-blocks / --write-back). So a shot analysed live and the
// same shot re-analysed are run, and judged, by the same code.
//
// IT HOLDS NO DATA and writes nothing: its result goes back to the caller, which persists it through
// RecordingRepository. Stateless (the reader it is given is stateless too), so any thread may call it.
// The analyzer call is wrapped: a throwing stage degrades to { ok=false, error }.
//
// Application ring (src/Analysis).

#include <QString>

#include "shot_analyzer.h"             // ShotAnalysisResult, ShotAnalysisJob
#include "analysis_job_builder.h"      // RunOptions, buildAnalysisJob
#include "effective_context.h"
#include "recording_integrity.h"       // IntegrityVerdicts, integrityFor
#include "../Recording/recording.h"
#include "../DomainModel/recording_reader.h"

namespace pinpoint::analysis {

struct AnalysedRecording {
    ShotAnalysisResult analysis;      // ok / error / score / metrics / trace / detail
    IntegrityVerdicts  integrity;     // the Recording's verdicts, whether or not the analysis succeeded
};

class AnalysisService {
public:
    // `reader` is what load() reads stored shots through; the live capture needs none.
    explicit AnalysisService(pinpoint::RecordingReader *reader = nullptr) : m_reader(reader) {}

    AnalysedRecording analyse(const pinpoint::recording::Recording &recording,
                              const EffectiveContext &effective, const RunOptions &options) const;
    AnalysedRecording analyseJob(const pinpoint::recording::Recording &recording,
                                 const ShotAnalysisJob &job) const;
    IntegrityVerdicts integrity(const pinpoint::recording::Recording &recording) const;

    // ok=false with an error when the service was given no reader.
    pinpoint::LoadedRecording load(const QString &swingDir,
                                   const pinpoint::RecordingLoadOptions &options = {}) const;

private:
    pinpoint::RecordingReader *m_reader = nullptr;
};

} // namespace pinpoint::analysis
