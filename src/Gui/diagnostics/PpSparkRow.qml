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

// SPARKROW — Replay's session line: one pattern per row — name · PpShotStrip · this shot's value · PpSparkline.
// The name wraps (never ellipsised) and the row grows with it. The PpSparkline is tier 2 (from compact up);
// below that the strip carries the run alone. Clicking the name → conditionActivated(id) (Replay: switch to
// Analyse at that condition); the strip and sparkline brush and pick shots. Its geometry is
// roles.sparkGeo(width, shots), which PpSparkHead shares, so a column head always sits over its column.
// Any session length (D7): the strip column is roles.stripWidthFor(n); past that the marks narrow, then wrap.
Item {
    id: sr
    objectName: "sdSparkRow"
    property QtObject roles: PpDiagRoles {}             // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property bool conditionsInteractive: true    // false: the condition names take no click and show no pointer (a
                                                 // persistent cast picks shots, never opens a condition); hover still brushes
    property var card: null
    property bool showSparkline: roles.shows(2, width)
    property bool atom: true
    readonly property var keyKinds: ["strip"]
    signal conditionActivated(string id)
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    readonly property var c: card || ({})
    readonly property string cardId: c.id || ""
    readonly property bool hot: brush && brush.hoveredCondition === c.id
    readonly property int shots: (c.ticks || []).length
    readonly property var geo: roles.sparkGeo(width, shots, showSparkline)
    // (kept for callers that read them)
    readonly property real valW: geo.valW
    readonly property real skW: geo.skW
    readonly property real stripW: geo.stripW
    readonly property real gap: geo.gap
    readonly property real nameW: geo.nameW
    readonly property real rowH: Math.max(roles.u(24), nm.implicitHeight + roles.u(8), strip.height + roles.u(10))
    implicitHeight: rowH
    height: implicitHeight
    opacity: brush && c.id ? brush.conditionOpacity(c.id, roles.dim) : 1
    Behavior on opacity { NumberAnimation { duration: sr.roles.normal } }

    PpPulseWash { anchors.fill: parent; roles: sr.roles; brush: sr.brush; conditionId: sr.cardId }
    Rectangle { anchors.fill: parent; visible: sr.hot; color: sr.roles.rowHover }
    Text {
        id: nm
        objectName: "sdSparkRowName"
        width: sr.geo.nameW; anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.WordWrap
        text: sr.c.name || ""; font: sr.hot ? sr.roles.fBodyM : sr.roles.fBody; color: sr.roles.ink
        MouseArea {
            enabled: sr.interactive; visible: enabled
            anchors.fill: parent; hoverEnabled: true; cursorShape: sr.conditionsInteractive ? Qt.PointingHandCursor : Qt.ArrowCursor
            onContainsMouseChanged: if (sr.brush && sr.c.id) sr.brush.hoverCondition(sr.c.id, containsMouse)
            onClicked: if (sr.conditionsInteractive) sr.conditionActivated(sr.c.id)
        }
    }
    PpShotStrip {
        id: strip
        x: sr.geo.stripX; anchors.verticalCenter: parent.verticalCenter
        roles: sr.roles; brush: sr.brush; medium: sr.medium; interactive: sr.interactive
        ticks: sr.c.ticks || []
        availableWidth: sr.geo.stripW
        onShotActivated: (d, s) => sr.shotActivated(d, s)
        onShotHovered: s => sr.shotHovered(s)
    }
    Text {
        objectName: "sdSparkRowShot"
        x: sr.geo.valX; width: sr.geo.valW; horizontalAlignment: Text.AlignRight
        anchors.verticalCenter: parent.verticalCenter
        text: sr.c.spread && sr.c.spread.currentNumber ? sr.c.spread.currentNumber : sr.roles.fig(sr.c.valueText)
        font: sr.roles.fFig; color: sr.c.thisShot === "fired" ? sr.roles.warn : sr.c.thisShot === "clean" ? sr.roles.ink : sr.roles.ink3
    }
    PpSparkline {
        visible: sr.geo.spark
        x: sr.geo.skX; width: sr.geo.skW; height: Math.min(sr.rowH - sr.roles.u(4), sr.roles.u(22))
        anchors.verticalCenter: parent.verticalCenter
        roles: sr.roles; brush: sr.brush; medium: sr.medium; interactive: sr.interactive
        card: sr.geo.spark ? sr.card : null
        onShotActivated: (d, s) => sr.shotActivated(d, s)
        onShotHovered: s => sr.shotHovered(s)
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: sr.roles.hw; color: sr.roles.hair }
}
