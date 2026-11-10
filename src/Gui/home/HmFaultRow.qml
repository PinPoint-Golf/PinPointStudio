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

// A FAULT ROW on Home: a PpExpandRow that opens a fault in place (HmFaultDetail), one at a time.
// Used by Coaching's focus ("Right now"), "Next on your list", and an open area's items on Your
// swing; the header cells are the host's (the default property, as PpExpandRow's).
//
//   controller     the work-ons controller: the row opens to its faultDetail(conditionId)
//   conditionId    the fault this row is
//   openId         the host's open fault ("" when none); the row is open while it names this one.
//                  A press asks for the change with openRequested(id) — the fault's id to open it,
//                  "" to close it — and the host sets its openId, so one row is open at a time
//   faultDetail    the condition's detail, or null when no session measured it (no detail: the row
//                  does not open — no chevron, no press, no hand cursor)
//   showOutcomeContrast  passed to HmFaultDetail: draw the outcome filter's contrast line at the top of the
//                  opened detail. False by default, because Coaching's rows (focus "Right now", Next on your
//                  list) carry the line themselves; only Your swing's area items (HmAreaRow) set it true
//
// The press area is "faultPress" (for a probe to find and click). The detail sits under the words
// column (columnX(1)), which a host with its own template can move with detailIndent.

import QtQuick
import PinPointStudio

PpExpandRow {
    id: faultRow

    property var    controller:  null
    property string conditionId: ""
    property string openId:      ""
    property bool   showOutcomeContrast: false

    signal openRequested(string nextOpenId)
    // The open fault's "Review the … session →".
    signal reviewSessionRequested(string sessionDir)

    // A condition's detail (WorkOnsController.faultDetail: any condition a session measured, a
    // listed work-on or not), or null. Reads detailsRevision so the binding follows each publish.
    function faultFor(conditionId) {
        const c = faultRow.controller
        if (!c || !conditionId || typeof c.faultDetail !== "function") return null
        if (c.detailsRevision < 0) return null
        const d = c.faultDetail(conditionId)
        return d && d.id ? d : null
    }
    readonly property var faultDetail: faultRow.faultFor(faultRow.conditionId)

    chevronSide: "right"
    expanded:    faultRow.openId !== "" && faultRow.openId === faultRow.conditionId
    autoToggle:  false
    onToggled:   faultRow.openRequested(faultRow.expanded ? "" : faultRow.conditionId)
    pressObjectName:  "faultPress"
    detailIndent:     faultRow.columnX(1) + faultRow.padH
    detailRightInset: faultRow.padH
    detail: faultRow.faultDetail ? faultRow._detailComponent : null

    // Held as a property, not a child: a child would land in the header's cells.
    readonly property Component _detailComponent: Component {
        HmFaultDetail {
            item: faultRow.faultDetail
            showOutcomeContrast: faultRow.showOutcomeContrast
            onReviewSessionRequested: (dir) => faultRow.reviewSessionRequested(dir)
        }
    }
}
