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

// Centre-stage arranger. Shows the enabled, host-wired stage panels (camera/charts/
// table and kin) packed per ViewLayout.arrangementFor(SessionMode.mode): tabs | split |
// stage. A panel a screen does not provide a delegate for is omitted (see `active`).
//
// Each arrangement's Loaders are gated on the active `arrangement` (not just the
// container's visible:), so only the selected layout instantiates its panels —
// a hidden arrangement must not spin up a second PpCameraTiles (and its camera
// frame subscriptions) behind the visible one.
//
// THE STAGE OWNS THE FRAME. Every panel it shows sits in a PpStageCard titled and toned from
// _defs, so the panels draw no background, border or title of their own. A panel may say one
// more thing on the card's heading row through `readonly property string cardAside`; a panel
// also used outside the stage carries `property bool framed` and is told here that it is
// framed already. In tabs the tab strip IS the card's heading, and the card takes the
// selected panel's tone.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: stage

    property Component cameraDelegate:      null
    property Component chartsDelegate:      null
    property Component sessionDiagnosticsDelegate: null
    property Component launchMonitorDelegate: null
    property Component wristMotionDelegate: null
    property Component tableDelegate:       null
    property Component markupDelegate:      null
    // The 3-D swing (swing3d/). Its delegate is only a SLOT: the View3D itself lives in the
    // screen's SwingViz3DHost and is lent to whichever slot is showing — never rebuilt here.
    property Component swing3dDelegate:     null

    // Layout resolves on the active session MODE, not the session type.
    readonly property string arrangement: ViewLayout.arrangementFor(SessionMode.mode)

    // ⚠ THE ORDER IS THE PRIORITY, and session diagnostics leads it.
    //
    // This list is read three times — the tab strip's order, the split row's order, and which
    // panel is DOMINANT in the stage arrangement (active[0] takes 62% of the width). So one
    // ordering decides where the eye goes in every arrangement, which is why the swap is made
    // here and not in three places.
    //
    // Camera led it because a session used to be a thing you watched. It is a thing you read
    // now: the diagnostics panel is what says what keeps happening, and a golfer who has turned
    // it on has said that is the question they came with. It only leads WHEN IT IS ON — `active`
    // filters on ViewLayout.isPanelOn and on the host screen having wired a delegate — so a
    // session without it is camera-first exactly as before, with nothing to notice.
    //
    // `title` is the card's Micro heading. `tone` is a ROLE, resolved by _toneFor: the two text
    // panels that name faults read in colorWarn, and the canvases (video, 3-D, plots, the
    // board, the table, markup) get the quiet card so the frame never competes with what is
    // in it. A role rather than the colour itself, because a colour here would rebuild this
    // list — and so every panel on the stage — on a theme change.
    // Theme.caps on the titles is the exception that is allowed: it changes only when the
    // aesthetic moves into or out of one that sets headings in capitals, not on every theme.
    readonly property var _defs: [
        { key: "sessionDiagnostics", label: qsTr("Session diagnostics"), title: Theme.caps(qsTr("Session diagnostics")),
          tone: "warn",  comp: sessionDiagnosticsDelegate },
        { key: "camera",        label: qsTr("Camera"),                title: Theme.caps(qsTr("Camera")),
          tone: "quiet", comp: cameraDelegate },
        { key: "swing3d",       label: qsTr("3-D swing"),             title: Theme.caps(qsTr("3-D swing")),
          tone: "quiet", comp: swing3dDelegate },
        { key: "launchMonitor", label: qsTr("Launch monitor"),        title: Theme.caps(qsTr("Launch monitor")),
          tone: "quiet", comp: launchMonitorDelegate },
        { key: "wristMotion",   label: qsTr("Wrist motion analysis"), title: Theme.caps(qsTr("Wrist motion")),
          tone: "warn",  comp: wristMotionDelegate },
        { key: "charts",        label: qsTr("Charts"),                title: Theme.caps(qsTr("Charts")),
          tone: "quiet", comp: chartsDelegate },
        { key: "table",         label: qsTr("Table"),                 title: Theme.caps(qsTr("Table")),
          tone: "quiet", comp: tableDelegate },
        { key: "markup",        label: qsTr("Markup"),                title: Theme.caps(qsTr("Markup")),
          tone: "quiet", comp: markupDelegate }
    ]
    function _toneFor(role) { return role === "warn" ? Theme.colorWarn : Theme.colorText3 }

    // ordered; enabled AND actually wired by the host screen. A panel a screen does not provide a
    // delegate for (e.g. "wristMotion" on screens that never wire it) is simply omitted rather
    // than shown as an empty placeholder.
    readonly property var active: _defs.filter(function(d) {
        return ViewLayout.isPanelOn(SessionMode.mode, d.key) && d.comp !== null
    })

    property int tabIndex: 0
    onActiveChanged: if (tabIndex >= active.length) tabIndex = 0
    readonly property var _tabDef: (stage.arrangement === "tabs" && stage.active.length > 0)
                                   ? stage.active[Math.min(stage.tabIndex, stage.active.length - 1)] : null

    readonly property int _gap: Theme.sp(12)

    // Both hooks are optional and only some panels declare them, so the panel is held untyped.
    function _asideOf(panel) {
        return (panel && panel.cardAside !== undefined) ? String(panel.cardAside) : ""
    }
    function _unframe(panel) {
        if (panel && panel.framed !== undefined) panel.framed = false
    }

    // A panel in its card. The aside is whatever the panel offers (nothing, for most); `framed`
    // is switched off on load so a panel that frames itself elsewhere does not frame itself twice.
    component PanelCard: PpStageCard {
        id: pc
        property var def: null
        property Component placeholder: null
        tone:  stage._toneFor(pc.def ? pc.def.tone : "")
        title: pc.def ? pc.def.title : ""
        aside: stage._asideOf(panelLoader.item)
        Loader {
            id: panelLoader
            anchors.fill: parent
            sourceComponent: pc.def ? (pc.def.comp || pc.placeholder) : null
            onLoaded: stage._unframe(panelLoader.item)
        }
    }

    // empty state
    PpCardNote {
        anchors.centerIn: parent
        visible: stage.active.length === 0
        text: qsTr("No panels selected — pick some in View")
    }

    // ── SPLIT — even row ─────────────────────────────────────────────────────
    RowLayout {
        anchors.fill: parent; anchors.margins: Theme.gap(10)
        spacing: stage._gap
        visible: stage.arrangement === "split" && stage.active.length > 0
        Repeater {
            model: stage.arrangement === "split" ? stage.active : []
            delegate: PanelCard {
                required property var modelData
                Layout.fillWidth: true; Layout.fillHeight: true
                def: modelData
                placeholder: placeholderComp
            }
        }
    }

    // ── STAGE — first panel dominant, rest in a side column ──────────────────
    RowLayout {
        anchors.fill: parent; anchors.margins: Theme.gap(10)
        spacing: stage._gap
        visible: stage.arrangement === "stage" && stage.active.length > 0
        PanelCard {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.preferredWidth: stage.width * 0.62
            def: (stage.arrangement === "stage" && stage.active.length > 0) ? stage.active[0] : null
            placeholder: placeholderComp
        }
        ColumnLayout {
            visible: stage.active.length > 1
            Layout.preferredWidth: stage.width * 0.30
            Layout.fillHeight: true
            spacing: stage._gap
            Repeater {
                model: (stage.arrangement === "stage" && stage.active.length > 1)
                       ? stage.active.slice(1) : []
                delegate: PanelCard {
                    required property var modelData
                    Layout.fillWidth: true; Layout.fillHeight: true
                    def: modelData
                    placeholder: placeholderComp
                }
            }
        }
    }

    // ── TABS — one card, its heading the tab strip ───────────────────────────
    PanelCard {
        anchors.fill: parent; anchors.margins: Theme.gap(10)
        visible: stage._tabDef !== null
        def: stage._tabDef
        placeholder: placeholderComp
        heading: tabStrip
    }

    // The tab strip, as a card heading: Micro labels, the selected one in colorText over a 2 px
    // rule in that panel's tone. The labels are the cards' own titles, so a panel reads the same
    // whether it is a tab or a card of its own.
    Component {
        id: tabStrip
        Row {
            spacing: Theme.gap(20)
            Repeater {
                model: stage.active
                delegate: Item {
                    id: tab
                    required property var modelData
                    required property int index
                    // By key: a Repeater hands each delegate its own copy of the entry.
                    readonly property bool sel: !!stage._tabDef && tab.modelData.key === stage._tabDef.key
                    implicitWidth:  tabTxt.implicitWidth
                    implicitHeight: tabTxt.implicitHeight + Theme.sp(6)
                    PpMicro {
                        id: tabTxt
                        text: tab.modelData.title
                        color: tab.sel ? Theme.colorText
                             : tabMa.containsMouse ? Theme.colorText2 : Theme.colorText3
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                    Rectangle {
                        visible: tab.sel
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2; radius: 1
                        color: stage._toneFor(tab.modelData.tone)
                    }
                    // A Micro label is a small target; the hit area reaches past it.
                    MouseArea {
                        id: tabMa
                        anchors.fill: parent
                        anchors.margins: -Theme.sp(6)
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: stage.tabIndex = tab.index
                    }
                }
            }
        }
    }

    Component { id: placeholderComp; PpStagePanel {} }
}
