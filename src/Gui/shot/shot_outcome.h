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

// WHO delivers the join for a given launch. Naming it is the point: the join
// used to be assumed to arrive and for one configuration it never did.
enum class JoinArrival : uint8_t {
    AnalysisWorker,      // the ordinary path — onAnalysisFinished() joins
    ExportWorker,        // corpus capture with something to encode
    LaunchPathDirectly,  // no worker starts, so finishGatherAndLaunch() joins itself
    Never,               // nothing joins — the shot wedges. Must be unreachable.
};

constexpr JoinArrival joinArrivalFor(const ShotLaunch &l)
{
    // The ordinary path always launches the analysis worker, and
    // onAnalysisFinished() calls maybeJoin() unconditionally after
    // startSwingSave() — so the export's early returns are covered there.
    if (!l.skipAnalysisCapture)
        return JoinArrival::AnalysisWorker;
    // Corpus capture launches the export and nothing else.
    if (l.swingDirAllocated && l.hasExportableCameras)
        return JoinArrival::ExportWorker;
    // ⚠ AND HERE IS WHERE THE SHOT USED TO DIE. startSwingSave() has two early
    // returns — no swing folder (an unwritable library: /mnt/swingdata
    // unmounted, 1 September 2026) and no exportable camera (an IMU-only
    // corpus shot) — and neither starts a worker. maybeJoin() only ever ran
    // from a worker's completion handler, so nothing joined: ShotProcessor sat
    // in Processing for ever, busy() stayed true so ShotController never
    // re-armed, and the SwingWindow held the EventBuffer Paused. Capture was
    // dead for the rest of the session with nothing on screen to say so.
    // finishGatherAndLaunch() now joins the shot itself in this case.
    return JoinArrival::LaunchPathDirectly;
}

// Every shot the pipeline accepts must reach a join, however little it produced:
// the join is what returns the buffer to the user's capture intent.
constexpr bool joinIsReachable(const ShotLaunch &l)
{
    return joinArrivalFor(l) != JoinArrival::Never;
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
    bool        analysisOk         = false;  // == Succeeded — it produced something
    bool        exportOk           = false;  // == Succeeded — it produced something
    // ⚠ NOT THE NEGATION OF THE TWO ABOVE, AND THE DIFFERENCE IS THE WHOLE
    // POINT OF StageOutcome::Skipped. A stage that was deliberately not run —
    // analysis off for a corpus capture, an export with no camera to encode —
    // produced nothing AND went wrong with nothing. Folding the two together is
    // what told the golfer their shot had failed when the pipeline had done
    // exactly what was asked of it.
    bool        analysisFaulted    = false;  // == Failed
    bool        exportFaulted      = false;  // == Failed
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
    d.analysisFaulted = in.analysis    == StageOutcome::Failed;
    d.exportFaulted   = in.mediaExport == StageOutcome::Failed;

    d.includeAnalysis  = d.analysisOk && in.hasAnalysisDetail;
    d.reviewableOnDisk = d.analysisOk && d.exportOk && in.documentWritten;

    // §7.5 R4 — what the shot BECAME. It became a shot when nothing went wrong
    // and something was kept: a stage that was skipped on purpose is not a
    // fault, and a shot with no document is not a shot however well the stages
    // ran, because there is nothing left of it after a restart.
    d.terminal = (!d.analysisFaulted && !d.exportFaulted && in.documentWritten)
                     ? Terminal::Processed : Terminal::Failed;

    if (d.reviewableOnDisk)
        d.after = AfterJoin::Finish;               // Review owns the playback
    else if (in.autoReplay && !in.skipAnalysisCapture && in.hasReplayTracks)
        d.after = AfterJoin::Replay;               // in-window ¼× fallback
    else
        d.after = AfterJoin::Finish;

    return d;
}

} // namespace pinpoint
