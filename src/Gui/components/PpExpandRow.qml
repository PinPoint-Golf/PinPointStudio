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

// An expand row (the §2.5 visual language): a PpTableRow with a drawn chevron that turns 90° when
// the row opens, and a detail block revealed under it in place.
//
//   expanded      open or closed
//   toggled()     the user asked to flip it: a click, Space / Return / Enter with the row focused,
//                 or an assistive technology's press. With `autoToggle` (the default) the row flips
//                 `expanded` itself first; a caller that binds `expanded` (one row open at a time)
//                 sets autoToggle: false and answers toggled() itself, so its binding survives
//   cells         the header's cells, as PpTableRow's (the default property)
//   detail        a Component, loaded under the header when the row is created and shown while
//                 expanded; `detailIndent` / `detailRightInset` narrow it
//   chevronSide   "left" (in the row's left padding, the area table's) or "right" (at the row's
//                 right edge, the work-ons list's); `chevronCentreY` centres it on a line of the
//                 header's own rather than the header's middle
//
// Keyboard-operable: the row takes focus on Tab, shows a focus ring, and toggles on Space, Return
// or Enter. Accessible.Button, its expanded state exposed as checked (QML has no expanded state of
// its own) and in the description.
//
// The chevron is HmWorkOns' (where it was drawn inline): two strokes, round caps, colorText3 closed
// and colorText2 open.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: xrow

    property bool expanded:   false
    property bool autoToggle: true
    signal toggled()

    property Component detail:           null
    property real      detailIndent:     0
    property real      detailRightInset: 0

    property string chevronSide:    "left"
    property real   chevronCentreY: -1
    property string accessibleName: ""
    // A name for the press area, for a probe to find and click (HmWorkOns' "workOnPress").
    property string pressObjectName: ""

    // The header row.
    property real  rowHeight:    Theme.sp(xrow.secondLine ? Theme.rowH2 : Theme.rowH)
    property alias secondLine:   header.secondLine
    property alias padH:         header.padH
    property alias columnOffset: header.columnOffset
    property alias centreCells:  header.centreCells
    property alias topRule:      header.topRule
    property alias hoverBleed:   header.hoverBleed
    property alias card:         header.card
    property alias layout:       header.layout
    readonly property alias headerRow:  header
    readonly property alias detailItem: detailLoader.item
    readonly property bool  hovered:    press.containsMouse
    default property alias cells: header.cells

    function activate() {
        if (xrow.autoToggle) xrow.expanded = !xrow.expanded
        xrow.toggled()
    }

    width:          parent ? parent.width : 0
    implicitHeight: header.height + (xrow.expanded && detailLoader.item ? detailLoader.height : 0)
    height:         implicitHeight

    activeFocusOnTab: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (!event.isAutoRepeat) xrow.activate()
            event.accepted = true
        }
    }

    Accessible.role:        Accessible.Button
    Accessible.name:        xrow.accessibleName
    Accessible.description: xrow.expanded ? qsTr("expanded") : qsTr("collapsed")
    Accessible.focusable:   true
    Accessible.checkable:   true
    Accessible.checked:     xrow.expanded
    Accessible.onPressAction:  xrow.activate()
    Accessible.onToggleAction: xrow.activate()

    // Under the header, so a control in a header cell takes its own clicks; the rest fall through
    // to here.
    MouseArea {
        id: press
        objectName:   xrow.pressObjectName
        x: -header.hoverBleed
        width:  header.width + 2 * header.hoverBleed
        height: header.height
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:    xrow.activate()
    }

    PpTableRow {
        id: header
        width:  xrow.width
        height: xrow.rowHeight
        highlighted: press.containsMouse
        // The header rounds its hover as the whole row would: at the top when it is the card's
        // first row, at the bottom only while closed and last.
        roundTop:    header.card !== null && xrow.parent === header.card.bodyItem
                     && xrow.y < 0.5 && header.card.headerHeight === 0
        roundBottom: !xrow.expanded && header.card !== null && xrow.parent === header.card.bodyItem
                     && Math.abs(xrow.y + xrow.height - xrow.parent.height) < 0.5
    }

    // Open / closed.
    Shape {
        id: chevron
        readonly property int s: Theme.sp(10)
        width: s; height: s
        x: xrow.chevronSide === "right" ? xrow.width - header.padH - s : header.padH
        y: Math.round((xrow.chevronCentreY >= 0 ? xrow.chevronCentreY : header.height / 2) - s / 2)
        rotation: xrow.expanded ? 90 : 0
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: xrow.expanded ? Theme.colorText2 : Theme.colorText3
            strokeWidth: Math.max(1.2, Theme.sp(1.3)); fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            startX: chevron.s * 0.32; startY: chevron.s * 0.12
            PathLine { x: chevron.s * 0.70; y: chevron.s * 0.5 }
            PathLine { x: chevron.s * 0.32; y: chevron.s * 0.88 }
        }
    }

    Rectangle {     // the focus ring, round the header
        visible: xrow.activeFocus
        x: 1 - header.hoverBleed; y: 1
        width:  header.width + 2 * header.hoverBleed - 2
        height: header.height - 2
        radius: Theme.radius
        color:  "transparent"
        border.width: Math.max(1, Theme.sp(2))
        border.color: Theme.colorAccent
    }

    Loader {
        id: detailLoader
        x: xrow.detailIndent
        y: header.height
        width:   Math.max(0, xrow.width - xrow.detailIndent - xrow.detailRightInset)
        visible: xrow.expanded
        active:  xrow.detail !== null
        sourceComponent: xrow.detail
    }
}
