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

// ABOUT THIS REPORT — the back page (design §3 page 9, §10 rule 7): the dates (§8.4), how we measured, what a
// target range is, what we could not check (said ONCE, here, in golfer words), the habits held back from today's
// list because their reading is in doubt (`held`, one line each), a short glossary, and the studio's sign-off.
// Every sentence is the map's; the labels are the page's headings. Each paragraph is an atom, so the page can run on.
Column {
    id: ab
    property QtObject ro: null
    property var rep: ({})
    property bool photos: true
    readonly property var a: rep.about || ({})
    readonly property var brand: rep.brand || ({})
    readonly property var held: rep.held || []
    width: 600

    FontMetrics { id: labFm; font: ab.ro.fLabel }
    readonly property real labelW: {
        let w = 0
        for (const s of ["Dates", "How we measured", "Target ranges", "What we could not check", "Left off the list today"]) w = Math.max(w, labFm.advanceWidth(s))
        return Math.min(14 * ro.em, w + 2.5 * ro.em)
    }

    component Para: Item {
        id: pa
        property QtObject ro: null
        property string label: ""
        property string text: ""
        property real labelW: 150
        property bool atom: true
        width: 300
        visible: text !== ""
        height: visible ? Math.max(pl.height, pt.height) + 14 : 0
        Text { id: pl; y: 2; width: pa.labelW - 12; text: pa.label; font: pa.ro.fLabel; color: pa.ro.ink; wrapMode: Text.WordWrap }
        Text { id: pt; x: pa.labelW; width: pa.width - x; text: pa.text; textFormat: Text.StyledText
               font: pa.ro.fBody; color: pa.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32 }
    }

    Column {
        objectName: "reportAbout"
        property bool atom: true
        property bool breakBefore: true
        property bool keepNext: true
        property string section: "About this report"
        width: ab.width
        Text { text: "About this report"; font: ab.ro.fKicker; color: ab.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: "How we measured, and how far to trust it"; font: ab.ro.fH1; color: ab.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 22 }
    }
    Para { ro: ab.ro; labelW: ab.labelW; width: ab.width; label: "Dates"; text: ab.a.dates || "" }
    Para { ro: ab.ro; labelW: ab.labelW; width: ab.width; label: "How we measured"; text: ab.a.measured || "" }
    Para { ro: ab.ro; labelW: ab.labelW; width: ab.width; label: "Target ranges"; text: ab.a.ranges || "" }
    Para { objectName: "reportNotChecked"; ro: ab.ro; labelW: ab.labelW; width: ab.width; label: "What we could not check"; text: ab.a.notChecked || "" }
    // reportHold conditions that showed: off every work-on list, named here with the reason (CONTRACTS §1)
    Para {
        objectName: "reportHeld"
        ro: ab.ro; labelW: ab.labelW; width: ab.width; label: "Left off the list today"
        text: ab.held.map(h => "<b>" + (h.name || "") + "</b>: " + (h.reason || "")).join("<br>")
    }

    // the glossary
    Item { width: 1; height: 12; visible: words.visible }
    Column {
        id: words
        objectName: "reportWords"
        readonly property var keyKinds: []
        property string printGroup: "words"
        visible: (ab.a.words || []).length > 0
        width: ab.width
        Item {
            property bool atom: true
            width: ab.width; height: wh.height + 12
            Rectangle { anchors.fill: parent; color: ab.ro.paper }
            Text { id: wh; text: "Words in this report"; font: ab.ro.fH3; color: ab.ro.ink }
            Rectangle { y: wh.height + 6; width: parent.width; height: ab.ro.hw; color: ab.ro.hair }
        }
        Repeater {
            model: ab.a.words || []
            delegate: Item {
                required property var modelData
                required property int index
                property bool atom: true
                property bool keepPrev: index === 1             // no orphan: the heading never stands with one entry
                width: ab.width; height: Math.max(wt.height, wd.height) + 12
                Text { id: wt; y: 6; width: ab.labelW - 12; text: modelData[0] || ""; font: ab.ro.fLabel; color: ab.ro.ink; wrapMode: Text.WordWrap }
                Text { id: wd; y: 5; x: ab.labelW; width: ab.width - ab.labelW; text: modelData[1] || ""; font: ab.ro.fItem; color: ab.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.3 }
            }
        }
    }

    // the studio's sign-off
    Item { width: 1; height: 32 }
    Rectangle {
        objectName: "reportSignOff"
        property bool atom: true
        width: ab.width; height: so.height + 40
        color: Qt.alpha(ab.ro.accent, 0.06); radius: 3
        Row {
            id: so
            x: 20; y: 20; spacing: 16
            PpReportCover.Mark { ro: ab.ro; brand: ab.brand; d: 48 }
            Column {
                spacing: 4; width: ab.width - 40 - 16 - 48 - (coach.visible ? coach.width + 16 : 0)
                Text { width: parent.width; text: ab.brand.studio || ""; font: ab.ro.fStudio; color: ab.ro.ink; wrapMode: Text.WordWrap }
                Text { visible: text !== ""; width: parent.width; text: ab.brand.coachLine || ""; font: ab.ro.fSmall; color: ab.ro.ink2; wrapMode: Text.WordWrap }
                Text { visible: text !== ""; width: parent.width; text: ab.brand.contact || ""; font: ab.ro.fSmall; color: ab.ro.ink2; wrapMode: Text.WordWrap }
                Item { width: 1; height: 4; visible: !!ab.brand.signoff }
                Text { visible: text !== ""; width: parent.width; text: ab.brand.signoff || ""; font: ab.ro.fBody; color: ab.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.3 }
            }
            // the coach's photo (optional, §7.2): a photograph, so this page is stored as JPEG
            Rectangle {
                id: coach
                readonly property bool isPhoto: visible && cimg.status === Image.Ready
                visible: !!ab.brand.coachPhoto
                width: 72; height: 72; radius: 3; clip: true; color: ab.ro.faint
                Image { id: cimg; anchors.fill: parent; source: ab.brand.coachPhoto ? ab.brand.coachPhoto + (ab.brand.stamp ? "?v=" + ab.brand.stamp : "") : ""; cache: false; fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 288; asynchronous: false; smooth: true; mipmap: true }
            }
        }
    }
    Item {
        property bool atom: true
        property bool keepPrev: true
        visible: !!ab.a.footnote
        width: ab.width; height: visible ? fn.height + 14 : 0
        Text { id: fn; y: 14; width: ab.width; text: ab.a.footnote || ""; font: ab.ro.fMicro; color: ab.ro.ink3; wrapMode: Text.WordWrap }
    }
}
