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

// Setup zone — the address-position checklist as a TILE GRID, one visual primitive
// per metric. Scalars (stance width, foot flare, low point) render as a PpRangeBar
// mini-bar: a marker sitting in its green/amber corridor, which answers "in bounds?"
// at a glance where a number only answers "what". Alignment angles render as a
// PpOrientationGlyph — the coaching read there is a WORD (square / open / closed),
// and the glyph draws the geometry rather than printing the word alone.
//
// The metric set is SWING-AGNOSTIC: the zone shows its CONFIGURED metrics (the pinned
// list, else every live point-in-time metric) and reads each at Address. A configured
// metric this swing has no sample for keeps its tile, rendered in the primitive's
// neutral no-value state — an empty corridor track, or the glyph's unknown form —
// labelled "NA" (live, not measured here) or "soon" (planned, no producer yet). A
// fixed template is what keeps the layout stable across swings, and the placeholders
// double as a visible ledger of remaining pipeline work.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: zone

    property var catalog: null
    property var shotCtx: ({})
    property var detail: ({})
    property int sessionType: -1
    property var pinnedMetrics: []
    property bool interactive: false

    signal metricActivated(string key)

    ChartMetrics { id: cm }

    readonly property int _phaseAddress: 0   // Phase::Address

    readonly property var _rows: _build()
    readonly property bool _hasContent: _rows.length > 0
    implicitHeight: _hasContent ? card.implicitHeight : 0
    visible: _hasContent

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
    function _bandColor(b) {
        return b === "green"  ? Theme.colorRagGood
             : b === "yellow" ? Theme.colorRagWatch
             : b === "red"    ? Theme.colorRagFault
             :                  Theme.colorRagNone
    }

    // The zone's CONFIGURED tiles, each joined to the current swing's Address sample.
    // Queried WITHOUT availableOnly — the config is a swing-agnostic template, so a
    // metric holds its tile whether or not this rig could measure it.
    function _build() {
        var out = []
        if (!catalog) return out
        var cat = catalog.query({ type: "pointInTime" }, {})
        var byKey = {}
        for (var i = 0; i < cat.length; ++i) byKey[cat[i].key] = cat[i]

        var pins = pinnedMetrics || []
        var keys = []
        if (pins.length > 0) {
            keys = pins
        } else {
            for (var j = 0; j < cat.length; ++j) if (!cat[j].planned) keys.push(cat[j].key)
        }

        for (var k = 0; k < keys.length; ++k) {
            var row = byKey[keys[k]]
            if (!row) continue                                  // not in the catalogue at all
            var s = _seriesFor(keys[k])
            var samp = s ? _sampleAt(s, _phaseAddress) : null

            var desc = catalog.descriptor(keys[k], shotCtx)
            var cor  = _corridorAt(desc, _phaseAddress)
            // Alignment angles read as an orientation word; everything else as a bar.
            // The glyph carries its own unknown state, so it still serves an NA tile.
            var glyph = (row.group === "Alignment" || keys[k] === "toeLineAngle")
            out.push({ key: keys[k],
                       label: (row.shortLabel && row.shortLabel.length) ? row.shortLabel : row.label,
                       unit: row.unit,
                       has: samp !== null,
                       // "soon" = planned (no producer yet); "NA" = live metric this
                       // swing simply didn't measure. Distinct on purpose.
                       emptyLabel: row.planned === true ? qsTr("soon") : qsTr("NA"),
                       value: samp ? samp.value : 0,
                       band: samp ? samp.band : "",
                       glyph: glyph,
                       orientation: (glyph && samp && cor)
                                    ? cm.orientationLabel(samp.value, cor.greenLo, cor.greenHi) : "",
                       greenLo: cor ? cor.greenLo : 0, greenHi: cor ? cor.greenHi : 0,
                       amberLo: cor ? cor.amberLo : 0, amberHi: cor ? cor.amberHi : 0,
                       hasCorridor: cor !== null })
        }
        return out
    }

    Rectangle {
        id: card
        width: zone.width
        implicitHeight: col.implicitHeight + Theme.sp(22)
        color: Theme.colorSurface
        radius: Theme.radiusLg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(11); rightMargin: Theme.sp(11); topMargin: Theme.sp(9) }
            spacing: Theme.sp(9)

            Text {
                text: qsTr("SETUP · ADDRESS")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.sp(10)
                Repeater {
                    model: zone._rows
                    delegate: SetupTile { required property var modelData; data_: modelData }
                }
            }
        }
    }

    // One metric = one tile: RAG dot + label, then the primitive.
    component SetupTile: Rectangle {
        property var data_: ({})

        // Sized for a glance, not a spreadsheet: wide enough that the corridor bar
        // has room to place its marker meaningfully and the label doesn't elide.
        width: Theme.sp(210)
        implicitHeight: tCol.implicitHeight + Theme.sp(20)
        radius: Theme.radius
        opacity: data_.has === true ? 1.0 : 0.6
        color: tileHover.hovered && zone.interactive ? Theme.colorBg2 : Theme.colorBg
        border.width: Theme.borderWidth
        border.color: tileHover.hovered && zone.interactive ? Theme.colorBorderMid : Theme.colorBorder
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        ColumnLayout {
            id: tCol
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(10); rightMargin: Theme.sp(10); topMargin: Theme.sp(8) }
            spacing: Theme.sp(5)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(6)
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Theme.sp(7); implicitHeight: Theme.sp(7); radius: width / 2
                    color: zone._bandColor(data_.band)
                    opacity: data_.has === true ? 1.0 : 0.5
                }
                Text {
                    Layout.fillWidth: true
                    text: (data_.label || "").toUpperCase()
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                    font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                    elide: Text.ElideRight
                }
            }

            // Categorical alignment → glyph; scalar → corridor mini-bar with its value.
            PpOrientationGlyph {
                visible: data_.glyph === true
                Layout.alignment: Qt.AlignHCenter
                orientation: data_.orientation || ""
                band: data_.band || ""
            }

            ColumnLayout {
                visible: data_.glyph !== true
                Layout.fillWidth: true
                spacing: Theme.sp(3)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        // Value, else "NA" (live metric, no value this swing) or
                        // "soon" (planned — no producer yet).
                        text: {
                            if (data_.has !== true) return data_.emptyLabel || qsTr("NA")
                            var v = data_.value
                            var a = Math.abs(v)
                            return a >= 100 ? String(Math.round(v)) : String(Math.round(v * 10) / 10)
                        }
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzHeading
                        font.weight: Font.DemiBold
                        font.italic: data_.has !== true && data_.emptyLabel === qsTr("soon")
                        color: data_.has === true ? zone._bandColor(data_.band) : Theme.colorText3
                    }
                    Text {
                        Layout.alignment: Qt.AlignBottom
                        Layout.bottomMargin: Theme.sp(2)
                        visible: data_.has === true
                        text: data_.unit || ""
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                        color: Theme.colorText3
                    }
                    Item { Layout.fillWidth: true }
                }
                PpRangeBar {
                    Layout.fillWidth: true
                    compact: true
                    // hasValue false ⇒ the neutral corridor track with no marker: the
                    // tile still reads as a bar, it just has nothing to place on it.
                    hasValue: data_.has === true
                    value: data_.value
                    band: data_.band || ""
                    greenLo: data_.greenLo; greenHi: data_.greenHi
                    amberLo: data_.amberLo; amberHi: data_.amberHi
                }
            }
        }

        // Click → the metric's catalogue page. There is deliberately NO hover
        // tooltip: a Controls ToolTip is unthemed chrome, unbounded in width, and
        // set in a size nobody can read from across the room — the exact failure a
        // wall surface cannot afford. MetricDetail is the readable home for a
        // metric's narrative, and one click away.
        HoverHandler {
            id: tileHover
            enabled: zone.interactive
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: zone.interactive
            onTapped: zone.metricActivated(data_.key)
        }
    }
}
