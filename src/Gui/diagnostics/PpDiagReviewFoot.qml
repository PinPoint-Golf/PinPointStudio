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

// PPDIAGREVIEWFOOT — the review note ("final session state · …") folded to the foot of a Session diagnostics
// view (F7, RULES 3): drawn there below medium, where the tab row has no room for it, or when the declared-miss
// chip takes the right of the tab row; never on the cast. One rule for the Analyse and Replay registers; each
// keeps its own type for it — Analyse a footnote (PpNotes), the Replay glance one small quiet line (`glance`).
// objectName: sdReviewNote (its `text` is the note as drawn).
Column {
    id: rf
    objectName: "sdReviewNote"
    property QtObject roles: PpTypeset {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var header: null                        // the register's header (reviewNote)
    property bool missShown: false                   // the declared-miss chip is in the tab row
    property bool cast: false
    property bool glance: false                      // the Replay register's small line, not Analyse's footnote
    readonly property string text: header ? roles.cap1(header.reviewNote) + "." : ""
    visible: !cast && !!header && String(header.reviewNote || "") !== "" && (!roles.shows("reviewNote", width) || missShown)
    PpNotes {
        visible: !rf.glance
        width: rf.width
        roles: rf.roles
        text: rf.text
    }
    Text {
        visible: rf.glance
        width: rf.width
        wrapMode: Text.WordWrap
        text: rf.text
        font: rf.roles.fSmall; color: rf.roles.ink3
    }
}
