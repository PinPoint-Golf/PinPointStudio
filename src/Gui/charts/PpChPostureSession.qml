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

// PPCHPOSTURESESSION — THE POSTURE SECTION OF SESSION (docs/design/posture_chart_design.md §5.5, 5.6; built the
// way PpChWristSession is; PpChartsSessionView draws it under its own fold head "Posture", after the standard
// session content, in Analyse only). "Which pattern is my posture, and do I leave it the same way every time?"
//   · the head and the pattern line (the session's, on the medians);
//   · the profile in Session mode: one box per position per row over the pattern's band (PpChBoxCell → PpBoxPlot,
//     the ONE box plot; the hover lays the values on the plot), this shot ringed, the median and how wide the
//     middle half is under each; "How far can I trust this?" under it, closed (said once);
//   · "At address" (PpChPostureAddress): the rig-sensitive address readings, the session's box against the band and
//     the earlier sessions' box from the same set-up, each with its ⚠;
//   · the pattern by shot: a mark per shot, ringed where its nearest pattern is not the session's; a mark opens
//     that swing;
//   · the departures as counts across the shots, each name opening its condition in Session diagnostics (when
//     that panel is up);
//   · repeatability: the tightest and the loosest row and position.
// Every word and number is ChartsPosturePresenter.session's (body.pSession).
// objectNames: chPostureSession, chPostureSessionHead, chPostureStrip (marks chPostureMark + ordinal, differs),
// chPostureDepartures (rows chPostureDeparture + id), chPostureRepeat.
Column {
    id: ss
    objectName: "chPostureSession"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property bool interactive: true
    readonly property var se: body ? body.pSession : ({})
    readonly property bool ok: !!roles
    readonly property bool available: ok && !!se && se.available === true
    readonly property var prof: available ? (se.profile || ({})) : ({})
    readonly property var strip: available ? (se.strip || ({})) : ({})
    readonly property var rep: available ? (se.repeat || ({})) : ({})
    readonly property bool canOpen: interactive && !!body && body.canOpenCondition
    spacing: ok ? roles.u(18) : 18

    Column {
        objectName: "chPostureSessionHead"
        width: ss.width; spacing: ss.ok ? ss.roles.u(6) : 6
        PpChText { roles: ss.roles; sentence: ss.se ? ss.se.head : null
                   font: ss.ok ? ss.roles.fStand : Qt.font({}); tone: ss.ok ? ss.roles.ink : "black" }
        Flow {
            objectName: "chPostureSessionPattern"
            visible: ss.available && !!ss.se.patternLine
            width: parent.width; spacing: ss.ok ? ss.roles.u(10) : 10
            Text { text: "Pattern"; font: ss.ok ? ss.roles.fKicker : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
            Text { width: Math.min(implicitWidth, ss.width); wrapMode: Text.WordWrap
                   text: ss.se.patternLine || ""; font: ss.ok ? ss.roles.fBody : Qt.font({}); color: ss.ok ? ss.roles.ink : "black" }
        }
    }

    // ── the profile: a box per position, this shot ringed ──
    Column {
        visible: ss.available
        width: ss.width; spacing: ss.ok ? ss.roles.u(8) : 8
        PpSectionHead { width: parent.width; roles: ss.roles; title: "Positions across the session"; sub: ss.prof.sub || "" }
        PpChPostureProfile {
            width: parent.width
            roles: ss.roles; brush: ss.brush; interactive: ss.interactive
            ticker: ss.body ? ss.body.relationsP : null
            rows: ss.prof.rows || []
            positions: ss.prof.positions || []
            selected: ss.body ? ss.body.pSelected : -1
            mode: "session"
            onColumnActivated: (i) => { const p = (ss.prof.positions || [])[i]
                                        if (p && p.seekUs >= 0 && typeof shotReplay !== "undefined" && shotReplay) shotReplay.seekToUs(p.seekUs) }
        }
        Text { visible: text !== ""; width: ss.ok ? ss.roles.measureW(parent.width) : 0; wrapMode: Text.WordWrap; text: ss.prof.caption || ""
               font: ss.ok ? ss.roles.fSmall : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
        PpChPostureTrust { width: parent.width; roles: ss.roles
                           lines: ss.available && ss.se.trust ? (ss.se.trust.lines || []) : [] }
    }

    // ── at address: the session's box per reading against the band, and the earlier sessions' box (§7.4.2) ──
    PpChPostureAddress { visible: ss.available && rows.length > 0; width: ss.width; roles: ss.roles; brush: ss.brush
                         interactive: ss.interactive; mode: "session"
                         block: ss.available ? (ss.se.address || null) : null }

    // ── the pattern by shot: a mark per shot; ringed = not the session's pattern; a mark opens that swing ──
    Column {
        id: strip
        objectName: "chPostureStrip"
        visible: ss.available && (ss.strip.marks || []).length > 0
        width: ss.width; spacing: ss.ok ? ss.roles.u(6) : 6
        readonly property var marks: ss.strip.marks || []
        readonly property real pitch: ss.ok ? Math.min(ss.roles.u(22), Math.max(ss.roles.u(10), (width - ss.roles.u(4)) / Math.max(1, marks.length))) : 16
        property string hoverLabel: ""
        PpSectionHead { width: parent.width; roles: ss.roles; title: "Pattern by shot"; sub: ss.strip.legend || "" }
        Item {
            width: parent.width; height: ss.ok ? ss.roles.u(24) : 24
            Repeater {
                model: strip.marks
                delegate: Item {
                    id: mk
                    required property var modelData
                    required property int index
                    objectName: "chPostureMark"
                    readonly property int ordinal: modelData.ordinal
                    readonly property bool differs: modelData.differs === true
                    readonly property bool picked: modelData.picked === true
                    x: index * strip.pitch; width: strip.pitch; height: parent.height
                    // the session's pattern: a filled bar; another pattern (or between): a hollow ringed bar; none: a hairline
                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.max(4, strip.pitch * 0.5); height: parent.height * 0.72
                        radius: ss.roles.square ? 0 : ss.roles.u(2)
                        color: !mk.modelData.ok ? "transparent" : mk.differs ? ss.roles.surface : ss.roles.ink2
                        border.width: !mk.modelData.ok ? 1 : mk.differs ? 2 : 0
                        border.color: !mk.modelData.ok ? ss.roles.hair : ss.roles.warn
                    }
                    // this shot: the ink ring round its mark
                    Rectangle { visible: mk.picked; anchors.centerIn: parent; width: Math.max(8, strip.pitch * 0.5) + ss.roles.u(6); height: parent.height
                                radius: ss.roles.square ? 0 : ss.roles.u(4); color: "transparent"; border.width: 1.5; border.color: ss.roles.ink }
                    MouseArea {
                        anchors.fill: parent; hoverEnabled: true
                        enabled: ss.interactive
                        cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: strip.hoverLabel = containsMouse ? (mk.modelData.label || "") : (strip.hoverLabel === (mk.modelData.label || "") ? "" : strip.hoverLabel)
                        onClicked: if (ss.body) ss.body.pickOrdinal(mk.ordinal)
                    }
                }
            }
        }
        Text { width: parent.width; wrapMode: Text.WordWrap
               text: strip.hoverLabel !== "" ? strip.hoverLabel : (ss.strip.line || "")
               font: ss.ok ? ss.roles.fSmall : Qt.font({}); color: ss.ok ? ss.roles.ink2 : "grey" }
    }

    // ── the departures: counts across the shots; a name opens its condition ──
    Column {
        objectName: "chPostureDepartures"
        visible: ss.available
        width: ss.width; spacing: ss.ok ? ss.roles.u(4) : 4
        readonly property var deps: ss.available ? (ss.se.departures || []) : []
        PpSectionHead { width: parent.width; roles: ss.roles; title: "Departures"; sub: "how many shots each fault was found on, and where it starts" }
        Text { visible: parent.deps.length === 0; width: parent.width; wrapMode: Text.WordWrap
               text: ss.se.departuresEmpty || ""; font: ss.ok ? ss.roles.fBody : Qt.font({}); color: ss.ok ? ss.roles.ink2 : "grey" }
        Repeater {
            model: parent.deps
            delegate: Item {
                id: dr
                required property var modelData
                objectName: "chPostureDeparture"
                readonly property string depId: modelData.id || ""
                readonly property string conditionId: (modelData.conditionIds || [])[0] || ""
                readonly property bool opens: ss.canOpen && conditionId !== ""
                width: ss.width; height: ss.roles.u(26)
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ss.roles.hw; color: ss.roles.hair }
                Text { id: dn; y: ss.roles.u(4); width: Math.min(implicitWidth, parent.width * 0.6); elide: Text.ElideRight
                       text: dr.modelData.name || ""; font: dr.opens ? ss.roles.fSmallL : ss.roles.fSmallM; color: dr.opens ? ss.roles.link : ss.roles.ink
                       MouseArea { anchors.fill: parent; enabled: dr.opens; cursorShape: Qt.PointingHandCursor
                                   onClicked: if (ss.body) ss.body.openCondition(dr.conditionId) } }
                Text { x: dn.x + dn.width + ss.roles.u(12); y: ss.roles.u(4); width: parent.width - x; elide: Text.ElideRight
                       text: dr.modelData.detail || ""
                       font: ss.ok ? ss.roles.fSmall : Qt.font({}); color: ss.ok ? ss.roles.ink2 : "grey" }
            }
        }
    }

    // ── repeatability: the tightest and the loosest row and position ──
    Column {
        objectName: "chPostureRepeat"
        visible: ss.available && (!!ss.rep.words || (!!ss.rep.tightest && ss.rep.tightest.has) || (!!ss.rep.loosest && ss.rep.loosest.has))
        width: ss.width; spacing: ss.ok ? ss.roles.u(4) : 4
        PpSectionHead { width: parent.width; roles: ss.roles; title: "Repeatable?"; sub: "" }
        Text { visible: text !== ""; width: ss.ok ? ss.roles.measureW(parent.width) : 0; wrapMode: Text.WordWrap
               text: ss.rep.words || ""; font: ss.ok ? ss.roles.fBody : Qt.font({}); color: ss.ok ? ss.roles.ink : "black" }
    }
}
