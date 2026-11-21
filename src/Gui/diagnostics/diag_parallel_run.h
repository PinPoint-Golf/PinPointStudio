/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE PARALLEL RUN (refactor step 3.5-ii; build/refactor/step-3/BRIEF.md §4.9; D14: DEC-179, D22: DEC-180).
//
// The new owner of a session's diagnostics (SessionDiagnostics) runs beside the old model (SessionDiagnosticsModel),
// IN MEMORY ONLY — it writes no file, so diagnostics.json and work_ons.json keep the old model as their one writer
// until the cut-over (3.7) — and the two are compared:
//   • in the app, with PINPOINT_DIAG_PARALLEL=1 (main.cpp installs the hook below): after each reduction the old
//     model settles, the new owner of the same session (app.sessionAt) is compared once it is idle over the same
//     shots, and ONE LINE PER SESSION goes to the app log ("[DiagParallel] <session> …"), written again only when
//     what it says changes;
//   • over a set of sessions, by regrade_ledger --engine=old|new, which dumps each engine in this same form for
//     tools/parity/parity_diff.py (run P).
//
// THE FORM ("P"): what both engines own, in ids and numbers — the stage, the pattern set, every condition's ledger
// figures, every shot's rows, its outcome's facts (its "because" joined from the session's reasons on the new side:
// J-14 (b)) and its three factors, the shot mix's counts, per club the factor mix's counts and carry today against the
// frozen usual, the reasons, the work-ons and evidence, the outcome evidence, the session's usuals. Left out: the
// panel's view state and every golfer-facing sentence (the cards' order, the coverage line, the shot's words), which
// are the thin adapter's at the cut-over (3.7; R-DER-9), and the club windows (D7), checked apart (run P case 7).
// The stamp (`stamp`) is in the dump and left out of the app's line: it differs on every session by design (run P
// case 1, R-STAMP-1).
//
// FROM 3.7-a the stage panel and the cast show the owner through its thin adapter; 3.7-d cut the report over and 3.7-e the
// catch-up, so NO OLD MODEL IS LEFT IN THE APP TO COMPARE: the app's hook (install) finds nothing, and run P's comparison is
// regrade_ledger --engine=old|new (until 3.10). `doors()`'s in-memory wiring is the tests' (diag_parallel_live_test).
//
// Adapters ring (src/Gui): reads the old model and the new owner; writes nothing but the log.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <functional>

class SessionDiagnosticsModel;

namespace pinpoint {

class RecordingRepository;
class RuleData;
class Session;
class SessionDiagnostics;
class SessionRepository;
class WorkPool;
class WorkReceiver;
struct DiagnosticsDoors;

// THE APP'S DIAGNOSTICS DOORS (appDiagnosticsDoors) moved to session_diagnostics_adapter.h with the thinning (refactor
// 3.7-e; review M9 of 3.7-a): the app's always-on wiring is not the parallel run's.

namespace parallel {

// THE PARALLEL RUN'S DOORS (refactor 3.7-0): the one place they are put together, so the app (main.cpp) and
// diag_parallel_live_test wire the new owner identically. IN MEMORY ONLY (§4.9): `persist` false (no diagnostics.json,
// no work_ons.json) and the firing reader built with writeSidecars=false — SidecarPolicy::ParseNoWrite: it PARSES the
// document whenever no current sidecar serves it (live, before the old model's End sweep writes any) and writes no
// sidecar and no file, so the old model stays the only writer of every file of the session (REVIEW-3.5-ii-1 item 5(a)).
// On `pool` with `receiver` (never synchronous: each read is a job on the session's lane, off the GUI thread).
// `ruleData` the process's. Every pointer and reference is borrowed and must outlive every Athlete given the doors.
DiagnosticsDoors doors(WorkPool *pool, WorkReceiver *receiver, RuleData *ruleData,
                       const SessionRepository &sessions, RecordingRepository &recordings);

// The form of one session, from each engine. `withStamp`: the findings stamp under "stamp" (the dump keeps it).
QJsonObject formOf(const SessionDiagnosticsModel &model, bool withStamp = true);
QJsonObject formOf(const SessionDiagnostics &owner, bool withStamp = true);   // empty before its first diagnosis

// Every difference between two forms: arrays of objects matched by their identity (shotId, conditionId, id, clubId),
// numbers within rtol/atol (whole numbers exact), everything else exact. One line per difference, "path: old → new".
QStringList differences(const QJsonValue &oldForm, const QJsonValue &newForm, double rtol = 1e-9, double atol = 1e-12);

// The app's switch (D14): installs the old model's hook. `sessionAt` is app.sessionAt (the folder's Session, loaded if
// it is not; null for a no-golfer session: N1); `loadedAt` answers only a Session already loaded. The catch-up's model
// (baseline authority "always") sweeps every ended session, so it is compared only for a session already loaded — the
// live one at its End (§4.9: the live session with the panel off) — and never loads one (Q-18). uninstall() before the
// objects the two reach are destroyed.
//
// WHEN IT COMPARES (refactor 3.7-0):
//   • never while the session is being recorded HERE (Session::runningHere() and in progress): the hook runs on the
//     GUI thread after every settle of the old model (two a shot), and a comparison builds both whole forms and every
//     difference — O(shots) a call, 11k–20k strings a call in D22's live run (3.7/D22-STALL-NOTES.md §4 A1). While
//     recorded here the hook does O(1) work and returns; the session is compared at its End (the catch-up's End
//     sweep, or the panel's next settle once it has ended). A session another process left in progress (a crash, a
//     shared library) is not being recorded here and is compared on every settle, as before;
//   • like with like on the club baseline: before comparing, the new owner re-reads club_baseline.json when it exists
//     (SessionDiagnostics::reloadClubBaseline(keepWhenAbsent)) — the old model freezes it at End, after the new owner
//     loaded — and, when that moved its numbers, the owner re-fires every shot and the comparison waits for that
//     diagnosis. An absent file changes nothing;
//   • a session skipped while recorded here that goes away uncompared (released, its athlete or library switched, the
//     app quit: uninstall) gets ONE line, "<session> not compared: the session was not ended" (or "… ended, but no
//     comparison landed …"), so a missing line can never be read as a pass. No comparison is made there.
// The comparison line itself is unchanged: one per session, written again only when what it says changes.
bool enabledByEnvironment();   // PINPOINT_DIAG_PARALLEL=1
void install(std::function<Session *(const QString &folder)> sessionAt,
             std::function<Session *(const QString &folder)> loadedAt);
void uninstall();

// TEST SEAMS (3.7-0 round 2): per session folder, the comparisons the hook ran, the lines it wrote, the last line.
int     comparisonsOf(const QString &sessionFolder);
int     linesOf(const QString &sessionFolder);
QString lastLineOf(const QString &sessionFolder);

} // namespace parallel
} // namespace pinpoint
