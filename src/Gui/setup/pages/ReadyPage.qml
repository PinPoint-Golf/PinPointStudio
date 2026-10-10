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

// Session setup — Ready, the closing page: "What this session will record" (design §4.14;
// the retired ScreenSessionWizard.qml Panel 7, l.1824–2043, for the heading, the issue list, the summary
// table and the notice, strings and colours unchanged).
//
// Mark, 5 Oct: close with a page describing what is mounted and how that will impact the
// recording — no trunk sensors and rotation is degraded, no arm/wrist sensors and no wrist
// metrics, no launch monitor and no ball metrics. Top to bottom:
//   the heading and today's copy ("good to go" when no step reports an issue);
//   To fix before you start — the flow's issues, each with its settings link;
//   What is set up — the flow's summary rows (one per step that has one) and the launch monitor;
//   What this session will record — one row per family, from ctx.capability (the catalogue's
//     resolver over the live setup, worded by setup_capability_rows.js — this page only draws it);
//   the notice.
// Start is the footer's and is always allowed (§4.12): this page describes, it gates nothing.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: readyCol.implicitHeight + Theme.sp(32)

    readonly property bool _fullyReady: page.flow.issues.length === 0

    // The summary table: the steps' rows, then the launch monitor (no step owns it).
    // DECISION(stage5b): the device's short name when the connector gives one, else "Configured".
    // Stage 5c: "None configured" is NEUTRAL, not amber — a launch monitor is optional kit.
    readonly property var _setupRows: {
        var rows = page.flow.summaryRows.slice()
        rows.push({ label: qsTr("Launch monitor"), good: page.ctx.launchMonitorConfigured,
                    tone: page.ctx.launchMonitorConfigured ? "" : "neutral",
                    value: page.ctx.launchMonitorConfigured
                               ? (page.ctx.launchMonitorName !== "" ? page.ctx.launchMonitorName : qsTr("Configured"))
                               : qsTr("None configured") })
        return rows
    }

    component SectionLabel: Text {
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color:              Theme.colorText3
    }

    Column {
        id: readyCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.gap(32) }
        spacing: Theme.gap(20)

        Column {
            width: parent.width
            spacing: Theme.gap(8)

            Text {
                text:               page.stepLabel
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
            }
            // DECISION(stage5b): today's heading, "You're good to go" (the brief quoted it as
            // "You're ready to go"; it also said "as today").
            Text {
                objectName:     "readyHeading"
                width:          parent.width
                text:           page._fullyReady ? qsTr("You're good to go") : qsTr("Not quite ready")
                font.family:    Theme.fontDisplay
                font.italic:    Theme.fontDisplayItalic
                font.weight:    Theme.fontDisplayWeight
                font.pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                color:          page._fullyReady ? Theme.colorText : Theme.colorWarn
                wrapMode:       Text.WordWrap
            }
            Text {
                width:          parent.width
                text:           page._fullyReady
                    ? qsTr("Everything checked out. The moment you take your address and make your first swing, Pinpoint starts capturing. There's nothing else to press.")
                    : qsTr("A few things couldn't be confirmed before this session. You can use ← Back to sort them out, or start anyway — Pinpoint will capture what it can, though some analysis may be limited or missing from your results.")
                font.family:    Theme.fontBody
                font.weight:    Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText2
                wrapMode:       Text.WordWrap
                lineHeight:     1.65
            }
        }

        // ── To fix before you start ──────────────────────────────────
        Column {
            visible: !page._fullyReady
            width:   parent.width
            spacing: Theme.gap(6)

            SectionLabel { text: Theme.caps(qsTr("To fix before you start")); bottomPadding: Theme.gap(4) }

            Repeater {
                model: page.flow.issues
                delegate: Row {
                    required property var modelData
                    width:   parent.width
                    spacing: Theme.gap(8)

                    Text {
                        text:           "⚠"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorWarn
                        anchors.top:    parent.top
                        anchors.topMargin: Theme.gap(1)
                    }

                    Column {
                        width:   parent.width - Theme.sp(8) - Theme.sp(16)
                        spacing: Theme.gap(3)

                        Text {
                            width:          parent.width
                            text:           modelData.text
                            font.family:    Theme.fontBody
                            font.weight:    Theme.fontBodyWeight
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText2
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.4
                        }

                        Text {
                            visible:        modelData.panel >= 0
                            text:           modelData.panel === page.ctx.settingsPanelCameras
                                                ? qsTr("→ Open camera settings")
                                                : qsTr("→ Open IMU settings")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color:          linkMa.containsMouse ? Theme.colorText : Theme.colorAccent
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                            MouseArea {
                                id:           linkMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape:  Qt.PointingHandCursor
                                onClicked:    page.flow.openSettings(modelData.panel)
                            }
                        }
                    }
                }
            }
        }

        // ── What is set up ───────────────────────────────────────────
        Column {
            width:   parent.width
            spacing: Theme.gap(10)

            SectionLabel { text: Theme.caps(qsTr("What is set up")) }

            Rectangle {
                width:  parent.width
                height: summaryRows.implicitHeight
                radius: Theme.radius
                color:  "transparent"
                border.width: 1
                border.color: Theme.colorBorderMid

                Column {
                    id: summaryRows
                    width: parent.width

                    Repeater {
                        model: page._setupRows
                        delegate: Column {
                            required property var modelData
                            required property int index
                            width: parent.width
                            Rectangle {
                                visible: index > 0
                                width: parent.width; height: 1; color: Theme.colorBorderMid
                            }
                            SetupSummaryRow {
                                width:    parent.width
                                rowLabel: modelData.label
                                rowValue: modelData.value
                                good:     modelData.good
                                tone:     modelData.tone || ""
                            }
                        }
                    }
                }
            }
        }

        // ── What this session will record ────────────────────────────
        Column {
            width:   parent.width
            spacing: Theme.gap(10)

            SectionLabel { text: Theme.caps(qsTr("What this session will record")) }

            Rectangle {
                width:  parent.width
                height: capRows.implicitHeight
                radius: Theme.radius
                color:  "transparent"
                border.width: 1
                border.color: Theme.colorBorderMid

                Column {
                    id: capRows
                    width: parent.width

                    Repeater {
                        model: page.ctx.capability
                        delegate: Column {
                            required property var modelData
                            required property int index
                            width: parent.width

                            Rectangle {
                                visible: index > 0
                                width: parent.width; height: 1; color: Theme.colorBorderMid
                            }

                            Item {
                                id: capRow
                                objectName: "capabilityRow"
                                // What the row says, for the tests: the row as one line.
                                property string family:    modelData.label
                                property string stateText: modelData.stateText
                                property string reason:    modelData.reason
                                property string tone:      modelData.tone
                                readonly property color _toneColor:
                                    modelData.tone === "good" ? Theme.colorGood
                                  : modelData.tone === "warn" ? Theme.colorWarn
                                                              : Theme.colorText3

                                width:  parent.width
                                height: capCol.implicitHeight + Theme.sp(20)

                                RowLayout {
                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                              margins: Theme.gap(14) }
                                    spacing: Theme.gap(10)

                                    Rectangle {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: Theme.gap(7)
                                        width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                                        color: capRow._toneColor
                                    }
                                    Column {
                                        id: capCol
                                        Layout.fillWidth: true
                                        spacing: Theme.gap(2)
                                        Text {
                                            width:          parent.width
                                            text:           modelData.label
                                            font.family:    Theme.fontBody
                                            font.pixelSize: Theme.fontSzBody2
                                            color:          Theme.colorText
                                        }
                                        Text {
                                            visible:            modelData.reason !== ""
                                            width:              parent.width
                                            text:               modelData.reason
                                            font.family:        Theme.fontData
                                            font.pixelSize:     Theme.fontSzMicro
                                            font.letterSpacing: Theme.trackingData
                                            color:              Theme.colorText3
                                            wrapMode:           Text.WordWrap
                                        }
                                    }
                                    Text {
                                        Layout.alignment:   Qt.AlignTop
                                        text:               modelData.stateText
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingData
                                        color:              capRow._toneColor
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Notice box
        Rectangle {
            width: parent.width
            height: noticeText.implicitHeight + Theme.sp(20)
            radius: Theme.radius
            color:  page._fullyReady ? Theme.colorGoodLight : Theme.colorWarnLight
            border.width: 1
            border.color: page._fullyReady ? Theme.colorGood : Theme.colorWarn

            Text {
                id: noticeText
                objectName: "readyNotice"
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(12) }
                text: page._fullyReady
                          ? qsTr("Everything's set up and ready to go. Step up when you like — Pinpoint will start capturing the moment you take your address.")
                          : qsTr("Starting with an incomplete setup is fine — partial data is often still useful. For the full picture though, it's worth coming back once the hardware is sorted. Your results will thank you for it.")
                font.family:    Theme.fontBody
                font.weight:    Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color:          page._fullyReady ? Theme.colorGood : Theme.colorWarn
                wrapMode:       Text.WordWrap
                lineHeight:     1.5
            }
        }
    }
}
