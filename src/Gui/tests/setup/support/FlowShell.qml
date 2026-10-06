// FlowShell — a DUMMY host for the Stage 4 engine (tst_setup_flow.qml): the wiring a real setup
// shell does (SetupFlow.qml's header comment), and nothing else — no header, footer or indicator.
//
// Built with harness.createWithContext() over the fakes, so SetupContext's inputs DEFAULT to the
// context properties exactly as they will in the app (cameraManager, imuManager, launchMonitor,
// athleteController, appLog) plus `pageJournal`, which the test pages report to.
//
// Every built-in step key loads support/pages/DummyPage.qml (SetupSteps.pageOverride): the real
// pages are Stage 5's. `pageOverride` (an initial property) replaces single keys.
import QtQuick
import PinPointStudio

Item {
    id: shell

    property var pageOverride: ({})

    readonly property url dummyPage: Qt.resolvedUrl("pages/DummyPage.qml")
    readonly property var _overrides: {
        var keys = ["goals", "cameras", "framing", "triangulate", "ball", "imus", "calibrateArm", "checkArm", "ready"]
        var out = {}
        for (var i = 0; i < keys.length; ++i) out[keys[i]] = shell.dummyPage
        for (var k in shell.pageOverride) out[k] = shell.pageOverride[k]
        return out
    }

    readonly property var flow:  flowObj
    readonly property var ctx:   ctxObj
    readonly property var draft: draftObj
    readonly property var steps: stepsObj
    readonly property Item loader: pageLoader

    SetupContext { id: ctxObj;   draft: draftObj }
    SetupDraft   { id: draftObj; ctx: ctxObj }
    SetupSteps   { id: stepsObj; pageOverride: shell._overrides }

    Loader { id: pageLoader; anchors.fill: parent }

    SetupFlow {
        id: flowObj
        registry: stepsObj
        ctx:      ctxObj
        draft:    draftObj
        loader:   pageLoader
    }
}
