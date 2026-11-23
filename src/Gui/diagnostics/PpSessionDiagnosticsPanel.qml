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
    // app.session is the session on screen (app.state.sessionOnScreen: the Session of sessionDir, through the athlete
    // that holds it whoever is current — J-67 — the Unassigned owner's for a session captured with no golfer: §5.11,
    // step 5.1); null when no athlete of the library holds it, no folder yet, or a folder that is gone (an empty
    // session's discard: no Session is ever made for it). The adapter for that Session and this surface is never null:
    // with no Session it is an EMPTY one, which judges nothing, and the panel says why (noGolferNotice).
    readonly property var session:
        (typeof app !== "undefined" && app && sessionDir !== "") ? app.session : null
    readonly property var diagModel:
        ((typeof app !== "undefined" && !!app.diagnosticsAdapters) && app.diagnosticsAdapters) ? app.diagnosticsAdapters.of(session, surface, sessionDir) : null
    // SHOW WITH A WARNING, NEVER HIDE: a folder the panel is pointed at that no Session holds has no diagnostics; the
    // panel says so instead of drawing an empty panel as if nothing had happened.
    readonly property bool noGolfer: sessionDir !== "" && session === null && !!diagModel && diagModel.adapter
    readonly property string noGolferText:
        qsTr("No golfer's session holds these swings, so they are not judged here.")

    // NEVER RESTORED (RestoreNone): the adapter outlives this panel, and the stage builds the incoming
    // panel before it destroys the outgoing one, so a restore would undo the incoming panel's values.
    // Both passed in rather than reached for. gradePolicy must be the SAME policy the other metric
    // surfaces and the launch monitor board grade against, or one reading would sit outside its
    // corridor on one panel and inside it on another.
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "cadence";     value: appSettings.sessionDiagnosticsCadence }
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "gradePolicy"; value: appSettings.diagnosticsGradePolicy }
    // Review FREEZES the ledger at Closing: a finished session's panel is its summary and must not
    // re-open because it was looked at.
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "reviewing";   value: app.sessionReviewController.reviewActive }
    // THE CAROUSEL OWNS THE SELECTION AND THE PANEL ONLY READS IT (brief §6.1, §8). The carousel
    // is already at the foot of the stage and is post-session the ONLY way in; a second selection
    // control on the panel would be a second answer to "which shot", and the two would disagree
    // the moment either was used.
    //
    // BY ShotRef, NOT BY ID (R-APP-4). The carousel numbers its rows with its own counter and the ledger numbers
    // shots by swing number, so the selection's ShotRef is the identity both sides share; the adapter resolves it
    // to its own id only within its own session (SessionDiagnosticsModel::selectedShot; E-05). The folder goes too,
    // for a session no id can be named for. The cast reads app.state.castSelection (today the same shot: Q-07).
    readonly property var _selection: surface === "cast" ? app.state.castSelection : app.state.selection
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "selectedShot";     value: root._selection.shotRef }
    Binding { restoreMode: Binding.RestoreNone; target: root.diagModel; property: "selectedSwingDir"; value: root._selection.swingDir }

    // THE SESSION THE PANEL IS POINTED AT — the loaded one while reviewing, the live one
    // otherwise. The folder names the Session (app.sessionAt, above); the Session's owner has
    // already read it — opening a finished session, resuming after a crash, and turning this panel
    // on half way through a session are all "the owner holds it" now (it judged every shot as it
    // landed). activateSession() on the adapter only brings what the panel shows up to date
    // (pointAdapter: the club baseline, the athlete's history).
    //
    // It is the session on screen (app.state.sessionOnScreenFolder: R-APP-3; the one derivation, E-08): the reviewed
    // session, else the running one (its folder exists before its first swing does), else the folder the carousel
    // shows — live is today's session whether or not it is running, so an ended session's panel still reads its swings.
    readonly property string sessionDir: app.state.sessionOnScreenFolder

    // ON EVERY POINTING, NOT ONLY A NEW FOLDER: the adapter outlives this panel (PpModeStage rebuilds
    // the panel on every mode switch), and each pointing is when a club baseline frozen elsewhere
    // since (the catch-up's, at End) is read — as each rebuilt panel's private model used to read it.
    // Cheap: the adapter reads no swing and re-judges nothing unless that baseline moved.
    function _pointAtSession() {
        if (!diagModel) return
        diagModel.activateSession(sessionDir)
    }

    // ── the session's focus: the Session's ─────────────────────────────────
    // THE SESSION OWNS ITS DECLARED FOCUS (OBJ-SESSION-INTENT; refactor 4-d). The wizard's pick is declared on the first
    // Session its Start opens (app.state's hand-over), a tap here goes to the Session through the model (EVT-10), and this
    // panel — every rebuilt stage panel, the cast's — reads it back from the owner through the adapter. Nothing here
    // adopts or writes back a copy (FOCUS-TRACE.md L2: an adopted empty copy cleared a saved focus).

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
        // NO SEAM (refactor 3.7-e: SessionMode.sessionDiagnostics is gone). The carousel's cards, Charts, Consistency and
        // the charts' "open in Session diagnostics" find the adapter this panel shows through
        // diagnosticsAdapters.shownFor, which _bindView's addView tells them about.
        if (remembersNav) {
            _restoreNav()
            place.watch()
        }
        place.ready = remembersNav
    }
    // The view this panel bound is let go with it (the adapter's use of its Session with it: 3.7-e).
    Component.onDestruction: {
        if (_viewOf) _viewOf.dropView()
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
    // WHO POSTS IT (refactor 3.10; the model is made by C++ since 3.7-e): a session a Session holds has
    // its owner's failed writes posted by the composition root (main.cpp's diagAttached, DEC-126), keyed
    // by the session's id. This block serves the golfer's edits the adapter forwards: one the Session or the
    // Shot could not save, or that reached no Session (refactor 5.4b). Guarded: a test may not provide
    // `notifications`.
    // THE ID IS THE FOLDER, THE WORDS ARE THE SESSION'S NAME: golfers never see folder names. The name is
    // the one the app already shows — the reviewing strip's and carousel chip's "Tue 18 Aug · 21:38"
    // (SessionReviewController's activeDayLabel/activeTimeLabel), or the session list's "Today" for the
    // live one. A folder that is neither (not expected for a panel) is "this session".
    function _writesBlockedId(dir) { return "diagnostics.writes-blocked:" + SessionMode.folderKey(dir) }
    function _sessionName(dir) {
        const src = ((typeof app !== "undefined" && !!app.sessionReviewController)) ? app.sessionReviewController : null
        if (src && src.reviewActive && SessionMode.sameFolder(dir, src.activeSessionId) && src.activeDayLabel)
            return qsTr("the %1 session").arg(src.activeDayLabel
                                              + (src.activeTimeLabel ? " · " + src.activeTimeLabel : ""))
        const live = ((typeof app !== "undefined" && !!app.shotProcessor) && app.shotProcessor) ? app.shotProcessor.activeSessionDir : ""
        if ((live && SessionMode.sameFolder(dir, live)) || (src && SessionMode.sameFolder(dir, src.liveSessionDir)))
            return qsTr("today's session")
        return qsTr("this session")
    }
    Connections {
        target: diagModel
        function onLedgerWritesBlocked(dir, reason) {
            if ((typeof app === "undefined" || !app.notifications) || !app.notifications) return
            app.notifications.postWarning(root._writesBlockedId(dir),
                                      qsTr("Changes to the diagnostics of %1 are not being saved")
                                          .arg(root._sessionName(dir)),
                                      reason)
        }
        function onLedgerWritesResumed(dir) {
            if ((typeof app === "undefined" || !app.notifications) || !app.notifications) return
            app.notifications.resolve(root._writesBlockedId(dir))
        }
    }

    // NO SHOT OF A SESSION IS FED FROM HERE (3.7-a): its owner hears each shot's document land (EVT-01), its late reading
    // (EVT-22), a re-analysis (EVT-02) and a trash (EVT-03) itself, with or without this panel. (Refactor 5.4b deleted the
    // shotProcessed and swingTrashed connections that fed the old engine's model.)

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
    // ── a session whose diagnostics are still being read (3.7-c review B3) ───
    //
    // Since 3.7-a a reviewed session's panel shows its Session's owner, which reads the session's
    // diagnostics (and re-judges any shot judged under older rules) off the screen thread. Until its
    // first diagnosis lands the adapter holds nothing, and the body alone would say "No shots yet"
    // over a session that has them. SHOW WITH A WARNING, NEVER HIDE: the same existing PpNotice,
    // in its para style and the accent, says it is being read; the empty body is not drawn until the diagnosis lands (3.7-e).
    readonly property bool loadingDiagnostics: !noGolfer && !!diagModel && !!diagModel.loading
    // ── the notices across the top: the no-golfer one, the "being read" one, and the club baseline's (3.7-e round 2) ──
    // One column, so whichever show stack from the top and the body sits below them (the same place as before when one
    // shows).
    Column {
        id: notices
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: Theme.gap(8)

        PpNotice {
            id: noGolferNotice
            objectName: "sdNoGolferNotice"
            visible: root.noGolfer
            width: parent.width
            tone: "warn"
            style: "warning"
            title: Theme.caps(qsTr("No diagnostics"))
            text: root.noGolferText
        }

        // a session whose diagnostics are still being read (3.7-c review B3; the body below is not drawn meanwhile)
        PpNotice {
            id: loadingNotice
            objectName: "sdLoadingNotice"
            visible: root.loadingDiagnostics
            width: parent.width
            tone: "accent"
            style: "para"
            text: qsTr("Reading this session's diagnostics. Its swings appear here in a moment.")
        }

        // ── the club baseline not ready (refactor 3.7-e round 2; J-49) ──────
        //
        // The session's usual numbers (its club baseline, frozen from earlier sessions' summaries) cannot be worked out
        // because some earlier sessions have not been summed up by this version — still in the catch-up's queue, or one that
        // cannot be (unreadable, its write blocked): the words say what is true in both cases (review R2-m1). Every shot is
        // judged; what is judged against the golfer's usual waits. SHOW WITH A WARNING, NEVER HIDE: the existing PpNotice, para style, warn tone, words.
        PpNotice {
            id: baselineNotice
            objectName: "sdBaselineNotice"
            visible: root.baselineWaiting
            width: parent.width
            tone: "warn"
            style: "para"
            text: qsTr("Your usual numbers for this session aren't ready: some earlier sessions haven't been summed up by this version. Every swing is judged; comparisons with your usual numbers are left out until those sessions are summed up.")
        }
    }
    readonly property bool baselineWaiting: !noGolfer && !!diagModel && !!diagModel.baselineWaiting

    PpSessionDiagnosticsBody {
        id: body
        // WHILE THE SESSION IS READ, ITS EMPTY BODY IS NOT DRAWN (refactor 3.7-e; CARRY 3.7-c B3, review R2-m4): before the
        // owner's first diagnosis the adapter holds nothing, and the cold body would say "No shots yet · No patterns ·
        // counted over 0 shots" under the notice over a session that has swings. The notice above says what is happening;
        // the body comes back with the diagnosis (nothing is withheld: there is nothing in it yet).
        visible: !root.loadingDiagnostics
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  top: notices.bottom
                  topMargin: (root.noGolfer || root.loadingDiagnostics || root.baselineWaiting) ? Theme.gap(8) : 0 }
        source: diagModel
        onScreenRequested: (ref, cond) => root.screenRequested(ref, cond)
        onShotRequested: (dir, id) => place.pickShot(dir)
    }
}
