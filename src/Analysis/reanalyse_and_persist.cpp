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

#include "reanalyse_and_persist.h"

namespace pinpoint::analysis {

PersistedReanalysis persistReanalysis(RecordingRepository &repo, const ShotRef &shot,
                                      const ReanalyzeResult *result, const PersistOptions &options)
{
    PersistedReanalysis out;
    if (!result) {
        out.deviceOnly = true;
        out.write = repo.writeAnalysis(shot, AnalysisOutcome{}, options);
        return out;
    }
    if (!result->ok || !result->analysis.detail) {
        out.analysisError = result->error;
        return out;   // nothing analysed, nothing written
    }
    out.analysed = true;
    out.score    = result->analysis.score;

    AnalysisOutcome outcome;
    outcome.analysis         = result->analysis.detail.get();
    outcome.imuIntegrity     = result->imuIntegrity ? &*result->imuIntegrity : nullptr;
    outcome.captureIntegrity = result->captureIntegrity ? &*result->captureIntegrity : nullptr;
    out.write = repo.writeAnalysis(shot, outcome, options);
    return out;
}

PersistedReanalysis reanalyseAndPersist(RecordingRepository &repo, RecordingReader &reader, const ShotRef &shot,
                                        const PersistOptions &options, const ReanalyzeOptions &reanalyse)
{
    if (repo.isDeviceOnly(shot))
        return persistReanalysis(repo, shot, nullptr, options);
    const ReanalyzeResult r = reanalyzeShot(reader, shot, reanalyse);
    return persistReanalysis(repo, shot, &r, options);
}



} // namespace pinpoint::analysis
