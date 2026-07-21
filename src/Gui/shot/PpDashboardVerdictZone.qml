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

// Verdict zone — the headline read, rendered as PRIMITIVES rather than numbers: the
// score is a PpDonutStat (arc + centre value ± uncertainty, hover-revealing the
// per-region contributions), the wrist pattern is a PpStateChip, tempo is a
// PpRangeBar marker in its ideal band, and the top fault is a fix line that seeks the
// replay to the phase it was found at.
//
// Drop rules: tempo has no producer today, so its tile is ABSENT rather than showing
// a fabricated ratio; the fault/strength cards come from the live
// WristDiagnosticsModel (severity-ordered) and are wrist-only — other session types
// simply have no findings and those rows do not render. The zone collapses to zero
// height when there is no score at all.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: zone

    property var detail: ({})
    property var wristModel: null
    property var catalog: null
    property var shotCtx: ({})

    // Interaction is window-only; the wall cast leaves this false so the resting
    // render of the two surfaces is identical.
    property bool interactive: false

    signal metricActivated(string key)

    ChartMetrics { id: cm }

    readonly property int  _score:   detail && detail.overall !== undefined ? detail.overall : -1
    readonly property int  _half:    detail && detail.interval ? detail.interval.halfWidth : -1
    readonly property string _pattern: detail && detail.pattern ? detail.pattern : ""
    readonly property bool _blended: detail && detail.blended === true

    // Donut breakdown — perRegion when the scorer produced it (adherence), else
    // perPhase, else nothing. Ordering is C++ (ChartMetrics.scoreSegments).
    readonly property var _segments: {
        if (!detail) return []
        if (detail.perRegion && Object.keys(detail.perRegion).length > 0)
            return cm.scoreSegments(detail.perRegion)
        if (detail.perPhase && Object.keys(detail.perPhase).length > 0)
            return cm.scoreSegments(detail.perPhase)
        return []
    }

    readonly property var _fault:    (wristModel && wristModel.findings && wristModel.findings.length > 0)
                                     ? wristModel.findings[0] : null
    readonly property var _strength: (wristModel && wristModel.strengths && wristModel.strengths.length > 0)
                                     ? wristModel.strengths[0] : null

    // Tempo: rendered only when the swing actually carries a value. No producer today
    // ⇒ no tile (a reserved-but-empty slot reads as a broken metric).
    readonly property var _tempo: _seriesFor("tempoRatio")
    readonly property var _tempoSample: _tempo ? _sampleAt(_tempo, 5 /*Impact*/) : null
    readonly property var _tempoCorridor: (catalog && _tempoSample)
        ? _corridorAt(catalog.descriptor("tempoRatio", shotCtx), 5) : null
    readonly property bool _hasTempo: _tempoSample !== null && _tempoCorridor !== null

    function _seriesFor(key) {
        var s = detail && detail.series ? detail.series : []
        for (var i = 0; i < s.length; ++i) if (s[i].key === key) return s[i]
        return null
    }
    function _sampleAt(series, phase) {
        var ps = series && series.phaseSamples ? series.phaseSamples : []
        for (var i = 0; i < ps.length; ++i) if (ps[i].phase === phase) return ps[i]
        return null
    }
    function _corridorAt(desc, phase) {
        var cs = (desc && desc.normative && desc.normative.corridors)
                 ? desc.normative.corridors : []
        for (var i = 0; i < cs.length; ++i) if (cs[i].phase === phase) return cs[i]
        return null
    }

    // Which catalogue entry the headline score IS — a resemblance estimand (Wrist)
    // carries a pattern label, adherence (Swing/GRF) does not. Lets the donut open
    // the same detail page as the rest of the tiles rather than being the one
    // number on the dashboard you cannot interrogate.
    readonly property string _scoreKey: (detail && detail.pattern) ? "wristScore" : "swingScore"

    readonly property bool _hasContent: _score >= 0
    implicitHeight: _hasContent ? card.implicitHeight : 0
    visible: _hasContent

    Rectangle {
        id: card
        width: zone.width
        implicitHeight: col.implicitHeight + Theme.sp(24)
        color: Theme.colorSurface
        radius: Theme.radiusLg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder

        // Coloured left edge — quality of this shot's score.
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.sp(3)
            radius: Theme.radiusLg
            color: Theme.qualityColor(zone._score)
        }

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(11); rightMargin: Theme.sp(11); topMargin: Theme.sp(10) }
            spacing: Theme.sp(10)

            Text {
                text: qsTr("VERDICT")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            // Score donut + the categorical/ratio reads beside it.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(16)

                PpDonutStat {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth:  Theme.sp(112)
                    Layout.preferredHeight: Theme.sp(112)
                    value:       zone._score
                    maxValue:    100
                    interval:    zone._half
                    bandColor:   Theme.qualityColor(zone._score)
                    centerLabel: qsTr("Score")
                    segments:    zone._segments
                    interactive: zone.interactive

                    // Click-through to the score's catalogue page, same gesture as
                    // every other tile. (HoverHandler inside the donut drives the
                    // breakdown; it does not consume taps, so both coexist.)
                    TapHandler {
                        enabled: zone.interactive
                        onTapped: zone.metricActivated(zone._scoreKey)
                    }
                    HoverHandler {
                        enabled: zone.interactive
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.sp(10)

                    // Wrist pattern — the word as a band-tinted chip, never bare text.
                    PpStateChip {
                        visible: zone._pattern.length > 0
                        caption: zone._blended ? qsTr("Pattern · blended") : qsTr("Pattern")
                        state_:  zone._pattern
                        band:    ""      // resemblance is not RAG-scored — neutral tint
                        emphasis: true
                    }

                    // Tempo ratio — marker in the ideal band. Absent with no producer.
                    ColumnLayout {
                        visible: zone._hasTempo
                        Layout.fillWidth: true
                        spacing: Theme.sp(2)
                        Text {
                            text: qsTr("TEMPO")
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingMicro
                            color: Theme.colorText3
                        }
                        PpRangeBar {
                            Layout.fillWidth: true
                            value:   zone._tempoSample ? zone._tempoSample.value : 0
                            band:    zone._tempoSample ? zone._tempoSample.band : ""
                            unit:    zone._tempo ? zone._tempo.unit : ""
                            greenLo: zone._tempoCorridor ? zone._tempoCorridor.greenLo : 0
                            greenHi: zone._tempoCorridor ? zone._tempoCorridor.greenHi : 0
                            amberLo: zone._tempoCorridor ? zone._tempoCorridor.amberLo : 0
                            amberHi: zone._tempoCorridor ? zone._tempoCorridor.amberHi : 0
                        }
                    }
                }
            }

            // Priority fix — the top fault, its drill, and what it cost. Click seeks
            // the replay to the phase the fault was found at.
            MiniCard {
                visible: zone._fault !== null
                Layout.fillWidth: true
                edgeColor: Theme.colorRagFault
                tag: zone._fault && zone._fault.pointsLost > 0
                     ? qsTr("Priority fix · −%1").arg(zone._fault.pointsLost)
                     : qsTr("Priority fix")
                title: zone._fault ? zone._fault.name : ""
                body: zone._fault ? zone._fault.coaching : ""
                seekUs: zone._fault && zone._fault.seekUs !== undefined ? zone._fault.seekUs : -1
            }

            // One protected strength.
            MiniCard {
                visible: zone._strength !== null
                Layout.fillWidth: true
                edgeColor: Theme.colorRagGood
                tag: qsTr("Keep")
                title: zone._strength ? zone._strength.name : ""
                body: zone._strength ? zone._strength.protect : ""
                seekUs: zone._strength && zone._strength.seekUs !== undefined ? zone._strength.seekUs : -1
            }
        }
    }

    // A tagged mini-card with a coloured left edge; click seeks the replay.
    component MiniCard: Rectangle {
        property color edgeColor: Theme.colorRagNone
        property string tag: ""
        property string title: ""
        property string body: ""
        property real seekUs: -1

        implicitHeight: mcCol.implicitHeight + Theme.sp(14)
        radius: Theme.radius
        color: mcMa.containsMouse ? Theme.colorBg2 : Theme.colorBg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.sp(3); radius: Theme.radius; color: edgeColor
        }

        ColumnLayout {
            id: mcCol
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(12); rightMargin: Theme.sp(10); topMargin: Theme.sp(7) }
            spacing: Theme.sp(2)
            Text {
                text: tag
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: edgeColor
            }
            Text {
                Layout.fillWidth: true
                text: title
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Font.Medium
                color: Theme.colorText
            }
            Text {
                Layout.fillWidth: true
                visible: body.length > 0
                text: body
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }
        }
        PpPressable {
            id: mcMa
            hoverScale: 1.0
            enabled: zone.interactive && seekUs >= 0
            onClicked: if (seekUs >= 0) shotReplay.seekToUs(seekUs)
        }
    }
}
