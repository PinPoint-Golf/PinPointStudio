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
import QtTest
import PinPointStudio
import "charts"

// THE CHARTS PANEL — the four views (and the Wrist tab, test_21–25) over REAL data (docs/design/charts_panel_traces_relations.md;
// build/charts-build/CONTRACT.md D1–D9 and its Addendum; build/charts-build/STEP3.md).
//
// The real PpChartsPanel → PpChartsBody ("chBody") → register → views, over the real
// ChartsTracesPresenter and ChartsRelationsPresenter, fed the real 4 July session (15 shots of a 7 iron;
// shot 8 picked) through `chartsFixture`. Every number and sentence asserted here is the presenters' —
// the C++ suites (charts_traces_presenter_test, charts_relations_presenter_test) pin the arithmetic; THIS
// suite pins that the panel puts it on screen, where a reader can press it, and that a press reaches the
// real path. Controls are pressed with the pointer, never set, except where a test says why.
//
// The single-shot controls the Addendum kept (metric on/off, overlay, P dots, cursor, the window, the folds)
// are tst_chart_presets.qml's.
Item {
    id: probe
    width: 1400; height: 900

    ChartsHarness { id: hn }

    // The 4 July fixture's conditions by how often they fired (of 15): fired on EVERY shot, or on fewer
    // than three — neither can split the session, so neither is offered as a facet.
    readonly property var alwaysFired: ["early_extension", "hips_closed_at_impact", "over_the_top", "pelvis_thrust_backswing"]
    readonly property var tooRare: ["steep_downswing_shaft", "hips_under_rotated_at_top", "off_balance_finish", "ball_back",
                                    "overswing", "deceleration", "slide"]

    TestCase {
        id: tc
        name: "ChartsPanel"
        when: windowShown

        // ── helpers ──────────────────────────────────────────────────────────
        function body() { return hn.body }
        function settle() { hn.settle(tc) }
        function shownAll(name, under) { return hn.shownAll(under || hn.body, name) }
        function oneShown(name, under) {
            const all = shownAll(name, under)
            verify(all.length >= 1, "expected a '" + name + "' on screen, found none (" + hn.findAll(under || hn.body, name).length + " hidden)")
            return all[0]
        }
        function textOf(item) { return hn.allText(item) }
        function intoView(item) {
            const f = hn.flickOf(item)
            if (!f) return
            const y = item.mapToItem(f.contentItem, 0, 0).y
            const hgt = Math.max(item.height, 1)
            if (y < f.contentY || y + hgt > f.contentY + f.height)
                f.contentY = Math.max(0, Math.min(y - 20, f.contentHeight - f.height))
            wait(16); waitForRendering(hn, 60)
        }
        function press(item) { intoView(item); mouseClick(item); settle() }
        // a ledger row opens from its name (its MouseArea spans the name, value and unit columns)
        function pressAt(item, x, y) { intoView(item); mouseClick(item, x, y); settle() }
        function tabKeys() { return shownAll("sdTab").map(function (t) { return t.key }).join(",") }
        function tabPress(key) {
            const t = shownAll("sdTab").filter(function (x) { return x.key === key })
            verify(t.length === 1, "one '" + key + "' tab on screen")
            press(t[0])
        }
        // a quiet control line's option (PpChChoices): press its word
        function option(set, key) {
            const o = shownAll("chOption").filter(function (x) { return x.setKey === set && x.key === key })
            verify(o.length >= 1, "an option '" + key + "' in the '" + set + "' line")
            return o[0]
        }
        function clickOption(set, key) {
            const o = option(set, key)
            const word = o.children.filter(function (k) { return hn.isText(k) })[0]
            press(word)
        }
        // an in-place list's entry (PpInlinePicker)
        function pickInline(name, id) {
            const it = shownAll(name).filter(function (x) { return x.missId === id })
            verify(it.length === 1, "one '" + id + "' in the open " + name)
            press(it[0])
        }
        // "Replay" is the glance register: the auto-replay after a shot (a Replay the golfer opens reads Analyse)
        function useMode(m) {
            SessionMode.autoReturnToCapture = m === "Replay"
            SessionMode.mode = m === "Analyse" ? SessionMode.analyse : SessionMode.replay
            settle()
            compare(hn.body.register, m, "the body loaded the " + m + " register")
        }
        function hueOk(c, key) { return Qt.colorEqual(c, Theme.metricColor(key)) }
        function layers(row, pred) { const p = hn.plotOf(row); return p ? p.layers.filter(pred) : [] }
        function fresh(opts, ordinal, detail) {
            hn.build()
            hn.feed(opts || {})
            if (ordinal !== undefined && ordinal > 0) hn.pick(ordinal, detail)
            settle()
        }

        function initTestCase() {
            verify(chartsFixture.count() === 15, "the 4 July fixture loads (15 shots)")
            hn.install()
        }
        function init() {
            Theme.themeIndex = 12
            Theme.fontScale = 1.0
            SessionMode.mode = SessionMode.analyse
            SessionMode.chartsNav = null
            appSettings.chartPrefs = ({})
            appSettings.sectionCollapse = ({})
            appSettings.wristReferenceSwingDir = ""
            hn.defaultTab = "shot"
            hn.defaultPreset = ""
            SessionMode.autoReturnToCapture = false
            hn.nav.currentIndex = AppScreen.Session
            hn.replay.seeks = 0; hn.replay.lastSeekUs = -1; hn.replay.scrubs = 0
            hn.unpick()
            fresh({}, 8)
        }
        function cleanup() {
            if (hn.panel) { hn.panel.destroy(); hn.panel = null }
            hn.unpick()
            wait(0)
        }
        function cleanupTestCase() {
            SessionMode.mode = SessionMode.capture
            SessionMode.autoReturnToCapture = false
            SessionMode.chartsNav = null
            appSettings.chartPrefs = ({})
            appSettings.sectionCollapse = ({})
            appSettings.wristReferenceSwingDir = ""
        }

        // ── the four views (D1) ──────────────────────────────────────────────
        function test_01_four_views_in_analyse_and_no_kinds_in_replay() {
            compare(hn.body.register, "Analyse")
            // no shot of this feed carries a wrist profile (and no feed offers a Wrist tab: test_21)
            compare(hn.body.wristAvailable, false)
            compare(tabKeys(), "shot,session,readings,relations,kinds")
            const views = { session: "chSessionView", readings: "chReadingsView", relations: "chRelationsView", kinds: "chKindsView", shot: "chShotView" }
            for (const k of ["session", "readings", "relations", "kinds", "shot"]) {
                tabPress(k)
                compare(hn.body.tab, k, "the press reached the body")
                verify(oneShown(views[k]).height > 0, k + " draws")
            }
            useMode("Replay")
            compare(tabKeys(), "shot,session,readings,relations", "Replay has no Kinds form")
            // a Kinds left on from Analyse reads Session in Replay
            hn.body.tab = "kinds"; settle()
            compare(hn.body.shownView, "session")
            verify(oneShown("chSessionView"))
            compare(shownAll("chKindsView").length, 0)
        }

        // ── This shot: the answer, D4 and the neutral note (rule 5) ──────────
        function test_02_the_answer_head_and_the_one_late_mark() {
            compare(hn.body.tChoices.preset, "Kinematic sequence", "Analyse opens on the sequence (design §3)")
            const head = textOf(oneShown("chHead"))
            verify(head.indexOf("Shot 8 of 15") >= 0, head)
            verify(head.indexOf("Out of sequence: the pelvis peaked late") >= 0, head)
            // D4: ONE segment is said late — the one that peaked late — in the chain and on its curve
            const lates = shownAll(/^chSeqLate:/)
            compare(lates.length, 1)
            compare(lates[0].objectName, "chSeqLate:pelvis")
            verify(shownAll(/^chSeqLink:/).length >= 2, "the others are plain links")
            const chain = textOf(oneShown("chSeqChain"))
            verify(chain.indexOf("Lead arm") === 0 && chain.indexOf("Pelvis") > chain.indexOf("Club"), chain)
            compare(hn.findAll(hn.body, "chPeak:pelvis")[0].p.late, true)
            for (const seg of ["leadArm", "club"]) compare(hn.findAll(hn.body, "chPeak:" + seg)[0].p.late, false, seg + " is not late")
            // the late pelvis is said NEUTRALLY wherever it is said late
            const view = textOf(oneShown("chShotView"))
            verify(view.indexOf("may be the golfer (hips held at impact, then catching up) or the two-camera measurement; it cannot yet tell them apart") >= 0,
                   "the neutral note is on This shot")
            // …and in Replay too: the glance opens on Club delivery, where nothing is said late; on the sequence
            // it keeps the chain, its one late mark and the neutral note
            useMode("Replay")
            compare(shownAll(/^chSeqLate:/).length, 0, "Club delivery says nothing late")
            press(oneShown("chPresetChip"))
            pickInline("chPresetOption", "Kinematic sequence")
            compare(shownAll(/^chSeqLate:/).map(function (x) { return x.objectName }).join(","), "chSeqLate:pelvis")
            verify(textOf(oneShown("chShotView")).indexOf("cannot yet tell them apart") >= 0, "the neutral note in Replay")
        }

        // ── This shot's traces: one per preset member, in the metric's own hue (rule 2) ──
        function test_03_a_trace_per_member_in_its_metric_colour() {
            const members = hn.body.tChoices.metrics.filter(function (m) { return m.on && m.curve })
            compare(members.map(function (m) { return m.key }).sort().join(","),
                    "clubAngularSpeed,leadArmAngularSpeed,pelvisAngularSpeed,thoraxAngularSpeed")
            compare(shownAll(/^chTrace:/).length, members.length, "no trace beyond the preset")
            for (const m of members) {
                const tr = oneShown("chTrace:" + m.key)
                const swatch = oneShown("chTraceLabel:" + m.key, tr).children[0]
                verify(hueOk(swatch.color, m.key), m.key + " swatch is Theme.metricColor")
                const own = layers(tr, function (l) { return l.k === "line" && l.z === 3 })
                compare(own.length, 1, m.key + ": this shot's curve")
                verify(hueOk(own[0].color, m.key), m.key + " curve is Theme.metricColor")
                const raw = layers(tr, function (l) { return l.k === "dots" })
                verify(raw.length === 1 && hueOk(raw[0].color, m.key), m.key + ": raw samples in the same hue")
            }
            // another theme: the same name, that theme's value — and the curve follows
            Theme.themeIndex = 7
            settle()
            for (const m of members)
                verify(hueOk(layers(oneShown("chTrace:" + m.key), function (l) { return l.k === "line" && l.z === 3 })[0].color, m.key),
                       m.key + " repaints with the theme")
        }

        // ── honesty: a bridged run is a bridge, never the placeholder (F18) ──
        function test_04_a_bridged_run_draws_a_bridge_and_no_value() {
            const tr = oneShown("chTrace:thoraxAngularSpeed")
            const line = layers(tr, function (l) { return l.k === "line" && l.z === 3 })[0]
            const bridges = layers(tr, function (l) { return l.k === "bridges" && l.z === 3 })
            compare(bridges.length, 1, "the bridged runs are handed to the painter as a bridge layer")
            const br = bridges[0].list.filter(function (b) { return b.t0 < 0 && b.t1 > 0 })[0]
            verify(!!br, "shot 8's thorax is bridged across impact")
            verify(br.v0 > 100 && br.v1 > 100, "from the last measured sample to the first after")
            verify(line.runs.some(function (r) { return r.style === "bridged" }), "the run is marked bridged")
            const raw = layers(tr, function (l) { return l.k === "dots" })[0]
            let inside = 0
            for (let i = 0; i < line.t.length; ++i) {
                if (line.t[i] > br.t0 + 0.01 && line.t[i] < br.t1 - 0.01) {
                    inside += 1
                    verify(isNaN(line.v[i]) && isNaN(raw.v[i]), "nothing drawn at " + line.t[i] + " ms inside the bridge")
                }
                if (isFinite(line.v[i])) verify(line.v[i] !== 0, "no placeholder 0 on the curve")
            }
            verify(inside > 0, "the bridge spans samples")
        }

        // ── Against (D1): the reference in the same hue, the largest gap said ──
        function test_05_against_the_median_draws_the_reference_and_says_the_gap() {
            compare(shownAll("chAgainstHead").length, 0, "nothing against at rest")
            clickOption("against", "open")
            verify(oneShown("chAgainstPicker"))
            pickInline("chAgainstOption", "median")
            compare(hn.body.against, "median")
            const head = textOf(oneShown("chAgainstHead"))
            verify(head.indexOf("Shot 8 left your usual swing furthest") === 0, head)
            for (const m of hn.body.tChoices.metrics.filter(function (x) { return x.on && x.curve })) {
                const ref = layers(oneShown("chTrace:" + m.key), function (l) { return l.k === "line" && l.z === 1 })
                compare(ref.length, 1, m.key + ": the session median drawn")
                verify(hueOk(ref[0].color, m.key), m.key + ": the reference keeps the metric's hue")
            }
            // the difference strip, this − reference, on a press
            clickOption("difference", "on")
            compare(hn.body.difference, true)
            verify(shownAll(/^chDiffStrip:/).length >= 1, "a difference strip under the traces")
            // a refusal is said, not drawn: shot 1 has no shot before it
            clickOption("difference", "off")
            hn.pick(1); settle()
            clickOption("against", "open")
            pickInline("chAgainstOption", "previous")
            verify(textOf(oneShown("chShotView")).indexOf("Shot 1 has no shot before it.") >= 0)
        }

        // ── Session: the envelope and where the curves part (§2.2) ──────────
        function test_06_session_envelope_and_where_the_curves_part() {
            tabPress("session")
            const head = textOf(oneShown("chSessionHead"))
            verify(head.indexOf(" parts most ") >= 0, head)
            const rows = hn.body.tSession.rows
            verify(rows.length >= 2)
            for (const r of rows) {
                const env = oneShown("chEnvelope:" + r.key)
                const panel = env.children.filter(function (k) { return hn.plotOf(k) !== null })[0]
                const ls = hn.plotOf(panel).layers
                verify(ls.some(function (l) { return l.k === "band" }), r.key + ": the middle half")
                verify(ls.some(function (l) { return l.k === "line" && l.z === 1 }), r.key + ": the median")
                verify(ls.some(function (l) { return l.k === "line" && l.z === 3 }), r.key + ": this shot at full strength")
            }
            verify(shownAll("chPartAt").length >= 1, "the parting instant is marked")
            // D3: no facet by default, and "every shot" draws each shot's own curve
            compare(hn.body.facet, "")
            clickOption("band", "every")
            compare(hn.body.band, "every")
            const env = oneShown("chEnvelope:" + rows[0].key)
            const ls = hn.plotOf(env.children.filter(function (k) { return hn.plotOf(k) !== null })[0]).layers
            verify(ls.filter(function (l) { return l.k === "line" && l.z === 0 }).length >= 10, "the other shots, each its own line")
        }

        // ── facets (D3): a fault that fired on every shot, or on too few, cannot split it ──
        function test_07_the_facet_offer_leaves_out_what_cannot_split() {
            tabPress("session")
            const offered = hn.body.tChoices.facets.filter(function (f) { return f.kind === "condition" })
            verify(offered.length > 0)
            for (const f of offered) {
                verify(probe.alwaysFired.indexOf(f.id) < 0, f.id + " fired on every shot and is offered")
                verify(probe.tooRare.indexOf(f.id) < 0, f.id + " fired on too few shots and is offered")
                verify(f.n >= 3 && f.n <= 12, f.id + " fired " + f.n)
            }
            // what the reader can press: the line, and the rest IN PLACE under it
            const onLine = shownAll("chOption").filter(function (o) { return o.setKey === "facet" && o.key !== "_more" && o.key !== "" }).map(function (o) { return o.key })
            clickOption("facet", "_more")
            const inList = shownAll("chFacetOption").map(function (o) { return o.missId })
            for (const id of probe.alwaysFired.concat(probe.tooRare))
                verify(onLine.indexOf(id) < 0 && inList.indexOf(id) < 0, id + " is not pressable")
            for (const f of offered) verify(onLine.indexOf(f.id) >= 0 || inList.indexOf(f.id) >= 0, f.id + " is reachable")
            // picking one splits the session in two and says co-occurrence is not cause
            pickInline("chFacetOption", offered[0].id)
            compare(hn.body.facet, offered[0].id)
            compare(hn.body.tSession.groups.length, 2)
            verify(textOf(oneShown("chSessionHead")).indexOf("They occur together; that is not to say one causes another.") >= 0)
        }

        // ── the group fit is the group's (rule 3, D2): refused below three ──
        function test_08_the_group_fit_is_refused_for_a_group_under_three() {
            fresh({ clubs: { "3": "DRIVER", "6": "DRIVER" } }, 3)
            tabPress("session")
            verify(textOf(oneShown("chSessionHead")).length > 0)
            clickOption("band", "fit")
            clickOption("facet", "club")
            compare(hn.body.band, "fit"); compare(hn.body.facet, "club")
            const key = hn.body.tSession.rows[0].key
            const fits = shownAll("chFit:" + key)
            verify(fits.length >= 1, "the fit panels are up")
            const refusedTexts = fits.map(function (f) { return textOf(f) }).filter(function (t) { return t.indexOf("fewer than three shots: no group fit") >= 0 })
            verify(refusedTexts.length >= 1, "the driver pair says why it has no fit")
            // and no curve is read from a refused fit
            const driver = hn.body.tSession.rows[0].panels.filter(function (p) { return p.fit && p.fit.refused })[0]
            verify(!!driver && driver.fit.v === undefined, "a refused fit carries no values")
            const iron = hn.body.tSession.rows[0].panels.filter(function (p) { return p.fit && !p.fit.refused })[0]
            verify(!!iron && iron.fit.label.indexOf("13 shots") >= 0, "the other group is fitted and labelled with its n")
        }

        // ── Relations (§2.3): the scatter and the chance line said out loud ──
        function test_09_relations_scatter_and_the_chance_line() {
            tabPress("relations")
            const sc = oneShown("chScatter")
            verify(sc.scatter.points.length >= 12, "a mark per shot that measured both: " + sc.scatter.points.length)
            compare(sc.scatter.points.filter(function (p) { return p.picked }).length, 1, "shot 8 is ringed")
            compare(textOf(oneShown("chChance")), "|r| under .51 is chance at n = 15")
            // a "moves with" row puts that reading across the foot
            const rows = shownAll("chMovesRow")
            verify(rows.length >= 3)
            const want = rows[2].readingId
            pressAt(rows[2], 40, 10)
            compare(hn.body.acrossId, want)
            compare(oneShown("chScatter").scatter.xId, want, "the scatter re-drew against it")
        }

        // ── Kinds (§2.4, D7): no structure said first, then the split, then the unplaced ──
        function test_10_kinds_say_no_structure_then_the_split() {
            tabPress("kinds")
            const head = textOf(oneShown("chKindsHead"))
            verify(head.indexOf("No structure") >= 0, head)
            const view = textOf(oneShown("chKindsView"))
            verify(/gap in clubhead speed/.test(view) && view.indexOf(": 6 faster, 9 slower") >= 0, "the split line: " + view.slice(0, 300))
            const members = textOf(oneShown("chKindMembers"))
            verify(members.indexOf("Faster") >= 0 && members.indexOf("Slower") >= 0, members)
            verify(shownAll("chKindRow").length >= 1, "what else separates them")
            // two shots without the split reading sit in neither: a third, neutral set
            fresh({ unmeasured: { key: "clubheadSpeed", phase: 5, ordinals: [1, 4] } }, 8)
            tabPress("kinds")
            verify(textOf(oneShown("chKindsHead")).indexOf("two not measured") >= 0)
            verify(textOf(oneShown("chKindMembers")).indexOf("Not measured") >= 0)
        }

        // ── Apart from the session (§2.1 B): seven readings, a row opens in place ──
        function test_11_the_apart_ledger_and_a_row_that_opens_in_place() {
            const apart = oneShown("chApart")
            verify(textOf(apart).indexOf("Seven readings apart from the session") >= 0)
            const rows = shownAll("chApartRow", oneShown("chApartList"))
            compare(rows.length, 7)
            const id = rows[0].readingId
            pressAt(rows[0], 30, 10)
            compare(hn.body.openId, id)
            const open = oneShown("chApartRowOpen", oneShown("chApartList"))
            compare(open.readingId, id)
            verify(shownAll("chReadingTrace", open).length + shownAll("chRun", open).length === 1, "it opens its curve or its run")
            // pressed again, it closes
            pressAt(open, 30, 10)
            compare(hn.body.openId, "")
            compare(shownAll("chApartRowOpen").length, 0)
        }

        // ── a press on a trace seeks the replay (F25) ───────────────────────
        function test_12_a_trace_seeks_the_replay() {
            const seek = oneShown("chSeek", oneShown("chTrace:clubAngularSpeed"))
            intoView(seek)
            mouseClick(seek, seek.width * 0.6, seek.height / 2)
            settle()
            verify(hn.replay.seeks >= 1, "shotReplay.seekToUs was called")
            verify(hn.replay.lastSeekUs >= 0 && hn.replay.lastSeekUs <= hn.replay.endUs, "inside the recording: " + hn.replay.lastSeekUs)
            compare(hn.replay.scrubs, 1, "the press brackets a scrub")
            // the playhead follows the replay
            verify(oneShown("chPlayhead"))
        }

        // ── a shot picked on a figure goes to the carousel's own path ───────
        function test_13_a_shot_picked_on_a_figure_opens_it() {
            tabPress("kinds")
            const members = hn.findAll(oneShown("chKindMembers"), "").filter(function (t) { return hn.isText(t) && t.no === 3 && hn.shown(t) })
            verify(members.length === 1, "shot 3 is listed as a kind member")
            press(members[0])
            compare(SessionMode.focusedShotId, 3)
            compare(SessionMode.mode, SessionMode.analyse, "Analyse stays Analyse")
            compare(hn.replay.swingDir, chartsFixture.dirOf(3), "the replay opened it")
            settle()
            compare(hn.body.tShot.ordinal, 3, "This shot follows")
        }

        // ── choices persist per screen × mode (F9) and restore ───────────────
        function test_14_choices_persist_per_screen_and_mode() {
            clickOption("draw", "overlay")
            compare(hn.body.overlay, true)
            const b = hn.sessionType + ":" + SessionMode.analyse + ":"
            compare(appSettings.chartPrefs[b + "split"], false, "saved as today's key")
            clickOption("against", "open"); pickInline("chAgainstOption", "median")
            compare(appSettings.chartPrefs[b + "against"], "median")
            tabPress("session")
            compare(appSettings.chartPrefs[b + "view"], "session")
            // Replay keeps its own
            useMode("Replay")
            compare(hn.body.overlay, false); compare(hn.body.against, "none"); compare(hn.body.tab, "shot")
            useMode("Analyse")
            compare(hn.body.overlay, true); compare(hn.body.against, "median"); compare(hn.body.tab, "session")
            // another session type keeps its own too (same session screen, its own saved prefs)
            hn.sessionType = 2
            fresh({}, 8)
            compare(hn.body.overlay, false); compare(hn.body.tab, "shot")
            hn.sessionType = 1
            // a rebuild (the stage, or a restart) puts them back
            fresh({}, 8)
            compare(hn.body.overlay, true); compare(hn.body.against, "median"); compare(hn.body.tab, "session")
            verify(shownAll("chTrace:overlay").length === 0 || hn.body.tab !== "shot")
            // today's saved "Custom" restores as its base preset with the switched-off series
            appSettings.chartPrefs = ({ [b + "preset"]: "Custom", [b + "presetBase"]: "Kinematic sequence",
                                        [b + "series"]: { pelvisAngularSpeed: false, clubAngularSpeed: true } })
            fresh({}, 8)
            compare(hn.body.preset, "Kinematic sequence")
            compare(hn.body.metricsOff.join(","), "pelvisAngularSpeed")
            compare(hn.body.tChoices.custom, true)
            compare(shownAll("chTrace:pelvisAngularSpeed").length, 0)
        }

        // ── the reader's place (ChartsNav) survives the stage rebuilding the panel ──
        function test_15_the_open_row_survives_a_rebuild() {
            const row = shownAll("chApartRow", oneShown("chApartList"))[1]
            const id = row.readingId
            pressAt(row, 30, 10)
            compare(hn.body.openId, id)
            verify(SessionMode.chartsNav !== null && SessionMode.chartsNav.openId === id, "the place is saved as it changes")
            compare(SessionMode.chartsNav.sessionDir, chartsFixture.sessionDir())
            fresh({}, 8)                                       // a Capture↔Replay flip rebuilds it
            compare(hn.body.openId, id, "the open row is back")
            compare(oneShown("chApartRowOpen").readingId, id)
            // another session starts at the defaults
            hn.review.liveSessionDir = "/fx/another-session"
            settle()
            compare(hn.body.openId, "")
            hn.review.liveSessionDir = chartsFixture.sessionDir()
        }

        // ── the cast: the auto-closing surface takes no clicks ───────────────
        function test_16_the_noninteractive_cast_takes_no_clicks() {
            hn.body.surface = "cast"
            hn.body.interactive = false
            settle()
            compare(hn.body.register, "Replay", "the cast reads in Replay")
            verify(oneShown("chCastHead"))
            compare(shownAll("chTabBar").length, 0, "no tab row to press")
            compare(hn.body.view, "shot", "it shows the shot")
            const before = hn.replay.seeks
            const seek = oneShown("chSeek")
            mouseClick(seek, seek.width / 2, seek.height / 2)
            mouseClick(hn.body, 20, 10)
            settle()
            compare(hn.replay.seeks, before, "a press on the cast seeks nothing")
            compare(hn.body.tab, "shot")
        }

        // ── the live states (STEP2C): n < 3, a shot not yet landed, no session ──
        function test_17_two_shots_refuse_the_session_and_keep_this_shot() {
            fresh({ shots: 2 }, 2)
            verify(textOf(oneShown("chHead")).length > 0, "This shot still reads")
            verify(shownAll(/^chTrace:/).length >= 2)
            tabPress("session")
            compare(textOf(oneShown("chSessionWhy")), "Three shots are needed to draw the session; this session has two.")
            tabPress("relations")
            compare(textOf(oneShown("chRelationsWhy")), "Five shots are needed to relate readings across a session; this session has two.")
            tabPress("kinds")
            compare(textOf(oneShown("chKindsWhy")), "Three shots are needed to draw the session; this session has two.")
        }
        function test_18_a_shot_not_yet_in_the_session_reads_from_its_own_detail() {
            fresh({ without: [15] }, 15)
            compare(hn.body.rStatus.pickedFromDetail, true)
            const head = textOf(oneShown("chHead"))
            verify(head.indexOf("Shot 15") >= 0, head)
            verify(shownAll(/^chTrace:/).length >= 2, "its traces draw")
            tabPress("session")
            verify(textOf(oneShown("chSessionHead")).length > 0, "the session draws, the shot on it")
            // the shot lands: the place does not jump
            tabPress("shot")
            hn.feed({}); settle()
            compare(hn.body.tab, "shot")
            compare(hn.body.rStatus.pickedInSession, true)
        }
        function test_19_no_session_says_so() {
            hn.unpick()
            fresh({ none: true })
            verify(textOf(oneShown("chShotEmpty")).length > 0, "This shot says why it is empty")
            compare(shownAll(/^chTrace:/).length, 0)
            tabPress("session")
            compare(textOf(oneShown("chSessionWhy")), "No session is loaded.")
            tabPress("relations")
            compare(textOf(oneShown("chRelationsWhy")), "No session is loaded.")
            // a session, nothing picked: Session and Relations read; This shot waits for a pick
            fresh({})
            tabPress("session")
            verify(textOf(oneShown("chSessionHead")).length > 0)
            tabPress("shot")
            verify(textOf(oneShown("chShotEmpty")).length > 0)
        }

        // ── the _screenActive gate (F41): an invisible screen is fed nothing ──
        function test_20_an_invisible_screen_follows_no_replay() {
            hn.nav.currentIndex = AppScreen.Home                 // another screen is up
            hn.pick(3); settle()
            compare(hn.body.tShot.ordinal, 8, "the hidden panel still holds shot 8")
            hn.nav.currentIndex = AppScreen.Session              // back on the session screen: it catches up
            settle()
            compare(hn.body.tShot.ordinal, 3)
        }

        // ── the Wrist tab (build/wrist-tab; docs/design/wrist_motion_into_charts.md §4 B) ──
        // The fixture's shots carry the pose trail wrist; `wrist: true` gives each its checkpoint profile
        // (ShotSeries::wrist, as SessionSeriesModel fills it). The words and numbers are
        // charts_wrist_presenter_test's; these pin WHERE the panel surfaces them (Mark, 16 Oct): no Wrist tab;
        // a "Wrist" section on This shot and Session when the session carries a wrist reading AND a wrist metric
        // (a strip row's key, here trailWristFlexExt) is among the curves on show.
        readonly property string wristPreset: "Wrist & forearm"
        function useWristPreset() {
            verify((hn.body.tChoices.presets || []).some(function (p) { return p.id === wristPreset }), "the wrist preset is offered")
            hn.body.choose("preset", wristPreset); settle()
            compare(hn.body.tChoices.preset, wristPreset)
        }
        function test_21_the_wrist_section_shows_when_a_wrist_metric_is_on_show() {
            fresh({ wrist: true }, 8)
            compare(hn.body.wristAvailable, true, "a shot carries a wrist reading")
            compare(tabKeys(), "shot,session,readings,relations,kinds", "no Wrist tab")
            // the sequence preset shows no wrist curve: no section
            compare(hn.body.wristOnShow, false)
            compare(shownAll("chWristSection").length, 0, "no wrist curve on show: no section")
            useWristPreset()
            compare(hn.body.wristOnShow, true)
            compare(hn.body.wristShotShown, true)
            const sec = oneShown("chWristSection")
            verify(sec.height > 0, "the section draws")
            verify(textOf(oneShown("chWristHead", sec)).length > 0, "a head")
            verify(shownAll("chWristRow", oneShown("chWristProfile", sec)).length >= 1, "a checkpoint row per wrist DOF")
            compare(shownAll("chWristCol", sec).length, 8, "eight checkpoints")
            verify(oneShown("chWristControls", sec))
            verify(oneShown("chWristFindings", sec))
            // the traces stay above it, untouched
            verify(oneShown("chShotView"))
            // the trimmed fixture keeps the trail wrist as phase readings only, so the traces presenter reads it as a
            // card (instant-only), not a curve: the standard content still answers above the section
            verify((hn.body.tShot.rows || []).length + (hn.body.tShot.cards || []).length >= 1, "the standard This shot content still answers")
            // the wrist metric switched off: the section goes
            hn.body.toggleMetric("trailWristFlexExt"); settle()
            compare(hn.body.wristOnShow, false)
            compare(shownAll("chWristSection").length, 0, "no wrist curve on show: no section")
            hn.body.toggleMetric("trailWristFlexExt"); settle()
            verify(oneShown("chWristSection"))
            // Session: the session's section after the standard content; the presenter's scope follows the view
            tabPress("session")
            compare(hn.body.wristP.scope, "session")
            compare(hn.body.wristSessionShown, true)
            verify(oneShown("chWristSessionSlot"))
            verify(oneShown("chWristSession"))
            tabPress("shot")
            compare(hn.body.wristP.scope, "shot")
        }
        function test_22_no_section_without_wrist_data_and_a_saved_wrist_tab_reads_this_shot() {
            // a session with no wrist reading: the wrist preset draws its curves, and no section
            fresh({}, 8)
            useWristPreset()
            compare(hn.body.wristAvailable, false)
            compare(hn.body.wristShotShown, false)
            compare(shownAll("chWristSection").length, 0)
            tabPress("session")
            compare(hn.body.wristSessionShown, false)
            compare(shownAll("chWristSession").length, 0)
            // a "wrist" saved by the retired Wrist tab reads This shot
            const b = hn.sessionType + ":" + SessionMode.analyse + ":"
            appSettings.chartPrefs = ({ [b + "view"]: "wrist" })
            fresh({ wrist: true }, 8)
            compare(hn.body.tab, "shot")
            compare(hn.body.shownView, "shot")
            verify(oneShown("chShotView"))
            compare(tabKeys(), "shot,session,readings,relations,kinds")
        }
        function test_23_the_wrist_choices_persist_per_screen_and_mode() {
            fresh({ wrist: true }, 8)
            useWristPreset()
            const b = hn.sessionType + ":" + SessionMode.analyse + ":"
            clickOption("against", "previous")
            compare(hn.body.wristAgainst, "previous")
            compare(appSettings.chartPrefs[b + "wristAgainst"], "previous")
            clickOption("curves", "on")
            compare(hn.body.wristCurves, true)
            compare(appSettings.chartPrefs[b + "wristCurves"], true)
            clickOption("archetype", "2")
            compare(hn.body.wristArchetype, 2)
            compare(appSettings.chartPrefs[b + "wristArchetype"], 2)
            verify(appSettings.chartPrefs[b + "wristScope"] === undefined, "no scope is saved: it follows the view")
            // the glance keeps its own
            useMode("Replay")
            compare(hn.body.wristAgainst, "address")
            useMode("Analyse")
            // a rebuild puts them back
            fresh({ wrist: true }, 8)
            compare(hn.body.tChoices.preset, wristPreset)
            compare(hn.body.wristAgainst, "previous")
            compare(hn.body.wristCurves, true); compare(hn.body.wristArchetype, 2)
            verify(oneShown("chWristSection"))
        }
        function test_24_the_reference_star_is_set_and_cleared_in_place() {
            fresh({ wrist: true }, 8)
            useWristPreset()
            const sec = oneShown("chWristSection")
            let link = oneShown("chWristRefLink", sec)
            compare(hn.plain(link.text), "★ set this swing as your reference")
            press(link)
            compare(appSettings.wristReferenceSwingDir, chartsFixture.dirOf(8), "the ★ is this swing")
            link = oneShown("chWristRefLink")
            compare(hn.plain(link.text), "★ this swing is your reference")
            press(link)
            compare(appSettings.wristReferenceSwingDir, "", "pressed again, it is cleared")
            // the traces' own link beside Against (its "reference ★" reads the same setting)
            const t = oneShown("chRefLink")
            press(t)
            compare(appSettings.wristReferenceSwingDir, chartsFixture.dirOf(8))
            const ag = (hn.body.tChoices.against || []).filter(function (a) { return a.id === "reference" })
            compare(ag.length, 1, "Against offers the ★ reference")
        }
        // ── the glance (the mat's Replay): This shot's section, compact; Session adds none ──
        function test_27_the_glance_shows_this_shots_profile_only() {
            fresh({ wrist: true }, 8)
            useMode("Replay")
            useWristPreset()
            const sec = oneShown("chWristSection")
            verify(textOf(oneShown("chWristHead", sec)).length > 0, "a head")
            verify(oneShown("chWristProfile", sec))
            compare(shownAll("chWristControls", sec).length, 0, "no controls at the glance")
            compare(shownAll("chWristFindings", sec).length, 0, "no ledgers at the glance")
            compare(shownAll("chWristMoves", sec).length, 0)
            tabPress("session")
            compare(hn.body.wristSessionShown, false)
            compare(shownAll("chWristSession").length, 0, "Session adds no wrist section at the glance")
        }
        // ── the Wrist screen opens This shot on the wrist preset: the host's default, a saved preset wins ──
        function test_26_the_wrist_screen_opens_this_shot_on_the_wrist_preset() {
            hn.defaultPreset = wristPreset
            fresh({ wrist: true }, 8)
            compare(hn.body.tab, "shot")
            compare(hn.body.preset, wristPreset, "no saved preset: the host's default")
            compare(hn.body.tChoices.preset, wristPreset)
            verify(oneShown("chWristSection"))
            // the reader's pick is saved and wins over the default
            hn.body.choose("preset", "Kinematic sequence"); settle()
            fresh({ wrist: true }, 8)
            compare(hn.body.preset, "Kinematic sequence", "the saved preset wins")
            compare(shownAll("chWristSection").length, 0)
        }

        // ── the retired Wrist motion panel: a saved layout naming it reads, and is rewritten, as Charts ──
        function test_25_a_saved_wrist_motion_layout_migrates_to_charts() {
            let r = ViewLayout._migrateLayout({ panels: ["camera", "wristMotion", "timeline"], arrangement: "split",
                                                sizes: { split: { camera: 1.2, wristMotion: 0.8 },
                                                         stage: { main: 0.6, side: { wristMotion: 1.1, table: 0.9 } } } })
            verify(r.changed)
            compare(r.layout.panels.join(","), "camera,charts,timeline")
            compare(r.layout.arrangement, "split")
            compare(r.layout.sizes.split.charts, 0.8)
            verify(r.layout.sizes.split.wristMotion === undefined)
            compare(r.layout.sizes.stage.main, 0.6)
            compare(r.layout.sizes.stage.side.charts, 1.1)
            compare(r.layout.sizes.stage.side.table, 0.9)
            // both in one layout: deduplicated, Charts keeps its own weight
            r = ViewLayout._migrateLayout({ panels: ["charts", "wristMotion", "camera"], sizes: { split: { charts: 1.3, wristMotion: 0.7, camera: 1 } } })
            compare(r.layout.panels.join(","), "charts,camera")
            compare(r.layout.sizes.split.charts, 1.3)
            verify(r.layout.sizes.split.wristMotion === undefined)
            // nothing retired: the same entry
            const plain = { panels: ["camera", "charts"], arrangement: "tabs" }
            r = ViewLayout._migrateLayout(plain)
            verify(!r.changed)
            verify(r.layout === plain)
            // the stored map: read as Charts at once, and rewritten once
            const saved = appSettings.viewLayoutByMode
            appSettings.viewLayoutByMode = ({ "1": { panels: ["camera", "wristMotion"], arrangement: "tabs" },
                                              "2": { panels: ["camera", "charts"], arrangement: "split" } })
            compare(ViewLayout.enabledKeysFor(1).join(","), "camera,charts", "read as Charts before the rewrite")
            verify(ViewLayout._migrateStored(), "rewritten")
            compare(appSettings.viewLayoutByMode["1"].panels.join(","), "camera,charts")
            compare(appSettings.viewLayoutByMode["2"].panels.join(","), "camera,charts")
            verify(!ViewLayout._migrateStored(), "once: nothing left to migrate")
            appSettings.viewLayoutByMode = saved
        }

        // ── the Posture section (docs/design/posture_chart_design.md §5, stage 3a) ──
        // `posture: true` feeds the Posture presenter the fixture's six posture series, read at address, the top
        // and impact (P1, P4, P7): every other cell is "-", and the top's trunk bend and hip depth carry the ⚠ the
        // capture always carries at P3–P5. The words and numbers are charts_posture_presenter_test's; these pin
        // WHERE the panel surfaces them: a "Posture" section on This shot and Session when a posture reading AND a
        // posture metric is among the curves on show (the Posture preset).
        readonly property string posturePreset: "Posture"
        function usePosturePreset() {
            verify((hn.body.tChoices.presets || []).some(function (p) { return p.id === posturePreset }), "the Posture preset is offered")
            hn.body.choose("preset", posturePreset); settle()
            compare(hn.body.tChoices.preset, posturePreset)
        }
        function postureCell(sec, rowKey, tag) {
            const c = shownAll("chPostureCell", sec).filter(function (x) { return x.rowKey === rowKey && x.tag === tag })
            compare(c.length, 1, "one " + rowKey + " cell at " + tag)
            return c[0]
        }
        function test_28_the_posture_section_shows_on_the_posture_preset() {
            fresh({ posture: true }, 8)
            compare(hn.body.postureAvailable, true, "a shot carries a posture reading")
            compare(hn.body.postureOnShow, false, "the default preset shows no posture curve")
            compare(shownAll("chPostureSection").length, 0, "no posture curve on show: no section")
            usePosturePreset()
            compare(hn.body.postureOnShow, true)
            compare(hn.body.postureShotShown, true)
            const sec = oneShown("chPostureSection")
            verify(sec.height > 0, "the section draws")
            verify(textOf(oneShown("chPostureHead", sec)).length > 0, "a head")
            verify(textOf(oneShown("chPosturePattern", sec)).indexOf("Pattern") === 0, "the pattern line")
            verify(oneShown("chPostureControls", sec))
            const prof = oneShown("chPostureProfile", sec)
            compare(shownAll("chPostureRow", prof).length, 6, "six rows")
            compare(shownAll("chPostureCol", prof).length, 8, "P1…P8")
            // a cell with no reading is a plain "-"; the top's trunk bend and hip depth keep their figure under a ⚠
            compare(postureCell(prof, "head", "P2").text, "-")
            verify(!postureCell(prof, "head", "P2").warned)
            // the top's trunk bend keeps its figure under a ⚠; the fixture reads no hip depth, so its ⚠ cell is a plain "-"
            const bend = postureCell(prof, "bend", "P4"), hips = postureCell(prof, "hips", "P4")
            verify(bend.warned, "⚠ on trunk bend at the top")
            verify(bend.text !== "-", "a ⚠ reading keeps its figure (" + bend.text + ")")
            verify(textOf(bend).indexOf("⚠") >= 0, "the ⚠ is said under the figure")
            compare(hips.text, "-", "no hip depth reading in the fixture")
            verify(!hips.warned && textOf(hips).indexOf("⚠") < 0, "a refused reading is a plain \"-\": no ⚠ on it")
            compare(postureCell(prof, "bend", "P7").warned, false, "no ⚠ at impact")
            // trunk bend's second reading (the back line, decision 16): under P3–P5 of that row only, never elsewhere.
            // The fixture carries no back-line series, so each says "back line -" and draws no ⚠ on a missing figure.
            const alts = shownAll("chPostureAltValue", prof)
            compare(alts.map(function (a) { return a.rowKey + ":" + a.tag }).sort().join(","), "bend:P3,bend:P4,bend:P5", "a second reading under P3–P5 of trunk bend")
            verify(alts.every(function (a) { return a.text.indexOf("back line") === 0 && !a.warned }), "\"back line -\" with no ⚠ where nothing was read")
            const altTop = alts.filter(function (a) { return a.tag === "P4" })[0]
            verify(altTop.mapToItem(prof, 0, 0).y > bend.mapToItem(prof, 0, 0).y, "under the cell's numbers")
            verify(postureCell(prof, "leadKnee", "P2").mapToItem(prof, 0, 0).y > altTop.mapToItem(prof, 0, 0).y + altTop.height, "the next row clears it")
            // said once: the trust fold under the profile, closed
            const tr = oneShown("chPostureTrust", sec)
            compare(shownAll("chPostureTrustLine", tr).length, 0, "closed by default")
            press(oneShown("chPostureTrustToggle", tr))
            verify(shownAll("chPostureTrustLine", tr).length >= 1, "opened, it says the trust lines")
            verify(oneShown("chPostureFindings", sec))
            verify(oneShown("chPostureWorkingWell", sec))
            // At address (§7.4.2): under the profile, above What moved; a row per address reading the capture can
            // produce, each with its ⚠ and its few words
            const adr = oneShown("chPostureAddress", sec)
            verify(adr, "the At address block")
            const arows = shownAll("chPostureAddressRow", adr)
            verify(arows.length >= 1, "an address reading (forward bend and trail-knee bend from the fixture's series)")
            compare(arows.length, hn.body.pShot.address.rows.length, "a row per reading the presenter gives")
            verify(arows.every(function (r) { return r.warned }), "the ⚠ on every row")
            compare(shownAll("chPostureAddressWarn", adr).length, arows.length, "its words under every row")
            verify(shownAll("chPostureAddressWarn", adr).every(function (w) { return w.text.indexOf("depends on where the camera stands") >= 0 }))
            verify(arows.some(function (r) { return r.measureId === "m_spineBendAtAddress" && r.text !== "-" && r.banded }), "forward bend: a number on its band")
            const prof0 = oneShown("chPostureProfile", sec), mv = oneShown("chPostureMoved", sec)
            verify(adr.mapToItem(sec, 0, 0).y > prof0.mapToItem(sec, 0, 0).y, "under the profile")
            if (mv) verify(adr.mapToItem(sec, 0, 0).y < mv.mapToItem(sec, 0, 0).y, "above What moved")
            verify(oneShown("chShotView"), "the traces stay above it")
            // the section's posture curves switched off: the section goes
            for (const k of ["headLift", "pelvisThrust", "spineForwardBend", "leadKneeFlexion", "trailKneeFlexion", "pelvisLift", "headRise"])
                if ((hn.body.tChoices.metrics || []).some(function (m) { return m.key === k && m.on })) hn.body.toggleMetric(k)
            settle()
            compare(hn.body.postureOnShow, false)
            compare(shownAll("chPostureSection").length, 0, "no posture curve on show: no section")
        }
        function test_29_no_posture_section_without_posture_data() {
            fresh({}, 8)
            usePosturePreset()
            compare(hn.body.postureAvailable, false)
            compare(hn.body.postureShotShown, false)
            compare(shownAll("chPostureSection").length, 0)
            tabPress("session")
            compare(hn.body.postureSessionShown, false)
            compare(shownAll("chPostureSessionSlot").length, 0)
        }
        function test_30_a_posture_column_seeks_and_the_session_section_reads() {
            fresh({ posture: true }, 8)
            usePosturePreset()
            const sec = oneShown("chPostureSection")
            const col = shownAll("chPostureCol", sec).filter(function (c) { return c.tag === "P4" })
            compare(col.length, 1)
            const before = hn.replay.seeks
            press(col[0])
            compare(hn.replay.seeks, before + 1, "a column press seeks")
            compare(hn.replay.lastSeekUs, hn.body.postureP.seekUsFor("P4"), "to that position")
            // Session: the session's section after the standard content; the presenter's scope follows the view
            tabPress("session")
            compare(hn.body.postureP.scope, "session")
            compare(hn.body.postureSessionShown, true)
            verify(oneShown("chPostureSessionSlot"))
            const ses = oneShown("chPostureSession")
            compare(shownAll("chPostureRow", oneShown("chPostureProfile", ses)).length, 6)
            compare(shownAll("chPostureAltValue", oneShown("chPostureProfile", ses)).length, 3, "Session: the second reading under trunk bend at P3–P5")
            verify(shownAll("chPostureMark", oneShown("chPostureStrip", ses)).length >= 1, "a mark per shot")
            verify(oneShown("chPostureDepartures", ses))
            const sadr = oneShown("chPostureAddress", ses)
            verify(sadr, "the At address block on Session")
            compare(shownAll("chPostureAddressRow", sadr).length, hn.body.pSession.address.rows.length, "a row per reading")
            verify(shownAll("chPostureAddressRow", sadr).every(function (r) { return r.warned }))
            tabPress("shot")
            compare(hn.body.postureP.scope, "shot")
        }
        function test_31_the_posture_glance_has_no_controls_and_no_session() {
            fresh({ posture: true }, 8)
            useMode("Replay")
            usePosturePreset()
            const sec = oneShown("chPostureSection")
            verify(textOf(oneShown("chPostureHead", sec)).length > 0, "a head")
            verify(oneShown("chPosturePattern", sec))
            verify(oneShown("chPostureProfile", sec))
            compare(shownAll("chPostureControls", sec).length, 0, "no controls at the glance")
            compare(shownAll("chPostureFindings", sec).length, 0, "no ledgers at the glance")
            compare(shownAll("chPostureMoved", sec).length, 0)
            tabPress("session")
            compare(hn.body.postureSessionShown, false)
            compare(shownAll("chPostureSessionSlot").length, 0, "Session adds no posture section at the glance")
        }
        function test_32_the_posture_fold_and_choices_persist() {
            fresh({ posture: true }, 8)
            usePosturePreset()
            const b = hn.sessionType + ":" + SessionMode.analyse + ":"
            clickOption("pattern", "1")
            compare(hn.body.posturePattern, 1)
            compare(appSettings.chartPrefs[b + "posturePattern"], 1)
            clickOption("against", "previous")
            compare(hn.body.postureAgainst, "previous")
            compare(appSettings.chartPrefs[b + "postureAgainst"], "previous")
            press(oneShown("chFold:posture"))
            compare(hn.body.collapsed.posture, true)
            compare(appSettings.sectionCollapse[b + "posture"], true)
            compare(shownAll("chPostureProfile").length, 0, "folded: the profile goes")
            verify(textOf(oneShown("chFold:posture")).length > "Posture".length, "folded, the head keeps a one-line state")
            // a rebuild puts them back
            fresh({ posture: true }, 8)
            compare(hn.body.posturePattern, 1)
            compare(hn.body.postureAgainst, "previous")
            compare(hn.body.collapsed.posture, true)
            verify(oneShown("chPostureSection"))
            compare(shownAll("chPostureProfile").length, 0)
        }
        // ── the picture strip (posture_chart_design.md §5.4, stage 5) ──
        // `picture: true` gives every shot the picture's document (data/posture/picture_doc.json, a real re-analysed
        // swing; no video, so each frame draws its lines on an empty frame). The geometry is charts_posture_presenter_test's.
        function test_33_the_posture_picture_strip_on_this_shot_seeks() {
            fresh({ posture: true, picture: true }, 8)
            usePosturePreset()
            const sec = oneShown("chPostureSection")
            const pic = oneShown("chPosturePicture", sec)
            const frames = shownAll("chPosturePictureFrame", pic)
            compare(frames.length, 6, "address, the top and impact, down the line and face-on")
            compare(frames[0].camera, "dtl", "down the line first")
            compare(frames.map(function (f) { return f.tag }).join(","), "P1,P4,P7,P1,P4,P7")
            verify(frames.every(function (f) { return f.coachDrawn > 0 }), "the frozen address lines on every frame (the default sets)")
            const di = frames.filter(function (f) { return f.camera === "dtl" && f.tag === "P7" })[0]
            // the strip is drawn where the pattern has a hip band at P7 — the norms the binary reads: run with
            // PINPOINT_CORE_NORMS / _PACK / _CONTEXTS at src/Resources/diagnostics (as ctest's Charts suites are) for the
            // shipped posture bands; the profile's P7 cell says the same either way
            const hipRow = (hn.body.pShot.profile.rows || []).filter(function (r) { return r.key === "hips" })[0]
            const hipBanded = !!hipRow && hipRow.cells[6].banded === true
            compare(di.bandDrawn, hipBanded ? 1 : 0, "the hip strip at impact, where the pattern has a band")
            compare(di.inkDrawn, 1, "the butt line at impact")
            const fi = frames.filter(function (f) { return f.camera === "faceOn" && f.tag === "P7" })[0]
            const headRow = (hn.body.pShot.profile.rows || []).filter(function (r) { return r.key === "head" })[0]
            // face-on impact: the tick at your head at address always (the band's anchor), the band where the pattern
            // has one, and your head at impact in ink
            compare(fi.bandDrawn, !!headRow && headRow.cells[6].banded === true ? 2 : 1, "the head band at impact, where the pattern has a band, and its tick")
            compare(fi.inkDrawn, 1, "your head at impact")
            compare(frames.filter(function (f) { return f.tag === "P4" && (f.bandDrawn + f.inkDrawn) > 0 }).length, 0, "nothing of the top's own")
            verify(oneShown("chPosturePictureLegend", pic))
            const before = hn.replay.seeks
            press(di)
            compare(hn.replay.seeks, before + 1, "a frame press seeks")
            compare(hn.replay.lastSeekUs, di.seekUs, "to that frame's position")
            verify(di.seekUs > 0)
            // without the document: no frames, and why
            fresh({ posture: true }, 8)
            const pic2 = oneShown("chPosturePicture", oneShown("chPostureSection"))
            compare(shownAll("chPosturePictureFrame", pic2).length, 0)
            verify(shownAll("chPosturePictureWhy", pic2).length >= 1, "it says why there is no picture")
        }
        function test_34_the_posture_picture_at_the_glance_does_not_seek() {
            fresh({ posture: true, picture: true }, 8)
            useMode("Replay")
            usePosturePreset()
            const pic = oneShown("chPosturePicture", oneShown("chPostureSection"))
            const frames = shownAll("chPosturePictureFrame", pic)
            compare(frames.length, 6, "the glance shows the picture strip")
            const before = hn.replay.seeks
            press(frames[0])
            compare(hn.replay.seeks, before, "no affordance at the glance")
            compare(shownAll("chPosturePictureWhy", pic).length, 0, "no reasons at the glance")
        }
        // the Wrist section's own "How far can I trust this?" (PpChTrustFold, prefix chWrist): under the profile on This
        // shot (Analyse only) and on Session, closed by default, the presenter's trust lines verbatim when opened
        function test_35_the_wrist_trust_fold_on_this_shot_and_session() {
            fresh({ wrist: true }, 8)
            useWristPreset()
            const sec = oneShown("chWristSection")
            const lines = (hn.body.wShot.trust || {}).lines || []
            verify(lines.length >= 1, "This shot's presenter says trust lines")
            const tr = oneShown("chWristTrust", sec)
            compare(shownAll("chWristTrustLine", tr).length, 0, "closed by default")
            verify(tr.mapToItem(sec, 0, 0).y > oneShown("chWristProfile", sec).mapToItem(sec, 0, 0).y, "under the profile")
            press(oneShown("chWristTrustToggle", tr))
            compare(shownAll("chWristTrustLine", tr).map(function (t) { return t.text }).join("\n"), lines.join("\n"), "opened: This shot's lines, verbatim")
            press(oneShown("chWristTrustToggle", tr))
            compare(shownAll("chWristTrustLine", tr).length, 0, "and it closes")
            // Session: the session's lines, under the profile caption, closed
            tabPress("session")
            const ses = oneShown("chWristSession")
            const sl = (hn.body.wSession.trust || {}).lines || []
            verify(sl.length >= 1, "Session's presenter says trust lines")
            const st = oneShown("chWristTrust", ses)
            compare(shownAll("chWristTrustLine", st).length, 0, "closed by default")
            press(oneShown("chWristTrustToggle", st))
            compare(shownAll("chWristTrustLine", st).map(function (t) { return t.text }).join("\n"), sl.join("\n"), "opened: Session's lines, verbatim")
            // the glance (Replay): no fold
            tabPress("shot")
            useMode("Replay")
            useWristPreset()
            compare(shownAll("chWristTrust", oneShown("chWristSection")).length, 0, "no trust fold at the glance")
        }
    }
}
