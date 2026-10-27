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
.pragma library

// The calibration routines' SPOKEN INSTRUCTIONS (EXPERIMENTAL, AppSettings.experimentalSpokenInstructions)
// — the ONE place each line's words live. The routine that says a line reads it from here
// (TrunkRoutine.spokenPhrases / _startLine / _turnBackLine), and its step descriptor lists the same array
// (SetupSteps, StepDescriptor.spokenPhrases) so the session-setup shell can have ttsController PREPARE
// them in advance, before the routine's page exists (Mark, 27 Oct 2026: "the phrases should be
// prepared in advance, ideally when the session setup is instantiated — makes no sense to do it at
// the last minute"). Prepared and spoken text must be identical, character for character: the
// controller's cache is keyed by the text.
//
// Instructions ONLY (Mark's rule): what the golfer must do next and cannot read at that moment —
// never a verdict, status, reading or failure.

// The trunk routine (TrunkRoutine.qml). The golfer turns AWAY from the screen, so the voice is the only
// way he knows when to start a turn and when to come back (Mark, 27 Oct: "I need to know when to start
// the turn and when to come back — there is no other way to know"):
//   the start lines — the moment a turn step is ARMED (still at address, the recording begun), every
//                     attempt;
//   the turn-back line — once per attempt, when he has turned ENOUGH (tuned::trunkRoutine::
//                     kTurnBackCueDeg) or has stopped at the top short of it.
var trunkStartBackswing     = qsTr("Start your backswing turn.")
var trunkStartFollowThrough = qsTr("Start your follow-through turn.")
var trunkTurnBack           = qsTr("Turn back to address.")
var trunk = [trunkStartBackswing, trunkStartFollowThrough, trunkTurnBack]
