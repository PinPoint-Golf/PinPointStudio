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

// HOME (ux_refactor_away_from_4_modes §2.3, Appendix A.1). No golfer yet: the first-run
// onboarding. A golfer selected, top to bottom:
//   1. Start — one table card: left, a new session (Start session, the club, the last session);
//      right, what this rig can measure (HmRigCapability), with the phone pairing entry, a phone's
//      offered sessions and the first device warning;
//   2. HmHomeTabs — Coaching | Your swing.
// The golfer's sessions are the rail's Sessions (ScreenSessions), not repeated here.
// D12: no identity row and no "PinPoint Studio" hero — the rail's avatar shows and switches the
// golfer. The contour background (PpTopoBackground) stays behind the first-run onboarding only; the
// mockup's ground under a golfer's Home is flat.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    signal addAthleteRequested()
    signal editCurrentAthleteRequested()
    // Start: open the session wizard. The request is { focusConditionId, focusLabel }: Home's
    // Start carries no focus (an open assessment, the golfer can pick one in the wizard); a
    // work-on's Practise on the Coaching or Your swing tab carries its condition.
    signal startSessionRequested(var request)
    // Swing diagnostics (the Work-ons screen), showing one area's work-ons — `group` is the area's
    // ConditionGroup token — or every work-on when it is "".
    signal swingDiagnosticsRequested(string group)
    // The rig pane's "Rig ▸": Settings → Studio.
    signal studioSettingsRequested()

    // EMPTY, not a club. This is carried into SessionController.activeClub at session
    // start, and start() only seeds from the athlete's preferred club when activeClub is
    // empty — so a literal here becomes the session's club and silently outranks the
    // preference whenever onAthMapChanged hasn't resolved one (no current athlete, or an
    // empty bag, which also hides the club picker so it can't be corrected).
    property string selectedClub: ""

    property var athMap: {
        if (!athleteController.hasCurrentAthlete) return {}
        var list = athleteController.athletes
        for (var i = 0; i < list.length; i++) {
            if (list[i].uuid === athleteController.currentUuid) return list[i]
        }
        return {}
    }

    // The current athlete's bag (canonical ids, vocabulary order). Empty when
    // there's no athlete or an emptied bag — the club picker hides in that case.
    // Re-evaluates on every club commit via athletesChanged.
    readonly property var clubModel: {
        void athleteController.athletes
        if (!athleteController.hasCurrentAthlete) return []
        var bag = athleteController.clubsFor(athleteController.currentUuid)
        var vocab = athleteController.clubOptions()
        var ids = Object.keys(bag)
        ids.sort(function (a, b) { return vocab.indexOf(a) - vocab.indexOf(b) })
        return ids
    }

    // Default the club to the athlete's preferred club (resolved to a real bag
    // club) whenever the athlete or their bag changes.
    onAthMapChanged: {
        if (!athleteController.hasCurrentAthlete) return
        var pref = athleteController.effectivePrimaryClub(athleteController.currentUuid)
        if (pref !== "") root.selectedClub = pref
    }

    readonly property bool hasGolfer: athleteController.hasCurrentAthlete

    // The last session, from the work-ons' records: { label, shots }, or empty.
    readonly property var lastSession: (typeof workOns !== "undefined" && workOns) ? workOns.lastSession : ({})
    readonly property string lastSessionText: {
        var s = root.lastSession || {}
        if (!s.label) return ""
        var n = s.shots || 0
        return n === 1 ? qsTr("Last session %1 · 1 shot").arg(s.label)
                       : qsTr("Last session %1 · %2 shots").arg(s.label).arg(n)
    }

    // The device warning in the rig pane reads resourceMonitor; keep it current while Home shows.
    Component.onCompleted: resourceMonitor.refresh()

    onVisibleChanged: if (visible && typeof workOns !== "undefined") workOns.refresh()

    Timer {
        interval: 2000
        running:  root.visible
        repeat:   true
        onTriggered: resourceMonitor.refresh()
    }

    Rectangle { anchors.fill: parent; color: Theme.colorBg }

    // Ambient theme-reactive contour field over the solid background, behind the first-run
    // onboarding only (a golfer's Home is flat ground, as the mockup draws it). Subtle by default;
    // pauses when hidden or backgrounded; respects reduced-motion. Hover lifts the terrain under the
    // cursor; click/tap sends a ripple.
    PpTopoBackground {
        id: topo
        anchors.fill: parent
        visible:   !root.hasGolfer
        colorLow:  Theme.gradientWarm
        colorMid:  Theme.gradientWarmLit
        colorHigh: Theme.gradientCool
        accentColor: Theme.colorAccent
        animated:  !appSettings.reduceMotion && visible
        hoverPoint: topoHover.hovered
            ? Qt.point(topoHover.point.position.x / width,
                       topoHover.point.position.y / height)
            : Qt.point(-1, -1)
    }

    Flickable {
        anchors.fill:  parent
        contentWidth:  width
        contentHeight: mainCol.implicitHeight + 80
        clip:          true

        // Pointer handlers live on the Flickable, not the screen root: the
        // full-bleed Flickable sits above the fixed background and intercepts
        // presses for flick-detection, so root-level handlers never see a
        // completed tap. Hosted here they share the viewport coordinate space the
        // background uses. HoverHandler never blocks; TapHandler's DragThreshold
        // keeps only a passive grab, so cards/buttons still get their clicks and
        // a vertical drag flicks the list instead of spawning a ripple.
        HoverHandler {
            id: topoHover
            enabled: topo.animated
        }
        TapHandler {
            enabled: topo.animated
            gesturePolicy: TapHandler.DragThreshold
            onTapped: (ep) => topo.ripple(ep.position.x / topo.width,
                                          ep.position.y / topo.height)
        }

        Column {
            id: mainCol
            anchors.horizontalCenter: parent.horizontalCenter
            // HOME'S OWN WIDTH RULE: about 90 % of the area with a golfer (the mockup's column), so
            // the Start card's two panes and the tables have room. Theme.contentWidth() (70 %) is the
            // rest of the app's and stays as it is; the first-run onboarding keeps it.
            width:   root.hasGolfer ? Math.round(parent.width * 0.9) : Theme.contentWidth(parent.width)
            spacing: 0

            Item { width: 1; height: root.hasGolfer ? Theme.sp(28) : Theme.sp(48) }

            // ── First run: no golfer yet ─────────────────────────────────────
            // No athlete state
            Column {
                visible: !athleteController.hasCurrentAthlete
                height:  visible ? implicitHeight : 0
                width:   parent.width
                spacing: 0

                Text {
                    text:               Theme.caps(qsTr("Golf swing analysis"))
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:              Theme.colorText3
                }
                Item { width: 1; height: Theme.sp(12) }

                PpDisplayText {
                    width:          parent.width
                    text:           qsTr("Welcome to Pinpoint Studio")
                    wrapMode:       Text.WordWrap
                    lineHeight:     1.1
                }
                Item { width: 1; height: Theme.sp(10) }

                Text {
                    width:          parent.width
                    text:           qsTr("An open-source workshop for understanding the golf swing — cameras, IMUs, and ground forces working together to show you what's actually happening.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText2
                    wrapMode:       Text.WordWrap
                    lineHeight:     1.7
                }
                Item { width: 1; height: Theme.sp(32) }

                Text {
                    width:          parent.width
                    text:           qsTr("Start by adding an athlete")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.colorText
                }
                Item { width: 1; height: Theme.sp(4) }

                Text {
                    width:          parent.width
                    text:           qsTr("Every session belongs to someone. That's usually you.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText3
                }
                Item { width: 1; height: Theme.sp(16) }

                Rectangle {
                    width:  parent.width
                    height: Theme.sp(42)
                    radius: Theme.radius
                    color:  Theme.colorAccent

                    Text {
                        anchors.centerIn: parent
                        text:           qsTr("Add your first athlete")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.dark ? Theme.colorBg : "#FFFFFF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.addAthleteRequested()
                    }
                }
                Item { width: 1; height: Theme.sp(48) }

                Row {
                    width:   parent.width
                    spacing: Theme.gap(10)

                    Repeater {
                        model: [
                            { icon: "⊞", title: qsTr("Connect a camera"), desc: qsTr("Basler or GenTL over USB3") },
                            { icon: "⌖", title: qsTr("Pair wrist IMUs"),   desc: qsTr("Lead and trail hand sensors") },
                            { icon: "↗", title: qsTr("Read the docs"),     desc: qsTr("Setup guides and hardware") }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width:  (parent.width - 20) / 3
                            height: secCardCol.implicitHeight + 28
                            radius: Theme.radius
                            color:  "transparent"
                            border.width: 1
                            border.color: Theme.colorBorder

                            Column {
                                id: secCardCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(14) }
                                spacing: 0

                                Item { width: 1; height: Theme.sp(14) }
                                Text {
                                    text:           modelData.icon
                                    font.pixelSize: Theme.sp(16)
                                    color:          Theme.colorText2
                                }
                                Item { width: 1; height: 7 }
                                Text {
                                    width:          parent.width
                                    text:           modelData.title
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText
                                }
                                Item { width: 1; height: 3 }
                                Text {
                                    width:          parent.width
                                    text:           modelData.desc
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText3
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.4
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape:  Qt.PointingHandCursor
                                onEntered:    parent.color = Theme.colorBg2
                                onExited:     parent.color = "transparent"
                            }
                        }
                    }
                }
                Item { width: 1; height: Theme.sp(24) }

                Text {
                    width:               parent.width
                    text:                qsTr("Nothing connects to the cloud unless you configure it.")
                    font.family:         Theme.fontBody
                    font.pixelSize:      Theme.fontSzBody2
                    font.weight:         Theme.fontBodyWeight
                    color:               Theme.colorText3
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // ── 1. Start ─────────────────────────────────────────────────────
            //
            // One table card, two panes: a new session on the left, what this rig can measure on the
            // right. Stacked, the rig under the session, where the column is too narrow for both.
            PpCard {
                id: startCard
                objectName: "homeStartCard"
                visible: root.hasGolfer
                height:  visible ? implicitHeight : 0
                width:   parent.width
                table:   true

                readonly property int  panePad: Theme.gap(28)
                readonly property bool stacked: width < Theme.sp(760)
                readonly property int  leftW:   stacked ? width : Math.round(width * 0.6)

                Item {
                    id: startBody
                    width:  startCard.width
                    height: startCard.stacked
                            ? startPane.implicitHeight + rigPane.implicitHeight + 4 * startCard.panePad + 1
                            : Math.max(startPane.implicitHeight, rigPane.implicitHeight) + 2 * startCard.panePad

                    // ── New session ──
                    Column {
                        id: startPane
                        x: startCard.panePad
                        y: startCard.panePad
                        width: startCard.leftW - 2 * startCard.panePad
                        spacing: 0

                        Item { width: 1; height: Theme.sp(8); visible: !startCard.stacked }
                        Text {
                            text:               Theme.caps(qsTr("New session"))
                            font.family:        Theme.fontData
                            font.pixelSize:     Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingMicro
                            color:              Theme.colorText3
                        }
                        Item { width: 1; height: Theme.sp(10) }
                        PpDisplayText {
                            width:    parent.width
                            text:     qsTr("Hit some shots")
                            wrapMode: Text.WordWrap
                        }
                        Item { width: 1; height: Theme.sp(10) }
                        Text {
                            width:          parent.width
                            text:           qsTr("Every shot is assessed against the swing model.")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            font.weight:    Theme.fontBodyWeight
                            color:          Theme.colorText2
                            wrapMode:       Text.WordWrap
                        }
                        Item { width: 1; height: Theme.sp(20) }

                        // Start session, and the club beside it.
                        Flow {
                            width:   parent.width
                            spacing: Theme.gap(20)

                            PpButton {
                                id: startButton
                                objectName: "homeStartButton"
                                primary: true
                                label:   qsTr("Start session")
                                glyph:   "▶"
                                height:  Theme.sp(40)
                                width:   implicitWidth + Theme.sp(16)
                                onClicked: root.startSessionRequested({ focusConditionId: "", focusLabel: "" })
                            }

                            // The club: the current athlete's bag, as a compact picker. Hidden with
                            // an empty bag (no club to pick; the session then takes the athlete's
                            // preference, see selectedClub).
                            Row {
                                visible: root.clubModel.length > 0
                                height:  startButton.height
                                spacing: Theme.gap(8)

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text:           qsTr("Club:")
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText2
                                }
                                PpComboBox {
                                    id: clubPicker
                                    objectName: "homeClubPicker"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width:     Theme.sp(150)
                                    model:     root.clubModel
                                    displayFn: function (id) { return ClubFormat.display(id) }
                                    currentIndex: root.clubModel.indexOf(root.selectedClub)
                                    onActivated: function (index) {
                                        root.selectedClub = root.clubModel[index]
                                        // The control set currentIndex itself; follow selectedClub again.
                                        clubPicker.currentIndex = Qt.binding(function () {
                                            return root.clubModel.indexOf(root.selectedClub)
                                        })
                                    }
                                }
                            }
                        }

                        Item { width: 1; height: Theme.sp(18); visible: lastSessionLine.visible }
                        Text {
                            id: lastSessionLine
                            objectName: "homeLastSession"
                            visible:        root.lastSessionText !== ""
                            width:          parent.width
                            text:           root.lastSessionText
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            font.weight:    Theme.fontBodyWeight
                            color:          Theme.colorText3
                            elide:          Text.ElideRight
                        }
                    }

                    // The pane rule: vertical between the panes, horizontal when stacked.
                    Rectangle {
                        x:      startCard.stacked ? 0 : startCard.leftW
                        y:      startCard.stacked ? startPane.y + startPane.implicitHeight + startCard.panePad : 0
                        width:  startCard.stacked ? startBody.width : 1
                        height: startCard.stacked ? 1 : startBody.height
                        color:  Theme.colorHairline
                    }

                    // ── What this rig can measure ──
                    HmRigCapability {
                        id: rigPane
                        x: startCard.stacked ? startCard.panePad : startCard.leftW + 1 + startCard.panePad
                        y: startCard.stacked ? startPane.y + startPane.implicitHeight + 2 * startCard.panePad + 1
                                             : startCard.panePad
                        width: startCard.stacked ? startCard.width - 2 * startCard.panePad
                                                 : startCard.width - startCard.leftW - 1 - 2 * startCard.panePad
                        onStudioSettingsRequested: root.studioSettingsRequested()
                    }
                }
            }

            // ── 2. Coaching | Your swing ─────────────────────────────────────
            //
            // The golfer's swing, read off WorkOnsController's catch-up (re-run whenever Home comes
            // back into view, onVisibleChanged — a session may have ended since). Practise opens the
            // wizard with that condition as the focus; "All of <area> ▸" and "All work-ons ▸" open
            // Swing diagnostics, on that area or on everything.
            Item { width: 1; height: Theme.sp(40); visible: homeTabs.visible }

            HmHomeTabs {
                id: homeTabs
                visible: root.hasGolfer
                height:  visible ? implicitHeight : 0
                width:   parent.width
                onPractiseRequested: (conditionId, label) =>
                    root.startSessionRequested({ focusConditionId: conditionId, focusLabel: label })
                onAllInAreaRequested: (group) => root.swingDiagnosticsRequested(group)
                onAllWorkOnsRequested: root.swingDiagnosticsRequested("")
            }

            Item { width: 1; height: 60 }
        }
    }
}
