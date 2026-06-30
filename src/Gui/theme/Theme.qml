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
// Set aesthetic ("studio" | "instrument" | "editorial" | "vector" | "terrain")
// and dark (bool) to switch the entire UI. Never hardcode colours, fonts, or
// sizes in components.
//
// Each token is computed inline to avoid nested-QtObject initialization-order
// issues that cause "Unable to assign [undefined]" warnings.
QtObject {
    id: root

    // Theme cycle index: 0–9 maps to instrument-light, instrument-dark,
    // editorial-light, editorial-dark, studio-light, studio-dark,
    // vector-light, vector-dark, terrain-light, terrain-dark.
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

    function cycleTheme() { themeIndex = (themeIndex + 1) % 10 }

    // Scale-aware pixel helper — use Theme.sp(n) instead of hardcoded pixel values
    // so all dimensions respond to the user's text size preference.
    function sp(n) { return Math.round(n * fontScale) }

    // Active aesthetic and mode — derived from themeIndex.
    // Can also be set directly to bind to C++ AppSettings at startup.
    readonly property string aesthetic: ["instrument","instrument","editorial","editorial","studio","studio","vector","vector","terrain","terrain"][themeIndex]
    readonly property bool   dark:      (themeIndex % 2) === 1

    // ── Colour tokens ────────────────────────────────────────────────────────

    readonly property color colorBg: {
        if (aesthetic === "instrument") return dark ? "#05080A" : "#F4EFE3"
        if (aesthetic === "editorial")  return dark ? "#141412" : "#FAFAF8"
        if (aesthetic === "vector")     return dark ? "#0A0B0D" : "#F0F1F4"
        if (aesthetic === "terrain")    return dark ? "#080D0A" : "#F2F4EC"
        return dark ? "#111110" : "#F6F6F5"
    }
    readonly property color colorBg2: {
        if (aesthetic === "instrument") return dark ? "#0A1115" : "#ECE6D7"
        if (aesthetic === "editorial")  return dark ? "#1C1C1A" : "#F2F1ED"
        if (aesthetic === "vector")     return dark ? "#0F1114" : "#E4E6EB"
        if (aesthetic === "terrain")    return dark ? "#0D140F" : "#E8EBDF"
        return dark ? "#191918" : "#EEEEED"
    }
    readonly property color colorBg3: {
        if (aesthetic === "instrument") return dark ? "#111B20" : "#E1DACA"
        if (aesthetic === "editorial")  return dark ? "#242422" : "#E8E7E2"
        if (aesthetic === "vector")     return dark ? "#151719" : "#D8DBE3"
        if (aesthetic === "terrain")    return dark ? "#121B15" : "#DCE1D1"
        return dark ? "#212120" : "#E4E4E3"
    }
    readonly property color colorSurface: {
        if (aesthetic === "instrument") return dark ? "#0A0F13" : "#FBF8F0"
        if (aesthetic === "editorial")  return dark ? "#181816" : "#FFFFFF"
        if (aesthetic === "vector")     return dark ? "#13151A" : "#FAFBFC"
        if (aesthetic === "terrain")    return dark ? "#0B110D" : "#FAFBF5"
        return dark ? "#161615" : "#FAFAF9"
    }

    // Borders — 1 px at these opacities simulate 0.5 px
    readonly property color colorBorder: {
        if (aesthetic === "instrument") return dark ? "#1ff2ede2" : "#1f3a322a"
        if (aesthetic === "editorial")  return dark ? "#12fffff5" : "#12000000"
        if (aesthetic === "vector")     return dark ? "#0fffffff" : "#12000000"
        if (aesthetic === "terrain")    return dark ? "#12eef3ec" : "#1227392b"
        return dark ? "#0effffff" : "#0f000000"
    }
    readonly property color colorBorderMid: {
        if (aesthetic === "instrument") return dark ? "#2bf2ede2" : "#2b3a322a"
        if (aesthetic === "editorial")  return dark ? "#1cfffff5" : "#1a000000"
        if (aesthetic === "vector")     return dark ? "#1affffff" : "#1c000000"
        if (aesthetic === "terrain")    return dark ? "#1ceef3ec" : "#1c27392b"
        return dark ? "#17ffffff" : "#19000000"
    }
    readonly property color colorBorderStrong: {
        if (aesthetic === "instrument") return dark ? "#38f2ede2" : "#383a322a"
        if (aesthetic === "editorial")  return dark ? "#26fffff5" : "#21000000"
        if (aesthetic === "vector")     return dark ? "#2effffff" : "#2e000000"
        if (aesthetic === "terrain")    return dark ? "#2aeef3ec" : "#2a27392b"
        return dark ? "#21ffffff" : "#26000000"
    }

    // Text
    readonly property color colorText: {
        if (aesthetic === "instrument") return dark ? "#F2EDE2" : "#0A1115"
        if (aesthetic === "editorial")  return dark ? "#F0EFE8" : "#111110"
        if (aesthetic === "vector")     return dark ? "#E8EAF0" : "#0A0B10"
        if (aesthetic === "terrain")    return dark ? "#ECF3EC" : "#16221A"
        return dark ? "#EDEDEC" : "#0A0A09"
    }
    readonly property color colorText2: {
        if (aesthetic === "instrument") return dark ? "#CFD3CC" : "#5A5246"
        if (aesthetic === "editorial")  return dark ? "#9A9A92" : "#4A4A47"
        if (aesthetic === "vector")     return dark ? "#8B90A0" : "#4A4E5E"
        if (aesthetic === "terrain")    return dark ? "#AEB9AD" : "#47554B"
        return dark ? "#878786" : "#525251"
    }
    readonly property color colorText3: {
        if (aesthetic === "instrument") return dark ? "#94A09E" : "#998F82"
        if (aesthetic === "editorial")  return dark ? "#5A5A55" : "#9B9B97"
        if (aesthetic === "vector")     return dark ? "#484E5E" : "#9098B0"
        if (aesthetic === "terrain")    return dark ? "#5C685D" : "#8C988D"
        return dark ? "#4A4A48" : "#ABABAA"
    }

    // Accent
    readonly property color colorAccent: {
        if (aesthetic === "instrument") return dark ? "#E6AC54" : "#9A5E12"
        if (aesthetic === "editorial")  return dark ? "#A8C4E0" : "#1A3A5C"
        if (aesthetic === "vector")     return dark ? "#FF5500" : "#CC3300"
        if (aesthetic === "terrain")    return dark ? "#4FCB8C" : "#1E7A4E"
        return dark ? "#4D90FF" : "#0066FF"
    }
    readonly property color colorAccentLight: {
        if (aesthetic === "instrument") return dark ? "#17e6ac54" : "#149a5e12"
        if (aesthetic === "editorial")  return dark ? "#12a8c4e0" : "#0f1a3a5c"
        if (aesthetic === "vector")     return dark ? "#12ff5500" : "#0fcc3300"
        if (aesthetic === "terrain")    return dark ? "#174fcb8c" : "#0f1e7a4e"
        return dark ? "#124d90ff" : "#0d0066ff"
    }
    readonly property color colorAccentMid: {
        if (aesthetic === "instrument") return dark ? "#29e6ac54" : "#2e9a5e12"
        if (aesthetic === "editorial")  return dark ? "#24a8c4e0" : "#241a3a5c"
        if (aesthetic === "vector")     return dark ? "#24ff5500" : "#24cc3300"
        if (aesthetic === "terrain")    return dark ? "#294fcb8c" : "#241e7a4e"
        return dark ? "#264d90ff" : "#1f0066ff"
    }

    // ── Brand gradient tokens ────────────────────────────────────────────────
    // Signature warm→cool sweep from the marketing site: pelvis-amber through
    // light-amber to club-cyan — the kinematic-sequence colours. Used for large
    // display titles (via PpDisplayText) and accent bars. Each aesthetic supplies
    // its own warm/cool pair so the sweep feels native; instrument matches the
    // site 1:1. Light variants are deepened to hold contrast on pale grounds.
    readonly property color gradientWarm: {
        if (aesthetic === "instrument") return dark ? "#E6AC54" : "#B5701A"
        if (aesthetic === "editorial")  return dark ? "#E6C25A" : "#8A6A14"
        if (aesthetic === "vector")     return dark ? "#FF8C35" : "#B85A00"
        if (aesthetic === "terrain")    return dark ? "#E0C766" : "#8A6A1E"
        return dark ? "#F5C451" : "#9C6F12"   // studio
    }
    readonly property color gradientWarmLit: {
        if (aesthetic === "instrument") return dark ? "#F3C987" : "#CC9A45"
        if (aesthetic === "editorial")  return dark ? "#EBD089" : "#A88A3A"
        if (aesthetic === "vector")     return dark ? "#FFB066" : "#D67A2A"
        if (aesthetic === "terrain")    return dark ? "#A8C96A" : "#5E8A3A"
        return dark ? "#F8D484" : "#C29545"   // studio
    }
    readonly property color gradientCool: {
        if (aesthetic === "instrument") return dark ? "#7BC0DB" : "#2B6E7A"
        if (aesthetic === "editorial")  return dark ? "#A8C4E0" : "#1A3A5C"
        if (aesthetic === "vector")     return dark ? "#3399FF" : "#0066CC"
        if (aesthetic === "terrain")    return dark ? "#3EC79A" : "#1C7A5A"
        return dark ? "#4D90FF" : "#0066FF"   // studio
    }

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
    // gradientTitlesActive folds in reduceMotion so callers test one flag.
    property bool gradientTitles: true
    onGradientTitlesChanged: appSettings.gradientTitles = gradientTitles
    readonly property bool gradientTitlesActive: gradientTitles && !reduceMotion

    // Good (success / go / connected)
    readonly property color colorGood: {
        if (aesthetic === "instrument") return dark ? "#8AB389" : "#357058"
        if (aesthetic === "editorial")  return dark ? "#8ABFA0" : "#1A4A2E"
        if (aesthetic === "vector")     return dark ? "#2EE8A0" : "#006B45"
        if (aesthetic === "terrain")    return dark ? "#86C49A" : "#3A7A56"
        return dark ? "#30C983" : "#0A7A4A"
    }
    readonly property color colorGoodLight: {
        if (aesthetic === "instrument") return dark ? "#178ab389" : "#14357058"
        if (aesthetic === "editorial")  return dark ? "#128abfa0" : "#121a4a2e"
        if (aesthetic === "vector")     return dark ? "#122ee8a0" : "#0f006b45"
        if (aesthetic === "terrain")    return dark ? "#1786c49a" : "#0f3a7a56"
        return dark ? "#1230c983" : "#0f0a7a4a"
    }

    // Warn (caution / unexpected)
    readonly property color colorWarn: {
        if (aesthetic === "instrument") return dark ? "#E07E64" : "#A8482A"
        if (aesthetic === "editorial")  return dark ? "#D4896A" : "#8B2500"
        if (aesthetic === "vector")     return dark ? "#FF8C35" : "#7A3800"
        if (aesthetic === "terrain")    return dark ? "#E6915A" : "#A8531E"
        return dark ? "#FF6B35" : "#D94A00"
    }
    readonly property color colorWarnLight: {
        if (aesthetic === "instrument") return dark ? "#17e07e64" : "#14a8482a"
        if (aesthetic === "editorial")  return dark ? "#12d4896a" : "#0f8b2500"
        if (aesthetic === "vector")     return dark ? "#14ff8c35" : "#0f7a3800"
        if (aesthetic === "terrain")    return dark ? "#17e6915a" : "#0fa8531e"
        return dark ? "#14ff6b35" : "#0fd94a00"
    }

    // Error (critical failure / fatal — distinctly red, darker than warn)
    readonly property color colorError: {
        if (aesthetic === "instrument") return dark ? "#E0595B" : "#9C2A2A"
        if (aesthetic === "editorial")  return dark ? "#C46868" : "#8B1E1E"
        if (aesthetic === "vector")     return dark ? "#FF4455" : "#8B0014"
        if (aesthetic === "terrain")    return dark ? "#E66B66" : "#A82E2A"
        return dark ? "#FF5555" : "#CC2000"
    }
    readonly property color colorErrorLight: {
        if (aesthetic === "instrument") return dark ? "#17e0595b" : "#149c2a2a"
        if (aesthetic === "editorial")  return dark ? "#12c46868" : "#0f8b1e1e"
        if (aesthetic === "vector")     return dark ? "#14ff4455" : "#0f8b0014"
        if (aesthetic === "terrain")    return dark ? "#17e66b66" : "#0fa82e2a"
        return dark ? "#14ff5555" : "#0fcc2000"
    }

    // Attention (call-to-action framing — draws the eye to a row/control that
    // needs the user to act, e.g. an uncalibrated sensor). Distinct from colorWarn
    // (orange-red caution) and colorError (red failure): this is a confident amber
    // "do this next" frame. Strong variant = border/text, Light variant = fill.
    readonly property color colorAttention: {
        if (aesthetic === "instrument") return dark ? "#F2C84A" : "#8A6612"
        if (aesthetic === "editorial")  return dark ? "#E6C25A" : "#8A6A14"
        if (aesthetic === "vector")     return dark ? "#FFD60A" : "#B58900"
        if (aesthetic === "terrain")    return dark ? "#E0C24A" : "#8A6612"
        return dark ? "#F5C451" : "#9C6F12"
    }
    readonly property color colorAttentionLight: {
        if (aesthetic === "instrument") return dark ? "#17f2c84a" : "#148a6612"
        if (aesthetic === "editorial")  return dark ? "#12e6c25a" : "#0f8a6a14"
        if (aesthetic === "vector")     return dark ? "#14ffd60a" : "#0fb58900"
        if (aesthetic === "terrain")    return dark ? "#17e0c24a" : "#0f8a6612"
        return dark ? "#14f5c451" : "#0f9c6f12"
    }

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
    readonly property color colorBandGreen: {
        if (aesthetic === "instrument") return dark ? "#248ab389" : "#24357058"
        if (aesthetic === "editorial")  return dark ? "#248abfa0" : "#241a4a2e"
        if (aesthetic === "vector")     return dark ? "#242ee8a0" : "#24006b45"
        if (aesthetic === "terrain")    return dark ? "#2486c49a" : "#243a7a56"
        return dark ? "#2430c983" : "#240a7a4a"
    }
    readonly property color colorBandAmber: {
        if (aesthetic === "instrument") return dark ? "#24f2c84a" : "#248a6612"
        if (aesthetic === "editorial")  return dark ? "#24e6c25a" : "#248a6a14"
        if (aesthetic === "vector")     return dark ? "#24ffd60a" : "#24b58900"
        if (aesthetic === "terrain")    return dark ? "#24e0c24a" : "#248a6612"
        return dark ? "#24f5c451" : "#249c6f12"
    }

    // IMU device-identity colours — A/B/C/D. Fixed hues (red / yellow / green /
    // blue) so a given sensor's colour is consistent across all aesthetics; only
    // brightness shifts for dark vs light backgrounds. Used by the 3D orientation
    // markers and IMU-related UI.
    readonly property color colorImuA: dark ? "#FF3B30" : "#D32F2F"   // red
    readonly property color colorImuB: dark ? "#FFD60A" : "#E0A400"   // yellow
    readonly property color colorImuC: dark ? "#34C759" : "#2E9E4F"   // green
    readonly property color colorImuD: dark ? "#3399FF" : "#0A6CFF"   // blue

    // Generic metric-series palette — categorical hues for charts that plot several
    // unrelated metrics together (PpMetricChart and future charts). Distinct from the
    // colorImu* identity hues, which mean "this specific sensor"; these carry no fixed
    // meaning, they just need to read apart on each background. ~6 hues per aesthetic,
    // tuned light/dark; index with chartSeriesColor(i) to wrap safely.
    readonly property var chartSeries: {
        if (aesthetic === "instrument") return dark
            ? ["#E6AC54", "#E07E64", "#8AB389", "#7BC0DB", "#B79AD6", "#E08FA8"]
            : ["#B5701A", "#A8482A", "#3E7E68", "#2B6E7A", "#6B4E8C", "#A0405A"]
        if (aesthetic === "editorial")  return dark
            ? ["#A8C4E0", "#8ABFA0", "#D8A06A", "#C99AC8", "#E6C25A", "#D98FA0"]
            : ["#1A3A5C", "#3E7E68", "#A85A2A", "#7A3B6E", "#8A6A14", "#8B2E45"]
        if (aesthetic === "vector")     return dark
            ? ["#FF5500", "#3399FF", "#2EE8A0", "#FFD60A", "#B266FF", "#FF4081"]
            : ["#CC3300", "#0066CC", "#006B45", "#B58900", "#7A29CC", "#C2185B"]
        if (aesthetic === "terrain")    return dark
            ? ["#4FCB8C", "#E0C24A", "#E6915A", "#5AB6C9", "#B79AD6", "#E6849A"]
            : ["#1E7A4E", "#8A6612", "#A8531E", "#2B7E8C", "#6B4E8C", "#A0405A"]
        return dark   // studio (default)
            ? ["#4D90FF", "#30C983", "#FF6B6B", "#F5C451", "#B79AF5", "#3FC2E0"]
            : ["#0066FF", "#0A7A4A", "#D64545", "#9C6F12", "#7A4FCF", "#0E7C9C"]
    }
    function chartSeriesColor(i) {
        var p = chartSeries
        return p[((i % p.length) + p.length) % p.length]
    }

    // ── Font family tokens ───────────────────────────────────────────────────
    // Falls back to the system default if the font file is not installed.
    readonly property string fontBody: {
        if (aesthetic === "instrument") return "Georgia"
        if (aesthetic === "editorial")  return "Instrument Sans"
        if (aesthetic === "vector")     return "Space Grotesk"
        if (aesthetic === "terrain")    return "Fraunces"
        return "Geist"
    }
    readonly property string fontData: {
        if (aesthetic === "instrument") return "DM Mono"
        if (aesthetic === "editorial")  return "JetBrains Mono"
        if (aesthetic === "vector")     return "Space Mono"
        if (aesthetic === "terrain")    return "DM Mono"
        return "Geist Mono"
    }
    readonly property string fontDisplay: {
        if (aesthetic === "instrument") return "Georgia"
        if (aesthetic === "editorial")  return "Playfair Display"
        if (aesthetic === "vector")     return "Space Mono"
        if (aesthetic === "terrain")    return "Fraunces"
        return "Geist"
    }
    // On Windows, Segoe UI Emoji intercepts symbol codepoints (e.g. ⚙ U+2699)
    // and renders them as large coloured emoji glyphs. Segoe UI Symbol has the
    // same characters as flat monochrome glyphs and prevents that fallback.
    // On macOS, Apple Color Emoji does the same — Apple Symbols provides flat
    // monochrome glyphs for those codepoints and wins the font-selection race.
    // Georgia (Instrument fontBody) and Fraunces (Terrain fontBody) are serifs with
    // no usable Light — use Normal to avoid thin, silently-rounded body text.
    readonly property int fontBodyWeight: (aesthetic === "instrument" || aesthetic === "terrain") ? Font.Normal : Font.Light

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
    readonly property int  fontSzDisplay: {
        var base = aesthetic === "instrument" ? 26
                 : aesthetic === "editorial"  ? 34
                 : aesthetic === "vector"     ? 24
                 : aesthetic === "terrain"    ? 28
                 : 22
        return Math.round(base * fontScale)
    }
    readonly property bool fontDisplayItalic: aesthetic === "editorial"
    readonly property int  fontDisplayWeight: aesthetic === "instrument" ? Font.Bold
                                            : aesthetic === "terrain"    ? Font.DemiBold
                                            : Font.Normal

    readonly property int  fontSzData:    Math.round(20 * fontScale)
    readonly property int  fontSzDataSm:  Math.round(13 * fontScale)
    readonly property int  fontSzMicro:   Math.round(10 * fontScale)
    readonly property int  fontSzHeading: Math.round(16 * fontScale)
    readonly property int  fontSzBody:    Math.round(13 * fontScale)
    readonly property int  fontSzBody2:   Math.round(12 * fontScale)
    readonly property int  fontSzLabel:   Math.round(11 * fontScale)

    // ── Letter-spacing tokens (px) ───────────────────────────────────────────
    readonly property real trackingMicro:  0.8
    readonly property real trackingLabel:  0.6
    readonly property real trackingData:   0.3
    readonly property real trackingNormal: 0.0

    // ── Geometry tokens ──────────────────────────────────────────────────────
    readonly property int railWidth: {
        if (aesthetic === "instrument") return 56
        if (aesthetic === "editorial")  return 58
        if (aesthetic === "vector")     return 52
        if (aesthetic === "terrain")    return 56
        return 52
    }
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
    function qualityColorLight(score) {   // unselected filter-chip tint
        if (score < 25) return colorErrorLight
        if (score < 50) return colorWarnLight
        if (score < 75) return colorAttentionLight
        return colorGoodLight
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
    readonly property int radius: {
        if (aesthetic === "instrument") return 6
        if (aesthetic === "editorial")  return 3
        if (aesthetic === "vector")     return 0
        if (aesthetic === "terrain")    return 8
        return 5
    }
    readonly property int radiusLg: {
        if (aesthetic === "instrument") return 10
        if (aesthetic === "editorial")  return 6
        if (aesthetic === "vector")     return 0
        if (aesthetic === "terrain")    return 12
        return 8
    }

    // ── Border tokens ────────────────────────────────────────────────────────
    readonly property real borderWidth:         1
    readonly property real borderOpacityNormal: 0.5
    readonly property real borderOpacityStrong: 0.75

    // ── Animation duration tokens (ms) ───────────────────────────────────────
    readonly property int durationFast:   reduceMotion ? 0 : 120
    readonly property int durationNormal: reduceMotion ? 0 : 220
    readonly property int durationSlow:   reduceMotion ? 0 : 350
}
