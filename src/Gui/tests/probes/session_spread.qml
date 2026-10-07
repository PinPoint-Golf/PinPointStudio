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
        const cs = find(body, "sdPatternCard").filter(c => c.card && c.card.id === id && shown(c, body))
        return cs.length > 0 ? cs[0] : null
    }
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
                navController.navigate(2)
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
                SessionMode.enterAnalyse()
            } else if (s === 4) {
                const bodies = find(probe.Window.window.contentItem, "sdBody")
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
                const card = probe.cardFor(probe.appBody, probe.condition)
                probe.check(!!card, "the panel shows a card for " + probe.condition)
                if (card) {
                    const sp = probe.model.spreadOf(probe.condition)
                    probe.check(probe.find(card, "sdCorridorStrip").length === 0
                                && probe.find(card, "sdValueRun").length === 0,
                                "…carrying no strip and no value run")
                    probe.check(probe.shown(probe.find(card, "sdTickRun")[0], card), "…and its tick run, as before")
                    const figs = probe.find(card, "sdCardFigures")[0]
                    const med = probe.find(card, "sdCardMedian")[0], cur = probe.find(card, "sdCardCurrent")[0]
                    probe.log("figures: median " + med.text + " · this shot " + cur.text + " (" + sp.currentState + ")"
                              + " · unit '" + probe.find(card, "sdCardFiguresUnit")[0].text + "' · captions "
                              + probe.find(card, "sdCardMedianCaption")[0].visible
                              + " · box " + Math.round(figs.x) + "," + Math.round(figs.y) + " "
                              + Math.round(figs.width) + "×" + Math.round(figs.height)
                              + " in card " + Math.round(card.width) + "×" + Math.round(card.height))
                    probe.check(probe.shown(figs, card) && med.text === sp.medianNumber && cur.text === sp.currentNumber,
                                "…with the session median (" + med.text + ") and this shot (" + cur.text + ")")
                    probe.check(figs.x >= 0 && figs.x + figs.width <= card.width + 0.5
                                && figs.y + figs.height <= card.height + 0.5,
                                "…inside the card at the real panel size")
                    probe.log("corridor line: \"" + (card.card.corridorText || "") + "\"")
                    probe.check((card.card.corridorText || "") === "pass up to 8.7 · fault at 13 % hand rise",
                                "the card's corridor line states the pass band and the fault line")
                }
                probe.queueGrab(probe.appBody, "app_panel")

                // The pictures, at the design size and the 396 split.
                probe.wide = bodyComp.createObject(probe, { x: 0, y: 0, width: 1168, height: 560,
                                                            source: probe.model, z: 100 })
                probe.narrow = bodyComp.createObject(probe, { x: 0, y: 0, width: 396, height: 900,
                                                              source: probe.model, z: 101 })
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
                    probe.check(h.columns.length > 0 && h.columns[h.columns.length - 1].current,
                                "…oldest first, this session last")
                }
                const hist = probe.find(probe.narrow, "sdDetailHistory")[0]
                probe.check(!!hist && probe.shown(hist, probe.narrow), "the 396 split draws the columns")
                const strip = probe.find(probe.narrow, "sdDetailStrip")[0]
                probe.check(!!strip && probe.shown(strip, probe.narrow) && strip.width <= 396,
                            "…and the large strip fits the split")
                const vrW = probe.find(probe.wide, "sdDetailValueRun")[0]
                probe.log("detail run " + Math.round(vrW.height) + " px tall · Y labels "
                          + probe.find(vrW, "sdRunYTick").map(t => t.text).join(" ")
                          + " · history labels "
                          + probe.find(probe.wide, "sdHistoryYTick").map(t => t.text).join(" "))
                probe.check(vrW.height >= 2 * Math.round(120 * Theme.fontScale * vrW.fit) - 1
                            && probe.find(vrW, "sdRunYTick").length >= 3,
                            "the detail run is twice its old height, with Y labels")
                // Nearest-dot picking on the real strip: every dot's own centre picks that dot (or
                // one stacked exactly on it — the same place on the axis).
                const sW = probe.find(probe.wide, "sdDetailStrip")[0]
                const dots = probe.find(sW, "sdStripDot")
                let pickOk = true
                for (let i = 0; i < dots.length; ++i) {
                    const c = dots[i].mapToItem(sW, dots[i].width / 2, dots[i].height / 2)
                    const got = sW.nearestDot(c.x, c.y)
                    const g = got >= 0 ? dots[got].mapToItem(sW, dots[got].width / 2, dots[got].height / 2) : null
                    if (!g || Math.hypot(g.x - c.x, g.y - c.y) > 0.5) pickOk = false
                }
                probe.check(pickOk, "every one of " + dots.length + " stacked dots is picked by the nearest centre")
                probe.queueGrab(probe.wide, "detail_wide_dark")
                probe.queueGrab(probe.narrow, "detail_narrow_dark")
            } else if (s === 10) {
                probe.hoverIn("strip", "dark")
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
                probe.hoverIn("strip", "light")
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
                // A TAP ON THE APP'S OWN DETAIL RUN picks the shot through the carousel's path.
                const vr = probe.find(probe.appBody, "sdDetailValueRun")[0]
                probe.check(!!vr, "the app's condition detail has a value run to tap")
                if (!vr) { probe.finish(); return }
                const target = Math.min(4, vr.count - 1)          // shot 5
                probe._wantDir = vr.spread.run[target].swingDir
                vr.pick(target)
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
                probe.big = bodyComp.createObject(probe, { x: 0, y: 0, width: 1168, height: 1100,
                                                           source: probe.model, z: 102 })
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
        const m = probe.model
        probe.check(m.detailConditionId === probe.condition, tag + ": the detail is still open on " + probe.condition)
        const d = probe.find(probe.big, "sdDetailBody")[0]
        probe.check(!!d && probe.shown(d, probe.big), tag + ": …and on screen")
        const h = m.detail.header, sp = h.spread
        const head = probe.find(d, "sdDetailHeaderCard")[0]
        const cur = probe.find(head, "sdCardCurrent")[0]
        const want = sp.run[idx]
        probe.log(tag + ": header " + cur.text + " (" + sp.currentState + ") · chip "
                  + probe.find(head, "sdStatePill")[0].children[0].text + " · line \"" + sp.currentReadout + "\"")
        probe.check(cur.text === (want.assessable ? sp.currentNumber : "-") && sp.currentReadout === want.readout,
                    tag + ": the header figure and the readout are shot " + (idx + 1) + "'s")
        const ticks = probe.find(probe.find(head, "sdTickRun")[0], "sdTick")
        probe.check(!!ticks[idx] && ticks[idx].selected, tag + ": the tick run's wide tick is shot " + (idx + 1))
        const ringed = probe.find(probe.find(d, "sdDetailStrip")[0], "sdStripDot").filter(x => x.current)
        probe.check(!want.assessable || (ringed.length === 1 && ringed[0].modelData.index === idx),
                    tag + ": the strip's ringed dot is shot " + (idx + 1))
        const wide = probe.find(probe.find(d, "sdDetailValueRun")[0], "sdRunSelected")
        probe.check(!!wide[idx] && wide[idx].visible && wide.filter(x => x.visible).length === 1,
                    tag + ": the run's wide mark is shot " + (idx + 1))
        const pill = probe.find(head, "sdStatePill")[0].children[0].text
        probe.check(pill === (want.state === "fired" ? "FIRED HERE" : want.state === "clean" ? "CLEAN HERE" : "NOT MEASURED"),
                    tag + ": the chip is this shot's (" + pill + ")")
    }
    property int _phase: 0

    // ── the linked hover, driven through the charts' OWN signals ─────────────
    //
    // Offscreen there is no pointer, so the probe raises `hovered` on the chart exactly as its
    // pick area does; what is verified is everything downstream — the owner's one hovered-shot
    // property, the OTHER chart lighting the same shot, and the readout line between them.
    // Shot choice: the strip hovers the furthest-out reading, the run a clean one.
    function hoverTargets(sp) {
        let far = -1, farV = -Infinity, clean = -1
        for (let i = 0; i < sp.run.length; ++i) {
            const r = sp.run[i]
            if (r.assessable && r.value > farV) { farV = r.value; far = i }
            if (clean < 0 && r.state === "clean") clean = i
        }
        return { strip: far, run: clean >= 0 ? clean : 0 }
    }
    function hoverIn(chart, theme) {
        const sp = probe.model.spreadOf(probe.condition)
        const idx = hoverTargets(sp)[chart]
        for (const b of [probe.wide, probe.narrow]) {
            const strip = probe.find(b, "sdDetailStrip")[0]
            const vr = probe.find(b, "sdDetailValueRun")[0]
            const line = probe.find(b, "sdDetailHoverReadout")[0]
            if (chart === "strip") { vr.hovered(-1); strip.hovered(idx) } else { strip.hovered(-1); vr.hovered(idx) }
            const dot = probe.find(strip, "sdStripDot").filter(d => d.modelData.index === idx)[0]
            const mark = probe.find(vr, "sdRunMark")[idx]
            const tag = (b === probe.wide ? "wide " : "396 ") + theme + ", hover in the " + chart
            probe.check(strip.hoverIndex === idx && vr.hoverIndex === idx,
                        tag + ": both charts hold shot " + (idx + 1))
            probe.check(!!dot && dot.hot && !!mark && mark.hot, tag + ": …and both light it")
            probe.check(line.text === sp.run[idx].readout, tag + ": the line reads \"" + line.text + "\"")
        }
        probe.queueGrab(probe.wide, "detail_wide_" + theme + "_hover_" + chart)
        probe.queueGrab(probe.narrow, "detail_narrow_" + theme + "_hover_" + chart)
    }
    function clearHover() {
        for (const b of [probe.wide, probe.narrow]) {
            const strip = probe.find(b, "sdDetailStrip")[0]
            if (strip) strip.hovered(-1)
        }
    }
}
