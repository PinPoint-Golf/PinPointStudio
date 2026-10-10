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

// Wrist Motion diagnostics — the read-only Tier-1 surface for a Wrist session's Analyse view
// (design §8), shown as the stage's WRIST MOTION panel: PpModeStage draws the card and its title,
// so this lays out only the body. It is fed the focused swing's real analysis
// (shotReplay.analysisDetail) and tracks the replay playhead; the selected checkpoint comes from the
// transit timeline, so there is no in-panel scrubber. The position and its note beside the score
// pill, the model and compare controls, the archetype similarity, per-DOF trajectory strips (band
// corridor + player line + RAG markers + consequence poles), the collapsible position×phase grid,
// then FINDINGS and WORKING WELL side by side as two hairline-separated sections, and a legend in
// words. Pure binding.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    WristDiagnosticsModel {
        id: dx
        analysisDetail: shotReplay.analysisDetail
        playheadUs:     shotReplay.positionUs
        // The PREVIOUS swing is "the one before this in the list on screen", so it has to be
        // asked of the list on screen: the live carousel, or a loaded session's own shots
        // while reviewing. Asking the live model unconditionally meant every past session's
        // ghost came back empty — the replayed swing was not in that list to have a
        // predecessor in it.
        previousAnalysisDetail:  sessionReviewController.activeShots
                                     .previousAnalysisDetail(shotReplay.swingDir)
        // The REFERENCE is a benchmark, not a neighbour: it outlives the session it was
        // marked in, so the lookup falls through to the document on disk when no loaded
        // list holds it. Asked of the list on screen only because that is where a hit is
        // likeliest and costs no parse — the answer is the same from either.
        referenceAnalysisDetail: sessionReviewController.activeShots
                                     .analysisDetailForSwingDir(appSettings.wristReferenceSwingDir)
    }

    readonly property var _pos: dx.positions[dx.selectedPosition]

    // Right of the card's title: which position the panel is reading.
    readonly property string cardAside: dx.hasData && root._pos && root._pos.tag
                                        ? Theme.caps(qsTr("At %1")).arg(root._pos.tag) : ""

    function _ragColor(r) {
        return r === "green" ? Theme.colorRagGood
             : r === "amber" ? Theme.colorRagWatch
             : r === "red"   ? Theme.colorRagFault
             :                 Theme.colorRagNone
    }

    // ── Empty state (no analysed wrist swing): one quiet line, so the card keeps its shape ──
    PpCardNote {
        width: parent.width
        visible: !dx.hasData
        text: (shotReplay.active ? qsTr("No wrist metrics for this swing.")
                                 : qsTr("Select a swing to review."))
              + " " + qsTr("Wrist diagnostics show the analysed joint angles against the expected corridor.")
    }

    Flickable {
        id: flick
        anchors.fill: parent
        visible: dx.hasData
        contentHeight: body.height + Theme.sp(8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: body
            width: flick.width
            // Fill the panel vertically: when the content is shorter than the viewport the body
            // stretches to it (the strips absorb the slack, so they grow rather than the panel
            // hugging the top); when it is taller, it grows past and scrolls.
            height: Math.max(implicitHeight, flick.height - Theme.sp(8))
            spacing: Theme.gap(14)

            // FINDINGS and WORKING WELL side by side when there's room; stacked on a narrow panel.
            readonly property bool _wide: width >= Theme.sp(640)

            // ── Header: the position and its note · score pill ──────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                Flow {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.gap(6)
                    Text {
                        text: root._pos ? root._pos.name : ""
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                        font.weight: Theme.fontBodyWeight
                        color: Theme.colorText
                    }
                    Text {
                        visible: !!root._pos && (root._pos.note || "") !== ""
                        text: root._pos ? ("· " + root._pos.note) : ""
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                        font.weight: Theme.fontBodyWeight
                        color: Theme.colorText3
                    }
                }
                WristScorePill {
                    Layout.alignment: Qt.AlignTop
                    score: dx.score
                    band: dx.scoreBand
                    breakdown: dx.scoreBreakdown
                    breakdownText: dx.scoreBreakdownText
                }
            }

            // ── Model (band archetype, Auto-detect) + Compare-to (ghost) controls ───
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gap(16)

                // Band model — Auto detects from the swing; the rest are manual overrides.
                Row {
                    spacing: Theme.gap(8)
                    PpMicro {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Theme.caps(qsTr("Model"))
                    }
                    PpSegmentedControl {
                        width: Theme.sp(230)
                        options: dx.archetypes                       // [Auto, Neutral, Bowed, Cupped]
                        selected: dx.archetypes[dx.archetype + 1]    // mode −1..2 → option index 0..3
                        onActivated: (v) => { dx.archetype = dx.archetypes.indexOf(v) - 1 }
                    }
                    PpMicro {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: dx.archetype === -1
                        text: "→ " + Theme.caps(dx.effectiveArchetypeName)
                        font.letterSpacing: Theme.trackingData
                    }
                }

                // Compare-to ghost.
                Row {
                    spacing: Theme.gap(8)
                    PpMicro {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Theme.caps(qsTr("Compare"))
                    }
                    PpSegmentedControl {
                        width: Theme.sp(210)
                        options: [qsTr("Address"), qsTr("Previous"), qsTr("Reference")]
                        selected: dx.compareTo === "previous"  ? qsTr("Previous")
                                : dx.compareTo === "reference" ? qsTr("Reference") : qsTr("Address")
                        onActivated: (v) => { dx.compareTo = (v === qsTr("Previous")  ? "previous"
                                                            : v === qsTr("Reference") ? "reference" : "address") }
                    }
                }

                // "Set as reference" toggle — marks the focused swing as the comparison reference.
                Rectangle {
                    id: refBtn
                    readonly property bool isRef: shotReplay.swingDir !== ""
                                                  && shotReplay.swingDir === appSettings.wristReferenceSwingDir
                    readonly property bool canSet: shotReplay.swingDir !== ""
                    width: refRow.implicitWidth + Theme.sp(14)
                    height: Theme.sp(30)
                    radius: Theme.radius
                    color: isRef ? Theme.colorAccentLight
                                 : refMa.containsMouse ? Theme.colorAccentMid : "transparent"
                    border.width: Theme.borderWidth
                    border.color: isRef ? Theme.colorAccentMid
                                        : refMa.containsMouse ? Theme.colorAccentMid : Theme.colorBorderMid
                    opacity: canSet ? 1.0 : 0.4
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                    Row {
                        id: refRow
                        anchors.centerIn: parent
                        spacing: Theme.gap(3)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "★"
                            font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzLabel
                            color: refBtn.isRef ? Theme.colorAccent : Theme.colorText3
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: refBtn.isRef ? qsTr("Reference") : qsTr("Set as reference")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                            color: refBtn.isRef ? Theme.colorAccent : Theme.colorText2
                        }
                    }
                    PpPressable {
                        id: refMa
                        enabled: refBtn.canSet
                        onClicked: appSettings.wristReferenceSwingDir = (refBtn.isRef ? "" : shotReplay.swingDir)
                    }
                }
            }

            // ── Resemblance readouts: the archetype the bands are drawn from says so in words ──
            Flow {
                Layout.fillWidth: true
                visible: dx.resemblance && dx.resemblance.neutral !== undefined
                spacing: Theme.gap(14)
                PpMicro {
                    height: Theme.sp(18); verticalAlignment: Text.AlignVCenter
                    text: Theme.caps(qsTr("Archetype similarity index"))
                }
                Repeater {
                    model: [ { name: qsTr("Neutral"), key: "neutral", mode: 0 },
                             { name: qsTr("Bowed"),   key: "bowed",   mode: 1 },
                             { name: qsTr("Cupped"),  key: "cupped",  mode: 2 } ]
                    delegate: Row {
                        id: simItem
                        required property var modelData
                        readonly property bool effective: dx.effectiveArchetype === modelData.mode
                        height: Theme.sp(18)
                        spacing: Theme.gap(6)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 %2%").arg(simItem.modelData.name)
                                  .arg(dx.resemblance[simItem.modelData.key] !== undefined
                                       ? dx.resemblance[simItem.modelData.key] : 0)
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                            color: simItem.effective ? Theme.colorText : Theme.colorText3
                        }
                        PpChip {
                            anchors.verticalCenter: parent.verticalCenter
                            text: simItem.effective ? qsTr("effective") : ""
                            tone: Theme.colorAccent
                        }
                    }
                }
            }

            // ── Trajectory strips, then the collapsible data grid ────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: implicitHeight
                spacing: Theme.gap(14)
                Repeater {
                    model: dx.strips
                    delegate: DofTrajectoryStrip {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: implicitHeight
                        strip: modelData
                        positions: dx.positions
                        selected: dx.selectedPosition
                    }
                }
            }

            PositionAngleGrid {
                Layout.fillWidth: true
                gridRows: dx.gridRows
                positions: dx.positions
                selected: dx.selectedPosition
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colorBorder }

            // ── FINDINGS · WORKING WELL — two sections side by side, a hairline between ──
            GridLayout {
                Layout.fillWidth: true
                columns: body._wide ? 3 : 1
                columnSpacing: Theme.gap(16)
                rowSpacing: Theme.gap(14)

                FindingsList {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    findings: dx.findings
                    onSeek: (u) => shotReplay.seekToUs(u)
                }
                Rectangle {
                    Layout.fillWidth: !body._wide
                    Layout.fillHeight: body._wide
                    implicitWidth: 1; implicitHeight: 1
                    color: Theme.colorBorder
                }
                StrengthsList {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    strengths: dx.strengths
                    onSeek: (u) => shotReplay.seekToUs(u)
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colorBorder }

            // ── Legend: each RAG glyph with its word ─────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gap(16)
                    Repeater {
                        model: [ { g: "●", t: qsTr("in range"), c: "green" },
                                 { g: "▲", t: qsTr("watch"),    c: "amber" },
                                 { g: "■", t: qsTr("fault"),    c: "red"   },
                                 { g: "◆", t: qsTr("no data"),  c: "none"  } ]
                        delegate: Row {
                            required property var modelData
                            spacing: Theme.gap(4)
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.modelData.g
                                font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzMicro
                                color: root._ragColor(parent.modelData.c)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.modelData.t
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                font.weight: Theme.fontBodyWeight
                                color: Theme.colorText3
                            }
                        }
                    }
                }
                PpMicro {
                    Layout.alignment: Qt.AlignTop
                    text: Theme.caps(qsTr("Δ from address · degrees"))
                    font.letterSpacing: Theme.trackingData
                }
            }
        }
    }
}
