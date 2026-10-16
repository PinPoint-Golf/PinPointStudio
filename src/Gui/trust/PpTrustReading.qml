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

// PPTRUSTREADING — ONE READING a gate touched, and every gate behind it (trust_readings.h READING), in the
// catalogue's words. The one block both homes use: a section of the Trust panel, and the in-place
// fallback under a claim (PpTrustInline). Typeset: the reading's label and its trust level on the head
// line with the figure the claim shows; under it, per gate, the title and what it compared — always
// shown — and, folded, why the gate exists, its threshold and which setups do not have it.
// `open` folds the whole reading; `highlight` washes it briefly when a click elsewhere brought you here.
// objectName: trustReading:<subject>; each gate trustGate:<gate>; each gate's fold trustWhy:<gate>.
Column {
    id: tr
    objectName: "trustReading:" + (reading ? reading.subject : "")
    property QtObject roles: null
    property var reading: null
    property bool open: true
    property bool foldable: true
    property bool highlight: false
    spacing: roles ? roles.u(4) : 4

    // the head line: ▾ Pelvis peak · Bounded · after −129 ms
    Item {
        width: tr.width
        height: headFlow.height + (tr.roles ? tr.roles.u(4) : 4)
        Rectangle {
            anchors.fill: parent; anchors.margins: tr.roles ? -tr.roles.u(3) : -3
            color: tr.roles ? tr.roles.pickWash : "transparent"
            opacity: tr.highlight ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 600 } }
        }
        Flow {
            id: headFlow
            width: parent.width
            spacing: tr.roles ? tr.roles.u(7) : 7
            Text { visible: tr.foldable; text: tr.open ? "▾" : "▸"; font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey"
                   height: lab.height; verticalAlignment: Text.AlignVCenter }
            Rectangle { visible: !!tr.reading && !!tr.reading.key; width: tr.roles ? tr.roles.u(7) : 7; height: width; radius: width / 2
                        color: tr.roles && tr.reading && tr.reading.key ? Theme.metricColor(tr.reading.key) : "transparent" }
            Text { id: lab; text: tr.reading ? tr.reading.label : ""; font: tr.roles ? tr.roles.fBodyM : Qt.font({}); color: tr.roles ? tr.roles.ink : "black" }
            Text { text: tr.reading ? tr.reading.levelWord : ""; font: tr.roles ? tr.roles.fSmallM : Qt.font({}); color: tr.roles ? tr.roles.ink2 : "grey"
                   height: lab.height; verticalAlignment: Text.AlignVCenter }
            Text { visible: !!tr.reading && tr.reading.figure && tr.reading.figure !== "-"
                   text: tr.reading ? tr.reading.figure : ""; font: tr.roles ? tr.roles.fFig : Qt.font({}); color: tr.roles ? tr.roles.ink2 : "grey"
                   height: lab.height; verticalAlignment: Text.AlignVCenter }
        }
        MouseArea { anchors.fill: parent; enabled: tr.foldable; cursorShape: Qt.PointingHandCursor; onClicked: tr.open = !tr.open }
    }
    Text {
        visible: tr.open && !!tr.reading && !!tr.reading.levelTip
        width: tr.roles ? tr.roles.measureW(tr.width) : tr.width; wrapMode: Text.WordWrap
        text: tr.reading ? tr.reading.levelTip : ""; font: tr.roles ? tr.roles.fSmallI : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey"
    }
    // what this reading kept from being judged (the diagnostics' findings, §4.4)
    Text {
        objectName: "trustBlocks:" + (tr.reading ? tr.reading.subject : "")
        visible: tr.open && !!tr.reading && (tr.reading.blocks || []).length > 0
        width: tr.roles ? tr.roles.measureW(tr.width) : tr.width; wrapMode: Text.WordWrap
        leftPadding: tr.roles ? tr.roles.u(14) : 14
        text: tr.reading ? "Not judged because of it: " + (tr.reading.blocks || []).join(", ") + "." : ""
        font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.ink2 : "grey"
    }
    Repeater {
        model: tr.open && tr.reading ? (tr.reading.gates || []) : []
        delegate: Column {
            id: g
            required property var modelData
            objectName: "trustGate:" + modelData.gate
            property bool why: false
            width: tr.width
            leftPadding: tr.roles ? tr.roles.u(14) : 14
            topPadding: tr.roles ? tr.roles.u(3) : 3
            spacing: tr.roles ? tr.roles.u(2) : 2
            readonly property real tw: tr.roles ? tr.roles.measureW(tr.width - leftPadding) : tr.width
            Row {
                spacing: tr.roles ? tr.roles.u(6) : 6
                Text { id: gt; text: g.modelData.title; font: tr.roles ? tr.roles.fSmallM : Qt.font({}); color: tr.roles ? tr.roles.ink : "black" }
                Text { text: g.modelData.effectWord; font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey"; anchors.baseline: gt.baseline }
            }
            Text { width: g.tw; wrapMode: Text.WordWrap; visible: text !== ""
                   text: g.modelData.evidence || ""; font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.ink2 : "grey" }
            Text {
                objectName: "trustWhy:" + g.modelData.gate
                text: g.why ? "Why this gate  ▾" : "Why this gate  ▸"
                font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.link : "grey"
                MouseArea { anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor; onClicked: g.why = !g.why }
            }
            Text { visible: g.why && text !== ""; width: g.tw; wrapMode: Text.WordWrap
                   text: g.modelData.explain || ""; font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey"
                   lineHeight: tr.roles ? tr.roles.r.smallLeading : 1 }
            Text { visible: g.why && text !== ""; width: g.tw; wrapMode: Text.WordWrap
                   text: [g.modelData.tuning || "", g.modelData.lift || ""].filter(function (s) { return s !== "" }).join("  ·  ")
                   font: tr.roles ? tr.roles.fSmall : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey" }
        }
    }
}
