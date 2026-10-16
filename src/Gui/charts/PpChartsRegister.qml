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

import QtQuick
import QtQuick.Controls
import PinPointStudio

// PPCHARTSREGISTER — what the Charts registers share (PpChartsReplay and PpChartsAnalyse are this with their
// `register` set; design §3, spec 15.6). A register is how much the panel says, never its encodings: the
// same views (PpChartsShotView, PpChartsSessionView, PpChartsRelationsView, PpChartsKindsView) are drawn by
// both, told which register they are in.
//   • the tab row: quiet PpTabs (underline, no capsule) — This shot · Session · Relations · Kinds in Analyse;
//     Replay has no Kinds form, so its tab row stops at Relations and a Kinds choice reads Session there.
//     There is no Wrist tab (Mark, 16 Oct): the wrist read is a section of This shot and of Session, drawn by
//     those views when it is meaningful (body.wristShotShown / wristSessionShown). A saved "wrist" reads This shot.
//     The register never owns the tab: a press writes body.tab (the body persists it).
//   • Replay keeps the preset as a quiet chip at the right of the tab row; it opens an inline list under the
//     row (never a menu).
//   • the cast (surface "cast"): always this register in Replay; an auto-closing cast (interactive false)
//     has no tab row but a running head (the shot on the left, the view and preset on the right), takes no
//     clicks, and is zoomed as a WHOLE for viewing distance — 1 to 1.6, from an em estimate of the view's
//     rows (data), never measured from its own layout, never below compact.
//   • each view is its own named Flickable (chShotFlick, chSessionFlick, chRelationsFlick, chKindsFlick),
//     built on first show and never torn down, so scroll and hover survive a republish; the panel saves and
//     restores their scroll (SessionMode.chartsNav).
//   • one PpChRoles and one PpBrush, declared last. The Brush's shot id is the ORDINAL everywhere.
//   • demoState (renders only, never in the app): the interaction states listed in `demoStates`; the ones that
//     change a reader's choice are applied by the body WITHOUT persisting them.
// objectNames: chRegister, chTabBar (PpTabs' own sdTabBar name is overridden here), chCastHead, chPresetChip.
Item {
    id: root
    objectName: "chRegister"
    property string register: "Analyse"
    property var body: null
    property string view: "shot"
    property string surface: "panel"
    property bool interactive: true
    property string demoState: ""
    property string medium: "screen"
    readonly property var demoStates: ["hoverShot12", "hover", "picker", "againstPicker", "upPicker", "acrossPicker"]
    readonly property bool replay: register === "Replay"
    readonly property bool cast: surface === "cast"
    readonly property bool castHeadOnly: cast && !interactive
    // an unknown tab (a "wrist" saved by the retired Wrist tab) reads This shot (the body keeps the saved tab)
    readonly property string shown: replay && view === "kinds" ? "session"
                                  : (["shot", "session", "relations", "kinds"].indexOf(view) >= 0 ? view : "shot")

    // ── demo: the register-level states (hover, an open picker); the body does the choice-level ones ──
    property real demoHoverMs: NaN
    property string demoPicker: ""
    property string demoRelPicker: ""
    property string _lastDemo: ""
    onDemoStateChanged: Qt.callLater(applyDemo)
    function applyDemo() {
        if (demoState === "" && _lastDemo === "") return
        _lastDemo = demoState
        br.clear()
        demoHoverMs = NaN; demoPicker = ""; demoRelPicker = ""; presetOpen = false
        for (const t of String(demoState).split("+")) {
            if (t.indexOf("hoverShot") === 0) br.hoveredShot = Number(t.slice(9))
            else if (t === "hover") demoHoverMs = -40
            else if (t === "picker") { if (root.replay) presetOpen = true; else demoPicker = "preset" }
            else if (t === "againstPicker") demoPicker = "against"
            else if (t === "upPicker") demoRelPicker = "up"
            else if (t === "acrossPicker") demoRelPicker = "across"
        }
        if (body) body.applyDemo(demoState, br)
    }

    // ── the chosen tab goes to the body ──
    function pickView(key) { if (!root.interactive || !root.body) return; root.body.choose("tab", key) }
    property bool presetOpen: false
    readonly property var tc: body ? body.tChoices : ({})

    readonly property var tabDefs: {
        const t = [{ key: "shot", label: "This shot", count: !replay && body && body.rShot && body.rShot.available ? body.rShot.apartCount : undefined },
                   { key: "session", label: "Session", count: !replay && body && body.tSession && body.tSession.available ? body.tSession.n : undefined },
                   { key: "relations", label: "Relations" }]
        if (!replay) t.push({ key: "kinds", label: "Kinds" })
        return t
    }

    // ═══ the surface, zoomed as a whole on the cast ═══
    // (This shot's wrist section, when drawn, adds its profile rows: ~9 em each)
    readonly property real castEm: (shown === "shot" ? 14 + 5.5 * Math.max(1, body && body.tShot && body.tShot.rows ? body.tShot.rows.length : 1)
                                         + (body && body.wristShotShown && body.wShot.profile ? 6 + 9 * (body.wShot.profile.rows || []).length : 0)
                                  : shown === "session" ? 12 + 5.5 * Math.max(1, body && body.tSession && body.tSession.rows ? body.tSession.rows.length : 1)
                                  : 30)
    readonly property real zoom: cast ? Math.max(1, Math.min(1.6, width / (42 * ro.em), height / ((castEm + 4) * ro.em))) : 1

    component ViewFlick: Flickable {
        id: vf
        property string key: ""
        property Component view: null
        readonly property bool on: root.shown === key
        readonly property var content: ld.item
        property bool built: false
        onOnChanged: if (on) built = true
        Component.onCompleted: if (on) built = true
        visible: on
        anchors { top: under.bottom; left: surfaceItem.left; right: surfaceItem.right; bottom: surfaceItem.bottom; topMargin: ro.u(10) }
        contentWidth: width; contentHeight: (ld.item ? ld.item.height : 0) + ro.u(20)
        clip: true; boundsBehavior: Flickable.StopAtBounds
        interactive: root.interactive && contentHeight > height
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Loader { id: ld; active: vf.built; width: vf.width - ro.u(8); sourceComponent: vf.view }
    }

    Item {
        id: surfaceItem
        width: root.width / root.zoom; height: root.height / root.zoom
        scale: root.zoom; transformOrigin: Item.TopLeft

        // the auto-closing cast's running head, in the tab row's place
        Item {
            id: castHead
            objectName: "chCastHead"
            visible: root.castHeadOnly
            width: parent.width; height: visible ? ro.u(28) : 0
            Text { anchors.verticalCenter: parent.verticalCenter
                   text: root.body && root.body.rStatus ? (root.body.rStatus.kicker || "") : ""; font: ro.fBody; color: ro.ink }
            Row {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: ro.u(8)
                Text { text: root.shown === "shot" ? "This shot" : root.shown === "session" ? "Session" : "Relations"; font: ro.fSmall; color: ro.ink3 }
                Text { text: "·"; font: ro.fSmall; color: ro.ink3 }
                Text { text: root.tc.presetTitle || ""; font: ro.fSmall; color: ro.ink3 }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ro.hw; color: ro.hair }
        }
        PpTabs {
            id: tabs
            objectName: "chTabBar"
            visible: !root.castHeadOnly
            anchors.top: castHead.bottom
            width: parent.width; height: visible ? implicitHeight : 0
            roles: ro; brush: br
            current: root.shown
            interactive: root.interactive
            // the preset as a quiet chip at the right of the tab row, in both registers: Analyse also offers it under
            // Controls, but a folded Controls hid the only way to change it (Mark, 16 Oct)
            chip: root.tc.presetTitle || ""
            chipOn: root.presetOpen
            chipObjectName: "chPresetChip"
            onChipActivated: root.presetOpen = !root.presetOpen
            tabs: root.tabDefs
            onTabActivated: (k) => root.pickView(k)
        }
        Column {
            id: under
            anchors.top: tabs.bottom
            width: parent.width
            PpInlinePicker {
                objectName: "chPresetPicker"
                visible: root.presetOpen && !root.castHeadOnly
                open: visible; width: Math.min(under.width, ro.u(380))
                roles: ro; interactive: root.interactive
                title: "Metrics to trace"
                items: visible ? (root.tc.presets || []).map(function (p) { return { id: p.id, label: p.label, note: String(p.count) } }) : []
                current: root.tc.preset || ""
                clearLabel: root.replay ? "The default for Replay" : "The default for Analyse"
                itemObjectName: "chPresetOption"; clearObjectName: "chPresetDefault"
                onPicked: (id) => { if (root.body) root.body.choose("preset", id); root.presetOpen = false }
            }
        }
        ViewFlick { id: shotFlick; objectName: "chShotFlick"; key: "shot"; view: shotC }
        ViewFlick { id: sessionFlick; objectName: "chSessionFlick"; key: "session"; view: sessionC }
        ViewFlick { id: relationsFlick; objectName: "chRelationsFlick"; key: "relations"; view: relationsC }
        ViewFlick { id: kindsFlick; objectName: "chKindsFlick"; key: "kinds"; view: kindsC }
    }
    // the auto-closing cast takes no clicks, no hovers, no wheel
    MouseArea {
        anchors.fill: parent; z: 1000
        visible: root.cast && !root.interactive; enabled: visible
        hoverEnabled: true; acceptedButtons: Qt.AllButtons; preventStealing: true
        onWheel: (wheel) => wheel.accepted = true
    }

    Component { id: shotC
        PpChartsShotView { roles: ro; brush: br; body: root.body; reg: root.register; interactive: root.interactive; cast: root.cast
                           pageH: shotFlick.height; demoHoverMs: isFinite(root.demoHoverMs) ? root.demoHoverMs : undefined; openPicker: root.demoPicker } }
    Component { id: sessionC
        PpChartsSessionView { roles: ro; brush: br; body: root.body; reg: root.register; interactive: root.interactive; cast: root.cast
                              pageH: sessionFlick.height } }
    Component { id: relationsC
        PpChartsRelationsView { roles: ro; brush: br; body: root.body; reg: root.register; interactive: root.interactive; cast: root.cast
                                pageH: relationsFlick.height; openPicker: root.demoRelPicker } }
    Component { id: kindsC
        PpChartsKindsView { roles: ro; brush: br; body: root.body; reg: root.register; interactive: root.interactive; cast: root.cast
                            pageH: kindsFlick.height } }

    // ── the shared objects, declared LAST so they are destroyed last ──
    PpChRoles { id: ro; medium: root.medium }
    PpBrush { id: br; pickedShot: root.body ? root.body.pickedOrdinal : -1 }
}
