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

#include <algorithm>
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
        st.startMs    = double(e.startNs) / 1e6;
        st.endMs      = double(e.endNs) / 1e6;
        st.thread     = e.thread;
        for (int p : e.preds)
            if (p >= 0 && p < int(ctx.trace.size())) st.preds.push_back(p);
        run.stages.push_back(st);

        if (e.ran) {
            const std::string scope = "Analysis.Stage." + e.name.toStdString();
            recordWall(prof.internScopeCopied(scope), uint64_t(e.elapsedNs));
        }
    }

    // Whole-analysis aggregate (wall ns).
    recordWall(prof.internScopeCopied("Analysis.analyze"),
               uint64_t(ctx.wall.nsecsElapsed()));

    markCriticalPath(run);
    AnalysisProfileLog::instance()->append(run);
}

void markCriticalPath(AnalysisProfileLog::AnalysisRun &run)
{
    QVector<AnalysisProfileLog::StageTiming> &st = run.stages;
    const int n = int(st.size());
    run.spanMs = run.workMs = run.criticalMs = 0.0;
    run.threads = 0;

    int last = -1;
    for (int i = 0; i < n; ++i) {
        st[i].critical = false;
        if (!st[i].ran) continue;
        run.workMs += st[i].ms;
        run.threads = std::max(run.threads, st[i].thread + 1);
        if (last < 0 || st[i].endMs > st[last].endMs) last = i;
    }
    if (last < 0) return;
    run.spanMs = st[last].endMs;

    // A stage counts as started "as soon as" its dependency ended within this much: the
    // executor starts the next stage on the thread that finished, or wakes one, in well
    // under a millisecond.
    constexpr double kStartSlackMs = 1.0;
    for (int cur = last; cur >= 0;) {
        st[cur].critical = true;
        int dep = -1;
        for (int p : st[cur].preds) {
            if (p < 0 || p >= n || p == cur || st[p].critical) continue;
            if (dep < 0 || st[p].endMs >= st[dep].endMs) dep = p;
        }
        int next = dep;
        if (dep < 0 || st[cur].startMs - st[dep].endMs > kStartSlackMs) {
            int held = -1;
            for (int k = 0; k < n; ++k) {
                if (k == cur || st[k].critical || !st[k].ran) continue;
                if (st[k].endMs > st[cur].startMs + kStartSlackMs) continue;
                if (dep >= 0 && st[k].endMs <= st[dep].endMs) continue;
                if (held < 0 || st[k].endMs > st[held].endMs) held = k;
            }
            if (held >= 0) next = held;
        }
        cur = next;
    }
    for (const AnalysisProfileLog::StageTiming &s : st)
        if (s.critical && s.ran) run.criticalMs += s.ms;
}

} // namespace pinpoint::analysis
