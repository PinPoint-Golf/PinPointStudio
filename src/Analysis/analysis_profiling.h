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

#include "AnalysisProfileLog.h"

#include <QString>

// Bridge from an analysis run's in-memory stage trace to the resource profiler.
// Called at the tail of an analyzer's analyze() (after runStages), it:
//   1. accumulates each ran stage's wall time into a "Analysis.Stage.<name>"
//      profiler scope (aggregate avg/peak/calls, surfaced in the monitor's
//      ANALYSIS STAGES table + the 60 s STATS HISTORY dumps), plus a whole-run
//      "Analysis.analyze" scope, and
//   2. appends one full per-stage record to AnalysisProfileLog for the ANALYSIS
//      RUNS drill-down.
//
// Pure instrumentation: it only READS ctx.trace/ctx.wall/ctx.detail and changes
// no analysis output, so it is byte-identical to omitting it (nothing new is
// serialized into swing.json).  Safe to call from the analysis worker thread.

namespace pinpoint::analysis {

struct AnalysisContext;

// profileName is the SessionProfile name ("Wrist" | "CameraKinematics").  ok is
// derived internally from ctx.halted.
void recordAnalysisRun(const QString &profileName, const AnalysisContext &ctx);

// The chain of stages that set the run's wall time, walked back from the ran stage
// that ended last. Each step follows the stage's latest-ending dependency (preds) when
// it started as soon as that dependency ended; when it started later than that it was
// held by something else — a free pool thread, its camera, the series cursor — and the
// step follows the ran stage whose end released it. Marks StageTiming::critical and
// fills spanMs / workMs / criticalMs / threads. In a sequential run every ran stage is
// on the path. Called by recordAnalysisRun; public for the unit test.
void markCriticalPath(AnalysisProfileLog::AnalysisRun &run);

} // namespace pinpoint::analysis
