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

// The how-often meter: ten segments, round(share × 10) of them lit in the tone and the rest the
// theme's unlit colour, every segment drawn from the meter tokens in every theme.
Item {
    id: probe
    width: 400; height: 100

    PpMeter { id: meter; share: 0.5; tone: "#123456" }

    function segments() {
        const out = []
        for (let i = 0; i < meter.children.length; ++i)
            if (meter.children[i].objectName === "meterSegment") out.push(meter.children[i])
        return out
    }

    TestCase {
        name: "PpMeter"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }
        function init() { meter.share = 0.5; meter.tone = "#123456" }

        function test_tenSegmentsLitByShare() {
            compare(probe.segments().length, 10)
            for (const c of [[0, 0], [0.04, 0], [0.05, 1], [0.34, 3], [0.66, 7], [1, 10], [1.4, 10], [-0.2, 0]]) {
                meter.share = c[0]
                compare(meter.filled, c[1], "share " + c[0])
                const segs = probe.segments()
                for (let i = 0; i < 10; ++i)
                    verify(Qt.colorEqual(segs[i].color, i < c[1] ? meter.tone : Theme.colorMeterUnlit),
                           "share " + c[0] + " segment " + i)
            }
        }

        function test_geometryFromTheTokens() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const where = Theme.aesthetic + (Theme.dark ? " dark" : " light")
                const s = probe.segments()[0]
                compare(s.width,  Theme.sp(Theme.meterSegW), where + ": width")
                compare(s.height, Theme.sp(Theme.meterSegH), where + ": height")
                compare(s.radius, Math.min(s.height / 2, Theme.sp(Theme.meterSegRadius)), where + ": radius")
                compare(meter.spacing, Theme.sp(Theme.meterSegGap), where + ": gap")
                tryCompare(meter, "implicitWidth", 10 * Theme.sp(Theme.meterSegW) + 9 * Theme.sp(Theme.meterSegGap))   // the Row lays out on polish
                meter.share = 0
                verify(Qt.colorEqual(probe.segments()[9].color, Theme.colorMeterUnlit), where + ": unlit")
                meter.share = 0.5
            }
        }
    }
}
