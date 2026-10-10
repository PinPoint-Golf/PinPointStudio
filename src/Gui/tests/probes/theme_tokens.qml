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
import PinPointStudio

// Dumps every Theme token (and sampled function results) for theme indices 0..N-1, one TOK: line
// per theme, so a restructure of Theme.qml can be proven value-identical (design system §14.1):
// dump from the old build and the new one, then compare the JSON per theme, skipping `_` keys.
// Run on a COPY of the settings — it writes themeIndex:
//   XDG_CONFIG_HOME=<scratch> QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <this file> \
//       [--probe-themes 14]      then: grep -o 'TOK: .*' <log>
Item {
    id: probe
    property int startTheme: 0
    function ser(v) {
        if (v === undefined) return "undefined"
        if (v === null) return "null"
        if (typeof v === "object" && v.r !== undefined && v.a !== undefined) return String(v)  // color
        if (typeof v === "object") return JSON.stringify(v)
        return String(v)
    }
    function dump(t) {
        Theme.themeIndex = t
        const out = {}
        const skip = { objectName: 1, _metricCatalogue: 1, metricColorNames: 1 }
        for (const k in Theme) {
            if (skip[k] || k.endsWith("Changed")) continue
            const v = Theme[k]
            if (typeof v === "function") continue
            out[k] = ser(v)
        }
        const f = {}
        for (const g of ["ideal","good","watch","action","open"]) { f["corridorFill:"+g] = ser(Theme.corridorFill(g)); f["corridorTone:"+g] = ser(Theme.corridorTone(g)); f["corridorWord:"+g] = Theme.corridorWord(g) }
        for (const v of ["good","attention","warn",""]) { f["verdictColor:"+v] = ser(Theme.verdictColor(v)); f["verdictWords:"+v] = Theme.verdictWords(v) }
        for (const b of ["good","green","attention","yellow","amber","warn","red","x"]) f["bandVerdict:"+b] = Theme.bandVerdict(b)
        for (const s of [0,24,25,49,50,74,75,100]) { f["qualityColor:"+s] = ser(Theme.qualityColor(s)); f["qualityColorLight:"+s] = ser(Theme.qualityColorLight(s)); f["qualityMark:"+s] = Theme.qualityMark(s) }
        for (let l = 0; l <= 6; ++l) f["strengthColor:"+l] = ser(Theme.strengthColor(l))
        for (let i = -2; i < 9; ++i) f["chartSeriesColor:"+i] = ser(Theme.chartSeriesColor(i))
        for (const n of Theme.metricColorNames) { f["paletteDefault:"+n] = ser(Theme.paletteDefault(n)); f["paletteColor:"+n] = ser(Theme.paletteColor(n)) }
        for (const k of ["clubheadSpeed","pelvisRotation","chestRotation","notAMetricKey"]) f["metricColor:"+k] = ser(Theme.metricColor(k))
        for (const n of [0,1,7,12.5,20]) f["sp:"+n] = Theme.sp(n)
        for (const w of [600,1200,1800]) f["contentWidth:"+w] = Theme.contentWidth(w)
        for (const h of [-999,0,5.4,-2]) f["formatHandicap:"+h] = Theme.formatHandicap(h)
        for (const gl of ["⌂","⌖","◑","x"]) f["symbolScale:"+gl] = Theme.symbolScale(gl)
        console.warn("TOK: " + t + " " + JSON.stringify({props: out, fns: f}))
    }
    Timer {
        interval: 1500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            const n = parseInt(Qt.application.arguments[Qt.application.arguments.indexOf("--probe-themes") + 1] || "12")
            for (let t = 0; t < n; ++t) probe.dump(t)
            Theme.themeIndex = probe.startTheme
            Qt.exit(0)
        }
    }
}
