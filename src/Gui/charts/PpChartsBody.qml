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
import PinPointStudio

// THE CHARTS PANEL'S BODY: the reader's choices and view state, the frame on surfaces that have no stage card
// round them, and the TYPESET REGISTER that draws everything else (design system §15; docs/design/
// charts_panel_traces_relations.md; build/charts-build/STEP2B.md).
//
// IT TAKES THE PRESENTERS AS SOURCES AND OWNS NONE OF THEM. PpChartsPanel owns the SessionSeriesModel and the
// presenters (ChartsTracesPresenter, ChartsRelationsPresenter, ChartsWristPresenter), feeds them, persists what this body
// chooses, and keeps the reader's place; this file binds each presenter output map ONCE (every read of a
// QVariantMap property is a deep copy) and hands the views those bindings: t* (traces), r* (relations) and
// w* (wrist).
//
// THE WRIST READ IS A SECTION, NOT A TAB (Mark, 16 Oct; docs/design/wrist_motion_into_charts.md §4): the standard
// curves first, the domain read added when it is meaningful. wristShotShown / wristSessionShown say when This
// shot / Session add their "Wrist" section: the shot (or some shot of the session) carries a wrist reading AND a
// wrist metric is among the curves on show — a member of the traces' preset that is on (tChoices.metrics[].on)
// whose key is one of the wrist presenter's strip rows (profile.rows[].key, "hm." prefix ignored on both sides).
//
// THE REGISTER IS CHOSEN, NOT THE LAYOUT (§15.6): forceRegister (tests) → surface "cast" → Replay →
// SessionMode: Analyse reads in depth (PpChartsAnalyse); Capture and Replay glance (PpChartsReplay).
// The register is handed `body: root` and reads and drives everything through this API.
//
// THE READER'S CHOICES live here as properties: the panel restores them from AppSettings chartPrefs per screen
// type × mode (as today, INVENTORY F9) and is told of every change made BY THE READER through choiceMade(name,
// value) — never on a property change, so a restore or a construction never clobbers what was saved (MC:153).
// Demo states (renders only) set choices WITHOUT choiceMade, after asking the panel to reset to the saved ones.
//
// THIS API IS THE INTEGRATION SEAM (the panel, the harness overlay, tests and probes reach in): tracesP,
// relationsP, wristP (+ wChoices wShot wSession wMoves wStandfirst wSelected wristAvailable wristOnShow
// wristShotShown wristSessionShown), tab, view, shownView, preset, metricsOff, window, customStartUs/EndUs, band,
// facet, against, difference, overlay, showDots, showCursor, upId, acrossId, relFacet, wristAgainst, wristCurves, wristArchetype, openId, collapsed, askedId, interactive, surface,
// framed, cardAside, register, registerItem, demoState, forceRegister, playUs, spanUs; choose(),
// toggleMetric(), chooseCustom(), chooseUp(), toggleSection(), openReading(), ask(), clearAsk(), pickOrdinal(),
// applyDemo(); choiceMade, sectionToggled, navChanged, demoReset, shotRequested; objectName "chBody".
Item {
    id: root
    objectName: "chBody"

    // ── the sources (owned by the panel) ──
    property QtObject tracesP: null
    property QtObject relationsP: null
    property QtObject wristP: null               // ChartsWristPresenter: the Wrist sections of This shot and Session

    // ── the outputs, each bound ONCE (a QVariantMap read is a deep copy; the views read these) ──
    readonly property var tChoices: tracesP ? tracesP.choices : ({})
    readonly property var tShot: tracesP ? tracesP.thisShot : ({})
    readonly property var tRef: tracesP ? tracesP.reference : ({})
    readonly property var tSession: tracesP ? tracesP.session : ({})
    readonly property var tNow: tracesP ? tracesP.now : ({})
    readonly property var rStatus: relationsP ? relationsP.status : ({})
    readonly property var rShot: relationsP ? relationsP.thisShot : ({})
    readonly property var rOpen: relationsP ? relationsP.openRow : ({})
    readonly property var rRel: relationsP ? relationsP.relations : ({})
    readonly property var rKinds: relationsP ? relationsP.kinds : ({})
    readonly property var rReplay: relationsP ? relationsP.replaySession : ({})
    readonly property var rPlayhead: relationsP ? relationsP.playhead : ({})
    readonly property var wChoices: wristP ? wristP.choices : ({})
    readonly property var wShot: wristP ? wristP.thisShot : ({})
    readonly property var wSession: wristP ? wristP.session : ({})
    readonly property var wMoves: wristP ? wristP.movesWith : ({})
    readonly property var wStandfirst: wristP ? wristP.standfirst : ({})
    readonly property int wSelected: wristP ? wristP.selected : -1
    // some shot of the session carries a wrist reading (gated on data, never on the session type)
    readonly property bool wristAvailable: !!wristP && wristP.available
    // the wrist strips' metric keys, and whether one of them is a curve on show (the trigger, see the head)
    function _bareKey(k) { return String(k || "").replace(/^hm\./, "") }
    readonly property var _wristKeys: {
        const rows = [].concat(wShot && wShot.profile ? (wShot.profile.rows || []) : [], wSession ? (wSession.rows || []) : [])
        return rows.filter(function (r) { return !r.numbersOnly && !!r.key }).map(function (r) { return root._bareKey(r.key) })
    }
    readonly property bool wristOnShow: (tChoices && tChoices.metrics ? tChoices.metrics : [])
                                        .some(function (m) { return m.on === true && root._wristKeys.indexOf(root._bareKey(m.key)) >= 0 })
    readonly property bool wristShotShown: wristOnShow && !!wShot && wShot.available === true
    // Session's section reads in depth only: the glance (Replay register) adds This shot's, never Session's
    readonly property bool wristSessionShown: wristOnShow && wristAvailable && register !== "Replay" && !!wSession && wSession.available === true
    readonly property int pickedOrdinal: tShot && tShot.available ? (tShot.ordinal || -1) : (rStatus && rStatus.pickedOrdinal > 0 ? rStatus.pickedOrdinal : -1)
    readonly property bool busy: (!!tracesP && tracesP.busy) || (!!relationsP && relationsP.busy) || (!!wristP && wristP.busy)

    // ── the replay, fed by the panel (constants on an invisible screen) ──
    property real playUs: -1
    property real spanUs: 0

    // ── surfaces ──
    property bool interactive: true
    property string surface: "panel"             // "panel" | "cast"
    property string forceRegister: ""            // tests only
    property string demoState: ""                // renders only; always "" in the app

    // ── the reader's choices (restored and persisted by the panel) ──
    property string tab: "shot"                  // "shot" | "session" | "relations" | "kinds"
    property string preset: ""                   // "" = the register's default
    property var metricsOff: []                  // a preset's members switched off ("Custom", F4)
    property string window: "auto"
    property real customStartUs: -1
    property real customEndUs: -1
    property string band: "middle"               // "middle" | "every" | "fit"
    property string facet: ""                    // D3: none by default
    property string against: "none"
    property bool difference: false
    property bool overlay: false                 // F6: small multiples (false) or one shared plot
    property bool showDots: true                 // F7
    property bool showCursor: true               // F7
    property string upId: ""                     // Relations: "" = the presenter's default
    property string acrossId: ""
    property string relFacet: ""
    // the Wrist sections' choices (ChartsWristPresenter inputs; its scope follows shownView)
    property string wristAgainst: "address"      // "address" | "previous" | "reference" | "median"
    property bool wristCurves: false             // the curve behind the checkpoints
    property int wristArchetype: -1              // -1 Auto | 0 Neutral | 1 Bowed | 2 Cupped
    // transient place (SessionMode.chartsNav, per session): the open profile row
    property string openId: ""
    // the folds (AppSettings sectionCollapse, per screen × mode)
    property var collapsed: ({})
    // the asked question (per register, not saved)
    property string askedId: ""

    signal choiceMade(string name, var value)
    signal sectionToggled(string name, bool collapsed)
    signal navChanged()
    signal demoReset()
    // A shot was picked on a figure. THIS BODY SELECTS NOTHING: the carousel owns the selection (the panel's
    // _pickShot), by swing folder — the one identity the carousel and the presenters share.
    signal shotRequested(string swingDir, int shotId)

    property bool _demo: false
    function choose(name, value) {
        root[name] = value
        if (!_demo) choiceMade(name, value)
    }
    function toggleMetric(key) {
        const off = (metricsOff || []).slice(), i = off.indexOf(key)
        if (i >= 0) off.splice(i, 1); else off.push(key)
        choose("metricsOff", off)
    }
    function chooseCustom(startUs, endUs) {
        choose("customStartUs", Math.round(startUs)); choose("customEndUs", Math.round(endUs)); choose("window", "custom")
    }
    // a new Up reading lets the presenter choose its strongest partner again
    function chooseUp(id) { choose("upId", id); choose("acrossId", "") }
    function toggleSection(name) {
        const c = Object.assign({}, collapsed || {})
        c[name] = !(c[name] === true)
        collapsed = c
        if (!_demo) sectionToggled(name, c[name])
    }
    function openReading(id) { openId = (openId === id ? "" : id); if (!_demo) navChanged() }

    // ── questions: answered IN PLACE (spec 15.7) ──
    function ask(q, brush) {
        if (!q) { clearAsk(brush); return }
        askedId = q.id
        if (q.setFacet) choose("facet", q.setFacet)
        if (q.setBand) choose("band", q.setBand)
        if (q.upId) chooseUp(q.upId)
        if (q.go) choose("tab", q.go)
        if (brush) brush.lightShots(q.lit || [], q.id)
    }
    function clearAsk(brush) { askedId = ""; if (brush) brush.lightShots([], "") }
    function questionById(id) {
        const lists = [(tSession && tSession.questions) || [], (rShot && rShot.asks) || [], (rRel && rRel.asks) || []]
        for (const l of lists) for (const q of l) if (q.id === id) return q
        return null
    }

    // ── a shot picked on a figure: marks carry the ordinal; the session's shots list names its swing ──
    function pickOrdinal(ord) {
        if (!interactive || ord <= 0) return
        const s = rStatus && rStatus.shots ? rStatus.shots[ord - 1] : null
        if (s && s.ordinal === ord && s.swingDir) shotRequested(s.swingDir, s.shotId)
    }

    // ── demo states (renders only): reset to the saved choices, then set these WITHOUT saving them ──
    //   against:<id>  diff  overlay  split  unfold  nodots  window:<key>  band:<id>  facet:<id>|facet:first  fitShape
    //   scrub[:ms]  open:apart  open:curve  open:run  ask:<question id>  up:<readingId>  relFacet:first  preset:<name>
    //   wristAgainst:<id>  wristCurves  wristArchetype:<-1..2>  seekP:<P tag> (the Wrist sections; also wristPrevious
    //   wristReference  wristMedian  atP<n>, safe in a file name; wristSession, from the retired Wrist tab, opens Session)
    property string _lastDemo: ""
    function applyDemo(state, brush) {
        if (state === "" && _lastDemo === "") return
        _lastDemo = state
        demoReset()                                  // the panel puts the saved choices back
        askedId = ""; openId = ""; _pendingAsk = ""; _pendingSeekTag = ""
        _demo = true
        for (const t of String(state).split("+")) {
            const i = t.indexOf(":"), k = i < 0 ? t : t.slice(0, i), v = i < 0 ? "" : t.slice(i + 1)
            if (k === "against") against = v
            else if (k === "diff") { if (against === "none") against = "median"; difference = true }
            else if (k === "overlay") overlay = true
            else if (k === "split") overlay = false
            else if (k === "unfold") collapsed = ({})
            else if (k === "nodots") showDots = false
            else if (k === "window") window = v
            else if (k === "band") band = v
            else if (k === "facet") facet = v === "first" ? ((tChoices.facets || []).filter(function (f) { return f.kind === "condition" })[0] || { id: "" }).id : v
            else if (k === "fitShape") { band = "fit"; facet = "shape" }
            else if (k === "up") { upId = v; acrossId = "" }
            else if (k === "relFacet") relFacet = v === "first" ? (((rRel && rRel.facets) || [])[0] || { id: "" }).id : v
            else if (k === "preset") preset = v
            else if (k === "wristAgainst") wristAgainst = v
            else if (k === "wristCurves") wristCurves = true
            else if (k === "wristArchetype") wristArchetype = Number(v)
            else if (k === "seekP") _pendingSeekTag = v
            // file-name-safe spellings of the same (render tags carry the state)
            else if (k === "wristSession") tab = "session"
            else if (k === "wristPrevious") wristAgainst = "previous"
            else if (k === "wristReference") wristAgainst = "reference"
            else if (k === "wristMedian") wristAgainst = "median"
            else if (/^atP[1-8]$/.test(k)) _pendingSeekTag = k.slice(2)
            else if (k === "open") {
                const ap = (rShot && rShot.apart) || [], all = [].concat.apply(ap, ((rShot && rShot.groups) || []).map(function (g) { return g.rows || [] }))
                const pick = v === "curve" ? all.filter(function (r) { return r.opens === "trace" })[0]
                           : v === "run" ? all.filter(function (r) { return r.opens === "run" })[0] : ap[0]
                openId = pick ? pick.readingId : ""
            }
            else if (k === "scrub") {
                // a real seek through the presenter: the playhead 40 ms before impact (renders of "now")
                const us = tracesP ? tracesP.seekUsForShotMs(v === "" ? -40 : Number(v)) : -1
                if (us >= 0 && typeof shotReplay !== "undefined") shotReplay.seekToUs(us)
            }
            else if (k === "ask") { _pendingAsk = v; _pendingBrush = brush }
        }
        _demo = false
        _tryAsk()
        _trySeek()
    }
    // a demo seek to a checkpoint waits for the wrist presenter to know the picked shot's checkpoints
    property string _pendingSeekTag: ""
    function _trySeek() {
        if (_pendingSeekTag === "" || !wristP || wristP.busy) return
        const us = wristP.seekUsFor(_pendingSeekTag)
        if (us < 0) return
        _pendingSeekTag = ""
        if (typeof shotReplay !== "undefined") shotReplay.seekToUs(us)
    }
    onWShotChanged: _trySeek()
    // a demo question is asked once the presenters have answered the reset choices (they recompute off-thread)
    property string _pendingAsk: ""
    property var _pendingBrush: null
    function _tryAsk() {
        if (_pendingAsk === "" || busy) return
        const q = questionById(_pendingAsk)
        if (!q) return
        _pendingAsk = ""
        _demo = true; ask(q, _pendingBrush); _demo = false
    }
    onBusyChanged: { _tryAsk(); _trySeek() }
    onTSessionChanged: _tryAsk()
    onRShotChanged: _tryAsk()
    onRRelChanged: _tryAsk()

    // ── the views ──
    // `tab` is the reader's; `_tab` differs only on the auto-closing cast, where nobody presses a tab and the
    // shot is what the cast is for (Session when there is no shot to read).
    readonly property string _tab: interactive ? tab : (tShot && tShot.available ? "shot" : "session")
    readonly property string view: _tab
    // the view actually drawn: Replay has no Kinds form and reads Session there; anything else unknown (a "wrist"
    // saved by the retired Wrist tab) reads This shot
    readonly property string shownView: register === "Replay" && view === "kinds" ? "session"
                                      : ["shot", "session", "relations", "kinds"].indexOf(view) >= 0 ? view : "shot"

    // ── the frame: the stage owns it (PpModeStage switches `framed` off); on the cast the register draws a
    // running head; anywhere else a framed body draws the quiet card itself ──
    property bool framed: true
    readonly property bool _drawFrame: framed && surface !== "cast"
    // the card heading's aside names the preset family (F1: the panel says which preset it is showing)
    readonly property string cardAside: Theme.caps(tChoices && tChoices.presetTitle ? tChoices.presetTitle : "")
    // …and it is the selector: a heading that names the preset (the stage card's, set by the stage through the panel,
    // or this body's own frame) opens the preset picker, and the tab row carries no second copy as a chip
    property bool presetInHeading: false
    readonly property bool presetInChip: !presetInHeading && !_drawFrame
    readonly property bool presetPickerOpen: !!registerItem && registerItem.presetOpen === true
    function togglePresetPicker() { if (registerItem && interactive) registerItem.presetOpen = !registerItem.presetOpen }
    readonly property int _framePad: Theme.gap(14)
    PpCardShell { anchors.fill: parent; visible: root._drawFrame; tone: Theme.colorText3 }
    Item {
        id: frameHead
        visible: root._drawFrame
        x: root._framePad; y: root._framePad
        width: parent.width - 2 * root._framePad
        height: visible ? frameTitle.implicitHeight : 0
        PpMicro { id: frameTitle; objectName: "chTitle"; text: Theme.caps(qsTr("Charts")); color: Theme.colorText3 }
        PpMicro {
            anchors.right: parent.right
            text: root.cardAside + (root.interactive && root.cardAside !== "" ? "  ▾" : "")
            font.letterSpacing: Theme.trackingData
            color: root.presetPickerOpen || frameAsideMouse.containsMouse ? Theme.colorText : Theme.colorText3
            MouseArea {
                id: frameAsideMouse
                anchors.fill: parent; anchors.margins: -Theme.gap(4)
                enabled: root.interactive; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.togglePresetPicker()
            }
        }
    }

    // ── the register ──
    // the glance (Replay register) is for the mat: live Capture and the auto-replay after a shot, which drops back to
    // Capture on its own. A Replay the golfer opened to review a session reads in depth, as Analyse does (Mark, 16 Oct).
    readonly property bool _atTheMat: SessionMode.mode === SessionMode.capture
                                      || (SessionMode.mode === SessionMode.replay && SessionMode.autoReturnToCapture)
    readonly property string register: forceRegister !== "" ? forceRegister
                                     : surface === "cast" ? "Replay"
                                     : _atTheMat ? "Replay" : "Analyse"
    readonly property var registerItem: register === "Analyse" ? analyseLoader.item : replayLoader.item
    Loader {
        id: replayLoader
        x: root._drawFrame ? root._framePad : 0
        y: root._drawFrame ? frameHead.y + frameHead.height + Theme.gap(10) : 0
        width: root.width - 2 * x
        height: root.height - y - (root._drawFrame ? root._framePad : 0)
        active: root.register === "Replay"
        sourceComponent: PpChartsReplay {
            body: root; view: root.view; surface: root.surface; interactive: root.interactive; demoState: root.demoState
        }
    }
    Loader {
        id: analyseLoader
        x: replayLoader.x; y: replayLoader.y; width: replayLoader.width; height: replayLoader.height
        active: root.register === "Analyse"
        sourceComponent: PpChartsAnalyse {
            body: root; view: root.view; surface: root.surface; interactive: root.interactive; demoState: root.demoState
        }
    }
}
