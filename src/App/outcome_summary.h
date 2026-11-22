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

// THE CAROUSEL'S SHOT SUMMARY, WORKED OUT (refactor 4-b; Application ring). The two functions the composition root
// hands AppState as its shotOutcome and watchOutcomeSummary doors (app_state.h). They read the owners and keep nothing:
//   which shot   AppState's `outcome` (R-APP-7: the selection's on a pick, move or clear; the newest shot the session
//                on screen adds), passed in with whether it is the selection (if not, the band names it)
//   shown        only a launch-monitor shot (the kept findings', else — not landed yet — its document's device)
//   the words    each factor from the Shot's diagnostics (ShotDiagnostics → its Session's SessionDiagnostics) in
//                shot_factor_words.h's words and kinds; "-" until judged or when not read
//   carry        the launch monitor's carry as the owner's findings hold it (yards, one decimal, "yd"), so it lands
//                with the factors (4-b decision: no new field in the summary file)
//   refreshed    on the Session's shotAdded and shotTrashed, its owner's resultKept, shotUnread and batchSettled — a
//                re-analysis (EVT-02) or a correction re-fires the shot and lands as resultKept, in place
// Linked only where the owner is (the app; outcome_summary_test): AppState reaches it through its doors.

#include "app_state.h"

namespace pinpoint {

ShotOutcomeSummary outcomeSummaryOf(Session *session, const ShotRef &shot, bool isSelection);
void               watchOutcomeSummary(Session *session, QObject *context, std::function<void()> changed);

} // namespace pinpoint
