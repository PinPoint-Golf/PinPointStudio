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

    // Theme cycle index: 0–5 maps to instrument-light, instrument-dark,
    // editorial-light, editorial-dark, studio-light, studio-dark.
    property int themeIndex: 0

    function cycleTheme() { themeIndex = (themeIndex + 1) % 6 }

    // Active aesthetic and mode — derived from themeIndex.
    // Can also be set directly to bind to C++ AppSettings at startup.
    readonly property string aesthetic: ["instrument","instrument","editorial","editorial","studio","studio"][themeIndex]
    readonly property bool   dark:      (themeIndex % 2) === 1

    // ── Colour tokens ────────────────────────────────────────────────────────

    readonly property color colorBg: {
        if (aesthetic === "instrument") return dark ? "#161412" : "#F5F2ED"
        if (aesthetic === "editorial")  return dark ? "#141412" : "#FAFAF8"
        return dark ? "#111110" : "#F6F6F5"
    }
    readonly property color colorBg2: {
        if (aesthetic === "instrument") return dark ? "#1E1C19" : "#EDE9E2"
        if (aesthetic === "editorial")  return dark ? "#1C1C1A" : "#F2F1ED"
        return dark ? "#191918" : "#EEEEED"
    }
    readonly property color colorBg3: {
        if (aesthetic === "instrument") return dark ? "#252220" : "#E4DFD6"
        if (aesthetic === "editorial")  return dark ? "#242422" : "#E8E7E2"
        return dark ? "#212120" : "#E4E4E3"
    }
    readonly property color colorSurface: {
        if (aesthetic === "instrument") return dark ? "#1A1816" : "#FDFAF6"
        if (aesthetic === "editorial")  return dark ? "#181816" : "#FFFFFF"
        return dark ? "#161615" : "#FAFAF9"
    }

    // Borders — 1 px at these opacities simulate 0.5 px
    readonly property color colorBorder: {
        if (aesthetic === "instrument") return dark ? "#14fff8eb" : "#1f504637"
        if (aesthetic === "editorial")  return dark ? "#12fffff5" : "#12000000"
        return dark ? "#0effffff" : "#0f000000"
    }
    readonly property color colorBorderMid: {
        if (aesthetic === "instrument") return dark ? "#1cfff8eb" : "#2b504637"
        if (aesthetic === "editorial")  return dark ? "#1cfffff5" : "#1a000000"
        return dark ? "#17ffffff" : "#19000000"
    }
    readonly property color colorBorderStrong: {
        if (aesthetic === "instrument") return dark ? "#24fff8eb" : "#38504637"
        if (aesthetic === "editorial")  return dark ? "#26fffff5" : "#21000000"
        return dark ? "#21ffffff" : "#26000000"
    }

    // Text
    readonly property color colorText: {
        if (aesthetic === "instrument") return dark ? "#F0EBE1" : "#1C1810"
        if (aesthetic === "editorial")  return dark ? "#F0EFE8" : "#111110"
        return dark ? "#EDEDEC" : "#0A0A09"
    }
    readonly property color colorText2: {
        if (aesthetic === "instrument") return dark ? "#A09880" : "#5C5448"
        if (aesthetic === "editorial")  return dark ? "#9A9A92" : "#4A4A47"
        return dark ? "#878786" : "#525251"
    }
    readonly property color colorText3: {
        if (aesthetic === "instrument") return dark ? "#665E52" : "#9A9087"
        if (aesthetic === "editorial")  return dark ? "#5A5A55" : "#9B9B97"
        return dark ? "#4A4A48" : "#ABABAA"
    }

    // Accent
    readonly property color colorAccent: {
        if (aesthetic === "instrument") return dark ? "#7EBFAA" : "#2B4A3F"
        if (aesthetic === "editorial")  return dark ? "#A8C4E0" : "#1A3A5C"
        return dark ? "#4D90FF" : "#0066FF"
    }
    readonly property color colorAccentLight: {
        if (aesthetic === "instrument") return dark ? "#177ebfaa" : "#142b4a3f"
        if (aesthetic === "editorial")  return dark ? "#12a8c4e0" : "#0f1a3a5c"
        return dark ? "#124d90ff" : "#0d0066ff"
    }
    readonly property color colorAccentMid: {
        if (aesthetic === "instrument") return dark ? "#297ebfaa" : "#2e2b4a3f"
        if (aesthetic === "editorial")  return dark ? "#24a8c4e0" : "#241a3a5c"
        return dark ? "#264d90ff" : "#1f0066ff"
    }

    // Good (success / go / connected)
    readonly property color colorGood: {
        if (aesthetic === "instrument") return dark ? "#7EBFAA" : "#1E4D3A"
        if (aesthetic === "editorial")  return dark ? "#8ABFA0" : "#1A4A2E"
        return dark ? "#30C983" : "#0A7A4A"
    }
    readonly property color colorGoodLight: {
        if (aesthetic === "instrument") return dark ? "#177ebfaa" : "#171e4d3a"
        if (aesthetic === "editorial")  return dark ? "#128abfa0" : "#121a4a2e"
        return dark ? "#1230c983" : "#0f0a7a4a"
    }

    // Warn (danger / stop / error)
    readonly property color colorWarn: {
        if (aesthetic === "instrument") return dark ? "#D4896A" : "#7A3B1E"
        if (aesthetic === "editorial")  return dark ? "#D4896A" : "#8B2500"
        return dark ? "#FF6B35" : "#D94A00"
    }
    readonly property color colorWarnLight: {
        if (aesthetic === "instrument") return dark ? "#17d4896a" : "#147a3b1e"
        if (aesthetic === "editorial")  return dark ? "#12d4896a" : "#0f8b2500"
        return dark ? "#14ff6b35" : "#0fd94a00"
    }

    // ── Font family tokens ───────────────────────────────────────────────────
    // Falls back to the system default if the font file is not installed.
    readonly property string fontBody: {
        if (aesthetic === "instrument") return "DM Sans"
        if (aesthetic === "editorial")  return "Instrument Sans"
        return "Geist"
    }
    readonly property string fontData: {
        if (aesthetic === "instrument") return "DM Mono"
        if (aesthetic === "editorial")  return "JetBrains Mono"
        return "Geist Mono"
    }
    readonly property string fontDisplay: {
        if (aesthetic === "instrument") return "DM Serif Display"
        if (aesthetic === "editorial")  return "Playfair Display"
        return "Geist"
    }

    // ── Typography scale tokens ──────────────────────────────────────────────
    readonly property int  fontSzDisplay: {
        if (aesthetic === "instrument") return 26
        if (aesthetic === "editorial")  return 34
        return 22
    }
    readonly property bool fontDisplayItalic: aesthetic !== "studio"

    readonly property int  fontSzData:    20   // large metric/data values
    readonly property int  fontSzDataSm:  13   // small data / status strings
    readonly property int  fontSzMicro:   10   // uppercase micro labels
    readonly property int  fontSzHeading: 16   // section headings
    readonly property int  fontSzBody:    13   // primary UI body text
    readonly property int  fontSzBody2:   12   // secondary/muted body text
    readonly property int  fontSzLabel:   11   // uppercase field labels

    // ── Letter-spacing tokens (px) ───────────────────────────────────────────
    readonly property real trackingMicro:  0.8
    readonly property real trackingLabel:  0.6
    readonly property real trackingData:   0.3
    readonly property real trackingNormal: 0.0

    // ── Geometry tokens ──────────────────────────────────────────────────────
    readonly property int railWidth: {
        if (aesthetic === "instrument") return 56
        if (aesthetic === "editorial")  return 58
        return 52
    }
    readonly property int headerHeight:    40
    readonly property int carouselHeight:  120
    readonly property int statusBarHeight: 36
    readonly property int radius: {
        if (aesthetic === "instrument") return 6
        if (aesthetic === "editorial")  return 3
        return 5
    }
    readonly property int radiusLg: {
        if (aesthetic === "instrument") return 10
        if (aesthetic === "editorial")  return 6
        return 8
    }

    // ── Border tokens ────────────────────────────────────────────────────────
    readonly property real borderWidth:         1
    readonly property real borderOpacityNormal: 0.5
    readonly property real borderOpacityStrong: 0.75

    // ── Animation duration tokens (ms) ───────────────────────────────────────
    readonly property int durationFast:   120
    readonly property int durationNormal: 220
    readonly property int durationSlow:   350
}
