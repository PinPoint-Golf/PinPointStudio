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

// THE CHARTS PANEL UNDER TEST — shared by tst_charts_panel.qml and tst_chart_presets.qml (not a tst_ file,
// so qml_ui does not run it on its own).
//
// The REAL PpChartsPanel (its wiring, persistence and place-keeping), its REAL body, registers and views,
// and its REAL presenters, fed the REAL 4 July session (the trimmed fixture the presenter unit tests read)
// by `chartsFixture` (qml_ui_test.cpp). Only the app's controllers are faked, in QML, and put on THIS
// file's engine under the names the panel reads (shotReplay, navController, sessionReviewController,
// shotProcessor, reanalysisController): the stage, the carousel and the replay are not under test here.
//
//   install()            once, before the first build()
//   build()              a fresh panel (the stage rebuilding it); the previous one is destroyed
//   feed(opts)           the fixture into the panel's presenters (chartsFixture.feed options)
//   pick(ordinal[, detail])   the replay opens that shot (as SessionMode.enterReplay does)
//   settle(tc)           the presenters idle, then a frame (tc: the TestCase, for waitForRendering)
// Tree helpers walk from the body: findAll, shown (every ancestor visible), shownAll, texts.
Item {
    id: h
    anchors.fill: parent

    property int sessionType: 1                  // the host's session type (1 is SessionController.Wrist): the persistence key
    property string defaultTab: "shot"           // the host's opening tab
    property string defaultPreset: ""            // the host's opening preset (ScreenWrist passes "Wrist & forearm")
    property var panel: null
    readonly property var body: panel ? panel.chBody : null
    readonly property alias replay: fakeReplay
    readonly property alias nav: fakeNav
    readonly property alias review: fakeReview
    readonly property alias processor: fakeProcessor

    // ── the app's controllers, faked ──
    QtObject {
        id: fakeReplay
        property bool active: false
        property int shotId: -1
        property string swingDir: ""
        property var analysisDetail: ({})
        property real startUs: 0
        property real endUs: 0
        property real positionUs: -1
        signal spanChanged()
        signal positionChanged()
        property int seeks: 0
        property real lastSeekUs: -1
        property int scrubs: 0
        function seekToUs(us) { seeks += 1; lastSeekUs = us; positionUs = us; positionChanged() }
        function beginScrub() { scrubs += 1 }
        function endScrub() {}
        // SessionMode._loadFocused → the replay opens the swing (the fixture's own detail)
        function start(id, dir, rate) {
            const ord = parseInt(String(dir).slice(-4))
            h.open(ord, chartsFixture.detailOf(ord), id)
        }
        function stop() { active = false; shotId = -1 }
    }
    QtObject { id: fakeNav; property int currentIndex: AppScreen.Session }   // the gate: the session screen is up
    ListModel { id: carouselShots }
    QtObject {
        id: fakeReview
        property bool reviewActive: false
        property string activeSessionId: ""
        property string liveSessionDir: ""
        property var activeShots: carouselShots
        property var shots: carouselShots
        function resumeLive() { reviewActive = false }
    }
    QtObject { id: fakeProcessor; property string activeSessionDir: ""; signal shotProcessed(int shotId, string swingDir) }
    QtObject { id: fakeReanalysis; signal reanalysed(string swingDir) }

    function install() {
        chartsFixture.provide("shotReplay", fakeReplay)
        chartsFixture.provide("navController", fakeNav)
        chartsFixture.provide("sessionReviewController", fakeReview)
        chartsFixture.provide("shotProcessor", fakeProcessor)
        chartsFixture.provide("reanalysisController", fakeReanalysis)
        carouselShots.clear()
        for (let o = 1; o <= chartsFixture.count(); ++o)
            carouselShots.append({ shotId: o, ordinal: o, swingDir: chartsFixture.dirOf(o) })
        // the session the panel's model points at: the fixture's folder (it reads nothing there; the
        // presenters are fed directly), so the place-keeping stamp is a real session folder
        fakeReview.liveSessionDir = chartsFixture.sessionDir()
    }

    // ── the panel ──
    Component { id: panelC; PpChartsPanel {} }
    function build() {
        if (panel) { panel.destroy(); panel = null }
        // unframed, as PpModeStage hosts it (the stage card draws the frame)
        panel = panelC.createObject(h, { sessionType: h.sessionType, defaultTab: h.defaultTab, defaultPreset: h.defaultPreset, framed: false })
        panel.width = Qt.binding(function () { return h.width })
        panel.height = Qt.binding(function () { return h.height })
        return panel
    }
    // the wrist presenter is fed too (detached from the model): without opts.wrist no shot carries a wrist
    // profile, so no wrist section shows — every suite but the Wrist cases sees the standard views only
    // (and the posture presenter: without opts.posture it is fed no shot, so no Posture section shows)
    function feed(opts) { return chartsFixture.feed([body.tracesP, body.relationsP, body.wristP, body.postureP], opts || {}) }

    // ── the replay ──
    // The span of a shot's recording: the last sample of any of its series (window-relative µs).
    function spanOf(detail) {
        let end = 0
        for (const s of (detail && detail.series) || []) { const t = s.t_us || []; if (t.length) end = Math.max(end, Number(t[t.length - 1])) }
        return end
    }
    function open(ordinal, detail, id) {
        fakeReplay.active = false
        fakeReplay.shotId = id === undefined ? ordinal : id
        fakeReplay.swingDir = chartsFixture.dirOf(ordinal)
        fakeReplay.analysisDetail = detail
        fakeReplay.startUs = 0
        fakeReplay.endUs = spanOf(detail)
        fakeReplay.positionUs = -1
        fakeReplay.active = true
        fakeReplay.spanChanged()
    }
    function pick(ordinal, detail) { open(ordinal, detail === undefined ? chartsFixture.detailOf(ordinal) : detail) }
    function unpick() { fakeReplay.active = false; fakeReplay.swingDir = ""; fakeReplay.analysisDetail = ({}) }

    // ⚠ NOT waitForRendering's default: it waits for a frame, and a scene with nothing new to draw sends none,
    // so each call would sit out its whole 5 s. Both presenters idle (they post their results), a few event
    // turns for the bindings and the polish, then at most a short wait for a frame.
    function settle(tc) {
        for (let i = 0; i < 2; ++i) {
            if (body && body.tracesP) body.tracesP.waitForIdle(60000)
            if (body && body.relationsP) body.relationsP.waitForIdle(60000)
            if (body && body.wristP) body.wristP.waitForIdle(60000)
            if (body && body.postureP) body.postureP.waitForIdle(60000)
            if (tc) tc.wait(16)
        }
        if (tc) tc.waitForRendering(h, 60)
    }

    // ── the tree ──
    function findAll(item, name, out) {
        out = out || []
        if (!item) return out
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            const n = kids[i].objectName
            if (name instanceof RegExp ? name.test(n) : n === name) out.push(kids[i])
            findAll(kids[i], name, out)
        }
        return out
    }
    // on screen only if every ancestor up to the body is visible too
    function shown(item) {
        let i = item
        while (i && i !== body) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function shownAll(item, name) { return findAll(item, name).filter(function (i) { return shown(i) }) }
    function isText(i) { return !!i && i.elide !== undefined && i.truncated !== undefined && typeof i.text === "string" }
    function textsIn(item, out) {
        out = out || []
        if (!item) return out
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) { if (isText(kids[i])) out.push(kids[i]); textsIn(kids[i], out) }
        return out
    }
    function plain(s) {
        return String(s === undefined || s === null ? "" : s).replace(/<[^>]*>/g, "").replace(/&nbsp;/g, " ").replace(/ /g, " ")
                   .replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&#8722;/g, "−").replace(/\s+/g, " ").trim()
    }
    // every shown text under an item — the item's own first, when it is a Text (a PpChText is one)
    function allText(item) {
        const ts = (isText(item) ? [item] : []).concat(textsIn(item))
        return ts.filter(function (t) { return shown(t) && t.text !== "" }).map(function (t) { return plain(t.text) }).join(" | ")
    }
    // the drawn layers of a trace or envelope row's plot (PpChPlot: the Canvas holding `layers`)
    function plotOf(row) {
        const kids = row ? row.children : []
        for (let i = 0; i < kids.length; ++i) if (kids[i].layers !== undefined && kids[i].xOf !== undefined) return kids[i]
        return null
    }
    // the Flickable a view scrolls in (chShotFlick …)
    function flickOf(item) {
        let f = item ? item.parent : null
        while (f && f !== body && !(f.contentY !== undefined && f.contentItem !== undefined && /Flick$/.test(f.objectName)))
            f = f.parent
        return f && f !== body ? f : null
    }
}
