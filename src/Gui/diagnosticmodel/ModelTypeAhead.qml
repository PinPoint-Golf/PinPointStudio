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
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

// The Swing model's type-ahead: a caps title, one text field, a count line and the list it narrows.
// Click, type three characters, Enter — never a picker dialog and never a scroll through 140 rows.
//
// ModelPicker (every link) and ModelCausePicker (which cause, existing or new) are both this. What
// they say is theirs — every user-facing word is set by them, never here — and so is what a choice
// DOES: this only reports which row was taken (`chosen`). The candidates come from `candidateSource`,
// asked again on every keystroke so the filtering, and the legality, stay in C++.
//
// The knobs below that are only spacing exist because the two pickers were drawn separately and are
// kept exactly as they were; they are not meant to be varied further.
Popup {
    id: root

    property string title: ""
    // (searchText) -> [{ id, label, detail }]. A row may also carry `glyph` (drawn before the label,
    // in the accent when `glyphAccent`); any other fields ride along to `chosen` untouched.
    property var    candidateSource: null
    // What the list shows. The candidates as given, unless the picker adds rows of its own.
    property var    rows: root._candidates
    // The right-hand detail of a row. A function rather than a row field so a detail that depends on
    // the picker's own state can change without rebuilding the list under the highlight.
    property var    rowDetail: (row) => row.detail || ""

    property string placeholderText: ""
    property string countText: ""
    property color  borderColor: Theme.colorBorderStrong
    property real   countBottomGap:   0
    property real   listMaxHeight:    Theme.sp(240)
    property real   listMinHeight:    Theme.sp(28)
    property real   listBottomMargin: 0
    property real   detailWidthFraction: 0.4
    // Shown under the list, below a 6 px gap, while `footerVisible`.
    property alias  footerData:    footerColumn.data
    property bool   footerVisible: false

    // The field and the highlight, for a footer that moves them.
    readonly property alias searchText: searchField.text
    property alias currentIndex: listView.currentIndex
    function focusField() { searchField.forceActiveFocus() }

    // The row taken, whether by Enter or by a click. The popup closes straight after.
    signal chosen(var row)

    padding: 0
    modal:   false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: { searchField.text = ""; searchField.forceActiveFocus(); listView.currentIndex = 0 }

    background: PpPopoverCard { style: "plainTight"; borderColor: root.borderColor }

    readonly property var _candidates:
        candidateSource ? candidateSource(searchField.text) : []

    function _take() {
        var list = root.rows
        if (!list || list.length === 0) return
        var i = Math.max(0, Math.min(listView.currentIndex, list.length - 1))
        root.chosen(list[i])
        root.close()
    }

    contentItem: ColumnLayout {
        spacing: 0

        PpMicro {
            kind:               "body"
            Layout.fillWidth:   true
            Layout.leftMargin:  Theme.gap(12)
            Layout.rightMargin: Theme.gap(12)
            Layout.topMargin:   Theme.gap(10)
            text:               root.title
        }

        PpSearchField {
            id: searchField
            Layout.fillWidth:   true
            Layout.leftMargin:  Theme.gap(10)
            Layout.rightMargin: Theme.gap(10)
            Layout.topMargin:   Theme.gap(6)
            placeholderText: root.placeholderText

            // Enter takes the highlighted row. With three characters typed that is usually the only
            // one left, which is the whole budget: click, three characters, Enter.
            Keys.onReturnPressed: root._take()
            Keys.onEnterPressed:  root._take()
            Keys.onDownPressed:   listView.currentIndex = Math.min(listView.currentIndex + 1,
                                                                   listView.count - 1)
            Keys.onUpPressed:     listView.currentIndex = Math.max(listView.currentIndex - 1, 0)
            // Typing changes what the list holds, so the highlight goes back to the top rather than
            // staying on whichever row happens to be at that index now.
            onTextChanged: listView.currentIndex = 0
        }

        Text {
            Layout.fillWidth:    true
            Layout.leftMargin:   Theme.gap(12)
            Layout.rightMargin:  Theme.gap(12)
            Layout.topMargin:    Theme.gap(6)
            Layout.bottomMargin: root.countBottomGap
            text: root.countText
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
            wrapMode:       Text.WordWrap
        }

        ListView {
            id: listView
            Layout.fillWidth:       true
            Layout.preferredHeight: Math.min(root.listMaxHeight,
                                             Math.max(root.listMinHeight, count * Theme.sp(28)))
            Layout.bottomMargin:    root.listBottomMargin
            clip:  true
            model: root.rows
            currentIndex: 0

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: rowItem
                required property var modelData
                required property int index

                width:  listView.width
                height: Theme.sp(28)
                color: listView.currentIndex === index ? Theme.colorAccentLight
                     : rowHover.hovered                ? Theme.colorBg2
                                                       : "transparent"

                HoverHandler { id: rowHover }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin:  Theme.gap(12)
                    anchors.rightMargin: Theme.gap(12)
                    spacing: Theme.gap(8)

                    Text {
                        text:    rowItem.modelData.glyph || ""
                        visible: text.length > 0
                        font.family:    Theme.fontSymbol
                        font.pixelSize: Theme.fontSzBody2
                        color: rowItem.modelData.glyphAccent === true ? Theme.colorAccent
                                                                      : Theme.colorText3
                    }

                    Text {
                        Layout.fillWidth: true
                        text: rowItem.modelData.label
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Theme.fontBodyWeight
                        color:          Theme.colorText
                        elide:          Text.ElideRight
                    }

                    Text {
                        text:    root.rowDetail(rowItem.modelData)
                        visible: text.length > 0
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                        elide:          Text.ElideRight
                        Layout.maximumWidth: root.width * root.detailWidthFraction
                    }
                }

                PpPressable {
                    hoverScale: 1.0
                    onClicked: { listView.currentIndex = rowItem.index; root._take() }
                }
            }
        }

        ColumnLayout {
            id: footerColumn
            Layout.fillWidth: true
            Layout.topMargin: Theme.gap(6)
            visible: root.footerVisible
            spacing: Theme.gap(6)
        }
    }
}
