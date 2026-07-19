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

#include "analysis_profiling.h"

#include "AnalysisProfileLog.h"
#include "analysis_stage.h"   // AnalysisContext, StageTraceEntry (+ swing_analysis.h, shot_analyzer.h)
#include "pp_profiler.h"

#include <string>

namespace pinpoint::analysis {

void recordAnalysisRun(const QString &profileName, const AnalysisContext &ctx)
{
    using pinpoint::profiling::Profiler;
    using pinpoint::profiling::recordWall;
    Profiler &prof = Profiler::instance();

    AnalysisProfileLog::AnalysisRun run;
    run.profile     = profileName;
    run.ok          = !ctx.halted;
    run.sessionType = ctx.job.sessionType;
    run.totalMs     = double(ctx.wall.elapsed());
    if (ctx.detail) {
        run.frames = int(ctx.detail->pose2d.frames.size());
        run.score  = double(ctx.detail->score.overall);
    }

    // Per-stage: build the drill-down breakdown and feed the aggregate scopes.
    // internScopeCopied dedups by name (off the hot path — this runs at shot
    // cadence, not per frame), so no separate call-site cache is needed.
    run.stages.reserve(int(ctx.trace.size()));
    for (const StageTraceEntry &e : ctx.trace) {
        AnalysisProfileLog::StageTiming st;
        st.name       = e.name;
        st.ran        = e.ran;
        st.skipReason = e.skipReason;
        st.ms         = e.ran ? double(e.elapsedNs) / 1e6 : 0.0;
        run.stages.push_back(st);

        if (e.ran) {
            const std::string scope = "Analysis.Stage." + e.name.toStdString();
            recordWall(prof.internScopeCopied(scope), uint64_t(e.elapsedNs));
        }
    }

    // Whole-analysis aggregate (wall ns).
    recordWall(prof.internScopeCopied("Analysis.analyze"),
               uint64_t(ctx.wall.nsecsElapsed()));

    AnalysisProfileLog::instance()->append(run);
}

} // namespace pinpoint::analysis
