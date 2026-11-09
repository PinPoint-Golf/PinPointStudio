// swing3d_session.qml — the 3-D panel's session view against a real session, WITHOUT screenshots
// (docs/design/swing_3d_session_synthesis_design.md §7, G3, G5 and G6).
//
//   QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
//     .../PinPointStudio.app/Contents/MacOS/PinPointStudio \
//     --probe-qml /abs/path/tools/probes/swing3d_session.qml --probe-swing <a swing dir in the session>
//
// Every line is prefixed "S3DSESSION". On a PRIVATE host (a diagnostic must not write the View
// layout): the time of the first open, what the synthesis reports, the clubhead against the ball at
// impact, the toggle both ways, another shot's clock. On the screen's own host: the session view is
// offered only for a session opened from the library.

import QtQml
import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function _arg(name, dflt) {
        var i = Qt.application.arguments.indexOf(name)
        return (i >= 0 && i + 1 < Qt.application.arguments.length) ? Qt.application.arguments[i + 1] : dflt
    }
    readonly property string swingDir: probe._arg("--probe-swing", "")
    readonly property string sessionDir: swingDir.substring(0, swingDir.lastIndexOf("/"))
    readonly property string otherShot: probe._arg("--probe-other", sessionDir + "/swing_0002")

    function say(s) { console.warn("S3DSESSION " + s) }

    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var kids = item.children
        for (var i = 0; i < kids.length; ++i) {
            var hit = probe.find(kids[i], name)
            if (hit) return hit
        }
        return null
    }

    Item { id: holder; width: 720; height: 540 }
    Component { id: slotComp; Item { width: 720; height: 540 } }

    SwingViz3DHost {
        id: host
        swingDir: probe.swingDir
        sessionAvailable: true
        positionUs: -1
    }

    function posTime(d, p) {
        for (var i = 0; i < d.positionCount; ++i) if (d.positionP(i) === p) return d.positionTimeUs(i)
        return -1
    }
    function posIndex(d, p) {
        for (var i = 0; i < d.positionCount; ++i) if (d.positionP(i) === p) return i
        return -1
    }

    property int step: 0
    property real t0: 0
    property string phase: "open"
    property var screenHost: null
    property var theSlot: null
    property int shotN: 0
    function ballGap(d) {
        var i7 = probe.posIndex(d, 7)
        if (i7 < 0) return "no P7"
        var h = d.positionHead(i7, d.revision), b = d.ballPosition
        return (Math.sqrt(Math.pow(h.x - b.x, 2) + Math.pow(h.y - b.y, 2) + Math.pow(h.z - b.z, 2)) * 100).toFixed(2)
    }

    Timer {
        interval: 100
        repeat: true
        running: true
        onTriggered: probe.advance()
    }

    function advance() {
        step += 1
        if (step === 1) {
            say("banner swing=" + probe.swingDir)
            if (probe.swingDir === "") { say("FAIL no --probe-swing"); Qt.quit(); return }
            probe.t0 = Date.now()
            probe.theSlot = slotComp.createObject(holder)
            host.attach(probe.theSlot)
            return
        }
        var v = host.view
        if (!v) { say("FAIL no view"); Qt.quit(); return }
        var d = v.driver
        if (phase === "open") {
            if (d.loading) return
            if (!d.available && d.sessionShots === 0 && d.sessionFallback === "" && step < 900) return
            say("first open ms=" + (Date.now() - probe.t0))
            say("scope=" + v.scope + " sessionScope=" + d.sessionScope + " shots=" + d.sessionShots + " planeShots=" + d.sessionPlaneShots
                + " fallback='" + d.sessionFallback + "' faceOnOnly=" + d.faceOnOnly + " clubUncertain=" + d.clubAfterImpactUncertain
                + " available=" + d.available + " reason='" + d.reason + "'")
            say("span=" + d.startUs + ".." + d.endUs + " positions=" + d.positionCount
                + " cams fo=" + d.cameraAvailable(0) + " dtl=" + d.cameraAvailable(1)
                + " plane=" + d.planeAvailable + (d.planeAvailable ? " incl=" + d.planeInclDeg.toFixed(1) : ""))
            var i7 = probe.posIndex(d, 7)
            if (i7 >= 0) {
                var head = d.positionHead(i7, d.revision), ball = d.ballPosition
                var gap = Math.sqrt(Math.pow(head.x - ball.x, 2) + Math.pow(head.y - ball.y, 2) + Math.pow(head.z - ball.z, 2)) * 100
                say("clubhead to ball at impact cm=" + gap.toFixed(2))
            }
            var toggle = probe.find(v, "scopeToggle")
            say("toggle visible=" + (toggle ? toggle.visible : "none") + " options=" + (toggle ? toggle.options : "")
                + " selected=" + (toggle ? toggle.selected : ""))
            say("P4 of this shot at " + probe.posTime(d, 4))
            probe.t0 = Date.now()
            toggle.activated("This shot")
            phase = "shot"
            return
        }
        if (phase === "shot") {
            if (d.loading) return
            say("this shot ms=" + (Date.now() - probe.t0) + ": scope=" + v.scope + " shots=" + d.sessionShots + " cams fo=" + d.cameraAvailable(0)
                + " available=" + d.available + " head to ball cm=" + probe.ballGap(d))
            if (probe.shotN < 8) {
                probe.shotN += 1
                host.swingDir = probe.sessionDir + "/swing_000" + probe.shotN
                probe.t0 = Date.now()
                return
            }
            host.swingDir = probe.swingDir
            probe.t0 = Date.now()
            probe.find(v, "scopeToggle").activated("Session")
            phase = "back"
            return
        }
        if (phase === "back") {
            if (d.loading) return
            say("back to session ms=" + (Date.now() - probe.t0) + ": shots=" + d.sessionShots + " P4 at " + probe.posTime(d, 4))
            probe.t0 = Date.now()
            host.swingDir = probe.otherShot
            say("other shot, same tick: loading=" + d.loading + " available=" + d.available + " P4 at " + probe.posTime(d, 4)
                + " ms=" + (Date.now() - probe.t0))
            host.detach(probe.theSlot)
            say("panel hidden: panelShown=" + d.panelShown)
            host.swingDir = probe.swingDir
            say("focus while hidden: loading=" + d.loading + " P4 still at " + probe.posTime(d, 4))
            host.attach(probe.theSlot)
            say("panel back: loading=" + d.loading + " P4 at " + probe.posTime(d, 4))
            screenHost = probe.find(probe.Window.window.contentItem, "swing3dHost")
            say("screen host found=" + (screenHost !== null) + " sessionAvailable before loading a session=" + (screenHost ? screenHost.sessionAvailable : "none")
                + " reviewActive=" + sessionReviewController.reviewActive)
            sessionReviewController.loadSession(probe.sessionDir)
            phase = "review"
            step = 1000
            return
        }
        if (phase === "review") {
            if (!sessionReviewController.reviewActive && step < 1100) return
            say("library session open: reviewActive=" + sessionReviewController.reviewActive
                + " screen host sessionAvailable=" + (screenHost ? screenHost.sessionAvailable : "none") + " mode=" + SessionMode.mode)
            SessionMode.enterCapture()
            say("back in Capture: reviewActive=" + sessionReviewController.reviewActive
                + " screen host sessionAvailable=" + (screenHost ? screenHost.sessionAvailable : "none"))
            say("DONE")
            Qt.quit()
        }
    }
}
