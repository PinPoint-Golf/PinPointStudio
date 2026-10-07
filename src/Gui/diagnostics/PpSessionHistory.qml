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

// ACROSS SESSIONS: one column per session of the same golfer, oldest on the left, each holding
// that session's readings of this condition's measure as dots, its interquartile range as a bar
// and its median as a tick — all against this condition's bands AS THEY STAND TODAY, so every
// column is measured with one ruler. This session's column is the highlighted one.
//
// It is the question a finished session raises and the tick run cannot answer: "46 of 50 fired"
// is the same sentence on 4 July and on 7 October, and the medians underneath it went 22 → 19 →
// 13. Review only (SessionDiagnosticsModel::conditionDetail publishes `history` only while
// reviewing); the other sessions are read off their own diagnostics.json, off the GUI thread,
// and never written.
//
// Dots are NEUTRAL here, not fired/clean: those verdicts were graded against whatever the norm
// was on the day, and colouring last month's dots with them would mix two rulers in one picture.
// Where each dot sits against today's bands is the comparison.
//
// Every position is the model's (historyFor: fractions of one shared axis, a beeswarm row per
// dot); this file scales and paints.

import QtQuick
import PinPointStudio

Item {
    id: root

    property var history: null
    property real fit: 1.0

    objectName: "sdSessionHistory"

    function px(n) { return Math.round(n * Theme.fontScale * root.fit) }
    readonly property int tzCaption: Math.max(1, Math.round(Theme.sp(8) * fit))
    readonly property int tzMicro:   Math.max(1, Math.round(Theme.fontSzMicro * fit))

    readonly property var _cols:   history ? (history.columns || []) : []
    readonly property var _bands:  history ? (history.bands || []) : []
    readonly property var _faults: history ? (history.faultLines || []) : []
    readonly property int count: _cols.length

    function bandColor(grade) {
        if (grade === "ideal")  return Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.18)
        if (grade === "good")   return Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.08)
        if (grade === "watch")  return Qt.rgba(Theme.colorAttention.r, Theme.colorAttention.g, Theme.colorAttention.b, 0.18)
        if (grade === "action") return Qt.rgba(Theme.colorError.r, Theme.colorError.g, Theme.colorError.b, 0.14)
        return "transparent"
    }

    readonly property int _labelsH: tzCaption * 2 + px(4)
    readonly property int _captionH: caption.implicitHeight + px(2)
    readonly property real _plotH: Math.max(px(20), height - _labelsH - _captionH)
    // THE Y LABELS, on the same terms as the value run's: round values on the axis's decimals in a
    // left gutter the columns never enter (historyFor: yTicks); the caption carries the unit.
    readonly property var _ticks: history ? (history.yTicks || []) : []
    readonly property string _longestTick: _ticks.reduce((a, t) => (t.text || "").length > a.length ? (t.text || "") : a, "")
    TextMetrics { id: tickMetrics; font.family: Theme.fontData; font.pixelSize: root.tzCaption; text: root._longestTick }
    readonly property real _gutter: _ticks.length > 0 ? Math.ceil(tickMetrics.advanceWidth) + px(8) : 0
    readonly property real _pw: Math.max(1, width - _gutter)
    readonly property real _colW: count > 0 ? _pw / count : _pw
    readonly property real _dotD: px(4)
    function _y(f) { return root._plotH - f * root._plotH }

    implicitHeight: px(150)
    visible: !!history && (count > 0 || (history.caption || "") !== "")

    Item {
        id: plot
        x: root._gutter; width: root._pw
        height: root._plotH

        Repeater {
            model: root._bands
            Rectangle {
                required property var modelData
                objectName: "sdHistoryBand"
                y: root._y(modelData.f1 || 0)
                width: plot.width
                height: Math.max(0, root._y(modelData.f0 || 0) - root._y(modelData.f1 || 0))
                color: root.bandColor(modelData.grade || "")
            }
        }
        Repeater {
            model: root._faults
            Rectangle {
                required property var modelData
                y: root._y(modelData.f || 0)
                width: plot.width
                height: 1
                color: Theme.colorError
                opacity: 0.7
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.top
                    text: parent.modelData.text || ""
                    font.family: Theme.fontData
                    font.pixelSize: root.tzCaption
                    color: Theme.colorError
                }
            }
        }

        Repeater {
            model: root._cols
            Item {
                id: col
                required property var modelData
                required property int index
                objectName: "sdHistoryColumn"
                readonly property bool current: modelData.current === true
                readonly property bool any: (modelData.n || 0) > 0
                x: index * root._colW
                width: root._colW
                height: plot.height

                // This session, picked out by a wash rather than by a colour on its dots.
                Rectangle {
                    anchors.fill: parent
                    visible: col.current
                    color: Theme.colorAccent
                    opacity: 0.08
                }
                // The dots, jittered sideways by their beeswarm row, left of the bar.
                Repeater {
                    model: col.modelData.values || []
                    Rectangle {
                        required property var modelData
                        objectName: "sdHistoryDot"
                        readonly property int  st: modelData.stack || 0
                        readonly property real off: ((st + 1) >> 1) * (st % 2 === 1 ? -1 : 1)
                                                    * root._dotD * 0.8
                        width: root._dotD
                        height: root._dotD
                        radius: width / 2
                        x: col.width * 0.38 + off - width / 2
                        y: root._y(modelData.f || 0) - height / 2
                        color: col.current ? Theme.colorAccent : Theme.colorText2
                        opacity: col.current ? 0.75 : 0.45
                    }
                }
                // The interquartile bar and the median tick, right of the dots.
                Rectangle {
                    objectName: "sdHistoryIqr"
                    visible: col.any
                    x: col.width * 0.70 - width / 2
                    width: root.px(5)
                    y: root._y(col.modelData.fQ3 || 0)
                    height: Math.max(1, root._y(col.modelData.fQ1 || 0) - root._y(col.modelData.fQ3 || 0))
                    radius: width / 2
                    color: col.current ? Theme.colorAccent : Theme.colorText2
                    opacity: 0.45
                }
                Rectangle {
                    objectName: "sdHistoryMedian"
                    visible: col.any
                    x: col.width * 0.70 - width / 2
                    width: root.px(14)
                    height: Math.max(2, root.px(2))
                    y: root._y(col.modelData.fMedian || 0) - height / 2
                    color: col.current ? Theme.colorAccent : Theme.colorText
                }
            }
        }
    }

    Repeater {
        model: root._ticks
        Item {
            required property var modelData
            objectName: "sdHistoryYTick"
            readonly property string text: modelData.text || ""
            readonly property real ty: root._y(modelData.f || 0)
            Rectangle {
                x: root._gutter
                y: parent.ty
                width: root._pw
                height: 1
                color: Theme.colorBorderMid
                opacity: 0.5
            }
            Text {
                text: parent.text
                width: root._gutter - root.px(5)
                horizontalAlignment: Text.AlignRight
                y: Math.max(0, Math.min(root._plotH - implicitHeight, parent.ty - implicitHeight / 2))
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText2
            }
        }
    }

    // ── each column's date and median ────────────────────────────────────────
    Repeater {
        model: root._cols
        Column {
            required property var modelData
            required property int index
            x: root._gutter + index * root._colW
            y: root._plotH + root.px(3)
            width: root._colW
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: parent.modelData.label || ""
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                font.bold: parent.modelData.current === true
                color: parent.modelData.current === true ? Theme.colorAccent : Theme.colorText2
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: parent.modelData.medianText || "-"
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText3
            }
        }
    }

    Text {
        id: caption
        objectName: "sdHistoryCaption"
        y: root._plotH + root._labelsH
        width: root.width
        text: root.history ? (root.history.caption || "") : ""
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        font.family: Theme.fontData
        font.pixelSize: root.tzMicro
        color: Theme.colorText2
    }
}
