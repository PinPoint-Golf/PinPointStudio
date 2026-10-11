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

// SHOTSTRIP — the session strip, ONE drawing used everywhere (ledger rows, folded lines, Replay rows,
// and each row of the PpBarcode). A cell per shot in the order struck, THREE distinct marks (D5):
//   fired         = a warn bar (colour means "outside its corridor")
//   clean         = an ink3 hairline tick, shorter than the bar
//   not measured  = a short flat ink3 STUB on the baseline: present, never a gap
// The picked shot is the pick column (a faint ink wash behind its cell); hovered, a fainter one.
// Under a shot highlight, the other shots recede.
// ANY SESSION LENGTH (D7): ticks.length cells. Given `availableWidth` (> 0) the pitch narrows to fit, down
// to roles.stripMinPitch, and then the strip WRAPS onto further lines; it never clips and never drops a
// shot. Without it the strip is ticks.length × pitch wide, as before.
Item {
    id: st
    objectName: "sdShotStrip"
    property var roles
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property var ticks: []                       // [{ state, shotId, selected }]
    property real pitch: roles ? roles.stripPitch : 9          // the pitch it WANTS
    property real barH: roles ? roles.stripH : 11
    property real barW: roles ? roles.stripBar : 3
    property real availableWidth: 0              // > 0: fit (narrow, then wrap) inside this width
    property real rowGap: roles ? roles.u(4) : 4
    property bool pickBand: true                 // false when the parent draws a continuous column
    property real bandPad: 3
    property bool interactive: medium === "screen"
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    readonly property int count: (ticks || []).length
    readonly property var fit: roles ? roles.stripFit(count, availableWidth, pitch, barW, barH, rowGap)
                                     : ({ pitch: pitch, perRow: Math.max(1, count), rows: 1, barW: barW, width: count * pitch, height: barH })
    readonly property real cellPitch: fit.pitch          // the pitch actually drawn (read it to align a column)
    readonly property int rows: fit.rows
    readonly property int perRow: fit.perRow
    function cellX(i) { return (i % perRow) * cellPitch }
    function cellY(i) { return Math.floor(i / perRow) * (barH + rowGap) }

    implicitWidth: fit.width
    implicitHeight: fit.height
    width: implicitWidth; height: implicitHeight

    Repeater {
        model: st.roles ? (st.ticks || []) : []
        delegate: Item {
            id: cell
            objectName: "sdShotCell"
            required property var modelData
            required property int index
            readonly property int shotId: modelData.shotId !== undefined ? modelData.shotId : index + 1
            readonly property string cellState: modelData.state === "fired" || modelData.state === "clean" ? modelData.state : "notMeasured"
            readonly property bool picked: st.brush ? st.brush.isPicked(shotId) : !!modelData.selected
            readonly property bool hovered: st.brush ? st.brush.isHovered(shotId) : false
            x: st.cellX(index); y: st.cellY(index); width: st.cellPitch; height: st.barH
            Rectangle {
                visible: st.pickBand && (cell.picked || cell.hovered)
                y: -st.bandPad; width: parent.width; height: parent.height + 2 * st.bandPad
                color: cell.picked ? st.roles.pickWash : st.roles.hoverWash
            }
            Item {
                anchors.fill: parent
                opacity: st.brush ? st.brush.shotOpacity(cell.shotId, st.roles.dim) : 1
                Behavior on opacity { NumberAnimation { duration: st.roles.normal } }
                Rectangle {
                    visible: cell.cellState === "fired"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: st.fit.barW; height: parent.height; radius: st.roles.r.barRadius
                    color: st.roles.warn
                }
                Rectangle {
                    visible: cell.cellState === "clean"
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                    width: Math.max(1, st.roles.hw); height: Math.round(parent.height * 0.6)
                    color: st.roles.ink3
                }
                Rectangle {
                    objectName: "sdShotStub"
                    visible: cell.cellState === "notMeasured"
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                    width: Math.max(2, st.fit.barW); height: st.roles.stubH(st.barH)
                    color: st.roles.ink3
                }
            }
            MouseArea {
                enabled: st.interactive; visible: enabled
                anchors.fill: parent; anchors.topMargin: -st.bandPad; anchors.bottomMargin: -st.bandPad
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onContainsMouseChanged: {
                    if (st.brush) st.brush.hoverShot(cell.shotId, containsMouse)
                    st.shotHovered(containsMouse ? cell.shotId : -1)
                }
                onClicked: st.shotActivated(st.brush ? st.brush.dirOf(cell.shotId) : "", cell.shotId)
            }
        }
    }
}
