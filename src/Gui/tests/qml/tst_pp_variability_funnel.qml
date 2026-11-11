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

// The spread funnel (docs/design/variability-mark/spec.md §6.1): the band for 3+ sessions in the latest
// unbroken run and none (the badge plus "Trend after 3 sessions") otherwise, the band in the CURRENT
// grade's tone at 16 % with its edge at 55 %, the current session's marker, the label and accessible
// name, the 96 px minimum width, the print uplift, and every case building silently under all 14 themes.
Item {
    id: probe
    width: 800; height: 300

    Component { id: funnelComp; PpVariabilityFunnel {} }
    Component { id: tsComp; PpTypeset { medium: "print"; fixedAesthetic: "folio" } }

    function fn(props) { return funnelComp.createObject(probe, props) }
    // n sessions, oldest first: grades from `grades` (the last one is the current), ρ falling.
    function run(grades, breakAt) {
        const out = []
        for (let i = 0; i < grades.length; ++i)
            out.push({ rho: 0.9 - 0.08 * i, grade: grades[i], breakBefore: i === breakAt })
        return out
    }
    function findNamed(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const f = findNamed(item.children[i], name)
            if (f) return f
        }
        return null
    }
    // Every ShapePath under `item` named `name` (they live in each Shape's `data`, not its children).
    function paths(item, name, out) {
        out = out || []
        if (!item) return out
        if (item.data)
            for (let i = 0; i < item.data.length; ++i) {
                const d = item.data[i]
                if (d && d.objectName === name && d.pathElements !== undefined) out.push(d)
            }
        for (let i = 0; i < item.children.length; ++i) paths(item.children[i], name, out)
        return out
    }
    function toneFor(g) {
        return g >= 4 ? Theme.colorGood : g === 3 ? Theme.colorAttention : g >= 1 ? Theme.colorWarn : Theme.colorText3
    }
    function sameRgb(a, b) { return Qt.colorEqual(Qt.alpha(a, 1), Qt.alpha(b, 1)) }
    function sameColor(a, b) {
        return Math.abs(a.r - b.r) < 0.004 && Math.abs(a.g - b.g) < 0.004 && Math.abs(a.b - b.b) < 0.004 && Math.abs(a.a - b.a) < 0.004
    }
    function themeName() { return Theme.aesthetic + (Theme.dark ? " dark" : " light") }
    // The band fill's alpha on screen in the current theme (§3): × variabilityWashAlpha, then held at or
    // under variabilityBandFillMax (R35, Mark 7 Nov 2026, 10709b0d: at ×3 the dark 16 % fill reached 48 %
    // and the 55 % edge barely showed, so dark themes draw it at 32 %; light themes' 16 % never reach it).
    function wash(a) { return Math.min(1, Theme.variabilityBandFillMax, a * Theme.variabilityWashAlpha) }

    TestCase {
        name: "PpVariabilityFunnel"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }
        function cleanup() { Theme.themeIndex = was }

        // §6.1: a band once the latest unbroken run holds 3 sessions; below that no band, and the
        // grade's badge plus "Trend after 3 sessions" in colorText3.
        function test_bandOnlyFromThreeSessions() {
            const three = probe.fn({ sessions: probe.run([4, 4, 4]), trend: "holding" })
            wait(0)
            verify(!three.trendAfter3, "3 sessions: a trend")
            compare(probe.paths(three, "funnelBand").length, 1, "3 sessions: one band")
            verify(probe.findNamed(three, "funnelWord").visible, "the grade word")
            verify(!probe.findNamed(three, "funnelTrendAfter3").visible, "no 'Trend after 3 sessions'")
            verify(!probe.findNamed(three, "funnelBadge").visible, "no badge")
            compare(probe.paths(three, "funnelCentre").length, 1, "the centre line")

            for (const c of [{ s: probe.run([4, 4]), why: "2 sessions" },
                             { s: probe.run([], -1), why: "no sessions" },
                             { s: probe.run([3, 3, 3, 4, 4], 3), why: "2 after a break" }]) {
                const f = probe.fn({ sessions: c.s, trend: "tightening" })
                wait(0)
                verify(f.trendAfter3, c.why + ": trendAfter3")
                compare(probe.paths(f, "funnelBand").length, 0, c.why + ": no band")
                const after = probe.findNamed(f, "funnelTrendAfter3"), badge = probe.findNamed(f, "funnelBadge")
                verify(after.visible, c.why + ": the phrase shows")
                compare(after.text, " · " + TrendVocabulary.tooFewSessions, c.why)
                compare(TrendVocabulary.tooFewSessions, "Trend after 3 sessions", "the §6.1 wording")
                verify(Qt.colorEqual(after.color, Theme.colorText3), c.why + ": in colorText3")
                verify(badge.visible, c.why + ": the badge shows")
                compare(badge.grade, f.currentGrade, c.why + ": the badge wears the current grade")
                verify(!probe.findNamed(f, "funnelWord").visible, c.why + ": the badge replaces the word")
                verify(!probe.findNamed(f, "funnelTrend").visible, c.why + ": no trend word")
                f.destroy()
            }

            // A break with 3 after it: two bands and one dashed tick; the ticks path has one segment.
            const br = probe.fn({ sessions: probe.run([3, 3, 4, 4, 4], 2), trend: "holding" })
            wait(0)
            compare(probe.paths(br, "funnelBand").length, 2, "a break: two bands")
            const ticks = probe.paths(br, "funnelTicks")
            compare(ticks.length, 1, "one ticks path")
            compare(ticks[0].strokeStyle, ShapePath.DashLine, "ticks dashed")
            verify(probe.sameRgb(ticks[0].strokeColor, Theme.colorBorderStrong), "ticks colorBorderStrong")
            const centre = probe.paths(br, "funnelCentre")[0]
            compare(centre.strokeStyle, ShapePath.DashLine, "centre dashed")
            compare(centre.strokeWidth, 1, "centre 1 px")
            verify(Qt.colorEqual(centre.strokeColor, Theme.colorBorderStrong), "centre colorBorderStrong")
            three.destroy(); br.destroy()
        }

        // §6.1: the band is the CURRENT grade's tone at 16 % (× variabilityWashAlpha on screen, at most
        // variabilityBandFillMax, §3 and R35), its edge at 55 %, 1 px; every band, old runs included. Every theme.
        function test_bandWearsTheCurrentTone() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                for (let g = 1; g <= 5; ++g) {
                    const f = probe.fn({ sessions: probe.run([5, 1, 2, 3, g], 1), trend: "holding" })
                    wait(0)
                    const bands = probe.paths(f, "funnelBand"), tone = probe.toneFor(g)
                    compare(bands.length, 2, "two runs")
                    for (const b of bands) {
                        verify(probe.sameRgb(b.fillColor, tone), probe.themeName() + " grade " + g + ": fill in the tone")
                        fuzzyCompare(b.fillColor.a, probe.wash(0.16) * tone.a, 0.005, probe.themeName() + " grade " + g + ": fill 16 % × wash")
                        verify(probe.sameRgb(b.strokeColor, tone), probe.themeName() + " grade " + g + ": edge in the tone")
                        fuzzyCompare(b.strokeColor.a, 0.55 * tone.a, 0.005, probe.themeName() + " grade " + g + ": edge 55 %")
                        compare(b.strokeWidth, 1, "edge 1 px")
                    }
                    verify(Qt.colorEqual(f.tone, tone), "tone by §3")
                    f.destroy()
                }
            }
        }

        // §6.1: markers r 1.6 in colorText2 on the centre line; the current session r 2.2 in the tone.
        function test_currentMarker() {
            const f = probe.fn({ sessions: probe.run([2, 2, 3, 4, 3, 4]), trend: "tightening" })
            wait(0)
            const cur = probe.paths(f, "funnelCurrentMarker"), rest = probe.paths(f, "funnelMarker")
            compare(cur.length, 1, "one current marker")
            compare(rest.length, 5, "the other five")
            verify(probe.sameRgb(cur[0].fillColor, Theme.colorGood), "current in the tone (grade 4)")
            fuzzyCompare(cur[0].pathElements[0].radiusX, 2.2, 0.001, "current r 2.2")
            const g = probe.findNamed(f, "funnelGraphic")
            fuzzyCompare(cur[0].pathElements[0].centerY, g.height / 2, 0.001, "on the centre line")
            fuzzyCompare(cur[0].pathElements[0].centerX, g.width - 6, 0.001, "the current one on the right, 6 px in")
            for (const m of rest) {
                verify(Qt.colorEqual(m.fillColor, Theme.colorText2), "a past marker in colorText2")
                fuzzyCompare(m.pathElements[0].radiusX, 1.6, 0.001, "past r 1.6")
            }
            // 14 sessions: 12 drawn, the current still the last.
            const many = []
            for (let i = 0; i < 14; ++i) many.push({ rho: 0.5, grade: 4, breakBefore: false })
            f.sessions = many
            wait(0)
            compare(probe.paths(f, "funnelMarker").length + probe.paths(f, "funnelCurrentMarker").length, 12, "12 drawn")
            f.destroy()
        }

        // §6.1 label, technical basis and accessible name.
        function test_labelAndAccessibleName() {
            const f = probe.fn({ sessions: probe.run([3, 3, 4, 4, 4, 4]), trend: "tightening",
                                 measureName: "Pelvis rotation at top",
                                 basisText: "ρ 0.62 → 0.38 over 6 sessions" })
            wait(0)
            compare(f.label, "Repeatable · ↑ Tightening")
            compare(probe.findNamed(f, "funnelWord").text, "Repeatable")
            verify(probe.sameRgb(probe.findNamed(f, "funnelWord").color, Theme.colorGood), "the word in the tone")
            compare(probe.findNamed(f, "funnelTrend").text, " · ↑ Tightening")
            verify(Qt.colorEqual(probe.findNamed(f, "funnelTrend").color, Theme.colorText2), "the trend in colorText2")
            compare(f.Accessible.name, "Pelvis rotation at top: Repeatable, tightening over 6 sessions")
            const basis = probe.findNamed(f, "funnelBasis")
            verify(!basis.visible, "golfer layer: no basis")
            f.contentLayer = "technical"
            verify(basis.visible, "technical layer: the basis")
            compare(basis.text, " · ρ 0.62 → 0.38 over 6 sessions")
            compare(basis.font.family, Theme.fontData)
            compare(basis.font.pixelSize, Theme.fontSzDataSm)

            f.trend = "loosening"
            compare(f.label, "Repeatable · ↓ Loosening")
            compare(f.Accessible.name, "Pelvis rotation at top: Repeatable, loosening over 6 sessions")
            f.trend = "holding"
            compare(f.label, "Repeatable · → Holding")
            f.trend = "insufficient"
            compare(f.label, "Repeatable", "no trend: the word alone")
            compare(f.Accessible.name, "Pelvis rotation at top: Repeatable")

            // A break: the count is the latest run's.
            f.trend = "tightening"
            f.sessions = probe.run([1, 2, 5, 5, 5, 5], 2)
            compare(f.Accessible.name, "Pelvis rotation at top: Grooved, tightening over 4 sessions")
            // Too few: the word and the phrase.
            f.sessions = probe.run([2, 4])
            compare(f.label, "Repeatable · Trend after 3 sessions")
            compare(f.Accessible.name, "Pelvis rotation at top: Repeatable. Trend after 3 sessions")
            f.measureName = ""
            compare(f.Accessible.name, "Consistency: Repeatable. Trend after 3 sessions", "no measure: the domain")
            f.destroy()
        }

        // §6.1: never narrower than 96; compact is 120 × variabilityFunnelH; wide fills its width.
        function test_sizes() {
            const c = probe.fn({ sessions: probe.run([4, 4, 4]) })
            wait(0)
            const g = probe.findNamed(c, "funnelGraphic")
            compare(g.width, Theme.sp(120), "compact width")
            compare(g.height, Theme.sp(Theme.variabilityFunnelH), "compact height")
            compare(c.minimumWidth, 96, "the provider's minimum")
            verify(g.x + g.width < probe.findNamed(c, "funnelLabel").x, "compact: the label beside")
            const w = probe.fn({ sessions: probe.run([4, 4, 4]), wide: true, width: 360 })
            wait(0)
            const wg = probe.findNamed(w, "funnelGraphic")
            compare(wg.width, 360, "wide fills its width")
            verify(probe.findNamed(w, "funnelLabel").y >= wg.height, "wide: the label beneath")
            w.width = 50
            compare(wg.width, 96, "never under 96")
            verify(w.implicitWidth >= 96, "implicit width at least the minimum")
            c.destroy(); w.destroy()
        }

        // §9.5.4: in print the band fill × variabilityPrintAlphaScale (clamped to 1); strokes unchanged.
        function test_printAlpha() {
            const k = Theme.variabilityPrintAlphaScale
            const f = probe.fn({ sessions: probe.run([4, 4, 4, 4]), trend: "holding" })
            wait(0)
            const b = probe.paths(f, "funnelBand")[0], tone = Theme.colorGood
            fuzzyCompare(b.fillColor.a, probe.wash(0.16) * tone.a, 0.005, "screen fill")
            f.medium = "print"
            fuzzyCompare(b.fillColor.a, Math.min(1, 0.16 * k) * tone.a, 0.005, "print fill")
            fuzzyCompare(b.strokeColor.a, 0.55 * tone.a, 0.005, "edge is a stroke: unchanged")
            fuzzyCompare(probe.paths(f, "funnelCurrentMarker")[0].fillColor.a, tone.a, 0.005, "an opaque marker stays opaque")
            f.destroy()
        }

        // §3, §10 (Mark, 7 Nov 2026): on screen the band FILL is a wash, × variabilityWashAlpha (1 light,
        // 3 dark), then held at or under variabilityBandFillMax (R35, 10709b0d: 32 %, so a dark band's fill
        // stays below its 55 % edge). The band's edge and the markers are not; print with roles never reads it.
        function test_washAlphaByTheme() {
            const ts = tsComp.createObject(probe), kp = Theme.variabilityPrintAlphaScale
            for (let t = 0; t < Theme.themeCount; t += 2) {
                let light = null
                for (const dark of [false, true]) {
                    Theme.themeIndex = t + (dark ? 1 : 0)
                    compare(Theme.variabilityWashAlpha, dark ? 3 : 1, probe.themeName())
                    const f = probe.fn({ sessions: probe.run([4, 4, 4, 4]), trend: "holding" })
                    wait(0)
                    const b = probe.paths(f, "funnelBand")[0], tone = Theme.colorGood
                    const a = { fill: b.fillColor.a / tone.a, edge: b.strokeColor.a / tone.a,
                                marker: probe.paths(f, "funnelMarker")[0].fillColor.a / Theme.colorText2.a,
                                current: probe.paths(f, "funnelCurrentMarker")[0].fillColor.a / tone.a }
                    if (!dark) {
                        fuzzyCompare(a.fill, 0.16, 0.005, probe.themeName() + ": light fill at the spec's 16 %")
                        light = a
                    } else {
                        fuzzyCompare(a.fill, Math.min(1, Theme.variabilityBandFillMax, 3 * light.fill), 0.005,
                                     probe.themeName() + ": fill × 3, at most variabilityBandFillMax")
                        verify(a.fill < a.edge, probe.themeName() + ": the fill stays below the band's edge (R35)")
                        fuzzyCompare(a.edge, light.edge, 0.005, probe.themeName() + ": the edge is a stroke")
                        fuzzyCompare(a.current, light.current, 0.005, probe.themeName() + ": the current marker is a dot")
                        fuzzyCompare(a.marker, light.marker, 0.005, probe.themeName() + ": a past marker is a dot")
                        // Print with roles: the print scale alone, whatever the screen's theme.
                        f.roles = ts
                        wait(0)
                        const pb = probe.paths(f, "funnelBand")[0]
                        fuzzyCompare(pb.fillColor.a, Math.min(1, 0.16 * kp) * ts.good.a, 0.005, probe.themeName() + ": print fill")
                        fuzzyCompare(pb.strokeColor.a, 0.55 * ts.good.a, 0.005, probe.themeName() + ": print edge")
                    }
                    f.destroy()
                }
            }
            ts.destroy()
        }

        // §9.5.4 with roles: the page's sizes, spaces, type and inks; the fill uplift is the print scale,
        // never roles.tintK. With roles on screen (R17; PpVariabilityFunnel.md "Typeset hosts") the sizes are
        // roles.u(), which on screen IS Theme.sp(), and the inks and type stay the host's: a fixed-aesthetic
        // typeset (Folio here) keeps Folio light's whatever the app shows. That end is checked under a theme
        // pinned away from Folio light, so it does not depend on the theme an earlier test file left behind.
        function test_printWithRoles() {
            const ts = tsComp.createObject(probe), k = Theme.variabilityPrintAlphaScale
            const f = probe.fn({ sessions: probe.run([3, 3, 4, 4, 4], 2), trend: "tightening", roles: ts,
                                 contentLayer: "technical", basisText: "ρ 0.66 → 0.58 over 3 sessions" })
            wait(0)
            compare(f.medium, "print", "medium follows roles")
            const g = probe.findNamed(f, "funnelGraphic")
            compare(g.width, ts.u(120), "compact width from roles.u()")
            compare(g.height, ts.u(Theme.variabilityFunnelH), "height from roles.u()")
            const lbl = probe.findNamed(f, "funnelLabel")
            compare(lbl.x - (g.x + g.width), ts.g(10), "the gap from roles.g()")
            verify(Qt.colorEqual(f.tone, ts.good), "grade 4 in roles.good")
            const band = probe.paths(f, "funnelBand")[0]
            verify(probe.sameRgb(band.fillColor, ts.good), "band in roles.good")
            fuzzyCompare(band.fillColor.a, Math.min(1, 0.16 * k) * ts.good.a, 0.005, "× print scale, not tintK")
            fuzzyCompare(band.strokeColor.a, 0.55 * ts.good.a, 0.005, "the edge keeps its alpha")
            verify(probe.sameColor(probe.paths(f, "funnelCentre")[0].strokeColor,
                                   Qt.alpha(ts.baseline, ts.baseline.a * ts.baselineOpacity)), "centre in the page's baseline")
            verify(probe.sameColor(probe.paths(f, "funnelTicks")[0].strokeColor,
                                   Qt.alpha(ts.baseline, ts.baseline.a * ts.baselineOpacity)), "ticks in the page's baseline")
            for (const m of probe.paths(f, "funnelMarker"))
                verify(probe.sameColor(m.fillColor, ts.ink2), "a past marker in roles.ink2")
            const word = probe.findNamed(f, "funnelWord"), trend = probe.findNamed(f, "funnelTrend"), basis = probe.findNamed(f, "funnelBasis")
            compare(word.font.family, ts.fBody.family, "the word in the page's body role")
            compare(word.font.pixelSize, ts.fBody.pixelSize)
            verify(Qt.colorEqual(word.color, ts.good), "the word in roles.good")
            compare(trend.font.family, ts.fBody.family, "the trend in the page's body role")
            verify(Qt.colorEqual(trend.color, ts.ink2), "the trend in roles.ink2")
            compare(basis.font.family, ts.fFig.family, "the basis in the page's figure role")
            verify(Qt.colorEqual(basis.color, ts.ink2), "the basis in roles.ink2")
            for (const c of [[3, ts.attention], [2, ts.warn], [1, ts.warn], [0, ts.ink3], [5, ts.good]]) {
                f.sessions = probe.run([4, 4, c[0]])
                verify(Qt.colorEqual(f.tone, c[1]), "grade " + c[0] + " in the page's ink")
            }
            f.sessions = probe.run([4, 4])
            wait(0)
            const after = probe.findNamed(f, "funnelTrendAfter3")
            verify(Qt.colorEqual(after.color, ts.ink3), "'Trend after 3 sessions' in roles.ink3")
            compare(probe.findNamed(f, "funnelBadge").roles, ts, "the badge gets the roles")
            Theme.themeIndex = 1   // instrument dark: its good and body face are not Folio light's (cleanup restores)
            verify(!Qt.colorEqual(Theme.colorGood, ts.good) && Theme.fontBody !== ts.fBody.family,
                   "the pinned theme tells Theme's inks and type from the host's")
            ts.medium = "screen"
            compare(f.medium, "screen")
            compare(g.width, Theme.sp(120), "roles on screen: Theme sizes (roles.u() on screen)")
            compare(lbl.x - (g.x + g.width), Theme.gap(10), "roles on screen: Theme spaces (roles.g() on screen)")
            f.sessions = probe.run([4, 4, 4])
            verify(Qt.colorEqual(f.tone, ts.good), "roles on screen: the host's inks, not Theme's")
            compare(word.font.family, ts.fBody.family, "roles on screen: the host's type, not Theme's")
            f.destroy(); ts.destroy()
        }

        // §6.1 "show the badge" is the measure's own: an Overlay funnel's too-few badge draws the Overlay
        // mark, a Dispersion one (the default) the Dispersion mark; anything else warns and is Dispersion.
        function test_badgeVariant() {
            const d = probe.fn({ sessions: probe.run([4, 4]) })
            compare(probe.findNamed(f0(d), "variabilityMark").variant, PpVariabilityMark.Dispersion, "default Dispersion")
            const o = probe.fn({ sessions: probe.run([4, 4]), variant: PpVariabilityMark.Overlay })
            wait(0)
            const om = probe.findNamed(f0(o), "variabilityMark")
            compare(om.variant, PpVariabilityMark.Overlay, "the badge's mark is the Overlay")
            verify(probe.paths(om, "variabilityTrace").length > 0, "it draws Overlay traces")
            compare(probe.paths(om, "variabilityDot").length, 0, "and no Dispersion dots")
            ignoreWarning(/PpVariabilityFunnel: variant 2 is not Dispersion or Overlay; drawn as Dispersion/)
            const n = probe.fn({ sessions: probe.run([4, 4]), variant: PpVariabilityMark.Needle })
            compare(probe.findNamed(f0(n), "variabilityMark").variant, PpVariabilityMark.Dispersion, "a Needle falls back to Dispersion")
            d.destroy(); o.destroy(); n.destroy()
        }
        function f0(f) { return probe.findNamed(f, "funnelBadge") }

        // The colour eases; the geometry changes at once.
        function test_colourEasesGeometryDoesNot() {
            const f = probe.fn({ sessions: probe.run([4, 4, 4]), trend: "holding" })
            wait(0)
            const b = probe.paths(f, "funnelBand")[0]
            f.sessions = probe.run([4, 4, 2])
            compare(probe.paths(f, "funnelBand")[0], b, "the same band path: rebound, not rebuilt")
            tryVerify(() => probe.sameRgb(b.fillColor, Theme.colorWarn), Theme.durationSlow + 1000, "eases to colorWarn")
            f.destroy()
        }

        // Every case, compact and wide, both layers, both media, under all 14 themes, with no warning.
        function test_everyThemeIsSilent() {
            // Warm the font database first (its one-off note belongs to the machine, not the funnel).
            const warm = probe.fn({ sessions: probe.run([4, 4, 4]), contentLayer: "technical", basisText: "ρ 1 → 1 over 3 sessions" })
            wait(0)
            warm.destroy()
            failOnWarning(/.*/)
            const cases = [probe.run([]), probe.run([3]), probe.run([3, 4]), probe.run([1, 2, 3, 4, 5]),
                           probe.run([5, 4, 3, 2, 1, 0]), probe.run([2, 3, 4, 4, 5], 2), probe.run([2, 3, 4, 4, 5], 3)]
            const many = []
            for (let i = 0; i < 14; ++i) many.push({ rho: 0.2 + 0.05 * i, grade: 1 + i % 5, breakBefore: i === 7 })
            cases.push(many)
            cases.push([{ rho: NaN, grade: 3 }, { rho: 0.4, grade: 4 }, { grade: 2 }, { rho: 0.3, grade: 4 }, { rho: 0.3, grade: 4 }])
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const made = []
                for (const s of cases)
                    for (const wide of [false, true])
                        for (const layer of ["golfer", "technical"])
                            for (const medium of ["screen", "print"])
                                made.push(probe.fn({ sessions: s, trend: "tightening", wide: wide, width: wide ? 300 : 0,
                                                     contentLayer: layer, medium: medium, measureName: "Stance width",
                                                     basisText: layer === "technical" ? "ρ 0.62 → 0.38 over 6 sessions" : "" }))
                wait(0)   // a frame, so every shape is polished and drawn under this theme
                for (const m of made) {
                    verify(m.label !== "", probe.themeName() + ": a label")
                    verify(m.Accessible.name !== "", probe.themeName() + ": an accessible name")
                    m.destroy()
                }
            }
        }
    }
}
