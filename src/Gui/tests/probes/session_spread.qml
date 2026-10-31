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

// END-TO-END: HOW FAR, NOT ONLY WHETHER — the corridor strip, the value run and the
// across-sessions columns on a REAL session, in the real app.
//
// The qml_ui suite presses the body with fixtures; this loads a session the way a golfer does
// (load → pick a swing in the carousel → Analyse), finds the Session diagnostics panel, and checks
// that what it DRAWS for one condition is what the ledger holds: a dot per measured shot, the
// unmeasured ones counted rather than placed, the fault line where the norm puts it, the value
// run's mark per shot, and the other sessions of the same golfer as columns in review. Then it
// taps a dot and checks the carousel's selection — and the ringed dot — follow.
//
// It also grabs the panel at the design size and at the 396 split, in studio dark and light,
// with the condition's detail open, so the pictures can be looked at.
//
// TYPESET REGISTERS (11 Oct): the panel is PpDiagAnalyse in Analyse. A pattern is a ledger row
// (`sdSessionRow`, keyed by `cardId`) with its shot strip and the median / this-shot / unit cells
// (`sdSessionRowMedian`, `sdSessionRowShot`, `sdSessionRowUnit`); the charts live in the detail's figure
// row (`sdFigureRow`: `sdRunChart` + its box plot `sdBoxStrip`, `sdHistoryStrip` on ONE value axis, Y labels
// `sdRunYTick` on the row). Hover is the register's brush (`hoveredShot`, read through either chart),
// keyed by SHOT ID; the run caption `sdDetailHoverReadout` reads the hovered shot. The probe-made bodies
// are forced onto the Analyse register (`forceRegister`, tests only) and given the app body's readout,
// so their pick ring is the carousel's shot, as in the app.
//
// RUN IT ON A COPY of the athlete folder (diagnostics.json copied, swing_* symlinked) and a COPY
// of the settings — review re-persists diagnostics.json and the theme switch writes the ini:
//   XDG_CONFIG_HOME=<scratch> QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <this file> \
//       --probe-session <copied session dir> [--probe-condition over_the_top]
//       [--probe-ordinal 20] [--probe-png-dir <dir>] [--probe-sessions 4]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. build/run-me/verify-session-spread.sh wraps it.

import QtQuick
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
    readonly property string sessionName: sessionDir.replace(/\/+$/, "").split("/").pop()
    readonly property string condition: arg("--probe-condition", "over_the_top")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "20"))
    readonly property int sessions: parseInt(arg("--probe-sessions", "4"))
    readonly property string pngDir: arg("--probe-png-dir", "")

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    property int startTheme: 5
    function finish() {
        Theme.themeIndex = probe.startTheme
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }

    function walk(item, pred, out) {
        if (!item) return
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out)
    }
    function find(root, name) {
        const out = []
        walk(root, it => it.objectName === name, out)
        return out
    }
    function shown(item, stop) {
        let i = item
        while (i && i !== stop) { if (!i.visible) return false; i = i.parent }
        return true
    }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    // Probe-owned bodies over the SAME model, at controlled sizes, for the pictures.
    Component {
        id: bodyComp
        PpSessionDiagnosticsBody { }
    }

    property var appBody: null
    property var model: null
    property var wide: null
    property var narrow: null
    property var grabs: []      // [ [item, file], ... ] drained one per tick
    property int step: 0
    property int idleWaits: 0

    function cardFor(body, id) {
        const cs = find(body, "sdSessionRow").filter(c => c.cardId === id && shown(c, body))
        return cs.length > 0 ? cs[0] : null
    }
    // the bodies this probe made: never the app's (a probe body reading its OWN readout is a binding loop —
    // the first run hit it, because the body being made is not yet in wide/narrow/big when it binds)
    property var made: []
    // The app body's readout, FOLLOWED rather than bound: a binding that walked the window tree depended on
    // every children list, and the probe body it fed rebuilt its views → a binding loop (second run).
    property var liveReadout: null
    function followReadout() {
        const a = probe.liveAppBody()
        const r = a ? a.readout : null
        if (r !== probe.liveReadout) probe.liveReadout = r
    }
    Timer { interval: 150; repeat: true; running: probe.made.length > 0; onTriggered: probe.followReadout() }
    function liveAppBody() {
        const bs = find(probe.Window.window.contentItem, "sdBody").filter(b => probe.made.indexOf(b) < 0 && shown(b, null))
        return bs.length ? bs[0] : probe.appBody
    }
    // a probe-made body over the same model, on the Analyse register, reading the carousel's shot
    function makeBody(props) {
        const b = bodyComp.createObject(probe, Object.assign({ source: probe.model, forceRegister: "Analyse" }, props))
        probe.made = probe.made.concat([b])
        probe.followReadout()
        b.readout = Qt.binding(function () { return probe.liveReadout })
        return b
    }
    function shownOne(root, name) { const o = find(root, name).filter(x => shown(x, root)); return o.length ? o[0] : null }
    function queueGrab(item, name) {
        if (probe.pngDir !== "") probe.grabs.push([item, probe.pngDir + "/" + name + ".png"])
    }

    Timer {
        interval: 1500; repeat: true; running: true
        onTriggered: {
            // One image per tick: grabToImage is asynchronous and a burst of them offscreen
            // races the theme switch that follows.
            if (probe.grabs.length > 0) {
                const g = probe.grabs.shift()
                g[0].grabToImage(function (res) { res.saveToFile(g[1]); probe.log("image " + g[1]) })
                return
            }
            probe.step += 1
            const s = probe.step
            if (s === 1) {
                probe.startTheme = Theme.themeIndex
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                navController.navigate(AppScreen.Session)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (s === 2) {
                probe.check(sessionReviewController.reviewActive, "the session loaded for review")
                let picked = false
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) { SessionMode.enterReplay(o.shotId, o.swingDir); picked = true }
                }
                probe.check(picked, "shot " + probe.ordinal + " is in the carousel")
            } else if (s === 3) {
                // The panel on, in the TABS arrangement on its own tab, whatever the copied settings'
                // layout says. The probe used to take the stage as it found it: a layout without the panel
                // failed before anything was checked, a split squeezed the row to a sliver, and a theme
                // switch rebuilds the panels (and their model) in split and stage, never in tabs.
                ViewLayout.setPanel(SessionMode.analyse, "sessionDiagnostics", true)
                ViewLayout.setArrangement(SessionMode.analyse, "tabs")
                SessionMode.enterAnalyse()
            } else if (s === 4) {
                const stages = []
                walk(probe.Window.window.contentItem, it => it.arrangement !== undefined && it.tabIndex !== undefined, stages)
                for (const st of stages) {
                    const i = (st.active || []).findIndex(d => d.key === "sessionDiagnostics")
                    if (i >= 0 && st.tabIndex !== i) st.tabIndex = i
                }
                const bodies = find(probe.Window.window.contentItem, "sdBody").filter(b => shown(b, null))
                if (bodies.length < 1 && probe.idleWaits++ < 6) { probe.step -= 1; return }
                probe.idleWaits = 0
                probe.check(bodies.length >= 1, "a Session diagnostics panel is up in Analyse")
                if (bodies.length < 1) { probe.finish(); return }
                probe.appBody = bodies[0]
                probe.model = probe.appBody.source
                probe.check(!!probe.model && typeof probe.model.spreadOf === "function",
                            "…on a model that publishes spreadOf()")
            } else if (s === 5) {
                // Let any regrade and the history scan land.
                if (probe.model.busy && probe.idleWaits++ < 40) { probe.step -= 1; return }
                const m = probe.model
                const sp = m.spreadOf(probe.condition)
                probe.check(!!sp && !!sp.dots, probe.condition + " has a spread")
                const run = sp.run || []
                let measured = 0, na = 0
                for (let i = 0; i < run.length; ++i) { if (run[i].assessable) ++measured; else ++na }
                probe.log("shots " + run.length + " · measured " + measured + " · not measured " + na
                          + " · axis " + sp.axisLo.toFixed(1) + "…" + sp.axisHi.toFixed(1)
                          + " · shape " + sp.shape)
                probe.log("caption: " + sp.caption)
                probe.log("fault: " + (sp.faultLines.length ? sp.faultLines[0].text : "none")
                          + " · tag: " + sp.normTag + " · clipped: " + (sp.clipHiText || "none"))
                probe.check(run.length === m.shotCount, "the value run has a mark for every shot (" + run.length + ")")
                probe.check(sp.dots.length === measured, "a strip dot for every measured shot (" + sp.dots.length + ")")
                probe.check(sp.notAssessable === na, "…the rest counted, not placed (" + na + ")")
                probe.check(sp.faultLines.length === 1 && Math.abs(sp.faultLines[0].value - 13) < 1e-6,
                            "one fault line, at the norm's 13 %")
                probe.check(sp.bands.length > 0 && sp.bands[0].grade === "open",
                            "a ceiling: the low side is drawn as not graded")
                const cur = sp.dots.filter(d => d.current)
                probe.check(cur.length === 1 && cur[0].index === probe.ordinal - 1,
                            "the ringed dot is the picked shot " + probe.ordinal)

                // ...and the REAL panel draws it.
                probe.appBody.tab = "session"
            } else if (s === 6) {
                const regs = probe.find(probe.appBody, "sdRegister")
                probe.check(regs.length === 1 && regs[0].register === "Analyse", "the Analyse register is loaded")
                const card = probe.cardFor(probe.appBody, probe.condition)
                probe.check(!!card, "the panel shows a ledger row for " + probe.condition)
                if (card) {
                    const sp = probe.model.spreadOf(probe.condition)
                    probe.check(probe.find(card, "sdBoxStrip").length === 0
                                && probe.find(card, "sdRunChart").length === 0,
                                "…carrying no box plot and no run chart (they are the detail's)")
                    const strip = probe.find(card, "sdShotStrip").filter(x => probe.shown(x, card))[0]
                    probe.check(!!strip && probe.find(strip, "sdShotCell").length === probe.model.shotCount,
                                "…and its shot strip, a cell per shot (" + (strip ? probe.find(strip, "sdShotCell").length : 0) + ")")
                    const med = probe.find(card, "sdSessionRowMedian")[0], cur = probe.find(card, "sdSessionRowShot")[0]
                    const unit = probe.find(card, "sdSessionRowUnit")[0]
                    probe.log("figures: median '" + (med ? med.text : "-") + "' · this shot '" + (cur ? cur.text : "-")
                              + "' (" + sp.currentState + ") · unit '" + (unit ? unit.text : "-") + "' · row "
                              + Math.round(card.width) + "×" + Math.round(card.height))
                    // a folded median carries its unit after it ("14 % hand rise"), so it is read as a prefix
                    probe.check(!!med && !!cur && String(med.text).indexOf(sp.medianNumber) === 0 && cur.text === sp.currentNumber,
                                "…with the session median (" + (med ? med.text : "-") + ") and this shot (" + (cur ? cur.text : "-") + ")")
                    let inside = true
                    for (const n of ["sdSessionRowMedian", "sdSessionRowShot", "sdSessionRowUnit"])
                        for (const it of probe.find(card, n)) {
                            const r = it.mapToItem(card, it.width, 0)
                            if (probe.shown(it, card) && r.x > card.width + 0.5) inside = false
                        }
                    probe.check(inside, "…inside the row at the real panel size")
                    // The corridor is the MODEL's line on the card; the typeset ledger draws it in the
                    // fired table's corridor column and the detail's feature head, not on the session row.
                    probe.log("corridor line: \"" + (card.modelData.corridorText || "") + "\"")
                    probe.check((card.modelData.corridorText || "") === "pass up to 8.7 · fault above 13 % hand rise",
                                "the card's corridor line states the pass band and the fault line")
                }
                probe.queueGrab(probe.appBody, "app_panel")

                // The pictures, at the design size and the 396 split.
                probe.wide = probe.makeBody({ x: 0, y: 0, width: 1168, height: 560, z: 100 })
                probe.narrow = probe.makeBody({ x: 0, y: 0, width: 396, height: 900, z: 101 })
                probe.wide.tab = "session"
                probe.narrow.tab = "session"
                Theme.themeIndex = 5
            } else if (s === 7) {
                probe.queueGrab(probe.wide, "session_wide_dark")
                probe.queueGrab(probe.narrow, "session_narrow_dark")
            } else if (s === 8) {
                // The detail scrolls; grab it tall enough to hold the strip, the run and the columns.
                probe.wide.height = 1100
                probe.narrow.height = 1500
                probe.model.openDetail(probe.condition)
            } else if (s === 9) {
                const d = probe.model.detail
                const h = d && d.history ? d.history : null
                probe.check(!!h && !!h.columns, "review: the detail carries the across-sessions columns")
                if (h && h.columns) {
                    probe.log("history: " + h.columns.map(c => c.label + "=" + c.medianText + " (n " + c.n + ")"
                                                         + (c.current ? "*" : "")).join("  ")
                              + " · " + h.caption)
                    probe.check(h.columns.length === probe.sessions,
                                "one column per session of this golfer (" + h.columns.length + ")")
                    // Oldest first (session folders are date-named, so their names sort by date), and the
                    // current column is THIS session. ("This session last" held only while it was the
                    // newest; the library has an 8 Oct session after 7 Oct now.)
                    const names = h.columns.map(c => String(c.sessionDir || "").replace(/\/+$/, "").split("/").pop())
                    const sorted = names.slice().sort()
                    const cur = h.columns.filter(c => c.current)
                    probe.check(names.join("|") === sorted.join("|"), "…oldest first")
                    probe.check(cur.length === 1 && String(cur[0].sessionDir || "").indexOf(probe.sessionName) >= 0,
                                "…and the current column is this session")
                }
                const hist = probe.shownOne(probe.narrow, "sdHistoryStrip")
                probe.check(!!hist && probe.find(hist, "sdHistoryColumn").length === (h && h.columns ? h.columns.length : -1),
                            "the 396 split draws the columns")
                const strip = probe.shownOne(probe.narrow, "sdBoxStrip")
                probe.check(!!strip && strip.mapToItem(probe.narrow, strip.width, 0).x <= 396 + 0.5,
                            "…and the box fits the split")
                const fr = probe.shownOne(probe.wide, "sdFigureRow")
                const vrW = probe.shownOne(probe.wide, "sdRunChart")
                const yt = fr ? probe.find(fr, "sdRunYTick").filter(t => t.visible) : []
                probe.log("detail run " + (vrW ? Math.round(vrW.height) : "-") + " px tall · Y labels "
                          + yt.map(t => t.text).join(" "))
                probe.check(!!vrW && vrW.height > 0 && yt.length >= 3,
                            "the detail run is drawn, with Y labels on the figure row's one axis")
                // The box (12 Oct: it replaced the spread strip's dots): its median is the model's
                // session median, and every shot past its whiskers, and the picked shot, is a mark with
                // its own pick target (a PpMark's own pick area, ≥ 12 px).
                const sW = probe.shownOne(probe.wide, "sdBoxStrip")
                const spD = probe.model.spreadOf(probe.condition)
                const medV = sW ? spD.axisLo + sW.fMedian * (spD.axisHi - spD.axisLo) : NaN
                probe.log("box: median " + medV.toFixed(3) + " (model " + spD.medianValue + ") · Q1 f " + (sW ? sW.fQ1.toFixed(3) : "-")
                          + " · Q3 f " + (sW ? sW.fQ3.toFixed(3) : "-") + " · outliers " + (sW ? sW.outliers.length : "-"))
                probe.check(!!sW && Math.abs(medV - spD.medianValue) < 1e-6 * Math.max(1, Math.abs(spD.axisHi - spD.axisLo)),
                            "the box's median is the model's session median")
                const dots = sW ? probe.find(sW, "sdBoxMark").filter(x => x.visible && x.role !== "hovered") : []
                let pickOk = sW && dots.length === sW.outliers.length + (sW.picked && !sW.isOutlier(sW.picked.shotId) ? 1 : 0)
                for (let i = 0; i < dots.length; ++i) {
                    let area = null
                    for (const k of dots[i].children) if (k.containsMouse !== undefined) area = k
                    if (!area || !area.enabled || area.width < 12) pickOk = false
                }
                probe.check(pickOk, "every one of " + dots.length + " box marks is a pick target of its own")
                probe.queueGrab(probe.wide, "detail_wide_dark")
                probe.queueGrab(probe.narrow, "detail_narrow_dark")
            } else if (s === 10) {
                probe.hoverIn("box", "dark")
            } else if (s === 11) {
                probe.hoverIn("run", "dark")
            } else if (s === 12) {
                probe.clearHover()
                Theme.themeIndex = 4
            } else if (s === 13) {
                probe.queueGrab(probe.wide, "detail_wide_light")
                probe.queueGrab(probe.narrow, "detail_narrow_light")
            } else if (s === 14 && probe._phase === 0) {
                probe._phase = 1; probe.step = 13
                probe.hoverIn("box", "light")
            } else if (s === 14 && probe._phase === 1) {
                probe._phase = 2; probe.step = 13
                probe.hoverIn("run", "light")
            } else if (s === 14 && probe._phase === 2) {
                probe._phase = 3; probe.step = 13
                probe.clearHover()
                probe.model.closeDetail()
            } else if (s === 14 && probe._phase === 3) {
                probe._phase = 4; probe.step = 13
                // The cards at the split, light then dark: two figures and the tick run.
                probe.narrow.height = 900
                probe.wide.height = 560
            } else if (s === 14 && probe._phase === 4) {
                probe._phase = 5; probe.step = 13
                probe.queueGrab(probe.narrow, "session_narrow_light")
                probe.queueGrab(probe.wide, "session_wide_light")
            } else if (s === 14 && probe._phase === 5) {
                probe._phase = 6; probe.step = 13
                Theme.themeIndex = 5
            } else if (s === 14 && probe._phase === 6) {
                probe._phase = 7; probe.step = 13
                probe.wide.destroy()
                probe.narrow.destroy()
                probe.model.openDetail(probe.condition)
            } else if (s === 14) {
                // A TAP ON THE APP'S OWN DETAIL RUN picks the shot through the carousel's path: the
                // run chart's shotActivated is exactly what a mark's click emits.
                probe.appBody = probe.liveAppBody()
                const vr = probe.shownOne(probe.appBody, "sdRunChart")
                probe.check(!!vr, "the app's condition detail has a run chart to tap")
                if (!vr) { probe.finish(); return }
                const run = vr.spread.run || []
                const target = Math.min(4, run.length - 1)          // shot 5
                probe._wantDir = run[target].swingDir
                vr.shotActivated(run[target].swingDir, run[target].shotId)
            } else if (s === 15) {
                const name = probe._wantDir.substring(probe._wantDir.lastIndexOf("/") + 1)
                probe.check(SessionMode.focusedSwingDir.endsWith(name),
                            "the tap moved the carousel's selection to " + name
                            + " (" + SessionMode.focusedSwingDir.substring(SessionMode.focusedSwingDir.lastIndexOf("/") + 1) + ")")
                probe.check(SessionMode.mode === SessionMode.analyse, "…and stayed in Analyse")
                const sp = probe.model.spreadOf(probe.condition)
                const cur = sp.dots.filter(d => d.current)
                probe.check(cur.length === 1 && cur[0].index === 4, "…and the ringed dot followed it to shot 5")
                const sel = sp.run.filter(r => r.selected)
                probe.check(sel.length === 1 && sel[0].index === 4, "…and so did the wide mark on the run")
            } else if (s === 16) {
                // ── THE LIVE CASE (Mark's: the panel on the replay view of a LIVE session) ──
                //
                // The model is put in the live tense over the same ledger — its `reviewing` binding
                // is broken on purpose, which is what a live session is to it — with the detail open.
                // Before the fix, picking a swing here republished a detail about the NEWEST shot.
                probe.model.reviewing = false
                probe.model.openDetail(probe.condition)
                probe.big = probe.makeBody({ x: 0, y: 0, width: 1168, height: 1100, z: 102 })
                probe.pickOrdinal(10)
            } else if (s === 17) {
                probe.checkDetailOn(9, "live, swing 10 picked")
                probe.queueGrab(probe.big, "detail_live_before_shot10")
            } else if (s === 18) {
                probe.pickOrdinal(30)
            } else if (s === 19) {
                probe.checkDetailOn(29, "live, swing 30 picked")
                probe.queueGrab(probe.big, "detail_live_after_shot30")
            } else if (s === 20) {
                probe.big.destroy()
                probe.finish()
            }
        }
    }
    property string _wantDir: ""
    property var big: null

    // The carousel's own click path, by ordinal.
    function pickOrdinal(n) {
        for (let i = 0; i < shots.count; ++i) {
            const o = shots.objectAt(i)
            // Analyse keeps Analyse — the panel's own path for a pick (PpSessionDiagnosticsPanel
            // _pickShot): enterReplay() would switch the stage and rebuild the panel, model and all.
            if (o.ordinal === n) {
                SessionMode.focusedShotId = o.shotId
                SessionMode.focusedSwingDir = o.swingDir
                SessionMode.enterAnalyse()
                return
            }
        }
        probe.check(false, "shot " + n + " is in the carousel")
    }
    // Every shot-dependent field of the OPEN detail, read off what the page draws.
    function checkDetailOn(idx, tag) {
        probe.followReadout()              // the probe bodies read the app's pick now, not one timer tick later
        const m = probe.model
        probe.check(m.detailConditionId === probe.condition, tag + ": the detail is still open on " + probe.condition)
        const d = probe.find(probe.big, "sdDetailBody")[0]
        probe.check(!!d && probe.shown(d, probe.big), tag + ": …and on screen")
        if (!d) return
        const sp = m.detail.header.spread
        const want = sp.run[idx]
        const line = probe.shownOne(d, "sdDetailHoverReadout")
        probe.log(tag + ": this shot " + (want.assessable ? sp.currentNumber : "-") + " (" + sp.currentState + ") · line \""
                  + (line ? line.text : "-") + "\"")
        probe.check(sp.currentReadout === want.readout && (want.assessable ? sp.currentNumber !== "-" : true),
                    tag + ": the model's this-shot figure and readout are shot " + (idx + 1) + "'s")
        probe.check(!!line && line.text === want.readout, tag + ": the run caption reads shot " + (idx + 1))
        const ringed = probe.find(probe.shownOne(d, "sdBoxStrip"), "sdBoxMark").filter(x => x.visible && x.ringed)
        probe.check(!want.assessable || (ringed.length === 1 && ringed[0].shotId === want.shotId),
                    tag + ": the box's ringed mark is shot " + (idx + 1))
        const sel = probe.find(probe.shownOne(d, "sdRunChart"), "sdRunSelected")
        probe.check(!want.assessable || (sel.length === 1 && sel[0].visible && sel[0].shotId === want.shotId),
                    tag + ": the run's ringed mark is shot " + (idx + 1))
        // (The state chip FIRED HERE / CLEAN HERE went with the pattern card: the typeset detail states
        // this shot in its key figures and colours only what is outside the corridor.)
    }
    property int _phase: 0

    // ── the linked hover, driven through the register's brush ────────────────
    //
    // Offscreen there is no pointer, so the probe does what a mark's pick area does on hover —
    // brush.hoverShot(shotId, true) — and verifies everything downstream: both charts holding that
    // shot, both lighting it, and the run caption reading it.
    // Shot choice: the box hovers the furthest-out reading, the run a clean one (never the picked shot).
    function hoverTargets(sp, pickedIdx) {
        let far = -1, farV = -Infinity, clean = -1
        for (let i = 0; i < sp.run.length; ++i) {
            const r = sp.run[i]
            if (i === pickedIdx) continue                    // a hovered PICKED shot keeps its pick ring
            if (r.assessable && r.value > farV) { farV = r.value; far = i }
            if (clean < 0 && r.state === "clean") clean = i
        }
        return { box: far, run: clean >= 0 ? clean : (pickedIdx === 0 ? 1 : 0) }
    }
    function hoverIn(chart, theme) {
        for (const b of [probe.wide, probe.narrow]) {
            const fr = probe.shownOne(b, "sdFigureRow")
            const strip = probe.shownOne(b, "sdBoxStrip")
            const vr = probe.shownOne(b, "sdRunChart")
            const line = probe.shownOne(b, "sdDetailHoverReadout")
            const tag = (b === probe.wide ? "wide " : "396 ") + theme + ", hover in the " + chart
            if (!fr || !strip || !vr || !line) { probe.check(false, tag + ": the figure row is drawn"); continue }
            const sp = fr.s
            const brush = strip.brush
            const idx = hoverTargets(sp, sp.run.findIndex(r => brush && brush.isPicked(r.shotId)))[chart]
            const id = sp.run[idx].shotId
            // what a pick area does on hover: the shared brush takes the shot
            brush.hoveredShot = -1
            brush.hoverShot(id, true)
            // on the box: its own mark when past a whisker, else the hovered echo on the centre line
            const dot = probe.find(strip, "sdBoxMark").filter(x => x.visible && x.shotId === id)[0]
            const mark = probe.find(vr, "sdRunMark").filter(x => x.shotId === id)[0]
            probe.check(strip.hoveredShot === id && vr.hoveredShot === id,
                        tag + ": both charts hold shot " + (idx + 1))
            probe.check((!sp.run[idx].assessable || (!!dot && dot.hovered && !!mark && mark.hovered)),
                        tag + ": …and both light it")
            probe.check(line.text === sp.run[idx].readout, tag + ": the line reads \"" + line.text + "\"")
        }
        probe.queueGrab(probe.wide, "detail_wide_" + theme + "_hover_" + chart)
        probe.queueGrab(probe.narrow, "detail_narrow_" + theme + "_hover_" + chart)
    }
    function clearHover() {
        for (const b of [probe.wide, probe.narrow]) {
            const strip = b ? probe.find(b, "sdBoxStrip")[0] : null
            if (strip && strip.brush) strip.brush.hoveredShot = -1
        }
    }
}
