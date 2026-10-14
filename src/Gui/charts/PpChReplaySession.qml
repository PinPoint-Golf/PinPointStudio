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

// PPCHREPLAYSESSION — Replay's Session follows the playhead (D6; design §3): the lead reading shot by shot at
// the latest P-position the playhead has passed (ChartsRelationsPresenter.replaySession, which notifies only
// when that P-position or its readout changes — never per frame for nothing). The P-positions it is read at
// are links that seek there; "now" against "typical" in one sentence; the run with its box; the strongest
// partner's scatter while two columns fit; and three "moves with it" lines that end on the chance line and the
// co-occurrence caveat (the presenter's sentences).
// objectName: chReplaySession.
Column {
    id: rs
    objectName: "chReplaySession"
    property QtObject roles: null
    property var brush: null
    property QtObject presenter: null            // ChartsRelationsPresenter (ticks)
    property var info: null                      // relations.replaySession
    property bool interactive: true
    property bool cast: false
    property bool atom: false
    readonly property var keyKinds: []
    signal shotActivated(int ordinal)
    readonly property bool ok: !!info && !!info.readingId
    readonly property bool side: roles ? width >= 66 * roles.em : false
    visible: ok
    spacing: roles ? roles.u(8) : 8

    Flow {
        width: rs.width; spacing: rs.roles ? rs.roles.u(10) : 10
        PpChText { roles: rs.roles; sentence: rs.ok ? rs.info.head : null; measure: false; width: Math.min(implicitWidth, rs.width)
                   font: rs.roles ? rs.roles.fBodyM : Qt.font({}); tone: rs.roles ? rs.roles.ink : "black" }
        Text { visible: rs.ok; text: rs.ok ? (rs.info.follows || "") : ""; font: rs.roles ? rs.roles.fSmall : Qt.font({}); color: rs.roles ? rs.roles.ink3 : "grey" }
    }
    // the instants it is read at: the reached one in ink, the others links that seek there
    Row {
        spacing: rs.roles ? rs.roles.u(12) : 12
        Repeater {
            model: rs.ok && rs.roles ? (rs.info.phases || []) : []
            delegate: Text {
                required property var modelData
                text: modelData.tag
                font: modelData.current ? rs.roles.fSmallM : (rs.interactive ? rs.roles.fSmallL : rs.roles.fSmall)
                color: modelData.current ? rs.roles.ink : rs.interactive ? rs.roles.link : rs.roles.ink3
                MouseArea { anchors.fill: parent; enabled: rs.interactive && !parent.modelData.current && typeof shotReplay !== "undefined" && parent.modelData.seekUs >= 0
                            cursorShape: Qt.PointingHandCursor; onClicked: shotReplay.seekToUs(parent.modelData.seekUs) }
            }
        }
    }
    PpChText { roles: rs.roles; sentence: rs.ok && rs.info.now ? rs.info.now.sentence : null; font: rs.roles ? rs.roles.fBody : Qt.font({}); tone: rs.roles ? rs.roles.ink : "black" }
    Flow {
        width: rs.width; spacing: rs.roles ? rs.roles.u(24) : 24
        PpChRunBox {
            width: rs.side ? Math.floor((rs.width - rs.roles.u(24)) * 0.55) : rs.width
            roles: rs.roles; brush: rs.brush; ticker: rs.presenter; interactive: rs.interactive
            run: rs.ok ? rs.info.run : null; showCaption: false
            plotH: rs.roles ? rs.roles.u(rs.cast ? 150 : rs.side ? 140 : 110) : 110
            onShotActivated: (o) => rs.shotActivated(o)
        }
        PpChScatter {
            visible: rs.side && rs.ok && !!rs.info.partner
            width: rs.side ? Math.floor((rs.width - rs.roles.u(24)) * 0.45) : 0
            roles: rs.roles; brush: rs.brush; ticker: rs.presenter; interactive: rs.interactive
            scatter: rs.ok && rs.info.partner ? rs.info.partner.scatter : null
            showMarginals: false
            plotH: rs.roles ? rs.roles.u(126) : 126
            onShotActivated: (o) => rs.shotActivated(o)
        }
    }
    Repeater {
        model: rs.ok && rs.roles ? (rs.info.movesWith || []) : []
        delegate: PpChText { required property var modelData; roles: rs.roles; sentence: modelData.sentence; font: rs.roles.fSmall; tone: modelData.clear ? rs.roles.ink : rs.roles.ink2 }
    }
    PpChText { roles: rs.roles; sentence: rs.ok ? rs.info.note : null; tone: rs.roles ? rs.roles.ink3 : "grey" }
}
