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

#include "analysis_service.h"

#include "swing_window.h"

#include <exception>

namespace pinpoint::analysis {

AnalysedRecording AnalysisService::analyse(const pinpoint::recording::Recording &recording,
                                           const EffectiveContext &effective, const RunOptions &options) const
{
    return analyseJob(recording, buildAnalysisJob(recording, effective, options));
}

AnalysedRecording AnalysisService::analyseJob(const pinpoint::recording::Recording &recording,
                                              const ShotAnalysisJob &job) const
{
    AnalysedRecording out;
    if (!recording.window) {
        out.analysis.error = QStringLiteral("no recorded streams to analyse");
        return out;
    }
    // A stopped job (#294): nothing runs, or nothing that ran is returned, and the verdicts are not reached.
    const auto stopped = [&out] {
        out.analysis       = ShotAnalysisResult{};
        out.analysis.error = QStringLiteral("stopped: the app is quitting");
        out.stopped        = true;
        return out;
    };
    if (job.stopRequested())
        return stopped();
    // Exception barrier: anything escaping a stage (e.g. a cv::Exception on malformed frame
    // geometry) degrades to a failed analysis rather than terminating the caller's thread.
    // A stop asked while it ran ends it with AnalysisStopped (or the pose runner's PoseRunStopped).
    try {
        auto analyzer = makeShotAnalyzer();
        out.analysis  = analyzer->analyze(*recording.window, job);
    } catch (const std::exception &e) {
        out.analysis       = ShotAnalysisResult{};
        out.analysis.error = QStringLiteral("analyzer threw: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        out.analysis       = ShotAnalysisResult{};
        out.analysis.error = QStringLiteral("analyzer threw an unknown exception");
    }
    if (job.stopRequested())   // whatever ended it, a run its owner stopped returns nothing
        return stopped();
    // The verdicts do not depend on the analysis: they are the Recording's.
    out.integrity = integrityFor(recording);
    return out;
}

IntegrityVerdicts AnalysisService::integrity(const pinpoint::recording::Recording &recording) const
{
    return integrityFor(recording);
}

pinpoint::LoadedRecording AnalysisService::load(const pinpoint::RecordingSource &source,
                                                const pinpoint::RecordingLoadOptions &options) const
{
    return source.read(options);
}


} // namespace pinpoint::analysis
