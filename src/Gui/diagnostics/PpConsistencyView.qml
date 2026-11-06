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
import PinPointStudio

// THE CONSISTENCY TAB (docs/design/consistency_design.md §3): how the swing repeats, area by span, and what stood out
// on the picked shot. Every word and figure is ChartsConsistencyPresenter's; this file only places them.
//
// IT OWNS ITS DATA. A SessionSeriesModel, the presenter, a PpChRoles and a PpBrush, kept current as Charts' panel keeps
// its own (ingest, re-analysis, trash, the ledger written). A register builds this inside a PpViewFlick, which makes a
// view on first show and keeps it, so nobody pays for this who never opens the tab.
//   glance false (Analyse)  the head and ball line; the table; the opened cell's readings; the picked shot in full.
//   glance true  (Replay)   the head, the table and the picked shot's one sentence (doc §3). A cell pressed here opens
//                           in the Analyse register with the stage still in Replay, as an open detail does. The open
//                           cell is the body's (consistencyOpenCell).
// With no shot picked it is the table alone (the presenter's "pick a shot" note is left unsaid).
// UNTIL THIS SESSION HAS ARRIVED it says nothing about it: a new view, or a new session, holds an empty snapshot for the
// second or two the read takes, and "no shot has been analysed" would be false. It keeps a table it already has and
// otherwise shows the one quiet line (`settling`).
//
// A ROW OPENS IN PLACE (PpChProfile: body.openId and relations.openRow, as a Readings row does), because Session
// diagnostics has no way to open a reading in Charts. The curve comes from a ChartsRelationsPresenter, the one thing
// here that builds a TRACE; it is made the first time a row is opened, and has no pickedDetail because a row only ever
// names a shot the model already holds.
//
// objectNames (unique per register, so a held glance cannot answer for the Analyse view over it):
//   sdConsistencyView, sdConsistencyHead, sdConsistencyMatrix, sdConsistencyCell, sdConsistencyShot
//   sdConsistencyGlanceView, sdConsistencyGlanceHead, sdConsistencyGlanceMatrix, sdConsistencyGlanceShot
Column {
    id: root
    property var body: null                      // the PpSessionDiagnosticsBody: the open cell, and picking a shot
    property bool glance: false
    property bool interactive: true
    property string medium: "screen"

    readonly property string _name: glance ? "sdConsistencyGlance" : "sdConsistency"
    objectName: _name + "View"
    spacing: ro.g(10)

    // ── the presenter's outputs, read once per publish (a read is a deep copy) ──
    readonly property bool ready: presenter.ready
    readonly property var hd: presenter.head
    readonly property var matrix: presenter.matrix
    readonly property var cell: presenter.cell
    readonly property var picked: presenter.shot
    readonly property var empty: presenter.empty
    readonly property var status: presenter.status
    // what the table cannot say, unless the headline has already said it
    readonly property var emptyLine: ro.has(empty.sentence) && !(hd.headline && hd.headline.text === empty.sentence.text) ? empty.sentence : null
    // no table yet, and the model or the presenter is still on its way to one; `ready` alone is not enough, because the
    // presenter publishes for the model's empty snapshot too. There is no gap between the two busies: the model's
    // snapshotChanged makes the presenter busy before the model's own busy goes false.
    readonly property bool settling: (!ready || presenter.busy || seriesModel.busy) && !(status.shotCount > 0)
    readonly property var settlingLine: status.settling || null

    // ── the session ──
    readonly property string sessionDir:
        (sessionReviewController.reviewActive && sessionReviewController.activeSessionId !== "")
            ? sessionReviewController.activeSessionId
            : shotProcessor.activeSessionDir !== "" ? shotProcessor.activeSessionDir : sessionReviewController.liveSessionDir
    function _pointAtSession() { if (!SessionMode.sameFolder(sessionDir, seriesModel.sessionDir)) seriesModel.activateSession(sessionDir) }
    // A session change is one real session to a different real one. The last real folder is kept apart from the model,
    // whose sessionDir is empty in a new view, and a view's first binding writes nothing: a view built by a press on the
    // glance must not clear the cell that press has just opened.
    property string _lastSessionDir: ""
    onSessionDirChanged: {
        const was = _lastSessionDir
        if (sessionDir !== "") _lastSessionDir = sessionDir
        if (was !== "" && sessionDir !== "" && !SessionMode.sameFolder(was, sessionDir)) {
            _closeRow()
            if (root.body) { root.body.consistencyCell = ""; root.body.consistencyGlanceCell = "" }
        }
        _pointAtSession()
    }
    Component.onCompleted: { _lastSessionDir = sessionDir; _pointAtSession(); _syncPick() }

    SessionSeriesModel { id: seriesModel }

    Connections {
        target: shotProcessor
        function onShotProcessed(shotId, swingDir) { seriesModel.ingestShot(shotId, swingDir) }
    }
    Connections {
        target: reanalysisController
        function onReanalysed(swingDir) { seriesModel.regradeShot(swingDir) }
    }
    // a swing moved to the trash leaves the session (#56); the model ignores one of another session
    Connections {
        target: SessionMode
        function onSwingTrashed(swingDir) { seriesModel.forgetShot(swingDir) }
        function onFocusedSwingDirChanged() { root._syncPick() }
    }
    // the diagnostics ledger was written: the model re-joins its rows (it posts the read itself)
    Connections {
        target: SessionMode.sessionDiagnostics
        ignoreUnknownSignals: true
        function onBusyChanged() { if (SessionMode.sessionDiagnostics && !SessionMode.sessionDiagnostics.busy) seriesModel.refreshDiagnostics() }
    }

    // ── the picked shot: the carousel's selection, as Session diagnostics' This shot takes it, not the replay's, because
    // a shot with no video can be picked but never replayed (doc §3). Copied only while this screen is the visible one. ──
    readonly property bool _screenActive: SessionMode.sessionScreenActive
    property string _swingDir: ""
    function _syncPick() {
        if (!_screenActive) return
        _swingDir = SessionMode.focusedSwingDir
    }
    on_ScreenActiveChanged: _syncPick()
    on_SwingDirChanged: if (_openIn === "shot") _closeRow()
    // a press on a shot goes to the carousel, by swing folder; the pick then comes back through SessionMode
    function _pickOrdinal(ordinal) {
        const s = (presenter.shots || []).find(x => x.ordinal === ordinal)
        if (s && s.swingDir && root.body && root.interactive) root.body.shotRequested(s.swingDir, s.shotId)
    }

    // ── the open cell ──
    readonly property string openCell: root.glance || !root.body ? "" : root.body.consistencyOpenCell
    onOpenCellChanged: if (_openIn === "cell") _closeRow()
    function _pressCell(key) { if (root.interactive && root.body) root.body._openConsistencyCell(key) }

    // ── a row open on its curve: one at a time, in the list it was pressed in (a reading can be in both) ──
    property string _openId: ""
    property string _openIn: ""                  // "cell" | "shot"
    property bool _rowEverOpened: false
    function _toggleRow(id, list) {
        const same = _openId === id && _openIn === list
        _openId = same ? "" : id
        _openIn = same ? "" : list
        _rowEverOpened = true
    }
    function _closeRow() { _openId = ""; _openIn = "" }
    readonly property QtObject _curves: curvesLoader.item
    readonly property var _openRow: _curves && _openId !== "" ? _curves.openRow : null
    property real _playUs: -1
    Connections {
        target: shotReplay
        enabled: root._openId !== ""
        function onPositionChanged() { root._playUs = root._screenActive && shotReplay.active ? shotReplay.positionUs : -1 }
    }
    readonly property real _playMs: {
        const s = _curves ? _curves.status : null
        return s && s.pickedImpactUs >= 0 && _playUs >= 0 ? (_playUs - s.pickedImpactUs) / 1000 : NaN
    }
    readonly property string _playText: _curves && _curves.playhead ? (_curves.playhead.openValueText || "") : ""
    Loader {
        id: curvesLoader
        visible: false
        active: root._rowEverOpened
        sourceComponent: Component {
            ChartsRelationsPresenter {
                model: seriesModel
                pickedSwingDir: root._swingDir
                register: "analyse"
                openId: root._openId
                playheadUs: root._playUs
                gradePolicy: appSettings.diagnosticsGradePolicy
            }
        }
    }

    // ── the head ──
    PpStandfirst {
        objectName: root._name + "Head"
        visible: root.ready && !root.settling
        width: root.width
        roles: ro; brush: br; interactive: false
        kicker: root.hd.kicker || ""
        head: root.hd.headline ? root.hd.headline.text : ""
        standfirst: root.glance ? "" : ro.html(root.hd.ball)
    }
    PpChText { roles: ro; font: ro.fBody; sentence: root.settling ? root.settlingLine : root.emptyLine }

    // ── the table ──
    PpChMatrix {
        objectName: root._name + "Matrix"
        visible: root.ready && root.status.shotCount > 0
        width: root.width
        roles: ro
        columns: root.matrix.columns || []
        rows: root.matrix.rows || []
        keyText: presenter.keyMarks
        interactive: root.interactive
        onCellActivated: (rowId, column) => root._pressCell(rowId + ":" + column)
    }
    // the table's words, and what the head had to leave out: under the table in Analyse, at the foot of the glance
    component TableNotes: Column {
        width: root.width
        spacing: ro.g(2)
        PpNotes { visible: root.status.shotCount > 0; width: parent.width; roles: ro; text: ro.esc(presenter.key) }
        Repeater {
            model: root.hd.notes || []
            delegate: PpChText { required property var modelData; roles: ro; sentence: modelData }
        }
    }
    TableNotes { visible: root.ready && !root.settling && !root.glance }

    // ── a cell, opened: the readings behind its word, loosest first ──
    Column {
        objectName: "sdConsistencyCell"
        visible: !root.glance && !!root.cell.title
        width: root.width
        spacing: ro.g(6)
        PpSectionHead { width: parent.width; roles: ro; paged: false; title: root.cell.title || ""; sub: root.cell.sub ? root.cell.sub.text : "" }
        PpChProfile {
            width: parent.width; roles: ro; brush: br; interactive: root.interactive
            presenter: root._curves
            rows: root.cell.rows || []
            headLabel: root.cell.headLabel || ""; shotLabel: root.cell.shotLabel || ""; countLabel: root.cell.countLabel || ""
            showPlace: true
            openId: root._openIn === "cell" ? root._openId : ""; openRow: root._openIn === "cell" ? root._openRow : null
            playMs: root._playMs; playText: root._playText
            onRowActivated: (id) => root._toggleRow(id, "cell")
            onShotActivated: (o) => root._pickOrdinal(o)
        }
        PpNotes { visible: text !== ""; width: parent.width; roles: ro; text: ro.esc(root.cell.unjudgedNote || "") }
    }

    // ── the picked shot ──
    Column {
        objectName: root._name + "Shot"
        visible: root.picked.available === true
        width: root.width
        spacing: ro.g(6)
        PpSectionHead { visible: !root.glance; width: parent.width; roles: ro; paged: false; title: root.picked.shotLabel || ""; sub: root.picked.lm || "" }
        Text { visible: root.glance; width: parent.width; wrapMode: Text.WordWrap; text: root.picked.kicker || ""; font: ro.fKicker; color: ro.ink3 }
        PpChText { roles: ro; font: ro.fBody; sentence: root.picked.sentence || null }
        PpNotes { visible: root.picked.guarded === true; width: parent.width; roles: ro; small: true; tone: 2; text: ro.esc(root.picked.guardNote || "") }
        PpChProfile {
            visible: !root.glance && (root.picked.groups || []).length > 0
            width: parent.width; roles: ro; brush: br; interactive: root.interactive
            presenter: root._curves
            groups: root.glance ? [] : (root.picked.groups || [])
            headLabel: root.picked.headLabel || ""; shotLabel: root.picked.shotLabel || ""; countLabel: root.picked.countLabel || ""
            openId: root._openIn === "shot" ? root._openId : ""; openRow: root._openIn === "shot" ? root._openRow : null
            playMs: root._playMs; playText: root._playText
            onRowActivated: (id) => root._toggleRow(id, "shot")
            onShotActivated: (o) => root._pickOrdinal(o)
        }
        PpNotes { visible: !root.glance && text !== ""; width: parent.width; roles: ro; text: ro.esc(root.picked.note || "") }
    }
    TableNotes { visible: root.ready && !root.settling && root.glance }

    ChartsConsistencyPresenter {
        id: presenter
        model: seriesModel
        pickedSwingDir: root._swingDir
        openCell: root.openCell
        medium: root.medium
        gradePolicy: appSettings.diagnosticsGradePolicy
    }

    // ── the shared objects, declared LAST so they are destroyed last ──
    PpChRoles { id: ro; medium: root.medium }
    PpBrush { id: br; pickedShot: root.status.pickedOrdinal > 0 ? root.status.pickedOrdinal : -1 }
}
