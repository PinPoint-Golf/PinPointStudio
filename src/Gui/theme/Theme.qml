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

pragma Singleton
import QtQuick

// Single source of truth for all visual tokens.
// Set themeIndex to switch the entire UI. Never hardcode colours, fonts, or sizes in components.
//
// A theme is DATA: one entry in `_themes` below (design system §14). Each entry holds what its
// aesthetic shares across modes (fonts, radii, the rail) and a `light` and a `dark` block of
// colours. A token reads the entry with `_m.<name>` (colours every theme defines) or
// `_v(name, default)` (everything a theme MAY set — the default is what the six original
// aesthetics do, so a theme lists only where it differs). A new theme is one entry plus a line in
// `themeOrder`; nothing else in the app changes.
//
// The tables are plain JS values on a `var`, not nested QtObjects, which avoids the
// initialisation-order issues that cause "Unable to assign [undefined]" warnings.
QtObject {
    id: root

    // Theme cycle index: 2 × the aesthetic's place in themeOrder, + 1 for dark. 0–11 are the six
    // original aesthetics (instrument-light, instrument-dark, … links-dark) and keep their indices,
    // so a saved ui/themeIndex still means the same theme; 12–13 are folio-light, folio-dark.
    property int themeIndex: 0

    Component.onCompleted: {
        themeIndex     = appSettings.themeIndex
        density        = appSettings.density
        reduceMotion   = appSettings.reduceMotion
        overlayOpacity = appSettings.overlayOpacity
        gradientTitles = appSettings.gradientTitles
    }
    onThemeIndexChanged: appSettings.themeIndex = themeIndex

    property real fontScale: 1.0
    onFontScaleChanged: appSettings.fontScale = fontScale

    property string density: "default"
    onDensityChanged: appSettings.density = density

    property bool reduceMotion: false
    onReduceMotionChanged: appSettings.reduceMotion = reduceMotion

    property real overlayOpacity: 0.7
    onOverlayOpacityChanged: appSettings.overlayOpacity = overlayOpacity

    readonly property var themeOrder: ["instrument", "editorial", "studio", "vector", "terrain", "links", "folio"]
    readonly property int themeCount: themeOrder.length * 2
    function cycleTheme() { themeIndex = (themeIndex + 1) % themeCount }

    // Scale-aware pixel helper — use Theme.sp(n) instead of hardcoded pixel values
    // so all dimensions respond to the user's text size preference.
    function sp(n) { return Math.round(n * fontScale) }
    // The same for SPACE between things — a layout's spacing, margins and padding, a card's pad —
    // as opposed to the size of a thing. It also follows the theme's spacingScale, so a theme can
    // breathe more without growing its type or its marks. 1.0 in every theme but Folio, where
    // gap(n) is exactly sp(n).
    function gap(n) { return Math.round(n * fontScale * spacingScale) }

    // Active aesthetic and mode — derived from themeIndex.
    // Can also be set directly to bind to C++ AppSettings at startup.
    readonly property string aesthetic: themeOrder[Math.floor(themeIndex / 2)]
    readonly property bool   dark:      (themeIndex % 2) === 1

    // ── The themes ───────────────────────────────────────────────────────────
    // Shared keys every theme sets: label, swatch ([light, dark] dot on the Appearance tile),
    // fontBody, fontData, fontDisplay, fontBodyWeight, displayBase, fontDisplayItalic,
    // fontDisplayWeight, radius, radiusLg, railWidth. Optional keys, with the original aesthetics'
    // behaviour as the default (see the token that reads each): railTone, toolbarTone,
    // railActiveBar, capsHeadings, heroSize, trackingMicro, trackingLabel, trackingData, microSize,
    // spacingScale, cardRule, heroRule, heroTinted, gradientTitlesAllowed, chartGrid,
    // chartGridOpacity, chartBaseline, chartBaselineOpacity, chartFrame, chartCurveWidth,
    // chartMarkerHollow. Optional keys may
    // also sit in a mode block when they differ between light and dark (Folio's chart greys).
    readonly property var _themes: ({
        instrument: {
            label: "Instrument", swatch: ["#B5701A", "#E6AC54"], fontBody: "Gelasio", fontData: "DM Mono", fontDisplay: "Gelasio",
            fontBodyWeight: Font.Normal, displayBase: 26, fontDisplayItalic: false, fontDisplayWeight: Font.Bold, radius: 6,
            radiusLg: 10, railWidth: 56, railTone: "bg2", toolbarTone: "bg2",
            light: { colorBg: "#F4EFE3", colorBg2: "#ECE6D7", colorBg3: "#E1DACA", colorSurface: "#FBF8F0",
                     colorBorder: "#1f3a322a", colorBorderMid: "#2b3a322a", colorBorderStrong: "#383a322a", colorText: "#0A1115",
                     colorText2: "#5A5246", colorText3: "#998F82", colorAccent: "#9A5E12", colorAccentLight: "#149a5e12",
                     colorAccentMid: "#2e9a5e12", gradientWarm: "#B5701A", gradientWarmLit: "#CC9A45", gradientCool: "#2B6E7A",
                     colorGood: "#357058", colorGoodLight: "#14357058", colorWarn: "#A8482A", colorWarnLight: "#14a8482a",
                     colorError: "#9C2A2A", colorErrorLight: "#149c2a2a", colorAttention: "#8A6612", colorAttentionLight: "#148a6612",
                     colorBandGreen: "#24357058", colorBandAmber: "#248a6612",
                     chartSeries: ["#B5701A", "#A8482A", "#3E7E68", "#2B6E7A", "#6B4E8C", "#A0405A"] },
            dark:  { colorBg: "#05080A", colorBg2: "#0A1115", colorBg3: "#111B20", colorSurface: "#0A0F13",
                     colorBorder: "#1ff2ede2", colorBorderMid: "#2bf2ede2", colorBorderStrong: "#38f2ede2", colorText: "#F2EDE2",
                     colorText2: "#CFD3CC", colorText3: "#94A09E", colorAccent: "#E6AC54", colorAccentLight: "#17e6ac54",
                     colorAccentMid: "#29e6ac54", gradientWarm: "#E6AC54", gradientWarmLit: "#F3C987", gradientCool: "#7BC0DB",
                     colorGood: "#8AB389", colorGoodLight: "#178ab389", colorWarn: "#E07E64", colorWarnLight: "#17e07e64",
                     colorError: "#E0595B", colorErrorLight: "#17e0595b", colorAttention: "#F2C84A", colorAttentionLight: "#17f2c84a",
                     colorBandGreen: "#248ab389", colorBandAmber: "#24f2c84a",
                     chartSeries: ["#E6AC54", "#E07E64", "#8AB389", "#7BC0DB", "#B79AD6", "#E08FA8"] }
        },
        editorial: {
            label: "Editorial", swatch: ["#234E8C", "#7FB0E8"], fontBody: "Instrument Sans", fontData: "JetBrains Mono", fontDisplay: "Source Serif 4",
            fontBodyWeight: Font.Light, displayBase: 34, fontDisplayItalic: true, fontDisplayWeight: Font.Normal, radius: 3,
            radiusLg: 6, railWidth: 58, railActiveBar: true,
            light: { colorBg: "#FAF8F4", colorBg2: "#F1EEE7", colorBg3: "#E7E2D8", colorSurface: "#FFFFFF",
                     colorBorder: "#121a1410", colorBorderMid: "#1a1a1410", colorBorderStrong: "#211a1410", colorText: "#1A1714",
                     colorText2: "#4C463E", colorText3: "#9B958A", colorAccent: "#234E8C", colorAccentLight: "#0f234e8c",
                     colorAccentMid: "#24234e8c", gradientWarm: "#9A7A2E", gradientWarmLit: "#A86A4A", gradientCool: "#234E8C",
                     colorGood: "#1E5238", colorGoodLight: "#0f1e5238", colorWarn: "#9A3A16", colorWarnLight: "#0f9a3a16",
                     colorError: "#8E1F22", colorErrorLight: "#0f8e1f22", colorAttention: "#8A6414", colorAttentionLight: "#0f8a6414",
                     colorBandGreen: "#241e5238", colorBandAmber: "#248a6414",
                     chartSeries: ["#234E8C", "#1E5238", "#A8531E", "#6E3A78", "#8A6414", "#8E2E48"] },
            dark:  { colorBg: "#14110F", colorBg2: "#1E1A17", colorBg3: "#272320", colorSurface: "#191612",
                     colorBorder: "#12f2ede4", colorBorderMid: "#1cf2ede4", colorBorderStrong: "#26f2ede4", colorText: "#F2EDE4",
                     colorText2: "#A69E92", colorText3: "#625B50", colorAccent: "#7FB0E8", colorAccentLight: "#127fb0e8",
                     colorAccentMid: "#247fb0e8", gradientWarm: "#E4C878", gradientWarmLit: "#DDA883", gradientCool: "#7FB0E8",
                     colorGood: "#7FC4A6", colorGoodLight: "#127fc4a6", colorWarn: "#DD9270", colorWarnLight: "#12dd9270",
                     colorError: "#D26E6E", colorErrorLight: "#12d26e6e", colorAttention: "#E6C36A", colorAttentionLight: "#12e6c36a",
                     colorBandGreen: "#247fc4a6", colorBandAmber: "#24e6c36a",
                     chartSeries: ["#7FB0E8", "#7FC4A6", "#DDA46A", "#C79ACB", "#E6C36A", "#DB8CA6"] }
        },
        studio: {
            label: "Studio", swatch: ["#0B5FE6", "#5A9BFF"], fontBody: "Hanken Grotesk", fontData: "Geist Mono", fontDisplay: "Hanken Grotesk",
            fontBodyWeight: Font.Light, displayBase: 22, fontDisplayItalic: false, fontDisplayWeight: Font.Normal, radius: 5,
            radiusLg: 8, railWidth: 52,
            light: { colorBg: "#F4F5F7", colorBg2: "#EAECEF", colorBg3: "#DEE1E6", colorSurface: "#FBFCFD",
                     colorBorder: "#0f0c1220", colorBorderMid: "#190c1220", colorBorderStrong: "#260c1220", colorText: "#0C0F14",
                     colorText2: "#4A505A", colorText3: "#9AA1AC", colorAccent: "#0B5FE6", colorAccentLight: "#0d0b5fe6",
                     colorAccentMid: "#1f0b5fe6", gradientWarm: "#0E7C9C", gradientWarmLit: "#0A5FBF", gradientCool: "#0B5FE6",
                     colorGood: "#0A7A4A", colorGoodLight: "#0f0a7a4a", colorWarn: "#D94A00", colorWarnLight: "#0fd94a00",
                     colorError: "#CC2000", colorErrorLight: "#0fcc2000", colorAttention: "#9C6F12", colorAttentionLight: "#0f9c6f12",
                     colorBandGreen: "#240a7a4a", colorBandAmber: "#249c6f12",
                     chartSeries: ["#0B5FE6", "#0A7A4A", "#D64545", "#9C6F12", "#7A4FCF", "#0E7C9C"] },
            dark:  { colorBg: "#0E1013", colorBg2: "#16191D", colorBg3: "#1E2228", colorSurface: "#131519",
                     colorBorder: "#0eeceef2", colorBorderMid: "#17eceef2", colorBorderStrong: "#21eceef2", colorText: "#ECEEF2",
                     colorText2: "#878D96", colorText3: "#474C54", colorAccent: "#5A9BFF", colorAccentLight: "#125a9bff",
                     colorAccentMid: "#265a9bff", gradientWarm: "#3FC2E0", gradientWarmLit: "#4FA6F0", gradientCool: "#5A9BFF",
                     colorGood: "#30C983", colorGoodLight: "#1230c983", colorWarn: "#FF6B35", colorWarnLight: "#14ff6b35",
                     colorError: "#FF5555", colorErrorLight: "#14ff5555", colorAttention: "#F5C451", colorAttentionLight: "#14f5c451",
                     colorBandGreen: "#2430c983", colorBandAmber: "#24f5c451",
                     chartSeries: ["#5A9BFF", "#30C983", "#FF6B6B", "#F5C451", "#B79AF5", "#3FC2E0"] }
        },
        vector: {
            label: "Vector", swatch: ["#CC3300", "#FF5500"], fontBody: "Space Grotesk", fontData: "Space Mono", fontDisplay: "Space Mono",
            fontBodyWeight: Font.Light, displayBase: 24, fontDisplayItalic: false, fontDisplayWeight: Font.Normal, radius: 0,
            radiusLg: 0, railWidth: 52, railActiveBar: true,
            light: { colorBg: "#F0F1F4", colorBg2: "#E4E6EB", colorBg3: "#D8DBE3", colorSurface: "#FAFBFC",
                     colorBorder: "#12000000", colorBorderMid: "#1c000000", colorBorderStrong: "#2e000000", colorText: "#0A0B10",
                     colorText2: "#4A4E5E", colorText3: "#9098B0", colorAccent: "#CC3300", colorAccentLight: "#0fcc3300",
                     colorAccentMid: "#24cc3300", gradientWarm: "#B85A00", gradientWarmLit: "#D67A2A", gradientCool: "#0066CC",
                     colorGood: "#006B45", colorGoodLight: "#0f006b45", colorWarn: "#7A3800", colorWarnLight: "#0f7a3800",
                     colorError: "#8B0014", colorErrorLight: "#0f8b0014", colorAttention: "#B58900", colorAttentionLight: "#0fb58900",
                     colorBandGreen: "#24006b45", colorBandAmber: "#24b58900",
                     chartSeries: ["#CC3300", "#0066CC", "#006B45", "#B58900", "#7A29CC", "#C2185B"] },
            dark:  { colorBg: "#0A0B0D", colorBg2: "#0F1114", colorBg3: "#151719", colorSurface: "#13151A",
                     colorBorder: "#0fffffff", colorBorderMid: "#1affffff", colorBorderStrong: "#2effffff", colorText: "#E8EAF0",
                     colorText2: "#8B90A0", colorText3: "#484E5E", colorAccent: "#FF5500", colorAccentLight: "#12ff5500",
                     colorAccentMid: "#24ff5500", gradientWarm: "#FF8C35", gradientWarmLit: "#FFB066", gradientCool: "#3399FF",
                     colorGood: "#2EE8A0", colorGoodLight: "#122ee8a0", colorWarn: "#FF8C35", colorWarnLight: "#14ff8c35",
                     colorError: "#FF4455", colorErrorLight: "#14ff4455", colorAttention: "#FFD60A", colorAttentionLight: "#14ffd60a",
                     colorBandGreen: "#242ee8a0", colorBandAmber: "#24ffd60a",
                     chartSeries: ["#FF5500", "#3399FF", "#2EE8A0", "#FFD60A", "#B266FF", "#FF4081"] }
        },
        terrain: {
            label: "Terrain", swatch: ["#1E7A4E", "#4FCB8C"], fontBody: "Fraunces", fontData: "DM Mono", fontDisplay: "Fraunces",
            fontBodyWeight: Font.Normal, displayBase: 28, fontDisplayItalic: false, fontDisplayWeight: Font.DemiBold, radius: 8,
            radiusLg: 12, railWidth: 56,
            light: { colorBg: "#F2F4EC", colorBg2: "#E8EBDF", colorBg3: "#DCE1D1", colorSurface: "#FAFBF5",
                     colorBorder: "#1227392b", colorBorderMid: "#1c27392b", colorBorderStrong: "#2a27392b", colorText: "#16221A",
                     colorText2: "#47554B", colorText3: "#8C988D", colorAccent: "#1E7A4E", colorAccentLight: "#0f1e7a4e",
                     colorAccentMid: "#241e7a4e", gradientWarm: "#8A6A1E", gradientWarmLit: "#5E8A3A", gradientCool: "#1C7A5A",
                     colorGood: "#3A7A56", colorGoodLight: "#0f3a7a56", colorWarn: "#A8531E", colorWarnLight: "#0fa8531e",
                     colorError: "#A82E2A", colorErrorLight: "#0fa82e2a", colorAttention: "#8A6612", colorAttentionLight: "#0f8a6612",
                     colorBandGreen: "#243a7a56", colorBandAmber: "#248a6612",
                     chartSeries: ["#1E7A4E", "#8A6612", "#A8531E", "#2B7E8C", "#6B4E8C", "#A0405A"] },
            dark:  { colorBg: "#080D0A", colorBg2: "#0D140F", colorBg3: "#121B15", colorSurface: "#0B110D",
                     colorBorder: "#12eef3ec", colorBorderMid: "#1ceef3ec", colorBorderStrong: "#2aeef3ec", colorText: "#ECF3EC",
                     colorText2: "#AEB9AD", colorText3: "#5C685D", colorAccent: "#4FCB8C", colorAccentLight: "#174fcb8c",
                     colorAccentMid: "#294fcb8c", gradientWarm: "#E0C766", gradientWarmLit: "#A8C96A", gradientCool: "#3EC79A",
                     colorGood: "#86C49A", colorGoodLight: "#1786c49a", colorWarn: "#E6915A", colorWarnLight: "#17e6915a",
                     colorError: "#E66B66", colorErrorLight: "#17e66b66", colorAttention: "#E0C24A", colorAttentionLight: "#17e0c24a",
                     colorBandGreen: "#2486c49a", colorBandAmber: "#24e0c24a",
                     chartSeries: ["#4FCB8C", "#E0C24A", "#E6915A", "#5AB6C9", "#B79AD6", "#E6849A"] }
        },
        links: {
            label: "Links", swatch: ["#7E2D3A", "#C85C6A"], fontBody: "Literata", fontData: "Geist Mono", fontDisplay: "Literata",
            fontBodyWeight: Font.Normal, displayBase: 30, fontDisplayItalic: false, fontDisplayWeight: Font.Normal, radius: 2,
            radiusLg: 4, railWidth: 56,
            light: { colorBg: "#EDE7D7", colorBg2: "#E3DCC8", colorBg3: "#D7CFB8", colorSurface: "#F6F1E4",
                     colorBorder: "#121b2a3a", colorBorderMid: "#1c1b2a3a", colorBorderStrong: "#2a1b2a3a", colorText: "#1B2A3A",
                     colorText2: "#4E5A68", colorText3: "#948C79", colorAccent: "#7E2D3A", colorAccentLight: "#0f7e2d3a",
                     colorAccentMid: "#247e2d3a", gradientWarm: "#7E2D3A", gradientWarmLit: "#A8791E", gradientCool: "#2A4A72",
                     colorGood: "#2E6B48", colorGoodLight: "#0f2e6b48", colorWarn: "#9A5220", colorWarnLight: "#0f9a5220",
                     colorError: "#A82A1E", colorErrorLight: "#0fa82a1e", colorAttention: "#8C6712", colorAttentionLight: "#0f8c6712",
                     colorBandGreen: "#242e6b48", colorBandAmber: "#248c6712",
                     chartSeries: ["#7E2D3A", "#2A4A72", "#A8791E", "#2E6B48", "#6A4E86", "#2B6E7A"] },
            dark:  { colorBg: "#10131A", colorBg2: "#161B24", colorBg3: "#1D2430", colorSurface: "#131820",
                     colorBorder: "#12ece6d6", colorBorderMid: "#1cece6d6", colorBorderStrong: "#2aece6d6", colorText: "#ECE6D6",
                     colorText2: "#ADA694", colorText3: "#6B6656", colorAccent: "#C85C6A", colorAccentLight: "#17c85c6a",
                     colorAccentMid: "#29c85c6a", gradientWarm: "#C85C6A", gradientWarmLit: "#E0A24E", gradientCool: "#5B87B8",
                     colorGood: "#7FB894", colorGoodLight: "#177fb894", colorWarn: "#D98A5A", colorWarnLight: "#17d98a5a",
                     colorError: "#E06B60", colorErrorLight: "#17e06b60", colorAttention: "#F5C63A", colorAttentionLight: "#17f5c63a",
                     colorBandGreen: "#247fb894", colorBandAmber: "#24f5c63a",
                     chartSeries: ["#C85C6A", "#5B87B8", "#E0A24E", "#7FB894", "#A98BC4", "#6FB6C4"] }
        },
        folio: {
            label: "Folio", swatch: ["#2573D1", "#3987E5"], fontBody: "Inter", fontData: "Inter Tabular", fontDisplay: "Inter Display",
            fontBodyWeight: Font.Normal, displayBase: 26, fontDisplayItalic: false, fontDisplayWeight: Font.Normal, radius: 4,
            radiusLg: 6, railWidth: 56, railActiveBar: true, capsHeadings: false, heroSize: 34, trackingMicro: 0.15,
            trackingLabel: 0.1, trackingData: 0, microSize: 11, spacingScale: 1.15, cardRule: 0,
            heroRule: 0, heroTinted: false, gradientTitlesAllowed: false, chartGridOpacity: 1.0, chartBaselineOpacity: 1.0,
            chartFrame: false, chartMarkerHollow: true,
            light: { colorBg: "#F6F6F3", colorBg2: "#EEEDE9", colorBg3: "#E5E4DF", colorSurface: "#FFFFFF",
                     colorBorder: "#1a0b0b0b", colorBorderMid: "#240b0b0b", colorBorderStrong: "#380b0b0b", colorText: "#2B2B2A",
                     colorText2: "#52514E", colorText3: "#898781", colorAccent: "#2573D1", colorAccentLight: "#142573d1",
                     colorAccentMid: "#262573d1", gradientWarm: "#2573D1", gradientWarmLit: "#3987E5", gradientCool: "#4A3AA7",
                     colorGood: "#018702", colorGoodLight: "#14018702", colorWarn: "#BA562D", colorWarnLight: "#14ba562d",
                     colorError: "#D03B3B", colorErrorLight: "#14d03b3b", colorAttention: "#9B6C05", colorAttentionLight: "#149b6c05",
                     colorBandGreen: "#24018702", colorBandAmber: "#249b6c05",
                     chartSeries: ["#2A78D6", "#EB6834", "#1BAF7A", "#EDA100", "#E87BA4", "#008300", "#4A3AA7", "#E34948"], chartGrid: "#E1E0D9", chartBaseline: "#C3C2B7" },
            dark:  { colorBg: "#0F0F0E", colorBg2: "#161615", colorBg3: "#22221F", colorSurface: "#1A1A19",
                     colorBorder: "#1affffff", colorBorderMid: "#26ffffff", colorBorderStrong: "#38ffffff", colorText: "#EDEDEA",
                     colorText2: "#C3C2B7", colorText3: "#8F8D86", colorAccent: "#3987E5", colorAccentLight: "#173987e5",
                     colorAccentMid: "#293987e5", gradientWarm: "#3987E5", gradientWarmLit: "#5598E7", gradientCool: "#9085E9",
                     colorGood: "#0CA30C", colorGoodLight: "#170ca30c", colorWarn: "#EC835A", colorWarnLight: "#17ec835a",
                     colorError: "#E66767", colorErrorLight: "#17e66767", colorAttention: "#FAB219", colorAttentionLight: "#17fab219",
                     colorBandGreen: "#240ca30c", colorBandAmber: "#24fab219",
                     chartSeries: ["#3987E5", "#D95926", "#199E70", "#C98500", "#D55181", "#008300", "#9085E9", "#E66767"], chartGrid: "#2C2C2A", chartBaseline: "#383835" }
        }
    })

    readonly property var _a: _themes[aesthetic] || _themes.studio
    readonly property var _m: dark ? _a.dark : _a.light
    // An optional key: the mode block's value, else the theme's, else the original aesthetics'.
    function _v(key, dflt) {
        var v = _m[key]
        if (v !== undefined) return v
        v = _a[key]
        return v !== undefined ? v : dflt
    }
    // What the Appearance panel draws for theme `i` — read from the table, so a new theme needs
    // no edit there. railBg / contentBg are the tile's two panes (surface and background).
    function themeInfo(i) {
        var t = _themes[themeOrder[Math.floor(i / 2)]] || _themes.studio
        var d = (i % 2) === 1
        var m = d ? t.dark : t.light
        return { aesthetic: t.label, mode: d ? "Dark" : "Light", railBg: m.colorSurface,
                 sidenavBg: m.colorSurface, contentBg: m.colorBg, dot: t.swatch[d ? 1 : 0],
                 text: m.colorText, text3: m.colorText3, border: m.colorBorderMid, fontBody: t.fontBody }
    }

    // ── Colour tokens ────────────────────────────────────────────────────────

    readonly property color colorBg: _m.colorBg
    readonly property color colorBg2: _m.colorBg2
    readonly property color colorBg3: _m.colorBg3
    readonly property color colorSurface: _m.colorSurface

    // Borders — 1 px at these opacities simulate 0.5 px
    readonly property color colorBorder: _m.colorBorder
    readonly property color colorBorderMid: _m.colorBorderMid
    readonly property color colorBorderStrong: _m.colorBorderStrong

    // Text
    readonly property color colorText: _m.colorText
    readonly property color colorText2: _m.colorText2
    readonly property color colorText3: _m.colorText3

    // Accent
    readonly property color colorAccent: _m.colorAccent
    readonly property color colorAccentLight: _m.colorAccentLight
    readonly property color colorAccentMid: _m.colorAccentMid

    // ── Brand gradient tokens ────────────────────────────────────────────────
    // Signature warm→cool sweep from the marketing site: pelvis-amber through
    // light-amber to club-cyan — the kinematic-sequence colours. Used for large
    // display titles (via PpDisplayText) and accent bars. Each aesthetic supplies
    // its own warm/cool pair so the sweep feels native; instrument matches the
    // site 1:1. Light variants are deepened to hold contrast on pale grounds.
    readonly property color gradientWarm: _m.gradientWarm
    readonly property color gradientWarmLit: _m.gradientWarmLit
    readonly property color gradientCool: _m.gradientCool

    // Sweep angle (deg). Site uses 96° for titles, 180° for vertical accent bars.
    // Native Rectangle gradients are H/V only, so PpDisplayText uses Horizontal;
    // 96° reads as horizontal. Use Shapes LinearGradient for the exact tilt later.
    readonly property real gradientAngle: 96

    // Programmatic stop list — for charts, Shapes, or custom paint.
    readonly property var gradientBrandStops: [
        { position: 0.0,  color: gradientWarm },
        { position: 0.55, color: gradientWarmLit },
        { position: 1.0,  color: gradientCool }
    ]

    // Master switch for gradient display titles. Persisted to AppSettings.
    // gradientTitlesActive folds in reduceMotion, and a theme that never sweeps (Folio's titles are
    // flat ink), so callers test one flag.
    property bool gradientTitles: true
    onGradientTitlesChanged: appSettings.gradientTitles = gradientTitles
    readonly property bool gradientTitlesActive: gradientTitles && !reduceMotion && _v("gradientTitlesAllowed", true)

    // Good (success / go / connected)
    readonly property color colorGood: _m.colorGood
    readonly property color colorGoodLight: _m.colorGoodLight

    // Warn (caution / unexpected)
    readonly property color colorWarn: _m.colorWarn
    readonly property color colorWarnLight: _m.colorWarnLight

    // Error (critical failure / fatal — distinctly red, darker than warn)
    readonly property color colorError: _m.colorError
    readonly property color colorErrorLight: _m.colorErrorLight

    // Attention (call-to-action framing — draws the eye to a row/control that
    // needs the user to act, e.g. an uncalibrated sensor). Distinct from colorWarn
    // (orange-red caution) and colorError (red failure): this is a confident amber
    // "do this next" frame. Strong variant = border/text, Light variant = fill.
    readonly property color colorAttention: _m.colorAttention
    readonly property color colorAttentionLight: _m.colorAttentionLight

    // RAG assessment palette (Wrist diagnostics, design §8.4). Semantic aliases onto the existing
    // status family so a "good/watch/fault/no-data" reading reuses the same hues a user already
    // associates with success / call-to-action / failure; the view pairs each with a shape + label
    // so colour is never the only channel.
    readonly property color colorRagGood:  colorGood
    readonly property color colorRagWatch: colorAttention
    readonly property color colorRagFault: colorError
    readonly property color colorRagNone:  colorText3

    // Band-corridor fills — the shaded "expected" corridor on a trajectory strip. Low-alpha
    // (~0x24) so the player line and points read on top; greener/amber than the ~0x14 *Light
    // fills, which are too faint to mark a corridor.
    readonly property color colorBandGreen: _m.colorBandGreen
    readonly property color colorBandAmber: _m.colorBandAmber

    // IMU device-identity colours — A/B/C/D. Fixed hues (red / yellow / green /
    // blue) so a given sensor's colour is consistent across all aesthetics; only
    // brightness shifts for dark vs light backgrounds. Used by the 3D orientation
    // markers and IMU-related UI.
    readonly property color colorImuA: dark ? "#FF3B30" : "#D32F2F"   // red
    readonly property color colorImuB: dark ? "#FFD60A" : "#E0A400"   // yellow
    readonly property color colorImuC: dark ? "#34C759" : "#2E9E4F"   // green
    readonly property color colorImuD: dark ? "#3399FF" : "#0A6CFF"   // blue

    // ── Pose overlay ("Biomech Blueprint") palette ───────────────────────────
    // A deliberately FIXED cool-cyan instrument palette for the pose skeleton
    // drawn over live video / replay footage (PpCameraFrame). Unlike every other
    // token here it does NOT reskin per aesthetic: the overlay is an instrument
    // readout, not UI chrome, and must read the same against grass/sky in every
    // direction. Mirrors the constants in src/Video/video_overlay_pose.cpp.
    readonly property color poseBone:        "#74d0e6"   // bones, joint rings, head, dots
    readonly property color poseInk:         "#c8121a23" // ring fill (dark ink, ~200 alpha)
    // The dark backing behind text laid over video (camera tiles, camera frame, markup).
    readonly property color colorScrim:      Qt.alpha(poseInk, 0.6)
    readonly property color poseSpineTop:    "#d6f2ff"   // spine neck end + end tick + diamonds
    readonly property color poseSpineBottom: "#4aa6c4"   // spine pelvis end
    readonly property color poseSpineTick2:  "#6cc3dc"   // pelvis-end spine tick

    // Generic series palette — categorical hues for charts whose series are NOT catalogue
    // metrics (a metric is drawn in its own colour: metricColor() below). Distinct from the
    // colorImu* identity hues, which mean "this specific sensor"; these carry no fixed
    // meaning, they just need to read apart on each background. ~6 hues per aesthetic,
    // tuned light/dark; index with chartSeriesColor(i) to wrap safely.
    readonly property var chartSeries: _m.chartSeries
    function chartSeriesColor(i) {
        var p = chartSeries
        return p[((i % p.length) + p.length) % p.length]
    }

    // ── Metric palette ───────────────────────────────────────────────────────
    // Every metric is drawn in ONE named colour wherever it is plotted — clubheadSpeed is
    // cornflower, pelvis is crimson, chest is mint — so a curve is recognisable before its legend is
    // read. The NAME is the metric's (MetricDescriptor::color in the manifest); what each name looks
    // like is the theme's, tuned for that theme's background in light and dark. Unlike chartSeries,
    // which is indexed by position and carries no meaning, these are identities.
    //
    // The twelve names alternate a deeper and a lighter tier around the hue wheel, so two names that
    // neighbour in hue never also share a lightness — that is what keeps neighbours apart for a
    // colour-blind reader. Each theme's values keep the family (cornflower is blue in every theme;
    // only how saturated, how warm and how light changes) and clear 3:1 against colorSurface.
    // Generated in OKLCH and checked (contrast, normal and colour-blind separation of the names a
    // group plots together) by tools/theme/metric_palette.py, which also authors the manifest's
    // names; a user retunes per theme in Settings → Appearance (appSettings.metricPalette).
    //
    // Resolution, all of it in metricColor(key):
    //   appSettings.metricColors[key]  → the user re-pointed this metric at another name
    //   ChartMetrics.colorName(key)    → the catalogue's name
    //   a stable hash of the key       → a key the catalogue does not know still keeps ONE colour
    // and then the name through paletteColor(): the user's retune for this theme and mode, else the
    // value below.
    readonly property var _metricPalettes: ({
        "studio": {
            dark: { crimson: "#D86165", coral: "#FEAA7E", ochre: "#BB7E05", gold: "#D8C23A", moss: "#6D9D2D", mint: "#4FDEA3",
                   teal: "#09A0A0", sky: "#50D2FF", cornflower: "#5889E6", lavender: "#BDB7FE", violet: "#A570D1", orchid: "#FF99E2" },
            light: { crimson: "#972430", coral: "#C96222", ochre: "#734C02", gold: "#958403", moss: "#3E6102", mint: "#099B6A",
                    teal: "#036262", sky: "#0991B6", cornflower: "#224FA7", lavender: "#8071D7", violet: "#6C3794", orchid: "#BB5BA2" }
        },
        "instrument": {
            dark: { crimson: "#D06E69", coral: "#FFAE83", ochre: "#BC8225", gold: "#DCC45E", moss: "#7F9C3F", mint: "#7BDCA2",
                   teal: "#07A49F", sky: "#59D5FE", cornflower: "#688DDB", lavender: "#C5B9FE", violet: "#AA77C5", orchid: "#FDA3D8" },
            light: { crimson: "#8A2F2F", coral: "#BB6735", ochre: "#714901", gold: "#967F02", moss: "#465C01", mint: "#2F9660",
                    teal: "#035F5C", sky: "#078EB1", cornflower: "#2E4E97", lavender: "#8370C4", violet: "#6B3982", orchid: "#B35F93" }
        },
        "editorial": {
            dark: { crimson: "#D17273", coral: "#FEB38D", ochre: "#BD8630", gold: "#D9C968", moss: "#7AA14E", mint: "#78E0AF",
                   teal: "#05A7A7", sky: "#6BD7FF", cornflower: "#6991DC", lavender: "#C4BFFE", violet: "#A77DCB", orchid: "#FAA9E2" },
            light: { crimson: "#86262E", coral: "#BA622F", ochre: "#694501", gold: "#8F7E03", moss: "#385802", mint: "#079465",
                    teal: "#055959", sky: "#0A8AAE", cornflower: "#234993", lavender: "#7A6FC5", violet: "#613482", orchid: "#AE5C98" }
        },
        "vector": {
            dark: { crimson: "#ED4857", coral: "#FEAA7E", ochre: "#BB7E05", gold: "#DBC203", moss: "#689F01", mint: "#0FE39D",
                   teal: "#09A0A0", sky: "#50D2FF", cornflower: "#4785FF", lavender: "#BDB7FE", violet: "#AE62E7", orchid: "#FF99E2" },
            light: { crimson: "#A60129", coral: "#D35F03", ochre: "#774F02", gold: "#998704", moss: "#406503", mint: "#009F6C",
                    teal: "#026565", sky: "#0994BA", cornflower: "#114CBF", lavender: "#846CF0", violet: "#762AA7", orchid: "#CA4EAD" }
        },
        "terrain": {
            dark: { crimson: "#CE7069", coral: "#FEAF83", ochre: "#BB822E", gold: "#DDC363", moss: "#829B41", mint: "#82DBA1",
                   teal: "#04A59D", sky: "#5DD5FD", cornflower: "#6B8DD8", lavender: "#C7B8FE", violet: "#AB77C1", orchid: "#FCA4D5" },
            light: { crimson: "#852E2C", coral: "#B86839", ochre: "#6D4703", gold: "#977E0D", moss: "#465802", mint: "#3A955F",
                    teal: "#015C58", sky: "#0A8EB0", cornflower: "#2F4B91", lavender: "#8571C0", violet: "#68377C", orchid: "#B2618F" }
        },
        "links": {
            dark: { crimson: "#C56B68", coral: "#FAA77A", ochre: "#B27D2C", gold: "#D2BE62", moss: "#779544", mint: "#79D4A1",
                   teal: "#089D9A", sky: "#5ECEF4", cornflower: "#6488CF", lavender: "#BEB1FF", violet: "#A074BC", orchid: "#F1A0D3" },
            light: { crimson: "#802D2F", coral: "#B06337", ochre: "#694400", gold: "#8D7A11", moss: "#3E5700", mint: "#2F8E60",
                    teal: "#035957", sky: "#0A87A9", cornflower: "#2B498B", lavender: "#7B6DB9", violet: "#62377A", orchid: "#A85D8D" }
        },
        "folio": {
            dark: { crimson: "#D46568", coral: "#FEAA7E", ochre: "#BB7E05", gold: "#D6C24A", moss: "#6F9C37", mint: "#5CDDA5",
                   teal: "#09A0A0", sky: "#50D2FF", cornflower: "#5C8AE1", lavender: "#BDB7FE", violet: "#A473CD", orchid: "#FB9CE0" },
            light: { crimson: "#932A33", coral: "#C5652D", ochre: "#734C02", gold: "#958403", moss: "#3E6102", mint: "#099B6A",
                    teal: "#036262", sky: "#0991B6", cornflower: "#2750A2", lavender: "#8073D1", violet: "#6B3A90", orchid: "#B75FA0" }
        }
    })

    readonly property var metricColorNames: _metricCatalogue.colorNames()
    readonly property var _metricCatalogue: ChartMetrics {}
    readonly property string _metricMode: dark ? "dark" : "light"

    // The theme's own value for a name in the current theme and mode, before any user retune.
    function paletteDefault(name) {
        var t = _metricPalettes[aesthetic] || _metricPalettes["studio"]
        var v = t[_metricMode][name]
        return v !== undefined ? v : colorText3
    }
    // The key a retune of `name` in the current theme and mode is stored under.
    function paletteKey(name) { return aesthetic + "/" + _metricMode + "/" + name }
    // What `name` draws as right now: the user's retune if there is one, else the theme's value.
    function paletteColor(name) {
        var o = appSettings.metricPalette[paletteKey(name)]
        return (o !== undefined && o !== "") ? o : paletteDefault(name)
    }
    // The catalogue's name for a metric — or, for a key it does not know, a stable pick from the
    // palette, so an uncatalogued curve keeps one colour too.
    function metricDefaultColorName(key) {
        var n = _metricCatalogue.colorName(key)
        if (n !== "") return n
        var h = 0
        for (var i = 0; i < key.length; ++i) h = (h * 31 + key.charCodeAt(i)) % 104729
        return metricColorNames.length ? metricColorNames[h % metricColorNames.length] : ""
    }
    // The name a metric is drawn in: the user's choice, else the catalogue's.
    function metricColorName(key) {
        var o = appSettings.metricColors[key]
        return (o !== undefined && o !== "") ? o : metricDefaultColorName(key)
    }
    // THE call: the colour a metric (or a measure, through the metric it reads) is drawn in.
    function metricColor(key) { return paletteColor(metricColorName(key)) }

    // ── Font family tokens ───────────────────────────────────────────────────
    // Falls back to the system default if the font file is not installed.
    readonly property string fontBody: _a.fontBody
    readonly property string fontData: _a.fontData
    readonly property string fontDisplay: _a.fontDisplay
    // On Windows, Segoe UI Emoji intercepts symbol codepoints (e.g. ⚙ U+2699)
    // and renders them as large coloured emoji glyphs. Segoe UI Symbol has the
    // same characters as flat monochrome glyphs and prevents that fallback.
    // On macOS, Apple Color Emoji does the same — Apple Symbols provides flat
    // monochrome glyphs for those codepoints and wins the font-selection race.
    // Gelasio (Instrument), Fraunces (Terrain) and Literata (Links) are serifs that
    // read best at Normal for body — use Normal to avoid thin, silently-rounded body
    // text. Literata additionally only ships concrete static faces at 400/500 in this
    // build (macOS/CoreText won't interpolate the variable weight axis — see main.cpp),
    // so its body weight must stay at 400.
    readonly property int fontBodyWeight: _a.fontBodyWeight

    readonly property string fontSymbol: {
        if (Qt.platform.os === "windows") return "Segoe UI Symbol"
        if (Qt.platform.os === "osx")     return "Apple Symbols"
        return ""
    }

    // Per-glyph size compensation for symbol icons (rail buttons, home tiles).
    // At equal pixelSize the symbol glyphs have very different ink heights, so
    // they look mismatched side by side. The ratios are platform-specific
    // because fontSymbol resolves to a different font on each OS:
    //   Linux   — fontconfig fallback chain: most glyphs come from DejaVu Sans
    //             (ink height 75–78% of em) but ⌂ is small (60%) and ⌖ comes
    //             from FreeSerif as a thin crosshair (60%, reads even smaller).
    //             Factors measured via QFontMetricsF::tightBoundingRect and
    //             cross-checked against the hand-tuned home-tile sizes.
    //   Windows — Segoe UI Symbol: a single pinned font with no fallback (all
    //             eight glyphs resolve in-font), but its ink heights still span
    //             0.677–0.785 em — even the geometric shapes disagree (▶ is the
    //             tallest, ◈ among the smallest), so identity left them visibly
    //             mismatched. Factors normalise every glyph to the geometric-
    //             shape mean ink height, measured on-device via
    //             QFontMetricsF::tightBoundingRect and cross-checked by render.
    //   macOS   — Apple Symbols: internally consistent metrics, identity
    //             until tuned on a Mac.
    // Factors are relative to the geometric-shape glyphs (◑ ▶ ◈) = 1.0 (their
    // mean, where the shapes themselves differ); any glyph absent from the
    // active table renders unscaled.
    readonly property var symbolScaleLinux:   ({ "⌂": 1.30, "⌖": 1.47, "⇅": 1.06, "✦": 1.03, "⚙": 1.04 })
    readonly property var symbolScaleWindows: ({ "◑": 1.02, "▶": 0.94, "◈": 1.05, "⌂": 1.03, "⌖": 1.05, "⇅": 1.09, "✦": 1.05 })
    readonly property var symbolScaleMac:     ({ })
    function symbolScale(glyph) {
        var table = Qt.platform.os === "windows" ? symbolScaleWindows
                  : Qt.platform.os === "osx"     ? symbolScaleMac
                  :                                symbolScaleLinux
        var s = table[glyph]
        return s === undefined ? 1.0 : s
    }

    // ── Typography scale tokens ──────────────────────────────────────────────
    readonly property int  fontSzDisplay: Math.round(_a.displayBase * fontScale)
    // The one title bigger than display: the home screen's "PinPoint Studio". Twice display in the
    // original aesthetics; a theme may set its own `heroSize` (Folio keeps it calm at 34).
    readonly property int  fontSzHero: _v("heroSize", 0) > 0 ? Math.round(_v("heroSize", 0) * fontScale) : fontSzDisplay * 2
    readonly property bool fontDisplayItalic: _a.fontDisplayItalic
    readonly property int  fontDisplayWeight: _a.fontDisplayWeight

    // The launch monitor board's headline figure. Larger than fontSzData because that
    // board is read from a few feet away, standing over the ball, and its whole job is
    // to be legible at a glance — the tile has room for it precisely because it carries
    // no corridor, no verdict and no trace.
    readonly property int  fontSzDataLg:  Math.round(26 * fontScale)
    readonly property int  fontSzData:    Math.round(20 * fontScale)
    readonly property int  fontSzDataSm:  Math.round(13 * fontScale)
    readonly property int  fontSzMicro:   Math.round(_v("microSize", 10) * fontScale)
    readonly property int  fontSzHeading: Math.round(16 * fontScale)
    readonly property int  fontSzBody:    Math.round(13 * fontScale)
    readonly property int  fontSzBody2:   Math.round(12 * fontScale)
    readonly property int  fontSzLabel:   Math.round(11 * fontScale)

    // ── Letter-spacing tokens (px) ───────────────────────────────────────────
    // Upper-case tracked labels want air between the capitals; Folio's sentence-case sans does not.
    readonly property real trackingMicro:  _v("trackingMicro", 0.8)
    readonly property real trackingLabel:  _v("trackingLabel", 0.6)
    readonly property real trackingData:   _v("trackingData", 0.3)
    readonly property real trackingNormal: 0.0

    // ── Heading case ─────────────────────────────────────────────────────────
    // Small headings (YOUR FOCUS, CHARTS, SESSIONS) are written in SENTENCE case in the source and
    // set in capitals by the theme: caps(s) for a string, capsFont for a Text's font.capitalization.
    // Every original aesthetic sets them in capitals, exactly as before; Folio leaves them as
    // written. Acronyms stay in capitals in the source ("IMU", "P1"), so they read right in both.
    readonly property bool capsHeadings: _v("capsHeadings", true)
    readonly property int  capsFont: capsHeadings ? Font.AllUppercase : Font.MixedCase
    function caps(s) { return capsHeadings ? String(s).toUpperCase() : String(s) }

    // ── Geometry tokens ──────────────────────────────────────────────────────
    readonly property int railWidth: _a.railWidth
    readonly property int sidenavWidth:    sp(275)
    function contentWidth(availableWidth) {
        return Math.max(Math.round(availableWidth * 0.7), sp(800))
    }

    // Golf handicap display helper.
    // h <= -99 is the "not set" sentinel (stored as -999.0); undefined/NaN also treated as not set.
    // Negative values that are not the sentinel are plus handicaps: -2 → "+2 hcp".
    function formatHandicap(h) {
        if (h === undefined || h === null || isNaN(h) || h <= -99) return qsTr("no hcp")
        if (h === 0) return qsTr("Scratch")
        if (h > 0)   return Math.round(h) + " hcp"
        return "+" + Math.round(-h) + " hcp"
    }

    // Shot-quality presentation helpers (PpQualityPill, PpShotFilter).
    // Quartile → quality colour ramp (red → rust → amber → green).
    function qualityColor(score) {
        if (score < 25) return colorError
        if (score < 50) return colorWarn
        if (score < 75) return colorAttention
        return colorGood
    }
    // The shape that goes with qualityColor (PpBadge kind), so a score's band is never colour
    // alone: the top band is a tick, everything below it a target to work on.
    function qualityMark(score) {
        return score >= 75 ? "check" : "target"
    }
    function qualityColorLight(score) {   // unselected filter-chip tint
        if (score < 25) return colorErrorLight
        if (score < 50) return colorWarnLight
        if (score < 75) return colorAttentionLight
        return colorGoodLight
    }
    // A judged reading's verdict (charts, chart summary, chart key). Either spelling of a band is
    // read — the chart's good / attention / warn, the scorer's green / yellow / red — and anything
    // else is no verdict, never a pass.
    function bandVerdict(b) {
        return (b === "good" || b === "green")                          ? "good"
             : (b === "attention" || b === "yellow" || b === "amber")   ? "attention"
             : (b === "warn" || b === "red")                            ? "warn"
             :                                                            ""
    }
    // The verdict's colour. No verdict is `none`, which defaults to the quiet grey of a dot that
    // only marks a position; a reading in running text passes colorText instead.
    function verdictColor(v, none) {
        return v === "good"      ? colorGood
             : v === "attention" ? colorAttention
             : v === "warn"      ? colorWarn
             : (none !== undefined ? none : colorText3)
    }
    // ...and its words, so a verdict is never told by colour alone.
    function verdictWords(v) {
        return v === "good"      ? qsTr("in range")
             : v === "attention" ? qsTr("watch")
             : v === "warn"      ? qsTr("outside")
             :                     ""
    }
    // Corridor bands (ideal / good / watch / action), wherever a corridor is drawn: the corridor
    // strip, the value run, the session history and the model editor's plot. The status family —
    // green inside, amber where it fires, the faults' coral past the fault line, never the alarm
    // red (13.2). Good is the Ideal green at half strength: inside the corridor, not at its centre.
    // The FILL is shaded low enough that a dot of the same family still reads on top of it.
    function corridorFill(grade) {
        if (grade === "ideal")  return Qt.alpha(colorGood, 0.20)
        if (grade === "good")   return Qt.alpha(colorGood, 0.09)
        if (grade === "watch")  return Qt.alpha(colorAttention, 0.20)
        if (grade === "action") return Qt.alpha(colorWarn, 0.16)
        return "transparent"   // `open`: the side a norm does not grade is background
    }
    // The band's own colour at full strength, for a swatch that names it.
    function corridorTone(grade) {
        if (grade === "ideal")  return colorGood
        if (grade === "good")   return Qt.alpha(colorGood, 0.5)
        if (grade === "watch")  return colorAttention
        if (grade === "action") return colorWarn
        return colorText3
    }
    function corridorWord(grade) {
        return grade === "ideal"  ? caps(qsTr("Ideal"))
             : grade === "good"   ? caps(qsTr("Good"))
             : grade === "watch"  ? caps(qsTr("Watch"))
             : grade === "action" ? caps(qsTr("Action"))
             :                      ""
    }
    // Session-diagnostics strength ramp (PpStrengthMeter): the 0..5 step a reading sits at,
    // off diagnostic_ledger.h's severityLevel(), to the status family the rest of the panel
    // already uses for the same reading. Green inside the corridor, amber across its edge,
    // orange-red and then red out beyond it — and the meter pairs it with a bar COUNT, so
    // colour is never the only channel.
    function strengthColor(level) {
        if (level <= 1) return colorGood        // 0 dead centre, 1 inside the band
        if (level <= 3) return colorAttention   // 2 at the edge, 3 outside it
        if (level <= 4) return colorWarn        // well outside
        return colorError                       // far outside
    }

    // Band ranges for the filter chips, low→high.
    readonly property var qualityBands: [
        { lo: 0,  hi: 24,  label: "0–24"   },
        { lo: 25, hi: 49,  label: "25–49"  },
        { lo: 50, hi: 74,  label: "50–74"  },
        { lo: 75, hi: 100, label: "75–100" }
    ]
    readonly property int headerHeight:    40
    readonly property int carouselHeight:  sp(116)
    readonly property int statusBarHeight: 36
    readonly property int radius: _a.radius
    readonly property int radiusLg: _a.radiusLg

    // ── Spacing ──────────────────────────────────────────────────────────────
    // How much more room this theme gives the space between things (gap()), not their size.
    readonly property real spacingScale: _v("spacingScale", 1.0)

    // ── Cards (design system §13.3–13.4) ─────────────────────────────────────
    // cardRule / heroRule: the tone rule across a card's top, px (0 = none — the tone then lives
    // in the title and the marks alone). heroTinted: the hero leans to its tone (a wash and a
    // tone hairline); untinted, it is a card like the rest and only its headline sets it apart.
    readonly property int  cardRule:   _v("cardRule", 3)
    readonly property int  heroRule:   _v("heroRule", 4)
    readonly property bool heroTinted: _v("heroTinted", true)

    // ── Charts ───────────────────────────────────────────────────────────────
    // The plot's furniture, kept recessive so the data reads first. The original charts each chose
    // their own grid colour and opacity, so these are PASS-THROUGHS: a chart hands in what it drew
    // before, and gets that back in every original aesthetic — value-identical by construction —
    // or the theme's own value where the theme sets one (Folio: opaque hairlines in the chart
    // greys, chartGrid / chartBaseline in its light and dark blocks).
    //   gridColor(c), gridOpacity(o)          — interior grid lines
    //   baselineColor(c), baselineOpacity(o)  — the zero line / axis line
    //   curveWidth(w)                         — a series' stroke, in sp units (pass the 2 of sp(2))
    // chartFrame: draw the box round the plot area. chartMarkerHollow: a point is a ring in the
    // series colour around a colorSurface fill, rather than a solid dot.
    function gridColor(c)       { return _v("chartGrid", c) }
    function gridOpacity(o)     { return _v("chartGridOpacity", o) }
    function baselineColor(c)   { return _v("chartBaseline", c) }
    function baselineOpacity(o) { return _v("chartBaselineOpacity", o) }
    function curveWidth(w)      { return _v("chartCurveWidth", w) }
    readonly property bool chartFrame:        _v("chartFrame", true)
    readonly property bool chartMarkerHollow: _v("chartMarkerHollow", false)

    // ── App chrome ───────────────────────────────────────────────────────────
    // The nav rail's and the session toolbar's ground, and how the rail marks the active page:
    // a filled box with a hairline, or (railActiveBar) an accent tint with a 2 px bar at its edge.
    readonly property color colorRail:     _v("railTone", "bg") === "bg2" ? colorBg2 : colorBg
    readonly property color colorToolbar:  _v("toolbarTone", "surface") === "bg2" ? colorBg2 : colorSurface
    readonly property bool  railActiveBar: _v("railActiveBar", false)

    // ── Border tokens ────────────────────────────────────────────────────────
    readonly property real borderWidth:         1
    readonly property real borderOpacityNormal: 0.5
    readonly property real borderOpacityStrong: 0.75

    // ── Animation duration tokens (ms) ───────────────────────────────────────
    readonly property int durationFast:   reduceMotion ? 0 : 120
    readonly property int durationNormal: reduceMotion ? 0 : 220
    readonly property int durationSlow:   reduceMotion ? 0 : 350
}
