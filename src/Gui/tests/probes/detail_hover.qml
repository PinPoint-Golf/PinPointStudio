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

// END-TO-END: HOVER ON THE CONDITION DETAIL, WITH A REAL POINTER, ON THE REPLAY VIEW.
//
// session_spread.qml drives the charts' `hovered` signals directly and the qml_ui tests press the
// body in a test window, so neither can see what broke hover in the app after round 3/4: the
// panel as a stage panel on the REPLAY view, a swing playing beside it, the condition detail
// open. This probe stands that up exactly — the Wrist screen's PpModeStage hosting the panel
// alongside the camera and charts, in each arrangement the stage offers (split, stage, tabs),
// with shotReplay playing — clicks through to Over the top, then moves a REAL pointer (QtTest's
// synthesized events, delivered through the window) onto a strip dot and a run mark and HOLDS it
// there for a couple of seconds while the replay runs, checking at every tick that:
//   · the chart's pick area still has the pointer,
//   · the hovered shot is still the one under it, and the readout line still reads it,
// and counting what happened underneath: replay position ticks, detail/surface republishes,
// panel recreations, and rebuilds of the charts' delegates.
//
// Run through --probe-qml on a COPY of the athlete folder: build/run-me/verify-detail-hover.sh.
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.

import QtQuick
import QtTest
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property string condition: arg("--probe-condition", "over_the_top")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "20"))
    readonly property string pngDir: arg("--probe-png-dir", "")
    readonly property var arrangements: arg("--probe-arrangements", "split,stage,tabs").split(",")

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) failures += 1 }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function walk(item, pred, out) {
        if (!item) return
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out)
    }
    function find(root, name) { const out = []; walk(root, it => it.objectName === name, out); return out }
    function shown(item) {
        let i = item
        while (i) { if (!i.visible) return false; i = i.parent }
        return true
    }
    function appBody() {
        const bs = find(probe.Window.window.contentItem, "sdBody").filter(b => shown(b))
        return bs.length ? bs[0] : null
    }

    // QtTest's event synthesis, used OUTSIDE a test run: its mouse functions send real events
    // through the item's window, so whatever the app has stacked over the charts gets them first.
    TestCase { id: tc; name: "detailHover"; when: false; running: false }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    // ── what happens underneath while the pointer is held still ──────────────
    property var body: null
    property var model: null
    property int detailChanges: 0
    property int surfaceChanges: 0
    property int positionTicks: 0
    Connections {
        target: probe.model
        function onDetailChanged()  { probe.detailChanges += 1 }
        function onSurfaceChanged() { probe.surfaceChanges += 1 }
    }
    Connections {
        target: shotReplay
        function onPositionChanged() { probe.positionTicks += 1 }
    }
    function resetCounts() { detailChanges = 0; surfaceChanges = 0; positionTicks = 0 }

    function bringIntoView(item) {
        const flick = find(body, "sdDetailFlick")[0]
        const p = item.mapToItem(flick.contentItem, 0, 0)
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, p.y - 20))
    }

    property var strip: null
    property var vr: null
    property var line: null
    property var chart: null       // the chart being hovered
    property var firstDelegate: null
    property int target: -1
    property int arrIdx: 0
    property int phase: 0
    property int holds: 0
    property bool okSoFar: true

    function grabCharts() {
        strip = find(body, "sdDetailStrip")[0]
        vr    = find(body, "sdDetailValueRun")[0]
        line  = find(body, "sdDetailHoverReadout")[0]
    }
    function hoverState() {
        const area = chart === strip ? find(strip, "sdStripPick")[0] : find(vr, "sdRunTap")[0]
        return { contains: !!area && area.containsMouse, idx: chart ? chart.hoverIndex : -2,
                 text: line ? line.text : "" }
    }

    Timer {
        id: ticker
        interval: 700; repeat: true; running: true
        property int step: 0
        onTriggered: {
            step += 1
            const arr = probe.arrangements[probe.arrIdx]
            if (step === 1) {
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                probe.Window.window.visibility = Window.Windowed
                probe.Window.window.width  = parseInt(probe.arg("--probe-width", "1920"))
                probe.Window.window.height = parseInt(probe.arg("--probe-height", "1080"))
                navController.navigate(2)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (step === 3) {
                // The replay view, with the panel on beside the camera and the charts.
                ViewLayout.setPanel(SessionMode.replay, "sessionDiagnostics", true)
                ViewLayout.setPanel(SessionMode.replay, "camera", true)
                ViewLayout.setPanel(SessionMode.replay, "charts", true)
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
                }
            } else if (step === 5) {
                probe.startArrangement()
            } else if (step > 5) {
                probe.advance()
            }
        }
    }

    function startArrangement() {
        const arr = arrangements[arrIdx]
        ViewLayout.setArrangement(SessionMode.replay, arr)
        phase = 0
    }

    // One arrangement, as a little sequence of phases, one per tick.
    function advance() {
        const arr = arrangements[arrIdx]
        if (phase === 0) {
            if (arr === "tabs") {
                // the panel's own tab — it leads the stage's order, so it is index 0
                const stages = []
                walk(probe.Window.window.contentItem, it => it.arrangement !== undefined && it.tabIndex !== undefined, stages)
                for (const st of stages) st.tabIndex = 0
            }
            // LOOPING, as the replay view does it: the impact clip's own loop runs on its own clock
            // for as long as it is on, so the stage is never still while the pointer is held.
            if (!shotReplay.playing && shotReplay.active) shotReplay.togglePlay()
            if (!shotReplay.impactLoopPlaying && shotReplay.active) shotReplay.toggleImpactLoop()
            phase = 1
        } else if (phase === 1) {
            body = appBody()
            check(!!body, arr + ": the Session diagnostics panel is up on the replay view")
            if (!body) { nextArrangement(); return }
            model = body.source
            // LIVE, as Mark's session is: the model in the live tense over the same ledger.
            if (arg("--probe-live", "1") === "1") model.reviewing = false
            model.openDetail(condition)
            log(arr + ": replay active " + shotReplay.active + " · playing " + shotReplay.playing
                + " · swing " + shotReplay.swingDir.substring(shotReplay.swingDir.lastIndexOf("/") + 1)
                + " · panel " + Math.round(body.width) + "×" + Math.round(body.height))
            phase = 2
        } else if (phase === 2) {
            grabCharts()
            check(body.detailOpen && !!strip && !!vr && !!line, arr + ": clicked through to " + condition)
            if (!strip) { nextArrangement(); return }
            bringIntoView(strip)
            phase = 3
        } else if (phase === 3) {
            // ── a REAL pointer onto a strip dot, then HELD ─────────────────────
            chart = strip
            const dots = find(strip, "sdStripDot")
            const dot = dots.filter(d => !d.current && d.modelData.clipped === 0)[Math.floor(dots.length / 3)]
            target = dot.modelData.index
            firstDelegate = dots[0]
            const c = dot.mapToItem(strip, dot.width / 2, dot.height / 2)
            resetCounts()
            tc.mouseMove(strip, c.x - 3, c.y)
            tc.mouseMove(strip, c.x, c.y)
            holds = 0; okSoFar = true
            phase = 4
        } else if (phase === 4 || phase === 7) {
            // held: three ticks, ~2 s, while the replay runs
            // keep the replay moving: restart the window playhead whenever it reaches its end
            if (shotReplay.active && !shotReplay.playing) { shotReplay.seekToFraction(0); shotReplay.togglePlay() }
            const st = hoverState()
            const good = st.contains && st.idx === target && st.text.indexOf("shot " + (target + 1) + " · ") === 0
            const what = chart === strip ? "strip" : "run"
            const rebuilt = chart === strip ? find(strip, "sdStripDot")[0] !== firstDelegate
                                            : find(vr, "sdRunMark")[0] !== firstDelegate
            log(arr + " · " + what + " held " + (holds + 1) + ": containsMouse " + st.contains + " · hoverIndex "
                + st.idx + " (want " + target + ") · line \"" + st.text + "\" · replay ticks " + positionTicks
                + " · detail republished " + detailChanges + "× · surface " + surfaceChanges
                + "× · delegates rebuilt " + rebuilt + " · same panel " + (appBody() === body)
                + " · loop " + shotReplay.impactLoopPlaying)
            if (!good) okSoFar = false
            holds += 1
            if (holds >= 3) {
                check(okSoFar, arr + ": a pointer held on a " + what + " " + (what === "strip" ? "dot" : "mark")
                      + " keeps its hover while the replay runs")
                if (pngDir !== "")
                    body.grabToImage(r => r.saveToFile(pngDir + "/" + arr + "_hover_" + what + ".png"))
                phase = phase === 4 ? 5 : 8
            }
        } else if (phase === 5) {
            bringIntoView(vr)
            phase = 6
        } else if (phase === 6) {
            chart = vr
            const marks = find(vr, "sdRunMark")
            target = Math.min(7, marks.length - 1)
            firstDelegate = marks[0]
            const m = marks[target]
            resetCounts()
            tc.mouseMove(vr, m.x + m.width / 2 - 3, m.y + m.height / 2)
            tc.mouseMove(vr, m.x + m.width / 2, m.y + m.height / 2)
            holds = 0; okSoFar = true
            phase = 7
        } else if (phase === 8) {
            tc.mouseMove(body, 2, 2)
            model.closeDetail()
            nextArrangement()
        }
    }
    function nextArrangement() {
        arrIdx += 1
        if (arrIdx >= arrangements.length) { finish(); return }
        startArrangement()
    }
}
