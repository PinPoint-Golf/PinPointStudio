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

// PpShotActionBar — the scope-aware action header of the shot carousel. ONE
// surface for the two action scopes that survived the PpShotPanel removal:
//
//   · FOCUSED SHOT (the swing on the stage)  — Export · Re-analyse · Move to trash
//   · FILTERED SET ("N of M shown")          — folded in as the "all N shown ▾"
//                                              menu, or the whole bar in Capture
//
// Three states fall out of (summary.valid, filterActive):
//   1. focus + filter — identity left; this-shot actions; "all N shown ▾" control
//   2. focus, no filter — identity left; this-shot actions only
//   3. no focus, filter — set descriptor left; the actions act on the SET
// ...and a fourth OVERRIDES them while the carousel has swings picked (#56):
//   4. selection — "N selected" left; Move N to trash · Clear. The picked swings are the
//      scope, whatever is on the stage or filtered, so nothing else is offered beside them.
// ...and a fifth with none of them (refactor 4-b):
//   5. outcome — nothing focused, filtered or picked, and a launch-monitor shot's outcome to show (a shot that just
//      landed: app.state.outcome, R-APP-7): the shot summary alone, named "SWING #N ·", and no actions — there is
//      nothing for them to act on.
//
// THE SHOT SUMMARY (refactor 4-b; Mark, 14 Nov 2026): strike · direction · flight · carry of a launch-monitor shot,
// right of the identity, after a hairline. `outcome` is app.state.shotOutcome (AppState's ShotOutcomeSummary): every
// word and tone kind is worked out in C++ from the Shot's diagnostics; here a kind only picks its colour, the tendency
// flow's (HmTendencyFlow.nodeTone): a fault colorWarn, a value to watch the watch tone, a good value colorGood, "-"
// colorText3 — always with its word. It names its shot only when that is not the selection (the identity already does).
//
// Re-analyse appears both as the focused-shot button (reanalyseShot) and, in the
// "all shown" menu, as a set-scope action (reanalyseShown) for bulk corpus re-runs.
//
// One row when it fits; on a window too narrow for it the actions stack under the left-hand
// words on a second row (`stacked`), right-aligned, and the bar is two bands tall. No control
// is ever dropped or hidden for width.
//
// Host-injected: the bar owns no model lookups. Pure bindings — handlers only
// forward a signal or open/close the menu; no computation lives here.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // ── Public API (host-injected) ──────────────────────────────────────────
    // Focused-shot metadata from ShotListModel.shotSummary(); { valid:false } when none.
    property var    summary:      ({})
    property bool   filterActive: false
    // The filtered counts for "N of M shown" and the menu tails.
    property int    visibleCount: 0
    property int    sourceCount:  0
    // Human filter descriptor (e.g. "50–100 · 4★"), "" if none.
    property string filterLabel:  ""
    // Swings picked on the cards (the carousel's selection); 0 = none, and states 1–3 apply.
    property int    markedCount:  0
    // The shot summary: app.state.shotOutcome ({ shown, selected, swingNumber, strikeText, strikeTone, … }); shown
    // false = no launch-monitor shot to summarise, and the bar is as it was.
    property var    outcome:      ({ shown: false })

    // Focused-shot actions.
    signal exportShot()
    signal reanalyseShot()
    signal trashShot()
    // The focused identity chip was clicked — the host opens the swing-edit popover.
    signal editRequested()
    // Filtered-set actions ("all shown") — emitted from the overflow menu.
    signal exportShown()
    signal reanalyseShown()
    signal trashShown()
    // Selection actions (state 4).
    signal trashMarked()
    signal clearMarks()

    // ── Derived scope state (pure bindings) ─────────────────────────────────
    readonly property bool _selecting: root.markedCount > 0
    readonly property bool _hasFocus:  !root._selecting && root.summary && root.summary.valid === true
    readonly property bool _hasFilter: root.filterActive
    readonly property bool _showAllCtl: root._hasFocus && root._hasFilter
    readonly property bool _hasOutcome: !root._selecting && !!root.outcome && root.outcome.shown === true
    readonly property bool _outcomeOnly: root._hasOutcome && !root._hasFocus && !root._hasFilter   // state 5

    // One label and its value in the summary, in the identity's voice: a Micro label, then the value in the data face,
    // coloured by the kind C++ gave it (above); "-" is never coloured.
    component SummaryPair: Row {
        id: pair
        property string label: ""
        property string text:  "-"
        property string tone:  ""
        spacing: Theme.gap(7)
        PpMicro {
            anchors.verticalCenter: parent.verticalCenter
            text: Theme.caps(pair.label)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text:           pair.text
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            color: pair.text === "-"     ? Theme.colorText3
                 : pair.tone === "fault" ? Theme.colorWarn
                 : pair.tone === "watch" ? Theme.toneColor("watch")
                 : pair.tone === "good"  ? Theme.colorGood
                 :                         Theme.colorText
        }
    }

    // One row when it fits: what the bar is about on the left, the actions at the right cap. When the
    // window is too narrow for both (the first-launch 1120 px at a large font scale and Spacious), the
    // actions stack under it on a second row, still right-aligned, so every control stays on screen.
    // And when even the identity and the shot summary do not fit side by side (a named shot with the
    // longest words, at 1120 px: refactor 4-b's review), the summary takes a row of its own under the
    // identity — three rows within the band's own height, nothing cut.
    // Every test compares the PARTS' implicit widths with the bar's own width — never `details`'
    // implicitWidth, which changes with its columns — so none feeds back into itself; the host gives the
    // bar its implicitHeight.
    readonly property real _bandH: Theme.sp(36)
    readonly property real _leftW:    leftGroup.visible ? leftGroup.implicitWidth : 0
    // The summary's words, plus the hairline beside the identity (1 px and the group's spacing) — from summaryRow, not
    // summaryGroup: the hairline and the margin change with summaryOwnRow, and must not feed back into it.
    readonly property real _summaryW: summaryGroup.visible
                                      ? summaryRow.implicitWidth
                                        + ((root._hasFocus || root._hasFilter) ? summaryGroup.spacing + 1 : 0)
                                        + (leftGroup.visible ? details.columnSpacing : 0) : 0
    readonly property real _actionsW: actions.visible ? actions.implicitWidth : 0
    readonly property real _oneRowWidth: _leftW + _summaryW + 2 * grid.columnSpacing + _actionsW
    readonly property bool stacked: width > 0 && _oneRowWidth > width
    readonly property bool summaryOwnRow: stacked && leftGroup.visible && summaryGroup.visible && _leftW + _summaryW > width
    readonly property int  _rows: (stacked ? 2 : 1) + (summaryOwnRow ? 1 : 0)
    implicitHeight: _rows * _bandH + (_rows - 1) * grid.rowSpacing

    GridLayout {
        id: grid
        anchors.fill: parent
        columns: root.stacked ? 1 : 3
        columnSpacing: Theme.gap(9)
        rowSpacing: Theme.gap(4)

        // What the bar is about: the identity (or the set or the selection), and beside it — or, when the two do
        // not fit side by side, under it (summaryOwnRow) — the shot summary.
        GridLayout {
            id: details
            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            columns: root.summaryOwnRow ? 1 : 2
            columnSpacing: Theme.gap(9)
            rowSpacing: Theme.gap(4)

            RowLayout {
                id: leftGroup
                objectName: "barLeft"
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                Layout.minimumHeight: root._bandH
                visible: !root._outcomeOnly            // state 5: the summary alone, no empty cell a band high
                spacing: Theme.gap(9)

                // ── LEFT · focused identity  (#N · club · time · ★) ─────────
                //    One clickable chip: hover-lifts and reveals an ✎ hint; a click opens
                //    the swing-edit popover (club / rating / note). The inner stars stay
                //    non-interactive so the whole chip is a single hit target.
                Rectangle {
                    id: identityChip
                    Layout.alignment: Qt.AlignVCenter
                    visible: root._hasFocus
                    implicitWidth:  identityRow.implicitWidth + Theme.sp(16)
                    implicitHeight: Theme.sp(28)
                    radius: Theme.radius
                    color:  idMa.containsMouse ? Theme.colorBg3 : "transparent"
                    border.width: 1
                    border.color: idMa.containsMouse ? Theme.colorBorderMid : "transparent"
                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    Row {
                        id: identityRow
                        anchors { left: parent.left; leftMargin: Theme.gap(8)
                                  verticalCenter: parent.verticalCenter }
                        spacing: Theme.gap(7)

                        // What the bar is about, in the dock's Micro voice — the chips row under it
                        // names its SESSION and FILTER the same way.
                        PpMicro {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Theme.caps(qsTr("Swing"))
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text:           "#" + (root.summary.ordinal !== undefined ? root.summary.ordinal : "")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText3
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text:           ClubFormat.display(root.summary.club)
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText3
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text:           root.summary.timestampLabel !== undefined ? root.summary.timestampLabel : ""
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText2
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText3
                        }
                        PpStarRating {
                            anchors.verticalCenter: parent.verticalCenter
                            value:       root.summary.rating !== undefined ? root.summary.rating : 0
                            interactive: false
                            starSize:    Theme.fontSzBody2
                        }
                        // No score pill after the stars (#186): the score is retired from view
                        // until the shot score is built, so the chip ends at the rating.
                        Text {   // edit hint — space reserved (opacity, not visibility) so hover never reflows
                            anchors.verticalCenter: parent.verticalCenter
                            text:           "✎"
                            font.family:    Theme.fontSymbol
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText3
                            opacity:        idMa.containsMouse ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                        }
                    }

                    PpPressable {
                        id: idMa
                        onClicked: root.editRequested()
                    }
                }

                // ── LEFT · selection descriptor  (N selected) — state 4 ─────────────
                Row {
                    id: markRow
                    objectName: "markRow"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: Theme.gap(8)
                    visible: root._selecting
                    spacing: Theme.gap(7)

                    PpMicro {
                        anchors.verticalCenter: parent.verticalCenter
                        text:  Theme.caps(qsTr("Selected"))
                        color: Theme.colorAccent
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           Theme.plural(root.markedCount, qsTr("%1 swing"), qsTr("%1 swings"))
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           qsTr("click swings to add or remove")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText3
                    }
                }

                // ── LEFT · set descriptor  (N of M shown · filter) ──────────────────
                Row {
                    id: setRow
                    Layout.alignment: Qt.AlignVCenter
                    visible: !root._hasFocus && !root._selecting
                    spacing: Theme.gap(7)

                    PpMicro {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Theme.caps(qsTr("Filtered"))
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           qsTr("%1 of %2 shown").arg(root.visibleCount).arg(root.sourceCount)
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.filterLabel !== ""
                        text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible:        root.filterLabel !== ""
                        text:           root.filterLabel
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText2
                    }
                }
            }

            // ── the shot summary (refactor 4-b): strike · direction · flight · carry ──
            RowLayout {
                id: summaryGroup
                objectName: "barSummary"
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                Layout.minimumHeight: root._bandH
                visible: root._hasOutcome
                spacing: Theme.gap(9)

                PpDivider {
                    orientation: Qt.Vertical
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: Theme.sp(20)
                    visible: (root._hasFocus || root._hasFilter) && !root.summaryOwnRow   // only after something, never to start a row
                }
                Row {
                    id: summaryRow
                    objectName: "shotSummary"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: (root._outcomeOnly || root.summaryOwnRow) ? Theme.gap(8) : 0   // under the identity's words
                    visible: root._hasOutcome
                    spacing: Theme.gap(14)

                    // Its shot, named when it is not the selection (the identity names that one).
                    Row {
                        objectName: "shotSummaryShot"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root._hasOutcome && root.outcome.selected !== true
                        spacing: Theme.gap(7)
                        PpMicro {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Theme.caps(qsTr("Swing"))
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text:           "#" + (root.outcome.swingNumber !== undefined ? root.outcome.swingNumber : "")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText3
                        }
                    }
                    SummaryPair { label: qsTr("Strike");    text: root.outcome.strikeText || "-";    tone: root.outcome.strikeTone || "" }
                    SummaryPair { label: qsTr("Direction"); text: root.outcome.directionText || "-"; tone: root.outcome.directionTone || "" }
                    SummaryPair { label: qsTr("Flight");    text: root.outcome.flightText || "-";    tone: root.outcome.flightTone || "" }
                    SummaryPair { label: qsTr("Carry");     text: root.outcome.carryText || "-" }
                }
            }
        }

        Item { Layout.fillWidth: true; visible: !root.stacked }   // grow — pushes the actions to the right cap

        RowLayout {
            id: actions
            objectName: "barActions"
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            Layout.minimumHeight: root._bandH
            visible: !root._outcomeOnly            // state 5: nothing to act on
            spacing: Theme.gap(9)

            // ── RIGHT · actions ─────────────────────────────────────────────────
            // State 4: the picked swings, and only them.
            PpButton {
                objectName: "trashMarkedButton"
                Layout.alignment: Qt.AlignVCenter
                visible: root._selecting
                destructive: true
                label: root.markedCount === 1 ? qsTr("Move to trash")
                                              : qsTr("Move %1 to trash").arg(root.markedCount)
                glyph: "🗑"
                onClicked: root.trashMarked()
            }
            PpButton {
                objectName: "clearMarksButton"
                Layout.alignment: Qt.AlignVCenter
                visible: root._selecting
                label: qsTr("Clear")
                glyph: "✕"
                onClicked: root.clearMarks()
            }

            // Export acts on the focused shot (state 1/2) or the whole set (state 3).
            PpButton {
                Layout.alignment: Qt.AlignVCenter
                visible: !root._selecting && !root._outcomeOnly
                label: qsTr("Export")
                glyph: "⤓"
                onClicked: root._hasFocus ? root.exportShot() : root.exportShown()
            }
            // Re-analyse — focused shot only (single-shot in v1); hidden in set scope.
            PpButton {
                Layout.alignment: Qt.AlignVCenter
                visible: root._hasFocus
                label: qsTr("Re-analyse")
                glyph: "↻"
                onClicked: root.reanalyseShot()
            }
            // Destructive — "Move to trash" (this shot) / "Move all shown to trash" (set).
            PpButton {
                Layout.alignment: Qt.AlignVCenter
                visible: !root._selecting && !root._outcomeOnly
                destructive: true
                label: root._hasFocus ? qsTr("Move to trash") : qsTr("Move all shown to trash")
                glyph: "🗑"
                onClicked: root._hasFocus ? root.trashShot() : root.trashShown()
            }

            // Hairline before the "all N shown" control (state 1 only).
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                visible: root._showAllCtl
                implicitWidth: 1
                implicitHeight: Theme.sp(20)
                color: Theme.colorBorderMid
                opacity: 0.7
            }

            // ── "all N shown ▾" — the relocated, re-scoped bulk menu (state 1) ───
            PpButton {
                id: allCtl
                Layout.alignment: Qt.AlignVCenter
                visible: root._showAllCtl
                style: "menu"; size: "compact"; mono: true
                active: allMenu.opened
                held: allMenu.opened       // stay grown while the menu is up
                chevron: true
                label: qsTr("all %1 shown").arg(root.visibleCount)
                onClicked: allMenu.opened ? allMenu.close() : allMenu.open()

                // Opens UPWARD over the stage (dock convention), right-aligned to the control, so it
                // grows leftward. As wide as its widest line (words and count, PpMenuItem's
                // contentImplicitWidth), never wider than the window less its margins; `margins` keeps
                // it inside the window when the control sits near an edge.
                Popup {
                    id: allMenu
                    objectName: "allMenu"
                    parent: allCtl
                    y: -height - Theme.gap(4)
                    x: allCtl.width - width
                    padding: Theme.gap(5)
                    margins: Theme.gap(8)
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    readonly property real _widestLine: {
                        let w = 0
                        for (let i = 0; i < allLines.children.length; ++i) {
                            const line = allLines.children[i]
                            if (line.contentImplicitWidth !== undefined) w = Math.max(w, line.contentImplicitWidth)
                        }
                        return w
                    }
                    readonly property real _maxContentWidth: allCtl.Window.width > 0
                        ? allCtl.Window.width - 2 * margins - leftPadding - rightPadding : Infinity
                    // a line is its content plus the row's own margins (PpMenuItem: gap(10) each side) and a little air
                    contentWidth: Math.max(0, Math.min(_widestLine + 2 * Theme.gap(10) + Theme.sp(8), _maxContentWidth))
                    background: PpPopoverCard {}

                    contentItem: Column {
                        id: allLines
                        PpMenuItem {   // Export all shown
                            glyph: "⤓"
                            text: qsTr("Export all shown")
                            tail: qsTr("%1 shots").arg(root.visibleCount)
                            fontWeight: Font.Normal
                            onTriggered: { allMenu.close(); root.exportShown() }
                        }

                        PpMenuItem {   // Re-analyse all shown — re-run the latest analysis
                            glyph: "↻"
                            text: qsTr("Re-analyse all shown")
                            tail: qsTr("%1 shots").arg(root.visibleCount)
                            fontWeight: Font.Normal
                            onTriggered: { allMenu.close(); root.reanalyseShown() }
                        }

                        PpMenuRule {}   // destructive action in its own group

                        PpMenuItem {   // Move all shown to trash (soft, recoverable)
                            glyph: "🗑"
                            text: qsTr("Move all shown to trash")
                            tail: qsTr("%1 shots").arg(root.visibleCount)
                            warn: true
                            fontWeight: Font.Normal
                            onTriggered: { allMenu.close(); root.trashShown() }
                        }
                    }
                }
            }
        }
    }
}
