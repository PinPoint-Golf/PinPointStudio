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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

// The list a Swing model enum opens: one row per option, the chosen one in the accent, one click
// picks. The inspector's field (ModelEnumField) and the table's enum cells (ModelTable) open the same
// list, so a tier or a strength is chosen the same way wherever it is changed.
//
// Not a PpComboBox popup: these rows are 28 px with the choice marked in the accent, where the combo's
// are 34 px with no mark, and the table's cell has no closed box of its own to hang a combo from.
//
// The caller places it (x, y) and decides when it is open; the list closes itself once a row is
// picked.
Popup {
    id: root

    // [{ value, label }] — the shape the façade hands both the table and the inspector.
    property var options: []
    property var value:   ""
    // Off: every row is as wide as the popup (the caller sets `width`, as the inspector field does to
    // sit flush under its box). On: the popup takes the width of its rows, each row as wide as its own
    // label needs and never narrower than 150 px — which is what a table cell, with no box to match,
    // wants.
    property bool fitContent: false

    signal picked(var value)

    padding: Theme.gap(4)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color:        Theme.colorSurface
        radius:       Theme.radius
        border.width: 1
        border.color: Theme.colorBorderStrong
    }

    contentItem: Column {
        spacing: 0
        Repeater {
            model: root.options || []
            delegate: Rectangle {
                id: option
                required property var modelData
                readonly property bool chosen: modelData.value === root.value

                width:  root.fitContent ? Math.max(Theme.sp(150), optionText.implicitWidth + Theme.sp(24))
                                        : root.availableWidth
                height: Theme.sp(28)
                radius: Theme.radius
                color:  optionHover.hovered ? Theme.colorBg2 : "transparent"

                HoverHandler { id: optionHover }

                Text {
                    id: optionText
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left:        parent.left
                    anchors.leftMargin:  Theme.gap(10)
                    // A fitted row is already as wide as its label, so only a fixed-width one needs
                    // to elide (and anchoring both edges of a fitted row would make its width depend
                    // on itself).
                    anchors.right:       root.fitContent ? undefined : parent.right
                    anchors.rightMargin: Theme.gap(10)
                    text: option.modelData.label
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color: option.chosen ? Theme.colorAccent : Theme.colorText
                    elide: Text.ElideRight
                }

                PpPressable {
                    hoverScale: 1.0
                    onClicked: { root.picked(option.modelData.value); root.close() }
                }
            }
        }
    }
}
