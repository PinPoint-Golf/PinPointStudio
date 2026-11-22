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
// Until refactor 5.4b the new owner of a session's diagnostics (SessionDiagnostics) ran beside the old model, in memory
// only, and the two were compared: in the app until 3.10 (PINPOINT_DIAG_PARALLEL=1), and over a set of sessions by
// regrade_ledger --engine=old|new. 5.4b deleted the old engine; the form below is now the owner's alone, the parity
// dump's (regrade_ledger --dump, compared with tools/parity/parity_diff.py against a baseline such as step5-baseline).
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
// `doors()`'s in-memory wiring is the tests' and the tools' (diag_parallel_live_test, owner_harness.h).
//
// Adapters ring (src/Gui): reads the owner; writes nothing.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>


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

// The form of one session's owner. `withStamp`: the findings stamp under "stamp" (the dump keeps it). (Refactor 5.4b
// deleted the old model's form with the old engine.)
QJsonObject formOf(const SessionDiagnostics &owner, bool withStamp = true);   // empty before its first diagnosis

// Every difference between two forms: arrays of objects matched by their identity (shotId, conditionId, id, clubId),
// numbers within rtol/atol (whole numbers exact), everything else exact. One line per difference, "path: old → new".
QStringList differences(const QJsonValue &oldForm, const QJsonValue &newForm, double rtol = 1e-9, double atol = 1e-12);

// THE APP'S HOOK IS GONE (refactor 3.10; CARRY 3.7-e R1-m9): since 3.7-e no old model is left in the app beside an owner,
// so PINPOINT_DIAG_PARALLEL=1 compared nothing; install/uninstall and their test seams were deleted with the switch.
// Parity is regrade_ledger --dump against a baseline; diag_parallel_live_test compares a live owner's form with a stored one's.

} // namespace parallel
} // namespace pinpoint
