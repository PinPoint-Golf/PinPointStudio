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
import "../setup/fakes"

// REQ: REQ-DEV-17
// (the studio bay's camera set-up kept: Settings > Cameras holds a camera's box and mirror flag, and the VIEW combo's role change
// after its role's device settings)

// PpcpPairDialog, loaded for real and driven through its injected controller.
//
// ⚠ THE FAKE IS WHY THE DIALOG TAKES A `controller` PROPERTY AT ALL.  The real
// one is `ppcpHost`, a context property installed only when libppcp and OpenSSL
// are both present, and this binary's setup installs `appSettings` and nothing
// else — so a component that reached for `ppcpHost` directly could not be stood
// up here.  The deleted PpcpPairPanel had the same seam and no test used it.
//
// What is asserted: opening publishes a code rather than waiting for a press,
// the countdown is formatted for a human, each of the four staged status lines
// appears for its own condition, closing invalidates the code (RV 7.3b) but not
// when a phone already got in, and a build with no PPCP renders nothing.
//
// The SECOND TestCase, "SettingsCameras", stands up the other Settings device panel, Settings ▸
// Cameras (CamerasPanel.qml, and through its ball panel PpCameraFrame.qml), over an inline fake
// `cameraManager` and the real AppSettings (this binary's scratch store). It guards a golfer's saved
// camera settings (#268, build/platform/C/NOTES-F31-probe.md): the ball panel shows the saved box and
// never saves the default over it, a click inside it re-saves the same box, a click outside it with no
// drag or a jitter of a pixel or two keeps it, and the Mirrored chip hands the manager the value it was
// clicked to.
Item {
    id: probe
    width: 900; height: 700

    // Stands in for PpcpHostService.  Only the surface PpcpPairDialog binds to,
    // and every name matches the real Q_PROPERTY so a rename breaks this file
    // rather than silently passing against a fake that drifted.
    QtObject {
        id: fakeHost

        signal codeChanged()

        property bool   listening:       true
        property bool   codeLive:        false
        property int    qrSize:          0
        property var    qrRows:          []
        property int    codeSecondsLeft: 0
        property var    codeEndpoints:   []
        property bool   connected:       false
        property int    connectedCount:  0
        property string peerName:        ""
        property string status:          "Waiting for a device on port 7788."
        // A phone that arrived and did not become a link.  Names match the real
        // Q_PROPERTYs so a rename breaks this file rather than drifting.
        property string lastFailureText: ""
        property int    failureCount:    0

        property int publishCount: 0
        property int closeCount:   0

        function publishPairingCode() {
            publishCount += 1
            // A 21-module symbol, which is a real version-1 size.  The contents
            // are not a code and do not need to be: the dialog draws modules.
            var rows = []
            for (var y = 0; y < 21; ++y) {
                var r = ""
                for (var x = 0; x < 21; ++x) r += ((x + y) % 3 === 0) ? "1" : "0"
                rows.push(r)
            }
            qrRows = rows
            qrSize = 21
            codeLive = true
            codeSecondsLeft = 272
            codeEndpoints = ["192.168.1.24:7788", "10.0.0.4:7788"]
            status = "Scan the code with the capture device."
            lastFailureText = ""
            codeChanged()
            return true
        }

        function closePairingCode() {
            closeCount += 1
            codeLive = false
            qrSize = 0
            qrRows = []
            codeSecondsLeft = 0
            codeChanged()
        }
    }

    PpcpPairDialog {
        id: dialog
        controller: fakeHost
    }

    // The H0 case: no libppcp, so no context property and no controller.
    PpcpPairDialog {
        id: nullDialog
        controller: null
    }

    // Smoke-loaded, not driven.  Neither has behaviour worth a suite of its own
    // yet, but both are new and both are reachable only from a screen this
    // binary does not stand up, so "it instantiates without a QML error" is the
    // difference between a typo found here and a typo found by Mark.
    PpQrGlyph {
        id: glyph
        width: 18; height: 18
    }

    PhonesPanel {
        id: phones
        width: 600; height: 400
        controller: null            // the H0 build, which is what a test is
    }

    // ── A host with one phone on it, for the alias field ─────────────────────
    //
    // ⚠ THE ALIAS FIELD HAS BEEN UNTYPEABLE TWICE, and both times the report was
    // the same sentence: "the phone heartbeat refreshes it while I am editing".
    // The first cure (30 Aug) moved `heartbeat_ack` off phonesChanged(); the
    // second (31 Aug) moved `relation_update` off it too.  Neither is what this
    // asserts.  Signals fired at a cadence are a C++ matter and this file cannot
    // see them; what it CAN pin down is the thing that made them fatal — that a
    // rebuild of the list threw away the text and the focus.  With this test the
    // third door, whatever it turns out to be, is an inconvenience rather than
    // an unusable field.
    QtObject {
        id: fakePhoneHost

        // Matches PpcpHostService: `phones` notifies phonesChanged, which QML
        // generates for a `var` property of that name.
        property var phones: [{
            kind: "Phone", pairingId: "pair-one", alias: "", declaredName: "Pixel 8",
            name: "Pixel 8", status: "connected", persisted: true, invalidated: false,
            transport: "wifi", counterpartId: "", batteryPct: 74, batteryBand: "ok", thermal: "nominal",
            syncSigmaMs: 3.5, armState: "disarmed", armBlockedReason: "", armReadyMs: -1,
            dataRateStr: "—", model: "", backend: "PPCP", identifier: "pair-one",
            hasWarning: false
        }]

        signal phoneHealthChanged()

        property int    aliasWrites: 0
        property string lastAlias:   ""

        // The studio name field (PhonesPanel, since fc998857) reads these off PpcpHostService.
        property string studioName: ""
        function defaultStudioName() { return "Test studio" }
        function setStudioName(name) { fakePhoneHost.studioName = name }

        function phoneHealth(pairingId) {
            return { batteryPct: 74, batteryBand: "ok", thermal: "nominal", syncSigmaMs: 3.5 }
        }

        // ⚠ EVERY REFERENCE IS `fakePhoneHost.`-QUALIFIED, AND THE UNQUALIFIED
        // VERSION IS NOT A STYLE CHOICE.  The H0 panel above is `id: phones`,
        // which shadows this object's own `phones` property inside its
        // functions — an id is not an lvalue, so the write failed at runtime
        // with "left-hand side of assignment operator is not an lvalue" and the
        // list never moved.
        //
        // ⚠ AND THE ROWS ARE REBUILT RATHER THAN PATCHED, for the same reason:
        // an object reached through a `var` property is not assignable in
        // place.  The real `phones()` builds fresh QVariantMaps on every read
        // anyway, so this is also the more faithful fake.
        function setPhoneAlias(pairingId, alias) {
            fakePhoneHost.aliasWrites += 1
            fakePhoneHost.lastAlias = alias
            fakePhoneHost.phones = fakePhoneHost.copyRows(pairingId, alias)
        }

        // What a phonesChanged() that is NOT an alias write looks like from the
        // panel's side: the list re-read with a READING moved in it, which is
        // exactly what a `heartbeat_ack` or a `relation_update` used to cause.
        //
        // ⚠ IT HAS TO MOVE SOMETHING.  A list rebuilt with identical contents
        // leaves the delegates standing — Qt diffs it — so an "identical copy"
        // version of this asserted nothing at all: the field it was meant to
        // destroy was never touched.  One changed reading is the difference
        // between a real regression test and a decoration.
        property int heartbeats: 0
        function heartbeat() {
            fakePhoneHost.heartbeats += 1
            var src = fakePhoneHost.phones
            var copy = []
            for (var i = 0; i < src.length; ++i) {
                var row = {}
                for (var k in src[i]) row[k] = src[i][k]
                row.batteryPct  = 74 - fakePhoneHost.heartbeats
                row.syncSigmaMs = 3.5 + fakePhoneHost.heartbeats
                copy.push(row)
            }
            fakePhoneHost.phones = copy
        }

        function copyRows(pairingId, alias) {
            var src = fakePhoneHost.phones
            var copy = []
            for (var i = 0; i < src.length; ++i) {
                var row = {}
                for (var k in src[i]) row[k] = src[i][k]
                if (pairingId && row.pairingId === pairingId) row.alias = alias
                copy.push(row)
            }
            return copy
        }
    }

    PhonesPanel {
        id: livePhones
        width: 600; height: 400
        controller: fakePhoneHost
    }

    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = findByName(item.children[i], name)
            if (found) return found
        }
        return null
    }

    TestCase {
        name: "PpcpPairDialog"
        when: windowShown

        function init() {
            fakeHost.publishCount = 0
            fakeHost.closeCount   = 0
            fakeHost.connected    = false
            fakeHost.peerName     = ""
            if (fakeHost.codeLive) fakeHost.closePairingCode()
            fakeHost.closeCount   = 0
            if (dialog.opened) dialog.close()
        }

        function cleanup() {
            if (dialog.opened) dialog.close()
        }

        function test_opening_shows_a_code_rather_than_a_button_that_makes_one() {
            compare(fakeHost.publishCount, 0)
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            verify(dialog.showing)
        }

        // A code already showing is not displaced by reopening the panel: 7.3d
        // mints a fresh psk and sid every publish, so a needless one would
        // invalidate a code the user may already be scanning.
        function test_reopening_does_not_replace_a_code_already_showing() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            dialog.close()
            // Closing invalidated it, so this open publishes a second.
            dialog.open()
            tryCompare(fakeHost, "publishCount", 2)
            // Now force a live code and reopen without closing it.
            dialog.close()
            fakeHost.publishPairingCode()
            var n = fakeHost.publishCount
            dialog.open()
            compare(fakeHost.publishCount, n)
        }

        // 4.4a — expiry is reported as expiry, and to a human. "272s" was what
        // the deleted panel printed.
        function test_the_countdown_is_minutes_and_seconds() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)

            var line = findByName(dialog.contentItem, "ppcpPairCountdown")
            verify(line !== null)
            verify(line.text.indexOf("4:32") >= 0,
                   "expected 4:32 in \"" + line.text + "\"")

            fakeHost.codeSecondsLeft = 65
            fakeHost.codeChanged()
            verify(line.text.indexOf("1:05") >= 0,
                   "a single-digit second must be padded: \"" + line.text + "\"")
        }

        function test_an_expired_code_says_so_rather_than_counting_below_zero() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            fakeHost.codeSecondsLeft = 0
            fakeHost.codeChanged()

            var line = findByName(dialog.contentItem, "ppcpPairCountdown")
            verify(line.text.indexOf("expired") >= 0, line.text)
        }

        // The four stages, each under its own condition.
        function test_the_staged_line_follows_the_pairing() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            var stage = findByName(dialog.contentItem, "ppcpPairStage")
            verify(stage !== null)

            verify(stage.text.indexOf("Waiting") >= 0, stage.text)
            compare(stage.color, Theme.colorText2)

            // Scanned: a link exists but the peer has not declared yet.
            fakeHost.connected = true
            verify(stage.text.indexOf("securing") >= 0, stage.text)
            compare(stage.color, Theme.colorAccent)

            // Declared: MSG 3.3 gave us the phone's own name.
            fakeHost.peerName = "Pixel 8"
            verify(stage.text.indexOf("Pixel 8") >= 0, stage.text)
            compare(stage.color, Theme.colorGood)

            // No code and no link.
            fakeHost.connected = false
            fakeHost.peerName = ""
            fakeHost.closePairingCode()
            verify(stage.text.indexOf("No code") >= 0, stage.text)
            compare(stage.color, Theme.colorText3)
        }

        // A refused phone displaces "Waiting…", and a phone that gets in
        // afterwards displaces the refusal.
        function test_a_failed_arrival_displaces_the_waiting_line() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            var stage = findByName(dialog.contentItem, "ppcpPairStage")
            verify(stage !== null)
            verify(stage.text.indexOf("Waiting") >= 0, stage.text)

            // A phone arrived and was refused.  ⚠ BOTH properties move: the
            // count is what a repeat of the same failure changes, and the arm
            // reads it so the binding re-evaluates.
            fakeHost.lastFailureText = "A phone connected but did not finish "
                                     + "securing the link in time, and was dropped."
            fakeHost.failureCount = 1
            verify(stage.text.indexOf("did not finish securing") >= 0, stage.text)
            compare(stage.color, Theme.colorWarn)

            // RV 4.4a — expiry is reported AS expiry, on its own line, and a
            // failure must not have swallowed it.
            var countdown = findByName(dialog.contentItem, "ppcpPairCountdown")
            verify(countdown !== null)
            verify(countdown.text.indexOf("expires") >= 0, countdown.text)

            // A phone that gets in afterwards has answered the question.
            fakeHost.connected = true
            fakeHost.peerName = "Pixel 8"
            verify(stage.text.indexOf("Pixel 8") >= 0, stage.text)
            compare(stage.color, Theme.colorGood)

            fakeHost.connected = false
            fakeHost.peerName = ""
            fakeHost.closePairingCode()
        }

        // 7.3b — a displayed code is invalidated when its panel goes, used or
        // not.  The close button and Esc are the same path.
        function test_closing_invalidates_the_code_that_was_showing() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            compare(fakeHost.closeCount, 0)

            var x = findByName(dialog.contentItem, "ppcpPairDialogClose")
            verify(x !== null, "the dialog has no close control")
            mouseClick(x)

            tryCompare(dialog, "opened", false)
            tryCompare(fakeHost, "closeCount", 1)
        }

        // ⚠ THIS TEST USED TO ASSERT THE OPPOSITE, AND THE RULE MOVED RATHER
        // THAN CHANGED.  The panel used to guard its own close with "unless a
        // phone got in first, because closing that code's session would take
        // the link down with it".  That rule now lives in PpcpHostService
        // (`displayedCodePairedAPhone()`), where every caller gets it instead
        // of only this one — and it has to, because the code on screen after a
        // phone pairs is no longer the spent one anyway: a fresh one is minted
        // the moment the link is adopted, so the next angle can just scan.
        //
        // What the panel owes is therefore simply: close the code you are
        // showing.  The invariant this replaced is pinned in C++ by
        // ppcp_host_service_test.
        function test_closing_after_a_phone_connected_still_closes_its_code() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            fakeHost.connected = true
            fakeHost.connectedCount = 1

            dialog.close()
            tryCompare(dialog, "opened", false)
            compare(fakeHost.closeCount, 1)
        }

        // Two angles is the ordinary case, so the panel says how many rather
        // than picking one phone's name to stand for both.
        function test_several_phones_are_counted_not_named() {
            dialog.open()
            tryCompare(fakeHost, "publishCount", 1)
            var stage = findByName(dialog.contentItem, "ppcpPairStage")

            fakeHost.connected = true
            fakeHost.connectedCount = 1
            fakeHost.peerName = "Pixel 8"
            verify(stage.text.indexOf("Pixel 8") >= 0, stage.text)

            // A second angle arrives.  `peerName` goes empty, exactly as the
            // real controller reports it, because there is no honest single
            // answer once there are two.
            fakeHost.connectedCount = 2
            fakeHost.peerName = ""
            verify(stage.text.indexOf("2") >= 0, stage.text)
            compare(stage.color, Theme.colorGood)

            fakeHost.connected = false
            fakeHost.connectedCount = 0
        }

        // H0 — a build with neither libppcp nor OpenSSL has no `ppcpHost`, and
        // the dialog must be inert rather than throwing on every binding.
        // The icon is a QR that must NOT be scannable: three finders and a
        // scatter with no timing pattern, no format stripe and no data. What is
        // checkable here is that it holds still — a seed re-rolled per paint
        // would make the button flicker on every hover.
        function test_the_qr_icon_is_drawn_once_and_holds_still() {
            verify(glyph.modules >= 9)
            var seed = glyph._seed
            verify(seed !== 1, "the seed was never rolled")
            glyph.requestPaint()
            wait(50)
            compare(glyph._seed, seed, "a repaint advanced the generator")
        }

        // RV 7.4b lives here now that the home-screen table is gone: a
        // remembered pairing must stay visible and individually revocable.
        function test_the_phones_panel_stands_up_with_no_ppcp() {
            compare(phones.havePpcp, false)
            compare(phones.rows.length, 0)
            compare(phones.scrollToItem(""), true)
            compare(phones.scrollToItem("no_such_row"), false)
        }

        // The report, twice over: "the edit alias still gets refreshed by the
        // phone heartbeat, making it impossible to edit whilst a phone is
        // connected."  A rebuild lands mid-word here on purpose.
        function test_an_alias_being_typed_survives_the_list_being_rebuilt() {
            var field = findByName(livePhones, "phoneAliasField")
            verify(field !== null, "no alias field in the panel")
            field.forceActiveFocus()
            tryVerify(function() { return field.activeFocus })

            // Unshifted, so lower case is what a key event actually produces.
            keyClick(Qt.Key_B); keyClick(Qt.Key_A); keyClick(Qt.Key_Y)
            compare(field.text, "bay")
            compare(fakePhoneHost.aliasWrites, 0, "a half-typed alias was written down")

            // The phone list is re-read while the caret is between "Bay" and
            // whatever came next.  Every delegate — this field included — is
            // destroyed and built again.
            fakePhoneHost.heartbeat()

            var again = findByName(livePhones, "phoneAliasField")
            verify(again !== field, "the delegate was not rebuilt — the test proves nothing")
            verify(again !== null, "the row did not come back")
            tryVerify(function() { return again.activeFocus }, 2000,
                      "the rebuilt field did not take the focus back")
            compare(again.text, "bay", "the half-typed alias was lost in the rebuild")
            compare(fakePhoneHost.aliasWrites, 0,
                    "the rebuild committed an unfinished alias")

            // Finishing it still writes it down exactly once.
            keyClick(Qt.Key_2)
            keyClick(Qt.Key_Return)
            tryCompare(fakePhoneHost, "aliasWrites", 1)
            compare(fakePhoneHost.lastAlias, "bay2")
        }

        // The other half: a real farewell IS a commit, and must not be mistaken
        // for a rebuild by the deferral that makes the test above pass.
        function test_leaving_the_field_writes_the_alias_down() {
            var field = findByName(livePhones, "phoneAliasField")
            verify(field !== null)
            fakePhoneHost.aliasWrites = 0
            field.forceActiveFocus()
            tryVerify(function() { return field.activeFocus })
            field.selectAll()
            keyClick(Qt.Key_D); keyClick(Qt.Key_T); keyClick(Qt.Key_L)
            probe.forceActiveFocus()          // the user moves on
            tryCompare(fakePhoneHost, "aliasWrites", 1)
            compare(fakePhoneHost.lastAlias, "dtl")
        }

        function test_with_no_controller_there_is_nothing_to_show() {
            compare(nullDialog.showing, false)
            compare(nullDialog.connected, false)
            compare(nullDialog.countdownText, "")
            nullDialog.open()
            tryCompare(nullDialog, "opened", true)
            var stage = findByName(nullDialog.contentItem, "ppcpPairStage")
            verify(stage !== null)
            verify(stage.text.indexOf("No code") >= 0, stage.text)
            nullDialog.close()
        }
    }
    // ══ Settings ▸ Cameras (#268) ══════════════════════════════════════════════════════════════
    //
    // The panel's and the frame's perspective bindings read `Perspective.*`, the one perspective type,
    // registered in this binary as in the app (src/Gui/shell/perspective_qml.h). The face-on perspective is
    // still given below as its code, 2, as the settings store holds it.
    //
    // The cameras' set-up is the Rig's (platform refactor C-5b): `app.rig` is a FakeRig (setup/fakes), and the fake
    // manager's writers ask it what camera_manager.cpp's ask the Rig (setIsMirrored, setBallRoi with "empty means
    // clear", clearBallRoi), so the values asserted are the ones the Rig would hold. Names match the real
    // Q_PROPERTYs and Q_INVOKABLEs.
    readonly property string camKey: "Blackfly S BFS-U3-16S2C|12345678"

    FakeRig { id: camRig }

    QtObject {
        id: fakeCams
        property var  calls: []
        property bool live: true
        property var  inst: null
        property bool isRecording: true
        property bool anyConnecting: false
        property bool impact: false   // impact-capable, with one mode (the VIEW combo's order case)
        readonly property bool anySelected: true
        readonly property var instances: (live && inst) ? [inst] : []
        readonly property var cameraList: [{
            index: 0, cameraKey: probe.camKey, description: "Blackfly S BFS-U3-16S2C", alias: "Face-on",
            perspective: 2, selected: live, enabled: true, sessionEnabled: true, serialNumber: "12345678",
            interface: "USB3", maxWidth: 1440, maxHeight: 1080, maxFps: 226, isPpcp: false, impactCapable: impact,
            impactModes: impact ? [{ w: 640, h: 240, fps: 600 }] : [], gainMaxDb: 48, hwTrigger: false, tuningLockable: false, vendorName: "FLIR",
            modelName: "Blackfly S", pixelFormat: "BayerRG8", bitsPerPixel: 8, slotWidth: 1440, slotHeight: 1080,
            slotBytesPerPixel: 1, slotFps: 226 }]
        function rec(name, args) { calls.push({ name: name, args: args }) }
        function named(name) { return calls.filter(function(c) { return c.name === name }) }

        function setIsMirrored(target, mirrored) {                    // camera_manager.cpp, setIsMirrored
            rec("setIsMirrored", [mirrored])
            if (!target) return
            target.isMirrored = mirrored
            camRig.setMirrored(probe.camKey, mirrored)
        }
        function setBallRoi(target, r) {                              // camera_manager.cpp, setBallRoi
            rec("setBallRoi", [r.x, r.y, r.width, r.height])
            if (!target) return
            var x1 = Math.max(0, Math.min(r.x, r.x + r.width)),  x2 = Math.min(1, Math.max(r.x, r.x + r.width))
            var y1 = Math.max(0, Math.min(r.y, r.y + r.height)), y2 = Math.min(1, Math.max(r.y, r.y + r.height))
            if (x2 - x1 <= 0 || y2 - y1 <= 0) { clearBallRoi(target); return }
            target.roi = Qt.rect(x1, y1, x2 - x1, y2 - y1)
            camRig.setHittingArea(probe.camKey, Qt.rect(x1, y1, x2 - x1, y2 - y1))
        }
        function clearBallRoi(target) {                               // camera_manager.cpp, clearBallRoi
            rec("clearBallRoi", [])
            if (!target) return
            target.roi = Qt.rect(0, 0, 0, 0)
            camRig.clearHittingArea(probe.camKey)
        }
        function relearnBallBaseline(t)    { rec("relearnBallBaseline", []) }
        function setSelected(i, on)        { rec("setSelected", [i, on]) }
        function startAll()                { rec("startAll", []) }
        function enumerate()               { rec("enumerate", []) }
        function createPreviewInstance(i)  { rec("createPreviewInstance", [i]); return null }
        function destroyPreviewInstance(p) { rec("destroyPreviewInstance", []) }
        function setTargetFps(i, f)        { rec("setTargetFps", [i, f]) }
        function setTriggerMode(i, m)      { rec("setTriggerMode", [i, m]) }
        function setCameraAlias(i, a)      { rec("setCameraAlias", [i, a]) }
        function setExcluded(i, e)         { rec("setExcluded", [i, e]) }
        function reconnectCamera(k)        { rec("reconnectCamera", [k]) }
        // What the role's device settings hold when the manager is asked (rig_design_addendum.md decision 8).
        function assignPerspective(k, p)   {
            rec("assignPerspective", [k, p, appSettings.cameraExposureUs[k] !== undefined,
                                      appSettings.cameraRoi[k + "#impact"] !== undefined,
                                      appSettings.cameraTargetFps[k] !== undefined])
        }
    }

    // A live CameraInstance as createController leaves it (camera_manager.cpp:991–1019): the mirror flag
    // applied, the box restored only for a fixed camera.
    Component {
        id: camInstComp
        QtObject {
            property string cameraKey:       probe.camKey
            property string deviceAlias:     "Face-on"
            property int    perspective:     2
            property bool   isMirrored:      false
            property rect   roi:             Qt.rect(0, 0, 0, 0)
            property rect   cropRoi:         Qt.rect(0, 0, 1, 1)
            property bool   needsDebayer:    true
            property int    frameWidth:      1440
            property int    frameHeight:     1080
            property bool   isRecording:     true
            property bool   isPreviewFeed:   false
            property real   configuredFps:   120
            property real   cameraFps:       120
            property string lastPreviewError: ""
            property bool   poseEnabled:     false
            property var    poseKeypoints:   []
            property real   levelPeak:       0
            property bool   levelClipped:    false
            property real   levelBackground: 0
            property bool   ballPresent:     false
            property bool   ballDetected:    false
            property real   ballX:           0
            property real   ballY:           0
            property real   ballRadius:      0
            function setRoi(r)            { roi = r }
            function clearRoi()           { roi = Qt.rect(0, 0, 0, 0) }
            function setCropRoi(r)        { cropRoi = r }
            function addVideoSink(s)      {}
            function removeVideoSink(s)   {}
            function addBayerItem(i)      {}
            function removeBayerItem(i)   {}
            function setSettingsSink(s)   {}
            function startPreview()       {}
            function stopPreview()        {}
            function relearnBallBaseline(){}
        }
    }
    // What PpCameraFrame reads through `app` and `shotProcessor`, inert.
    QtObject {
        id: camShotReplay
        property bool   active: false
        property var    streams: []
        property real   positionUs: 0
        property real   impactPositionUs: 0
        property string swingDir: ""
        property var    analysisDetail: ({})
        signal positionChanged()
        function setVideoSink(i, s) {}
    }
    QtObject {
        id: camShotProcessor
        property bool isReplaying: false
        property var  replayAnalysisDetail: ({})
        property real replayPositionUs: 0
        signal replayPositionChanged()
    }
    QtObject {
        id: camApp
        property var state: ({ replay: camShotReplay, mode: 0, selection: { valid: false, shotRef: "", swingDir: "" } })
        property var shotProcessor: camShotProcessor
        property var rig: camRig
    }

    Item { id: camPanelHost; anchors.fill: parent; z: 100 }
    Component { id: camPanelComp; CamerasPanel { } }

    TestCase {
        id: camTc
        name: "SettingsCameras"
        when: windowShown

        readonly property var savedBox: ({ x: 0.12, y: 0.18, w: 0.25, h: 0.22 })
        readonly property var defaultBox: ({ x: 0.40, y: 0.55, w: 0.20, h: 0.30 })
        property var panel: null

        function initTestCase() {
            chartsFixture.provide("cameraManager", fakeCams)
            chartsFixture.provide("app", camApp)
            chartsFixture.provide("shotProcessor", camShotProcessor)
        }
        function cleanup() {
            if (panel) panel.destroy()
            panel = null
            if (fakeCams.inst) fakeCams.inst.destroy()
            fakeCams.inst = null
            fakeCams.impact = false
            camRig.hittingAreas        = ({})
            camRig.camerasMirrored     = []
            camRig.camerasFixedInPlace = []
            camRig.calls               = []
            appSettings.cameraExposureUs = ({})
            appSettings.cameraRoi        = ({})
            appSettings.cameraTargetFps  = ({})
        }

        function findAll(item, pred, out) {
            out = out || []
            if (!item) return out
            if (pred(item)) out.push(item)
            var kids = item.children
            if (kids) for (var i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
            return out
        }
        function one(obj) { var o = {}; o[probe.camKey] = obj; return o }
        function sameBox(r, b, what) {
            verify(r !== undefined && r !== null, what + ": none")
            fuzzyCompare(r.x, b.x, 1e-9, what + " x"); fuzzyCompare(r.y, b.y, 1e-9, what + " y")
            fuzzyCompare(r.w !== undefined ? r.w : r.width,  b.w, 1e-9, what + " w")
            fuzzyCompare(r.h !== undefined ? r.h : r.height, b.h, 1e-9, what + " h")
        }
        // The store as given, then (when `live`) a connected instance as createController leaves it, then
        // the panel.
        function makePanel(store, live) {
            var saved = store.ballRoi ? store.ballRoi[probe.camKey] : null
            camRig.hittingAreas        = saved ? one(Qt.rect(saved.x, saved.y, saved.w, saved.h)) : ({})
            camRig.camerasMirrored     = store.mirrored ? [probe.camKey] : []
            camRig.camerasFixedInPlace = store.fixed ? [probe.camKey] : []
            camRig.calls = []
            fakeCams.calls = []
            fakeCams.live = live
            var p = { isMirrored: camRig.camerasMirrored.indexOf(probe.camKey) >= 0 }
            var b = camRig.hittingAreaOf(probe.camKey)
            if (camRig.camerasFixedInPlace.indexOf(probe.camKey) >= 0 && b.width > 0) p.roi = b
            fakeCams.inst = live ? camInstComp.createObject(probe, p) : null
            panel = camPanelComp.createObject(camPanelHost, { width: camPanelHost.width, height: camPanelHost.height })
            verify(panel !== null, "CamerasPanel instantiates over the fake")
        }
        // Scroll the panel so `it` is in view: the mouse functions go through the window.
        function scrollTo(it) {
            var flick = panel.scrollView.contentItem
            flick.contentY = Math.max(0, it.mapToItem(flick.contentItem, 0, 0).y - 40)
            waitForRendering(panel)
        }
        // Opens the ball panel the way its button does for a connected camera; returns the frame's editor
        // area and overlay.
        function openBallPanel() {
            var btn = findAll(panel, function(o) { return o.label === "Ball detection" })
            compare(btn.length, 1, "the row's Ball detection button")
            btn[0].clicked()
            compare(panel.openBallIndex, 0)
            var frames
            tryVerify(function() {
                frames = findAll(panel, function(o) { return o.roiEditable === true && o.instance !== undefined })
                return frames.length === 1
            }, 3000, "the ball panel's camera frame")
            // The ball panel opens with a height animation and clips while it runs: wait it out, or the
            // editor's lower part is clipped away and a press there reaches nothing.
            var bp = findAll(panel, function(o) { return o.fixedInPlace !== undefined && o.clip === true })[0]
            tryVerify(function() { return bp.height > 0 && bp.height === bp.children[0].implicitHeight }, 3000,
                      "the ball panel finished opening")
            var area = findAll(frames[0], function(o) { return o.dragMode !== undefined && o.hitZone !== undefined })[0]
            var ov   = findAll(frames[0], function(o) { return o.crW !== undefined && o.crX !== undefined })[0]
            // The row's height animates too, and the card clips: wait until the editor is inside it.
            var row = area; while (row && row.camData === undefined) row = row.parent
            tryVerify(function() { return area.mapToItem(row, 0, 0).y + area.height <= row.height },
                      3000, "the row grew to hold the editor (row " + row.height + ", implicit " + row.implicitHeight + ")")
            scrollTo(area)
            verify(ov.crW > 0 && ov.crH > 0, "the picture has a size (" + ov.crW + " x " + ov.crH + ")")
            return { area: area, ov: ov }
        }

        // The ball panel: the saved box, shown and kept. The click inside the box (a move of nothing)
        // re-saves what the editor shows, so the editor must show the golfer's box (#268 a, B1).
        function test_the_ball_panel_keeps_the_saved_box_data() {
            return [
                { tag: "saved, not fixed", saved: true,  fixed: false, wantSaves: 0 },
                { tag: "nothing saved",    saved: false, fixed: false, wantSaves: 1 },
                { tag: "saved, fixed",     saved: true,  fixed: true,  wantSaves: 0 }
            ]
        }
        function test_the_ball_panel_keeps_the_saved_box(row) {
            makePanel({ ballRoi: row.saved ? one(savedBox) : null, fixed: row.fixed ? one(true) : null }, true)
            var f = openBallPanel()
            compare(fakeCams.named("setBallRoi").length, row.wantSaves, "saves on opening")
            var want = row.saved ? savedBox : defaultBox
            sameBox(fakeCams.inst.roi, want, "the box the editor shows")
            sameBox(camRig.hittingAreaOf(probe.camKey), want, "the box the Rig holds")
            // A plain click inside the box.
            var px = f.ov.x + f.ov.width / 2, py = f.ov.y + f.ov.height / 2
            compare(f.area.hitZone(px, py), "move")
            mouseClick(f.area, px, py)
            sameBox(camRig.hittingAreaOf(probe.camKey), want, "the box the Rig holds after a click inside it")
        }

        // Outside the box: a click, with no drag or a jitter of a pixel or two, keeps the box; a drag
        // draws a new one (#268 b, B2).
        function test_a_click_outside_the_box_keeps_it_data() {
            return [
                { tag: "no drag",       dx: 0,   dy: 0,   drawn: false },
                { tag: "2 px jitter",   dx: 2,   dy: 2,   drawn: false },
                { tag: "a real drag",   dx: -80, dy: -60, drawn: true }
            ]
        }
        function test_a_click_outside_the_box_keeps_it(row) {
            makePanel({ ballRoi: one(savedBox), fixed: one(true) }, true)
            var f = openBallPanel()
            // Below and right of the box, inside the picture.
            var px = f.ov.crX + f.ov.crW * 0.8, py = f.ov.crY + f.ov.crH * 0.8
            compare(f.area.hitZone(px, py), "new")
            mousePress(f.area, px, py)
            compare(f.area.dragMode, "new", "the press reached the editor")
            if (row.dx !== 0 || row.dy !== 0) {
                mouseMove(f.area, px + row.dx / 2, py + row.dy / 2)
                mouseMove(f.area, px + row.dx, py + row.dy)
            }
            mouseRelease(f.area, px + row.dx, py + row.dy)
            compare(fakeCams.named("clearBallRoi").length, 0, "nothing clears the box")
            if (row.drawn) {
                compare(fakeCams.named("setBallRoi").length, 1, "the drawn box is saved")
                var r = camRig.hittingAreaOf(probe.camKey)
                verify(r.width > 0.1 && r.height > 0.1, "the drawn box " + JSON.stringify(r))
            } else {
                compare(fakeCams.named("setBallRoi").length, 0, "a click saves nothing")
                sameBox(fakeCams.inst.roi, savedBox, "the live box")
                sameBox(camRig.hittingAreaOf(probe.camKey), savedBox, "the box the Rig holds")
            }
        }

        // The Mirrored chip hands the manager the value it was clicked to, not the old one, so the click
        // sticks with a camera connected (#268 c).
        function test_the_mirror_chip_keeps_the_click_data() {
            return [
                { tag: "connected, mirrored → off",     live: true,  from: true },
                { tag: "connected, not mirrored → on",  live: true,  from: false },
                { tag: "not connected, mirrored → off", live: false, from: true }
            ]
        }
        function test_the_mirror_chip_keeps_the_click(row) {
            makePanel({ mirrored: row.from ? one(true) : null }, row.live)
            var chip = findAll(panel, function(o) { return o.label === "Mirrored" && o.isMirrored !== undefined })
            compare(chip.length, 1, "the row's Mirrored chip")
            compare(chip[0].isMirrored, row.from)
            scrollTo(chip[0])
            mouseClick(chip[0])
            var calls = fakeCams.named("setIsMirrored")
            if (row.live) {
                compare(calls.length, 1, "one live apply")
                compare(calls[0].args[0], !row.from, "the manager is handed the clicked value")
                compare(fakeCams.inst.isMirrored, !row.from)
            } else {
                compare(calls.length, 0, "no camera to apply it to")
            }
            compare(camRig.camerasMirrored.indexOf(probe.camKey) >= 0, !row.from, "the flag the Rig holds")
            var asked = camRig.calls.filter(function(c) { return c.name === "setMirrored" })
            verify(asked.length >= 1 && asked[0].args[1] === !row.from, "the Rig is asked for the clicked value first")
            compare(chip[0].isMirrored, !row.from, "the chip shows the click")
        }

        // Choosing Impact in the VIEW combo writes that role's device settings (the exposure, the #impact crop, the
        // rate) BEFORE the manager asks the Rig for the role, so the reconnect the role change brings reads them
        // (rig_design_addendum.md decision 8: the precondition of assignPerspective, pinned here).
        function test_the_view_combo_writes_the_role_settings_first() {
            fakeCams.impact = true
            makePanel({}, true)
            var combos = findAll(panel, function(o) { return o.viewOptions !== undefined && o.activated !== undefined })
            compare(combos.length, 1, "the row's VIEW combo")
            var opts = combos[0].viewOptions, idx = -1
            for (var i = 0; i < opts.length; ++i) if (opts[i].perspective === Perspective.Impact) idx = i
            verify(idx >= 0, "Impact is offered for an impact-capable camera")
            combos[0].activated(idx)
            var calls = fakeCams.named("assignPerspective")
            compare(calls.length, 1, "one role change asked for")
            compare(calls[0].args[0], probe.camKey)
            compare(calls[0].args[1], Perspective.Impact)
            verify(calls[0].args[2] && calls[0].args[3] && calls[0].args[4],
                   "the exposure, the #impact crop and the rate were written before the role change")
        }
    }
}
