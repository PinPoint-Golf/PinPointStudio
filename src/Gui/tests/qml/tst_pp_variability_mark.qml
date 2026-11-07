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

// The variability mark and its badge (docs/design/variability-mark/spec.md §3 – §5, §8.6, §9.5.4): the
// tone of each grade, grade 0 drawing only its neutral element, clamping, the accessible names, every
// graded variant × form × grade building silently under all 14 themes, the badge's word, colours,
// layers, basis string, measurement-limited substitution and dense size, and the print uplift.
Item {
    id: probe
    width: 600; height: 200

    Component { id: markComp; PpVariabilityMark {} }
    Component { id: badgeComp; PpVariabilityBadge {} }
    Component { id: tsComp; PpTypeset { medium: "print"; fixedAesthetic: "folio" } }

    readonly property var graded: [PpVariabilityMark.Dispersion, PpVariabilityMark.Overlay, PpVariabilityMark.Needle]
    readonly property var allVariants: [PpVariabilityMark.Dispersion, PpVariabilityMark.Overlay, PpVariabilityMark.Needle,
                                        PpVariabilityMark.Typicality, PpVariabilityMark.TypicalityTrace]
    readonly property var forms: [PpVariabilityMark.Icon, PpVariabilityMark.Cell]

    function findNamed(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const f = findNamed(item.children[i], name)
            if (f) return f
        }
        return null
    }
    // The mark's ShapePaths, in paint order.
    function paths(mark) {
        const shape = findNamed(mark, "variabilityShape"), out = []
        if (!shape) return out
        for (let i = 0; i < shape.data.length; ++i)
            if (String(shape.data[i].objectName).indexOf("variability") === 0) out.push(shape.data[i])
        return out
    }
    function named(mark, name) { return paths(mark).filter(p => p.objectName === name) }
    // Arcs, lines and ticks are neutral; the needle (a line) and everything else wears the tone.
    function isNeutral(p) {
        return p.objectName === "variabilityArc" || p.objectName === "variabilityLine" || p.objectName === "variabilityTicks"
    }
    function seen(c) { return c.a > 0.001 }
    function where(v, f, g) { return "variant " + v + " form " + f + " grade " + g + " (" + Theme.aesthetic + (Theme.dark ? " dark)" : " light)") }
    function toneFor(g) {
        return g >= 4 ? Theme.colorGood : g === 3 ? Theme.colorAttention : g >= 1 ? Theme.colorWarn : Theme.colorText3
    }
    function sameRgb(a, b) { return Qt.colorEqual(Qt.alpha(a, 1), Qt.alpha(b, 1)) }
    function mk(props) { return markComp.createObject(probe, props) }
    function bdg(props) { return badgeComp.createObject(probe, props) }
    function sameColor(a, b) { return Math.abs(a.r - b.r) < 0.004 && Math.abs(a.g - b.g) < 0.004 && Math.abs(a.b - b.b) < 0.004 && Math.abs(a.a - b.a) < 0.004 }
    // A wash's alpha on screen in the current theme (§3): × variabilityWashAlpha, clamped to 1.
    function wash(a) { return Math.min(1, a * Theme.variabilityWashAlpha) }

    TestCase {
        name: "PpVariabilityMark"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }
        function cleanup() { Theme.themeIndex = was }

        // §3: 5–4 good, 3 attention, 2–1 warn, 0 text3; typicality the accent. Every theme.
        function test_toneByGrade() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                for (const v of probe.graded) {
                    for (let g = 0; g <= 5; ++g) {
                        const m = probe.mk({ variant: v, grade: g })
                        verify(Qt.colorEqual(m.tone, probe.toneFor(g)), probe.where(v, 0, g) + ": tone " + m.tone)
                        m.destroy()
                    }
                }
                for (const v of [PpVariabilityMark.Typicality, PpVariabilityMark.TypicalityTrace]) {
                    const m = probe.mk({ variant: v, grade: 2 })
                    verify(Qt.colorEqual(m.tone, Theme.colorAccent), probe.where(v, 0, 2) + ": typicality wears the accent")
                    m.destroy()
                }
            }
        }

        // The tone is what the tone elements are drawn in, and a grade change eases the colour over
        // durationSlow without changing the geometry's objects.
        function test_drawnInTheTone() {
            const m = probe.mk({ variant: PpVariabilityMark.Dispersion, grade: 4 })
            const dot = probe.named(m, "variabilityDot")[0]
            verify(probe.sameRgb(dot.fillColor, Theme.colorGood), "grade 4 dot in colorGood")
            m.grade = 2
            tryVerify(() => probe.sameRgb(dot.fillColor, Theme.colorWarn), Theme.durationSlow + 1000, "eases to colorWarn")
            compare(probe.named(m, "variabilityDot")[0], dot, "the same ShapePath: the geometry is not rebuilt")
            const n = probe.mk({ variant: PpVariabilityMark.Needle, form: PpVariabilityMark.Cell, grade: 3 })
            verify(probe.sameRgb(probe.named(n, "variabilityNeedle")[0].strokeColor, Theme.colorAttention), "needle in colorAttention")
            compare(probe.named(n, "variabilityNeedle")[0].strokeColor.a, 1, "needle at full tone")
            verify(probe.sameRgb(probe.named(n, "variabilityPivot")[0].fillColor, Theme.colorAttention), "pivot in colorAttention")
            m.destroy(); n.destroy()
        }

        // §4.7: grade 0 is the neutral element alone, in colorBorderStrong or colorBorderMid.
        function test_gradeZeroShowsOnlyTheNeutralElement() {
            for (const v of probe.graded) {
                for (const f of probe.forms) {
                    for (const g of [0, 3]) {
                        const m = probe.mk({ variant: v, form: f, grade: g })
                        const ps = probe.paths(m)
                        verify(ps.length > 0, probe.where(v, f, g) + ": draws")
                        let neutral = 0, tone = 0
                        for (const p of ps) {
                            if (probe.isNeutral(p)) {
                                verify(probe.seen(p.strokeColor), probe.where(v, f, g) + ": " + p.objectName + " drawn")
                                verify(Qt.colorEqual(p.strokeColor, Theme.colorBorderStrong) || Qt.colorEqual(p.strokeColor, Theme.colorBorderMid),
                                       probe.where(v, f, g) + ": " + p.objectName + " is a neutral token")
                                ++neutral
                            } else if (probe.seen(p.fillColor) || probe.seen(p.strokeColor)) {
                                ++tone
                            }
                        }
                        verify(neutral >= 1, probe.where(v, f, g) + ": a neutral element")
                        if (g === 0) compare(tone, 0, probe.where(v, f, g) + ": no tone element")
                        else verify(tone > 0, probe.where(v, f, g) + ": tone elements")
                        m.destroy()
                    }
                }
            }
            // The Needle at grade 5: its three wedges have no spread and are skipped.
            const n5 = probe.mk({ variant: PpVariabilityMark.Needle, grade: 5 })
            for (const w of probe.named(n5, "variabilityWedge"))
                verify(!probe.seen(w.fillColor), "grade 5 wedge skipped")
            verify(probe.seen(probe.named(n5, "variabilityNeedle")[0].strokeColor), "grade 5 keeps the needle")
            n5.destroy()
        }

        // §4.1: out of range clamps, with a console warning.
        function test_clamping() {
            ignoreWarning(/PpVariabilityMark: grade 7 is outside 0–5; clamped to 5/)
            const hi = probe.mk({ grade: 7 })
            compare(hi._grade, 5)
            compare(hi.label, "Grooved")
            compare(hi.Accessible.name, "Consistency: Grooved (5 of 5)")
            ignoreWarning(/PpVariabilityMark: grade -1 is outside 0–5; clamped to 0/)
            const lo = probe.mk({ grade: -1 })
            compare(lo._grade, 0)
            compare(lo.label, "Not enough swings")
            ignoreWarning(/PpVariabilityMark: grade 9 is outside 0–5; clamped to 5/)
            lo.grade = 9
            compare(lo._grade, 5)
            hi.destroy(); lo.destroy()
        }

        // §4.2: "<domain>: <word> (<grade> of 5)", from the vocabulary; role Graphic.
        function test_accessibleNames() {
            const cases = [
                [PpVariabilityMark.Dispersion, 4, "Consistency: Repeatable (4 of 5)", "Repeatable"],
                [PpVariabilityMark.Overlay,    1, "Consistency: Scattered (1 of 5)", "Scattered"],
                [PpVariabilityMark.Dispersion, 0, "Consistency: Not enough swings", "Not enough swings"],
                [PpVariabilityMark.Needle,     4, "Measurement: Steady (4 of 5)", "Steady"],
                [PpVariabilityMark.Needle,     0, "Measurement: Checking", "Checking"],
            ]
            for (const c of cases) {
                const m = probe.mk({ variant: c[0], grade: c[1] })
                compare(m.Accessible.name, c[2])
                compare(m.Accessible.role, Accessible.Graphic)
                compare(m.label, c[3])
                m.destroy()
            }
            const n = probe.mk({ variant: PpVariabilityMark.Needle })
            verify(n.vocabulary === MeasurementVocabulary, "Needle speaks Measurement")
            const o = probe.mk({ variant: PpVariabilityMark.Overlay })
            verify(o.vocabulary === ConsistencyVocabulary, "Overlay speaks Consistency")
            // §4.1: the label is the word the accessible name uses, overridden or not.
            const l = probe.mk({ grade: 4, label: "Mostly the same" })
            compare(l.Accessible.name, "Consistency: Mostly the same (4 of 5)")
            l.label = "Very much the same"
            compare(l.Accessible.name, "Consistency: Very much the same (4 of 5)")
            l.destroy()
            const t = probe.mk({ variant: PpVariabilityMark.Typicality })
            verify(t.vocabulary === TypicalityVocabulary, "Typicality speaks Typicality")
            compare(t.label, "", "no band yet: no typicality word (tst_pp_variability_typicality.qml has the words)")
            n.destroy(); o.destroy(); t.destroy()
        }

        // §4.1 `values` (not `data`, which stays Item's child list): set, read back, and a nested child
        // still lands in the mark.
        function test_valuesAndChildren() {
            const m = probe.mk({ variant: PpVariabilityMark.Typicality, values: { z: 0.4 } })
            compare(m.values.z, 0.4)
            const kid = Qt.createQmlObject('import QtQuick; import PinPointStudio; PpVariabilityMark { grade: 3; Item { objectName: "nested" } }', probe)
            verify(probe.findNamed(kid, "nested") !== null, "a nested child is a child")
            verify(probe.findNamed(kid, "variabilityShape") !== null, "and the mark still draws")
            m.destroy(); kid.destroy()
        }

        // Pixel geometry: design units × size / 24, e.g. the Dispersion Icon's ring, r 10.5 (§4.4).
        function test_pixelGeometry() {
            for (const s of [20, 48]) {
                const m = probe.mk({ grade: 3, size: s })
                const ring = probe.named(m, "variabilityArc")[0], arc = ring.pathElements[0]
                fuzzyCompare(arc.radiusX, 10.5 * s / 24, 1e-9, "ring radius at " + s)
                fuzzyCompare(arc.centerX, 12 * s / 24, 1e-9, "ring centre at " + s)
                compare(ring.strokeWidth, 1, "ring stroke stays 1 px at " + s)
                const c = probe.mk({ variant: PpVariabilityMark.Dispersion, form: PpVariabilityMark.Cell, grade: 1, size: s })
                const e = probe.named(c, "variabilityEllipse")[0].pathElements[0]
                fuzzyCompare(e.radiusX, (30 + 4) * s / 24, 1e-9, "grade-1 ellipse rx at " + s)
                fuzzyCompare(e.radiusY, (7.5 + 3.5) * s / 24, 1e-9, "grade-1 ellipse ry at " + s)
                m.destroy(); c.destroy()
            }
        }

        // §4.1 size, §4.2 strokes: pixels from k = size / 24, never scaled; the small strokes at ≤ 16 px.
        function test_strokesStayInPixels() {
            const m = probe.mk({})
            compare(m.size, Theme.sp(Theme.variabilityIconSize), "default size")
            compare(m.implicitWidth, m.size)
            const c = probe.mk({ form: PpVariabilityMark.Cell, size: 24 })
            compare(c.implicitWidth, 80); compare(c.implicitHeight, 24)
            for (const s of [16, 24, 48]) {
                const o = probe.mk({ variant: PpVariabilityMark.Overlay, grade: 3, size: s })
                compare(o.scale, 1)
                compare(probe.named(o, "variabilityTrace")[0].strokeWidth, s <= 16 ? 1.25 : 1.5, "Overlay Icon trace at " + s)
                compare(probe.named(o, "variabilityLine")[0].strokeWidth, 1, "baseline at " + s)
                const n = probe.mk({ variant: PpVariabilityMark.Needle, grade: 3, size: s })
                compare(probe.named(n, "variabilityNeedle")[0].strokeWidth, s <= 16 ? 1.25 : 1.5, "Needle at " + s)
                compare(probe.named(n, "variabilityArc")[0].strokeWidth, 1, "gauge at " + s)
                const oc = probe.mk({ variant: PpVariabilityMark.Overlay, form: PpVariabilityMark.Cell, grade: 3, size: s })
                compare(probe.named(oc, "variabilityTrace")[0].strokeWidth, 1.3, "Overlay Cell trace at " + s)
                o.destroy(); n.destroy(); oc.destroy()
            }
            m.destroy(); c.destroy()
        }

        // Every variant × form × grade, under all 14 themes, builds and draws with no warning at all.
        function test_everyCombinationIsSilent() {
            // Warm the font database first: its one-off "Populating font family aliases" note is the
            // test machine's, not the mark's, and lands on whichever test first lays out text.
            const warm = probe.bdg({ grade: 3, contentLayer: "technical", basisText: "n 1" })
            wait(0)
            warm.destroy()
            failOnWarning(/.*/)
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const made = []
                for (const v of probe.allVariants)
                    for (const f of probe.forms)
                        for (let g = 0; g <= 5; ++g) {
                            // TypicalityTrace has no Icon and says so (§6.2): tst_pp_variability_typicality.qml.
                            if (v === PpVariabilityMark.TypicalityTrace && f === PpVariabilityMark.Icon) continue
                            const m = probe.mk({ variant: v, form: f, grade: g })
                            verify(m !== null, probe.where(v, f, g))
                            if (probe.graded.indexOf(v) >= 0)
                                verify(m.label !== "", probe.where(v, f, g) + ": a word")
                            // Typicality with no values still draws its zones (or the trace its baseline).
                            verify(probe.paths(m).length > 0, probe.where(v, f, g) + ": a shape")
                            made.push(m)
                        }
                for (const f of probe.forms)
                    for (let g = 0; g <= 5; ++g)
                        for (const layer of ["golfer", "technical"])
                            made.push(probe.bdg({ form: f, grade: g, contentLayer: layer, basisText: "σ 2.1° · n 24",
                                                  measurementLimited: g === 2 }))
                compare(TypicalityVocabulary.word(""), "", "no band yet: no word, and no warning")
                wait(0)   // a frame, so the shapes are polished and drawn under this theme
                for (const m of made) m.destroy()
            }
        }

        // §9.5.4: in print, translucent fills × variabilityPrintAlphaScale, clamped to 1; strokes unchanged.
        function test_printAlpha() {
            const k = Theme.variabilityPrintAlphaScale
            const d = probe.mk({ variant: PpVariabilityMark.Dispersion, form: PpVariabilityMark.Cell, grade: 3 })
            const ell = probe.named(d, "variabilityEllipse")[0], dot = probe.named(d, "variabilityDot")[0]
            fuzzyCompare(ell.fillColor.a, probe.wash(0.12), 0.005, "screen ellipse")
            fuzzyCompare(dot.fillColor.a, 0.85, 0.005, "screen dot")
            d.medium = "print"
            fuzzyCompare(ell.fillColor.a, Math.min(1, 0.12 * k), 0.005, "print ellipse")
            fuzzyCompare(dot.fillColor.a, Math.min(1, 0.85 * k), 0.005, "print dot, clamped")
            fuzzyCompare(probe.named(d, "variabilityLine")[0].strokeColor.a, Theme.colorBorderMid.a, 0.005, "neutral line: its token, unchanged")

            for (const c of [[20, [0.12, 0.16, 0.24]], [16, [0.14, 0.18, 0.26]]]) {
                const n = probe.mk({ variant: PpVariabilityMark.Needle, grade: 2, size: c[0] })
                const ws = probe.named(n, "variabilityWedge")
                compare(ws.length, 3)
                for (let i = 0; i < 3; ++i) fuzzyCompare(ws[i].fillColor.a, probe.wash(c[1][i]), 0.005, "screen wedge " + i + " at " + c[0])
                n.medium = "print"
                for (let i = 0; i < 3; ++i) fuzzyCompare(ws[i].fillColor.a, Math.min(1, c[1][i] * k), 0.005, "print wedge " + i + " at " + c[0])
                n.destroy()
            }
            const o = probe.mk({ variant: PpVariabilityMark.Overlay, grade: 3, medium: "print" })
            fuzzyCompare(probe.named(o, "variabilityTrace")[0].strokeColor.a, 0.70, 0.005, "a trace is a stroke: unchanged")
            const b = probe.bdg({ form: PpVariabilityMark.Cell, grade: 3, medium: "print" })
            fuzzyCompare(probe.named(probe.findNamed(b, "variabilityMark"), "variabilityEllipse")[0].fillColor.a,
                         Math.min(1, 0.12 * k), 0.005, "the badge passes print to its mark")
            d.destroy(); o.destroy(); b.destroy()
        }

        // §3, §10 (Mark, 7 Nov 2026): on screen every wash × variabilityWashAlpha, 1 in light themes and
        // 3 in dark, clamped to 1. The dots and strokes are not washes; print with roles never reads it.
        function test_washAlphaByTheme() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                compare(Theme.variabilityWashAlpha, Theme.dark ? 3 : 1, Theme.aesthetic + (Theme.dark ? " dark" : " light"))
            }
            // Every aesthetic, light then dark: the washes triple, the rest stay.
            const ts = tsComp.createObject(probe), kp = Theme.variabilityPrintAlphaScale
            for (let t = 0; t < Theme.themeCount; t += 2) {
                const seen = {}
                for (const dark of [false, true]) {
                    Theme.themeIndex = t + (dark ? 1 : 0)
                    const w = probe.where(0, 0, 3)
                    const d = probe.mk({ variant: PpVariabilityMark.Dispersion, form: PpVariabilityMark.Cell, grade: 3 })
                    const ni = probe.mk({ variant: PpVariabilityMark.Needle, grade: 2 })
                    const ns = probe.mk({ variant: PpVariabilityMark.Needle, grade: 2, size: 16 })
                    const nc = probe.mk({ variant: PpVariabilityMark.Needle, form: PpVariabilityMark.Cell, grade: 2 })
                    const o = probe.mk({ variant: PpVariabilityMark.Overlay, grade: 3 })
                    const a = {
                        ellipse: probe.named(d, "variabilityEllipse")[0].fillColor.a,
                        wedgesIcon: probe.named(ni, "variabilityWedge").map(p => p.fillColor.a),
                        wedgesSmall: probe.named(ns, "variabilityWedge").map(p => p.fillColor.a),
                        wedgesCell: probe.named(nc, "variabilityWedge").map(p => p.fillColor.a),
                        dot: probe.named(d, "variabilityDot")[0].fillColor.a,
                        trace: probe.named(o, "variabilityTrace")[0].strokeColor.a,
                        needle: probe.named(ni, "variabilityNeedle")[0].strokeColor.a
                    }
                    compare(a.wedgesIcon.length, 3); compare(a.wedgesSmall.length, 3); compare(a.wedgesCell.length, 3)
                    // A neutral stroke is its own token, never multiplied.
                    fuzzyCompare(probe.named(d, "variabilityLine")[0].strokeColor.a, Theme.colorBorderMid.a, 0.005, w + ": target line, its token")
                    if (dark) {
                        const l = seen.light
                        fuzzyCompare(a.ellipse, Math.min(1, 3 * l.ellipse), 0.005, w + ": ellipse × 3")
                        // × 3, but the innermost is lowered so the three composite to at most
                        // variabilityWedgeCompositeMax where they overlap (R25), and the needle reads.
                        const x3 = (ws, i) => {
                            const t = Math.min(1, 3 * ws[i])
                            if (i < 2) return t
                            const under = (1 - Math.min(1, 3 * ws[0])) * (1 - Math.min(1, 3 * ws[1]))
                            return Math.max(0, Math.min(t, 1 - (1 - Theme.variabilityWedgeCompositeMax) / under))
                        }
                        for (let i = 0; i < 3; ++i) {
                            fuzzyCompare(a.wedgesIcon[i], x3(l.wedgesIcon, i), 0.005, w + ": Icon wedge " + i + " × 3")
                            fuzzyCompare(a.wedgesSmall[i], x3(l.wedgesSmall, i), 0.005, w + ": small Icon wedge " + i + " × 3")
                            fuzzyCompare(a.wedgesCell[i], x3(l.wedgesCell, i), 0.005, w + ": Cell wedge " + i + " × 3")
                        }
                        for (const ws of [a.wedgesIcon, a.wedgesSmall, a.wedgesCell])
                            verify(1 - (1 - ws[0]) * (1 - ws[1]) * (1 - ws[2]) <= Theme.variabilityWedgeCompositeMax + 0.005, w + ": the wedges composite under the cap")
                        fuzzyCompare(a.dot, l.dot, 0.005, w + ": a dot is not a wash")
                        fuzzyCompare(a.trace, l.trace, 0.005, w + ": a trace is a stroke")
                        fuzzyCompare(a.needle, l.needle, 0.005, w + ": the needle is a stroke")
                        // Print with roles: the print scale alone, whatever the screen's theme.
                        d.roles = ts; ni.roles = ts
                        fuzzyCompare(probe.named(d, "variabilityEllipse")[0].fillColor.a, Math.min(1, 0.12 * kp), 0.005, w + ": print ellipse")
                        const pw = probe.named(ni, "variabilityWedge")
                        for (let i = 0; i < 3; ++i)
                            fuzzyCompare(pw[i].fillColor.a, Math.min(1, [0.12, 0.16, 0.24][i] * kp), 0.005, w + ": print wedge " + i)
                    } else {
                        fuzzyCompare(a.ellipse, 0.12, 0.005, w + ": light ellipse at the spec's alpha")
                    }
                    seen[dark ? "dark" : "light"] = a
                    d.destroy(); ni.destroy(); ns.destroy(); nc.destroy(); o.destroy()
                }
            }
            ts.destroy()
        }

        // §9.5.4 with roles: the page's sizes, inks and furniture; the fill uplift is the print scale,
        // never roles.tintK. On screen (no roles, or roles on screen) the mark reads Theme.
        function test_printWithRoles() {
            const ts = tsComp.createObject(probe), k = Theme.variabilityPrintAlphaScale
            verify(Qt.colorEqual(ts.good, Theme._themes.folio.light.colorGood), "good = Folio light colorGood")
            const m = probe.mk({ grade: 4, roles: ts })
            compare(m.medium, "print", "medium follows roles")
            compare(m.size, ts.u(Theme.variabilityIconSize), "size from roles.u()")
            verify(Qt.colorEqual(m.tone, ts.good), "grade 4 in roles.good")
            const ring = probe.named(m, "variabilityArc")[0]
            verify(probe.sameColor(ring.strokeColor, Qt.alpha(ts.baseline, ts.baseline.a * ts.baselineOpacity)), "ring in the page's baseline")
            fuzzyCompare(probe.named(m, "variabilityDot")[0].fillColor.a, Math.min(1, 0.85 * k), 0.005, "dot uplift")
            for (const c of [[3, ts.attention], [2, ts.warn], [1, ts.warn], [0, ts.ink3], [5, ts.good]]) {
                m.grade = c[0]
                verify(Qt.colorEqual(m.tone, c[1]), "grade " + c[0] + " in the page's ink")
            }
            const cell = probe.mk({ form: PpVariabilityMark.Cell, grade: 3, roles: ts })
            fuzzyCompare(probe.named(cell, "variabilityEllipse")[0].fillColor.a, Math.min(1, 0.12 * k), 0.005, "× print scale, not tintK")
            verify(probe.sameColor(probe.named(cell, "variabilityLine")[0].strokeColor, Qt.alpha(ts.grid, ts.grid.a * ts.gridOpacity)),
                   "target line in the page's grid ink")
            const typ = probe.mk({ variant: PpVariabilityMark.Typicality, roles: ts })
            verify(Qt.colorEqual(typ.tone, ts.accent), "typicality in roles.accent")
            ts.medium = "screen"
            compare(m.medium, "screen")
            compare(m.size, Theme.sp(Theme.variabilityIconSize), "roles on screen: Theme sizes")
            verify(Qt.colorEqual(m.tone, Theme.colorGood), "roles on screen: Theme inks")
            m.destroy(); cell.destroy(); typ.destroy(); ts.destroy()
        }
    }

    TestCase {
        name: "PpVariabilityBadge"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }
        function cleanup() { Theme.themeIndex = was }

        function parts(b) {
            return { mark: probe.findNamed(b, "variabilityMark"), word: probe.findNamed(b, "variabilityWord"),
                     basis: probe.findNamed(b, "variabilityBasis") }
        }

        // §5: the word in the tone in the body face at 12 (fontSzBody2); colorText3 at grade 0 (§4.7).
        function test_wordAndColours() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                for (const v of probe.graded) {
                    for (let g = 0; g <= 5; ++g) {
                        const b = bdgOf(v, g), p = parts(b)
                        const vocab = v === PpVariabilityMark.Needle ? MeasurementVocabulary : ConsistencyVocabulary
                        compare(p.word.text, vocab.word(g), probe.where(v, 0, g))
                        compare(b.word, vocab.word(g))
                        verify(Qt.colorEqual(p.word.color, g === 0 ? Theme.colorText3 : probe.toneFor(g)), probe.where(v, 0, g) + ": word colour")
                        verify(Qt.colorEqual(b.tone, probe.toneFor(g)), probe.where(v, 0, g) + ": tone")
                        compare(p.word.font.family, Theme.fontBody)
                        compare(p.word.font.pixelSize, Theme.fontSzBody2)
                        compare(p.word.font.weight, Theme.fontBodyWeight)
                        compare(b.line, vocab.line(g), "the golfer-layer tooltip line")
                        b.destroy()
                    }
                }
            }
        }
        function bdgOf(v, g) { return probe.bdg({ variant: v, grade: g }) }

        // §5 sizes and gaps: Icon at variabilityIconSize (16 dense), 6 to the word; Cell at
        // variabilityCellW × variabilityCellH, 10 to the word.
        function test_sizesAndGaps() {
            const icon = probe.bdg({ grade: 4 }), dense = probe.bdg({ grade: 4, dense: true })
            const cell = probe.bdg({ grade: 4, form: PpVariabilityMark.Cell })
            const pi = parts(icon), pd = parts(dense), pc = parts(cell)
            compare(pi.mark.size, Theme.sp(Theme.variabilityIconSize))
            compare(pi.mark.width, Theme.sp(Theme.variabilityIconSize))
            compare(pd.mark.size, Theme.sp(16), "dense")
            compare(pc.mark.form, PpVariabilityMark.Cell)
            compare(pc.mark.height, Theme.sp(Theme.variabilityCellH))
            compare(pc.mark.width, pc.mark.height * 80 / 24, "the box is the drawing's own width")
            verify(Math.abs(pc.mark.width - Theme.sp(Theme.variabilityCellW)) < 1.5, "≈ variabilityCellW")
            compare(pc.mark.size, Theme.sp(Theme.variabilityCellH))
            tryCompare(icon, "implicitWidth", pi.mark.width + Theme.gap(6) + pi.word.implicitWidth)   // the Row lays out on polish
            compare(pi.word.x - (pi.mark.x + pi.mark.width), Theme.gap(6), "Icon gap")
            tryCompare(cell, "implicitWidth", pc.mark.width + Theme.gap(10) + pc.word.implicitWidth)
            compare(pc.word.x - (pc.mark.x + pc.mark.width), Theme.gap(10), "Cell gap")
            icon.destroy(); dense.destroy(); cell.destroy()
        }

        // §5 / §7.5: the technical layer's basis string after " · ", data-sm in colorText2; never on
        // the golfer layer, and nothing when there is none.
        function test_layers() {
            const g = probe.bdg({ grade: 4, basisText: "σ 2.1° · 0.42 of corridor · n 24" })
            verify(!parts(g).basis.visible, "golfer layer: no basis")
            compare(g.line, ConsistencyVocabulary.line(4))
            const t = probe.bdg({ grade: 4, contentLayer: "technical", basisText: "σ 2.1° · 0.42 of corridor · n 24" })
            const b = parts(t).basis, sep = probe.findNamed(t, "variabilityBasisSeparator")
            verify(b.visible)
            verify(sep.visible)
            compare(sep.text, " · ")
            compare(b.text, "σ 2.1° · 0.42 of corridor · n 24")
            compare(sep.font.family, Theme.fontData)
            verify(Qt.colorEqual(sep.color, Theme.colorText2))
            compare(b.font.family, Theme.fontData)
            compare(b.font.pixelSize, Theme.fontSzDataSm)
            verify(Qt.colorEqual(b.color, Theme.colorText2))
            compare(parts(t).word.text, "Repeatable")
            tryCompare(t, "implicitWidth", parts(t).mark.width + Theme.gap(6) + parts(t).word.implicitWidth + sep.implicitWidth + b.implicitWidth)
            const e = probe.bdg({ grade: 4, contentLayer: "technical" })
            verify(!parts(e).basis.visible, "no basis string, nothing after the word")
            verify(!probe.findNamed(e, "variabilityBasisSeparator").visible, "nor the separator")
            g.destroy(); t.destroy(); e.destroy()
        }

        // §8.6: golfer layer → the grade-0 mark and "Checking"; technical → the real grade.
        function test_measurementLimited() {
            for (const f of probe.forms) {
                const g = probe.bdg({ grade: 4, form: f, measurementLimited: true }), pg = parts(g)
                verify(g.substituted)
                compare(pg.mark.grade, 0)
                compare(pg.word.text, MeasurementVocabulary.word(0))
                compare(pg.word.text, "Checking")
                verify(Qt.colorEqual(pg.word.color, Theme.colorText3))
                compare(pg.mark.Accessible.name, "Measurement: Checking")
                compare(pg.mark.variant, PpVariabilityMark.Dispersion, "still the Dispersion shape")
                for (const p of probe.paths(pg.mark))
                    if (!probe.isNeutral(p)) verify(!probe.seen(p.fillColor) && !probe.seen(p.strokeColor), "grade-0 mark: no tone")
                compare(g.line, MeasurementVocabulary.line(0))

                const t = probe.bdg({ grade: 4, form: f, contentLayer: "technical", measurementLimited: true,
                                      basisText: "σ 2.1° · n 24 · measurement-limited" }), pt = parts(t)
                verify(!t.substituted)
                compare(pt.mark.grade, 4)
                compare(pt.word.text, "Repeatable")
                verify(Qt.colorEqual(pt.word.color, Theme.colorGood))
                compare(pt.basis.text, "σ 2.1° · n 24 · measurement-limited")
                g.destroy(); t.destroy()
            }
        }

        // The badge's tone is an input like the mark's: the grade's by default, overridable.
        function test_toneOverride() {
            const b = probe.bdg({ grade: 4 })
            verify(Qt.colorEqual(b.tone, Theme.colorGood))
            b.tone = Theme.colorAccent
            const p = parts(b)
            verify(Qt.colorEqual(p.mark.tone, Theme.colorAccent), "the mark wears it")
            tryVerify(() => Qt.colorEqual(p.word.color, Theme.colorAccent), Theme.durationSlow + 1000, "so does the word")
            b.destroy()
        }

        // Screen readers hear the mark's name once: the drawn words are ignored.
        function test_wordsNotReadTwice() {
            const b = probe.bdg({ grade: 4, contentLayer: "technical", basisText: "n 24" }), p = parts(b)
            verify(p.word.Accessible.ignored)
            verify(p.basis.Accessible.ignored)
            verify(probe.findNamed(b, "variabilityBasisSeparator").Accessible.ignored)
            compare(p.mark.Accessible.name, "Consistency: Repeatable (4 of 5)")
            b.destroy()
        }

        // §9.5.4 with roles: the page's sizes, gaps, type and inks.
        function test_printWithRoles() {
            const ts = tsComp.createObject(probe)
            const icon = probe.bdg({ grade: 4, roles: ts, contentLayer: "technical", basisText: "n 24" }), pi = parts(icon)
            compare(pi.mark.size, ts.u(Theme.variabilityIconSize))
            compare(pi.word.font.family, ts.fBody.family)
            compare(pi.word.font.pixelSize, ts.fBody.pixelSize)
            verify(Qt.colorEqual(pi.word.color, ts.good))
            compare(pi.basis.font.family, ts.fFig.family)
            verify(Qt.colorEqual(pi.basis.color, ts.ink2))
            tryCompare(icon, "implicitWidth", pi.mark.width + ts.g(6) + pi.word.implicitWidth
                       + probe.findNamed(icon, "variabilityBasisSeparator").implicitWidth + pi.basis.implicitWidth)
            const dense = probe.bdg({ grade: 4, roles: ts, dense: true })
            compare(parts(dense).mark.size, ts.u(16))
            const cell = probe.bdg({ grade: 0, roles: ts, form: PpVariabilityMark.Cell }), pc = parts(cell)
            compare(pc.mark.size, ts.u(Theme.variabilityCellH))
            verify(Qt.colorEqual(pc.word.color, ts.ink3), "grade 0 in the page's ink3")
            tryCompare(cell, "implicitWidth", pc.mark.width + ts.g(10) + pc.word.implicitWidth)
            icon.destroy(); dense.destroy(); cell.destroy(); ts.destroy()
        }
    }
}
