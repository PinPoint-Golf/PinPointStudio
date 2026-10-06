// Stage 5a — the progress indicator (catalogue group P, docs/design/session_wizard_refactor_design.md
// §4.9, §7.3). P1, P2, P4 press PpFlowIndicator ALONE with hand-made models; P3 and P5 drive it
// through the real shell, ScreenSessionSetup, with support/SetupDriver.qml.
//
// P4's containment check is tst_lm_graphics.qml's escapes(): an item escapes when its mapped
// rectangle leaves the strip by more than a pixel.
import QtQuick
import QtTest
import PinPointStudio
import "support"

// ⚠ THE ROOT IS A PLAIN Item, NOT THE TestCase (see tst_setup_smoke.qml).
Item {
    id: root
    width: 1400
    height: 900

    readonly property var _testLog: testLog

    Item { id: host; anchors.fill: parent }
    SetupDriver { id: d; host: host; testCase: tc; testLog: root._testLog }

    Component { id: indComp; PpFlowIndicator {} }
    Component { id: spyComp; SignalSpy {} }

    TestCase {
        id: tc
        name: "SetupIndicator"
        when: windowShown

        readonly property int wrist: SessionController.Wrist

        function init() {
            testLog.reset()
            // Offscreen-QPA font notice, not a QML warning — see tst_setup_smoke.qml.
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            d.setUp()
        }
        function cleanup() {
            d.tearDown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── Fixtures (component cases) ───────────────────────────────────────
        readonly property var groupLabels: ({ session: "Session", cameras: "Cameras", sensors: "Sensors", ready: "Ready" })

        // [key, label, group, state] → the model; `cur` is the current key; numbers follow order.
        function makeModel(spec, cur, attentionKeys) {
            var out = []
            for (var i = 0; i < spec.length; ++i)
                out.push({ key: spec[i][0], label: spec[i][1], group: spec[i][2], state: spec[i][3],
                           current: spec[i][0] === cur, number: i + 1,
                           attention: (attentionKeys || []).indexOf(spec[i][0]) >= 0,
                           tip: "tip " + spec[i][0] })
            return out
        }
        readonly property var eight: [
            ["g", "Goals", "session", "done"], ["c", "Cameras", "cameras", "done"],
            ["t", "Triangulate", "cameras", "skipped"], ["b", "Ball", "cameras", "pending"],
            ["i", "IMUs", "sensors", "pending"], ["k", "Calibrate", "sensors", "pending"],
            ["h", "Confirm", "sensors", "pending"], ["r", "Ready", "ready", "pending"]
        ]
        function progressOf(model) {
            var p = 0
            for (var i = 0; i < model.length; ++i) if (model[i].state !== "pending") ++p
            return { passed: p, total: model.length }
        }
        function makeIndicator(width, model) {
            var ind = createTemporaryObject(indComp, host, { width: width, model: model, groups: groupLabels,
                                                             progress: progressOf(model) })
            verify(ind !== null)
            settleIndicator(ind)
            return ind
        }
        function pips(ind) {
            return d.findAll(ind, function(o) { return o.objectName === "flowPip" })
        }
        function pip(ind, key) {
            var p = pips(ind).filter(function(o) { return o.stepKey === key && o.present })
            return p.length ? p[0] : null
        }
        function child(item, name) { return d.find(item, function(o) { return o.objectName === name }) }
        // Every pip at its target size, every leaving pip gone.
        function settleIndicator(ind) {
            tryVerify(function() {
                var ps = pips(ind)
                for (var i = 0; i < ps.length; ++i) {
                    if (!ps[i].present) return false
                    if (ps[i].grow !== (ps[i].shown ? 1 : 0)) return false
                }
                return true
            }, 3000, "the indicator's pips never settled")
            waitForItemPolished(ind)
        }
        function shownInOrder(ind) {
            return pips(ind).filter(function(o) { return o.shown })
                            .sort(function(a, b) { return a.mapToItem(ind, 0, 0).x - b.mapToItem(ind, 0, 0).x })
                            .map(function(o) { return o.stepKey })
        }
        // tst_lm_graphics.qml's escapes(): one pixel of slack.
        function escapes(card, anno) {
            const tl = anno.mapToItem(card, 0, 0)
            const br = anno.mapToItem(card, anno.width, anno.height)
            const tol = 1.0
            return tl.x < -tol || tl.y < -tol
                || br.x > card.width + tol || br.y > card.height + tol
        }

        // ── P1 ───────────────────────────────────────────────────────────────
        function test_P1_onePipPerStepGroupsAndColours() {
            var m = makeModel(eight, "b", ["c"])
            var ind = makeIndicator(1000, m)
            verify(!ind.collapsed)
            compare(shownInOrder(ind), ["g", "c", "t", "b", "i", "k", "h", "r"])
            // Group labels: one over each run, starting at the run's first pip.
            var labels = d.findAll(ind, function(o) { return o.objectName === "flowGroupLabel" && o.visible })
            compare(labels.map(function(l) { return l.text }), ["Session", "Cameras", "Sensors", "Ready"])
            compare(labels[1].font.capitalization, Font.AllUppercase)
            var runFirst = ["g", "c", "i", "r"], runLast = ["g", "b", "h", "r"]
            for (var r = 0; r < labels.length; ++r) {
                var lx = labels[r].mapToItem(ind, 0, 0).x
                var first = child(pip(ind, runFirst[r]), "flowPipDot").mapToItem(ind, 0, 0).x
                var lastCell = pip(ind, runLast[r])
                verify(Math.abs(lx - first) <= 1, labels[r].text + " label not over its first pip")
                verify(lx + labels[r].width <= lastCell.mapToItem(ind, 0, 0).x + lastCell.width + 1,
                       labels[r].text + " label runs past its run")
            }
            // Colours and glyphs per state.
            function dot(k) { return child(pip(ind, k), "flowPipDot") }
            function glyph(k) { return child(pip(ind, k), "flowPipGlyph").text }
            tryVerify(function() { return Qt.colorEqual(dot("g").color, Theme.colorGoodLight) })
            verify(Qt.colorEqual(dot("g").border.color, Theme.colorGood))
            compare(glyph("g"), "✓")
            verify(Qt.colorEqual(dot("t").color, Theme.colorWarnLight))
            verify(Qt.colorEqual(dot("t").border.color, Theme.colorWarn))
            compare(glyph("t"), "⚠")
            verify(Qt.colorEqual(dot("b").color, Theme.colorAccent), "current is filled accent")
            compare(glyph("b"), "4")
            verify(Qt.colorEqual(dot("i").border.color, Theme.colorBorderMid), "pending is an outline")
            compare(glyph("i"), "5")
            // Attention ring only where asked.
            verify(child(pip(ind, "c"), "flowPipAttention").visible)
            verify(!child(pip(ind, "g"), "flowPipAttention").visible)
            // Progress and accessibility.
            compare(child(ind, "flowProgressText").text, "3 of 8 done")
            var area = d.find(pip(ind, "t"), function(o) { return d.typeName(o) === "QQuickMouseArea" })
            compare(area.Accessible.name, "Triangulate, skipped")
            compare(d.find(pip(ind, "b"), function(o) { return d.typeName(o) === "QQuickMouseArea" }).Accessible.name,
                    "Ball, current")
            // Jump-back only: a done pip asks, a future one does not.
            var spy = createTemporaryObject(spyComp, tc, { target: ind, signalName: "jumpRequested" })
            mouseClick(d.find(pip(ind, "c"), function(o) { return d.typeName(o) === "QQuickMouseArea" }))
            mouseClick(d.find(pip(ind, "i"), function(o) { return d.typeName(o) === "QQuickMouseArea" }))
            mouseClick(d.find(pip(ind, "b"), function(o) { return d.typeName(o) === "QQuickMouseArea" }))
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], "c")
        }

        // ── P2 ───────────────────────────────────────────────────────────────
        function test_P2_insertAndRemoveInPlace() {
            var without = eight.filter(function(s) { return s[0] !== "t" })
            var ind = makeIndicator(1000, makeModel(without, "c"))
            compare(shownInOrder(ind), ["g", "c", "b", "i", "k", "h", "r"])
            var before = {}
            pips(ind).forEach(function(p) { before[p.stepKey] = p })
            // Inserted: grows in place, nothing else is rebuilt or moved out of order.
            ind.model = makeModel(eight, "c")
            ind.progress = progressOf(ind.model)
            var t = pip(ind, "t")
            verify(t !== null && t.shown)
            verify(t.grow < 1, "the inserted pip did not animate in")
            settleIndicator(ind)
            compare(shownInOrder(ind), ["g", "c", "t", "b", "i", "k", "h", "r"])
            for (var k in before) verify(pip(ind, k) === before[k], k + " pip was rebuilt")
            // Numbering follows the model (and so the page eyebrow — see P3).
            compare(child(pip(ind, "b"), "flowPipGlyph").text, "4")
            compare(child(pip(ind, "r"), "flowPipGlyph").text, "8")
            // Removed: shrinks in place, then goes; the rest keep their objects and order.
            ind.model = makeModel(without, "c")
            var leaving = pips(ind).filter(function(o) { return o.stepKey === "t" })[0]
            verify(leaving !== undefined && !leaving.present, "the removed pip vanished at once")
            settleIndicator(ind)
            compare(pips(ind).filter(function(o) { return o.stepKey === "t" }).length, 0)
            compare(shownInOrder(ind), ["g", "c", "b", "i", "k", "h", "r"])
            for (var k2 in before) verify(pip(ind, k2) === before[k2], k2 + " pip was rebuilt")
            compare(child(pip(ind, "b"), "flowPipGlyph").text, "3")
        }

        // ── P4 ───────────────────────────────────────────────────────────────
        function checkContainment(ind, label) {
            var checked = 0
            var ps = pips(ind)
            for (var i = 0; i < ps.length; ++i) {
                if (!ps[i].visible || ps[i].width <= 0) continue
                var parts = [child(ps[i], "flowPipDot"), child(ps[i], "flowPipLabel"), child(ps[i], "flowGroupLabel"),
                             child(ps[i], "flowPipAttention")]
                for (var j = 0; j < parts.length; ++j) {
                    if (!parts[j] || !parts[j].visible) continue
                    ++checked
                    verify(!escapes(ind, parts[j]), label + ": " + parts[j].objectName + " of " + ps[i].stepKey
                           + " escapes the strip (" + Math.round(parts[j].mapToItem(ind, 0, 0).x) + "+"
                           + Math.round(parts[j].width) + " in " + Math.round(ind.width) + ")")
                }
            }
            verify(checked >= 8, label + ": expected 8+ items, saw " + checked)
        }
        function test_P4_narrowCollapsesAndContains_data() {
            return [ { tag: "360", width: 360 }, { tag: "240", width: 240 } ]
        }
        function test_P4_narrowCollapsesAndContains(data) {
            var ind = makeIndicator(1000, makeModel(eight, "i", ["c"]))
            verify(!ind.collapsed)
            checkContainment(ind, "1000")
            ind.width = data.width
            settleIndicator(ind)
            verify(ind.collapsed, "not collapsed at " + data.width)
            // The current group (sensors) whole; every other group one pip with a count.
            compare(shownInOrder(ind), ["g", "c", "i", "k", "h", "r"])
            compare(child(pip(ind, "c"), "flowPipLabel").text, "Cameras 2/3")
            compare(child(pip(ind, "c"), "flowPipGlyph").text, "4")       // its first step not passed
            verify(child(pip(ind, "c"), "flowPipAttention").visible, "a collapsed group lost its attention ring")
            compare(child(pip(ind, "g"), "flowPipLabel").text, "Session 1/1")
            compare(child(pip(ind, "g"), "flowPipGlyph").text, "✓")
            checkContainment(ind, String(data.width))
            // Back to full width: everything again.
            ind.width = 1000
            settleIndicator(ind)
            verify(!ind.collapsed)
            compare(shownInOrder(ind), ["g", "c", "t", "b", "i", "k", "h", "r"])
        }

        // ── P3 (through the shell) ───────────────────────────────────────────
        function test_P3_clickDoneJumpsFutureInert() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCamera("DTL1", CameraInstance.DownTheLine, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.open(wrist)
            d.next(); d.next(); d.next(); d.next()       // Goals, Cameras, Framing, Triangulate done → Ball
            compare(d.current(), "ball")
            // The pips' numbers are the eyebrows' (one source, flow.plan).
            compare(d.stepLabel(), "STEP " + d.pip("ball").glyph + " OF 7 · BALL DETECTION")
            d.clickPip("imus")
            compare(d.current(), "ball", "a future pip navigated")
            d.clickPip("triangulate")
            compare(d.current(), "triangulate")
            compare(d.stepLabel(), "STEP 4 OF 7 · TRIANGULATION")
            compare(d.state("triangulate"), "done")
            compare(d.pip("triangulate").state, "current")
            compare(d.pip("goals").glyph, "✓")
        }

        // ── P5 (through the shell) ───────────────────────────────────────────
        function test_P5_attentionRingAndHover() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addCameraInstance("FO1", { ballPresent: true })
            d.open(wrist)
            d.next(); d.next(); d.next()                 // Goals, Cameras, Framing done → Ball
            compare(d.current(), "ball")
            verify(!d.pip("cameras").attention, "attention before anything went wrong")
            d.cams.setCameraSelected("FO1", false)       // the face-on camera drops
            tryVerify(function() { return d.pip("cameras").attention }, 2000, "no attention ring on Cameras")
            compare(d.pip("cameras").tip, "FO1 not connected — press Connect in the cameras step")
            var tip = d.hoverPip("cameras")
            tryVerify(function() { return tip.visible }, 2000, "hover did not show the issue")
            compare(d.find(tip, function(o) { return d.typeName(o) === "QQuickText" }).text,
                    "FO1 not connected — press Connect in the cameras step")
            // The current step's hover is its footer hint.
            compare(d.pip("ball").tip, d.hint())
            // Back to health: the ring goes.
            d.cams.setCameraSelected("FO1", true)
            tryVerify(function() { return !d.pip("cameras").attention }, 2000, "the ring outlived the issue")
        }
    }
}
