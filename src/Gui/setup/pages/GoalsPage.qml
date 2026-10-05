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

// Session setup — Goals (the retired ScreenSessionWizard.qml Panel 0, l.807–915, and its footer hint,
// l.2070–2074), ported with every string, colour and size unchanged. The picks live in the draft
// (SetupDraft.goals), not here: this page exists only while current (R2), and a trip to Settings
// no longer re-seeds them (F4).
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: goalsCol.implicitHeight + Theme.sp(32)

    hint: {
        var n = page.draft.goals.length
        if (n === 0) return qsTr("No goals selected — defaulting to %1")
                                .arg(page.draft.defaultGoal ? page.draft.defaultGoal.name : "")
        return n === 1 ? qsTr("1 goal selected") : qsTr("%1 goals selected").arg(n)
    }
    // Today's footer drew the Goals hint in colorText3: neutral.

    Column {
        id: goalsCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
        spacing: Theme.sp(16)

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("What are you working on today?")
            body:    qsTr("Pick the areas you want to focus on this session. Pinpoint will highlight these metrics in the live view and lead with them in your summary. We've picked up where you left off — change anything you like and we'll remember it next time.")
        }

        GridLayout {
            width:         parent.width
            columns:       2
            columnSpacing: Theme.sp(8)
            rowSpacing:    Theme.sp(8)

            Repeater {
                model: page.draft.goalDefs

                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    required property int index

                    Layout.fillWidth:    true
                    Layout.preferredHeight: chipCol.implicitHeight + Theme.sp(36)
                    radius: Theme.radius

                    readonly property bool sel: page.draft.goals.indexOf(modelData.key) !== -1
                    readonly property bool showLast: !page.draft.goalsInteracted
                                                     && sel
                                                     && page.draft.goals.length > 0
                                                     && page.draft.goals[0] === modelData.key

                    // Multi-select goal card — chip/tile language: selected shows the
                    // accent wash + accent border; hover (unselected) fades a faint bg
                    // fill in (alpha-ramped → no flash) and eases the border to accentMid.
                    // Pairs with the PpPressable scale-grow / press-dip below.
                    color:        sel                  ? Theme.colorAccentLight
                                : goalMa.containsMouse ? Theme.colorBg2
                                :                        Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                    border.width: 1
                    border.color: sel                  ? Theme.colorAccent
                                : goalMa.containsMouse ? Theme.colorAccentMid
                                :                        Theme.colorBorderMid
                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    Column {
                        id: chipCol
                        anchors {
                            left: parent.left; right: parent.right
                            leftMargin: Theme.sp(14); rightMargin: Theme.sp(14)
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: Theme.sp(5)

                        Row {
                            spacing: Theme.sp(6)
                            Text {
                                text:           chip.modelData.name
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          chip.sel ? Theme.colorAccent : Theme.colorText
                            }
                            Text {
                                visible:        chip.showLast
                                text:           qsTr("↩ last session")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width:              parent.width
                            text:               chip.modelData.sub
                            font.family:        Theme.fontData
                            font.pixelSize:     Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingData
                            color:              Theme.colorText3
                            elide:              Text.ElideRight
                        }
                    }

                    PpPressable {
                        id: goalMa
                        onClicked: page.draft.toggleGoal(chip.modelData.key)
                    }
                }
            }
        }
    }
}
