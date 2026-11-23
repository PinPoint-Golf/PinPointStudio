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

// END-TO-END: THE ★ REFERENCE, GRABBED FOR A BEFORE/AFTER SHEET (refactor 5.5c). The ★ is set to a swing in ANOTHER
// session (--probe-reference; or one of this session's own, to check the in-session path), then, as charts_tabs.qml:
//   load the session → pick shot N → Analyse until the shot is graded → Charts alone → for each of
//     Traces against the ★: This shot split and overlaid, Session split and overlaid;  Relations;
//     Wrist & forearm against the ★: This shot, Session;  Posture against the ★: This shot (its picture strip), Session;
//   wait for the presenters, settle, log what the ★ says ("PROBE: REF …") and grab to <out>/<NN>-<tab>-<view>.png.
// The same file runs on the CONTROL and on the refactored build: it reads only chBody's properties and objectNames.
//
//   run through tools/probe/run_headless.sh (build/refactor/step-5/5.5c/sheets/run-sheets.sh):
//     --probe-qml <this file> --probe-session <session dir> --probe-reference <★ swing dir> --probe-out <png dir>
//     [--probe-ordinal 3] [--probe-window-h 4600] [--probe-settle 1500]
// Loading a session writes into it: use scratch copies.

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
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "3"))
    readonly property string outDir: arg("--probe-out", "")
    readonly property int settleMs: parseInt(arg("--probe-settle", "1500"))
    // tabs this session cannot show (a session with no HackMotion has no Wrist): --probe-skip wrist,delivery
    readonly property var skip: arg("--probe-skip", "").split(",").filter(function (x) { return x !== "" })
    readonly property string referenceDir: arg("--probe-reference", "")

    // ── helpers (as charts_posture.qml) ──
    function log(s) { console.warn("PROBE: " + s) }
    property int fails: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails += 1; return ok }
    function root() { return probe.Window.window.contentItem }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function shown(item, top) {
        let i = item
        while (i && i !== top) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function all(under, name) { const o = []; walk(under || root(), name, o); return o.filter(function (x) { return shown(x, root()) }) }
    function one(under, name) { return all(under, name)[0] || null }
    function words(it) {
        const tx = []
        const grab = function (x) {
            if (!x || !x.visible) return
            if (x.text !== undefined && typeof x.text === "string" && x.text !== "") tx.push(String(x.text).replace(/<[^>]*>/g, "").replace(/\s+/g, " ").trim())
            for (const k of (x.children || [])) grab(k)
        }
        grab(it)
        return tx.filter(function (t) { return t !== "" })
    }
    function sentence(s) { return !s ? "" : (typeof s === "string" ? s : (s.text || "")) }
    function short(t, n) { t = String(t || ""); return t.length > (n || 160) ? t.slice(0, n || 160) + "…" : t }
    function flickOf(it) { for (let p = it ? it.parent : null; p; p = p.parent) if (p.contentY !== undefined && p.contentItem !== undefined) return p; return null }

    // ── async: one action after another ──
    Component { id: timerC; Timer { repeat: false } }
    function after(ms, fn) {
        const t = timerC.createObject(probe, { interval: ms })
        t.triggered.connect(function () { t.destroy(); fn() })
        t.start()
    }
    function waitFor(cond, ms, what, fn) {
        const t0 = Date.now()
        const poll = function () {
            let ok = false
            try { ok = !!cond() } catch (e) { ok = false }
            if (ok) { fn(true); return }
            if (Date.now() - t0 > ms) { log("timed out after " + ms + " ms waiting for " + what); fn(false); return }
            after(250, poll)
        }
        poll()
    }
    property var queue: []
    function run(list) { queue = list; next() }
    function next() {
        if (!queue.length) { finish(); return }
        const f = queue.shift()
        try { f(next) } catch (e) { check(false, "a step threw: " + e); next() }
    }
    function finish() {
        log(fails === 0 ? "RESULT PASS" : "RESULT FAIL (" + fails + ")")
        Qt.exit(fails === 0 ? 0 : 1)
    }
    Timer { interval: 290000; running: true; onTriggered: { probe.check(false, "the probe finished inside 290 s"); probe.finish() } }

    Instantiator {
        id: shots
        model: app.sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    // ── the layout: exactly these stage panels in Analyse ──
    function layout(panels, arrangement) {
        const m = app.state.analyseMode
        for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup", "trust", "charts"])
            ViewLayout.setPanel(m, k, panels.indexOf(k) >= 0)
        ViewLayout.setArrangement(m, arrangement)
    }
    function body() { return one(null, "chBody") }
    // every Charts presenter idle, and the picture strips' stills made (body.busy omits posture and delivery)
    function idle(b) {
        if (!b) return false
        if (b.busy) return false
        for (const p of [b.tracesP, b.relationsP, b.wristP, b.postureP, b.deliveryP]) if (p && p.busy) return false
        const pp = b.pShot && b.pShot.picture, dp = b.dShot && b.dShot.picture
        if (b.postureShotShown && pp && pp.pending) return false
        if (b.deliveryShotShown && dp && dp.pending) return false
        return true
    }
    // the playhead pinned at impact, so the "now" cursor is the same in every run
    function pinPlayhead() {
        const r = app.state.replay
        if (!r || !r.active) return
        if (r.impactLoopPlaying) r.toggleImpactLoop()
        if (r.playing) r.togglePlay()
        r.seekToUs(r.impactUs)
    }

    // ── the grab. A grab of a Charts view alone comes back with no alpha (its page black, its ink premultiplied on
    // black), so the WINDOW is grabbed instead, made tall enough (--probe-window-h, default 4600) that the target, scrolled
    // to the top of its flick, is wholly on screen; "PROBE: GEO <name> x y w h" says where, and run-sheets.sh crops the
    // window PNG (<name>.window.png) to it (<name>.png). ──
    function winRoot() {
        const ci = probe.Window.window.contentItem
        let top = body() || one(null, "sdBody") || one(null, "trustPanel")
        while (top && top.parent && top.parent !== ci) top = top.parent
        return top
    }
    function grab(target, name, done) {
        if (!target || probe.outDir === "") { if (!target) check(false, name + ": something to grab"); done(); return }
        const top = winRoot()
        const p = target.mapToItem(null, 0, 0)
        const w = Math.round(target.width), h = Math.round(target.height)
        const vis = probe.Window.window.height - Math.round(p.y)
        log("GEO " + name + " " + Math.round(p.x) + " " + Math.round(p.y) + " " + w + " " + Math.min(h, vis) + (h > vis ? " (cut: " + h + " tall, " + vis + " on screen)" : ""))
        top.grabToImage(function (res) {
            const saved = res.saveToFile(probe.outDir + "/" + name + ".window.png")
            log("GRAB " + name + " " + w + "×" + h + (saved ? "" : " (save FAILED)"))
            done()
        })
    }
    // the target at the top of its flick
    function scrollTo(target) {
        const f = target ? flickOf(target) : null
        if (!f) return
        const y = target.mapToItem(f.contentItem, 0, 0).y
        f.contentY = Math.max(0, Math.min(y, f.contentHeight - f.height))
    }

    // ── one Charts view: choose, wait, settle, log, check, grab ──
    function chartsStep(nn, tabName, preset, tab, targetName, report, setup) {
        return function (done) {
            if (probe.skip.indexOf(tabName) >= 0) { log("SKIP " + nn + " " + tabName + " · " + tab + " (--probe-skip)"); done(); return }
            const b = body()
            if (!check(!!b, nn + " " + tabName + "/" + tab + ": a Charts body is shown")) { done(); return }
            b.collapsed = ({})
            b.choose("preset", preset)
            b.tab = tab
            if (setup) setup(b)
            after(400, function () {
                waitFor(function () { return probe.idle(body()) }, 60000, nn + " presenters idle", function () {
                    pinPlayhead()
                    after(probe.settleMs, function () {
                        waitFor(function () { return probe.idle(body()) }, 30000, nn + " presenters idle after the settle", function () {
                            const bb = body()
                            log("── " + nn + " " + tabName + " · " + tab + " · preset '" + ((bb.tChoices && bb.tChoices.preset) || "") + "' (asked '" + preset
                                + "') · shown view " + bb.shownView + " · register " + bb.register)
                            const target = report(bb)
                            scrollTo(target)
                            after(600, function () { grab(target, nn + "-" + tabName + "-" + tab, done) })
                        })
                    })
                })
            })
        }
    }

    function logRows(tag, rows) { log(tag + ": " + (rows || []).length + " rows [" + (rows || []).slice(0, 12).map(function (r) { return r.key || r.label || r.id || "?" }).join(", ") + "]") }

    // the Traces This shot view
    function rTracesShot(b) {
        const sh = b.tShot || {}
        log("kicker: " + (sh.kicker || "")); log("head: " + short(sentence(sh.head)))
        logRows("rows", sh.rows)
        log("presets: " + ((b.tChoices.presets || []).map(function (p) { return p.label + " (" + p.count + ")" }).join(", ")))
        log("wrist available " + b.wristAvailable + " · posture available " + b.postureAvailable + " · delivery available " + b.deliveryAvailable)
        const v = one(null, "chShotView")
        check(sh.available === true, "Traces This shot: the shot is available")
        check(!!v && v.height > 0 && !one(v, "chShotEmpty"), "Traces This shot: chShotView drawn, not its empty state")
        check(!!one(v, "chHead"), "Traces This shot: chHead shown")
        return v
    }
    function rTracesSession(b) {
        const se = b.tSession || {}
        log("kicker: " + (se.kicker || "")); log("head: " + short(sentence(se.head)))
        log("n " + se.n + " · aligned " + se.aligned + " · groups " + (se.groups || []).length)
        logRows("rows", se.rows)
        const v = one(null, "chSessionView")
        check(se.available === true, "Traces Session: the session is available (n " + se.n + ")")
        check(!!v && !one(v, "chSessionWhy") && !!one(v, "chSessionHead"), "Traces Session: chSessionHead drawn, not chSessionWhy")
        return v
    }
    function rRelations(b) {
        const rl = b.rRel || {}, st = b.rStatus || {}
        log("status: ready " + st.ready + " · shotCount " + st.shotCount + " · picked " + st.pickedOrdinal)
        log("head: " + short(sentence(rl.head)) + " · chance " + JSON.stringify(rl.chance ? { n: rl.chance.n, tests: rl.chance.tests, clear: rl.chance.clearCount } : {}))
        log("relations keys: " + Object.keys(rl).slice(0, 20).join(","))
        const v = one(null, "chRelationsView")
        check(rl.available === true, "Relations: available")
        check(!!v && !one(v, "chRelationsWhy"), "Relations: chRelationsView drawn, not chRelationsWhy")
        return v
    }
    function rKinds(b) {
        const kd = b.rKinds || {}
        log("head: " + short(sentence(kd.head)) + " · groups " + (kd.groups || []).length
            + " [" + (kd.groups || []).map(function (g) { return g.name + " " + g.n }).join(", ") + "]")
        const v = one(null, "chKindsView")
        check(kd.available === true, "Kinds: available")
        check(!!v && !one(v, "chKindsWhy"), "Kinds: chKindsView drawn, not chKindsWhy")
        return v
    }
    function rReadings(b) {
        const rs = b.rShot || {}
        log("readings: " + (rs.groups || []).length + " groups, " + (rs.groups || []).reduce(function (a, g) { return a + (g.rows || []).length }, 0)
            + " rows, apart " + (rs.apart || []).length + " · " + (rs.groups || []).map(function (g) { return (g.title || g.name || "?") + " " + (g.rows || []).length }).join(", "))
        const v = one(null, "chReadingsView")
        check(rs.available === true && (rs.groups || []).length > 0, "Readings: available with groups")
        check(!!v && !one(v, "chReadingsWhy"), "Readings: chReadingsView drawn, not chReadingsWhy")
        return v
    }
    function rWristShot(b) {
        const w = b.wShot || {}
        log("wrist on show " + b.wristOnShow + " · shot shown " + b.wristShotShown + " · kicker: " + (w.kicker || "") + " · head: " + short(w.headText || sentence(w.head)))
        const s = one(null, "chWristSection")
        log("sections: chWristHead " + all(s, "chWristHead").length + " · chWristScore " + all(s, "chWristScore").length + " · chWristMoves " + all(s, "chWristMoves").length)
        check(b.wristShotShown, "Wrist This shot: wristShotShown")
        check(!!s && s.height > 0, "Wrist This shot: chWristSection drawn")
        return s
    }
    function rWristSession(b) {
        const w = b.wSession || {}
        log("wrist session shown " + b.wristSessionShown + " · kicker: " + (w.kicker || "") + " · head: " + short(w.headText || sentence(w.head)) + " · pending " + w.pending)
        const s = one(null, "chWristSession")
        log("sections: chWristConsistency " + all(s, "chWristConsistency").length + " · chWristStyles " + all(s, "chWristStyles").length + " · chWristRecurring " + all(s, "chWristRecurring").length)
        check(b.wristSessionShown, "Wrist Session: wristSessionShown")
        check(!!s && s.height > 0, "Wrist Session: chWristSession drawn")
        return s
    }
    function rPostureShot(b) {
        const sh = b.pShot || {}
        log("posture shot shown " + b.postureShotShown + " · graded " + sh.graded + " · kicker: " + (sh.kicker || "") + " · head: " + short(sh.headText))
        log("pattern line: " + short(sh.patternLine) + " · findings " + (sh.findings || []).length + " · working well " + (sh.workingWell || []).length)
        const s = one(null, "chPostureSection")
        const rows = s ? all(s, "chPostureRow") : []
        log("rows " + rows.length + " · picture frames " + (s ? all(s, "chPosturePictureFrame").length : 0))
        check(b.postureShotShown, "Posture This shot: postureShotShown")
        check(!!s && rows.length > 0, "Posture This shot: chPostureSection with rows (" + rows.length + ")")
        return s
    }
    function rPostureSession(b) {
        const se = b.pSession || {}
        log("posture session shown " + b.postureSessionShown + " · kicker: " + (se.kicker || "") + " · head: " + short(se.headText))
        log("strip: " + short((se.strip && se.strip.line) || "") + " · departures " + (se.departures || []).length)
        const s = one(null, "chPostureSession")
        const rows = s ? all(s, "chPostureRow") : []
        log("rows " + rows.length + " · marks " + (s ? all(s, "chPostureMark").length : 0))
        check(b.postureSessionShown, "Posture Session: postureSessionShown")
        check(!!s && rows.length > 0, "Posture Session: chPostureSession with rows (" + rows.length + ")")
        return s
    }
    function rDeliveryShot(b) {
        const sh = b.dShot || {}
        log("delivery shot shown " + b.deliveryShotShown + " · kicker: " + (sh.kicker || "") + " · mode " + sh.mode + " · head: " + short(sh.headText))
        log("impact lines " + ((sh.impact && sh.impact.lines) || []).length + " · shape " + ((sh.shape && sh.shape.word) || "-") + " · picture available " + (sh.picture ? sh.picture.available : "-"))
        const s = one(null, "chDeliverySection")
        log("sections: chDeliveryHead " + all(s, "chDeliveryHead").length + " · chDeliveryStory " + all(s, "chDeliveryStory").length + " · chDeliveryImpactLine " + all(s, "chDeliveryImpactLine").length)
        check(b.deliveryShotShown, "Club delivery This shot: deliveryShotShown")
        check(!!s && s.height > 0, "Club delivery This shot: chDeliverySection drawn")
        return s
    }
    function rDeliverySession(b) {
        const se = b.dSession || {}
        log("delivery session shown " + b.deliverySessionShown + " · kicker: " + (se.kicker || "") + " · head: " + short(se.headText))
        const s = one(null, "chDeliverySession")
        log("sections: chDeliveryBandCol " + all(s, "chDeliveryBandCol").length + " · chDeliveryVaries " + all(s, "chDeliveryVaries").length + " · chDeliveryUsual " + all(s, "chDeliveryUsual").length)
        check(b.deliverySessionShown, "Club delivery Session: deliverySessionShown")
        check(!!s && s.height > 0, "Club delivery Session: chDeliverySession drawn")
        return s
    }

    // what the ★ shows: every line under the target that names it
    function logRef(tag, target) {
        const lines = words(target).filter(function (t) { return t.indexOf("★") >= 0 || t.toLowerCase().indexOf("reference") >= 0 })
        log("REF " + tag + ": " + (lines.length ? lines.slice(0, 8).map(function (t) { return short(t, 140) }).join(" | ") : "(no ★ line)"))
    }
    function refTraces(b) { const v = b.tab === "session" ? rTracesSession(b) : rTracesShot(b); logRef("traces " + b.tab + (b.overlay ? " overlaid" : " split"), v); return v }
    function refWrist(b) { const v = b.tab === "session" ? rWristSession(b) : rWristShot(b); logRef("wrist " + b.tab, v); return v }
    function refPosture(b) { const v = b.tab === "session" ? rPostureSession(b) : rPostureShot(b); logRef("posture " + b.tab, v); return v }
    function against(which) { return function (b) { b.against = which; b.difference = false } }

    // the Trust panel alone
    function trustStep(nn) {
        return function (done) {
            layout(["trust"], "tabs")
            waitFor(function () { const p = one(null, "trustPanel"); return !!p && words(one(p, "trustHead")).length > 0 }, 40000, "the Trust panel's head", function () {
                pinPlayhead()
                after(probe.settleMs, function () {
                    const p = one(null, "trustPanel")
                    const head = words(one(p, "trustHead")), ses = words(one(p, "trustSession"))
                    log("── " + nn + " trust · panel")
                    log("trust head: " + short(head.join(" | "), 300))
                    log("trust session: " + short(ses.slice(0, 6).join(" | "), 300))
                    check(!!p && head.length > 0, "Trust panel: trustHead has words")
                    check(ses.length > 0, "Trust panel: trustSession has words")
                    const f = one(p, "trustFlick")
                    if (f) f.contentY = 0
                    const target = p
                    after(600, function () { grab(target, nn + "-trust-panel", done) })
                })
            })
        }
    }
    // Session diagnostics alone, on its Consistency tab
    function consistencyStep(nn) {
        return function (done) {
            layout(["sessionDiagnostics"], "tabs")
            waitFor(function () { return !!one(null, "sdBody") }, 30000, "Session diagnostics' body", function () {
                one(null, "sdBody").tab = "consistency"
                waitFor(function () { const v = one(null, "sdConsistencyView"); return !!v && v.ready && !v.settling }, 90000, "the Consistency table", function () {
                    pinPlayhead()
                    after(probe.settleMs, function () {
                        const v = one(null, "sdConsistencyView")
                        log("── " + nn + " consistency · session")
                        if (v) {
                            log("kicker: " + (v.hd.kicker || "") + " · headline: " + short(JSON.stringify(v.hd.headline || {}), 200))
                            log("shotCount " + v.status.shotCount + " · picked " + v.status.pickedOrdinal + " · matrix rows " + v.matrixRows.length
                                + " · columns " + (v.matrix.columns || []).length + " · cells " + all(v, "sdConsistencyCell").length)
                        }
                        check(!!v && v.ready && !v.settling, "Consistency: ready and not settling")
                        check(!!v && v.status.shotCount > 0 && v.matrixRows.length > 0, "Consistency: a table with rows")
                        check(!!v && !!one(v, "sdConsistencyMatrix"), "Consistency: sdConsistencyMatrix drawn")
                        scrollTo(v)
                        after(600, function () { grab(v, nn + "-consistency-session", done) })
                    })
                })
            })
        }
    }

    Component.onCompleted: after(2500, function () { run([
        function (done) {
            if (!check(sessionDir !== "" && outDir !== "" && referenceDir !== "", "--probe-session, --probe-reference and --probe-out given")) { finish(); return }
            probe.Window.window.height = parseInt(arg("--probe-window-h", "4600"))
            appSettings.wristReferenceSwingDir = referenceDir
            log("the ★: " + (referenceDir.indexOf(sessionDir) === 0 ? "a shot of this session" : "a shot of another session"))
            app.navController.navigate(AppScreen.Session)
            app.state.enterCapture()
            app.sessionReviewController.loadSession(sessionDir)
            waitFor(function () { return shots.count > 0 }, 60000, "the carousel", function () { log("carousel: " + shots.count + " shots"); done() })
        },
        function (done) {
            let found = false
            for (let i = 0; i < shots.count; ++i) {
                const s = shots.objectAt(i)
                if (s.ordinal === probe.ordinal) { app.state.enterReplay(app.sessionReviewController.activeShots.shotRefOf(s.shotId), s.swingDir); found = true }
            }
            if (!check(found, "shot " + ordinal + " is on the carousel")) { finish(); return }
            after(2000, done)
        },
        function (done) {
            app.state.enterAnalyse()
            layout(["charts", "sessionDiagnostics"], "split")
            waitFor(function () { return !!body() }, 30000, "the Charts body", function () { done() })
        },
        function (done) {
            const b = body()
            b.choose("preset", "Posture"); b.tab = "shot"
            waitFor(function () { const bb = body(); const sh = bb ? (bb.pShot || {}) : {}; return probe.idle(bb) && !!bb.postureP && (sh.graded === true || sh.available !== true) },
                    150000, "the picked shot's diagnostics", function (ok) { done() })
        },
        function (done) {
            layout(["charts"], "tabs")
            waitFor(function () { return !!body() }, 30000, "the Charts body alone", function () { body().choose("preset", ""); after(1000, done) })
        },
        chartsStep("R01", "traces", "", "shot", "chShotView", refTraces, function (b) { b.against = "reference"; b.difference = false; b.overlay = false }),
        chartsStep("R02", "traces", "", "shot", "chShotView", refTraces, function (b) { b.against = "reference"; b.difference = false; b.overlay = true }),
        chartsStep("R03", "traces", "", "session", "chSessionView", refTraces, function (b) { b.against = "reference"; b.difference = false; b.overlay = false }),
        chartsStep("R04", "traces", "", "session", "chSessionView", refTraces, function (b) { b.against = "reference"; b.difference = false; b.overlay = true }),
        chartsStep("R05", "traces", "", "relations", "chRelationsView", rRelations, function (b) { b.against = "none"; b.overlay = false }),
        chartsStep("R06", "wrist", "Wrist & forearm", "shot", "chWristSection", refWrist, function (b) { b.wristAgainst = "reference" }),
        chartsStep("R07", "wrist", "Wrist & forearm", "session", "chWristSession", refWrist, function (b) { b.wristAgainst = "reference" }),
        chartsStep("R08", "posture", "Posture", "shot", "chPostureSection", refPosture, function (b) { b.postureAgainst = "reference" }),
        chartsStep("R09", "posture", "Posture", "session", "chPostureSession", refPosture, function (b) { b.postureAgainst = "reference" }),
        function (done) { const b = body(); if (b) { b.choose("preset", ""); b.tab = "shot"; b.against = "none"; b.overlay = false } after(500, done) }
    ]) })
}
