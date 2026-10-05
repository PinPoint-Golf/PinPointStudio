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

// The session diagnostics stage panel: what keeps happening this session, and what the
// model thinks is causing it. Per session and cumulative, and most of its design is the
// restraint that keeps it from ever saying more than the ledger supports.
//
// THIS FILE IS THE WIRING AND NOTHING ELSE. Everything the panel LOOKS like is
// PpSessionDiagnosticsBody, which takes a `source` and owns none of it. The split is the
// one PpLaunchMonitorPanel makes with PpLmGraphicsBody, and it buys the same thing twice
// over: the body can be loaded offscreen with a fixture, so the states nobody can produce
// on demand — Cold with an empty fault profile, the bandwidth-quiet strip, a
// not-assessable tick — are asserted rather than eyeballed; and there is exactly one place
// where this panel touches the app's singletons, which is here.
//
// THE MODEL IS INSTANTIATED, NOT REACHED FOR. SessionDiagnosticsModel owns a characteristic
// pack, a norm provider and a worker thread that re-runs detection on every shot, and this
// panel is OFF by default — the user turns it on in View. A golfer who never opens it
// should not be paying for any of that, which is why the model is not a context property in
// main.cpp. See the class comment on SessionDiagnosticsModel.
//
// CADENCE GATES SURFACING, NEVER THE ENGINE. ingestShot() below is wired straight to
// ShotProcessor::shotProcessed with no cadence check anywhere near it, and there must never
// be one: the ledger accumulates at full rate whatever the panel is showing (brief §3.4,
// §5.6). ingestShot() is idempotent per shot id, which is what makes it safe to wire this
// way and to call again from activateSession()'s back-fill.

import QtQuick
import PinPointStudio

Item {
    id: root

    // Set by PpModeStage only on the muted placeholder path; the panel titles itself.
    property string title: qsTr("Session diagnostics")

    // THE ONE INDIRECTION THE BODY READS THROUGH. Left at diagModel in the app; a test or a
    // second host can point the body at a different object of the same shape without this
    // file having to know.
    property alias source: body.source

    // Off on the auto-closing post-shot pop, where the focus tap and the miss picker would be
    // armed on a surface that is about to vanish under the pointer. See
    // PpSessionDiagnosticsWindow.interactive; nothing about what the panel SAYS changes.
    property alias interactive: body.interactive

    // A screen was asked for — the driver footer's CTA, or a screened root on the rail.
    //
    // DELIBERATELY UNCONNECTED. Running a screen is a thirty-second physical test with a
    // protocol to read and a present/absent to record, and that flow is not designed (brief
    // §9: "tapping a chain node through … come back for a design"). The model already has the
    // other half — SessionDiagnosticsModel::recordScreenResult() — so when the protocol UI
    // lands it hangs off this signal and nothing on the panel moves. What must not happen in
    // the meantime is a screen UI invented here, because a screened root presented as settled
    // by a guess is precisely the claim §5.4 forbids.
    signal screenRequested(string screenRef, string conditionId)

    SessionDiagnosticsModel {
        id: diagModel

        // Both passed in rather than reached for, so the model stays constructible in a
        // test. gradePolicy must be the SAME policy the other metric surfaces and the
        // launch monitor board grade against, or one reading would sit outside its
        // corridor on one panel and inside it on another.
        cadence:     appSettings.sessionDiagnosticsCadence
        gradePolicy: appSettings.diagnosticsGradePolicy

        // Review FREEZES the ledger at Closing: a finished session's panel is its summary
        // and must not re-open because it was looked at.
        reviewing:   sessionReviewController.reviewActive

        // THE CAROUSEL OWNS THE SELECTION AND THE PANEL ONLY READS IT (brief §6.1, §8). The
        // carousel is already at the foot of the stage and is post-session the ONLY way in;
        // a second selection control on the panel would be a second answer to "which shot",
        // and the two would disagree the moment either was used.
        //
        // BY SWING FOLDER, NOT BY ID. SessionMode.focusedShotId is the carousel model's own
        // counter and this model numbers shots differently, so passing the id selected nothing
        // and THIS SHOT stayed empty. The folder is the identity both sides share; the model
        // resolves it to its own id (SessionDiagnosticsModel::selectedSwingDir).
        selectedSwingDir: SessionMode.focusedSwingDir
    }

    // THE SESSION THE PANEL IS POINTED AT — the loaded one while reviewing, the live one
    // otherwise. A session's id IS its directory, which is why one property answers both:
    // SessionReviewController::loadSession() takes the same string and reads the same
    // swing_* dirs out of it that activateSession() reconciles against.
    //
    // activateSession() covers three situations that are the same problem — opening a
    // finished session, resuming after a crash, and turning this panel on half way through a
    // session — by reconciling diagnostics.json against the swing_* directories beside it and
    // back-filling the difference. Entering and leaving review is a fourth, and it is the
    // same call for the same reason.
    //
    // LIVE IS TODAY'S SESSION WHETHER OR NOT IT IS RUNNING. shotProcessor.activeSessionDir is
    // set only between a session's start and its end, and this used to read nothing else: end
    // the session, or relaunch the app, and the panel was pointed at "" and cleared — all three
    // tabs blank over a carousel still showing the day's swings. So when no session is running
    // it reads the folder the carousel is showing (sessionReviewController.liveSessionDir).
    // The running session's own folder comes first: it exists before its first swing does.
    readonly property string sessionDir:
        (sessionReviewController.reviewActive
         && sessionReviewController.activeSessionId !== "")
            ? sessionReviewController.activeSessionId
            : shotProcessor.activeSessionDir !== ""
                ? shotProcessor.activeSessionDir
                : sessionReviewController.liveSessionDir

    // GUARDED ON THE MODEL'S OWN ANSWER, not on a remembered one. activateSession() is a disk
    // scan and a back-fill, so it must not run again for a session already loaded — and
    // asking the model what it holds is what makes leaving review correct: coming back to a
    // live directory the panel was pointed at BEFORE the excursion still differs from the
    // reviewed one the model is holding now, so the live ledger is rebuilt rather than the
    // reviewed one being left on screen in the live tense. Empty is a real value here (a live
    // session before its first export) and clears the model, for the same reason.
    //
    // COMPARED AS FOLDERS, NOT AS STRINGS: the two live sources spell the same folder
    // differently when the library path ends in a slash ("…/lib//athlete/…" from the
    // processor, "…/lib/athlete/…" from the carousel), and a session ending must not cost a
    // second scan of the folder the panel is already on.
    function _folder(p) { return p.replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "") }
    function _pointAtSession() {
        if (_folder(sessionDir) !== _folder(diagModel.sessionDir))
            diagModel.activateSession(sessionDir)
    }

    // ORDER IS DELIBERATELY NOT PINNED between this and the `reviewing` binding above, which
    // reviewActiveChanged also dirties: both republish the WHOLE surface, so whichever lands
    // second publishes a state that has both facts in it, and the pair converges within the
    // same event-loop turn either way. Pinning it would mean breaking the declarative binding
    // on `reviewing` to set it by hand, which buys a transient nobody can observe.
    onSessionDirChanged: { _pointAtSession(); _refreshReadout() }
    Component.onCompleted: {
        _pointAtSession()
        // ⚠ FETCHED HERE AS WELL AS FROM THE SIGNALS, and it has to be. The panel is built when
        // its stage tab first shows — typically on entering Analyse with a shot already picked —
        // and _pointAtSession() loads the ledger and resolves that pick SYNCHRONOUSLY, inside
        // this handler, before the Connections below are connected. The selectedShotIdChanged
        // it emits is lost, nothing else changes afterwards, and THIS SHOT sat on "pick a shot"
        // with shot 8 selected and readable. Reproduced and verified end to end with
        // --probe-qml (load a session, pick shot 8, enter Analyse).
        _refreshReadout()
        // THE CAROUSEL'S SEAM, CLAIMED UNCONDITIONALLY BY THE MOST RECENT PANEL. Both the
        // session-mode and the wrist screens can host one of these, and the shot cards read
        // their pips through the singleton — so the last panel to come up is the one whose
        // ledger the cards draw. That is the right answer because it is the one the user is
        // looking at, and it is the only answer that does not need a registry.
        SessionMode.sessionDiagnostics = diagModel
    }
    // ...and released only if it is still OURS. A panel being torn down while a second one
    // holds the seam must not null a pointer it no longer owns — which is exactly what
    // happens when the user switches screens and the outgoing panel is destroyed after the
    // incoming one has already claimed it.
    Component.onDestruction:
        if (SessionMode.sessionDiagnostics === diagModel)
            SessionMode.sessionDiagnostics = null

    Connections {
        target: shotProcessor
        function onShotProcessed(shotId, swingDir) {
            diagModel.ingestShot(shotId, swingDir)
        }
    }

    // shotReadout() IS AN INVOKABLE, SO IT CANNOT BE BOUND — somebody has to call it, and
    // this is the only file with the model's signals in front of it. Re-fetched on all three
    // things that can change the answer: which shot is selected, whether the panel is in
    // review (the ledger it reads against), and any republication of the surface (a late back-fill landing changes
    // the tiers the readout's cells carry).
    function _refreshReadout() {
        // Live as well as in review: the body decides what a live pick of the newest swing
        // shows (the after-shot strip), and needs the readout for every other pick.
        body.readout = diagModel.selectedShotId >= 0
                       ? diagModel.shotReadout(diagModel.selectedShotId)
                       : null
    }

    Connections {
        target: diagModel
        function onSurfaceChanged()        { root._refreshReadout() }
        function onSelectedShotIdChanged() { root._refreshReadout() }
        function onReviewingChanged()      { root._refreshReadout() }

        // THE AFTER-SHOT MOMENT, handed to the body the same way the readout is: shotIngested
        // is a signal and the body has no model to connect to, so the one file with the
        // model's signals in front of it does the connecting.
        //
        // GATED ON `surfaced`, which is cadence's answer and not a second one taken here.
        // Bandwidth mode decided this shot was not worth interrupting for; pulsing the cards
        // anyway would be the interruption it declined, arriving by a different route. That is
        // the same rule as the quiet strip and it has to hold on both or on neither.
        function onShotIngested(shotId, surfaced) {
            if (surfaced) body.pulseAfterShot()
        }
    }

    PpSessionDiagnosticsBody {
        id: body
        anchors.fill: parent
        source: diagModel
        onScreenRequested: (ref, cond) => root.screenRequested(ref, cond)
    }
}
