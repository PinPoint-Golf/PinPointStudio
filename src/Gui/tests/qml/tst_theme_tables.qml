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

// Theme as data (design system §14). Every theme is one entry in Theme._themes; a token reads the
// entry, and an optional key falls back to what the six original aesthetics always did. These cases
// pin the three promises that makes:
//   * a saved ui/themeIndex still means the same theme (0–11 never moved; Folio is 12–13);
//   * every theme defines every colour, so no token comes back undefined in a theme nobody checked;
//   * the new token families are pass-throughs in the original aesthetics — caps(), gap(), the
//     chart furniture and the card rules give exactly what the screens drew before — and Folio's
//     own values where Folio sets them.
// And that Folio's faces are really bundled: "Inter Tabular" exists and its digits are one width.
TestCase {
    name: "ThemeTables"

    property int was: 0
    function initTestCase() { was = Theme.themeIndex }
    function cleanupTestCase() { Theme.themeIndex = was }
    function same(a, b) { return Qt.colorEqual(a, b) }

    readonly property var colours: [
        "colorBg", "colorBg2", "colorBg3", "colorSurface", "colorBorder", "colorBorderMid",
        "colorBorderStrong", "colorText", "colorText2", "colorText3", "colorAccent", "colorAccentLight",
        "colorAccentMid", "gradientWarm", "gradientWarmLit", "gradientCool", "colorGood", "colorGoodLight",
        "colorWarn", "colorWarnLight", "colorError", "colorErrorLight", "colorAttention",
        "colorAttentionLight", "colorBandGreen", "colorBandAmber", "colorRail", "colorToolbar"
    ]

    // The order a saved index has always meant. Appending a theme must never renumber these.
    function test_savedIndicesKeepTheirTheme() {
        const order = ["instrument", "editorial", "studio", "vector", "terrain", "links"]
        for (let i = 0; i < 12; ++i) {
            Theme.themeIndex = i
            compare(Theme.aesthetic, order[Math.floor(i / 2)], "index " + i)
            compare(Theme.dark, i % 2 === 1, "index " + i + " mode")
        }
        Theme.themeIndex = 12; compare(Theme.aesthetic, "folio"); verify(!Theme.dark)
        Theme.themeIndex = 13; compare(Theme.aesthetic, "folio"); verify(Theme.dark)
        compare(Theme.themeCount, 14)
    }

    function test_everyThemeDefinesEveryToken() {
        for (let i = 0; i < Theme.themeCount; ++i) {
            Theme.themeIndex = i
            const where = Theme.aesthetic + (Theme.dark ? " dark" : " light")
            for (const c of colours)
                verify(Theme[c] !== undefined && String(Theme[c]).charAt(0) === "#", where + ": " + c)
            verify(Theme.chartSeries.length >= 6, where + ": chartSeries")
            for (const f of ["fontBody", "fontData", "fontDisplay"])
                verify(Theme[f].length > 0, where + ": " + f)
            for (const n of Theme.metricColorNames)
                verify(!same(Theme.paletteDefault(n), Theme.colorText3),
                       where + ": metric colour " + n + " is the theme's own, not the grey fallback")
            const info = Theme.themeInfo(i)
            compare(info.mode, Theme.dark ? "Dark" : "Light")
            verify(same(info.contentBg, Theme.colorBg) && same(info.railBg, Theme.colorSurface), where + ": themeInfo")
        }
    }

    // In the original aesthetics every new family hands back what the screens drew before.
    function test_originalAestheticsArePassThroughs() {
        for (let i = 0; i < 12; ++i) {
            Theme.themeIndex = i
            const where = Theme.aesthetic + (Theme.dark ? " dark" : " light")
            verify(Theme.capsHeadings, where)
            compare(Theme.caps("Your focus"), "YOUR FOCUS", where)
            compare(Theme.caps(qsTr("Path %1")).arg("in-to-out"), "PATH in-to-out", where + ": arg keeps its case")
            compare(Theme.capsFont, Font.AllUppercase, where)
            for (const n of [0, 1, 6, 12.5, 24]) compare(Theme.gap(n), Theme.sp(n), where + ": gap(" + n + ")")
            verify(same(Theme.gridColor("#123456"), "#123456"), where + ": gridColor")
            compare(Theme.gridOpacity(0.37), 0.37, where)
            verify(same(Theme.baselineColor("#654321"), "#654321"), where + ": baselineColor")
            compare(Theme.baselineOpacity(0.7), 0.7, where)
            compare(Theme.curveWidth(2), 2, where)
            verify(Theme.chartFrame && !Theme.chartMarkerHollow, where + ": chart frame / solid markers")
            compare(Theme.cardRule, 3, where); compare(Theme.heroRule, 4, where); verify(Theme.heroTinted, where)
            compare(Theme.trackingMicro, 0.8, where)
            compare(Theme.fontSzMicro, Math.round(10 * Theme.fontScale), where)
            // The rail and toolbar ground the components used to pick by name.
            verify(same(Theme.colorRail, Theme.aesthetic === "instrument" ? Theme.colorBg2 : Theme.colorBg), where + ": rail")
            verify(same(Theme.colorToolbar, Theme.aesthetic === "instrument" ? Theme.colorBg2 : Theme.colorSurface), where + ": toolbar")
            compare(Theme.railActiveBar, Theme.aesthetic === "editorial" || Theme.aesthetic === "vector", where)
        }
        // The Appearance tiles' dots, exactly as the panel hardcoded them.
        compare(String(Theme.themeInfo(0).dot).toUpperCase(), "#B5701A")
        compare(String(Theme.themeInfo(11).dot).toUpperCase(), "#C85C6A")
    }

    function test_folio() {
        for (const i of [12, 13]) {
            Theme.themeIndex = i
            const where = "folio " + (Theme.dark ? "dark" : "light")
            verify(!Theme.capsHeadings, where)
            compare(Theme.caps("Your focus"), "Your focus", where)
            compare(Theme.capsFont, Font.MixedCase, where)
            compare(Theme.fontBody, "Inter"); compare(Theme.fontData, "Inter Tabular"); compare(Theme.fontDisplay, "Inter Display")
            compare(Theme.cardRule, 0, where); compare(Theme.heroRule, 0, where); verify(!Theme.heroTinted, where)
            verify(!Theme.chartFrame && Theme.chartMarkerHollow, where)
            verify(!Theme.gradientTitlesActive, where + ": flat titles")
            verify(Theme.spacingScale > 1 && Theme.gap(20) > Theme.sp(20), where + ": more room between things")
            // The chart greys are Folio's own, opaque, whatever the site handed in.
            verify(same(Theme.gridColor("#123456"), Theme.dark ? "#2C2C2A" : "#E1E0D9"), where + ": grid")
            compare(Theme.gridOpacity(0.37), 1.0, where)
            verify(same(Theme.baselineColor("#654321"), Theme.dark ? "#383835" : "#C3C2B7"), where + ": baseline")
        }
        compare(Theme.themeInfo(12).aesthetic, "Folio")
    }

    FontMetrics { id: tabular; font.family: "Inter Tabular"; font.pixelSize: 40 }
    FontMetrics { id: inter;   font.family: "Inter";         font.pixelSize: 40 }
    function test_folioFacesAreBundled() {
        const fams = Qt.fontFamilies()
        for (const f of ["Inter", "Inter Display", "Inter Tabular"])
            verify(fams.indexOf(f) >= 0, f + " is registered (cmake/PinPointFonts.cmake → :/fonts)")
        // Every digit one advance in Inter Tabular — that is the whole reason it exists — while
        // Inter itself keeps its proportional figures.
        const w1 = tabular.advanceWidth("1")
        for (const d of "023456789") compare(tabular.advanceWidth(d), w1, "Inter Tabular '" + d + "'")
        verify(inter.advanceWidth("1") < inter.advanceWidth("8"), "Inter's own figures are proportional")
    }
}
