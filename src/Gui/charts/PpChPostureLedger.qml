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
import QtQuick
import PinPointStudio

// PPCHPOSTURELEDGER — FINDINGS / WORKING WELL in This shot's Posture section (docs/design/posture_chart_design.md
// §5.2): the posture conditions that fired on this shot (the departures D1–D7, and the trail knee when it
// straightened), or, as Working well, the posture conditions graded clean whose reading sits inside its band. A
// row: the mark and its word (the session tier: pattern filled, watching a ring — a mark AND a word, never colour
// alone; working ink3), the name — a link that opens the condition in Session diagnostics when that panel is up —
// the positions, "Go to P7" (a seek), and the explanation (Working well: none — its name is its golferWell line; protect is ""). Every word is the
// presenter's FINDING; nothing is composed here. The ledger is PpChFindingsLedger (shared with the wrist), with its goTo
// part and a name that opens (canOpen).
// objectNames: chPostureFindings / chPostureWorkingWell (by `strengths`); each row chPostureFinding (+ findingId);
// its name chPostureFindingName, its seek chPostureGoTo.
PpChFindingsLedger {
    objectName: strengths ? "chPostureWorkingWell" : "chPostureFindings"
    prefix: "chPosture"
    goTo: true; protectWhenStrong: true; spacingU: 9
}
