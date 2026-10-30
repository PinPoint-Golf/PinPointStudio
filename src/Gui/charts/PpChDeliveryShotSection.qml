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

// PPCHDELIVERYSHOTSECTION — THE CLUB DELIVERY SECTION OF THIS SHOT (docs/design/club_delivery_chart_design.md §4.2–4.6,
// 4.8, 4.9; built the way PpChPostureShotSection is). "What did my plane do, and what did it do to the ball?"
// PpChartsShotView draws it under its traces only when this shot carries the swing plane curve or a monitor delivery
// reading AND one of those curves is on show (body.deliveryShotShown) — in practice the Club delivery preset; a section
// of the page, a fold after Summary that remembers its state ("delivery"), its head as the state when closed. The
// plot's own lines (the address line, the shoulder plane, the slot, the hands' stretches) are the trace figure's
// (PpChTraceFigure.planeLayers), above it.
//   Analyse: the head (§4.3) and the standfirst at the playhead's position; the plane story (§5.1) with the published
//     shape against it, the plane at each position (a press seeks there; the playhead's position in ink) and the slot;
//     then, side by side when the width allows, "Ball flight" (PpChDeliveryFlight: the fit, this shot's place and the
//     scatter over the golfer's day) | "At impact" (the presenter's lines, the monitor's first) and "Shot shape"
//     (PpChShotShapeGrid: this shot ringed, the session's shots behind it); "How far can I trust this?" under them,
//     closed (said once, §4.9).
//   The picture strip (PpChDeliveryPicture, stage 6): the down-the-line frames at address, the top and P6 with the
//     address line, the shoulder plane and the slot frozen at address, after the plane story; a press on a frame seeks there.
//   No down-the-line camera (thisShot.mode "noDtl", §4.3 rule 4): no plane story; the ball flight, the impact lines, the
//     grid and the trust fold, which says why the plane is absent — every reading still shown.
//   Replay (the glance at the mat, and the cast; §4.8): the head, the plane story, the shot-shape mark when the
//     monitor has one and the picture strip (as Posture's glance carries its strip; the frames do not seek) — under the glance cards, never instead of them (§8.3). No scatter, no controls, no trust fold.
// Every word and number is ChartsDeliveryPresenter's (body.d*, each bound ONCE on the body).
// objectNames: chDeliverySection, chDeliveryHead, chDeliveryStory (chDeliveryPos + tag), chDeliverySlot,
// chDeliveryFlight, chDeliveryImpact (chDeliveryImpactLine + key), chDeliveryShape, chDeliveryPicture, chDeliveryTrust
// (+ the blocks').
Item {
    id: ds
    objectName: "chDeliverySection"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    readonly property bool replay: reg === "Replay"
    readonly property var ty: roles
    readonly property var sh: body ? body.dShot : ({})
    readonly property bool has: !!roles && !!sh && sh.available === true
    readonly property var story: has ? (sh.story || ({})) : ({})
    readonly property var slot: has ? (sh.slot || ({})) : ({})
    readonly property var shape: has ? (sh.shape || ({})) : ({})
    readonly property var impactLines: has && sh.impact ? (sh.impact.lines || []) : []
    readonly property var marks: body && body.dSession && body.dSession.shapes ? (body.dSession.shapes.marks || []) : []
    readonly property real posTagH: roles ? roles.u(14) : 14
    readonly property real posRowH: roles ? roles.u(19) : 19
    readonly property bool folded: !!body && !!body.collapsed && body.collapsed.delivery === true
    height: has ? secHead.height + (folded ? 0 : ty.u(14) + mainCol.height) : 0

    function seekTo(us) { if (us >= 0 && typeof shotReplay !== "undefined" && shotReplay) shotReplay.seekToUs(us) }
    function pick(o) { if (ds.body) ds.body.pickOrdinal(o) }

    PpChFoldHead { id: secHead; visible: ds.has; width: ds.width; roles: ds.ty; key: "delivery"; title: "Club delivery"
                   interactive: ds.interactive; collapsed: ds.folded
                   state_: ds.folded ? (ds.sh.headText || "") : ""
                   onToggled: if (ds.body) ds.body.toggleSection("delivery") }
    Column {
        id: mainCol
        visible: ds.has && !ds.folded
        y: secHead.height + (ds.has ? ds.ty.u(14) : 0)
        width: ds.width
        spacing: ds.has ? ds.ty.u(14) : 0

        // ── the head, and the standfirst at the playhead's position ──
        Column {
            objectName: "chDeliveryHead"
            width: ds.width
            spacing: ds.has ? ds.ty.u(6) : 0
            PpChText { roles: ds.ty; sentence: ds.has ? ds.sh.head : null; font: ds.has ? ds.ty.fStand : Qt.font({}); tone: ds.has ? ds.ty.ink : "black" }
            PpChText { visible: !ds.replay && present; roles: ds.ty; sentence: ds.has && ds.body ? ds.body.dStandfirst : null; font: ds.has ? ds.ty.fBody : Qt.font({}) }
        }

        // ── the plane story (§5.1); in Analyse the published shape, the positions and the slot ──
        Column {
            objectName: "chDeliveryStory"
            visible: ds.has && ds.story.available === true
            width: ds.width
            spacing: ds.has ? ds.ty.u(6) : 0
            Flow {
                width: parent.width
                spacing: ds.has ? ds.ty.u(10) : 0
                Text { text: "Plane"; font: ds.has ? ds.ty.fKicker : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                Text { width: Math.min(implicitWidth, ds.has ? ds.ty.measureW(ds.width) : ds.width); wrapMode: Text.WordWrap
                       text: ds.story.text || ""; font: ds.has ? ds.ty.fBody : Qt.font({}); color: ds.has ? ds.ty.ink : "black" }
            }
            Text { visible: !ds.replay && text !== ""; width: ds.has ? ds.ty.measureW(parent.width) : 0; wrapMode: Text.WordWrap
                   text: ds.story.shapeLine || ""; font: ds.has ? ds.ty.fSmall : Qt.font({}); color: ds.has ? ds.ty.ink2 : "grey" }
            // the plane at each position, the hands' under it; a press seeks there, the playhead's position in ink
            Row {
                visible: !ds.replay && (ds.story.positions || []).length > 0
                spacing: ds.has ? ds.ty.u(16) : 0
                Column {
                    spacing: ds.has ? ds.ty.u(1) : 0
                    Text { text: " "; height: ds.posTagH; font: ds.has ? ds.ty.fFigLabel : Qt.font({}) }
                    Text { text: "plane"; height: ds.posRowH; verticalAlignment: Text.AlignVCenter; font: ds.has ? ds.ty.fLabelUnit : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                    Text { text: "hands"; height: ds.posRowH; verticalAlignment: Text.AlignVCenter; font: ds.has ? ds.ty.fLabelUnit : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                }
                Repeater {
                    model: ds.has ? (ds.story.positions || []) : []
                    delegate: Item {
                        id: pc
                        required property var modelData
                        objectName: "chDeliveryPos"
                        readonly property string tag: modelData.tag
                        readonly property real seekUs: modelData.seekUs
                        readonly property bool current: !!ds.body && ds.body.dSelected === modelData.index
                        width: pcol.width; height: pcol.height
                        Column {
                            id: pcol
                            spacing: ds.ty.u(1)
                            Text { text: pc.modelData.tag; height: ds.posTagH; font: ds.ty.fFigLabel; color: pc.current ? ds.ty.ink : ds.ty.ink3 }
                            Text { text: pc.modelData.text || "-"; height: ds.posRowH; verticalAlignment: Text.AlignVCenter
                                   font: pc.current ? ds.ty.fBodyM : ds.ty.fFigS; color: ds.ty.ink }
                            Text { text: pc.modelData.handsText || "-"; height: ds.posRowH; verticalAlignment: Text.AlignVCenter
                                   font: ds.ty.fFigS; color: ds.ty.ink2 }
                        }
                        MouseArea { anchors.fill: parent; anchors.margins: -ds.ty.u(4); enabled: ds.interactive && pc.seekUs >= 0
                                    cursorShape: Qt.PointingHandCursor; onClicked: ds.seekTo(pc.seekUs) }
                    }
                }
            }
            // the slot: the address line and the shoulder plane (the lines on the plot), and where the curve left it
            Flow {
                objectName: "chDeliverySlot"
                visible: !ds.replay && ds.slot.available === true
                width: parent.width
                spacing: ds.has ? ds.ty.u(10) : 0
                Text { text: "Slot"; font: ds.has ? ds.ty.fKicker : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                Text { text: "address"; font: ds.has ? ds.ty.fSmall : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                Text { text: ds.slot.addressText || "-"; font: ds.has ? ds.ty.fFigS : Qt.font({}); color: ds.has ? ds.ty.ink : "black" }
                Text { text: "shoulders"; font: ds.has ? ds.ty.fSmall : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                Text { text: ds.slot.shouldersText || "-"; font: ds.has ? ds.ty.fFigS : Qt.font({}); color: ds.has ? ds.ty.ink : "black" }
                Text { visible: text !== ""; text: ds.slot.widthText || ""; font: ds.has ? ds.ty.fSmall : Qt.font({}); color: ds.has ? ds.ty.ink2 : "grey" }
                Text { visible: text !== ""; text: ds.slot.text ? "·  " + ds.slot.text : ""; font: ds.has ? ds.ty.fSmall : Qt.font({}); color: ds.has ? ds.ty.ink : "black" }
            }
        }

        // ── the glance: the shot-shape mark when the monitor has one ──
        Row {
            objectName: "chDeliveryShape"
            visible: ds.replay && ds.shape.available === true
            spacing: ds.has ? ds.ty.u(14) : 0
            Text { text: "Shot shape"; font: ds.has ? ds.ty.fKicker : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
            PpChShotShapeGrid { roles: ds.ty; shot: ds.shape; marks: []; line: ""; interactive: false
                                cell: ds.has ? ds.ty.u(20) : 20 }
            Text { text: ds.shape.word || "-"; font: ds.has ? ds.ty.fBodyM : Qt.font({}); color: ds.has ? ds.ty.ink : "black" }
        }

        // ── the picture strip: address, the top and P6 down the line, the lines frozen at address (stage 6); at the glance
        //    too, with no affordance ──
        PpChDeliveryPicture {
            visible: ds.has && on
            width: ds.width
            roles: ds.ty
            picture: ds.has ? (ds.sh.picture || null) : null
            interactive: ds.interactive && !ds.cast
            glance: ds.replay
            onSeek: (us) => ds.seekTo(us)
        }

        // ── Analyse: ball flight | at impact and the shot shape ──
        Item {
            id: cols
            visible: ds.has && !ds.replay
            width: ds.width
            readonly property real gap: ds.has ? ds.ty.u(28) : 28
            readonly property bool two: ds.has && ds.ty.columns(cols.width, 30, gap) >= 2
            readonly property real colW: two ? (cols.width - gap) / 2 : cols.width
            height: !visible ? 0 : two ? Math.max(fl.height, rightCol.height) : fl.height + ds.ty.u(18) + rightCol.height
            PpChDeliveryFlight {
                id: fl
                width: cols.colW
                roles: ds.ty; brush: ds.brush; interactive: ds.interactive && !ds.cast
                ticker: ds.body ? ds.body.relationsP : null
                flight: ds.has ? (ds.sh.flight || null) : null
                onShotActivated: (o) => ds.pick(o)
            }
            Column {
                id: rightCol
                x: cols.two ? cols.colW + cols.gap : 0; y: cols.two ? 0 : fl.height + ds.ty.u(18)
                width: cols.colW
                spacing: ds.has ? ds.ty.u(18) : 0
                Column {
                    objectName: "chDeliveryImpact"
                    visible: ds.impactLines.length > 0
                    width: parent.width
                    spacing: ds.has ? ds.ty.u(6) : 0
                    Text { text: "At impact"; font: ds.has ? ds.ty.fKicker : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                    Repeater {
                        model: ds.impactLines
                        delegate: Text {
                            required property var modelData
                            objectName: "chDeliveryImpactLine"
                            readonly property string key: modelData.key || ""
                            width: ds.ty.measureW(rightCol.width); wrapMode: Text.WordWrap
                            text: modelData.text || "-"; font: ds.ty.fBody; color: ds.ty.ink
                        }
                    }
                }
                Column {
                    objectName: "chDeliveryShape"
                    width: parent.width
                    spacing: ds.has ? ds.ty.u(6) : 0
                    Flow {
                        width: parent.width; spacing: ds.has ? ds.ty.u(10) : 0
                        Text { text: "Shot shape"; font: ds.has ? ds.ty.fKicker : Qt.font({}); color: ds.has ? ds.ty.ink3 : "grey" }
                        Text { text: ds.shape.word || "-"; font: ds.has ? ds.ty.fBodyM : Qt.font({}); color: ds.has ? ds.ty.ink : "black" }
                        Text { visible: ds.shape.available === true
                               text: ds.shape.fromBall ? "start " + (ds.shape.launchDirText || "-") + "  ·  spin axis " + (ds.shape.spinAxisText || "-")
                                                 : "face " + (ds.shape.faceText || "-") + "  ·  face to path " + (ds.shape.faceToPathText || "-")
                               font: ds.has ? ds.ty.fFigS : Qt.font({}); color: ds.has ? ds.ty.ink2 : "grey" }
                    }
                    PpChShotShapeGrid {
                        roles: ds.ty; interactive: ds.interactive && !ds.cast
                        shot: ds.shape; marks: ds.marks
                        line: ds.body && ds.body.dSession && ds.body.dSession.shapes ? (ds.body.dSession.shapes.line || "") : ""
                        onShotActivated: (o) => ds.pick(o)
                    }
                }
            }
        }

        // ── why, said once (§4.9), closed ──
        PpChTrustFold {
            prefix: "chDelivery"
            visible: ds.has && !ds.replay && !ds.cast && (lines || []).length > 0
            width: ds.width; roles: ds.ty
            lines: ds.has && ds.sh.trust ? (ds.sh.trust.lines || []) : []
        }
    }
}
