// Stage 0 part B1 — the Check page (catalogue group CK,
// docs/design/session_wizard_refactor_design.md §7.3).
//
// Cases assert the catalogue's EXPECTED column. All access goes through support/SetupDriver.qml.
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
    SetupDriver { id: drv; host: host; testCase: tc; testLog: root._testLog }

    TestCase {
        id: tc
        name: "SetupCheck"
        when: windowShown

        // The adapter (support/SetupDriver.qml).
        readonly property var d: drv

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

        // ── CK1 ──────────────────────────────────────────────────────────────
        function test_CK01_liveWristActiveOnlyOnCheck() {
            d.addCamera("FO1", CameraInstance.FaceOn, true)
            d.addWitmotion("WT-A", "A", true)
            d.addWitmotion("WT-B", "B", true)
            d.open(wrist)
            d.walkTo("checkArm")
            verify(d.liveWrist.active, "liveWrist not active on Check")
            d.back()
            compare(d.current(), "calibrateArm")
            verify(!d.liveWrist.active, "liveWrist still active after leaving Check")

            // Another user (the trunk check view, the toolbar) turns it on while the wizard
            // sits on another step; the wizard then moves between two non-Check steps.
            d.liveWrist.active = true
            d.back()
            compare(d.current(), "imus")
            var log = d.liveWrist.activeLog.map(function(e) { return e.value })
            console.info("[CK1] liveWrist.active write log: " + JSON.stringify(log))
            verify(d.liveWrist.active, "liveWrist.active was overridden to false by the wizard")
        }

        // ── CK2 ──────────────────────────────────────────────────────────────
        function test_CK02_noArmVizOffCheck() {
            d.open(wrist)
            compare(d.current(), "goals")
            var n = d.armVizCount()
            console.info("[CK2] ArmVizView instances while on Goals: " + n)
            compare(n, 0)
        }
    }
}
