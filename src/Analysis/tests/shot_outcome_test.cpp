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

// shot_outcome.h — ShotProcessor's launch and join decisions.
//
// ⚠ WHAT THIS SUITE ASSERTS IS THE CONTRACT, NOT THE CODE.  Every expectation
// below is derived from a stated invariant — design §7.5 R4 ("one terminal
// statement per shot, saying what the shot BECAME"), the StageOutcome
// vocabulary's own distinction between Failed and Skipped, and the header
// comment on maybeJoin() that says the buffer must resume however the shot
// ends.  Nothing here was obtained by running the code and writing down the
// answer: a test built that way certifies whatever is there and can never find
// a defect.  Where an expectation below fails, the contract is right and the
// pipeline is wrong.

#include "shot/shot_outcome.h"

#include <cstdio>
#include <cstring>

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const char *label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_fail;
}

static const char *stageName(StageOutcome s)
{
    switch (s) {
    case StageOutcome::Pending:   return "Pending";
    case StageOutcome::Succeeded: return "Succeeded";
    case StageOutcome::Failed:    return "Failed";
    case StageOutcome::Skipped:   return "Skipped";
    }
    return "?";
}

static const char *terminalName(Terminal t)
{
    return t == Terminal::Processed ? "Processed" : "Failed";
}

// ---------------------------------------------------------------------------
// L — launch: a shot that launches no worker never joins
// ---------------------------------------------------------------------------
//
// maybeJoin() runs only from a worker's finished() handler.  A shot that starts
// no worker therefore never reaches it, and ShotProcessor stays in Processing
// forever: busy() stays true, ShotController never re-arms, and the SwingWindow
// keeps the EventBuffer Paused.  Capture is dead for the rest of the session
// with nothing on screen to say so.  The invariant is that EVERY shot the
// pipeline accepts must reach a join.

static void testLaunchReachability()
{
    std::printf("\nL — every accepted shot must reach a join\n");

    // The ordinary path always launches the analysis worker, and
    // onAnalysisFinished() calls maybeJoin() whatever the export did.
    {
        ShotLaunch l;
        l.skipAnalysisCapture  = false;
        l.swingDirAllocated    = false;
        l.hasExportableCameras = false;
        check("L1 ordinary shot joins even with no folder and no cameras",
              joinIsReachable(l));
    }

    // Corpus capture (saveRawFrames + skipAnalysisForRawCapture) launches the
    // export and nothing else, so the export's own early returns decide.
    {
        ShotLaunch l;
        l.skipAnalysisCapture  = true;
        l.swingDirAllocated    = true;
        l.hasExportableCameras = true;
        check("L2 corpus capture with a folder and cameras joins", joinIsReachable(l));
    }

    // ⭐ The /mnt/swingdata case.  The library is unreachable, SwingPaths hands
    // back no folder, startSwingSave() marks the export Skipped and returns
    // WITHOUT starting a worker — and the corpus path returned before calling
    // maybeJoin().  Nothing will ever join this shot.
    {
        ShotLaunch l;
        l.skipAnalysisCapture  = true;
        l.swingDirAllocated    = false;
        l.hasExportableCameras = true;
        check("L3 corpus capture onto an unwritable library still joins",
              joinIsReachable(l));
    }

    // Same wedge by the other early return: an IMU-only corpus capture with no
    // camera to encode.
    {
        ShotLaunch l;
        l.skipAnalysisCapture  = true;
        l.swingDirAllocated    = true;
        l.hasExportableCameras = false;
        check("L4 corpus capture with no exportable camera still joins",
              joinIsReachable(l));
    }
}

// ---------------------------------------------------------------------------
// J — the join gate
// ---------------------------------------------------------------------------

static void testJoinGate()
{
    std::printf("\nJ — the join waits for both stages and no longer\n");

    const StageOutcome settled[] = { StageOutcome::Succeeded, StageOutcome::Failed,
                                     StageOutcome::Skipped };

    for (StageOutcome a : settled) {
        ShotJoinInputs in;
        in.analysis    = a;
        in.mediaExport = StageOutcome::Pending;
        char label[128];
        std::snprintf(label, sizeof label,
                      "J1 analysis=%s export=Pending does not join", stageName(a));
        check(label, !decideJoin(in).ready);
    }

    for (StageOutcome e : settled) {
        ShotJoinInputs in;
        in.analysis    = StageOutcome::Pending;
        in.mediaExport = e;
        char label[128];
        std::snprintf(label, sizeof label,
                      "J2 analysis=Pending export=%s does not join", stageName(e));
        check(label, !decideJoin(in).ready);
    }

    // Skipped is settled, not pending — a deliberately skipped stage must not
    // hold the join open, or the buffer never resumes.
    for (StageOutcome a : settled) {
        for (StageOutcome e : settled) {
            ShotJoinInputs in;
            in.analysis    = a;
            in.mediaExport = e;
            char label[128];
            std::snprintf(label, sizeof label,
                          "J3 analysis=%s export=%s joins", stageName(a), stageName(e));
            check(label, decideJoin(in).ready);
        }
    }
}

// ---------------------------------------------------------------------------
// T — §7.5 R4: one terminal statement, and it must be true
// ---------------------------------------------------------------------------
//
// The signal says what the shot BECAME.  A stage that was deliberately not run
// did not fail, and telling the golfer it did is the failure this rule exists
// to prevent — the whole point of R4 was that "shotFailed ... has never once
// been said out loud", so now that it IS said out loud it has to be accurate.
// StageOutcome distinguishes Failed from Skipped; the terminal statement must
// honour that distinction or the enum value means nothing.

static void expectTerminal(const char *label, ShotJoinInputs in, Terminal want)
{
    const ShotJoinDecision d = decideJoin(in);
    char msg[192];
    std::snprintf(msg, sizeof msg, "%s (got %s, want %s)",
                  label, terminalName(d.terminal), terminalName(want));
    check(msg, d.terminal == want);
}

static void testTerminalStatement()
{
    std::printf("\nT — the terminal statement says what the shot became\n");

    {
        ShotJoinInputs in;
        in.analysis        = StageOutcome::Succeeded;
        in.mediaExport     = StageOutcome::Succeeded;
        in.documentWritten = true;
        expectTerminal("T1 both stages succeeded", in, Terminal::Processed);
    }
    {
        ShotJoinInputs in;
        in.analysis    = StageOutcome::Failed;
        in.mediaExport = StageOutcome::Succeeded;
        expectTerminal("T2 analysis genuinely failed", in, Terminal::Failed);
    }
    {
        ShotJoinInputs in;
        in.analysis    = StageOutcome::Succeeded;
        in.mediaExport = StageOutcome::Failed;
        expectTerminal("T3 export genuinely failed", in, Terminal::Failed);
    }

    // ⭐ A corpus capture asked for the analysis to be skipped.  It was skipped.
    // The export succeeded and the frames are on disk.  Nothing failed, and the
    // shot must not be announced as a failure — the ONE statement the user gets
    // for this shot would otherwise be false.  (In the pipeline this is worse
    // than merely misleading: shotFailed carries m_analysisResult.error, which
    // was reset to {} for this shot, so the toast is empty.)
    {
        ShotJoinInputs in;
        in.analysis            = StageOutcome::Skipped;
        in.mediaExport         = StageOutcome::Succeeded;
        in.documentWritten     = true;
        in.skipAnalysisCapture = true;
        expectTerminal("T4 analysis skipped by request, export fine", in, Terminal::Processed);
    }

    // ⭐ An IMU-only shot has no camera to encode, so the export is Skipped and
    // an analysis-only swing.json is written on purpose.  That is the designed
    // path ("no exportable cameras — analysis-only swing"), not a failure.
    {
        ShotJoinInputs in;
        in.analysis          = StageOutcome::Succeeded;
        in.mediaExport       = StageOutcome::Skipped;
        in.hasAnalysisDetail = true;
        in.swingDirAllocated = true;
        in.documentWritten   = true;
        expectTerminal("T5 export skipped, analysis-only swing written", in, Terminal::Processed);
    }

    // The genuinely empty shot: nothing ran, nothing was saved.  This one IS a
    // failure and must say so.
    {
        ShotJoinInputs in;
        in.analysis    = StageOutcome::Skipped;
        in.mediaExport = StageOutcome::Skipped;
        expectTerminal("T6 both stages skipped, nothing persisted", in, Terminal::Failed);
    }
}

// ---------------------------------------------------------------------------
// P — the persist path
// ---------------------------------------------------------------------------
//
// The stated rule (maybeJoin comment): "savedSwingDir is set only when a
// swing.json was actually written, so the carousel row links to a real file
// ... and an unwritten shot stays in-memory only".  The corollary the pipeline
// must honour: whenever there IS something worth keeping and somewhere to keep
// it, a document is written — a shot that survives a restart is the difference
// between a session and a lost afternoon.

static const char *persistName(PersistPath p)
{
    switch (p) {
    case PersistPath::None:                 return "None";
    case PersistPath::FullDocument:         return "FullDocument";
    case PersistPath::AnalysisOnlyDocument: return "AnalysisOnlyDocument";
    }
    return "?";
}

static void expectPersist(const char *label, ShotJoinInputs in, PersistPath want)
{
    const PersistPath got = persistPathFor(in);
    char msg[192];
    std::snprintf(msg, sizeof msg, "%s (got %s, want %s)",
                  label, persistName(got), persistName(want));
    check(msg, got == want);
}

static void testPersistPath()
{
    std::printf("\nP — what the join writes to disk\n");

    {
        ShotJoinInputs in;
        in.analysis          = StageOutcome::Succeeded;
        in.mediaExport       = StageOutcome::Succeeded;
        in.hasAnalysisDetail = true;
        in.swingDirAllocated = true;
        expectPersist("P1 both succeeded -> full document", in, PersistPath::FullDocument);
    }
    {
        // Raw-only: the export wrote media, the analysis did not run. The
        // document is still the exporter's, minus the analysis block.
        ShotJoinInputs in;
        in.analysis          = StageOutcome::Skipped;
        in.mediaExport       = StageOutcome::Succeeded;
        in.swingDirAllocated = true;
        expectPersist("P2 export succeeded, analysis skipped -> full document (raw only)",
                      in, PersistPath::FullDocument);
        check("P2b raw-only document carries no analysis block",
              !decideJoin(in).includeAnalysis);
    }
    {
        ShotJoinInputs in;
        in.analysis          = StageOutcome::Succeeded;
        in.mediaExport       = StageOutcome::Failed;
        in.hasAnalysisDetail = true;
        in.swingDirAllocated = true;
        expectPersist("P3 export failed, analysis succeeded -> analysis-only document",
                      in, PersistPath::AnalysisOnlyDocument);
    }
    {
        // No folder to write into — nothing can be persisted, and the row stays
        // in memory. This is the one legitimate None.
        ShotJoinInputs in;
        in.analysis          = StageOutcome::Succeeded;
        in.mediaExport       = StageOutcome::Skipped;
        in.hasAnalysisDetail = true;
        in.swingDirAllocated = false;
        expectPersist("P4 no swing folder -> nothing persisted", in, PersistPath::None);
    }
    {
        ShotJoinInputs in;
        in.analysis    = StageOutcome::Failed;
        in.mediaExport = StageOutcome::Failed;
        expectPersist("P5 both failed -> nothing persisted", in, PersistPath::None);
    }
}

// ---------------------------------------------------------------------------
// R — reviewable, and what plays afterwards
// ---------------------------------------------------------------------------
//
// "Reviewable on disk" is what the UI promotes straight into Review, so it must
// mean the files are genuinely there.  And however the shot ends, exactly one
// of the two continuations must be chosen: both of them lead to finishShot(),
// which is what returns the buffer to the user's capture intent.

static void testReviewableAndReplay()
{
    std::printf("\nR — reviewable-on-disk and the continuation\n");

    {
        ShotJoinInputs in;
        in.analysis          = StageOutcome::Succeeded;
        in.mediaExport       = StageOutcome::Succeeded;
        in.documentWritten   = true;
        in.hasReplayTracks   = true;
        const ShotJoinDecision d = decideJoin(in);
        check("R1 analysis+export+document -> reviewable", d.reviewableOnDisk);
        check("R1b reviewable shots skip the in-window transient",
              d.after == AfterJoin::Finish);
    }
    {
        // The write itself failed: the media may be on disk but nothing indexes
        // it, so Review has nothing to open.
        ShotJoinInputs in;
        in.analysis        = StageOutcome::Succeeded;
        in.mediaExport     = StageOutcome::Succeeded;
        in.documentWritten = false;
        in.hasReplayTracks = true;
        const ShotJoinDecision d = decideJoin(in);
        check("R2 no document written -> not reviewable", !d.reviewableOnDisk);
        check("R2b and the in-window transient plays instead", d.after == AfterJoin::Replay);
    }
    {
        ShotJoinInputs in;
        in.analysis        = StageOutcome::Failed;
        in.mediaExport     = StageOutcome::Succeeded;
        in.autoReplay      = false;
        in.hasReplayTracks = true;
        check("R3 auto-replay off -> no transient",
              decideJoin(in).after == AfterJoin::Finish);
    }
    {
        // Corpus capture wants uninterrupted back-to-back hitting.
        ShotJoinInputs in;
        in.analysis            = StageOutcome::Skipped;
        in.mediaExport         = StageOutcome::Succeeded;
        in.documentWritten     = true;
        in.skipAnalysisCapture = true;
        in.hasReplayTracks     = true;
        check("R4 corpus capture never replays",
              decideJoin(in).after == AfterJoin::Finish);
    }
    {
        ShotJoinInputs in;
        in.analysis        = StageOutcome::Failed;
        in.mediaExport     = StageOutcome::Failed;
        in.hasReplayTracks = false;
        check("R5 nothing captured -> no transient",
              decideJoin(in).after == AfterJoin::Finish);
    }

    // Whatever the combination, a settled join always resolves to exactly one
    // continuation — there is no state in which the shot neither finishes nor
    // replays, because both roads end at finishShot() and the buffer's resume.
    const StageOutcome settled[] = { StageOutcome::Succeeded, StageOutcome::Failed,
                                     StageOutcome::Skipped };
    bool allResolve = true;
    for (StageOutcome a : settled)
        for (StageOutcome e : settled)
            for (int doc = 0; doc < 2; ++doc)
                for (int replay = 0; replay < 2; ++replay)
                    for (int tracks = 0; tracks < 2; ++tracks) {
                        ShotJoinInputs in;
                        in.analysis          = a;
                        in.mediaExport       = e;
                        in.documentWritten   = doc != 0;
                        in.autoReplay        = replay != 0;
                        in.hasReplayTracks   = tracks != 0;
                        const ShotJoinDecision d = decideJoin(in);
                        if (!d.ready) allResolve = false;
                    }
    check("R6 every settled combination reaches a continuation", allResolve);
}

int main()
{
    std::printf("shot_outcome_test — ShotProcessor launch/join decisions\n");
    testLaunchReachability();
    testJoinGate();
    testTerminalStatement();
    testPersistPath();
    testReviewableAndReplay();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
