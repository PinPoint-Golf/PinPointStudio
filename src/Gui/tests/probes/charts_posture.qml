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

// END-TO-END: THE POSTURE SECTION OF CHARTS ON REAL DATA (docs/design/posture_chart_design.md §5, stage 3a).
//
//   load a session → pick shot N with the carousel's click path → Analyse (or the glance) → Charts on the
//   Posture preset → wait for ChartsPosturePresenter → report the section's head, pattern line, each row's cells,
//   findings / working well, "At address" and what moved (This shot), or the session's head, strip, departures,
//   "At address" and repeatability (Session) → PASS/FAIL checks on the objectNames → GEO lines → grab the section.
//   A freshly copied session regrades on open: the probe waits (up to five minutes) until its diagnostics have
//   graded the picked shot before it logs or grabs anything.
//
// Run through --probe-qml (dev builds only), offscreen, under tools/typeset-harness/guard.sh:
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 2 [--probe-png <abs path>] [--probe-window-png <abs path>]
//   --probe-mode analyse | glance — Analyse (default); the glance at the mat (Replay, autoReturnToCapture)
//   --probe-tab shot | session (default shot)
//   --probe-pattern -1|0|1, --probe-against address|previous|reference|median — the section's choices (not saved)
//   --probe-panels camera,table — the panels beside Charts (default none: Charts alone, the widest section)
//   --probe-trust open — open the section's "How far can I trust this?" before the grab
//   --probe-fold posture|postureSession — fold that section before the checks (its one-line state is logged)
// "PROBE:" lines carry the findings; "PROBE: GEO <objectName> x y w h" the window geometry of each key item
// (for review-sheet crops), "PROBE: PASS|FAIL …" the checks. Exit 0 = every check passed.
// Loading a session writes into it: use a scratch copy.

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
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "2"))
    readonly property string png: arg("--probe-png", "")
    readonly property string windowPng: arg("--probe-window-png", "")
    readonly property string mode: arg("--probe-mode", "analyse")
    readonly property string tab: arg("--probe-tab", "shot")
    readonly property bool glance: mode === "glance"
    property int picWaits: 0
    property int gradeWaits: 0
    property int gradePhase: 0
    // the "At address" block (§7.4.2): the payload's rows and the drawn rows, checked and logged
    function logAddress(under, block) {
        const ad = probe.one(under, "chPostureAddress")
        const rows = (block && block.rows) || []
        probe.check(!!ad === rows.length > 0, "chPostureAddress " + (ad ? "shown" : "not shown") + " with " + rows.length + " address reading(s)")
        const drawn = ad ? probe.all(ad, "chPostureAddressRow") : []
        probe.check(drawn.length === rows.length, "a chPostureAddressRow per reading (" + drawn.length + ")")
        probe.check(drawn.every(r => r.warned) && (ad ? probe.all(ad, "chPostureAddressWarn").length : 0) === drawn.length, "the ⚠ and its words under every row")
        for (const r of rows)
            probe.log("  address " + r.measureId + " · " + r.label + " (" + r.camera + ", " + r.unit + "): " + r.text + " " + (r.valueWord || "")
                      + " · " + r.bandText + (r.bandSource ? " [" + r.bandSource + " @" + r.bandContext + "]" : "") + " · " + r.boxText
                      + " · " + (r.hasHistory ? r.historyText : "no history: " + r.historyWhy) + " · " + (r.warned ? r.warn + " " + r.warnShort : "no ⚠")
                      + " · scale " + Number(r.xLo).toFixed(1) + "…" + Number(r.xHi).toFixed(1))
        for (const l of ((block && block.lines) || [])) probe.log("  address trust: " + l)
    }

    function log(s) { console.warn("PROBE: " + s) }
    property int fails: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails += 1 }
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
    function all(under, name) { const o = []; walk(under, name, o); return o.filter(function (x) { return shown(x, probe.Window.window.contentItem) }) }
    function one(under, name) { return all(under, name)[0] || null }
    function words(it) {
        const tx = []
        const grab = function (x) {
            if (!x || !x.visible) return
            if (x.text !== undefined && typeof x.text === "string" && x.text !== "" && x.elide !== undefined) tx.push(String(x.text).replace(/<[^>]*>/g, ""))
            for (const k of (x.children || [])) grab(k)
        }
        grab(it)
        return tx
    }
    function geo(name, it) {
        if (!it) return
        const p = it.mapToItem(null, 0, 0)
        log("GEO " + name + " " + Math.round(p.x) + " " + Math.round(p.y) + " " + Math.round(it.width) + " " + Math.round(it.height))
    }
    function flickOf(it) { for (let p = it ? it.parent : null; p; p = p.parent) if (p.contentY !== undefined && p.contentItem !== undefined) return p; return null }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    // the section grab: a source of the section on the page colour, off to the side of nothing (not shown)
    Rectangle { id: holder; x: 0; y: 0; z: -100; visible: holderSrc.sourceItem !== null
                ShaderEffectSource { id: holderSrc; anchors.fill: parent; live: false; recursive: false; hideSource: false } }
    property var afterGrab: null
    Timer { id: grabber; interval: 400; onTriggered: holder.grabToImage(function (res) { res.saveToFile(probe.png); if (probe.afterGrab) probe.afterGrab() }) }

    property int step: 0
    property int waits: 0
    property var body: null
    property var section: null
    Timer {
        id: tick
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const root = probe.Window.window.contentItem
            if (probe.step === 1) {
                navController.navigate(2)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (probe.step === 2) {
                let found = false
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal === probe.ordinal) { SessionMode.enterReplay(s.shotId, s.swingDir); found = true }
                }
                probe.log("shot " + probe.ordinal + (found ? " picked" : " NOT in the carousel"))
                if (!found) Qt.exit(1)
            } else if (probe.step === 3) {
                if (!probe.glance) SessionMode.enterAnalyse()
                else SessionMode.autoReturnToCapture = true   // the auto-replay after a shot: the glance
                const m = probe.glance ? SessionMode.replay : SessionMode.analyse
                const besideArg = probe.arg("--probe-panels", "")
                const asked = besideArg === "" ? [] : besideArg.split(",")
                // the grading phase: Session diagnostics is up (its model is what grades a freshly copied session on
                // open — it exists only with its panel), then the layout asked for once the picked shot is graded
                const beside = probe.gradePhase === 0 && asked.indexOf("sessionDiagnostics") < 0 ? asked.concat(["sessionDiagnostics"]) : asked
                for (const k of ["camera", "swing3d", "launchMonitor", "sessionDiagnostics", "table", "markup", "trust"])
                    ViewLayout.setPanel(m, k, beside.indexOf(k) >= 0)
                ViewLayout.setPanel(m, "charts", true)
                ViewLayout.setArrangement(m, beside.length ? "split" : "tabs")
                // --probe-coach-sets posture,plane,headHips: the coach-line sets the picture draws (else the settings')
                const coachArg = probe.arg("--probe-coach-sets", "")
                if (coachArg !== "")
                    for (const s of ["posture", "plane", "headHips"]) ViewLayout.setCoachSet(m, s, coachArg.split(",").indexOf(s) >= 0)
            } else if (probe.step === 4) {
                const bodies = []
                probe.walk(root, "chBody", bodies)
                probe.body = bodies.filter(b => probe.shown(b, root))[0] || bodies[0] || null
                if (!probe.body) { probe.log("FAIL no Charts body"); Qt.exit(1); return }
                const b = probe.body
                probe.log("mode " + SessionMode.mode + " register " + b.register)
                b.choose("preset", "Posture")
                b.tab = probe.tab
                // the section's choices, set without saving (the probe's settings are a copy either way)
                const pat = probe.arg("--probe-pattern", ""), ag = probe.arg("--probe-against", "")
                if (pat !== "") b.posturePattern = Number(pat)
                if (ag !== "") b.postureAgainst = ag
                const c = {}
                const fold = probe.arg("--probe-fold", "")
                if (fold !== "") c[fold] = true
                b.collapsed = c
            } else if (probe.step === 5) {
                // wait for the presenters (a coalesced job each)
                const b = probe.body
                if ((b.busy || !b.postureP || b.postureP.busy) && probe.waits < 20) { probe.waits += 1; probe.step -= 1; return }
                // the picture strip's stills are made off the GUI thread after the section lands (stage 5)
                const pic0 = (b.pShot && b.pShot.picture) || null
                if (probe.tab === "shot" && pic0 && pic0.pending && probe.picWaits < 120) { probe.picWaits += 1; probe.step -= 1; return }
                // a freshly copied session regrades on open: wait until its diagnostics have graded the picked shot
                // (findings available, not "Diagnostics have not graded this swing yet"), up to five minutes
                const sh0 = b.pShot || {}
                if (sh0.available && !sh0.graded && probe.gradeWaits < 120) {
                    if (probe.gradeWaits % 8 === 0) probe.log("waiting for the diagnostics to grade shot " + probe.ordinal + " (" + probe.gradeWaits + ")")
                    probe.gradeWaits += 1; probe.step -= 1; return
                }
                if (probe.gradePhase === 0) {
                    probe.log("diagnostics graded the picked shot: " + (sh0.graded === true) + (probe.gradeWaits ? " (after " + probe.gradeWaits + " waits)" : ""))
                    probe.gradePhase = 1
                    if (probe.arg("--probe-panels", "").split(",").indexOf("sessionDiagnostics") < 0) {
                        // back to the layout asked for: lay the panels out again and find the Charts body afresh
                        probe.waits = 0; probe.step = 2; return
                    }
                }
                probe.check(sh0.graded === true || !sh0.available, "the diagnostics graded the picked shot before the section is read")
                probe.log("preset '" + (b.tChoices.preset || "") + "' · posture on show " + b.postureOnShow + ", available " + b.postureAvailable
                          + ", shot shown " + b.postureShotShown + ", session shown " + b.postureSessionShown)
                probe.log("choices: " + (b.pChoices.summary || ""))
                if (probe.tab === "shot") {
                    const sh = b.pShot || {}
                    probe.log("kicker: " + (sh.kicker || ""))
                    probe.log("head: " + (sh.headText || ""))
                    probe.log("pattern line: " + (sh.patternLine || ""))
                    probe.log("standfirst: " + ((b.pStandfirst && b.pStandfirst.text) || ""))
                    probe.section = probe.one(root, "chPostureSection")
                    probe.check(b.postureShotShown, "postureShotShown on the Posture preset")
                    probe.check(!!probe.section && probe.section.height > 0, "chPostureSection shown")
                    if (probe.section) {
                        const sec = probe.section
                        const folded = !!b.collapsed.posture
                        const fh = probe.one(sec, "chFold:posture")
                        probe.log("fold head: " + probe.words(fh).join(" ¦ "))
                        if (folded) {
                            probe.check(probe.all(sec, "chPostureProfile").length === 0, "folded: no profile")
                        } else {
                            probe.check(!!probe.one(sec, "chPostureHead"), "chPostureHead")
                            probe.check(!!probe.one(sec, "chPosturePattern"), "chPosturePattern")
                            const prof = probe.one(sec, "chPostureProfile")
                            probe.check(!!prof, "chPostureProfile")
                            const rows = prof ? probe.all(prof, "chPostureRow") : []
                            probe.check(rows.length === 6, "six chPostureRow (" + rows.length + ")")
                            probe.check((prof ? probe.all(prof, "chPostureCol") : []).length === 8, "eight chPostureCol")
                            for (const r of rows) {
                                const cells = probe.all(r, "chPostureCell")
                                probe.log("  row " + r.key + " [" + probe.words(r).slice(0, 3).join(" · ") + "]: "
                                          + cells.map(c => c.tag + " " + probe.words(c).join(" ")).join(" | "))
                            }
                            const ctl = probe.all(sec, "chPostureControls").length
                            if (probe.glance) {
                                probe.check(ctl === 0, "glance: no chPostureControls")
                                probe.check(probe.all(sec, "chPostureFindings").length === 0, "glance: no chPostureFindings")
                                probe.check(probe.all(sec, "chPostureMoved").length === 0, "glance: no chPostureMoved")
                            } else {
                                probe.check(ctl === 1, "chPostureControls")
                                probe.log("controls: " + probe.words(probe.one(sec, "chPostureControls")).join(" "))
                                probe.check(!!probe.one(sec, "chPostureFindings"), "chPostureFindings")
                                probe.check(!!probe.one(sec, "chPostureWorkingWell"), "chPostureWorkingWell")
                                for (const f of (sh.findings || [])) probe.log("  finding: " + f.severityWord + " · " + f.name + " · " + (f.positionsText || "") + " · " + (f.seekText || ""))
                                for (const f of (sh.workingWell || [])) probe.log("  working well: " + f.name + " · " + (f.positionsText || ""))
                                if (!(sh.findings || []).length) probe.log("  findings empty: " + (sh.findingsEmpty || ""))
                                const mv = probe.one(sec, "chPostureMoved")
                                probe.check(!!mv, "chPostureMoved")
                                const wm = sh.whatMoved || {}
                                probe.log("what moved: top '" + ((wm.top && wm.top.line) || "") + "' · impact '" + ((wm.impact && wm.impact.line) || "") + "'")
                                const tr = probe.one(sec, "chPostureTrust")
                                probe.check(!!tr, "chPostureTrust (closed under the profile)")
                                if (tr && probe.arg("--probe-trust", "") === "open") tr.open = true
                                for (const t of ((sh.trust && sh.trust.lines) || [])) probe.log("  trust: " + t)
                                probe.logAddress(sec, sh.address)
                            }
                            // the picture strip (stage 5): Analyse and the glance alike
                            const pic = probe.one(sec, "chPosturePicture")
                            const pm = sh.picture || {}
                            probe.check(!!pic, "chPosturePicture")
                            const frames = pic ? probe.all(pic, "chPosturePictureFrame") : []
                            probe.check(frames.length === (pm.groups || []).reduce((a, g) => a + (g.frames || []).length, 0),
                                        "a chPosturePictureFrame per frame (" + frames.length + ")")
                            probe.log("picture: " + frames.length + " frames, pending " + pm.pending + "; legend: " + (pm.legend || "")
                                      + ((pm.why || []).length ? "; why: " + pm.why.join(" ") : ""))
                            for (const f of frames) {
                                probe.log("  frame " + f.camera + " " + f.tag + " " + Math.round(f.width) + "×" + Math.round(f.height) + " image " + f.hasImage
                                          + " coach " + f.coachDrawn + " band " + f.bandDrawn + " ink " + f.inkDrawn + " seek " + f.seekUs
                                          + " | " + (f.frame.caption || "") + (f.frame.note ? " | " + f.frame.note : "") + (f.frame.imageWhy ? " | " + f.frame.imageWhy : ""))
                                for (const m of (f.frame.marks || []))
                                    if (m.role !== "coach" || (f.sets && f.sets[m.set]))
                                        probe.log("    GEO " + f.camera + "_" + f.tag + " " + m.role + " " + m.kind + " '" + m.label + "' "
                                                  + (m.kind === "rect" ? "x " + m.x.toFixed(1) + " y " + m.y.toFixed(1) + " w " + m.w.toFixed(1) + " h " + m.h.toFixed(1)
                                                     : m.kind === "vline" ? "x " + m.x.toFixed(1)
                                                     : m.kind === "circle" ? "cx " + m.cx.toFixed(1) + " cy " + m.cy.toFixed(1) + " r " + m.r.toFixed(1)
                                                     : (m.pts || []).map(q => q[0].toFixed(1) + "," + q[1].toFixed(1)).join(" "))
                                                  + " (frame px; drawn ×" + f.s.toFixed(3) + ")")
                            }
                            if (!probe.glance && frames.length > 0 && typeof shotReplay !== "undefined" && shotReplay) {
                                probe.log("picture seek target of " + frames[0].camera + " " + frames[0].tag + ": " + frames[0].seekUs + " (the profile's P1: " + b.postureP.seekUsFor("P1") + ")")
                            }
                        }
                    }
                } else {
                    const se = b.pSession || {}
                    probe.log("kicker: " + (se.kicker || ""))
                    probe.log("head: " + (se.headText || ""))
                    probe.log("pattern line: " + (se.patternLine || ""))
                    const slot = probe.one(root, "chPostureSessionSlot")
                    if (probe.glance) {
                        probe.check(!b.postureSessionShown && !slot, "glance: no chPostureSessionSlot")
                    } else {
                        probe.check(b.postureSessionShown, "postureSessionShown")
                        probe.check(!!slot, "chPostureSessionSlot")
                        probe.section = probe.one(root, "chPostureSession")
                        const ses = probe.section
                        probe.check(!!ses, "chPostureSession")
                        if (ses) {
                            const prof = probe.one(ses, "chPostureProfile")
                            const rows = prof ? probe.all(prof, "chPostureRow") : []
                            probe.check(rows.length === 6, "six chPostureRow in Session (" + rows.length + ")")
                            for (const r of rows) {
                                const cells = probe.all(r, "chPostureCell")
                                probe.log("  row " + r.key + ": " + cells.map(c => c.tag + " " + probe.words(c).join(" ")).join(" | "))
                            }
                            const strip = probe.one(ses, "chPostureStrip")
                            probe.check(!!strip, "chPostureStrip")
                            probe.log("strip: " + ((se.strip && se.strip.line) || "") + " · marks " + (strip ? probe.all(strip, "chPostureMark").map(m => m.ordinal + (m.differs ? "◯" : "▮") + (m.picked ? "*" : "")).join(" ") : ""))
                            probe.check(!!probe.one(ses, "chPostureDepartures"), "chPostureDepartures")
                            for (const d of (se.departures || [])) probe.log("  departure: " + (d.line || ""))
                            if (!(se.departures || []).length) probe.log("  departures empty: " + (se.departuresEmpty || ""))
                            probe.logAddress(ses, se.address)
                            const rp = probe.one(ses, "chPostureRepeat")
                            probe.log("repeat (" + (rp ? "shown" : "not shown") + "): " + ((se.repeat && se.repeat.words) || ""))
                        }
                    }
                }
                // the section at the top of its scroll, for the GEO lines and a window grab
                const f = probe.section ? probe.flickOf(probe.section) : null
                if (f) f.contentY = Math.max(0, Math.min(probe.section.mapToItem(f.contentItem, 0, 0).y - 10, f.contentHeight - f.height))
            } else if (probe.step === 6) {
                const b = probe.body
                probe.geo("chBody", b)
                const names = ["chPostureSection", "chPostureHead", "chPosturePattern", "chPostureControls", "chPostureProfile",
                               "chPostureAddress", "chPostureMoved", "chPostureFindings", "chPostureWorkingWell", "chPostureSession", "chPostureStrip",
                               "chPostureDepartures", "chPostureRepeat", "chPosturePicture", "chPosturePictureFrame"]
                for (const n of names) for (const it of probe.all(b, n)) probe.geo(n, it)
                const rows = probe.all(b, "chPostureRow")
                for (let i = 0; i < rows.length; ++i) probe.geo("chPostureRow" + i, rows[i])
                const arows = probe.all(b, "chPostureAddressRow")
                for (let i = 0; i < arows.length; ++i) probe.geo("chPostureAddressRow" + i + ":" + arows[i].measureId, arows[i])
            } else if (probe.step === 7) {
                tick.running = false
                const done = function () { probe.log(probe.fails === 0 ? "RESULT PASS" : "RESULT FAIL (" + probe.fails + ")"); Qt.exit(probe.fails === 0 ? 0 : 1) }
                const target = probe.section
                const grabSection = function () {
                    if (probe.png === "" || !target) { done(); return }
                    probe.log("grabbing the section " + Math.round(target.width) + "×" + Math.round(target.height) + " → " + probe.png)
                    // the section draws on the card's surface: grab it through a source laid on the page colour
                    holder.width = target.width; holder.height = target.height
                    holder.color = Theme.colorSurface
                    holderSrc.sourceItem = target
                    holderSrc.scheduleUpdate()
                    probe.afterGrab = done
                    grabber.start()
                }
                if (probe.windowPng !== "") {
                    // the contentItem itself has no QML engine to grab with: its top QML child (the app's root, at 0,0)
                    const ci = probe.Window.window.contentItem
                    let top = probe.body
                    while (top && top.parent && top.parent !== ci) top = top.parent
                    const p = top.mapToItem(null, 0, 0)
                    probe.log("grabbing the window's root " + Math.round(p.x) + "," + Math.round(p.y) + " " + Math.round(top.width) + "×" + Math.round(top.height) + " → " + probe.windowPng)
                    top.grabToImage(function (res) { res.saveToFile(probe.windowPng); grabSection() })
                } else grabSection()
            }
        }
    }
}
