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

// THE CHARTS STAGE PANEL: this swing's metric curves and the session behind them, in two voices — Traces (the
// instrument) and Relations (the workbench) — across four views: This shot · Session · Relations · Kinds
// (docs/design/charts_panel_traces_relations.md; build/charts-build/CONTRACT.md D1–D9 and its Addendum). The wrist
// read is no tab of its own: This shot and Session add a "Wrist" section when a shot carries a wrist reading and a
// wrist metric is on show (docs/design/wrist_motion_into_charts.md §4; PpChartsBody.wristShotShown).
//
// THIS FILE IS THE WIRING AND NOTHING ELSE (the Session diagnostics pattern, INVENTORY §7). Everything the
// panel LOOKS like is PpChartsBody, which takes the presenters as sources and owns none of them. Here:
//   • THE MODEL AND THE PRESENTERS ARE INSTANTIATED, NOT REACHED FOR: one SessionSeriesModel and the three
//     presenters (ChartsTracesPresenter, ChartsRelationsPresenter, ChartsWristPresenter), each with a private worker thread. The
//     model is fed exactly as its header says: the session folder (the same binding as the diagnostics
//     panel), ShotProcessor.shotProcessed → ingestShot, the carousel's re-analysis → regradeShot, and the
//     diagnostics ledger written (SessionMode.sessionDiagnostics busy → false) → refreshDiagnostics.
//   • THE _screenActive GATE STAYS (INVENTORY F41, cbb63aae): all four session screens live in one
//     StackLayout, and both hosts (ScreenSessionMode, ScreenWrist) carry this panel. An invisible screen's
//     presenters are fed nothing: the picked swing, its detail, the span and the playhead are copied in only
//     while this screen is the visible one, so a replay on another screen costs this one no recompute.
//   • THE READER'S CHOICES PERSIST AS TODAY: AppSettings chartPrefs per screen type × mode (key base
//     "<sessionType>:<mode>:", app_settings.h chartPrefs), restored on creation and on every key-base change,
//     written ON INTERACTION only (the body's choiceMade), so Replay and Analyse each remember their own chart
//     across restarts. The folds keep their saved state in sectionCollapse under the same base. Keys:
//       today's   preset · split (small multiples ↔ overlay) · dots · cursor
//       wrist     wristAgainst · wristCurves · wristArchetype           (the Wrist sections; a saved wristScope is ignored)
//       new       view · metricsOff · window · customStartUs · customEndUs · band · facet · against ·
//                 difference · up · across · relFacet
//       folds     controls · summary · sequence            (sectionCollapse)
//     A saved "Custom" from today's panel restores as its base preset with its switched-off series.
//   • THE READER'S PLACE (transient, per session) is SessionMode.chartsNav: { sessionDir, openId, scroll }.
//     The stage rebuilds its panels on every Capture↔Replay flip, so the place is saved as it changes and put
//     back on build, stamped with the session folder (a different session starts at the defaults).
//     chartsNav arrives with app build 2: every read and write goes through _nav()/_setNav(), which do nothing
//     while SessionMode has no such property.
//   • A SHOT PICKED ON A FIGURE goes to the carousel's own click path (_pickShot, as the diagnostics panel's).
import QtQuick
import PinPointStudio

Item {
    id: root

    // Set by PpModeStage only on the muted placeholder path; the panel titles itself.
    property string title: qsTr("Charts")
    // The screen this panel sits on (SessionController.Swing / Wrist …): the persistence key and the
    // _screenActive gate only — the analysis never branches on it.
    property int sessionType: -1
    // The tab and the preset this panel opens on until the reader picks one (a saved "view" / "preset" wins).
    // The HOST says them, so the panel never branches on the screen: the Wrist screen opens This shot on the
    // "Wrist & forearm" preset (a catalogue group, the traces presenter's preset id), so its wrist section shows.
    // A saved "wrist" (the retired Wrist tab) reads This shot.
    property string defaultTab: "shot"
    property string defaultPreset: ""
    property alias framed: body.framed
    readonly property string cardAside: body.cardAside
    // the card's aside IS the preset selector (PpStageCard asideActive): set by the stage, the tab row drops its chip
    property alias asideHosted: body.presetInHeading
    readonly property bool asideActive: body.interactive && body.cardAside !== ""
    readonly property bool asideOn: body.presetPickerOpen
    function activateAside() { body.togglePresetPicker() }
    property alias interactive: body.interactive
    property alias surface: body.surface
    property alias demoState: body.demoState
    property alias tab: body.tab
    readonly property string view: body.view
    readonly property alias chBody: body
    // Whether this panel keeps the reader's place across its own rebuilds (off for a cast).
    property bool remembersNav: true

    // ── the session, its model and the presenters ──
    readonly property bool _screenActive: navController.currentIndex === root.sessionType + 1
    readonly property string sessionDir:
        (sessionReviewController.reviewActive && sessionReviewController.activeSessionId !== "")
            ? sessionReviewController.activeSessionId
            : shotProcessor.activeSessionDir !== "" ? shotProcessor.activeSessionDir : sessionReviewController.liveSessionDir
    function _folder(p) { return String(p || "").replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "") }
    function _pointAtSession() { if (_folder(sessionDir) !== _folder(seriesModel.sessionDir)) seriesModel.activateSession(sessionDir) }
    onSessionDirChanged: _pointAtSession()

    SessionSeriesModel { id: seriesModel }

    // what the presenters are told of the replay: copied only while this screen is the visible one
    property string _swingDir: ""
    property var _detail: ({})
    property real _spanUs: -1
    property real _playUs: -1
    function _syncPick() {
        if (!_screenActive) return
        _swingDir = shotReplay.active ? shotReplay.swingDir : ""
        _detail = shotReplay.active && shotReplay.analysisDetail ? shotReplay.analysisDetail : ({})
        _spanUs = shotReplay.active ? shotReplay.endUs - shotReplay.startUs : -1
        _playUs = shotReplay.active ? shotReplay.positionUs : -1
    }
    on_ScreenActiveChanged: _syncPick()
    Connections {
        target: shotReplay
        function onActiveChanged()   { root._syncPick() }
        function onSpanChanged()     { if (root._screenActive) root._spanUs = shotReplay.active ? shotReplay.endUs - shotReplay.startUs : -1 }
        function onPositionChanged() { if (root._screenActive) root._playUs = shotReplay.active ? shotReplay.positionUs : -1 }
    }
    readonly property string _register: body.register === "Analyse" ? "analyse" : "replay"

    ChartsTracesPresenter {
        id: tracesPresenter
        model: seriesModel
        pickedSwingDir: root._swingDir
        pickedDetail: root._detail
        register: root._register
        view: body.shownView === "session" ? "session" : "shot"
        preset: body.preset
        metricsOff: body.metricsOff
        window: body.window
        customStartUs: body.customStartUs
        customEndUs: body.customEndUs
        spanUs: root._spanUs
        band: body.band
        facet: body.facet
        against: body.against
        difference: body.difference
        playheadUs: root._playUs
        // the ★ reference swing for Against "reference" (any session; the wrist section's ★ link writes it)
        referenceSwingDir: appSettings.wristReferenceSwingDir
    }
    ChartsRelationsPresenter {
        id: relationsPresenter
        model: seriesModel
        pickedSwingDir: root._swingDir
        pickedDetail: root._detail
        register: root._register
        family: body.preset
        upId: body.upId
        acrossId: body.acrossId
        facetId: body.relFacet
        openId: body.openId
        playheadUs: root._playUs
    }
    // the Wrist sections (build/wrist-tab, CONTRACT I4): their findings and score are the wrist-domain diagnostics
    // conditions, read off the model's snapshot — no diagnostics object is an input (the model re-joins the
    // ledger on refreshDiagnostics below). Its scope follows the view on show.
    ChartsWristPresenter {
        id: wristPresenter
        model: seriesModel
        pickedSwingDir: root._swingDir
        pickedDetail: root._detail
        register: root._register
        playheadUs: root._playUs
        scope: body.shownView === "session" ? "session" : "shot"
        against: body.wristAgainst
        curvesBehind: body.wristCurves
        archetype: body.wristArchetype
        referenceSwingDir: appSettings.wristReferenceSwingDir
    }

    Connections {
        target: shotProcessor
        function onShotProcessed(shotId, swingDir) { seriesModel.ingestShot(shotId, swingDir) }
    }
    Connections {
        target: reanalysisController
        function onReanalysed(swingDir) { seriesModel.regradeShot(swingDir) }
    }
    // the diagnostics ledger was written: the model re-joins its rows (it posts the read itself)
    Connections {
        target: SessionMode.sessionDiagnostics
        ignoreUnknownSignals: true
        function onBusyChanged() { if (SessionMode.sessionDiagnostics && !SessionMode.sessionDiagnostics.busy) seriesModel.refreshDiagnostics() }
    }

    // ── persistence: AppSettings chartPrefs per screen type × mode, written on interaction only ──
    readonly property string _keyBase: root.sessionType + ":" + SessionMode.mode + ":"
    on_KeyBaseChanged: { _restorePrefs(); _restoreFolds() }
    readonly property var _prefKey: ({ tab: "view", overlay: "split", showDots: "dots", showCursor: "cursor", upId: "up", acrossId: "across" })
    function _restorePrefs(keepTab) {
        const m = appSettings.chartPrefs || {}, b = _keyBase
        const has = function (k) { return m[b + k] !== undefined && m[b + k] !== null }
        const str = function (k, d) { return has(k) ? String(m[b + k]) : d }
        if (!keepTab) { const t = str("view", root.defaultTab); body.tab = t === "wrist" ? "shot" : t }
        // today's "Custom" (a hand-picked subset) is its base preset with the switched-off series
        if (str("preset", "") === "Custom") {
            body.preset = str("presetBase", root.defaultPreset)
            const s = has("series") && typeof m[b + "series"] === "object" ? m[b + "series"] : {}
            body.metricsOff = Object.keys(s).filter(function (k) { return s[k] === false })
        } else {
            body.preset = str("preset", root.defaultPreset)
            body.metricsOff = has("metricsOff") ? (m[b + "metricsOff"] || []).slice() : []
        }
        body.window = str("window", "auto")
        body.customStartUs = has("customStartUs") ? Number(m[b + "customStartUs"]) : -1
        body.customEndUs = has("customEndUs") ? Number(m[b + "customEndUs"]) : -1
        body.band = str("band", "middle")
        body.facet = str("facet", "")
        body.against = str("against", "none")
        body.difference = has("difference") ? m[b + "difference"] === true : false
        body.overlay = has("split") ? m[b + "split"] === false : false
        body.showDots = has("dots") ? m[b + "dots"] === true : true
        body.showCursor = has("cursor") ? m[b + "cursor"] === true : true
        body.upId = str("up", "")
        body.acrossId = str("across", "")
        body.relFacet = str("relFacet", "")
        body.wristAgainst = str("wristAgainst", "address")
        body.wristCurves = has("wristCurves") ? m[b + "wristCurves"] === true : false
        body.wristArchetype = has("wristArchetype") ? Math.max(-1, Math.min(2, Number(m[b + "wristArchetype"]) || 0)) : -1
    }
    function _persist(name, value) {
        if (root.sessionType < 0 || !root.remembersNav) return
        const key = _prefKey[name] || name
        const v = name === "overlay" ? !value : value
        const mm = Object.assign({}, appSettings.chartPrefs || {})
        mm[_keyBase + key] = v
        if (name === "preset") { mm[_keyBase + "presetBase"] = value; mm[_keyBase + "metricsOff"] = []; body.metricsOff = [] }
        appSettings.chartPrefs = mm
    }
    function _restoreFolds() {
        const m = appSettings.sectionCollapse || {}, b = _keyBase
        body.collapsed = { controls: m[b + "controls"] === true, summary: m[b + "summary"] === true, sequence: m[b + "sequence"] === true }
    }
    function _persistFold(name, val) {
        if (root.sessionType < 0 || !root.remembersNav) return
        const mm = Object.assign({}, appSettings.sectionCollapse || {})
        mm[_keyBase + name] = val
        appSettings.sectionCollapse = mm
    }

    // ── the reader's place: SessionMode.chartsNav (build 2), guarded so build 1 runs without it ──
    function _nav() { return SessionMode.chartsNav !== undefined ? SessionMode.chartsNav : null }
    function _setNav(v) { if (SessionMode.chartsNav !== undefined) SessionMode.chartsNav = v }
    property bool _navReady: false
    readonly property var _navFlicks: ["chShotFlick", "chSessionFlick", "chRelationsFlick", "chKindsFlick"]
    function _findNamed(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) { const hit = _findNamed(kids[i], name); if (hit) return hit }
        return null
    }
    property var _flickCache: ({})
    function _flick(name) { let f = _flickCache[name]; if (!f) { f = _findNamed(body, name); if (f) _flickCache[name] = f } return f }
    function _watchFlicks() {
        _flickCache = ({})
        for (let i = 0; i < flickWatch.count; ++i) { const c = flickWatch.objectAt(i); c.target = _flick(c.modelData) }
    }
    function _saveNav() {
        if (!_navReady) return
        const scroll = {}
        for (const name of _navFlicks) {
            if (_pendingScroll[name] !== undefined) { scroll[name] = _pendingScroll[name]; continue }
            const f = _flick(name)
            if (f && f.contentY > 0) scroll[name] = f.contentY
        }
        _setNav({ sessionDir: _folder(seriesModel.sessionDir), openId: body.openId, scroll: scroll })
    }
    function _restoreNav() {
        const nav = _nav()
        if (!nav || nav.sessionDir !== _folder(seriesModel.sessionDir)) return
        body.openId = nav.openId || ""
        _pendingScroll = Object.assign({}, nav.scroll || {})
        _scrollTries = {}
        if (Object.keys(_pendingScroll).length > 0) scrollRestore.start()
    }
    property var _pendingScroll: ({})
    property var _scrollTries: ({})
    Timer {
        id: scrollRestore
        interval: 50; repeat: true
        onTriggered: {
            const left = {}
            for (const name in root._pendingScroll) {
                const want = root._pendingScroll[name], f = root._flick(name)
                if (!f || !f.visible) { left[name] = want; continue }
                const tries = (root._scrollTries[name] || 0) + 1
                root._scrollTries[name] = tries
                const most = Math.max(0, f.contentHeight - f.height)
                f.contentY = Math.min(want, most)
                if (most < want && tries < 10) left[name] = want
            }
            root._pendingScroll = left
            if (Object.keys(left).every(function (n) { const f = root._flick(n); return !f || !f.visible })) stop()
        }
    }
    Timer { id: scrollSave; interval: 300; onTriggered: root._saveNav() }
    Instantiator {
        id: flickWatch
        model: root.remembersNav ? root._navFlicks : []
        delegate: Connections {
            required property string modelData
            target: null
            function onMovementStarted() { delete root._pendingScroll[modelData] }
            function onContentYChanged() { if (root._pendingScroll[modelData] === undefined) scrollSave.restart() }
            function onVisibleChanged() { if (root._pendingScroll[modelData] !== undefined) scrollRestore.start() }
        }
    }
    Connections {
        target: body
        function onChoiceMade(name, value) { root._persist(name, value) }
        function onSectionToggled(name, collapsed) { root._persistFold(name, collapsed) }
        function onNavChanged() { root._saveNav() }
        function onDemoReset() { root._restorePrefs(true); root._restoreFolds() }   // a render keeps the view it is on
        function onRegisterItemChanged() { if (root.remembersNav) Qt.callLater(root._watchFlicks) }
        function onViewChanged() { if (root.remembersNav) Qt.callLater(root._watchFlicks) }
    }
    Connections {
        target: seriesModel
        // a different session: the place does not carry over
        function onSessionChanged() { if (root._navReady && root._nav() && root._nav().sessionDir !== root._folder(seriesModel.sessionDir)) body.openId = "" }
    }

    Component.onCompleted: {
        _restorePrefs(); _restoreFolds()
        _pointAtSession()
        _syncPick()
        if (remembersNav) { _restoreNav(); Qt.callLater(_watchFlicks) }
        _navReady = remembersNav
    }

    // ── a shot picked on a figure: the carousel's own click path ──
    Instantiator {
        id: carouselShots
        model: sessionReviewController.activeShots
        delegate: QtObject { required property int shotId; required property string swingDir }
    }
    function _pickShot(swingDir) {
        if (!swingDir) return false
        const want = _folder(swingDir), name = want.substring(want.lastIndexOf("/") + 1)
        for (let i = 0; i < carouselShots.count; ++i) {
            const s = carouselShots.objectAt(i)
            if (!s || !s.swingDir) continue
            const have = _folder(s.swingDir)
            if (have === want || have.substring(have.lastIndexOf("/") + 1) === name) {
                if (SessionMode.mode === SessionMode.analyse) {
                    SessionMode.focusedShotId = s.shotId
                    SessionMode.focusedSwingDir = s.swingDir
                    SessionMode.enterAnalyse()
                } else SessionMode.enterReplay(s.shotId, s.swingDir)
                return true
            }
        }
        return false
    }

    PpChartsBody {
        id: body
        anchors.fill: parent
        tracesP: tracesPresenter
        relationsP: relationsPresenter
        wristP: wristPresenter
        playUs: root._playUs
        spanUs: root._spanUs
        onShotRequested: (dir, id) => root._pickShot(dir)
    }
}
