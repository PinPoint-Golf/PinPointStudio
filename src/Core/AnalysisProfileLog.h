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

#include <QList>
#include <QMutex>
#include <QString>
#include <QVector>

// Thread-safe singleton ring of per-analysis run records — a sibling of
// PpStatsLog, but where PpStatsLog carries the coarse periodic profiler summary
// (aggregate scopes/gauge), this ring carries ONE record per analyze() call with
// its full per-stage timing breakdown, so the monitor can drill into a specific
// analysis.  The analysis bridge (src/Analysis/analysis_profiling.cpp) is the
// sole producer; the profiler controller polls fetchSince() and surfaces it in
// the monitor's ANALYSIS RUNS tab with its own Clear + Export.
//
// Appended from the analysis worker thread (a QtConcurrent pool thread on the
// live/re-analyse paths, the main thread offline) — the mutex makes that safe,
// exactly like PpStatsLog.  This is runtime telemetry only; nothing here is ever
// serialized into swing.json (the analysis pipeline forbids persisting the
// per-stage trace).
class AnalysisProfileLog
{
public:
    // One stage's contribution within a run.  Skipped stages carry ms == 0 and a
    // skipReason (e.g. "halted", or the stage's canRun reason) so a camera-only
    // or IMU-only run is legible from the breakdown alone.
    struct StageTiming {
        QString name;
        double  ms       = 0.0;
        bool    ran      = false;
        QString skipReason;
    };

    // One analyze() call.  score/frames are 0 on a halted (ok == false) run whose
    // detail projection is null.
    struct AnalysisRun {
        QString              timestamp;      // "HH:mm:ss"
        QString              profile;        // "Wrist" | "CameraKinematics"
        bool                 ok          = false;
        int                  sessionType = -1;
        double               totalMs     = 0.0;
        int                  frames      = 0;
        double               score       = 0.0;
        QVector<StageTiming> stages;
        int                  seq         = 0; // monotonically increasing, for polling
    };

    static AnalysisProfileLog *instance();

    // Append a completed run — safe from any thread.
    void append(const AnalysisRun &run);

    // Returns all runs with seq > afterSeq.  Updates *afterSeq to the latest seq
    // seen.  Safe to call from any thread.
    QList<AnalysisRun> fetchSince(int &afterSeq) const;

    // Current highest sequence number without fetching runs.
    int currentSeq() const;

    // Drop every retained run (the sequence counter keeps climbing so pollers are
    // not confused).  Used by tests; the controller's "Clear" advances its own
    // read cursor instead, leaving the ring intact.
    void clear();

    static constexpr int kMaxRuns = 200;

private:
    AnalysisProfileLog() = default;

    mutable QMutex     m_mutex;
    QList<AnalysisRun> m_runs;
    int                m_nextSeq = 0;
};
