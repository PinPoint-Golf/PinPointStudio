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

// Transit — the swing's phases as a line of stations, and the master scrub/seek
// surface for Review mode. Stations sit at their TRUE proportional position on the
// line; upright full-word labels are nudged along the line only as far as needed to
// clear their neighbours (a 1-D solve done in C++ — TimelineLabels), with a hairline
// elbow back to each dot. A bead rides the line at the playhead carrying a readout
// chip (phase · time · live metric). Drag the line to scrub; click a station to seek.
// Both orientations render from one station model:
//   • horizontal — line near the top, labels in a row beneath (host: top rail).
//   • vertical   — line near the left, labels in a column to the right (host: left of stage).
// All iterative maths lives on `solver`; QML keeps to declarative bindings + handlers.
//
// A FLAT RAIL, NOT A CARD: it runs along the stage's edge and frames nothing. Its chips are the
// card family's chips (PpChip), and the measured P-positions use the provenance marks — a solid
// dot where the position was fitted, a hollow ring where it was sampled off the track — with
// the key in words at the rail's far end.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Item {
    id: root

    // Set by the host; bound to appSettings.timelineOrientation in Phase 4.
    property string orientation: "horizontal"
    property bool   snapToPhases: false

    readonly property bool _horizontal: orientation !== "vertical"

    // ── Replay-derived model (one µs domain: window-relative, t0 subtracted) ──────
    readonly property var  _detail:  shotReplay.analysisDetail
    readonly property var  _phases:  (_detail && _detail.phases) ? _detail.phases : []
    readonly property var  _series0: (_detail && _detail.series && _detail.series.length)
                                     ? _detail.series[0] : null
    readonly property real _span:    Math.max(1, shotReplay.endUs - shotReplay.startUs)
    readonly property real _playFrac: Math.max(0, Math.min(1,
                                       (shotReplay.positionUs - shotReplay.startUs) / _span))
    readonly property int  _activeIdx: solver.activeStation(_phases, shotReplay.positionUs)

    // ── Geometry (cross-axis offsets fixed; main-axis = the line direction) ──────
    // The cross-axis stack, from the rail's top (horizontal) or left (vertical) edge: the readout
    // chip's row (horizontal only), the P-chips, their provenance marks at _markGap off the line,
    // the line, then the station labels. Vertical puts the P-chips LEFT of the line, so the line
    // stands far enough in for the widest of them ("P10") to fit.
    readonly property real _insetMain: _horizontal ? Theme.sp(34) : Theme.sp(22)
    readonly property real _lineCross: _horizontal ? Theme.sp(52) : Theme.sp(48)
    readonly property real _labelBand: _horizontal ? Theme.sp(80) : Theme.sp(68)
    readonly property real _markGap:   Theme.sp(10)     // line → provenance mark centre
    readonly property real _chipGap:   Theme.sp(14)     // line → P-chip's near edge
    readonly property real _lineLen:   Math.max(1, (_horizontal ? width : height) - 2 * _insetMain)
    readonly property real _beadMain:  _insetMain + _playFrac * _lineLen
    // Markup diamonds ride the side of the line OPPOSITE the labels (above when
    // horizontal, left when vertical); this is the cross-axis offset of their centre.
    readonly property real _diamondGap: Theme.sp(13)

    // Natural cross-axis extent of the horizontal layout: line → elbow drop to the
    // label band → one upright label row (+ a little breathing room). Hosts bind the
    // top rail's height to this so no dead space is left beneath the labels.
    readonly property real contentHeight: _labelBand + Theme.sp(2)
                                          + Math.round(Theme.fontSzLabel * 1.5) + Theme.sp(4)

    // The full render model — recomputed only on layout/orientation/data change (NOT
    // on playhead movement). Each entry: {phase,tUs,frac,center,label,name,isImpact,elbow}.
    readonly property var _stations: shotReplay.active
        ? solver.stationLayout(_phases, shotReplay.startUs, shotReplay.endUs, _horizontal,
                               _lineLen, Theme.sp(12), Theme.fontData, Theme.fontSzLabel)
        : []

    // Measured coaching positions (P1..P8 — the fused TrackSample/MilestoneFit club
    // track from shaft_position_first) in the same window-relative µs domain as the
    // stations. Present on both the live and disk replay facades. No View toggle
    // today — this is always-on chrome that rides with the timeline itself; a
    // user-facing show/hide would be a View-menu/ViewLayout concern (per-view display
    // settings), not something owned here.
    readonly property var _positions: (_detail && _detail.club) ? (_detail.club.positions || []) : []
    // gap arg reserves a PpChip's padding (sp(12)) plus a hair between neighbours, so
    // clustered chips (the P5/P6/P7 downswing group) never touch.
    readonly property var _pTicks: (shotReplay.active && _positions.length > 0)
        ? solver.positionLayout(_positions, shotReplay.startUs, shotReplay.endUs, _horizontal,
                                _lineLen, Theme.sp(14), Theme.fontData, Theme.fontSzMicro)
        : []
    readonly property bool _anyFitted:  _pTicks.some(function (t) { return t.source === 1 })
    readonly property bool _anySampled: _pTicks.some(function (t) { return t.source !== 1 })

    // Ground-truth markup positions for the swing currently on the line, in the same
    // window-relative µs domain as the stations. Shown only while the Markup panel is
    // actually on-screen (panelVisible) and focused on THIS swing — so the diamonds
    // are a live markup-workflow affordance that disappears when the panel is toggled
    // out of the View, not always-on chrome.
    readonly property var _markupMarkers: (shotReplay.active
        && markupController.panelVisible
        && markupController.hasSwing
        && markupController.currentSwingDir === shotReplay.swingDir)
        ? markupController.eventList : []

    // Every frame on the current swing that has a club/shaft laid, same domain and
    // visibility gate as the diamonds above. Drawn as thin ticks straddling the line
    // so the spread of shaft markings reads at a glance — complementing the diamonds,
    // which only flag the named P-positions.
    readonly property var _shaftMarkers: (shotReplay.active
        && markupController.panelVisible
        && markupController.hasSwing
        && markupController.currentSwingDir === shotReplay.swingDir)
        ? markupController.shaftList : []

    // Readout chip values at the playhead.
    readonly property string _activeName: (_activeIdx >= 0 && _activeIdx < _stations.length)
                                          ? _stations[_activeIdx].name : ""
    readonly property real   _metricVal:  _series0
                                          ? solver.valueAtNearest(_series0.t_us, _series0.value,
                                                                  shotReplay.positionUs) : 0
    readonly property string _metricUnit: (_series0 && _series0.unit) ? _series0.unit : ""

    // The one value-formatting rule, shared with PpMetricChart and PpChartSummary.
    ChartMetrics { id: chartFmt }

    TimelineLabels { id: solver }

    // Map a pointer position (in `root` coords) to a seek. Imperative handler helper
    // (no iteration); the snap search itself lives in C++.
    function _scrubTo(pt) {
        var main = root._horizontal ? pt.x : pt.y
        var frac = Math.max(0, Math.min(1, (main - root._insetMain) / root._lineLen))
        if (root.snapToPhases)
            shotReplay.seekToUs(solver.snap(root._phases,
                                            shotReplay.startUs + frac * root._span,
                                            0.03 * root._span))
        else
            shotReplay.seekToFraction(frac)
    }

    // ── Active timeline ──────────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: shotReplay.active

        // Backing line.
        Rectangle {
            radius: Theme.sp(2)
            color: Theme.colorBorderMid
            x: root._horizontal ? root._insetMain : (root._lineCross - width / 2)
            y: root._horizontal ? (root._lineCross - height / 2) : root._insetMain
            width:  root._horizontal ? root._lineLen : Theme.sp(4)
            height: root._horizontal ? Theme.sp(4)   : root._lineLen
        }
        // Travelled fill up to the playhead.
        Rectangle {
            radius: Theme.sp(2)
            color: Theme.colorAccent
            x: root._horizontal ? root._insetMain : (root._lineCross - width / 2)
            y: root._horizontal ? (root._lineCross - height / 2) : root._insetMain
            width:  root._horizontal ? (root._playFrac * root._lineLen) : Theme.sp(4)
            height: root._horizontal ? Theme.sp(4) : (root._playFrac * root._lineLen)
        }

        // ── Shaft ticks — frames carrying a club/shaft label ────────────────────
        // One thin tick per shaft-marked frame, straddling the line at its true
        // proportional time. Subtle and non-interactive (the diamonds own the seek
        // affordance): they just show where the club has been laid along the swing.
        // Declared before the dots/labels/diamonds/playhead so all of those draw over.
        Repeater {
            model: root._shaftMarkers
            delegate: Rectangle {
                id: tick
                required property var modelData
                readonly property real frac: Math.max(0, Math.min(1,
                    (tick.modelData.tUs - shotReplay.startUs) / root._span))
                readonly property real tickMain: root._insetMain + frac * root._lineLen
                readonly property real len:  Theme.sp(11)
                readonly property real thick: Theme.sp(2)
                width:  root._horizontal ? thick : len
                height: root._horizontal ? len   : thick
                radius: Theme.sp(1)
                antialiasing: true
                color: Theme.colorGood
                opacity: 0.5
                x: (root._horizontal ? tickMain : root._lineCross) - width / 2
                y: (root._horizontal ? root._lineCross : tickMain) - height / 2
            }
        }

        // ── Measured P-positions — provenance marks ─────────────────────────────
        // One mark per fused position (see root._pTicks above), at its TRUE proportional time,
        // just off the line on the P-chips' side — the anchor the chip above it was nudged from.
        // The provenance vocabulary: a solid dot for MilestoneFit (source 1, the discrete fit),
        // a hollow ring for TrackSample (source 0, sampled off the fused track). The clickable
        // "Pn" chips that carry the label and seek to each position are a SEPARATE layer
        // declared after the scrub band (so their taps win) — below.
        Repeater {
            model: root._pTicks
            delegate: Rectangle {
                id: ptick
                required property var modelData
                readonly property bool fitted: ptick.modelData.source === 1
                readonly property real tickMain: root._insetMain + ptick.modelData.frac * root._lineLen
                readonly property real markCross: root._lineCross - root._markGap
                width: Theme.sp(6); height: width; radius: width / 2
                antialiasing: true
                color: ptick.fitted ? Theme.colorText2 : "transparent"
                border.width: ptick.fitted ? 0 : Theme.sp(1.5)
                border.color: Theme.colorText2
                x: (root._horizontal ? tickMain : markCross) - width / 2
                y: (root._horizontal ? markCross : tickMain) - height / 2
            }
        }

        // Elbow connectors (dot → label). Horizontal always draws the drop; the
        // cross run appears only when the label is offset. Vertical draws nothing
        // when the label sits on its dot.
        Repeater {
            model: root._stations
            delegate: Item {
                id: conn
                required property var modelData
                anchors.fill: parent
                readonly property real dotMain:   root._insetMain + conn.modelData.center
                readonly property real labelMain: root._insetMain + conn.modelData.label

                // horizontal: vertical drop at the dot
                Rectangle {
                    visible: root._horizontal
                    color: Theme.colorBorderStrong
                    width: Theme.sp(1)
                    x: conn.dotMain - width / 2
                    y: root._lineCross + Theme.sp(8)
                    height: Math.max(0, root._labelBand - (root._lineCross + Theme.sp(8)))
                }
                // horizontal: cross run to the offset label
                Rectangle {
                    visible: root._horizontal && conn.modelData.elbow
                    color: Theme.colorBorderStrong
                    height: Theme.sp(1)
                    x: Math.min(conn.dotMain, conn.labelMain)
                    y: root._labelBand - height / 2
                    width: Math.abs(conn.labelMain - conn.dotMain)
                }
                // vertical: run out from the line
                Rectangle {
                    visible: !root._horizontal && conn.modelData.elbow
                    color: Theme.colorBorderStrong
                    height: Theme.sp(1)
                    x: root._lineCross + Theme.sp(2)
                    y: conn.dotMain - height / 2
                    width: Math.max(0, root._labelBand - (root._lineCross + Theme.sp(2)))
                }
                // vertical: drop to the offset label
                Rectangle {
                    visible: !root._horizontal && conn.modelData.elbow
                    color: Theme.colorBorderStrong
                    width: Theme.sp(1)
                    x: root._labelBand - width / 2
                    y: Math.min(conn.dotMain, conn.labelMain)
                    height: Math.abs(conn.labelMain - conn.dotMain)
                }
            }
        }

        // Scrub band over the line — drag to scrub, tap empty line to seek there.
        // Declared below the dots/labels so their tap handlers win for station seeks;
        // the DragHandler still claims drags that start anywhere on the band.
        Item {
            id: scrubBand
            x: root._horizontal ? root._insetMain : (root._lineCross - Theme.sp(14))
            y: root._horizontal ? (root._lineCross - Theme.sp(14)) : root._insetMain
            width:  root._horizontal ? root._lineLen : Theme.sp(28)
            height: root._horizontal ? Theme.sp(28)  : root._lineLen

            DragHandler {
                target: null
                dragThreshold: 0
                onActiveChanged: active ? shotReplay.beginScrub() : shotReplay.endScrub()
                onCentroidChanged: if (active)
                    root._scrubTo(scrubBand.mapToItem(root, centroid.position.x, centroid.position.y))
            }
            TapHandler {
                onTapped: (ep) => root._scrubTo(scrubBand.mapToItem(root, ep.position.x, ep.position.y))
            }
            HoverHandler { cursorShape: root._horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor }
        }

        // Station dots.
        Repeater {
            model: root._stations
            delegate: Rectangle {
                id: dot
                required property var modelData
                required property int index
                readonly property bool active: dot.index === root._activeIdx
                readonly property real sz: dot.modelData.isImpact ? Theme.sp(13) : Theme.sp(11)
                readonly property real dotMain: root._insetMain + dot.modelData.center
                width: sz; height: sz; radius: sz / 2
                x: (root._horizontal ? dotMain : root._lineCross) - sz / 2
                y: (root._horizontal ? root._lineCross : dotMain) - sz / 2
                color: dot.modelData.isImpact ? Theme.colorAccent : Theme.colorSurface
                border.width: 2
                border.color: (dot.active || dot.modelData.isImpact) ? Theme.colorAccent
                                                                     : Theme.colorBorderStrong
                scale: dot.active ? 1.18 : 1.0
                Behavior on scale {
                    enabled: !Theme.reduceMotion
                    NumberAnimation { duration: Theme.durationFast }
                }
                TapHandler { onTapped: shotReplay.seekToUs(dot.modelData.tUs) }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }

        // Upright station labels (full words).
        Repeater {
            model: root._stations
            delegate: Text {
                id: lbl
                required property var modelData
                required property int index
                readonly property bool active: lbl.index === root._activeIdx
                readonly property real labelMain: root._insetMain + lbl.modelData.label
                text: lbl.modelData.name
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
                font.weight: lbl.active ? Font.DemiBold : Font.Normal
                color: (lbl.active || lbl.modelData.isImpact) ? Theme.colorAccent : Theme.colorText2
                x: root._horizontal ? (labelMain - width / 2) : (root._labelBand + Theme.sp(6))
                y: root._horizontal ? (root._labelBand + Theme.sp(2)) : (labelMain - height / 2)
                TapHandler { onTapped: shotReplay.seekToUs(lbl.modelData.tUs) }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }

        // ── Measured P-positions — clickable "Pn" chips ─────────────────────────
        // The seek affordance for the measured coaching positions: one small chip
        // per fused position, carrying the "Pn" label at the solved (never-
        // overlapping) main-axis position so the clustered downswing positions stay
        // legible. Sits on the side of the line OPPOSITE the station labels (above
        // when horizontal, left when vertical), over its provenance tick above.
        // Subtle by default, brightening on hover (chromeless-until-hover house
        // style); tap seeks the playhead here, mirroring a station tap. Declared
        // after the scrub band so a chip tap wins over a scrub drag.
        Repeater {
            model: root._pTicks
            delegate: PpChip {
                id: pchip
                required property var modelData
                readonly property bool hovered: pchipHover.hovered
                readonly property real labelMain: root._insetMain + pchip.modelData.center
                text: pchip.modelData.label
                tinted: pchip.hovered
                tone: pchip.hovered ? Theme.colorAccent : Theme.colorText2
                x: root._horizontal ? (labelMain - width / 2)
                                    : (root._lineCross - root._chipGap - width)
                y: root._horizontal ? (root._lineCross - root._chipGap - height)
                                    : (labelMain - height / 2)

                TapHandler { onTapped: shotReplay.seekToUs(pchip.modelData.tUs) }
                HoverHandler { id: pchipHover; cursorShape: Qt.PointingHandCursor }
            }
        }

        // The provenance key, in words, at the rail's far end: across the top (horizontal),
        // under the line (vertical). Only the marks that are on the rail are named.
        Row {
            visible: root._pTicks.length > 0
            spacing: Theme.gap(10)
            x: root._horizontal ? root.width - root._insetMain - width : Theme.sp(6)
            y: root._horizontal ? Theme.sp(3) : root.height - height - Theme.sp(4)
            Repeater {
                model: [{ fitted: true,  on: root._anyFitted,  text: qsTr("fitted") },
                        { fitted: false, on: root._anySampled, text: qsTr("sampled") }]
                delegate: Row {
                    id: keyItem
                    required property var modelData
                    visible: keyItem.modelData.on
                    spacing: Theme.gap(4)
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.sp(6); height: width; radius: width / 2
                        color: keyItem.modelData.fitted ? Theme.colorText2 : "transparent"
                        border.width: keyItem.modelData.fitted ? 0 : Theme.sp(1.5)
                        border.color: Theme.colorText2
                    }
                    PpMicro {
                        anchors.verticalCenter: parent.verticalCenter
                        text: keyItem.modelData.text
                        font.letterSpacing: Theme.trackingData
                    }
                }
            }
        }

        // ── Markup overlay — ground-truth P-positions as diamonds ───────────────
        // One diamond per marked position, sitting just off the line on the side away
        // from the labels, at its TRUE proportional time — so a manual markup can be
        // eyeballed against the discovered station directly across the line. Solid =
        // the position also has a club/shaft laid; hollow = time only. Tap to seek.
        Repeater {
            model: root._markupMarkers
            delegate: Item {
                id: mk
                required property var modelData
                anchors.fill: parent
                readonly property real frac: Math.max(0, Math.min(1,
                    (mk.modelData.tUs - shotReplay.startUs) / root._span))
                readonly property real markMain:  root._insetMain + frac * root._lineLen
                readonly property real markCross: root._lineCross - root._diamondGap

                Rectangle {
                    readonly property real dsz: Theme.sp(9)
                    width: dsz; height: dsz
                    rotation: 45
                    radius: Theme.sp(1)
                    antialiasing: true
                    color: mk.modelData.hasClub ? Theme.colorGood : "transparent"
                    border.width: mk.modelData.hasClub ? 0 : Theme.sp(2)
                    border.color: Theme.colorGood
                    x: (root._horizontal ? mk.markMain : mk.markCross) - width / 2
                    y: (root._horizontal ? mk.markCross : mk.markMain) - height / 2
                }
                // Un-rotated hit target (a touch larger than the diamond) — seeks the
                // playhead to this markup time, mirroring a station tap.
                Item {
                    readonly property real hsz: Theme.sp(20)
                    width: hsz; height: hsz
                    x: (root._horizontal ? mk.markMain : mk.markCross) - hsz / 2
                    y: (root._horizontal ? mk.markCross : mk.markMain) - hsz / 2
                    TapHandler { onTapped: shotReplay.seekToUs(mk.modelData.tUs) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }

        // Playhead halo + bead.
        Rectangle {
            readonly property real sz: Theme.sp(21)
            width: sz; height: sz; radius: sz / 2
            color: Theme.colorAccentLight
            x: (root._horizontal ? root._beadMain : root._lineCross) - sz / 2
            y: (root._horizontal ? root._lineCross : root._beadMain) - sz / 2
        }
        Rectangle {
            readonly property real sz: Theme.sp(13)
            width: sz; height: sz; radius: sz / 2
            color: Theme.colorAccent
            x: (root._horizontal ? root._beadMain : root._lineCross) - sz / 2
            y: (root._horizontal ? root._lineCross : root._beadMain) - sz / 2
        }

        // Readout chip — follows the bead. Horizontal: along the top, centred on the
        // playhead. Vertical: in the label column (aligned with the station labels),
        // shifting left only if a wide chip would overflow the panel edge. A PpChip in the
        // accent, on an opaque base so the P-chips and labels it passes over do not show through.
        Rectangle {
            id: readout
            color: Theme.colorSurface
            radius: height / 2
            width:  readoutRow.implicitWidth + Theme.sp(14)
            height: Theme.sp(18)
            x: root._horizontal
               ? Math.max(root._insetMain,
                          Math.min(root.width - root._insetMain - width, root._beadMain - width / 2))
               : Math.max(Theme.sp(6),
                          Math.min(root._labelBand, root.width - width - Theme.sp(6)))
            y: root._horizontal
               ? 0
               : Math.max(root._insetMain,
                          Math.min(root.height - root._insetMain - height, root._beadMain - height / 2))

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Qt.alpha(Theme.colorAccent, Theme.dark ? 0.12 : 0.09)
                border.width: 1
                border.color: Qt.alpha(Theme.colorAccent, 0.45)
            }
            Row {
                id: readoutRow
                anchors.centerIn: parent
                spacing: Theme.gap(7)
                PpMicro {
                    text: root._activeName
                    // Vertical mode omits the phase name — the highlighted active
                    // station label in the column already shows it (keeps the chip
                    // compact + aligned with the labels).
                    visible: root._horizontal && text.length > 0
                    color: Theme.colorAccent
                    font.letterSpacing: Theme.trackingData
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: 1; height: Theme.sp(10); color: Qt.alpha(Theme.colorAccent, 0.45)
                    visible: root._horizontal && root._activeName.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                }
                PpMicro {
                    text: ((shotReplay.positionUs - shotReplay.startUs) / 1000000).toFixed(2) + "s"
                    color: Theme.colorText2
                    font.letterSpacing: Theme.trackingData
                    anchors.verticalCenter: parent.verticalCenter
                }
                PpMicro {
                    // ChartMetrics.formatValue, not a local round-and-concatenate: this used to
                    // print "12% stance width" — the catalogue's full phrase, jammed against the
                    // number with no separator — in a bead chip a few characters wide.
                    visible: root._series0 !== null
                    text: chartFmt.formatValue(root._metricVal, root._metricUnit)
                    font.letterSpacing: Theme.trackingData
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // ── Empty state ──────────────────────────────────────────────────────────────
    PpCardNote {
        anchors.centerIn: parent
        visible: !shotReplay.active
        text: qsTr("Select a swing to review")
    }
}
