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
import QtQuick.Shapes
import QtTest
import PinPointStudio

// The typicality variants of PpVariabilityMark and their badge (docs/design/variability-mark/spec.md
// §6.2, §7.4, §8.7, §9.5.4): the ring follows z and clamps with a chevron, the ghost dots and their
// deterministic jitter, the trace's envelope, the corridor only with showCorridor, the accent and no
// status colour, the words from the band, TypicalityTrace's missing Icon, every theme silent, and print
// with a PpTypeset roles. The shapes' numbers are the provider's (variability_data_geometry_test); here
// they are checked as DRAWN, in pixels.
Item {
    id: probe
    width: 600; height: 200

    Component { id: markComp; PpVariabilityMark {} }
    Component { id: badgeComp; PpVariabilityBadge {} }
    Component { id: tsComp; PpTypeset { medium: "print"; fixedAesthetic: "folio" } }

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
    function one(mark, name) { return named(mark, name)[0] }
    function seen(c) { return c.a > 0.001 }
    function drawn(p) { return seen(p.fillColor) || (p.strokeWidth > 0 && seen(p.strokeColor)) }
    function sameRgb(a, b) { return Qt.colorEqual(Qt.alpha(a, 1), Qt.alpha(b, 1)) }
    function sameColor(a, b) { return Math.abs(a.r - b.r) < 0.004 && Math.abs(a.g - b.g) < 0.004 && Math.abs(a.b - b.b) < 0.004 && Math.abs(a.a - b.a) < 0.004 }
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, c.a * a) }
    // A wash's alpha on screen in the current theme (§3): × variabilityWashAlpha, clamped to 1.
    function wash(a) { return Math.min(1, a * Theme.variabilityWashAlpha) }
    function themeName() { return Theme.aesthetic + (Theme.dark ? " dark" : " light") }
    function mk(props) { return markComp.createObject(probe, props) }
    function bdg(props) { return badgeComp.createObject(probe, props) }

    readonly property int typ: PpVariabilityMark.Typicality
    readonly property int trace: PpVariabilityMark.TypicalityTrace
    readonly property int icon: PpVariabilityMark.Icon
    readonly property int cell: PpVariabilityMark.Cell

    // FNV-1a 32-bit over an ASCII id (its UTF-8 bytes are its char codes), finished with murmur3's
    // fmix32 avalanche, mapped to [−4, 4): the provider's documented ghost jitter
    // (variability_geometry.h, ghostJitter), spelled independently. Math.imul for the 32-bit
    // multiplies, >>> 0 to keep every step unsigned.
    function jitter(id) {
        let h = 2166136261
        for (let i = 0; i < id.length; ++i) {
            h = (h ^ id.charCodeAt(i)) >>> 0
            h = Math.imul(h, 16777619) >>> 0
        }
        h = (h ^ (h >>> 16)) >>> 0
        h = Math.imul(h, 0x85ebca6b) >>> 0
        h = (h ^ (h >>> 13)) >>> 0
        h = Math.imul(h, 0xc2b2ae35) >>> 0
        h = (h ^ (h >>> 16)) >>> 0
        return (h / 4294967296 * 2 - 1) * 4
    }
    function ghosts(n) {
        const out = []
        for (let i = 0; i < n; ++i) out.push({ swingId: "swing_" + String(i + 1).padStart(4, "0"), z: -2 + 4 * i / Math.max(1, n - 1) })
        return out
    }
    // A synthetic 101-point window: a hump on a positive base, this swing a little above it.
    function curves() {
        const mean = [], sd = [], swing = []
        for (let i = 0; i <= 100; ++i) {
            const m = 2 + 10 * Math.sin(Math.PI * i / 100), s = 1 + 0.5 * Math.sin(Math.PI * i / 50) ** 2
            mean.push(m); sd.push(s); swing.push(m + 1.5 * s)
        }
        return { mean: mean, sd: sd, swing: swing }
    }
    function traceValues(band, corridor) {
        const c = curves()
        return { mean: c.mean, sd: c.sd, swing: c.swing, corridor: corridor === undefined ? null : corridor, band: band }
    }
    function typValues(z, band, corridor) {
        return { z: z, ghosts: ghosts(6), corridor: corridor === undefined ? null : corridor, band: band }
    }

    TestCase {
        name: "PpVariabilityTypicality"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }
        function cleanup() { Theme.themeIndex = was }

        // §6.2 Cell: x = 40 + z × 14, ring r 3, 1.75 px whatever the size; |z| > 2.6 clamps at x 4 / 76
        // and adds an outward chevron. Icon: x = 12 + clamp(z, ±2.2) × 5.25, r 2.6, no chevron.
        function test_ringFollowsZ() {
            for (const s of [24, 48]) {
                const k = s / 24
                for (const c of [[0, 40, false], [1.5, 61, false], [-1, 26, false], [2.5, 75, false],
                                 [3.5, 76, true], [-3.5, 4, true], [Infinity, 76, true]]) {
                    const m = probe.mk({ variant: probe.typ, form: probe.cell, size: s, values: probe.typValues(c[0], "usual") })
                    const ring = probe.one(m, "variabilityRing"), arc = ring.pathElements[0]
                    fuzzyCompare(arc.centerX, c[1] * k, 1e-9, "ring x at z " + c[0] + ", " + s + " px")
                    fuzzyCompare(arc.centerY, 12 * k, 1e-9)
                    fuzzyCompare(arc.radiusX, 3 * k, 1e-9, "ring r × k")
                    compare(ring.strokeWidth, 1.75, "the ring's stroke stays 1.75 px at " + s)
                    compare(m.scale, 1, "never scaled")
                    const centre = probe.one(m, "variabilityCentre").pathElements[0]
                    fuzzyCompare(centre.centerX, c[1] * k, 1e-9, "the centre dot sits in the ring")
                    const chev = probe.one(m, "variabilityChevron")
                    compare(probe.drawn(chev), c[2], "chevron at z " + c[0])
                    compare(m._typGeom.chevron !== null, c[2])
                    if (c[2]) {
                        compare(chev.strokeWidth, 1.5, "chevron 1.5 px")
                        compare(m._typGeom.side, c[0] < 0 ? -1 : 1, "outward")
                        // Outward: the tip is the chevron's outermost point.
                        const p = m._typGeom.chevron.points
                        verify(c[0] < 0 ? p[2] < p[0] : p[2] > p[0], "the tip points out")
                    }
                    m.destroy()
                }
            }
            for (const c of [[0, 12], [1, 17.25], [-1, 6.75], [3, 23.55], [-3, 0.45]]) {
                const m = probe.mk({ variant: probe.typ, form: probe.icon, size: 24, values: probe.typValues(c[0], "usual") })
                const ring = probe.one(m, "variabilityRing")
                fuzzyCompare(ring.pathElements[0].centerX, c[1], 1e-9, "Icon ring x at z " + c[0])
                fuzzyCompare(ring.pathElements[0].radiusX, 2.6, 1e-9)
                compare(probe.named(m, "variabilityChevron").length, 0, "the Icon has no chevron")
                compare(probe.named(m, "variabilityGhost").length, 0, "nor ghosts")
                m.destroy()
            }
            // No z: the zones, no ring.
            const none = probe.mk({ variant: probe.typ, form: probe.cell, values: { ghosts: probe.ghosts(3), band: "" } })
            verify(!probe.drawn(probe.one(none, "variabilityRing")), "no z, no ring")
            verify(probe.drawn(probe.one(none, "variabilityZoneOuter")), "the zones still draw")
            none.destroy()
        }

        // §6.2: up to 12 ghosts at x 40 + zᵢ × 14 (clamped at the stops), r 1.5, colorText3 at 70 %; the
        // y is the deterministic jitter from the swing id, the same on every build of the mark.
        function test_ghosts() {
            const list = probe.ghosts(14)
            list[13] = { swingId: "swing_far", z: 4 }
            const m = probe.mk({ variant: probe.typ, form: probe.cell, size: 48, values: { z: 0, ghosts: list, band: "usual" } })
            const gs = probe.named(m, "variabilityGhost")
            compare(gs.length, 12, "twelve slots")
            const k = 2, kept = list.slice(2)   // the latest 12
            for (let i = 0; i < 12; ++i) {
                verify(probe.drawn(gs[i]), "ghost " + i + " drawn")
                const a = gs[i].pathElements[0], z = kept[i].z
                fuzzyCompare(a.centerX, (Math.abs(z) > 2.6 ? 76 : 40 + z * 14) * k, 1e-9, "ghost " + i + " x")
                fuzzyCompare(a.centerY, (12 + probe.jitter(kept[i].swingId)) * k, 1e-6, "ghost " + i + " y = 12 + jitter(id)")
                verify(a.centerY >= 8 * k && a.centerY < 16 * k, "within ±4 units")
                fuzzyCompare(a.radiusX, 1.5 * k, 1e-9)
                verify(probe.sameColor(gs[i].fillColor, probe.withAlpha(Theme.colorText3, 0.70)), "colorText3 at 70 %")
            }
            // Deterministic: another mark, the same ids at other z, gives the same y.
            const moved = kept.map(g => ({ swingId: g.swingId, z: g.z / 2 }))
            const m2 = probe.mk({ variant: probe.typ, form: probe.cell, size: 48, values: { z: 1, ghosts: moved, band: "usual" } })
            const gs2 = probe.named(m2, "variabilityGhost")
            for (let i = 0; i < 12; ++i)
                compare(gs2[i].pathElements[0].centerY, gs[i].pathElements[0].centerY, "ghost " + i + ": same id, same y")
            // Fewer ghosts: the spare slots draw nothing.
            const few = probe.mk({ variant: probe.typ, form: probe.cell, values: { z: 0, ghosts: probe.ghosts(3), band: "usual" } })
            compare(probe.named(few, "variabilityGhost").filter(p => probe.drawn(p)).length, 3)
            // A ghost's dot sits under the ring (paint order: ghosts, ring, centre, chevron).
            const order = probe.paths(m).map(p => p.objectName)
            verify(order.lastIndexOf("variabilityGhost") < order.indexOf("variabilityRing"), "ghosts beneath the ring")
            verify(order.indexOf("variabilityCorridor") < order.indexOf("variabilityZoneOuter"), "the corridor first")
            m.destroy(); m2.destroy(); few.destroy()
        }

        // §6.2 trace: envelope colorText at 9 % (× variabilityWashAlpha on screen, §3), mean 1 px dashed colorText3, this swing 1.75 px accent,
        // round cap and join, over a colorBorderStrong baseline. Bad inputs: the baseline alone.
        function test_envelope() {
            const m = probe.mk({ variant: probe.trace, form: probe.cell, values: probe.traceValues("usual") })
            const env = probe.one(m, "variabilityEnvelope"), mean = probe.one(m, "variabilityMean")
            const swing = probe.one(m, "variabilitySwing"), base = probe.one(m, "variabilityLine")
            verify(m._typGeom.valid, "the provider took the 101-point curves (copied from a QVariantList)")
            verify(probe.drawn(env), "envelope drawn")
            verify(probe.sameColor(env.fillColor, probe.withAlpha(Theme.colorText, probe.wash(0.09))), "colorText at 9 % × wash")
            compare(m._typGeom.envelope.points.length, 2 * (2 * 101 + 1), "mean + SD out, mean − SD back, closed")
            compare(mean.strokeWidth, 1)
            compare(mean.strokeStyle, ShapePath.DashLine, "the mean is dashed")
            compare(mean.dashPattern.length, 2)
            compare(mean.dashPattern[0], m._typGeom.dashPattern[0], "the provider's dash")
            compare(mean.dashPattern[1], m._typGeom.dashPattern[1])
            verify(probe.sameColor(mean.strokeColor, Theme.colorText3), "mean colorText3")
            compare(swing.strokeWidth, 1.75)
            compare(swing.capStyle, ShapePath.RoundCap)
            compare(swing.joinStyle, ShapePath.RoundJoin)
            verify(probe.sameRgb(swing.strokeColor, Theme.colorAccent), "this swing in the accent")
            verify(probe.sameColor(base.strokeColor, Theme.colorBorderStrong), "baseline colorBorderStrong")
            compare(m.implicitWidth, m.size * 80 / 24, "a Cell")
            const bad = probe.mk({ variant: probe.trace, form: probe.cell, values: { mean: [1, 2], sd: [1], swing: [], band: "" } })
            verify(!bad._typGeom.valid)
            verify(!probe.drawn(probe.one(bad, "variabilityEnvelope")), "no envelope from bad inputs")
            verify(!probe.drawn(probe.one(bad, "variabilitySwing")))
            verify(probe.drawn(probe.one(bad, "variabilityLine")), "the baseline stays")
            m.destroy(); bad.destroy()
        }

        // §6.2 showCorridor: the 1.5 px colorGood bracket, only when asked for, on both variants.
        function test_corridorOnlyWithShowCorridor() {
            const cases = [[probe.typ, probe.cell, probe.typValues(0.5, "usual", { loZ: -1, hiZ: 0.5 }), 26, 47],
                           [probe.typ, probe.icon, probe.typValues(0.5, "usual", { loZ: -1, hiZ: 0.5 }), 6.75, 14.625],
                           [probe.trace, probe.cell, probe.traceValues("usual", { loT: 0.6, hiT: 0.8 }), 47.6, 62.8]]
            for (const c of cases) {
                const m = probe.mk({ variant: c[0], form: c[1], size: 24, values: c[2] })
                const br = probe.one(m, "variabilityCorridor")
                compare(m._typGeom.corridor, null, "off by default: the provider is given no corridor")
                verify(!probe.drawn(br), "no bracket without showCorridor")
                m.showCorridor = true
                verify(probe.drawn(br), "the bracket with showCorridor")
                compare(br.strokeWidth, 1.5)
                verify(probe.sameColor(br.strokeColor, Theme.colorGood), "colorGood")
                fuzzyCompare(m._typGeom.corridor.x1, c[3], 1e-9, "lo end")
                fuzzyCompare(m._typGeom.corridor.x2, c[4], 1e-9, "hi end")
                m.showCorridor = false
                verify(!probe.drawn(br), "off again")
                m.destroy()
            }
            // showCorridor with no corridor in the values: nothing.
            const n = probe.mk({ variant: probe.typ, form: probe.cell, showCorridor: true, values: probe.typValues(0, "usual") })
            verify(!probe.drawn(probe.one(n, "variabilityCorridor")))
            n.destroy()
        }

        // §3, §6.2: accent, never a status colour, in every theme: every drawn element is the accent,
        // a neutral, an ink or the surface; with showCorridor the bracket is the one colorGood element.
        function test_accentNeverStatus() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const status = [Theme.colorGood, Theme.colorAttention, Theme.colorWarn, Theme.colorError]
                    .filter(c => !probe.sameRgb(c, Theme.colorAccent))   // a theme whose accent is a status hue: the tone checks still hold
                const marks = [probe.mk({ variant: probe.typ, form: probe.cell, values: probe.typValues(3.5, "unusual", { loZ: -1, hiZ: 1 }) }),
                               probe.mk({ variant: probe.typ, form: probe.icon, values: probe.typValues(1.5, "aLittleOff", { loZ: -1, hiZ: 1 }) }),
                               probe.mk({ variant: probe.trace, form: probe.cell, values: probe.traceValues("usual", { loT: 0.2, hiT: 0.4 }) })]
                for (const m of marks) {
                    verify(Qt.colorEqual(m.tone, Theme.colorAccent), probe.themeName() + ": the tone is colorAccent")
                    for (const show of [false, true]) {
                        m.showCorridor = show
                        for (const p of probe.paths(m)) {
                            if (!probe.drawn(p)) continue
                            const isCorridor = p.objectName === "variabilityCorridor"
                            for (const c of [p.strokeWidth > 0 ? p.strokeColor : null, p.fillColor]) {
                                if (!c || !probe.seen(c)) continue
                                if (isCorridor) {
                                    verify(show, "the corridor draws only with showCorridor")
                                    verify(probe.sameRgb(c, Theme.colorGood), probe.themeName() + ": the bracket is colorGood")
                                    continue
                                }
                                for (const s of status)
                                    verify(!probe.sameRgb(c, s), probe.themeName() + ": " + p.objectName + " wears a status colour " + c)
                            }
                        }
                    }
                    // The ring: accent stroke, opaque colorSurface fill.
                    const ring = probe.one(m, "variabilityRing")
                    if (ring) {
                        verify(probe.sameRgb(ring.strokeColor, Theme.colorAccent), probe.themeName() + ": ring stroke accent")
                        verify(probe.sameColor(ring.fillColor, Qt.alpha(Theme.colorSurface, 1)) || probe.sameColor(ring.fillColor, Theme.colorSurface),
                               probe.themeName() + ": ring fill colorSurface")
                    }
                    m.destroy()
                }
            }
        }

        // §7.4 words from the band: the label, "Typicality: <word>" (no grade), the badge's word in the
        // accent, its tooltip line, and its basis on the technical layer.
        function test_wordsFromTheBand() {
            const words = { usual: "Your usual", aLittleOff: "A little off", unusual: "Unusual" }
            for (const band of ["usual", "aLittleOff", "unusual"]) {
                for (const v of [probe.typ, probe.trace]) {
                    const vals = v === probe.typ ? probe.typValues(0.3, band) : probe.traceValues(band)
                    const m = probe.mk({ variant: v, form: probe.cell, values: vals, grade: 4 })
                    compare(m.label, words[band])
                    compare(m.label, TypicalityVocabulary.word(band))
                    compare(m.Accessible.name, "Typicality: " + words[band], "never '(n of 5)'")
                    compare(m.Accessible.role, Accessible.Graphic)
                    verify(m.vocabulary === TypicalityVocabulary)
                    m.destroy()

                    const b = probe.bdg({ variant: v, form: probe.cell, values: vals, contentLayer: "golfer" })
                    const w = probe.findNamed(b, "variabilityWord")
                    compare(w.text, words[band])
                    compare(b.word, words[band])
                    verify(Qt.colorEqual(w.color, Theme.colorAccent), "the word in the accent, never colorText3 (no grade)")
                    compare(b.line, TypicalityVocabulary.line(band), "the tooltip is the band's line")
                    verify(b.line !== "")
                    compare(probe.findNamed(b, "variabilityMark").Accessible.name, "Typicality: " + words[band])
                    b.destroy()
                }
            }
            // The technical layer: the word, then the caller's basis; measurement-limited never applies.
            const t = probe.bdg({ variant: probe.typ, values: probe.typValues(1.2, "aLittleOff"), contentLayer: "technical",
                                  basisText: "this swing 5.2 cm · usual 4.8 cm", measurementLimited: true })
            verify(!t.substituted, "§8.6 is not a typicality rule")
            compare(probe.findNamed(t, "variabilityWord").text, "A little off")
            const basis = probe.findNamed(t, "variabilityBasis")
            verify(basis.visible)
            compare(basis.text, "this swing 5.2 cm · usual 4.8 cm")
            verify(Qt.colorEqual(basis.color, Theme.colorText2))
            const g = probe.bdg({ variant: probe.typ, values: probe.typValues(1.2, "aLittleOff"), measurementLimited: true })
            verify(!g.substituted)
            compare(g.word, "A little off", "golfer layer, measurement-limited: still the band's word")
            // A band change re-words the mark; no band, no word, and the domain alone as the name.
            const m = probe.mk({ variant: probe.typ, values: probe.typValues(0, "usual") })
            m.values = probe.typValues(2.4, "unusual")
            compare(m.label, "Unusual")
            m.values = probe.typValues(2.4, "")
            compare(m.label, "")
            compare(m.Accessible.name, "Typicality")
            ignoreWarning(/TypicalityVocabulary: band normal is not usual \| aLittleOff \| unusual; no word shown/)
            m.values = probe.typValues(0, "normal")
            compare(m.label, "")
            t.destroy(); g.destroy(); m.destroy()
        }

        // §6.2 "No icon form": a TypicalityTrace asked for an Icon warns and draws the Cell; its badge
        // is laid out as a Cell badge.
        function test_traceIconWarns() {
            ignoreWarning(/PpVariabilityMark: TypicalityTrace has no Icon form \(spec §6\.2\); drawn as a Cell/)
            const m = probe.mk({ variant: probe.trace, form: probe.icon, size: 24, values: probe.traceValues("usual") })
            verify(m._cell, "drawn as a Cell")
            compare(m.implicitWidth, 80)
            compare(m.implicitHeight, 24)
            verify(probe.drawn(probe.one(m, "variabilitySwing")), "the trace is drawn")
            verify(probe.drawn(probe.one(m, "variabilityEnvelope")))
            m.destroy()
            // A change of form says it too.
            const c = probe.mk({ variant: probe.trace, form: probe.cell, values: probe.traceValues("usual") })
            ignoreWarning(/PpVariabilityMark: TypicalityTrace has no Icon form/)
            c.form = probe.icon
            verify(c._cell)
            c.destroy()
            ignoreWarning(/PpVariabilityMark: TypicalityTrace has no Icon form/)
            const b = probe.bdg({ variant: probe.trace, form: probe.icon, values: probe.traceValues("usual") })
            const bm = probe.findNamed(b, "variabilityMark")
            compare(bm.size, Theme.sp(Theme.variabilityCellH), "the badge sizes it as a Cell")
            tryCompare(b, "implicitWidth", bm.width + Theme.gap(10) + probe.findNamed(b, "variabilityWord").implicitWidth)
            b.destroy()
            // Typicality's Icon is a real form: no warning.
            failOnWarning(/TypicalityTrace has no Icon/)
            const ok = probe.mk({ variant: probe.typ, form: probe.icon, values: probe.typValues(1, "usual") })
            verify(!ok._cell)
            ok.destroy()
        }

        // Every theme, both variants and forms (the trace a Cell), both corridor settings, the badges on
        // both layers: no warning at all.
        function test_everyThemeIsSilent() {
            const warm = probe.bdg({ variant: probe.typ, values: probe.typValues(0, "usual"), contentLayer: "technical", basisText: "n 1" })
            wait(0)
            warm.destroy()
            failOnWarning(/.*/)
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const made = []
                for (const show of [false, true])
                    for (const z of [0, 1.5, 2.5, 3.5, -3.5, NaN]) {
                        const band = isNaN(z) ? "" : Math.abs(z) <= 1 ? "usual" : Math.abs(z) <= 2 ? "aLittleOff" : "unusual"
                        for (const f of [probe.icon, probe.cell])
                            made.push(probe.mk({ variant: probe.typ, form: f, showCorridor: show,
                                                 values: probe.typValues(z, band, { loZ: -1.5, hiZ: 3 }) }))
                        made.push(probe.mk({ variant: probe.trace, form: probe.cell, showCorridor: show,
                                             values: probe.traceValues(band, { loT: 0.3, hiT: 0.5 }) }))
                    }
                made.push(probe.mk({ variant: probe.typ }))                       // no values at all
                made.push(probe.mk({ variant: probe.trace, form: probe.cell }))
                for (const layer of ["golfer", "technical"])
                    for (const f of [probe.icon, probe.cell]) {
                        made.push(probe.bdg({ variant: probe.typ, form: f, contentLayer: layer, values: probe.typValues(1.2, "aLittleOff"),
                                              basisText: "this swing 5.2 cm · usual 4.8 cm" }))
                        made.push(probe.bdg({ variant: probe.trace, form: probe.cell, contentLayer: layer, values: probe.traceValues("unusual"),
                                              basisText: "peak 45 ms late, higher" }))
                    }
                for (const m of made)
                    verify(probe.paths(probe.findNamed(m, "variabilityMark") || m).length > 0, probe.themeName() + ": draws")
                wait(0)   // a frame, so the shapes are polished and drawn under this theme
                for (const m of made) m.destroy()
            }
        }

        // §3, §10 (Mark, 7 Nov 2026): on screen the inner zone and the trace's envelope are washes,
        // × variabilityWashAlpha (1 light, 3 dark), clamped to 1. The dashed outer zone, the ghost dots,
        // the mean and the ring are not; print with roles never reads it.
        function test_washAlphaByTheme() {
            const ts = tsComp.createObject(probe), kp = Theme.variabilityPrintAlphaScale
            for (let t = 0; t < Theme.themeCount; t += 2) {
                let light = null
                for (const dark of [false, true]) {
                    Theme.themeIndex = t + (dark ? 1 : 0)
                    compare(Theme.variabilityWashAlpha, dark ? 3 : 1, probe.themeName())
                    const m = probe.mk({ variant: probe.typ, form: probe.cell, values: probe.typValues(0.5, "usual") })
                    const ic = probe.mk({ variant: probe.typ, form: probe.icon, values: probe.typValues(0.5, "usual") })
                    const tr = probe.mk({ variant: probe.trace, form: probe.cell, values: probe.traceValues("usual") })
                    // Each alpha as a share of its role's own ink, so a translucent token compares across modes.
                    const a = {
                        inner: probe.one(m, "variabilityZoneInner").fillColor.a / Theme.colorText.a,
                        innerIcon: probe.one(ic, "variabilityZoneInner").fillColor.a / Theme.colorText.a,
                        envelope: probe.one(tr, "variabilityEnvelope").fillColor.a / Theme.colorText.a,
                        outer: probe.one(m, "variabilityZoneOuter").strokeColor.a / Theme.colorBorderStrong.a,
                        ghost: probe.named(m, "variabilityGhost")[0].fillColor.a / Theme.colorText3.a,
                        mean: probe.one(tr, "variabilityMean").strokeColor.a / Theme.colorText3.a,
                        ring: probe.one(m, "variabilityRing").strokeColor.a / Theme.colorAccent.a
                    }
                    if (!dark) {
                        fuzzyCompare(a.inner, 0.07, 0.005, probe.themeName() + ": light inner zone at the spec's 7 %")
                        fuzzyCompare(a.envelope, 0.09, 0.005, probe.themeName() + ": light envelope at the spec's 9 %")
                        light = a
                    } else {
                        for (const k of ["inner", "innerIcon", "envelope"])
                            fuzzyCompare(a[k], Math.min(1, 3 * light[k]), 0.005, probe.themeName() + ": " + k + " × 3")
                        for (const k of ["outer", "ghost", "mean", "ring"])
                            fuzzyCompare(a[k], light[k], 0.005, probe.themeName() + ": " + k + " is not a wash")
                        // Print with roles: the print scale alone, whatever the screen's theme.
                        m.roles = ts; tr.roles = ts
                        verify(probe.sameColor(probe.one(m, "variabilityZoneInner").fillColor, probe.withAlpha(ts.ink, Math.min(1, 0.07 * kp))),
                               probe.themeName() + ": print inner zone")
                        verify(probe.sameColor(probe.one(tr, "variabilityEnvelope").fillColor, probe.withAlpha(ts.ink, Math.min(1, 0.09 * kp))),
                               probe.themeName() + ": print envelope")
                    }
                    m.destroy(); ic.destroy(); tr.destroy()
                }
            }
            ts.destroy()
        }

        // §9.5.4 print with a PpTypeset roles: the page's inks for every role (accent, paper, ink, ink3,
        // baseline, good), fills × the print scale, strokes unchanged; the badge's word in roles.accent.
        function test_printWithRoles() {
            const ts = tsComp.createObject(probe), k = Theme.variabilityPrintAlphaScale
            const m = probe.mk({ variant: probe.typ, form: probe.cell, roles: ts, showCorridor: true,
                                 values: probe.typValues(3.5, "unusual", { loZ: -1, hiZ: 1 }) })
            compare(m.medium, "print")
            compare(m.size, ts.u(Theme.variabilityIconSize), "size from roles.u()")
            verify(Qt.colorEqual(m.tone, ts.accent), "the tone is roles.accent")
            const ring = probe.one(m, "variabilityRing")
            verify(probe.sameRgb(ring.strokeColor, ts.accent), "ring stroke roles.accent")
            verify(probe.sameColor(ring.fillColor, ts.paper), "ring fill roles.paper")
            compare(ring.strokeWidth, 1.75, "strokes unchanged")
            verify(probe.sameColor(probe.one(m, "variabilityZoneInner").fillColor, probe.withAlpha(ts.ink, Math.min(1, 0.07 * k))),
                   "inner zone roles.ink, × the print scale")
            verify(probe.sameColor(probe.one(m, "variabilityZoneOuter").strokeColor, Qt.alpha(ts.baseline, ts.baseline.a * ts.baselineOpacity)),
                   "outer zone the page's baseline")
            verify(probe.sameColor(probe.named(m, "variabilityGhost")[0].fillColor, probe.withAlpha(ts.ink3, Math.min(1, 0.70 * k))),
                   "ghosts roles.ink3, × the print scale")
            verify(probe.sameColor(probe.one(m, "variabilityCorridor").strokeColor, ts.good), "corridor roles.good")
            verify(probe.sameRgb(probe.one(m, "variabilityChevron").strokeColor, ts.accent), "chevron roles.accent")

            const tr = probe.mk({ variant: probe.trace, form: probe.cell, roles: ts, values: probe.traceValues("usual") })
            verify(probe.sameColor(probe.one(tr, "variabilityEnvelope").fillColor, probe.withAlpha(ts.ink, Math.min(1, 0.09 * k))),
                   "envelope roles.ink, × the print scale")
            verify(probe.sameColor(probe.one(tr, "variabilityMean").strokeColor, ts.ink3), "mean roles.ink3")
            verify(probe.sameRgb(probe.one(tr, "variabilitySwing").strokeColor, ts.accent), "swing roles.accent")

            const b = probe.bdg({ variant: probe.typ, roles: ts, values: probe.typValues(0.2, "usual"), contentLayer: "technical",
                                  basisText: "this swing 5.2 cm · usual 4.8 cm" })
            const w = probe.findNamed(b, "variabilityWord")
            verify(Qt.colorEqual(w.color, ts.accent), "the badge's word in roles.accent")
            compare(w.font.family, ts.fBody.family)
            compare(probe.findNamed(b, "variabilityBasis").font.family, ts.fFig.family)

            // Print without roles: Theme's inks, fills lifted.
            const p = probe.mk({ variant: probe.typ, form: probe.cell, medium: "print", values: probe.typValues(0, "usual") })
            verify(probe.sameColor(probe.one(p, "variabilityZoneInner").fillColor, probe.withAlpha(Theme.colorText, Math.min(1, 0.07 * k))))
            verify(probe.sameColor(probe.one(p, "variabilityRing").fillColor, Theme.colorSurface))
            // Roles on screen: Theme again.
            ts.medium = "screen"
            verify(Qt.colorEqual(m.tone, Theme.colorAccent))
            verify(probe.sameColor(probe.one(m, "variabilityRing").fillColor, Theme.colorSurface))
            m.destroy(); tr.destroy(); b.destroy(); p.destroy(); ts.destroy()
        }
    }
}
