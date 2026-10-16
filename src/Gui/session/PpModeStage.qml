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
import PinPointStudio

Item {
    id: stage

    property Component cameraDelegate:      null
    property Component chartsDelegate:      null
    property Component sessionDiagnosticsDelegate: null
    property Component launchMonitorDelegate: null
    property Component tableDelegate:       null
    // "Readings and their limits" (trust/PpTrustPanel.qml): why a reading was refused, bounded or withheld
    property Component trustDelegate:       null
    property Component markupDelegate:      null
    // The 3-D swing (swing3d/). Its delegate is only a SLOT: the View3D itself lives in the
    // screen's SwingViz3DHost and is lent to whichever slot is showing — never rebuilt here.
    property Component swing3dDelegate:     null

    // Layout resolves on the active session MODE, not the session type.
    readonly property string arrangement: ViewLayout.arrangementFor(SessionMode.mode)

    // ⚠ THE ORDER IS THE PRIORITY: camera leads, session diagnostics follows it.
    //
    // This list is read three times — the tab strip's order, the split row's order, and which
    // panel is DOMINANT in the stage arrangement (active[0] takes 62% of the width). So one
    // ordering decides where the eye goes in every arrangement, which is why it is decided
    // here and not in three places.
    //
    // Diagnostics led for a while (Aug–Oct 2026) because its old cards needed the dominant slot to
    // be readable at all. The typeset panel (design system §15) lays itself out for whatever width
    // it is given — full, stage, half or third — so it no longer needs to lead, and the camera,
    // which is what the golfer watches, takes the first place back (Mark, 11 Oct 2026).
    //
    // `title` is the card's Micro heading. `tone` is a ROLE, resolved by _toneFor. EVERY card is
    // quiet (design system §15, Mark, 11 Oct 2026: "Quiet tone please"): colour on the stage means
    // "outside its corridor" and nothing else, so a frame never speaks for what is inside it — a
    // warn-toned title over a 100/100 swing read as a fault. The "warn" role is kept for a surface
    // that genuinely is a warning. A role rather than the colour itself, because a colour here would
    // rebuild this list — and so every panel on the stage — on a theme change.
    // Theme.caps on the titles is the exception that is allowed: it changes only when the
    // aesthetic moves into or out of one that sets headings in capitals, not on every theme.
    readonly property var _defs: [
        { key: "camera",        label: qsTr("Camera"),                title: Theme.caps(qsTr("Camera")),
          tone: "quiet", comp: cameraDelegate },
        { key: "sessionDiagnostics", label: qsTr("Session diagnostics"), title: Theme.caps(qsTr("Session diagnostics")),
          tone: "quiet", comp: sessionDiagnosticsDelegate },
        { key: "swing3d",       label: qsTr("3-D swing"),             title: Theme.caps(qsTr("3-D swing")),
          tone: "quiet", comp: swing3dDelegate },
        { key: "launchMonitor", label: qsTr("Launch monitor"),        title: Theme.caps(qsTr("Launch monitor")),
          tone: "quiet", comp: launchMonitorDelegate },
        { key: "charts",        label: qsTr("Charts"),                title: Theme.caps(qsTr("Charts")),
          tone: "quiet", comp: chartsDelegate },
        { key: "trust",         label: qsTr("Trust"),                 title: Theme.caps(qsTr("Readings and their limits")),
          tone: "quiet", comp: trustDelegate },
        { key: "table",         label: qsTr("Table"),                 title: Theme.caps(qsTr("Table")),
          tone: "quiet", comp: tableDelegate },
        { key: "markup",        label: qsTr("Markup"),                title: Theme.caps(qsTr("Markup")),
          tone: "quiet", comp: markupDelegate }
    ]
    function _toneFor(role) { return role === "warn" ? Theme.colorWarn : Theme.colorText3 }

    // ordered; enabled AND actually wired by the host screen. A panel a screen does not provide a
    // delegate for (e.g. "markup" on a screen that never wires it) is simply omitted rather
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
    // A panel whose aside is a control (asideActive) is told the card hosts it, so it drops its own copy.
    function _asideActive(panel) { return !!panel && panel.asideActive === true }
    function _asideOn(panel)     { return !!panel && panel.asideOn === true }
    function _unframe(panel) {
        if (panel && panel.framed !== undefined) panel.framed = false
        if (panel && panel.asideHosted !== undefined) panel.asideHosted = true
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
        asideActive: stage._asideActive(panelLoader.item)
        asideOn: stage._asideOn(panelLoader.item)
        onAsideClicked: if (panelLoader.item && panelLoader.item.activateAside) panelLoader.item.activateAside()
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

    // ── SIZES — the separators the user drags ────────────────────────────────
    // Split and stage pack their cards at explicit positions computed from per-panel WEIGHTS, so a
    // proportion is exact (a RowLayout with fillWidth does not honour one). The weights are stored
    // per mode and arrangement by ViewLayout.setSizes, keyed by panel key, and normalised over the
    // panels that are on — so turning one panel on or off keeps the others' proportions.
    //
    // The sizes are NOT the Repeaters' model: a resize only moves x/width/height, never rebuilds a
    // card (the camera and the 3-D swing are expensive to recreate). During a drag the sizes live in
    // the _drag* properties below and nothing is persisted; the one write happens on release (a
    // settings write mid-gesture is what broke the crop drag, Aug 2026). Double-click a separator
    // to put that arrangement back to its default.
    //
    // A separator is a MouseArea inside the gap between two cards, and draws nothing: the only cue
    // is the split cursor (Mark, 12 Oct 2026: no line, handle or highlight).
    readonly property int  _minW: Theme.sp(220)    // no card narrower than this…
    readonly property int  _minH: Theme.sp(140)    // …or a side card shorter than this
    readonly property int  _hit:  Math.max(8, Math.min(stage._gap, 10))   // within the gap
    // The stage arrangement's default: the dominant card and the side column share the row 62 : 30,
    // as the RowLayout they replaced did (its preferred widths 62 % and 30 % of the stage, the room
    // left over shared in the same proportion).
    readonly property real _stageMainDefault: 0.62 / 0.92

    // The stored sizes for this mode (one read per change: the map is a deep copy).
    readonly property var _sizes: ViewLayout.sizesFor(SessionMode.mode)
    property var  _dragSplit: null   // { key: px } while a split separator is held
    property real _dragMain:  -1     // the dominant card's width in px while its separator is held
    property var  _dragSide:  null   // { key: px } while a side separator is held

    function _weights(keys, stored) {
        return keys.map(function (k) {
            const w = stored ? Number(stored[k]) : NaN
            return (isFinite(w) && w > 0) ? w : 1
        })
    }

    // Shares `avail` px in proportion to `ws`, holding every share at `mn` or more (a share that
    // would fall short is pinned at the minimum and the rest re-shared). When even the minima do
    // not fit, the shares are even.
    function _distribute(avail, ws, mn) {
        const n = ws.length
        if (n === 0) return []
        avail = Math.max(0, avail)
        if (avail < n * mn) mn = avail / n
        const pinned = ws.map(function () { return false })
        const out = ws.map(function () { return 0 })
        for (let pass = 0; pass <= n; ++pass) {
            let rest = avail, wsum = 0, free = 0
            for (let i = 0; i < n; ++i) { if (pinned[i]) rest -= mn; else { wsum += ws[i]; free++ } }
            let changed = false
            for (let i = 0; i < n; ++i) {
                if (pinned[i]) { out[i] = mn; continue }
                out[i] = wsum > 0 ? rest * ws[i] / wsum : rest / Math.max(1, free)
                if (out[i] < mn - 1e-6) { pinned[i] = true; changed = true }
            }
            if (!changed) break
        }
        return out
    }

    // Sizes → whole-pixel spans [{ pos, size }], laid end to end with `gap` between.
    function _spans(sizes, gap) {
        const out = []
        let at = 0
        for (let i = 0; i < sizes.length; ++i) {
            const a = Math.round(at), b = Math.round(at + sizes[i])
            out.push({ pos: a, size: b - a })
            at += sizes[i] + gap
        }
        return out
    }

    // Weights to store: the active panels' px, normalised to a mean of 1, merged over what was
    // stored so a panel that is off keeps its weight for when it comes back.
    function _normalised(keys, px, stored) {
        const out = {}
        if (stored) for (const k in stored) out[k] = stored[k]
        let sum = 0
        for (let i = 0; i < px.length; ++i) sum += px[i]
        if (sum <= 0) return out
        for (let i = 0; i < keys.length; ++i) out[keys[i]] = Math.round(px[i] / (sum / keys.length) * 1e4) / 1e4
        return out
    }

    // The new pair of sizes when the shared edge of a and b moves by d, clamped at `mn` each.
    function _movePair(a, b, d, mn) {
        const pair = a + b
        if (pair < 2 * mn) return [a, b]
        const na = Math.max(mn, Math.min(pair - mn, a + d))
        return [na, pair - na]
    }

    function _resetSizes(arr) {
        stage._dragSplit = null; stage._dragMain = -1; stage._dragSide = null
        ViewLayout.setSizes(SessionMode.mode, arr, null)
    }

    // An invisible separator: the split cursor on hover and while held, nothing drawn. `vertical`
    // means a vertical bar (drag left/right). The host gives it a press/move/release/reset.
    component Separator: MouseArea {
        id: sep
        property bool vertical: true
        property Item frame: null           // the coordinate space the drag is measured in
        property real _start: 0
        signal begin()
        signal moved(real delta)
        signal end()
        signal reset()
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: sep.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
        function _at(mouse) {
            const p = sep.mapToItem(sep.frame, mouse.x, mouse.y)
            return sep.vertical ? p.x : p.y
        }
        onPressed: function (mouse) { sep._start = sep._at(mouse); sep.begin() }
        onPositionChanged: function (mouse) { if (sep.pressed) sep.moved(sep._at(mouse) - sep._start) }
        onReleased: sep.end()
        onCanceled: sep.end()
        onDoubleClicked: sep.reset()
    }

    // ── SPLIT — a row, even until the user drags ─────────────────────────────
    Item {
        id: splitBox
        anchors.fill: parent; anchors.margins: Theme.gap(10)
        visible: stage.arrangement === "split" && stage.active.length > 0
        readonly property var keys: stage.arrangement === "split" ? stage.active.map(function (d) { return d.key }) : []
        readonly property var sizes: stage._distribute(
            splitBox.width - Math.max(0, splitBox.keys.length - 1) * stage._gap,
            stage._dragSplit ? splitBox.keys.map(function (k) { return stage._dragSplit[k] || 1 })
                             : stage._weights(splitBox.keys, stage._sizes.split),
            stage._minW)
        readonly property var spans: stage._spans(splitBox.sizes, stage._gap)
        property var _startSizes: []

        Repeater {
            model: stage.arrangement === "split" ? stage.active : []
            delegate: PanelCard {
                required property var modelData
                required property int index
                x: splitBox.spans[index] ? splitBox.spans[index].pos : 0
                width: splitBox.spans[index] ? splitBox.spans[index].size : 0
                height: splitBox.height
                def: modelData
                placeholder: placeholderComp
            }
        }
        Repeater {
            model: stage.arrangement === "split" ? Math.max(0, stage.active.length - 1) : 0
            delegate: Separator {
                id: splitSep
                required property int index
                objectName: "stageSep_split_" + index
                frame: splitBox
                readonly property var _s: splitBox.spans[index]
                x: _s ? Math.round(_s.pos + _s.size + (stage._gap - stage._hit) / 2) : 0
                width: stage._hit; height: splitBox.height
                onBegin: splitBox._startSizes = splitBox.sizes.slice()
                onMoved: function (delta) {
                    const st = splitBox._startSizes, i = splitSep.index
                    if (i + 1 >= st.length) return
                    const pr = stage._movePair(st[i], st[i + 1], delta, stage._minW)
                    const d = {}
                    for (let j = 0; j < splitBox.keys.length; ++j) d[splitBox.keys[j]] = st[j]
                    d[splitBox.keys[i]] = pr[0]; d[splitBox.keys[i + 1]] = pr[1]
                    stage._dragSplit = d
                }
                onEnd: {
                    if (!stage._dragSplit) return
                    const px = splitBox.keys.map(function (k) { return stage._dragSplit[k] })
                    ViewLayout.setSizes(SessionMode.mode, "split",
                                        stage._normalised(splitBox.keys, px, stage._sizes.split))
                    stage._dragSplit = null
                }
                onReset: stage._resetSizes("split")
            }
        }
    }

    // ── STAGE — first panel dominant, rest in a side column ──────────────────
    // Default: the dominant card and the side column share the row 62 : 30 (_stageMainDefault).
    Item {
        id: stageBox
        anchors.fill: parent; anchors.margins: Theme.gap(10)
        visible: stage.arrangement === "stage" && stage.active.length > 0
        readonly property bool hasSide: stage.arrangement === "stage" && stage.active.length > 1
        readonly property real avail: stageBox.width - stage._gap
        readonly property real mainW: {
            if (!stageBox.hasSide) return stageBox.width
            let w
            if (stage._dragMain >= 0) w = stage._dragMain
            else {
                const st = stage._sizes.stage, f = st ? Number(st.main) : NaN
                w = ((isFinite(f) && f > 0 && f < 1) ? f : stage._stageMainDefault) * stageBox.avail
            }
            if (stageBox.avail < 2 * stage._minW) return Math.round(Math.max(0, stageBox.avail) / 2)
            return Math.round(Math.max(stage._minW, Math.min(stageBox.avail - stage._minW, w)))
        }
        readonly property var sideKeys: stageBox.hasSide ? stage.active.slice(1).map(function (d) { return d.key }) : []
        readonly property var sideSizes: {
            const st = stage._sizes.stage
            return stage._distribute(
                stageBox.height - Math.max(0, stageBox.sideKeys.length - 1) * stage._gap,
                stage._dragSide ? stageBox.sideKeys.map(function (k) { return stage._dragSide[k] || 1 })
                                : stage._weights(stageBox.sideKeys, st ? st.side : null),
                stage._minH)
        }
        readonly property var sideSpans: stage._spans(stageBox.sideSizes, stage._gap)
        property real _startMain: 0
        property var  _startSide: []

        PanelCard {
            x: 0; y: 0
            width: stageBox.mainW; height: stageBox.height
            def: (stage.arrangement === "stage" && stage.active.length > 0) ? stage.active[0] : null
            placeholder: placeholderComp
        }
        Item {
            id: sideCol
            visible: stageBox.hasSide
            x: stageBox.mainW + stage._gap
            width: Math.max(0, stageBox.width - sideCol.x); height: stageBox.height
            Repeater {
                model: (stage.arrangement === "stage" && stage.active.length > 1)
                       ? stage.active.slice(1) : []
                delegate: PanelCard {
                    required property var modelData
                    required property int index
                    y: stageBox.sideSpans[index] ? stageBox.sideSpans[index].pos : 0
                    height: stageBox.sideSpans[index] ? stageBox.sideSpans[index].size : 0
                    width: sideCol.width
                    def: modelData
                    placeholder: placeholderComp
                }
            }
            Repeater {
                model: (stage.arrangement === "stage" && stage.active.length > 2) ? stage.active.length - 2 : 0
                delegate: Separator {
                    id: sideSep
                    required property int index
                    objectName: "stageSep_side_" + index
                    vertical: false
                    frame: stageBox
                    readonly property var _s: stageBox.sideSpans[index]
                    y: _s ? Math.round(_s.pos + _s.size + (stage._gap - stage._hit) / 2) : 0
                    height: stage._hit; width: sideCol.width
                    onBegin: stageBox._startSide = stageBox.sideSizes.slice()
                    onMoved: function (delta) {
                        const st = stageBox._startSide, i = sideSep.index
                        if (i + 1 >= st.length) return
                        const pr = stage._movePair(st[i], st[i + 1], delta, stage._minH)
                        const d = {}
                        for (let j = 0; j < stageBox.sideKeys.length; ++j) d[stageBox.sideKeys[j]] = st[j]
                        d[stageBox.sideKeys[i]] = pr[0]; d[stageBox.sideKeys[i + 1]] = pr[1]
                        stage._dragSide = d
                    }
                    onEnd: {
                        if (!stage._dragSide) return
                        const px = stageBox.sideKeys.map(function (k) { return stage._dragSide[k] })
                        const old = stage._sizes.stage || {}
                        const out = { side: stage._normalised(stageBox.sideKeys, px, old.side) }
                        if (old.main !== undefined) out.main = old.main
                        ViewLayout.setSizes(SessionMode.mode, "stage", out)
                        stage._dragSide = null
                    }
                    onReset: stage._resetSizes("stage")
                }
            }
        }
        Separator {
            objectName: "stageSep_main"
            visible: stageBox.hasSide
            frame: stageBox
            x: Math.round(stageBox.mainW + (stage._gap - stage._hit) / 2)
            width: stage._hit; height: stageBox.height
            onBegin: stageBox._startMain = stageBox.mainW
            onMoved: function (delta) {
                stage._dragMain = Math.max(stage._minW, Math.min(stageBox.avail - stage._minW, stageBox._startMain + delta))
            }
            onEnd: {
                if (stage._dragMain < 0) return
                const old = stage._sizes.stage || {}
                const out = { main: Math.round(stage._dragMain / Math.max(1, stageBox.avail) * 1e4) / 1e4 }
                if (old.side !== undefined) out.side = old.side
                ViewLayout.setSizes(SessionMode.mode, "stage", out)
                stage._dragMain = -1
            }
            onReset: stage._resetSizes("stage")
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
