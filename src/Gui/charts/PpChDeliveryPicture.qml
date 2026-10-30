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

// PPCHDELIVERYPICTURE — THE CLUB DELIVERY PICTURE STRIP OF THIS SHOT (docs/design/club_delivery_chart_design.md §9 stage 6;
// the twin of PpChPosturePicture): the golfer's own down-the-line frames at address, the top and P6, with the address line
// (solid, from the ball up the shaft), the shoulder plane (dashed, from the ball through the trail shoulder) and the slot
// between them (a faint wash), all FROZEN at address and drawn the same on every frame. ChartsDeliveryPresenter's
// thisShot.picture, placed (PpChPostureFrame per frame, its "poly" mark the slot); a press on a frame seeks the replay
// there (Analyse; the glance draws it with no affordance). The lines are the section's own reading — the plot's two
// hairlines — so they are drawn whatever the reader's Coach lines toggles are (the picture's defaultSets). Nothing at
// all with no down-the-line camera (no groups, nothing said).
// objectNames: chDeliveryPicture, chDeliveryPictureFrame (×3: tag, seekUs, coachDrawn, polyDrawn, hasImage),
// chDeliveryPictureLegend, chDeliveryPictureWhy.
Column {
    id: dp
    objectName: "chDeliveryPicture"
    property QtObject roles: null
    property var picture: null
    property bool interactive: true
    property bool glance: false
    signal seek(var us)
    readonly property var ty: roles
    readonly property bool on: !!roles && !!picture && ((picture.groups || []).length > 0 || (picture.why || []).length > 0)
    readonly property var groups: on ? (picture.groups || []) : []
    readonly property var why: on ? (picture.why || []) : []
    readonly property var sets: on && picture.defaultSets ? picture.defaultSets : ({})
    // the row of frames fills the width, as tall as that allows up to a cap (layout only: the frames' sizes and every
    // mark are the presenter's)
    readonly property real frameCap: on ? ty.u(glance ? 220 : 340) : 0
    function rowH(frames) {
        if (!on) return 0
        let a = 0
        for (let i = 0; i < frames.length; ++i) a += frames[i].height > 0 ? frames[i].width / frames[i].height : 0.6
        const gaps = ty.u(10) * Math.max(0, frames.length - 1)
        return a > 0 ? Math.min(frameCap, (width - gaps) / a) : frameCap
    }
    visible: on
    spacing: on ? ty.u(8) : 0

    Text { visible: dp.on; text: "Address, the top and P6"; font: dp.on ? dp.ty.fSection : Qt.font({}); color: dp.on ? dp.ty.ink : "black" }
    Repeater {
        model: dp.groups
        delegate: Column {
            id: grp
            readonly property var frames: modelData.frames || []
            readonly property real rowH: dp.rowH(frames)
            width: dp.width
            spacing: dp.on ? dp.ty.u(4) : 0
            Text { text: modelData.label || ""; font: dp.on ? dp.ty.fKicker : Qt.font({}); color: dp.on ? dp.ty.ink3 : "grey" }
            Flow {
                width: parent.width
                spacing: dp.on ? dp.ty.u(10) : 0
                Repeater {
                    model: grp.frames
                    delegate: PpChPostureFrame {
                        objectName: "chDeliveryPictureFrame"
                        frame: modelData
                        sets: dp.sets
                        frameH: grp.rowH
                        coachColor: Theme.colorGood
                        coachAlpha: 0.9
                        paper: Qt.darker(dp.ty.surface, 1.35)
                        captionColor: dp.ty.ink
                        noteColor: dp.ty.ink3
                        captionFont: dp.ty.fSmall
                        noteFont: dp.ty.fSmall
                        hueOf: function (k) { return dp.ty.hue(k) }
                        interactive: dp.interactive && !dp.glance
                        onActivated: (f) => dp.seek(f.seekUs)
                    }
                }
            }
        }
    }
    Text { objectName: "chDeliveryPictureLegend"; visible: dp.on && text !== ""; width: dp.on ? dp.ty.measureW(dp.width) : 0; wrapMode: Text.WordWrap
           text: dp.on ? (dp.picture.legend || "") : ""; font: dp.on ? dp.ty.fSmall : Qt.font({}); color: dp.on ? dp.ty.ink3 : "grey" }
    Repeater {
        model: dp.glance ? [] : dp.why
        delegate: Text { objectName: "chDeliveryPictureWhy"; width: dp.on ? dp.ty.measureW(dp.width) : 0; wrapMode: Text.WordWrap
                         text: modelData; font: dp.on ? dp.ty.fSmall : Qt.font({}); color: dp.on ? dp.ty.ink3 : "grey" }
    }
}
