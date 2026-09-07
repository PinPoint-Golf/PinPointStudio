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

#include <cstdint>

// Shot launch/join decisions — pure logic, header-only, no Qt; unit-tested
// standalone in src/Analysis/tests (the shot_arbiter.h / session_summary.h
// precedent for Gui headers).
//
// ⚠ THIS IS A SEAM, NOT A FIX. Every rule below reproduces EXACTLY what
// ShotProcessor::maybeJoin() and finishGatherAndLaunch() did inline before the
// extraction, defects included. It was moved because it could not be reached
// otherwise: shot_processor.cpp includes camera_instance.h, imu_manager.h,
// ShotAnalyzer and ImuVisionFuser, so linking a test against the decision logic
// meant linking ONNX, OpenCV and FFmpeg. Here it costs nothing.
//
// The contract these decisions are supposed to honour is design §7.5 R4: ONE
// terminal statement per shot, saying what the shot BECAME. shot_outcome_test
// asserts that contract; where a rule below contradicts it, the test is right
// and the rule is the bug.
namespace pinpoint {

// Per-stage result. Pending means "worker still running"; Skipped means the
// stage was deliberately not run for this shot (corpus capture with analysis
// off, a shot with no exportable cameras) and is NOT a failure.
enum class StageOutcome : uint8_t { Pending, Succeeded, Failed, Skipped };

// ── Launch: is a join guaranteed to be reached? ─────────────────────────────
//
// maybeJoin() is only ever called from a worker's finished() handler, so a shot
// that launches no worker never joins: the processor stays busy(), the trigger
// never re-arms and the buffer never resumes. Two things launch a worker —
// analysis (always, unless the corpus-capture skip is on) and the media export
// (only when it has both a swing directory and a camera to encode).
struct ShotLaunch {
    bool skipAnalysisCapture   = false;  // saveRawFrames && skipAnalysisForRawCapture
    bool swingDirAllocated     = false;  // SwingPaths gave the shot a folder
    bool hasExportableCameras  = false;  // the export job has at least one camera
};

// ⚠ NOT YET CONSULTED BY THE PIPELINE. finishGatherAndLaunch() still returns
// straight after startSwingSave() on the corpus-capture path without checking
// this, which is the defect shot_outcome_test pins down; wiring it in is the
// fix, and the fix is deliberately not part of the extraction.
constexpr bool joinIsReachable(const ShotLaunch &l)
{
    // The ordinary path always launches the analysis worker, and
    // onAnalysisFinished() calls maybeJoin() unconditionally after
    // startSwingSave() — so the export's early returns are covered there.
    if (!l.skipAnalysisCapture)
        return true;
    // Corpus capture launches the export and nothing else, so the export's own
    // early returns decide whether anything ever joins.
    return l.swingDirAllocated && l.hasExportableCameras;
}

// ── Join: what the shot became ──────────────────────────────────────────────

// Which swing.json, if any, the join writes.
enum class PersistPath : uint8_t {
    None,                  // nothing to write — the shot is in-memory only
    FullDocument,          // the exporter's manifest (+ inline analysis when there is any)
    AnalysisOnlyDocument,  // export failed/skipped — synthesised header + analysis
};

// The single terminal statement (§7.5 R4).
enum class Terminal : uint8_t { Processed, Failed };

// What happens once the statement is made.
enum class AfterJoin : uint8_t { Finish, Replay };

struct ShotJoinInputs {
    StageOutcome analysis    = StageOutcome::Pending;
    StageOutcome mediaExport = StageOutcome::Pending;
    bool hasAnalysisDetail   = false;  // the analyzer returned a detail payload
    bool swingDirAllocated   = false;  // m_swingDir is non-empty
    bool documentWritten     = false;  // the persist attempt actually wrote a file
    bool skipAnalysisCapture = false;
    bool autoReplay          = true;   // AppSettings::autoReplayAfterCapture()
    bool hasReplayTracks     = false;  // the window captured at least one camera track
};

struct ShotJoinDecision {
    bool        ready              = false;  // both stages settled
    bool        analysisOk         = false;
    bool        exportOk           = false;
    bool        includeAnalysis    = false;  // inline the analysis block in the document
    bool        reviewableOnDisk   = false;  // promote straight into Review
    Terminal    terminal           = Terminal::Failed;
    AfterJoin   after              = AfterJoin::Finish;
};

// Which document the join writes. Called BEFORE the write; the outcome of the
// write itself comes back as ShotJoinInputs::documentWritten.
constexpr PersistPath persistPathFor(const ShotJoinInputs &in)
{
    if (in.mediaExport == StageOutcome::Succeeded)
        return PersistPath::FullDocument;
    if (in.analysis == StageOutcome::Succeeded && in.hasAnalysisDetail && in.swingDirAllocated)
        return PersistPath::AnalysisOnlyDocument;
    return PersistPath::None;
}

constexpr ShotJoinDecision decideJoin(const ShotJoinInputs &in)
{
    ShotJoinDecision d;
    // Wait for BOTH workers. Skipped counts as settled; Pending does not.
    if (in.analysis == StageOutcome::Pending || in.mediaExport == StageOutcome::Pending)
        return d;
    d.ready = true;

    d.analysisOk = in.analysis    == StageOutcome::Succeeded;
    d.exportOk   = in.mediaExport == StageOutcome::Succeeded;

    d.includeAnalysis  = d.analysisOk && in.hasAnalysisDetail;
    d.reviewableOnDisk = d.analysisOk && d.exportOk && in.documentWritten;
    d.terminal = (d.analysisOk && d.exportOk) ? Terminal::Processed : Terminal::Failed;

    if (d.reviewableOnDisk)
        d.after = AfterJoin::Finish;               // Review owns the playback
    else if (in.autoReplay && !in.skipAnalysisCapture && in.hasReplayTracks)
        d.after = AfterJoin::Replay;               // in-window ¼× fallback
    else
        d.after = AfterJoin::Finish;

    return d;
}

} // namespace pinpoint
