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

// TRUSTFOCUS — where a click on a claim's mark goes (docs/design/trust_and_gates_design.md §7.0).
//
// THE PLACEMENT RULE. A reason sits where the claim is made, at the claim's weight (a quiet mark:
// "out of sight", "not judged"), and opens on demand. Its HOME is the Trust panel (PpTrustPanel), a
// stage panel like Charts or Session diagnostics. A surface asks `open(subject, swingDir)`:
//   • the Trust panel is on → it opens and scrolls to that reading, and the surface does nothing more
//     (`open` returns true);
//   • it is off → `open` returns false and the surface expands the same reading IN PLACE, under the claim
//     (PpTrustInline). A reason is never more than one click away, and never behind a setting.
pragma Singleton
import QtQuick
import PinPointStudio

QtObject {
    id: tf
    // the reading last asked for, and a counter so asking for the same one again still scrolls to it
    property string subject: ""
    property string swingDir: ""
    property int    nonce: 0
    readonly property bool panelOn: ViewLayout.isPanelOn(SessionMode.mode, "trust")
    function open(subject, swingDir) {
        if (!panelOn) return false
        tf.subject = subject || ""
        tf.swingDir = swingDir || ""
        tf.nonce += 1
        return true
    }
}
