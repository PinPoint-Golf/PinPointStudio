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

// PPTRUSTPANEL — "Readings and their limits": the home of every reason a reading was refused, bounded or
// withheld (docs/design/trust_and_gates_design.md §7). A stage panel like Charts or Session diagnostics,
// turned on in View; it follows the picked shot.
//
//   head      the shot, and one sentence of what it could not measure (§7.4, the shot half)
//   readings  one PpTrustReading per reading a gate touched, grouped by area, each foldable
//   session   how often each gate fired across the session's shots, per reading (§7.5's count, per session)
//
// A click on a claim's mark anywhere (TrustFocus.open) opens that reading here and scrolls to it. Every word
// comes from TrustPresenter → trust_readings.h → the gate catalogue; nothing is decided here.
// objectNames: trustPanel, trustHead, trustSession, trustFlick.
Item {
    id: root
    objectName: "trustPanel"
    property int sessionType: -1                 // the host's session type; < 0 = no session-screen host (always active)
    property bool framed: true

    readonly property bool _screenActive: root.sessionType < 0 || navController.currentIndex === AppScreen.Session
    readonly property string sessionDir:
        (sessionReviewController.reviewActive && sessionReviewController.activeSessionId !== "")
            ? sessionReviewController.activeSessionId
            : shotProcessor.activeSessionDir !== "" ? shotProcessor.activeSessionDir : sessionReviewController.liveSessionDir
    function _folder(p) { return String(p || "").replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "") }
    function _pointAtSession() { if (_folder(sessionDir) !== _folder(seriesModel.sessionDir)) seriesModel.activateSession(sessionDir) }
    onSessionDirChanged: _pointAtSession()
    Component.onCompleted: { _pointAtSession(); _syncPick() }

    SessionSeriesModel { id: seriesModel }

    property string _swingDir: ""
    property var _detail: ({})
    function _syncPick() {
        if (!_screenActive) return
        _swingDir = shotReplay.active ? shotReplay.swingDir : ""
        _detail = shotReplay.active && shotReplay.analysisDetail ? shotReplay.analysisDetail : ({})
    }
    on_ScreenActiveChanged: _syncPick()
    Connections { target: shotReplay; function onActiveChanged() { root._syncPick() } }

    TrustPresenter {
        id: presenter
        model: seriesModel
        pickedSwingDir: root._swingDir
        pickedDetail: root._detail
    }
    readonly property var shot: presenter.shot
    readonly property var session: presenter.session

    PpTypeset { id: ro }

    // ── a click elsewhere: open that reading and bring it into view ──
    property var _openState: ({})                // subject → open (the reader's folds, kept across picks)
    property string _lit: ""
    function _isOpen(subject) { return _openState[subject] !== false }
    Connections {
        target: TrustFocus
        function onNonceChanged() { root._focus(TrustFocus.subject) }
    }
    function _focus(subject) {
        const s = Object.assign({}, _openState); s[subject] = true; _openState = s
        _lit = subject
        litTimer.restart()
        Qt.callLater(function () {
            for (let i = 0; i < readingsRep.count; ++i) {
                const it = readingsRep.itemAt(i)
                if (it && it.subject === subject) {
                    const y = it.mapToItem(flick.contentItem, 0, 0).y - ro.u(12)
                    flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
                }
            }
        })
    }
    Timer { id: litTimer; interval: 1600; onTriggered: root._lit = "" }

    Flickable {
        id: flick
        objectName: "trustFlick"
        anchors.fill: parent
        anchors.margins: ro.u(16)
        contentWidth: width
        contentHeight: col.height + ro.u(24)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: flick.width
            spacing: ro.u(10)

            // ── the shot ──
            Column {
                objectName: "trustHead"
                width: parent.width
                spacing: ro.u(4)
                Text { text: root.shot.available ? (root.shot.kicker || "") : "No shot picked"
                       font: ro.fKicker; color: ro.ink2 }
                // "How far can I trust this?" (§7.4): its few words as the standfirst, its lines under it —
                // the same statement the fold in Session diagnostics opens to
                Text { width: ro.measureW(parent.width); wrapMode: Text.WordWrap
                       text: !root.shot.available ? "Pick a shot to see which readings a gate refused, bounded or kept out of the findings, and why."
                             : root.shot.summary && root.shot.summary.tail ? ro.cap1(root.shot.summary.tail) + "."
                             : (root.shot.head || "")
                       font: ro.fStand; color: ro.ink }
                Repeater {
                    model: root.shot.available && root.shot.summary ? (root.shot.summary.lines || []) : []
                    delegate: Text {
                        required property var modelData
                        width: ro.measureW(col.width); wrapMode: Text.WordWrap
                        text: modelData.text; font: ro.fSmall; color: ro.ink2
                    }
                }
                Rectangle { width: parent.width; height: ro.r.figureRuleTop; color: ro.rule }
            }
            // the readings, under their area's crosshead
            Repeater {
                id: readingsRep
                model: root.shot.available ? (root.shot.readings || []) : []
                delegate: Column {
                    id: rd
                    required property var modelData
                    required property int index
                    readonly property string subject: modelData.subject
                    readonly property var prev: index > 0 ? root.shot.readings[index - 1] : null
                    width: col.width
                    spacing: ro.u(4)
                    Text { visible: !!rd.modelData.area && (!rd.prev || rd.prev.area !== rd.modelData.area)
                           text: rd.modelData.area; font: ro.fSection; color: ro.ink; topPadding: ro.u(4) }
                    PpTrustReading {
                        width: parent.width
                        roles: ro
                        reading: rd.modelData
                        open: root._isOpen(rd.subject)
                        highlight: root._lit === rd.subject
                        onOpenChanged: if (open !== root._isOpen(rd.subject)) {
                            const s = Object.assign({}, root._openState); s[rd.subject] = open; root._openState = s
                        }
                    }
                }
            }
            // ── the session ──
            Column {
                objectName: "trustSession"
                visible: (root.session.gates || []).length > 0 || !!root.session.head
                width: parent.width
                spacing: ro.u(4)
                topPadding: ro.u(10)
                Rectangle { width: parent.width; height: ro.hw; color: ro.rule }
                Text { text: "Across the session"; font: ro.fSection; color: ro.ink; topPadding: ro.u(6) }
                Repeater {
                    model: root.session.summary ? (root.session.summary.lines || []) : []
                    delegate: Text {
                        required property var modelData
                        width: ro.measureW(col.width); wrapMode: Text.WordWrap
                        text: modelData.text; font: ro.fSmall; color: ro.ink2
                    }
                }
                Repeater {
                    model: root.session.gates || []
                    delegate: Item {
                        id: sg
                        required property var modelData
                        width: col.width
                        height: Math.max(sgT.implicitHeight, sgSubs.implicitHeight) + ro.u(6)
                        Text { id: sgT; width: parent.width * 0.45; wrapMode: Text.WordWrap
                               text: sg.modelData.title + (sg.modelData.acted ? "" : "  (marked on the chart only)")
                               font: ro.fSmallM; color: ro.ink }
                        Text { id: sgSubs; x: parent.width * 0.47; width: parent.width * 0.53; wrapMode: Text.WordWrap
                               text: (sg.modelData.subjects || []).map(function (s) { return s.label + " " + s.shots + " of " + sg.modelData.of }).join("  ·  ")
                               font: ro.fSmall; color: ro.ink2 }
                    }
                }
            }
        }
    }
}
