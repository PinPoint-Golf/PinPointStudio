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

// A STILL — one frame of the golfer at a swing position, captioned (design §6). The JPEG is made by ReportStills,
// already cropped to the golfer (nobody else leaves the studio) with the body line drawn on it; `src` is its
// file:// URL. Here it is only framed: a portrait frame of fixed aspect, the picture filling it (centred crop).
// A still whose file is missing (a fixture away from the share, a still that failed) is an EMPTY FRAME of the same
// size, so the page lays out identically with or without it, and it is not counted as a photograph.
// `isPhoto` (a loaded picture) is what PpSessionReport reads to mark the page as one the PDF writer stores as JPEG.
Column {
    id: st
    property QtObject ro: null
    property string src: ""
    property string caption: ""
    property string value: ""
    property string note: ""
    property real w: 150
    property real aspect: 1.5                           // height ÷ width
    readonly property bool isPhoto: src !== "" && img.status === Image.Ready
    objectName: "reportStill"
    spacing: 5

    Rectangle {
        width: st.w; height: Math.round(st.w * st.aspect)
        color: img.status === Image.Ready ? "#111111" : "transparent"           // a picture's mat; else paper
        border.color: img.status === Image.Ready ? "transparent" : (st.ro ? st.ro.hair : "transparent")
        border.width: st.ro ? st.ro.hw : 0
        clip: true
        Image {
            id: img
            anchors.fill: parent
            source: st.src
            fillMode: Image.PreserveAspectCrop
            asynchronous: false; cache: false
            sourceSize.width: Math.round(st.w * 4)     // ≥ 300 dpi at the printed size
            smooth: true; mipmap: true
        }
    }
    // the caption, then the value beside it — or under it, when the two do not fit the still's width
    Flow {
        width: st.w; spacing: 6
        visible: st.caption !== "" || st.value !== ""
        Text { visible: text !== ""; width: Math.min(implicitWidth, st.w); text: st.caption; font: st.ro.fMicro; color: st.ro.ink2; wrapMode: Text.WordWrap }
        Text { visible: st.value !== ""; width: Math.min(implicitWidth, st.w); text: st.value; font: st.ro.fMicroM; color: st.ro.ink; wrapMode: Text.WordWrap }
    }
    Text { visible: st.note !== ""; width: st.w; text: st.note; font: st.ro.fFolio; color: st.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.2 }
}
