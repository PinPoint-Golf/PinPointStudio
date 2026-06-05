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
// Set aesthetic ("studio" | "instrument" | "editorial") and dark (bool)
// to switch the entire UI. Never hardcode colours, fonts, or sizes in components.
//
// Each token is computed inline to avoid nested-QtObject initialization-order
// issues that cause "Unable to assign [undefined]" warnings.
QtObject {
    id: root

    // Theme cycle index: 0–7 maps to instrument-light, instrument-dark,
    // editorial-light, editorial-dark, studio-light, studio-dark,
    // vector-light, vector-dark.
    property int themeIndex: 0

    Component.onCompleted: {
        themeIndex     = appSettings.themeIndex
        density        = appSettings.density
        reduceMotion   = appSettings.reduceMotion
        overlayOpacity = appSettings.overlayOpacity
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

    function cycleTheme() { themeIndex = (themeIndex + 1) % 8 }

    // Scale-aware pixel helper — use Theme.sp(n) instead of hardcoded pixel values
    // so all dimensions respond to the user's text size preference.
    function sp(n) { return Math.round(n * fontScale) }

    // Active aesthetic and mode — derived from themeIndex.
    // Can also be set directly to bind to C++ AppSettings at startup.
    readonly property string aesthetic: ["instrument","instrument","editorial","editorial","studio","studio","vector","vector"][themeIndex]
    readonly property bool   dark:      (themeIndex % 2) === 1

    // ── Colour tokens ────────────────────────────────────────────────────────

    readonly property color colorBg: {
        if (aesthetic === "instrument") return dark ? "#161412" : "#F5F2ED"
        if (aesthetic === "editorial")  return dark ? "#141412" : "#FAFAF8"
        if (aesthetic === "vector")     return dark ? "#0A0B0D" : "#F0F1F4"
        return dark ? "#111110" : "#F6F6F5"
    }
    readonly property color colorBg2: {
        if (aesthetic === "instrument") return dark ? "#1E1C19" : "#EDE9E2"
        if (aesthetic === "editorial")  return dark ? "#1C1C1A" : "#F2F1ED"
        if (aesthetic === "vector")     return dark ? "#0F1114" : "#E4E6EB"
        return dark ? "#191918" : "#EEEEED"
    }
    readonly property color colorBg3: {
        if (aesthetic === "instrument") return dark ? "#252220" : "#E4DFD6"
        if (aesthetic === "editorial")  return dark ? "#242422" : "#E8E7E2"
        if (aesthetic === "vector")     return dark ? "#151719" : "#D8DBE3"
        return dark ? "#212120" : "#E4E4E3"
    }
    readonly property color colorSurface: {
        if (aesthetic === "instrument") return dark ? "#1A1816" : "#FDFAF6"
        if (aesthetic === "editorial")  return dark ? "#181816" : "#FFFFFF"
        if (aesthetic === "vector")     return dark ? "#13151A" : "#FAFBFC"
        return dark ? "#161615" : "#FAFAF9"
    }

    // Borders — 1 px at these opacities simulate 0.5 px
    readonly property color colorBorder: {
        if (aesthetic === "instrument") return dark ? "#14fff8eb" : "#1f504637"
        if (aesthetic === "editorial")  return dark ? "#12fffff5" : "#12000000"
        if (aesthetic === "vector")     return dark ? "#0fffffff" : "#12000000"
        return dark ? "#0effffff" : "#0f000000"
    }
    readonly property color colorBorderMid: {
        if (aesthetic === "instrument") return dark ? "#1cfff8eb" : "#2b504637"
        if (aesthetic === "editorial")  return dark ? "#1cfffff5" : "#1a000000"
        if (aesthetic === "vector")     return dark ? "#1affffff" : "#1c000000"
        return dark ? "#17ffffff" : "#19000000"
    }
    readonly property color colorBorderStrong: {
        if (aesthetic === "instrument") return dark ? "#24fff8eb" : "#38504637"
        if (aesthetic === "editorial")  return dark ? "#26fffff5" : "#21000000"
        if (aesthetic === "vector")     return dark ? "#2effffff" : "#2e000000"
        return dark ? "#21ffffff" : "#26000000"
    }

    // Text
    readonly property color colorText: {
        if (aesthetic === "instrument") return dark ? "#F0EBE1" : "#1C1810"
        if (aesthetic === "editorial")  return dark ? "#F0EFE8" : "#111110"
        if (aesthetic === "vector")     return dark ? "#E8EAF0" : "#0A0B10"
        return dark ? "#EDEDEC" : "#0A0A09"
    }
    readonly property color colorText2: {
        if (aesthetic === "instrument") return dark ? "#A09880" : "#5C5448"
        if (aesthetic === "editorial")  return dark ? "#9A9A92" : "#4A4A47"
        if (aesthetic === "vector")     return dark ? "#8B90A0" : "#4A4E5E"
        return dark ? "#878786" : "#525251"
    }
    readonly property color colorText3: {
        if (aesthetic === "instrument") return dark ? "#665E52" : "#9A9087"
        if (aesthetic === "editorial")  return dark ? "#5A5A55" : "#9B9B97"
        if (aesthetic === "vector")     return dark ? "#484E5E" : "#9098B0"
        return dark ? "#4A4A48" : "#ABABAA"
    }

    // Accent
    readonly property color colorAccent: {
        if (aesthetic === "instrument") return dark ? "#7EBFAA" : "#2B4A3F"
        if (aesthetic === "editorial")  return dark ? "#A8C4E0" : "#1A3A5C"
        if (aesthetic === "vector")     return dark ? "#FF5500" : "#CC3300"
        return dark ? "#4D90FF" : "#0066FF"
    }
    readonly property color colorAccentLight: {
        if (aesthetic === "instrument") return dark ? "#177ebfaa" : "#142b4a3f"
        if (aesthetic === "editorial")  return dark ? "#12a8c4e0" : "#0f1a3a5c"
        if (aesthetic === "vector")     return dark ? "#12ff5500" : "#0fcc3300"
        return dark ? "#124d90ff" : "#0d0066ff"
    }
    readonly property color colorAccentMid: {
        if (aesthetic === "instrument") return dark ? "#297ebfaa" : "#2e2b4a3f"
        if (aesthetic === "editorial")  return dark ? "#24a8c4e0" : "#241a3a5c"
        if (aesthetic === "vector")     return dark ? "#24ff5500" : "#24cc3300"
        return dark ? "#264d90ff" : "#1f0066ff"
    }

    // Good (success / go / connected)
    readonly property color colorGood: {
        if (aesthetic === "instrument") return dark ? "#7EBFAA" : "#1E4D3A"
        if (aesthetic === "editorial")  return dark ? "#8ABFA0" : "#1A4A2E"
        if (aesthetic === "vector")     return dark ? "#2EE8A0" : "#006B45"
        return dark ? "#30C983" : "#0A7A4A"
    }
    readonly property color colorGoodLight: {
        if (aesthetic === "instrument") return dark ? "#177ebfaa" : "#171e4d3a"
        if (aesthetic === "editorial")  return dark ? "#128abfa0" : "#121a4a2e"
        if (aesthetic === "vector")     return dark ? "#122ee8a0" : "#0f006b45"
        return dark ? "#1230c983" : "#0f0a7a4a"
    }

    // Warn (caution / unexpected)
    readonly property color colorWarn: {
        if (aesthetic === "instrument") return dark ? "#D4896A" : "#7A3B1E"
        if (aesthetic === "editorial")  return dark ? "#D4896A" : "#8B2500"
        if (aesthetic === "vector")     return dark ? "#FF8C35" : "#7A3800"
        return dark ? "#FF6B35" : "#D94A00"
    }
    readonly property color colorWarnLight: {
        if (aesthetic === "instrument") return dark ? "#17d4896a" : "#147a3b1e"
        if (aesthetic === "editorial")  return dark ? "#12d4896a" : "#0f8b2500"
        if (aesthetic === "vector")     return dark ? "#14ff8c35" : "#0f7a3800"
        return dark ? "#14ff6b35" : "#0fd94a00"
    }

    // Error (critical failure / fatal — distinctly red, darker than warn)
    readonly property color colorError: {
        if (aesthetic === "instrument") return dark ? "#C46868" : "#7A2020"
        if (aesthetic === "editorial")  return dark ? "#C46868" : "#8B1E1E"
        if (aesthetic === "vector")     return dark ? "#FF4455" : "#8B0014"
        return dark ? "#FF5555" : "#CC2000"
    }
    readonly property color colorErrorLight: {
        if (aesthetic === "instrument") return dark ? "#17c46868" : "#147a2020"
        if (aesthetic === "editorial")  return dark ? "#12c46868" : "#0f8b1e1e"
        if (aesthetic === "vector")     return dark ? "#14ff4455" : "#0f8b0014"
        return dark ? "#14ff5555" : "#0fcc2000"
    }

    // Attention (call-to-action framing — draws the eye to a row/control that
    // needs the user to act, e.g. an uncalibrated sensor). Distinct from colorWarn
    // (orange-red caution) and colorError (red failure): this is a confident amber
    // "do this next" frame. Strong variant = border/text, Light variant = fill.
    readonly property color colorAttention: {
        if (aesthetic === "instrument") return dark ? "#E8B54A" : "#9A6B12"
        if (aesthetic === "editorial")  return dark ? "#E6C25A" : "#8A6A14"
        if (aesthetic === "vector")     return dark ? "#FFD60A" : "#B58900"
        return dark ? "#F5C451" : "#9C6F12"
    }
    readonly property color colorAttentionLight: {
        if (aesthetic === "instrument") return dark ? "#17e8b54a" : "#149a6b12"
        if (aesthetic === "editorial")  return dark ? "#12e6c25a" : "#0f8a6a14"
        if (aesthetic === "vector")     return dark ? "#14ffd60a" : "#0fb58900"
        return dark ? "#14f5c451" : "#0f9c6f12"
    }

    // IMU device-identity colours — A/B/C/D. Fixed hues (red / yellow / green /
    // blue) so a given sensor's colour is consistent across all aesthetics; only
    // brightness shifts for dark vs light backgrounds. Used by the 3D orientation
    // markers and IMU-related UI.
    readonly property color colorImuA: dark ? "#FF3B30" : "#D32F2F"   // red
    readonly property color colorImuB: dark ? "#FFD60A" : "#E0A400"   // yellow
    readonly property color colorImuC: dark ? "#34C759" : "#2E9E4F"   // green
    readonly property color colorImuD: dark ? "#3399FF" : "#0A6CFF"   // blue

    // ── Font family tokens ───────────────────────────────────────────────────
    // Falls back to the system default if the font file is not installed.
    readonly property string fontBody: {
        if (aesthetic === "instrument") return "Georgia"
        if (aesthetic === "editorial")  return "Instrument Sans"
        if (aesthetic === "vector")     return "Space Grotesk"
        return "Geist"
    }
    readonly property string fontData: {
        if (aesthetic === "instrument") return "DM Mono"
        if (aesthetic === "editorial")  return "JetBrains Mono"
        if (aesthetic === "vector")     return "Space Mono"
        return "Geist Mono"
    }
    readonly property string fontDisplay: {
        if (aesthetic === "instrument") return "Georgia"
        if (aesthetic === "editorial")  return "Playfair Display"
        if (aesthetic === "vector")     return "Space Mono"
        return "Geist"
    }
    // On Windows, Segoe UI Emoji intercepts symbol codepoints (e.g. ⚙ U+2699)
    // and renders them as large coloured emoji glyphs. Segoe UI Symbol has the
    // same characters as flat monochrome glyphs and prevents that fallback.
    // On macOS, Apple Color Emoji does the same — Apple Symbols provides flat
    // monochrome glyphs for those codepoints and wins the font-selection race.
    // Georgia (Instrument fontBody) has no Light variant — use Normal to avoid silent rounding.
    readonly property int fontBodyWeight: aesthetic === "instrument" ? Font.Normal : Font.Light

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
                 : 22
        return Math.round(base * fontScale)
    }
    readonly property bool fontDisplayItalic: aesthetic === "editorial"
    readonly property int  fontDisplayWeight: aesthetic === "instrument" ? Font.Bold : Font.Normal

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
    readonly property int headerHeight:    40
    readonly property int carouselHeight:  120
    readonly property int statusBarHeight: 36
    readonly property int radius: {
        if (aesthetic === "instrument") return 6
        if (aesthetic === "editorial")  return 3
        if (aesthetic === "vector")     return 0
        return 5
    }
    readonly property int radiusLg: {
        if (aesthetic === "instrument") return 10
        if (aesthetic === "editorial")  return 6
        if (aesthetic === "vector")     return 0
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
