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

// SHOTRULER — the session's one shot axis (round 4, from Replay's ReplayShotRuler and the PpBarcode's
// "firings per shot" row): every shot in the order struck, each with how many conditions went outside
// their corridor on it. It answers "was this a worse swing than the others?" in one look, and it is a
// brushable, clickable shot axis: the picked shot is the pick column (ink figure on the 7 % wash), a
// hovered shot the fainter one. A click puts that shot in the carousel. Counts are ink2/ink3 figures,
// never warn (they are counts, not readings outside a corridor); "-" where no condition was measurable.
// Counted from `conditions` (default readout.conditions: EVERY condition on the shot, so a ruler under a
// subset still counts the whole shot). ANY LENGTH (D7): given availableWidth the pitch narrows to the
// figures' width, then the ruler wraps into further lines; shot numbers thin first, counts never.
Item {
    id: rl
    objectName: "sdShotRuler"
    property QtObject roles: null
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var readout: null
    property var conditions: (readout && readout.conditions) || []
    property real pitch: roles ? roles.u(19) : 19        // the pitch it WANTS
    property real availableWidth: 0                      // > 0: fit inside this width (narrow, then wrap)
    property real labelW: -1                             // < 0: as wide as its labels
    property bool showShots: true                        // the shot-number line
    property bool showCounts: true                       // the count line
    property string shotsLabel: "shot"
    property string countsLabel: "outside"
    property bool atom: true
    readonly property var keyKinds: []
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    readonly property int nShots: {
        let n = readout && readout.shotCount !== undefined ? readout.shotCount : 0
        for (let i = 0; i < (conditions || []).length; ++i) n = Math.max(n, (conditions[i].ticks || []).length)
        return n
    }
    readonly property var cells: {
        const out = []
        for (let s = 0; s < nShots; ++s) out.push({ sid: s + 1, n: 0, known: 0 })
        const cs = conditions || []
        for (let i = 0; i < cs.length; ++i) {
            const t = cs[i].ticks || []
            for (let s = 0; s < Math.min(t.length, nShots); ++s) {
                if (t[s].shotId !== undefined) out[s].sid = t[s].shotId
                if (t[s].state === "fired") out[s].n++
                if (t[s].state === "fired" || t[s].state === "clean") out[s].known++
            }
        }
        return out
    }
    FontMetrics { id: fmF; font: rl.roles ? rl.roles.fFigLabel : Qt.font({}) }
    // The labels are measured AS DRAWN, by two unseen Texts: FontMetrics.advanceWidth("outside") ignores the
    // label face's capitals and tracking (Vector sets labels in spaced capitals), which under-measured the
    // column and clipped "OUTSIDE" at the left edge on the Vector cast.
    Text { id: mShots; visible: false; text: rl.shotsLabel; font: rl.roles ? rl.roles.fLabel : Qt.font({}) }
    Text { id: mCounts; visible: false; text: rl.countsLabel; font: rl.roles ? rl.roles.fLabel : Qt.font({}) }
    readonly property real gapX: roles ? roles.u(8) : 8
    readonly property real autoLabelW: Math.ceil(Math.max(showShots ? mShots.implicitWidth : 0, showCounts ? mCounts.implicitWidth : 0)) + gapX
    readonly property real lw: labelW >= 0 ? labelW : autoLabelW
    // fmF.font is read so the figure measures follow a theme change (advanceWidth() is a call, untracked)
    readonly property real figW88: fmF.font.pixelSize > 0 ? fmF.advanceWidth("88") : 0
    readonly property real minPitch: Math.ceil(figW88) + (roles ? roles.u(3) : 3)
    readonly property real cellPitch: availableWidth > 0 && nShots > 0 ? Math.max(Math.min(pitch, minPitch), Math.min(pitch, (availableWidth - lw) / nShots)) : pitch
    readonly property int perRowMax: availableWidth > 0 ? Math.max(1, Math.min(nShots, Math.floor((availableWidth - lw + 0.01) / cellPitch))) : Math.max(1, nShots)
    readonly property int perRow: Math.max(1, Math.ceil(nShots / Math.ceil(Math.max(1, nShots) / perRowMax)))    // balanced lines
    readonly property int rowsN: Math.max(1, Math.ceil(nShots / perRow))
    readonly property int labelStep: roles ? roles.shotLabelStep(cellPitch, figW88) : 1
    readonly property real lineH: roles ? roles.u(13) : 13
    readonly property real bandH: (showShots ? lineH : 0) + (showShots && showCounts ? (roles ? roles.u(4) : 4) : 0) + (showCounts ? lineH : 0)
    readonly property real bandGap: roles ? roles.u(8) : 8
    readonly property real countsY: showShots ? lineH + (roles ? roles.u(4) : 4) : 0

    implicitWidth: lw + Math.min(nShots, perRow) * cellPitch
    implicitHeight: rowsN * bandH + (rowsN - 1) * bandGap
    width: implicitWidth; height: implicitHeight

    Repeater {
        model: rl.roles ? rl.rowsN : 0
        delegate: Item {
            required property int index
            y: index * (rl.bandH + rl.bandGap); width: rl.lw; height: rl.bandH
            Text { visible: rl.showShots; y: (rl.lineH - height) / 2; width: rl.lw - rl.gapX; horizontalAlignment: Text.AlignRight
                   text: rl.shotsLabel; font: rl.roles.fLabel; color: rl.roles.ink3 }
            Text { visible: rl.showCounts; y: rl.countsY + (rl.lineH - height) / 2; width: rl.lw - rl.gapX; horizontalAlignment: Text.AlignRight
                   text: rl.countsLabel; font: rl.roles.fLabel; color: rl.roles.ink3 }
        }
    }
    Repeater {
        model: rl.roles ? rl.cells : []
        delegate: Item {
            id: cell
            objectName: "sdRulerCell"
            required property var modelData
            required property int index
            readonly property int shotId: modelData.sid
            readonly property int count: modelData.known ? modelData.n : -1
            readonly property bool picked: rl.brush ? rl.brush.isPicked(modelData.sid) : false
            readonly property bool hovered: rl.brush ? rl.brush.isHovered(modelData.sid) : false
            readonly property bool labelled: rl.roles.axisLabelShown(index, rl.nShots, rl.labelStep, [Math.floor(index / rl.perRow) * rl.perRow, Math.min(rl.nShots, (Math.floor(index / rl.perRow) + 1) * rl.perRow) - 1,
                                                                                               rl.brush ? rl.cells.findIndex(c => rl.brush.isPicked(c.sid)) : -1, rl.brush ? rl.cells.findIndex(c => c.sid === rl.brush.hoveredShot) : -1])
            x: rl.lw + (index % rl.perRow) * rl.cellPitch
            y: Math.floor(index / rl.perRow) * (rl.bandH + rl.bandGap)
            width: rl.cellPitch; height: rl.bandH
            Rectangle { anchors.fill: parent; visible: cell.picked || cell.hovered
                        color: cell.picked ? rl.roles.pickWash : rl.roles.hoverWash }
            Text { visible: rl.showShots && cell.labelled; anchors.horizontalCenter: parent.horizontalCenter; y: (rl.lineH - height) / 2
                   text: String(cell.index + 1); font: rl.roles.fFigLabel
                   color: cell.picked || cell.hovered ? rl.roles.ink : rl.roles.ink3 }
            Text { visible: rl.showCounts; anchors.horizontalCenter: parent.horizontalCenter; y: rl.countsY + (rl.lineH - height) / 2
                   text: cell.count >= 0 ? String(cell.count) : "-"; font: rl.roles.fFigLabel
                   color: cell.picked || cell.hovered ? rl.roles.ink : rl.roles.ink2
                   opacity: rl.brush ? rl.brush.shotOpacity(cell.shotId, rl.roles.dim) : 1 }
            MouseArea {
                enabled: rl.interactive; visible: enabled
                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onContainsMouseChanged: { if (rl.brush) rl.brush.hoverShot(cell.shotId, containsMouse); rl.shotHovered(containsMouse ? cell.shotId : -1) }
                onClicked: rl.shotActivated(rl.brush ? rl.brush.dirOf(cell.shotId) : "", cell.shotId)
            }
        }
    }
}
