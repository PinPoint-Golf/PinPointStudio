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
import PinPointStudio

// PpSequenceStrip — the kinematic sequence TILE under the chart (2026-10-02), deliberately plain:
// four rows in the order the segments SHOULD peak, each with the place it ACTUALLY peaked, when,
// how fast, and that speed as a % of the tour-pro peak, each with its ±σ. It mirrors the plot:
// every peak marked on a curve has a row with numbers; an unplaced one is dimmed with a "?". The
// ACTUAL column carries the judgement as a mark beside the number — a tick in turn, a target out
// of turn, a dashed ring where the peak could not be pinned — so a wrong order is a shape and a
// colour, and nothing else needs reading. A first cut carried σ, timing windows, two order
// sentences and a cohort paragraph — Mark: "way too wordy and almost unreadable".
//
// Shown only under the "Kinematic sequence" preset. Every string is ChartMetrics.sequenceTable's
// (chart_metrics_test asserts them); this file binds and picks colours and marks.
//
// A SECTION OF THE CHART PANEL, NOT A CARD: it sits inside the panel's card, so it opens on a
// hairline and a Micro heading like the panel's other sections rather than drawing a box.
ColumnLayout {
    id: root
    objectName: "sequenceStrip"

    property var  kinematicSequence: null      // analysisDetail.kinematicSequence (may be null)
    property real impactUs: -1                 // carried for parity with the summary; the rows are already impact-relative
    // Folded, the tile is its title line alone — the caret, SEQUENCE and the verdict — so the
    // plot above takes the height back and the one answer the tile gives is still on screen.
    // The host owns the state (and persists it); the title line asks for the toggle.
    property bool collapsed: false
    signal toggled()
    // "Sequence checks: n of m" (trunk_imu_sequence_grf_design.md §10.6, E4): the shape
    // SessionDiagnosticsModel.sequenceChecks(shotId) publishes, read off the ENGINE's own signal
    // verdicts — never re-derived from kinematicSequence. null / {} draws no line.
    property var  checks: null

    spacing: Theme.gap(6)

    ChartMetrics { id: cm }

    component HeadText: PpMicro {}
    component CellText: Text {
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody
        color: Theme.colorText
    }

    readonly property var _ks:    root.kinematicSequence ? root.kinematicSequence : ({})
    readonly property var _table: cm.sequenceTable(root._ks)

    readonly property real _wLabel: Theme.sp(80)
    readonly property real _wRank:  Theme.sp(60)
    readonly property real _wPeak:  Theme.sp(120)
    readonly property real _wSpeed: Theme.sp(100)

    Item {
        objectName: "sequenceTile"
        Layout.fillWidth: true
        implicitHeight: tileCol.implicitHeight + Theme.sp(10)

        Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

        ColumnLayout {
            id: tileCol
            x: Theme.sp(6); y: Theme.sp(8)
            width: parent.width - Theme.sp(12)
            spacing: Theme.gap(6)

            // ── title + the verdict ────────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(7)
                Text {
                    objectName: "sequenceCaret"
                    text: "▸"; rotation: root.collapsed ? 0 : 90
                    color: Theme.colorText3; font.pixelSize: Theme.fontSzBody2
                    Behavior on rotation { enabled: !Theme.reduceMotion
                                           NumberAnimation { duration: Theme.durationFast } }
                }
                HeadText { objectName: "sequenceStripHeader"; text: Theme.caps(qsTr("Sequence")) }
                Item { Layout.fillWidth: true }
                Text {
                    objectName: "sequenceVerdict"
                    text: (root._table.verdictState === "match"    ? "✓  "
                         : root._table.verdictState === "mismatch" ? "✗  " : "")
                          + root._table.verdictText
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                    font.weight: Font.DemiBold
                    // colorWarn, not colorError: an order to work on is a fault, not an alarm.
                    color: root._table.verdictState === "match"    ? Theme.colorGood
                         : root._table.verdictState === "mismatch" ? Theme.colorWarn
                                                                   : Theme.colorText2
                }
            }

            RowLayout {
                visible: !root.collapsed
                Layout.fillWidth: true
                spacing: Theme.gap(12)
                HeadText { text: "";                 Layout.preferredWidth: root._wLabel }
                HeadText { text: Theme.caps(qsTr("Should"));     Layout.preferredWidth: root._wRank }
                HeadText { text: Theme.caps(qsTr("Actual"));     Layout.preferredWidth: root._wRank }
                HeadText { text: Theme.caps(qsTr("Peak"));       Layout.preferredWidth: root._wPeak }
                HeadText { text: qsTr("°/s");                    Layout.preferredWidth: root._wSpeed }   // a unit: never capitalised
                HeadText { text: Theme.caps(qsTr("% of pro"));   Layout.fillWidth: true }
            }

            Repeater {
                model: root._table.rows
                delegate: RowLayout {
                    id: seg
                    required property var modelData
                    objectName: "sequenceRow:" + seg.modelData.segment
                    visible: !root.collapsed
                    Layout.fillWidth: true
                    spacing: Theme.gap(12)
                    // A peak the producer could not pin down is shown — it is on the curve — but
                    // dimmed, and its rank carries a "?".
                    opacity: seg.modelData.placed || seg.modelData.actualRank === "—" ? 1.0 : 0.6
                    CellText {
                        text: seg.modelData.label
                        Layout.preferredWidth: root._wLabel
                        font.family: Theme.fontBody; font.weight: Font.Medium
                    }
                    CellText {
                        text: seg.modelData.shouldRank
                        Layout.preferredWidth: root._wRank
                        color: Theme.colorText3
                    }
                    // The rank and its mark: out of turn is a target in colorWarn, in turn a tick in
                    // colorGood, a peak the producer could not pin a dashed ring (its rank already
                    // carries the "?"), and no peak at all neither.
                    Row {
                        id: rankCell
                        Layout.preferredWidth: root._wRank
                        spacing: Theme.gap(6)
                        readonly property string mark: seg.modelData.actualRank === "—" ? ""
                                                      : seg.modelData.outOfTurn          ? "target"
                                                      : seg.modelData.placed             ? "check"
                                                      :                                    "unconfirmed"
                        CellText {
                            id: rankText
                            objectName: "sequenceActual:" + seg.modelData.segment
                            text: seg.modelData.actualRank
                            font.weight: Font.Bold
                            color: seg.modelData.outOfTurn          ? Theme.colorWarn
                                 : seg.modelData.actualRank !== "—"  ? Theme.colorGood : Theme.colorText3
                        }
                        PpBadge {
                            visible: rankCell.mark !== ""
                            anchors.verticalCenter: rankText.verticalCenter
                            size: Theme.sp(16)
                            kind: rankCell.mark !== "" ? rankCell.mark : "check"
                            tone: rankCell.mark === "target" ? Theme.colorWarn
                                : rankCell.mark === "check"  ? Theme.colorGood : Theme.colorText3
                        }
                    }
                    CellText {
                        objectName: "sequencePeaked:" + seg.modelData.segment
                        text: seg.modelData.peakText
                        Layout.preferredWidth: root._wPeak
                        color: seg.modelData.placed ? Theme.colorText : Theme.colorText3
                    }
                    CellText {
                        text: seg.modelData.speedText
                        Layout.preferredWidth: root._wSpeed
                    }
                    CellText {
                        objectName: "sequencePct:" + seg.modelData.segment
                        text: seg.modelData.pctText
                        Layout.fillWidth: true
                        font.weight: Font.DemiBold
                    }
                }
            }

            // ── Sequence checks: one quiet line, a mark per check; hover lists them, a tap pins ──
            Item {
                id: chk
                objectName: "sequenceChecks"
                readonly property var cs: root.checks && root.checks.checks ? root.checks : null
                readonly property var list: cs ? cs.checks : []
                property bool pinned: false
                readonly property bool open: pinned || chkHover.hovered
                // The label column fits the longest label, never less than the columns it lines up under.
                FontMetrics { id: chkFm; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro }
                readonly property real labelW: {
                    var w = root._wLabel + root._wRank + Theme.sp(40)
                    for (var i = 0; i < list.length; ++i)
                        w = Math.max(w, chkFm.advanceWidth(list[i].label) + Theme.sp(8))
                    return w
                }
                function ink(state) {
                    return state === "fail" ? Theme.colorWarn : state === "pass" ? Theme.colorGood
                         : state === "unsure" ? Theme.colorText2 : Theme.colorText3
                }
                visible: !root.collapsed && !!cs
                Layout.fillWidth: true
                implicitHeight: chkCol.implicitHeight + Theme.sp(4)
                HoverHandler { id: chkHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: chk.pinned = !chk.pinned }
                Column {
                    id: chkCol
                    y: Theme.sp(4)
                    width: parent.width
                    spacing: Theme.gap(3)
                    Item {
                        width: parent.width
                        height: chkLine.implicitHeight
                        // The marks' face, unseen, so the marks' row can sit on the line's baseline.
                        CellText { id: chkMarkProbe; visible: false; text: "✓"; font.weight: Font.Bold }
                        Row {
                            id: chkRow
                            spacing: Theme.gap(10)
                            CellText { id: chkLine; objectName: "sequenceChecksLine"
                                       text: chk.cs ? chk.cs.line : ""
                                       font.family: Theme.fontBody; font.weight: Font.Medium }
                            // A Row places x only, so each child's y puts its baseline on the line's.
                            Row {
                                objectName: "sequenceChecksMarks"
                                y: chkLine.baselineOffset - chkMarkProbe.baselineOffset
                                spacing: Theme.gap(4)
                                Repeater {
                                    model: chk.list
                                    delegate: CellText {
                                        required property var modelData
                                        objectName: "sequenceCheckMark:" + modelData.facet
                                        text: modelData.mark
                                        font.weight: Font.Bold
                                        color: chk.ink(modelData.state)
                                    }
                                }
                            }
                            Text {
                                id: chkGap
                                objectName: "sequenceChecksGap"
                                y: chkLine.baselineOffset - baselineOffset
                                visible: text !== ""
                                width: Math.max(0, Math.min(implicitWidth, chk.width - x - chkWhose.implicitWidth - Theme.sp(12)))
                                elide: Text.ElideRight
                                text: chk.cs ? (chk.cs.gapNote || "") : ""
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                                color: Theme.colorText3
                            }
                        }
                        Text {
                            id: chkWhose
                            anchors.right: parent.right
                            y: chkLine.baselineOffset - baselineOffset
                            visible: chk.width - chkGap.x > implicitWidth + Theme.sp(12)
                            text: chk.cs ? chk.cs.whose : ""
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                            color: Theme.colorText3
                        }
                    }
                    Repeater {
                        model: chk.open ? chk.list : []
                        delegate: Row {
                            required property var modelData
                            objectName: "sequenceCheck:" + modelData.facet
                            spacing: Theme.gap(8)
                            Text { width: Theme.sp(16); horizontalAlignment: Text.AlignHCenter
                                   text: modelData.mark; font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                                   font.weight: Font.Bold; color: chk.ink(modelData.state) }
                            Text { width: chk.labelW; text: modelData.label
                                   font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText }
                            Text { width: Math.max(0, chk.width - chk.labelW - Theme.sp(40))
                                   elide: Text.ElideRight; text: modelData.detail
                                   font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3 }
                        }
                    }
                }
            }

            HeadText {
                visible: !root.collapsed
                text: qsTr("Pro = tour average, Cheetham 2008")
                font.letterSpacing: 0
            }
        }

        // The whole title line toggles, edge to edge — the same hit area a section header has.
        Item {
            x: 0; y: 0; width: parent.width
            height: Theme.sp(8) + Theme.sp(24)
            PpPressable { objectName: "sequenceToggle"; hoverScale: 1.0; onClicked: root.toggled() }
        }
    }
}
