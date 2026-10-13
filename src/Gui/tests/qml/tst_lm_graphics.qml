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

// The launch monitor panel's body (the FSX design, docs/design/lm_panel_fsx.md), loaded for real and
// measured.
//
// THE ASSERTIONS THAT EARN THIS FILE: the four club figures (Path, Loft, Spin, Impact) are one size and
// stand in FSX's order; no annotation escapes its figure, at the design size, at fontScale 1.25, in both
// themes and in the narrow column; directions are words; a missing reading is "-"; and the interaction
// set does what the design says — a click on a mark asks for that shot, the pinned card leaves a shot out,
// the mismatch note undoes, the club key scopes, and a static body (cast, print) takes no input at all.
//
// This loads the REAL PpLmGraphicsBody. It can, because that component takes plain maps and emits
// intents: the fixture below is the shape LmSessionModel publishes (graphics.values, .shots, .clubs,
// .mismatch, .device), so nothing here is a stand-in for the thing under test.
Item {
    id: probe
    width: 1600; height: 900

    function field(v, text, unit, label, band, mean, sd) {
        const spread = mean !== undefined
        return { value: v, text: text, unit: unit, abbrev: label, label: label, bandIndex: band, has: true,
                 mean: spread ? "μ " + mean : "", sd: spread ? "±" + sd : "",
                 meanValue: spread ? mean : v, sdValue: spread ? sd : 0, hasSpread: spread, n: spread ? 5 : 1, grade: "" }
    }
    function shot(id, n, club, recorded, src, carry, offline, path, face, h, v, extra) {
        return Object.assign({ id: id, number: n, club: club, recordedClub: recorded, clubSource: src, excluded: false,
                               values: { "lm.carryDistance": carry, "lm.offline": offline, "lm.totalDistance": carry + 12,
                                         "lm.clubPath": path, "lm.faceAngle": face, "lm.strikeLocation": h, "lm.strikeHeight": v,
                                         "lm.launchAngle": 16 + n * 0.3, "lm.spinRate": 5200 + n * 40, "lm.ballSpeed": 112 + n,
                                         "lm.clubheadSpeed": 84 + n * 0.2, "lm.dynamicLoft": 24 + n * 0.2, "lm.smashFactor": 1.33 },
                               evidence: {} }, extra || {})
    }
    function stat(mean, sd, n) { return { mean: mean, sd: sd, n: n } }

    readonly property var fixture: ({
        leftHanded: false,
        has: true,
        values: {
            "lm.clubheadSpeed":   field(84.5, "84.5", "mph", "Clubhead speed", 0, 84.1, 2.1),
            "lm.attackAngle":     field(-3.5, "−3.5", "°", "Attack angle", 0, -3.2, 0.9),
            "lm.clubPath":        field(-1.8, "−1.8", "°", "Club path", 0, -2.1, 1.4),
            "lm.faceAngle":       field(-1.1, "−1.1", "°", "Face angle", 0, -0.9, 1.1),
            "lm.faceToPath":      field(0.7, "0.7", "°", "Face to path", 0),
            "lm.dynamicLoft":     field(25.7, "25.7", "°", "Dynamic loft", 0, 25.1, 1.3),
            "lm.spinLoft":        field(29.2, "29.2", "°", "Spin loft", 0),
            "lm.lieAngle":        field(1.6, "1.6", "°", "Lie angle", 0),
            "lm.closureRate":     field(1840, "1840", "°/s", "Closure rate", 0),
            "lm.smashFactor":     field(1.33, "1.33", "ratio", "Smash factor", 1, 1.34, 0.02),
            "lm.strikeLocation":  field(-9, "−9", "mm", "Strike location", 1, -4, 5),
            "lm.strikeHeight":    field(-1, "−1", "mm", "Strike height", 1, 3, 4),
            "lm.ballSpeed":       field(118.1, "118.1", "mph", "Ball speed", 2, 116.2, 3.1),
            "lm.launchAngle":     field(17.2, "17.2", "°", "Launch angle", 2, 17.6, 1.0),
            "lm.launchDirection": field(-1.3, "−1.3", "°", "Start direction", 2, -0.8, 1.2),
            "lm.spinRate":        field(5386, "5386", "rpm", "Spin rate", 3, 5300, 220),
            "lm.backSpin":        field(5370, "5370", "rpm", "Back spin", 3),
            "lm.sideSpin":        field(384, "384", "rpm", "Side spin", 3),
            "lm.spinAxis":        field(4.7, "4.7", "°", "Spin axis", 3, 3.9, 2.6),
            "lm.carryDistance":   field(166.6, "166.6", "yd", "Carry", 4, 164.2, 5.4),
            "lm.totalDistance":   field(181.9, "181.9", "yd", "Total distance", 4),
            "lm.offline":         field(5.1, "5.1", "yd", "Offline", 4, 2.8, 4.1),
            "lm.peakHeight":      field(63, "63", "ft", "Peak height", 4),
            "lm.descentAngle":    field(44.3, "44.3", "°", "Descent angle", 4),
        },
        flight: {
            has: true,
            profile: [Qt.point(0, 0), Qt.point(0.29, 0.62), Qt.point(0.58, 1.0), Qt.point(0.78, 0.72), Qt.point(0.9159, 0.0)],
            track:   [Qt.point(0, 0), Qt.point(0.29, -0.10), Qt.point(0.58, 0.24), Qt.point(0.78, 0.58), Qt.point(0.9159, 0.889)],
            landing: { x: 0.9159, y: 0.0, z: 0.889 }, finish: { x: 1.0, y: 0.0, z: 1.0 },
            carryFraction: 0.9159, apexAtX: 0.5814, lateralExtentYd: 5.73, carryYd: 166.6, totalYd: 181.9,
            apexFt: 63.0, offlineYd: 5.1,
        },
        lowPoint: { has: false },
        shape:  { has: true, name: "Pull–fade", evidence: "start 1.3° left · axis 4.7° right" },
        strike: { has: true, name: "Heel", evidence: "strike 9 mm heel" },
        // Two clubs: the 7 iron in scope (five shots, so it has its ellipses), the driver with two (none).
        shots: [
            shot(1, 1, "DRIVER", "DRIVER", "recorded", 214, -6, -2.0, -1.0, 4, 2),
            shot(2, 2, "DRIVER", "DRIVER", "recorded", 221, 9, 1.0, 2.0, -3, 5),
            shot(3, 3, "7 IRON", "DRIVER", "evidence", 158, 2, -1.2, -0.4, -7, 1, { evidence: { suggest: "7 IRON", votes: 3, of: 3 } }),
            shot(4, 4, "7 IRON", "7 IRON", "recorded", 162, -4, -2.5, -1.5, -2, 6),
            shot(5, 5, "7 IRON", "7 IRON", "recorded", 166.6, 5.1, -1.8, -1.1, -9, -1),
            shot(6, 6, "7 IRON", "7 IRON", "recorded", 170, 8, -3.0, -0.2, 1, 4),
            shot(7, 7, "7 IRON", "7 IRON", "recorded", 165, -1, -1.0, -1.8, -5, 7),
        ],
        clubs: [
            { club: "7 IRON", head: "iron", n: 5,
              stats: { "lm.carryDistance": stat(164.3, 4.6, 5), "lm.offline": stat(2.0, 4.8, 5), "lm.clubPath": stat(-1.9, 0.8, 5),
                       "lm.faceAngle": stat(-1.0, 0.6, 5), "lm.ballSpeed": stat(116.2, 3.1, 5), "lm.launchAngle": stat(17.6, 1.0, 5),
                       "lm.spinRate": stat(5300, 220, 5), "lm.strikeLocation": stat(-4.4, 3.9, 5), "lm.strikeHeight": stat(3.4, 3.2, 5) },
              ellipses: { dispersion: { cx: 164, cy: 2, a: 9, b: 6, angleDeg: 20 },
                          strike: { cx: -4, cy: 3, a: 7, b: 4, angleDeg: -15 } } },
            { club: "DRIVER", head: "driver", n: 2,
              stats: { "lm.carryDistance": stat(217.5, 4.9, 2) }, ellipses: {} },
        ],
        mismatch: { count: 1, recorded: "DRIVER", suggested: "7 IRON", shotIds: [3] },
        device: { name: "GC Quad", maker: "Foresight", markers: "four" },
    })

    // An empty map: the state before a monitor has said anything. Every accessor has to survive it.
    readonly property var emptyFixture: ({})

    PpLmGraphicsBody {
        id: body
        anchors.fill: parent
        g: probe.fixture
        focusedShotId: 5
    }

    SignalSpy { id: shotSpy;    target: body; signalName: "shotRequested" }
    SignalSpy { id: excludeSpy; target: body; signalName: "excludeRequested" }
    SignalSpy { id: undoSpy;    target: body; signalName: "undoMismatchRequested" }

    TestCase {
        id: tc
        name: "LmGraphics"
        when: windowShown

        function walk(item, pred, acc) {
            acc = acc || []
            if (!item || !item.children) return acc
            for (let i = 0; i < item.children.length; ++i) {
                const c = item.children[i]
                if (pred(c)) acc.push(c)
                walk(c, pred, acc)
            }
            return acc
        }
        function visibleNow(it) {
            for (let p = it; p; p = p.parent) if (!p.visible) return false
            return true
        }
        function cells() { return walk(body, c => c.objectName === "lmCell" && visibleNow(c)) }
        function cellTitled(t) { const cs = cells(); for (const c of cs) if (c.title === t) return c; return null }
        function annotations(item) { return walk(item, c => c.objectName === "anno" && visibleNow(c)) }
        function chips(text) { return walk(body, c => c.objectName === "lmChip" && visibleNow(c) && String(c.text).indexOf(text) === 0) }
        function picker(src) { const ps = walk(body, c => c.objectName === "lmPicker" && c.src === src && visibleNow(c)); return ps.length ? ps[0] : null }

        function init() {
            probe.width = 1600; probe.height = 900
            body.g = probe.fixture
            body.tab = "shot"
            body.interactive = true
            body.unpinCard()
            body.scopeKey = ""
            shotSpy.clear(); excludeSpy.clear(); undoSpy.clear()
            wait(0)
        }

        // Is `anno` fully inside the figure that holds it? Measured in the figure's own coordinates.
        function checkContainment(label) {
            const cs = cells()
            compare(cs.length, 4, label + ": four figures")
            let checked = 0
            for (const c of cs) {
                for (const a of annotations(c)) {
                    if (a.width <= 0) continue
                    ++checked
                    const tl = a.mapToItem(c, 0, 0), br = a.mapToItem(c, a.width, a.height)
                    verify(tl.x >= -1 && tl.y >= -1 && br.x <= c.width + 1 && br.y <= c.height + 1,
                           label + ": an annotation escapes " + c.title + " (" + Math.round(tl.x) + "," + Math.round(tl.y)
                           + " " + Math.round(a.width) + "×" + Math.round(a.height) + " in " + Math.round(c.width) + "×" + Math.round(c.height) + ")")
                }
            }
            // a walk that found nothing would pass forever: the floor catches that, not the count
            verify(checked >= 14, label + ": expected 14+ annotations, saw " + checked)
        }

        // ── the four figures: one size, FSX's order ─────────────────────────
        function test_01_fourEqualFiguresInFsxOrder() {
            const cs = cells()
            compare(cs.length, 4, "Path, Loft, Spin and Impact")
            for (const c of cs) {
                compare(Math.round(c.width), Math.round(cs[0].width), c.title + " is as wide as the others")
                compare(Math.round(c.height), Math.round(cs[0].height), c.title + " is as tall as the others")
            }
            const P = cellTitled("Path"), S = cellTitled("Spin"), L = cellTitled("Loft"), I = cellTitled("Impact")
            verify(P && S && L && I, "every figure is titled")
            if (body.lastShotLayout && body.lastShotLayout.arrangement === "quad") {
                // FSX image 03: Path and Spin on top, Loft and Impact below
                const p = P.mapToItem(body, 0, 0), s = S.mapToItem(body, 0, 0), l = L.mapToItem(body, 0, 0), i = I.mapToItem(body, 0, 0)
                compare(Math.round(p.y), Math.round(s.y), "Path and Spin share the top row")
                verify(p.x < s.x, "Path left of Spin")
                compare(Math.round(l.y), Math.round(i.y), "Loft and Impact share the bottom row")
                verify(l.y > p.y && l.x < i.x, "Loft below Path, left of Impact")
            }
        }

        function test_02_annotationsStayInTheirFigures() {
            checkContainment("design size")
            const was = Theme.fontScale
            Theme.fontScale = 1.25
            wait(0)
            checkContainment("fontScale 1.25")
            Theme.fontScale = was
            const wasTheme = appSettings.themeIndex
            for (let t = 0; t < 2; ++t) { appSettings.themeIndex = t; wait(0); checkContainment("theme " + t) }
            appSettings.themeIndex = wasTheme
            wait(0)
        }

        // ── narrow: one column in the owner's order, never a sideways scroll ─
        function test_03_oneColumnWhenNarrow() {
            probe.width = 430; probe.height = 900
            wait(0)
            const order = ["Path", "Loft", "Spin", "Impact"].map(t => cellTitled(t))
            verify(order.every(c => !!c), "all four figures survive")
            for (let k = 1; k < 4; ++k) {
                const a = order[k - 1].mapToItem(body, 0, 0), b = order[k].mapToItem(body, 0, 0)
                compare(Math.round(a.x), Math.round(b.x), "one column")
                verify(b.y > a.y, order[k].title + " below " + order[k - 1].title)
            }
            checkContainment("one column")
            const flicks = walk(body, c => c.contentWidth !== undefined && c.contentHeight !== undefined && visibleNow(c))
            verify(flicks.length > 0, "the column is flickable")
            for (const f of flicks) verify(f.contentWidth <= f.width + 1, "nothing to pan to sideways")
        }

        // ── the degenerate case ─────────────────────────────────────────────
        function test_04_survivesAnEmptyMap() {
            body.g = probe.emptyFixture
            wait(0)
            compare(body.hasData, false, "nothing reported, nothing claimed")
            compare(body.flight.has === true, false, "no flight")
            for (const t of body.heroTiles) compare(t.value, "-", t.label + " reads -")
            body.tab = "session"
            wait(0)
            compare(body.clubs.length, 0, "no clubs")
        }

        // ── words, not signs; "-" for what is missing ───────────────────────
        function test_05_wordsNotSigns() {
            const off = body.heroTiles.find(t => t.key === "lm.offline")
            compare(off.value, "5.1", "no sign on the figure")
            compare(off.dir, "R", "the side as a letter")
            compare(body.figParts("lm.faceToPath").dir, "OPEN", "face to path in words")
            compare(body.figParts("lm.strikeLocation").dir, "HEEL", "strike in words")
            compare(body.figParts("lm.attackAngle").dir, "DOWN", "attack in words")
            body.g = Object.assign({}, probe.fixture, { leftHanded: true })
            wait(0)
            compare(body.figParts("lm.faceToPath").dir, "CLOSED", "open and closed flip for a left-hander")
            compare(body.figParts("lm.offline").dir, "R", "left and right never flip")
            compare(body.figParts("lm.notReported").num, "-", "a reading the device did not report is -")
        }

        // ── the ellipse rules: the model's, and none under three shots ──────
        function test_06_ellipsesAreTheModels() {
            const iron = body.clubs.find(c => c.key === "7 iron"), drv = body.clubs.find(c => c.key === "driver")
            verify(iron && iron.ell, "the 7 iron's dispersion ellipse is drawn from graphics.clubs")
            compare(iron.ell.mx, 164, "at the model's centre")
            compare(drv.ell, null, "two driver shots: no ellipse")
            compare(drv.shots.length, 2, "but their dots are still there")
        }

        // ── click to focus, the pinned card, Leave out ──────────────────────
        function test_07_clickAMarkAsksForThatShot() {
            body.tab = "session"
            wait(50)
            const pk = picker("disp")
            verify(pk !== null, "the dispersion takes clicks")
            const p = pk.pts.find(q => q.id === 6)
            verify(!!p, "shot 6 has a mark")
            mouseClick(pk, p.x, p.y)
            compare(shotSpy.count, 1, "a click asks for the shot")
            compare(shotSpy.signalArguments[0][0], 6, "that shot")
            compare(body.pinnedId, 6, "and pins its card")
            // Let the card it just pinned lay out and draw before pressing on it, as a golfer must. A press in the
            // same event-loop turn lands before the new card takes input and reaches the figure under it instead
            // (reproduced in the app with a real click: at once it re-picks the figure, after a frame it is asked).
            wait(100)
            const leave = chips("Leave out")
            verify(leave.length > 0, "the pinned card offers Leave out")
            mouseClick(leave[0])
            compare(excludeSpy.count, 1, "Leave out asks the model")
            compare(excludeSpy.signalArguments[0][0], 6)
            compare(excludeSpy.signalArguments[0][1], true)
        }

        function test_08_mismatchNoteUndoes() {
            const undo = chips("Undo")
            verify(undo.length > 0, "the mismatch note carries Undo")
            mouseClick(undo[0])
            compare(undoSpy.count, 1, "Undo asks the model")
        }

        function test_09_clubKeyScopes() {
            body.tab = "session"
            wait(50)
            compare(body.focusKey, "7 iron", "the focused shot's club is the scope")
            const keys = walk(body, c => c.objectName === "lmClubKey" && visibleNow(c))
            verify(keys.length >= 2, "one key per club")
            for (const k of keys) if (k.parent.modelData && k.parent.modelData.key === "driver") mouseClick(k)
            compare(body.focusKey, "driver", "the key sets the scope")
        }

        // ── cast and print are static ───────────────────────────────────────
        function test_10_staticWhenNotInteractive() {
            body.interactive = false
            body.tab = "session"
            wait(50)
            const pk = picker("disp")
            verify(pk === null || !pk.enabled, "the marks take no clicks")
            compare(chips("Undo").length, 0, "the notes show no controls")
            if (pk) { const p = pk.pts[0]; if (p) mouseClick(pk, p.x, p.y) }
            compare(shotSpy.count, 0, "nothing is asked for")
        }
    }
}
