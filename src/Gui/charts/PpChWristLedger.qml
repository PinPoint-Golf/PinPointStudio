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

// PPCHWRISTLEDGER — FINDINGS / WORKING WELL in This shot's wrist section (W6, W7): the wrist-domain diagnostics conditions
// that fired on this shot (or read clean on every tail: a strength), in the presenter's order. A row: the mark
// and its word (the session tier: pattern filled, watching a ring — a mark AND a word, never colour alone; a strength ink3), the name,
// the points it cost, the positions (the first a seek link), the magnitude and the confidence pips, the
// explanation; "more" opens coaching (or protect), linked to and corroborated by IN PLACE. Every word is the
// presenter's FINDING; nothing is composed here. The ledger is PpChFindingsLedger (shared with Posture), with its points,
// pips and more parts.
// objectNames: chWristFindings / chWristStrengths (by `strengths`); each row chWristFinding (+ findingId).
PpChFindingsLedger {
    objectName: strengths ? "chWristStrengths" : "chWristFindings"
    prefix: "chWrist"
    points: true; pips: true; more: true
}
