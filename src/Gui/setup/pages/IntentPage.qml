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

// Session setup — Focus (step key "intent"; phase 2 C5, replacing the retired Goals page). What
// this session works on: one condition the session's diagnostics track from the first swing, or
// none (Open assessment). Single choice, from SetupContext.focusChoices:
//   - Open assessment (focus "");
//   - the coaching summary's focus, marked "your focus";
//   - the active and easing work-ons;
// plus the focus the visit was opened with when it is none of those (Practise on a cleared
// condition: SetupDraft.requestedFocus). Every list but the first may be empty.
//
// The pick lives in the draft (SetupDraft.focusConditionId / focusLabel), not here: this page
// exists only while current (R2), and a trip to Settings keeps it (F4). Start hands it on.
// The chips are the retired Goals page's (chip/tile language), single-select.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: intentCol.implicitHeight + Theme.sp(32)

    // The choices: the context's, with the opened-with focus added (after the coaching focus) when
    // no other entry offers it.
    readonly property var choices: {
        var base = page.ctx ? page.ctx.focusChoices : [{ id: "", label: qsTr("Open assessment"), kind: "open", status: "" }]
        var req = page.draft ? page.draft.requestedFocus : null
        if (!req || !req.id) return base
        for (var i = 0; i < base.length; ++i)
            if (base[i].id === req.id) return base
        var out = base.slice()
        var at = out.length > 1 && out[1].kind === "coaching" ? 2 : 1
        out.splice(at, 0, { id: req.id, label: req.label, kind: "requested", status: "" })
        return out
    }
    // No coaching focus and no work-ons to offer (a new athlete, or the first scan not run yet).
    readonly property bool onlyOpen: {
        var c = page.choices
        for (var i = 0; i < c.length; ++i)
            if (c[i].kind !== "open") return false
        return true
    }

    hint: page.draft && page.draft.focusConditionId !== ""
          ? qsTr("Focus: %1").arg(page.draft.focusLabel)
          : qsTr("Open assessment — no focus")
    // Neutral, as the Goals hint was: either answer is a fine way to start.

    function _sub(c) {
        if (c.kind === "open")      return Theme.caps(qsTr("Look at everything"))
        if (c.kind === "requested") return Theme.caps(qsTr("Picked to practice"))
        var status = c.status === "easing" ? qsTr("Easing") : c.status === "active" ? qsTr("Active") : ""
        if (c.kind === "coaching")
            return Theme.caps(status !== "" ? qsTr("From your swing summary · %1").arg(status)
                                            : qsTr("From your swing summary"))
        return Theme.caps(status !== "" ? qsTr("Work-on · %1").arg(status) : qsTr("Work-on"))
    }

    Column {
        id: intentCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.gap(32) }
        spacing: Theme.gap(16)

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("What are you working on today?")
            body:    qsTr("Pick one thing to work on and this session's diagnostics will follow it from your first swing. Choose Open assessment to look at everything with no focus.")
        }

        GridLayout {
            width:         parent.width
            columns:       2
            columnSpacing: Theme.gap(8)
            rowSpacing:    Theme.gap(8)

            Repeater {
                model: page.choices

                delegate: Rectangle {
                    id: chip
                    objectName: "intentChoice"
                    required property var modelData
                    required property int index

                    // For the tests: what this chip stands for.
                    readonly property string choiceId:    modelData.id
                    readonly property string choiceKind:  modelData.kind
                    readonly property string choiceLabel: modelData.label

                    Layout.fillWidth:    true
                    Layout.preferredHeight: chipCol.implicitHeight + Theme.sp(36)
                    radius: Theme.radius

                    readonly property bool sel: page.draft !== null && page.draft.focusConditionId === modelData.id

                    // Single-select card — the Goals page's chip/tile language: selected shows the
                    // accent wash + accent border; hover (unselected) fades a faint bg fill in
                    // (alpha-ramped → no flash) and eases the border to accentMid.
                    color:        sel                    ? Theme.colorAccentLight
                                : choiceMa.containsMouse ? Theme.colorBg2
                                :                          Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                    border.width: 1
                    border.color: sel                    ? Theme.colorAccent
                                : choiceMa.containsMouse ? Theme.colorAccentMid
                                :                          Theme.colorBorderMid
                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    Column {
                        id: chipCol
                        anchors {
                            left: parent.left; right: parent.right
                            leftMargin: Theme.gap(14); rightMargin: Theme.gap(14)
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: Theme.gap(5)

                        Row {
                            width:   parent.width
                            spacing: Theme.gap(6)
                            Text {
                                width:          Math.min(implicitWidth, parent.width - (yourFocus.visible ? yourFocus.implicitWidth + parent.spacing : 0))
                                text:           chip.modelData.label
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          chip.sel ? Theme.colorAccent : Theme.colorText
                                elide:          Text.ElideRight
                            }
                            Text {
                                id: yourFocus
                                objectName:     "intentYourFocus"
                                visible:        chip.modelData.kind === "coaching"
                                text:           qsTr("★ your focus")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width:              parent.width
                            text:               page._sub(chip.modelData)
                            font.family:        Theme.fontData
                            font.pixelSize:     Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingData
                            color:              Theme.colorText3
                            elide:              Text.ElideRight
                        }
                    }

                    PpPressable {
                        id: choiceMa
                        onClicked: page.draft.setFocus(chip.modelData.id, chip.modelData.label)
                    }
                }
            }
        }

        // Nothing but Open assessment to offer: say why, so the single chip does not read as broken.
        Text {
            objectName:     "intentEmptyNote"
            visible:        page.onlyOpen
            width:          parent.width
            text:           qsTr("No work-ons yet. Pinpoint finds them in your sessions as you hit more swings; until then each session is an open assessment.")
            font.family:    Theme.fontBody
            font.weight:    Theme.fontBodyWeight
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText3
            wrapMode:       Text.WordWrap
            lineHeight:     1.65
        }
    }
}
