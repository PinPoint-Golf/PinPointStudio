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

// THE CHARTS PANEL'S SINGLE-SHOT CONTROLS — the preset ("Metrics"), a metric on/off within it ("Custom"),
// overlay, P dots, cursor, the time window (chips, the in-place P-pair list, the brush), the folds, and
// their persistence. REWRITTEN 14 Oct 2026 for the new panel (build/charts-build/STEP3.md; INVENTORY §5):
// the old suite drove PpMetricChart's private state (_applyPreset, _toggle, _visible, presetCombo,
// sequenceStrip); the CONTRACT Addendum KEPT every one of these capabilities in the new This shot, so the
// claims are redesigned here against the new body (objectNames ch*, INVENTORY §6b), pressed with the
// pointer, over the REAL presenters fed the REAL 4 July session (ChartsHarness / chartsFixture).
//
// Old → new (the grouping itself stays pinned in C++: chart_metrics_test "seriesGroups"):
//   001 default group            → test_001 (each register's own default, design §3)
//   002 scalars not offered      → test_002 (a curve-less member is a card, never a trace)
//   003 legend = preset          → test_003 (the Metrics line lists the preset's members)
//   004 "All"                    → OBSOLETE: there is no "All" preset in the new panel (the design's presets
//                                  are the catalogue's groups; "every reading" is the profile below the traces)
//   005/006 chip → Custom        → test_005, test_006 (metricsOff; a switched-off metric stays in the line)
//   007 Custom survives a swing  → test_007
//   008/009 falls back           → test_009 (a preset the shot carries nothing of falls back; Custom goes with it)
//   010 preset kept              → test_010
//   011/012 title                → test_011, test_012 (the card's aside and the opener name the family)
//   013 selector placement       → test_013 (in the rail at wide; folded, the head still says it; Replay's chip)
//   014 sequence preset          → test_014
//   015 one curve is no preset   → OBSOLETE in QML: presets are offered on the SESSION's data now, so a shot
//                                  with one ω curve still has the preset; the ≥ 2 rule stays in chart_metrics_test
//   016 the strip                → test_016 (the chain and the peaks under the preset only; rising, no ring)
//   017 outside ≠ dash           → test_017 KEPT verbatim on the bare PpChartPlot (the camera tiles' compact
//                                  graph), plus test_017b on the new traces
//   018 the tile folds           → test_018 (the sequence table folds to its head; persisted)
//   019 opens on the data span   → test_019 (and the window chips, the P-pair list and the brush)
//   new: test_020 overlay (F6), test_021 P dots and their key (F7, F23), test_022 the hover reading (F26)
Item {
    id: probe
    width: 1400; height: 900

    // A bare plot, for the run-state test (test_017): a masked series — out of its window at both
    // ends, one bridged sample in the middle.
    PpChartPlot {
        id: barePlot
        width: 400; height: 200
        domStartUs: 0; domEndUs: 90000; valueLo: 0; valueHi: 10
        series: [{ key: "clubAngularSpeed", label: "club", unit: "°/s", color: "#ff8866",
                   t_us:  [0, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000],
                   value: [1, 2, 3, 4, 5, 6, 7, 8, 9],
                   valid: [0, 0, 1, 1, 0, 1, 1, 0, 0] }]
    }

    ChartsHarness { id: hn }

    readonly property var omegas: ["clubAngularSpeed", "leadArmAngularSpeed", "pelvisAngularSpeed", "thoraxAngularSpeed"]

    TestCase {
        id: tc
        name: "ChartPresets"
        when: windowShown

        // ── helpers ──────────────────────────────────────────────────────────
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
        function options(set) { return shownAll("chOption").filter(function (x) { return x.setKey === set }) }
        function option(set, key) {
            const o = options(set).filter(function (x) { return x.key === key })
            verify(o.length >= 1, "an option '" + key + "' in the '" + set + "' line")
            return o[0]
        }
        function wordOf(opt) { return opt.children.filter(function (k) { return hn.isText(k) })[0] }
        function clickOption(set, key) { press(wordOf(option(set, key))) }
        function pickInline(name, id) {
            const it = shownAll(name).filter(function (x) { return x.missId === id })
            verify(it.length === 1, "one '" + id + "' in the open " + name)
            press(it[0])
        }
        function choosePreset(name) {
            clickOption("preset", "open")
            pickInline("chPresetOption", name)
            compare(hn.body.tChoices.preset, name)
        }
        function traceKeys() { return shownAll(/^chTrace:/).map(function (t) { return t.objectName.slice(8) }).sort().join(",") }
        function useMode(m) {
            // "Replay" is the glance at the mat: the auto-replay after a shot (a Replay opened to review reads Analyse)
            SessionMode.autoReturnToCapture = m === "Replay"
            SessionMode.mode = m === "Analyse" ? SessionMode.analyse : SessionMode.replay
            settle()
            compare(hn.body.register, m)
        }
        function base() { return hn.sessionType + ":" + SessionMode.mode + ":" }
        function fresh(opts, ordinal, detail) {
            hn.build()
            hn.feed(opts || {})
            if (ordinal !== undefined && ordinal > 0) hn.pick(ordinal, detail)
            settle()
        }
        // a shot's detail with some of its curves dropped (a swing whose cameras missed them)
        function detailWithout(ordinal, keys) {
            const d = chartsFixture.detailOf(ordinal)
            d.series = d.series.filter(function (s) { return keys.indexOf(s.key) < 0 })
            return d
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
            hn.nav.currentIndex = hn.sessionType + 1
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
            appSettings.chartPrefs = ({})
            appSettings.sectionCollapse = ({})
        }

        function test_001_each_register_opens_on_its_own_default() {
            compare(hn.body.preset, "", "nothing chosen yet")
            compare(hn.body.tChoices.preset, "Kinematic sequence", "Analyse reads the sequence")
            useMode("Replay")
            compare(hn.body.tChoices.preset, "Club delivery", "Replay glances at club and speed")
            compare(oneShown("chPresetChip").text, "Club delivery")
        }

        function test_002_a_curveless_member_is_a_card_not_a_trace() {
            // find a preset with an instant-only member this shot read (a card at its P-position) beside a curve
            let found = null
            for (const p of hn.body.tChoices.presets) {
                hn.body.preset = p.id; settle()                // a search, not a reader's choice
                const cards = hn.body.tShot.cards || []
                if (cards.length && (hn.body.tShot.rows || []).length) { found = { preset: p.id, cards: cards }; break }
            }
            verify(found !== null, "some preset mixes curves and instant-only readings")
            for (const c of found.cards) {
                compare(hn.findAll(hn.body, "chTrace:" + c.key).length, 0, c.key + " draws no trace")
                const m = hn.body.tChoices.metrics.filter(function (x) { return x.key === c.key })[0]
                verify(!!m && m.curve === false, c.key + " is a member without a curve")
                verify(options("metric").some(function (o) { return o.key === c.key }), c.key + " is still offered in the Metrics line")
            }
            verify(textOf(oneShown("chSummary")).indexOf(found.cards[0].label) >= 0, "the card is read in the summary")
        }

        function test_003_the_metrics_line_carries_the_preset_not_the_swing() {
            const shown = function () { return options("metric").map(function (o) { return o.key }).sort().join(",") }
            compare(shown(), probe.omegas.slice().sort().join(","))
            choosePreset("Club delivery")
            compare(shown(), hn.body.tChoices.metrics.map(function (m) { return m.key }).sort().join(","))
            verify(shown().indexOf("AngularSpeed") < 0, "the sequence curves left with their preset")
            // each member wears its own hue (identity), the same as its trace
            for (const o of options("metric")) {
                const sw = o.children.filter(function (k) { return k.radius !== undefined && k.width > 0 })[0]
                verify(!!sw && Qt.colorEqual(sw.color, Theme.metricColor(o.key)), o.key + " swatch")
            }
        }

        function test_005_switching_a_metric_off_is_custom() {
            compare(hn.body.tChoices.custom, false)
            compare(textOf(oneShown("chChoice:metric")).indexOf("Showing"), 0)
            clickOption("metric", "pelvisAngularSpeed")
            compare(hn.body.metricsOff.join(","), "pelvisAngularSpeed")
            compare(hn.body.tChoices.custom, true)
            compare(textOf(oneShown("chChoice:metric")).indexOf("Custom"), 0, "the line says it is the reader's own")
            compare(hn.findAll(hn.body, "chTrace:pelvisAngularSpeed").length, 0)
            compare(traceKeys(), "clubAngularSpeed,leadArmAngularSpeed,thoraxAngularSpeed")
            compare(appSettings.chartPrefs[base() + "metricsOff"].join(","), "pelvisAngularSpeed", "saved")
        }

        function test_006_a_switched_off_metric_stays_in_the_line() {
            clickOption("metric", "pelvisAngularSpeed")
            const off = option("metric", "pelvisAngularSpeed")
            compare(off.on, false, "still there, off — never silently dropped")
            clickOption("metric", "pelvisAngularSpeed")
            compare(option("metric", "pelvisAngularSpeed").on, true)
            compare(hn.body.tChoices.custom, false)
            compare(traceKeys(), probe.omegas.slice().sort().join(","))
        }

        function test_007_custom_survives_a_new_swing_that_kept_its_group() {
            clickOption("metric", "thoraxAngularSpeed")
            hn.pick(3); settle()
            compare(hn.body.tShot.ordinal, 3)
            compare(hn.body.tChoices.preset, "Kinematic sequence")
            compare(hn.body.tChoices.custom, true)
            compare(option("metric", "thoraxAngularSpeed").on, false)
            compare(hn.findAll(hn.body, "chTrace:thoraxAngularSpeed").length, 0)
        }

        function test_009_a_preset_the_new_swing_lacks_falls_back() {
            clickOption("metric", "thoraxAngularSpeed")             // Custom on the sequence
            // a live shot whose cameras gave none of the four ω curves (and no sequence)
            fresh({ without: [15] })
            const d = detailWithout(15, probe.omegas)
            d.kinematicSequence = ({})
            hn.pick(15, d); settle()
            verify(hn.body.tShot.available, "the shot reads from its own detail")
            verify(hn.body.tChoices.preset !== "Kinematic sequence", "fell back to " + hn.body.tChoices.preset)
            compare(hn.body.tChoices.custom, false, "Custom went with the preset it was made in")
            for (const k of probe.omegas) compare(hn.findAll(hn.body, "chTrace:" + k).length, 0)
            verify(shownAll(/^chTrace:/).length + (hn.body.tShot.cards || []).length > 0, "it draws what it has")
            // the next shot that has the sequence opens on it again, the reader's switch-off kept
            hn.pick(8); settle()
            compare(hn.body.tChoices.preset, "Kinematic sequence")
            compare(hn.body.tChoices.custom, true)
        }

        function test_010_a_chosen_preset_is_kept_across_swings() {
            choosePreset("Club delivery")
            compare(appSettings.chartPrefs[base() + "preset"], "Club delivery")
            hn.pick(3); settle()
            compare(hn.body.tChoices.preset, "Club delivery")
            fresh({}, 5)                                         // a rebuild restores it
            compare(hn.body.tChoices.preset, "Club delivery")
        }

        function test_011_the_title_follows_the_preset() {
            compare(hn.body.cardAside, Theme.caps("Kinematic sequence"), "the stage card's aside")
            compare(wordOf(option("preset", "open")).text.indexOf("Kinematic sequence"), 0, "the opener names it")
            choosePreset("Club delivery")
            compare(hn.body.cardAside, Theme.caps("Club delivery"))
            compare(wordOf(option("preset", "open")).text.indexOf("Club delivery"), 0)
            // the folded Controls head still says it
            press(oneShown("chFold:controls"))
            verify(textOf(oneShown("chFold:controls")).indexOf("Club delivery") >= 0)
        }

        function test_012_a_custom_selection_is_still_titled_by_its_family() {
            clickOption("metric", "clubAngularSpeed")
            compare(hn.body.tChoices.custom, true)
            compare(hn.body.tChoices.presetTitle, "Kinematic sequence", "a title is a name, not a status line")
            compare(hn.body.cardAside, Theme.caps("Kinematic sequence"))
        }

        function test_013_the_picker_is_reachable_in_both_registers() {
            // the controls sit under the figure (Mark, 17 Oct 2026: headline, plot, controls, summary)
            const picker = oneShown("chPresetPicker")
            const view = oneShown("chShotView")
            const figs = shownAll("chTraces")
            if (figs.length) verify(picker.mapToItem(view, 0, 0).y > figs[0].mapToItem(view, 0, 0).y, "the controls sit under the figure")
            // folding Controls keeps the choice said on the fold's own line, and is remembered
            press(oneShown("chFold:controls"))
            compare(hn.body.collapsed.controls, true)
            compare(shownAll("chPresetPicker").length, 0)
            verify(textOf(oneShown("chFold:controls")).indexOf("Kinematic sequence") >= 0)
            compare(appSettings.sectionCollapse[base() + "controls"], true)
            fresh({}, 8)
            compare(hn.body.collapsed.controls, true, "the fold is restored")
            press(oneShown("chFold:controls"))
            verify(oneShown("chPresetPicker"))
            // Replay: a quiet chip at the right of the tab row, opening the list in place
            useMode("Replay")
            press(oneShown("chPresetChip"))
            const other = shownAll("chPresetOption").filter(function (o) { return o.missId !== "Club delivery" && o.missId !== "Kinematic sequence" })[0]
            verify(!!other, "another preset is offered")
            const name = other.missId
            press(other)
            compare(hn.body.tChoices.preset, name)
            compare(oneShown("chPresetChip").text, name)
            useMode("Analyse")
            compare(hn.body.tChoices.preset, "Kinematic sequence", "each register keeps its own")
        }

        // Mark, 16 Oct: with Controls folded there was no way to change the preset in Analyse, nor in a Replay
        // opened to review (which reads Analyse). Whatever is folded, every register keeps a way to the presets.
        function test_013b_the_preset_can_be_changed_with_controls_folded_in_every_register() {
            if (hn.body.collapsed.controls !== true) press(oneShown("chFold:controls"))
            compare(hn.body.collapsed.controls, true)
            const modes = [{ mode: SessionMode.analyse, auto: false, reg: "Analyse", why: "Analyse" },
                           { mode: SessionMode.replay, auto: false, reg: "Analyse", why: "a Replay opened to review" },
                           { mode: SessionMode.replay, auto: true, reg: "Replay", why: "the auto-replay after a shot" }]
            for (const m of modes) {
                SessionMode.autoReturnToCapture = m.auto
                SessionMode.mode = m.mode
                settle()
                compare(hn.body.register, m.reg, m.why + " reads " + m.reg)
                const before = hn.body.tChoices.preset
                press(oneShown("chPresetChip"))
                const other = shownAll("chPresetOption").filter(function (o) { return o.missId !== before })[0]
                verify(!!other, m.why + ": another preset is offered")
                const name = other.missId
                press(other)
                compare(hn.body.tChoices.preset, name, m.why + ": the preset changed")
            }
        }

        function test_014_the_sequence_preset_is_offered_and_draws_the_four() {
            verify(hn.body.tChoices.presets.some(function (p) { return p.id === "Kinematic sequence" }))
            compare(traceKeys(), probe.omegas.slice().sort().join(","))
        }

        function test_016_the_sequence_shows_under_its_preset_and_only_there() {
            verify(oneShown("chSeqChain"))
            for (const seg of ["leadArm", "club", "pelvis"]) verify(hn.findAll(hn.body, "chPeak:" + seg).length === 1, seg + " peak ringed")
            compare(hn.findAll(hn.body, /^chRising:.+/).length, 0, "shot 8 has no trunk still rising")
            verify(oneShown("chSeqTable"))
            choosePreset("Club delivery")
            compare(shownAll("chSeqChain").length, 0)
            compare(shownAll("chSeqTable").length, 0)
            compare(hn.findAll(hn.body, /^chPeak:.+/).length, 0, "no sequence peaks (a plain curve's peak has no segment)")
            // a trunk that had not peaked by impact: the open chevron, never a ring on the ball (S8)
            hn.body.choose("preset", "Kinematic sequence"); settle()
            fresh({ without: [15] })
            const d = chartsFixture.detailOf(15)
            const ks = JSON.parse(JSON.stringify(d.kinematicSequence))
            for (const n of ks.nodes) if (n.segment === "pelvis") { n.placed = false; n.peakNoEarlierThanMs = 0 }
            ks.order = ks.order.filter(function (s) { return s !== "pelvis" })
            d.kinematicSequence = ks
            hn.pick(15, d); settle()
            compare(hn.findAll(hn.body, "chPeak:pelvis").length, 0, "no ring on the ball")
            const rising = hn.findAll(hn.body, "chRising:pelvis")
            compare(rising.length, 1)
            verify(textOf(rising[0]).indexOf("rising") >= 0, "it says rising")
            verify(hn.findAll(hn.body, "chPeak:club").length === 1, "the club still rings")
        }

        function test_017_outside_the_window_is_not_a_dash() {
            // A dash says "this joins two measurements and is not one". The unmeasured runs that
            // reach the series' ends join nothing — they are the curve outside the window the
            // metric is defined on (the sequence masks everything but transition→impact) — and
            // drawn dashed they made a chart read as mostly untrusted. Faint solid, not dashed.
            var runs = barePlot._traceRuns
            compare(runs.length, 5)
            var states = runs.map(function (r) { return r.outside ? "outside" : r.dashed ? "dashed" : "solid" })
            compare(states.join(","), "outside,solid,dashed,solid,outside")
            // The leading run still reaches one sample into its solid neighbour, so it joins up.
            compare(runs[0].t[runs[0].t.length - 1], 20000)
        }

        function test_017b_the_new_traces_never_dash_an_end() {
            // the same rule on This shot's traces: a run touching either end of a curve is never "bridged"
            hn.body.choose("window", "full"); settle()
            for (const r of hn.body.tShot.rows) {
                const runs = r.runs || []
                if (!runs.length) continue
                verify(runs[0].style !== "bridged" && runs[runs.length - 1].style !== "bridged", r.key + " ends " + runs[0].style + "/" + runs[runs.length - 1].style)
            }
        }

        function test_018_the_sequence_table_folds_to_its_head() {
            const head = oneShown("chFold:sequence")
            verify(oneShown("chSeqTable"))
            press(head)
            compare(hn.body.collapsed.sequence, true)
            compare(shownAll("chSeqTable").length, 0, "the rows go")
            verify(textOf(oneShown("chFold:sequence")).indexOf(hn.body.tShot.sequence.verdictText) >= 0, "the verdict stays on the head")
            compare(appSettings.sectionCollapse[base() + "sequence"], true)
            press(oneShown("chFold:sequence"))
            verify(oneShown("chSeqTable"))
        }

        function test_019_a_trimmed_chart_opens_on_its_data_span() {
            const w = hn.body.tShot.window
            compare(hn.body.tChoices.window, "data")
            compare(w.tagsText, "P4→P8")
            compare(w.header, "Top → Shaft-parallel through")
            verify(textOf(oneShown("chWindowHeader")).indexOf("P4→P8") >= 0)
            // offered as a chip of its own, straight after the swing's
            const keys = options("window").map(function (o) { return o.key })
            verify(keys.indexOf("data") === keys.indexOf("swing") + 1, keys.join(","))
            clickOption("window", "swing")
            compare(hn.body.window, "swing")
            compare(appSettings.chartPrefs[base() + "window"], "swing")
            verify(hn.body.tShot.window.t0Ms < w.t0Ms, "the swing is wider than the data")
            // a pair of adjacent P-positions, from the list that opens in place
            clickOption("window", "_segs")
            const segs = shownAll("chWindowOption")
            verify(segs.length >= 3)
            press(segs[1])
            verify(String(hn.body.window).indexOf("seg:") === 0, hn.body.window)
            // a sweep on the brush is a custom window
            const br = oneShown("chWindowBrush")
            intoView(br)
            const ty = br.sparkH + br.roles.u(12) + br.trackH / 2       // the recording's track
            mousePress(br, br.width * 0.05, ty)
            mouseMove(br, br.width * 0.20, ty)
            mouseMove(br, br.width * 0.35, ty)
            mouseRelease(br, br.width * 0.35, ty)
            settle()
            compare(hn.body.window, "custom")
            verify(hn.body.customEndUs > hn.body.customStartUs && hn.body.customStartUs >= 0)
            compare(appSettings.chartPrefs[base() + "window"], "custom")
            compare(hn.body.tChoices.window, "custom")
            verify(options("window").some(function (o) { return o.key === "custom" }), "Custom is a chip while it is current")
        }

        function test_020_overlay_is_one_shared_plot() {
            clickOption("draw", "overlay")
            compare(hn.body.overlay, true)
            compare(shownAll(/^chTrace:/).map(function (t) { return t.objectName }).join(","), "chTrace:overlay")
            for (const k of probe.omegas) verify(oneShown("chTraceLabel:" + k), k + " named on the shared plot")
            clickOption("draw", "split")
            compare(traceKeys(), probe.omegas.slice().sort().join(","))
        }

        function test_021_p_dots_and_their_key_in_words() {
            const key = textOf(oneShown("chDotKey"))
            verify(key.indexOf("P dots") === 0, key)
            verify(hn.body.tShot.dotKey.every(function (k) { return key.indexOf(k.word) >= 0 }), "every kind present is worded")
            clickOption("toggles", "dots")
            compare(hn.body.showDots, false)
            compare(shownAll("chDotKey").length, 0)
            compare(appSettings.chartPrefs[base() + "dots"], false)
            clickOption("toggles", "dots")
            verify(oneShown("chDotKey"))
        }

        function test_022_the_hover_reads_the_curves_in_place_and_the_cursor_toggles() {
            const tr = oneShown("chTrace:clubAngularSpeed")
            const seek = oneShown("chSeek", tr)
            intoView(seek)
            mouseMove(seek, seek.width * 0.55, seek.height / 2)
            settle()
            verify(oneShown("chCrosshair"), "the crosshair")
            verify(/raw /.test(textOf(tr)), "the figure column reads the hover, raw beside the mean: " + textOf(tr))
            mouseMove(hn.body, 2, 2); settle()
            clickOption("toggles", "cursor")
            compare(hn.body.showCursor, false)
            const seek2 = oneShown("chSeek", oneShown("chTrace:clubAngularSpeed"))
            mouseMove(seek2, seek2.width * 0.5, seek2.height / 2)
            settle()
            compare(shownAll("chCrosshair").length, 0, "no crosshair with the cursor off")
            compare(appSettings.chartPrefs[base() + "cursor"], false)
        }
    }
}
