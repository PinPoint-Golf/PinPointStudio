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

// A DRILL CARD — what to do (design §3 work-on pages, §5): the first drill in full (what you do, what it is for,
// and if it doesn't work), and at most one more as a compact "Also" (§9.1 rule 4: 1 in full + 1 "Also"; the pack
// may hold more, they are not printed). One atom: a card is never cut. Its height is its text's.
// The words are the pack's (drills.json); the labels ("Drill", "What it is for.", …) are the card's headings.
Rectangle {
    id: dc
    property QtObject ro: null
    property var drill: ({})
    property var also: null                             // one further drill, or null
    property bool dense: false                          // the work-on page's density: the body a size down
    property bool atom: true
    objectName: "reportDrill"
    width: 600
    height: body.implicitHeight + 24
    color: ro ? ro.tint : "transparent"
    border.color: ro ? ro.edge : "transparent"; border.width: ro ? ro.hw : 0
    radius: 3

    Column {
        id: body
        x: 14; y: 12; width: parent.width - 28; spacing: 5
        Item {
            width: parent.width; height: Math.max(dl.height, dlab.height)
            Text { id: dl; y: 2; text: "Drill"; font: dc.ro.fSmallM; color: dc.ro.accent }
            Text {
                id: dlab
                x: dl.width + 8; width: parent.width - x
                textFormat: Text.RichText; wrapMode: Text.WordWrap
                text: "<span style=\"font-weight:600;\">" + (dc.drill.label || "") + "</span>"
                      + (dc.drill.equipment ? "&nbsp; <span style=\"font-size:10px; color:" + dc.ro.ink3 + ";\">· you need " + dc.drill.equipment + "</span>" : "")
                font: dc.dense ? dc.ro.fBodyD : dc.ro.fBody; color: dc.ro.ink
            }
        }
        Text { width: parent.width; text: dc.drill.instruction || ""; font: dc.dense ? dc.ro.fBodyD : dc.ro.fBody; color: dc.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32 }
        Text { visible: !!dc.drill.targets; width: parent.width; textFormat: Text.StyledText; wrapMode: Text.WordWrap; lineHeight: 1.3
               text: "<i>What it is for.</i> " + (dc.drill.targets || ""); font: dc.ro.fSmall; color: dc.ro.ink2 }
        Text { visible: !!dc.drill.note; width: parent.width; textFormat: Text.StyledText; wrapMode: Text.WordWrap; lineHeight: 1.3
               text: "<i>If it doesn’t work.</i> " + (dc.drill.note || ""); font: dc.ro.fSmall; color: dc.ro.ink3 }
        Item { width: 1; height: 2; visible: alsoText.visible }
        Text {
            id: alsoText
            visible: !!dc.also && !!dc.also.label
            width: parent.width; textFormat: Text.StyledText; wrapMode: Text.WordWrap; lineHeight: 1.3
            text: dc.also ? "<b>Also: " + (dc.also.label || "") + ".</b> " + (dc.also.instruction || "") : ""
            font: dc.ro.fSmall; color: dc.ro.ink
        }
    }
}
