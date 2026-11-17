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

namespace {
WriteResult noWrite()
{
    return { WriteResult::Status::Failed, QStringLiteral("no write-back for this shot") };
}
} // namespace

PersistedReanalysis persistReanalysis(const AnalysisWrite &write, const ReanalyzeResult *result,
                                      const PersistOptions &options)
{
    PersistedReanalysis out;
    if (!result) {
        out.deviceOnly = true;
        out.write = write ? write(AnalysisOutcome{}, options) : noWrite();
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
    out.write = write ? write(outcome, options) : noWrite();
    return out;
}

PersistedReanalysis reanalyseAndPersist(const AnalysisWrite &write, const RecordingSource &source, bool deviceOnly,
                                        const PersistOptions &options, const ReanalyzeOptions &reanalyse)
{
    if (deviceOnly)
        return persistReanalysis(write, nullptr, options);
    const ReanalyzeResult r = reanalyzeShot(source, reanalyse);
    return persistReanalysis(write, &r, options);
}

} // namespace pinpoint::analysis
