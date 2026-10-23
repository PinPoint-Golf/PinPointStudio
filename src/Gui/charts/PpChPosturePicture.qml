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

// PPCHPOSTUREPICTURE — THE PICTURE STRIP OF THIS SHOT (docs/design/posture_chart_design.md §5.4, stage 5): the golfer's
// own frames at address, the top and impact, down the line then face-on (each camera only where the swing has it), the
// address coach lines frozen on all three, the good-player bands at impact drawn as offsets of the golfer's own lines,
// and the golfer's own butt line at impact in ink. ChartsPosturePresenter's thisShot.picture, placed (PpChPostureFrame per
// frame); a press on a frame seeks the replay there (Analyse; the glance draws it with no affordance). The coach sets are
// the reader's Coach lines toggles when any is on, else the picture's own default sets (posture and head-and-hips):
// the strip is about posture, so it never goes blank because the replay's coach lines are off.
// objectNames: chPosturePicture, chPosturePictureGroup (+ camera), chPosturePictureFrame (×n), chPosturePictureLegend,
// chPosturePictureWhy.
Column {
    id: pp
    objectName: "chPosturePicture"
    property QtObject roles: null
    property var picture: null
    property bool interactive: true
    property bool glance: false
    signal seek(var us)
    readonly property var ty: roles
    readonly property bool on: !!roles && !!picture && ((picture.groups || []).length > 0 || (picture.why || []).length > 0)
    readonly property var groups: on ? (picture.groups || []) : []
    readonly property var why: on ? (picture.why || []) : []
    readonly property var sets: ViewLayout.coachLinesOn(SessionMode.mode) ? ViewLayout.coachSetsFor(SessionMode.mode)
                                                                          : (on && picture.defaultSets ? picture.defaultSets : ({}))
    // each camera's row of frames fills the width, as tall as that allows up to a cap (layout only: the frames'
    // sizes and every mark are the presenter's)
    readonly property real frameCap: on ? ty.u(glance ? 220 : 340) : 0
    function rowH(frames) {
        if (!on) return 0
        let a = 0
        for (let i = 0; i < frames.length; ++i) a += frames[i].height > 0 ? frames[i].width / frames[i].height : 0.6
        const gaps = ty.u(10) * Math.max(0, frames.length - 1)
        return a > 0 ? Math.min(frameCap, (width - gaps) / a) : frameCap
    }
    readonly property int frameCount: { let n = 0; for (let i = 0; i < groups.length; ++i) n += (groups[i].frames || []).length; return n }
    visible: on
    spacing: on ? ty.u(8) : 0

    Text { visible: pp.on; text: "Address, the top and impact"; font: pp.on ? pp.ty.fSection : Qt.font({}); color: pp.on ? pp.ty.ink : "black" }
    Repeater {
        model: pp.groups
        delegate: Column {
            id: grp
            objectName: "chPosturePictureGroup"
            readonly property string camera: modelData.camera || ""
            readonly property var frames: modelData.frames || []
            readonly property real rowH: pp.rowH(frames)
            width: pp.width
            spacing: pp.on ? pp.ty.u(4) : 0
            Text { text: modelData.label || ""; font: pp.on ? pp.ty.fKicker : Qt.font({}); color: pp.on ? pp.ty.ink3 : "grey" }
            Flow {
                width: parent.width
                spacing: pp.on ? pp.ty.u(10) : 0
                Repeater {
                    model: grp.frames
                    delegate: PpChPostureFrame {
                        frame: modelData
                        sets: pp.sets
                        frameH: grp.rowH
                        coachColor: Theme.colorGood
                        paper: Qt.darker(pp.ty.surface, 1.35)
                        captionColor: pp.ty.ink
                        noteColor: pp.ty.ink3
                        captionFont: pp.ty.fSmall
                        noteFont: pp.ty.fSmall
                        hueOf: function (k) { return pp.ty.hue(k) }
                        interactive: pp.interactive && !pp.glance
                        onActivated: (f) => pp.seek(f.seekUs)
                    }
                }
            }
        }
    }
    Text { objectName: "chPosturePictureLegend"; visible: pp.on && text !== ""; width: pp.on ? pp.ty.measureW(pp.width) : 0; wrapMode: Text.WordWrap
           text: pp.on ? (pp.picture.legend || "") : ""; font: pp.on ? pp.ty.fSmall : Qt.font({}); color: pp.on ? pp.ty.ink3 : "grey" }
    Repeater {
        model: pp.glance ? [] : pp.why
        delegate: Text { objectName: "chPosturePictureWhy"; width: pp.on ? pp.ty.measureW(pp.width) : 0; wrapMode: Text.WordWrap
                         text: modelData; font: pp.on ? pp.ty.fSmall : Qt.font({}); color: pp.on ? pp.ty.ink3 : "grey" }
    }
}
