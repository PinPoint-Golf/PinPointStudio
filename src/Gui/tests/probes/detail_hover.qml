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

// END-TO-END: HOVER ON THE CONDITION DETAIL, WITH A REAL POINTER, ON THE ANALYSE VIEW.
//
// session_spread.qml drives the charts' `hovered` signals directly and the qml_ui tests press the
// body in a test window, so neither can see what broke hover in the app after round 3/4: the
// panel as a stage panel on the REPLAY view, a swing playing beside it, the condition detail
// open. This probe stands that up exactly — the Wrist screen's PpModeStage hosting the panel
// alongside the camera and charts, in each arrangement the stage offers (split, stage, tabs),
// with shotReplay playing — clicks through to Over the top, then moves a REAL pointer (QtTest's
// synthesized events, delivered through the window) onto a box mark and a run mark and HOLDS it
// there for a couple of seconds while the replay runs, checking at every tick that:
//   · the chart's pick area still has the pointer,
//   · the hovered shot is still the one under it, and the readout line still reads it,
// and counting what happened underneath: replay position ticks, detail/surface republishes,
// panel recreations, and rebuilds of the charts' delegates.
//
// TYPESET REGISTERS (11 Oct): the detail is drawn only in Analyse now (PpDiagAnalyse; Replay never
// drills, D9), so the probe picks the swing on Replay (the carousel's path, which starts the replay),
// then enters ANALYSE — the stage keeps the replay loaded — and holds the pointer there. The handles are
// the figure row's PpBoxStrip (`sdBoxStrip`, marks `sdBoxMark`: the shots past its whiskers and the picked
// shot; since 12 Oct it replaces the spread strip's dots) and PpRunChart (`sdRunChart`, marks
// `sdRunMark`): each mark is a PpMark with its own pick area (its one MouseArea), the hovered shot is the
// register's brush (`hoveredShot`, read through either chart), and the run caption
// `sdDetailHoverReadout` reads run[hovered].readout while a shot is hovered. A hover is identified by
// SHOT ID (ids survive a deleted swing; ordinals do not).
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

    function bringIntoViewCentred(item) {
        const flick = find(body, "sdDetailFlick")[0]
        const p = item.mapToItem(flick.contentItem, 0, 0)
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, p.y - flick.height / 2))
    }
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

    property var mark: null        // the PpMark under the pointer
    property string wantLine: ""   // what the run caption must read while `target` is hovered
    function grabCharts() {
        strip = find(body, "sdBoxStrip").filter(x => shown(x))[0] || null
        vr    = find(body, "sdRunChart").filter(x => shown(x))[0] || null
        line  = find(body, "sdDetailHoverReadout").filter(x => shown(x))[0] || null
    }
    // the mark's own pick area: a PpMark's one MouseArea child
    // A pointer target the pointer can actually reach: inside the chart, and not under a LATER mark's pick
    // area (later delegates are on top and take the hover — in a crowd of box outliers, or a 172 px split run,
    // marks overlap; the first run picked covered ones). Prefers the middle of the reachable ones.
    function reachable(list, chartItem, ok) {
        const out = []
        for (let i = 0; i < list.length; ++i) {
            const a = list[i]
            if (!ok(a) || a.x < 2 || a.x > chartItem.width - 2) continue
            // and on screen: inside the detail's view (a chart wider than a very narrow panel is clipped)
            const flick = find(body, "sdDetailFlick")[0]
            const fp = a.mapToItem(flick, 0, 0)
            if (fp.x < 2 || fp.x > flick.width - 2) continue
            const pa = pickArea(a)
            const half = (pa ? pa.width : 12) / 2 + 1
            let covered = false
            for (let j = i + 1; j < list.length && !covered; ++j)
                covered = Math.abs(list[j].x - a.x) < half && Math.abs(list[j].y - a.y) < half
            if (!covered) out.push(a)
        }
        return out
    }
    // what is under a point (window coordinates): the deepest visible item, topmost first — logged when a
    // hold finds no hover, so a failure names what took the pointer
    function underPoint(item, sx, sy, depth) {
        const kids = item.children || []
        for (let i = kids.length - 1; i >= 0; --i) {
            const k = kids[i]
            if (!k.visible || k.width === undefined) continue
            const p = k.mapFromItem(null, sx, sy)
            const inside = p.x >= 0 && p.y >= 0 && p.x <= k.width && p.y <= k.height
            if (k.clip && !inside) continue
            const deeper = underPoint(k, sx, sy, depth + 1)
            if (deeper) return deeper
            if (inside && k.width > 0 && k.height > 0)
                return (k.objectName || String(k).split("(")[0]) + (k.containsMouse !== undefined ? "[MouseArea hover " + k.hoverEnabled + "]" : "")
                       + " ← " + (k.parent ? (k.parent.objectName || String(k.parent).split("(")[0]) : "")
        }
        return ""
    }
    function pickArea(m) { const k = m ? m.children : []; for (let i = 0; i < k.length; ++i) if (k[i].containsMouse !== undefined) return k[i]; return null }
    function readoutFor(shotId) {
        const run = (vr && vr.spread && vr.spread.run) || []
        for (let i = 0; i < run.length; ++i) if (run[i].shotId === shotId) return run[i].readout || ""
        return ""
    }
    function hoverState() {
        const area = pickArea(mark)
        return { contains: !!area && area.containsMouse, idx: chart ? chart.hoveredShot : -2,
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
                navController.navigate(AppScreen.Session)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (step === 3) {
                // The analyse view, with the panel on beside the camera and the charts.
                ViewLayout.setPanel(SessionMode.analyse, "sessionDiagnostics", true)
                ViewLayout.setPanel(SessionMode.analyse, "camera", true)
                ViewLayout.setPanel(SessionMode.analyse, "charts", true)
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
                }
            } else if (step === 4) {
                SessionMode.enterAnalyse()               // the replay stays loaded (SessionMode._loadFocused)
            } else if (step === 5) {
                probe.startArrangement()
            } else if (step > 5) {
                probe.advance()
            }
        }
    }

    function startArrangement() {
        const arr = arrangements[arrIdx]
        // The camera leads the stage's order (since 11 Oct), so in the stage arrangement it takes the
        // dominant slot and the panel shares the side column with everything else on — a sliver.
        // This probe is about HOVER, not about that squeeze, so the stage pass turns the camera off
        // and the panel leads; the other arrangements keep the camera on beside it.
        ViewLayout.setPanel(SessionMode.analyse, "camera", arr !== "stage")
        ViewLayout.setArrangement(SessionMode.analyse, arr)
        phase = 0
    }

    // One arrangement, as a little sequence of phases, one per tick.
    function advance() {
        const arr = arrangements[arrIdx]
        if (phase === 0) {
            if (arr === "tabs") {
                // the panel's own tab, found by key (the camera leads the stage's order)
                const stages = []
                walk(probe.Window.window.contentItem, it => it.arrangement !== undefined && it.tabIndex !== undefined, stages)
                for (const st of stages) {
                    const i = (st.active || []).findIndex(d => d.key === "sessionDiagnostics")
                    if (i >= 0) st.tabIndex = i
                }
            }
            // LOOPING, as the replay view does it: the impact clip's own loop runs on its own clock
            // for as long as it is on, so the stage is never still while the pointer is held.
            if (!shotReplay.playing && shotReplay.active) shotReplay.togglePlay()
            if (!shotReplay.impactLoopPlaying && shotReplay.active) shotReplay.toggleImpactLoop()
            phase = 1
        } else if (phase === 1) {
            body = appBody()
            check(!!body, arr + ": the Session diagnostics panel is up on the analyse view")
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
            const regs = find(body, "sdRegister")
            check(regs.length === 1 && regs[0].register === "Analyse", arr + ": the Analyse register draws the detail")
            check(body.detailOpen && !!strip && !!vr && !!line, arr + ": clicked through to " + condition)
            if (!strip) { nextArrangement(); return }
            bringIntoView(strip)
            phase = 3
        } else if (phase === 3) {
            // ── a REAL pointer onto a box mark, then HELD ──────────────────────
            // a shot past the whiskers when the session has one, else the picked shot on the box
            chart = strip
            const dots = find(strip, "sdBoxMark").filter(d => d.visible && d.role !== "hovered")
            let free = reachable(dots, strip, d => d.role === "outlier" && !d.ringed && !(d.dot && d.dot.clipped > 0))
            if (!free.length) free = reachable(dots, strip, d => d.role === "picked")
            check(free.length > 0, arr + ": a box mark the pointer can reach")
            const dot = free[Math.floor(free.length / 2)] || dots[0]
            mark = dot
            bringIntoViewCentred(dot)
            target = dot.shotId
            wantLine = readoutFor(target)
            firstDelegate = dots[0]
            // a PpMark is a 0×0 item at its centre: its x / y ARE the point to hover
            resetCounts()
            tc.mouseMove(strip, dot.x - 3, dot.y)
            tc.mouseMove(strip, dot.x, dot.y)
            holds = 0; okSoFar = true
            phase = 4
        } else if (phase === 4 || phase === 7) {
            // held: three ticks, ~2 s, while the replay runs
            // keep the replay moving: restart the window playhead whenever it reaches its end
            if (shotReplay.active && !shotReplay.playing) { shotReplay.seekToFraction(0); shotReplay.togglePlay() }
            const st = hoverState()
            const good = st.contains && st.idx === target && (wantLine === "" || st.text === wantLine)
            const what = chart === strip ? "box" : "run"
            const rebuilt = chart === strip ? find(strip, "sdBoxMark")[0] !== firstDelegate
                                            : find(vr, "sdRunMark")[0] !== firstDelegate
            log(arr + " · " + what + " held " + (holds + 1) + ": containsMouse " + st.contains + " · hovered shot "
                + st.idx + " (want shot id " + target + ") · line \"" + st.text + "\" · replay ticks " + positionTicks
                + " · detail republished " + detailChanges + "× · surface " + surfaceChanges
                + "× · delegates rebuilt " + rebuilt + " · same panel " + (appBody() === body)
                + " · loop " + shotReplay.impactLoopPlaying)
            if (!good && holds === 0 && mark) {
                const sp = mark.mapToItem(null, 0, 0)
                log(arr + " · under the pointer (" + Math.round(sp.x) + "," + Math.round(sp.y) + "): " + underPoint(probe.Window.window.contentItem, sp.x, sp.y, 0))
            }
            if (!good) okSoFar = false
            holds += 1
            if (holds >= 3) {
                check(okSoFar, arr + ": a pointer held on a " + what + " " + "mark"
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
            const free = reachable(marks, vr, x => !x.ringed)
            check(free.length > 0, arr + ": a run mark the pointer can reach")
            const m = free[Math.floor(free.length / 2)] || marks[0]
            mark = m
            bringIntoViewCentred(m)
            const fl = find(body, "sdDetailFlick")[0], right = vr.mapToItem(fl, vr.width, 0).x
            if (right > fl.width + 1)
                log(arr + " · NOTE the run chart is wider than the detail's view (right edge " + Math.round(right)
                    + " in " + Math.round(fl.width) + " px): its right part is clipped")
            target = m.shotId
            wantLine = readoutFor(target)
            firstDelegate = marks[0]
            resetCounts()
            tc.mouseMove(vr, m.x - 3, m.y)
            tc.mouseMove(vr, m.x, m.y)
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
