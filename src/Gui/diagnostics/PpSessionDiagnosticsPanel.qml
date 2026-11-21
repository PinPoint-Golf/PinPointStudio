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
// THE SESSION'S DIAGNOSTICS ARE REACHED, NOT MADE (refactor step 3.7-a; build/refactor/step-3/BRIEF.md §4.10).
// The panel used to instantiate a private model that judged the session itself, and only while
// the panel was up — so with it off (the default) no shot was judged (audit D-02). Now every
// loaded Session's one SessionDiagnostics judges every shot, panel or not (DEC-02), and the panel
// shows it: the folder it chooses (sessionDir, below: still its own choice until step 4's
// sessionOnScreen) names a Session through app.sessionAt (D1: DEC-164), and
// diagnosticsAdapters.of() answers that Session's thin adapter — made by C++, one per
// Session and surface (the stage panel and the wall cast each keep their own view state over the
// one owner). See the class comment on SessionDiagnosticsModel's adapter half.
//
// CADENCE GATES SURFACING, NEVER THE ENGINE. Nothing here feeds a shot to anything: the owner
// hears each shot's document land (EVT-01) and its late parts (EVT-22) itself, so the ledger
// accumulates at full rate whatever the panel is showing (brief §3.4, §5.6).

import QtQuick
import PinPointStudio

Item {
    id: root

    // Set by PpModeStage only on the muted placeholder path; the panel titles itself.
    property string title: qsTr("Session diagnostics")

    // THE FRAME IS WHOEVER HOSTS IT. On the stage PpModeStage switches this off on load and
    // frames the panel in its own SESSION DIAGNOSTICS card, with `cardAside` on the card's
    // heading row; the wall cast (PpSessionDiagnosticsWindow) has no stage around it and leaves
    // it on, so the body draws the same card itself. See PpSessionDiagnosticsBody.framed.
    property alias framed: body.framed
    readonly property string cardAside: body.cardAside

    // THE ONE INDIRECTION THE BODY READS THROUGH. Left at diagModel in the app; a test or a
    // second host can point the body at a different object of the same shape without this
    // file having to know.
    property alias source: body.source

    // Off on the auto-closing post-shot pop, where the focus tap and the miss picker would be
    // armed on a surface that is about to vanish under the pointer. See
    // PpSessionDiagnosticsWindow.interactive; nothing about what the panel SAYS changes.
    property alias interactive: body.interactive

    // "panel" (the stage) or "cast" (PpSessionDiagnosticsWindow). The cast always reads the Replay
    // register and draws its own running head, so the body's frame stands down there (D4, S30).
    property alias surface: body.surface

    // Whether this panel keeps the reader's place across its own rebuilds (SessionMode
    // .diagnosticsNav). On for the stage panel, which PpModeStage tears down and rebuilds on
    // every mode switch; off for the secondary-display cast, which has its own lifetime and
    // must not overwrite where the reader was on the stage.
    property bool remembersNav: true

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

    // ── the Session this panel shows, and its thin adapter (3.7-a) ──────────────
    //
    // app.sessionAt(folder) asks the CURRENT athlete for the Session of the folder; null when no
    // athlete holds it — a session captured with no golfer (the `unknown` folder: Mark's N1, kept
    // with no diagnostics), or no folder yet. Asked again when the athlete changes (the folder may
    // stay the same string). The adapter for that Session and this surface is never null: with no
    // Session it is an EMPTY one, which judges nothing, and the panel says why (noGolferNotice).
    readonly property var session:
        (typeof app !== "undefined" && app && sessionDir !== ""
         && (typeof athleteController === "undefined" || athleteController === null
             || athleteController.currentUuid !== ""))   // a golfer chosen (and asked again when it changes)
            ? app.sessionAt(sessionDir) : null
    readonly property var diagModel:
        (typeof diagnosticsAdapters !== "undefined" && diagnosticsAdapters) ? diagnosticsAdapters.of(session, surface, sessionDir) : null
    // SHOW WITH A WARNING, NEVER HIDE (Mark's N1): a folder the panel is pointed at that no Session
    // of the current golfer owns has no diagnostics; the panel says so instead of drawing an empty
    // panel as if nothing had happened.
    // (A folder in the golfer's own folder that no Session of theirs holds — another athlete id in its file — gets
    // today's engine model instead, made by C++: not an adapter, so no notice. session_diagnostics_adapter.h.)
    readonly property bool noGolfer: sessionDir !== "" && session === null && !!diagModel && diagModel.adapter
    // What is true of the folder (review M6): no golfer chosen here; a session recorded with no golfer (the library's
    // `unknown` folder); or another golfer's session.
    readonly property string noGolferText: {
        const parts = SessionMode.folderKey(sessionDir).split("/")
        const owner = parts.length >= 2 ? parts[parts.length - 2] : ""
        if (typeof athleteController !== "undefined" && athleteController && athleteController.currentUuid === "")
            return qsTr("No golfer is chosen, so this session's swings are not judged. Choose a golfer to see its diagnostics.")
        if (owner === "unknown")
            return qsTr("This session was recorded with no golfer, so its swings are not judged. Move it to a golfer to see its diagnostics.")
        return qsTr("This session is not one of the chosen golfer's, so its swings are not judged here.")
    }

    // NEVER RESTORED (RestoreNone): the adapter outlives this panel, and the stage builds the incoming
    // panel before it destroys the outgoing one, so a restore would undo the incoming panel's values.
    // Both passed in rather than reached for. gradePolicy must be the SAME policy the other metric
    // surfaces and the launch monitor board grade against, or one reading would sit outside its
    // corridor on one panel and inside it on another.
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "cadence";     value: appSettings.sessionDiagnosticsCadence }
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "gradePolicy"; value: appSettings.diagnosticsGradePolicy }
    // Review FREEZES the ledger at Closing: a finished session's panel is its summary and must not
    // re-open because it was looked at.
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "reviewing";   value: sessionReviewController.reviewActive }
    // THE CAROUSEL OWNS THE SELECTION AND THE PANEL ONLY READS IT (brief §6.1, §8). The carousel
    // is already at the foot of the stage and is post-session the ONLY way in; a second selection
    // control on the panel would be a second answer to "which shot", and the two would disagree
    // the moment either was used.
    //
    // BY SWING FOLDER, NOT BY ID. SessionMode.focusedShotId is the carousel model's own counter and
    // the ledger numbers shots by swing number, so the folder is the identity both sides share; the
    // adapter resolves it to its own id (SessionDiagnosticsModel::selectedSwingDir).
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "selectedSwingDir"; value: SessionMode.focusedSwingDir }

    // THE SESSION THE PANEL IS POINTED AT — the loaded one while reviewing, the live one
    // otherwise. The folder names the Session (app.sessionAt, above); the Session's owner has
    // already read it — opening a finished session, resuming after a crash, and turning this panel
    // on half way through a session are all "the owner holds it" now (it judged every shot as it
    // landed). activateSession() on the adapter only brings what the panel shows up to date
    // (pointAdapter: the club baseline, the athlete's history).
    //
    // LIVE IS TODAY'S SESSION WHETHER OR NOT IT IS RUNNING. shotProcessor.activeSessionDir is
    // set only between a session's start and its end, and this used to read nothing else: end
    // the session, or relaunch the app, and the panel was pointed at "" and cleared — all three
    // tabs blank over a carousel still showing the day's swings. So when no session is running
    // it reads the folder the carousel is showing (sessionReviewController.liveSessionDir).
    // The running session's own folder comes first: it exists before its first swing does.
    // KEEP IT EQUAL TO PpShotCarousel._diagFolder (refactor 3.7-c): the cards find this panel's
    // adapter by that folder, until step 4's sessionOnScreen replaces both.
    readonly property string sessionDir:
        (sessionReviewController.reviewActive
         && sessionReviewController.activeSessionId !== "")
            ? sessionReviewController.activeSessionId
            : shotProcessor.activeSessionDir !== ""
                ? shotProcessor.activeSessionDir
                : sessionReviewController.liveSessionDir

    // ON EVERY POINTING, NOT ONLY A NEW FOLDER: the adapter outlives this panel (PpModeStage rebuilds
    // the panel on every mode switch), and each pointing is when a club baseline frozen elsewhere
    // since (the catch-up's, at End) is read — as each rebuilt panel's private model used to read it.
    // Cheap: the adapter reads no swing and re-judges nothing unless that baseline moved.
    function _pointAtSession() {
        if (!diagModel) return
        // Today's engine model (a folder no Session holds): a disk scan and a back-fill, so only for a new folder, as
        // before 3.7-a — compared as folders, not strings (SessionMode.sameFolder).
        if (!diagModel.adapter && SessionMode.sameFolder(sessionDir, diagModel.sessionDir)) { _adoptFocus(); return }
        // …and only for a folder no Session holds: the two change handlers above may run before `diagModel` has moved
        // to a held folder's adapter, and an engine model must never judge (and write) a session its owner holds.
        if (!diagModel.adapter && typeof app !== "undefined" && app && sessionDir !== "" && app.sessionAt(sessionDir)) return
        // WHO MAY FREEZE THE BASELINE, set by hand before the pointing reads it (a binding's order isn't
        // pinned): a reviewed session never freezes one, a live panel may (today's rule).
        diagModel.baselineAuthority = (typeof sessionReviewController !== "undefined" && sessionReviewController
                                       && sessionReviewController.reviewActive) ? "never" : "live"
        diagModel.activateSession(sessionDir)
        _adoptFocus()
    }

    // ── the session's focus: SessionController's, adopted ────────────────────
    //
    // IN A LIVE SESSION THE FOCUS BELONGS TO SessionController, NOT TO THIS MODEL. It is set at
    // Start, before any panel need exist — the default layouts carry none — and this model does not
    // outlive its panel: PpModeStage rebuilds it on every Capture↔Replay flip, the cast window has
    // a second one, and activateSession() wipes the focus and re-reads whatever the file says. So
    // each model ADOPTS the controller's focus once it holds the live folder, and again whenever
    // the controller's changes; a declaration on any surface is written back to the controller by
    // the body (focusController below), which is how it reaches the others. Without it, a model that
    // came up with an empty intent wrote that over the focus at its next shot.
    //
    // LIVE = RUNNING, NOT REVIEWING, AND POINTED AT THE RUNNING SESSION'S OWN FOLDER — not the
    // carousel's fallback folder once a session has ended. `running` is what keeps the adoption out
    // of both ends: Main sets the focus BEFORE the new folder exists (a panel still on the last
    // session must not take it), and endSession() stops the clock BEFORE it clears the focus (the
    // finished session's focus must not be cleared on its way out).
    readonly property bool liveSession:
        typeof sessionController !== "undefined" && sessionController !== null
        && typeof shotProcessor !== "undefined" && shotProcessor !== null
        && sessionController.running
        && !!diagModel && !diagModel.reviewing
        && shotProcessor.activeSessionDir !== ""
        && SessionMode.sameFolder(sessionDir, shotProcessor.activeSessionDir)
    onLiveSessionChanged: _adoptFocus()

    // ONLY ON A REAL DIFFERENCE, by id: re-adopting the focus already held would re-split it at
    // today's row count. And ONLY ONCE THE MODEL HOLDS THE LIVE FOLDER — read here, never bound:
    // the model announces its new folder before it has read that folder's intent, and an
    // adoption then would land on rows not yet loaded and be overwritten by the file.
    function _adoptFocus() {
        if (!liveSession || !diagModel) return
        if (!SessionMode.sameFolder(diagModel.sessionDir, shotProcessor.activeSessionDir)) return
        const want = sessionController.focusConditionId || ""
        if (want === (diagModel.focusConditionId || "")) return
        if (want === "") diagModel.clearFocus()
        else diagModel.declareFocusFrom(want, sessionController.focusFromShot)
    }
    // DEFERRED: the body writes the controller's two properties one after the other, and each emits
    // focusChanged. Adopting on the first would act on a half-written pair — revert this model's own
    // declaration, or hand the cast the old split — so the adoption runs once both have landed.
    Connections {
        target: (typeof sessionController !== "undefined") ? sessionController : null
        function onFocusChanged() { Qt.callLater(root._adoptFocus) }
    }

    // ORDER IS DELIBERATELY NOT PINNED between this and the `reviewing` binding above, which
    // reviewActiveChanged also dirties: both republish the WHOLE surface, so whichever lands
    // second publishes a state that has both facts in it, and the pair converges within the
    // same event-loop turn either way. Pinning it would mean breaking the declarative binding
    // on `reviewing` to set it by hand, which buys a transient nobody can observe.
    // ON THE FOLDER AS WELL AS ON THE MODEL (review B1): a folder no Session holds is shown by one engine-mode model per
    // surface (session_diagnostics_adapter.h), so a move between two of them keeps the same object and only the folder
    // says so. _pointAtSession is guarded on sameFolder for that model and idempotent for an adapter.
    onSessionDirChanged: { _pointAtSession(); _refreshReadout() }
    // The view bound to the adapter (review M2): the adapter does view work only while a view holds it.
    property var _viewOf: null
    function _bindView() {
        if (_viewOf === diagModel) return
        if (_viewOf) _viewOf.dropView()
        _viewOf = diagModel
        if (_viewOf) _viewOf.addView()
    }
    onDiagModelChanged: {
        _bindView()
        _pointAtSession()
        _refreshReadout()
        // The seam follows the panel onto its new adapter (it was ours: the last panel up claims it, below).
        if (SessionMode.sessionDiagnosticsClaimant === root) SessionMode.sessionDiagnostics = diagModel
    }

    Component.onCompleted: {
        _bindView()
        _pointAtSession()
        // ⚠ FETCHED HERE AS WELL AS FROM THE SIGNALS, and it has to be. The panel is built when
        // its stage tab first shows — typically on entering Analyse with a shot already picked —
        // and _pointAtSession() loads the ledger and resolves that pick SYNCHRONOUSLY, inside
        // this handler, before the Connections below are connected. The selectedShotIdChanged
        // it emits is lost, nothing else changes afterwards, and THIS SHOT sat on "pick a shot"
        // with shot 8 selected and readable. Reproduced and verified end to end with
        // --probe-qml (load a session, pick shot 8, enter Analyse).
        _refreshReadout()
        // THE SEAM, CLAIMED UNCONDITIONALLY BY THE MOST RECENT PANEL (Charts, Consistency and the
        // charts' "open in Session diagnostics" read it until 3.7-e). The carousel's cards no longer
        // do (refactor 3.7-c): they find the adapter this panel shows through
        // diagnosticsAdapters.shownFor, which _bindView's addView tells them about.
        SessionMode.sessionDiagnostics = diagModel
        SessionMode.sessionDiagnosticsClaimant = root
        if (remembersNav) {
            _restoreNav()
            place.watch()
        }
        place.ready = remembersNav
    }
    // ...and released only if it is still OURS. A panel being torn down while a second one
    // holds the seam must not null a pointer it no longer owns — which is exactly what
    // happens when the user switches screens and the outgoing panel is destroyed after the
    // incoming one has already claimed it. BY PANEL, NOT BY OBJECT (3.7-a): the incoming panel
    // claims the SAME adapter, so the object alone cannot say whose claim stands.
    Component.onDestruction: {
        if (_viewOf) _viewOf.dropView()
        if (SessionMode.sessionDiagnosticsClaimant === root) {
            SessionMode.sessionDiagnostics = null
            SessionMode.sessionDiagnosticsClaimant = null
        }
    }

    // ── keeping the reader's place across a rebuild ──────────────────────────
    //
    // THE PANEL DOES NOT SURVIVE A MODE SWITCH. PpModeStage rebuilds its panels on every
    // Capture↔Replay flip — twice per auto-replayed shot — and a new panel is a new model and
    // a new body at their defaults, so the characteristic the reader had open, the tab and the
    // filter were all lost on every shot. So the place is saved on SessionMode as it changes
    // and put back on build.
    //
    // SAVED AS IT CHANGES, NOT ON TEARDOWN: the stage builds the incoming panel BEFORE it
    // destroys the outgoing one, so a save in onDestruction lands after the restore it was for
    // (verified with diag_nav_survives_mode_switch.qml). Saving starts only once the restore is
    // done (place.ready), so the fresh model's activateSession() clearing the detail on build
    // cannot overwrite the place it is about to be given back.
    //
    // A DIFFERENT SESSION STARTS AT THE DEFAULTS — the same rule activateSession() applies to
    // the open detail — so the saved place is stamped with the folder it was read in.
    //
    // THE MACHINERY IS PpReaderPlace, shared with the Charts panel: the stamp, each list's scroll and
    // its restore (a list's content arrives over the next few frames, and a list hidden behind an open
    // detail is not laid out at all, so it waits, uncounted, until the detail closes), the flick
    // watching and the cache it reads them through. The scroll is kept PER REGISTER — the Replay glance's
    // sdCardsFlick and the Analyse one are different lists — and only the reader's own scroll moves it,
    // never a clamp to a shorter list, a restore or a rebuild (#129). What the place IS here — tab, filter, Watching
    // fold, open characteristic and its measure — is this file's, below.
    PpReaderPlace {
        id: place
        content: body.registerItem
        scope: body.placeScope
        flickNames: ["sdShotCardsFlick", "sdCardsFlick", "sdConsistencyFlick", "sdWatchingFlick", "sdDetailFlick"]
        // the one detail list shows one condition at a time: each condition keeps its own place
        flickKeys: ({ sdDetailFlick: diagModel.detailConditionId || "" })
        active: root.remembersNav
        model: diagModel
        onSaveRequested: root._saveNav()
    }
    function _saveNav() {
        const nav = place.capture({
            tab:               body.tab,
            cardFilter:        body.cardFilter,
            watchingExpanded:  body.watchingExpanded,
            consistencyCell:   body.consistencyCell,
            detailConditionId: diagModel.detailConditionId || "",
            detailMeasureId:   (diagModel.detail && diagModel.detail.measurePicked) || ""
        })
        if (nav) SessionMode.diagnosticsNav = nav
    }
    function _restoreNav() {
        const nav = SessionMode.diagnosticsNav
        if (!place.matches(nav)) return
        body.tab              = nav.tab
        body.cardFilter       = nav.cardFilter
        body.watchingExpanded = nav.watchingExpanded
        body.consistencyCell  = nav.consistencyCell || ""
        if (nav.detailConditionId !== "") {
            diagModel.openDetail(nav.detailConditionId)   // a no-op for an id the pack lacks
            if (nav.detailMeasureId)
                diagModel.setDetailMeasure(nav.detailMeasureId)   // declined if not its measure
        }
        place.restore(nav)
    }

    // The saves, on change. A scroll is saved by PpReaderPlace a moment after the READER's scroll settles;
    // its own writes (a restore, a list following its place as its content changes) and a clamp to a shorter
    // list never count. The body swapping its REGISTER (Replay ⇄ Analyse, or Analyse over the glance) swaps
    // the named Flickables: PpReaderPlace follows `content` and `scope` itself, and gives a new list its place.
    Connections {
        target: body
        function onTabChanged()              { root._saveNav(); place.resume() }
        function onCardFilterChanged()       { root._saveNav() }
        function onWatchingExpandedChanged() { root._saveNav(); place.resume() }
        function onConsistencyCellChanged()  { root._saveNav() }
        function onDetailOpenChanged()       { place.resume() }
    }
    Connections {
        target: diagModel
        function onDetailChanged() { root._saveNav() }
    }

    // ── a session whose diagnostics cannot be saved ──────────────────────────
    //
    // SHOW WITH A WARNING, NEVER HIDE. When the session's diagnostics.json is there but cannot be
    // opened (or cannot be read and cannot be moved aside), the model grades in memory and writes
    // nothing for the rest of the activation — while this panel goes on taking corrections, intents,
    // a declared focus or miss and screen answers. So the golfer is told, through the app's one
    // notification centre: a condition naming the session, latched until it is dismissed or the
    // session is next opened with a file that opens.
    //
    // ONE ID PER SESSION FOLDER, so the wall cast's panel (its own model, the same file) restates the
    // notice rather than adding a second one, and postWarning() does not count a restatement.
    //
    // ⚠ REFACTOR-TEMPORARY WIRING: the model is created here, so it cannot reach the C++ notification
    // centre itself. When the Session owns its diagnostics (target architecture, step 3) it posts
    // directly and this block goes. Guarded: a test engine may not provide `notifications`.
    // THE ID IS THE FOLDER, THE WORDS ARE THE SESSION'S NAME: golfers never see folder names. The name is
    // the one the app already shows — the reviewing strip's and carousel chip's "Tue 18 Aug · 21:38"
    // (SessionReviewController's activeDayLabel/activeTimeLabel), or the session list's "Today" for the
    // live one. A folder that is neither (not expected for a panel) is "this session".
    function _writesBlockedId(dir) { return "diagnostics.writes-blocked:" + SessionMode.folderKey(dir) }
    function _sessionName(dir) {
        const src = (typeof sessionReviewController !== "undefined") ? sessionReviewController : null
        if (src && src.reviewActive && SessionMode.sameFolder(dir, src.activeSessionId) && src.activeDayLabel)
            return qsTr("the %1 session").arg(src.activeDayLabel
                                              + (src.activeTimeLabel ? " · " + src.activeTimeLabel : ""))
        const live = (typeof shotProcessor !== "undefined" && shotProcessor) ? shotProcessor.activeSessionDir : ""
        if ((live && SessionMode.sameFolder(dir, live)) || (src && SessionMode.sameFolder(dir, src.liveSessionDir)))
            return qsTr("today's session")
        return qsTr("this session")
    }
    Connections {
        target: diagModel
        function onLedgerWritesBlocked(dir, reason) {
            if (typeof notifications === "undefined" || !notifications) return
            notifications.postWarning(root._writesBlockedId(dir),
                                      qsTr("Changes to the diagnostics of %1 are not being saved")
                                          .arg(root._sessionName(dir)),
                                      reason)
        }
        function onLedgerWritesResumed(dir) {
            if (typeof notifications === "undefined" || !notifications) return
            notifications.resolve(root._writesBlockedId(dir))
        }
    }

    // NO SHOT OF A SESSION IS FED FROM HERE (3.7-a): its owner hears each shot's document land (EVT-01),
    // its late reading (EVT-22), a re-analysis (EVT-02) and a trash (EVT-03) itself, with or without
    // this panel, and an adapter ignores both calls below. They reach only today's engine model, for a
    // folder no Session holds, exactly as before.
    Connections {
        target: shotProcessor
        function onShotProcessed(shotId, swingDir) { if (root.diagModel) root.diagModel.ingestShot(shotId, swingDir) }
    }
    Connections {
        target: SessionMode
        function onSwingTrashed(swingDir) { if (root.diagModel) root.diagModel.forgetShot(swingDir) }
    }

    // shotReadout() IS AN INVOKABLE, SO IT CANNOT BE BOUND — somebody has to call it, and
    // this is the only file with the model's signals in front of it. Re-fetched on all three
    // things that can change the answer: which shot is selected, whether the panel is in
    // review (the ledger it reads against), and any republication of the surface (a late back-fill landing changes
    // the tiers the readout's cells carry).
    function _refreshReadout() {
        // Live as well as in review: the body decides what a live pick of the newest swing
        // shows (the after-shot strip), and needs the readout for every other pick.
        body.readout = diagModel && diagModel.selectedShotId >= 0
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

    // ── a shot picked on a value run ─────────────────────────────────────────
    //
    // HANDED TO THE CAROUSEL'S OWN CLICK PATH, not set on the model. The carousel owns the
    // selection (brief §8) and the panel only reads it back through selectedSwingDir above, so a
    // dot tapped on a card does exactly what tapping that swing's card in the carousel does: the
    // swing is promoted onto the stage, the carousel lights it, and THIS SHOT, the ringed dot and
    // the wide mark all follow from that one change. A second, panel-local selection would be a
    // second answer to "which shot", and the two would disagree the moment either moved.
    //
    // The carousel numbers its shots with its own counter, so the request (by swing folder, the
    // identity both sides share) is resolved against the shots the app is SHOWING — the loaded
    // session's in review, the live carousel's otherwise (PpReaderPlace.pickShot, as the Charts
    // panel's).

    // ── a session with no golfer (Mark's N1) ─────────────────────────────────
    //
    // An existing PpNotice (warning style) across the top of the panel, the body below it in its empty
    // (cold) state: nothing here is judged, and the golfer is told why and what to do.
    PpNotice {
        id: noGolferNotice
        objectName: "sdNoGolferNotice"
        visible: root.noGolfer
        anchors { left: parent.left; right: parent.right; top: parent.top }
        tone: "warn"
        style: "warning"
        title: Theme.caps(qsTr("No diagnostics"))
        text: root.noGolferText
    }

    // ── a session whose diagnostics are still being read (3.7-c review B3) ───
    //
    // Since 3.7-a a reviewed session's panel shows its Session's owner, which reads the session's
    // diagnostics (and re-judges any shot judged under older rules) off the screen thread. Until its
    // first diagnosis lands the adapter holds nothing, and the body alone would say "No shots yet"
    // over a session that has them. SHOW WITH A WARNING, NEVER HIDE: the same existing PpNotice,
    // in its para style and the accent, says it is being read; the body stays, below, until the diagnosis replaces it.
    readonly property bool loadingDiagnostics: !noGolfer && !!diagModel && !!diagModel.loading
    PpNotice {
        id: loadingNotice
        objectName: "sdLoadingNotice"
        visible: root.loadingDiagnostics
        anchors { left: parent.left; right: parent.right; top: parent.top }
        tone: "accent"
        style: "para"
        text: qsTr("Reading this session's diagnostics. Its swings appear here in a moment.")
    }

    PpSessionDiagnosticsBody {
        id: body
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  top: root.noGolfer ? noGolferNotice.bottom : root.loadingDiagnostics ? loadingNotice.bottom : parent.top
                  topMargin: (root.noGolfer || root.loadingDiagnostics) ? Theme.gap(8) : 0 }
        source: diagModel
        // Where a declaration made on this panel is written back, while it is live (above).
        focusController: root.liveSession ? sessionController : null
        onScreenRequested: (ref, cond) => root.screenRequested(ref, cond)
        onShotRequested: (dir, id) => place.pickShot(dir)
    }
}
