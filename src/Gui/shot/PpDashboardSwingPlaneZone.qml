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

// Swing plane zone — the scorecard read for the T1-T8 idealised-swing comparator
// (dark behind swingref.enabled=false today). Where the Motion zone already renders
// this feature's three TimeSeries (shaft/lag/hub delta, usedBy dashboard:motion) as
// full band rails, this card surfaces its seven "one number per segment" scalars
// (usedBy scorecard:swingplane) as compact value chips — RMS-per-segment, lag
// retention, forward-lean at impact, tempo delta and the fit-quality residual.
//
// All seven ride the same degenerate-MetricSeries shape as tempoRatio (empty curve,
// one phaseSample) — see PpDashboardVerdictZone's _tempoSample. Each key's phase is
// a FIXED producer contract (SwingRefStage's pushScalar, wrist_analyzer.cpp), the
// same "hardcode the known phase" idiom Setup (Address) and Verdict (tempo @Impact)
// already use — not a per-shot value, so reading it from the catalogue would need
// an extra descriptor() round-trip for no benefit.
//
// Only refLeanDeltaP7 explicitly "shares the same sign convention as the full trace"
// (manifest howToRead) — positive = measured shaft steeper than reference — so only
// its value is tinted with the diverging steep/shallow scale (identical formula to
// PpCameraFrame._deltaColor, the reference-model colour-mapped trace, WP5a/T8). The
// three RMS metrics and the projection residual are always ≥0 (magnitudes, no
// direction); lag retention is explicitly "a magnitude, not a signed delta"; tempo
// delta is signed but against a fixed 3:1 constant, not a plane-steepness read, so it
// gets a +/- prefix but plain (non-tinted) ink. The legend card-wide explains the one
// colour scale that DOES appear here (and, in the video overlay, elsewhere) —
// T8 flagged that scale as published with no key.
//
// The metric set is SWING-AGNOSTIC like every other zone: pinned list, else every
// live "Swing plane" scalar used by this card. A configured metric this swing has no
// sample for holds its chip, muted, rather than dropping — same reasoning as Setup/
// Motion's ghost tiles. The whole card collapses to one muted row (never zero
// height) when nothing in the group has a value — matching the Sequence zone's
// "hold place, don't vanish" precedent, since a dashboard preset is a fixed template
// that must render ANY swing, including the (today, universal) case where the
// reference feature is off.

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

    // Canonical manifest order (metric_catalogue_manifest.cpp) — used both as the
    // default display order and to order any pinned subset consistently.
    readonly property var _order_: ["refRmsBackswing", "refRmsDownswing", "refExitDelta",
                                     "refLagRetention", "refLeanDeltaP7", "refTempoDelta",
                                     "refProjResidual"]
    function _order(key) {
        var i = _order_.indexOf(key)
        return i < 0 ? 99 : i
    }

    // Fixed per-key phase — the SwingRefStage producer's contract (wrist_analyzer.cpp
    // pushScalar), not a per-shot value: refRmsBackswing@Top(2), refRmsDownswing@Impact(5),
    // refExitDelta@ShaftParallelThrough(14), refLagRetention@Delivery(9),
    // refLeanDeltaP7@Impact(5), refTempoDelta@Impact(5), refProjResidual@Address(0).
    readonly property var _phaseByKey: ({
        refRmsBackswing: 2, refRmsDownswing: 5, refExitDelta: 14,
        refLagRetention: 9, refLeanDeltaP7: 5, refTempoDelta: 5, refProjResidual: 0
    })

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
    function _fmt(v) {
        var a = Math.abs(v)
        return a >= 100 ? String(Math.round(v)) : String(Math.round(v * 10) / 10)
    }

    // Diverging steep/shallow colour — identical formula to PpCameraFrame._deltaColor
    // (the reference-model colour-mapped measured trace, WP5a/T8): clamp at
    // ±_deltaRangeDeg for full saturation (a display-only range, not an analysis
    // parameter), lerp the neutral zero-delta ink toward gradientWarm (steeper) or
    // gradientCool (shallower). Kept in lock-step with that function deliberately —
    // one colour language for "steep vs shallow" across the whole app.
    readonly property real _deltaRangeDeg: 10.0
    function _lerpColor(c0, c1, t) {
        t = Math.max(0, Math.min(1, t))
        return Qt.rgba(c0.r + (c1.r - c0.r) * t, c0.g + (c1.g - c0.g) * t,
                       c0.b + (c1.b - c0.b) * t, c0.a + (c1.a - c0.a) * t)
    }
    function _deltaColor(deg) {
        var t = Math.max(-1, Math.min(1, deg / zone._deltaRangeDeg))
        return t >= 0 ? zone._lerpColor(Theme.colorText3, Theme.gradientWarm, t)
                      : zone._lerpColor(Theme.colorText3, Theme.gradientCool, -t)
    }

    // The zone's CONFIGURED metric set, each joined to the current swing. Queried
    // WITHOUT availableOnly — the config is a swing-agnostic template. The pool is
    // the "Swing plane" group's Summary/PointInTime rows whose usedBy names this
    // card (scorecard:swingplane) — deliberately excludes the group's own three
    // TimeSeries (dashboard:motion's) and the dark DTL-only planned rows (which
    // carry no usedBy at all), so a group-only filter can't leak them in here.
    function _build() {
        var out = []
        if (!catalog) return out

        var cat = catalog.query({ type: "summary" }, {}).concat(
                  catalog.query({ type: "pointInTime" }, {}))
        var byKey = {}
        for (var i = 0; i < cat.length; ++i) {
            var row = cat[i]
            if (row.group !== "Swing plane") continue
            var desc = catalog.descriptor(row.key, {})
            if (!desc || !desc.usedBy || desc.usedBy.indexOf("scorecard:swingplane") < 0) continue
            byKey[row.key] = row
        }

        var pins = pinnedMetrics || []
        var keys = []
        if (pins.length > 0) {
            keys = pins.slice()
        } else {
            for (var k in byKey) if (!byKey[k].planned) keys.push(k)
        }
        keys.sort(function (a, b) { return zone._order(a) - zone._order(b) })

        for (var m = 0; m < keys.length; ++m) {
            var r = byKey[keys[m]]
            if (!r) continue                                    // not in this pool at all
            var s = zone._seriesFor(keys[m])
            var phase = zone._phaseByKey[keys[m]]
            var samp = (s && phase !== undefined) ? zone._sampleAt(s, phase) : null
            out.push({ key: keys[m],
                       label: (r.shortLabel && r.shortLabel.length) ? r.shortLabel : r.label,
                       unit: r.unit,
                       has: samp !== null,
                       // "soon" = planned (no producer yet); "NA" = live metric this
                       // swing simply didn't measure (today: always this — the
                       // comparator is dark behind swingref.enabled=false).
                       emptyLabel: r.planned === true ? qsTr("soon") : qsTr("NA"),
                       value: samp ? samp.value : 0,
                       // Signed read: only refLeanDeltaP7 (plane steep/shallow) and
                       // refTempoDelta (vs the fixed 3:1 reference) can go negative;
                       // the three RMS metrics, lag retention and the projection
                       // residual are all non-negative magnitudes (see file header).
                       signed_: keys[m] === "refLeanDeltaP7" || keys[m] === "refTempoDelta",
                       deltaColored: keys[m] === "refLeanDeltaP7" })
        }
        return out
    }

    readonly property var _tiles: _build()
    readonly property bool _hasAnyValue: {
        for (var i = 0; i < _tiles.length; ++i) if (_tiles[i].has) return true
        return false
    }
    // Never zero-height (unlike Verdict/Setup/Motion which vanish with no content):
    // a swing-agnostic template holds its place, matching the Sequence zone's own
    // "NA" placeholder precedent for a group with no producer this swing.
    implicitHeight: card.implicitHeight
    visible: true

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
            spacing: Theme.sp(8)

            Text {
                text: qsTr("SWING PLANE")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            // Collapsed state — the whole group is absent for this shot (feature
            // off, no face-on club track, or the reference fit failed). One muted
            // row rather than an empty grid of "NA" chips.
            Text {
                visible: !zone._hasAnyValue
                Layout.fillWidth: true
                text: qsTr("Swing plane — no reference data")
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }

            Flow {
                visible: zone._hasAnyValue
                Layout.fillWidth: true
                spacing: Theme.sp(8)
                Repeater {
                    model: zone._tiles
                    delegate: ValueChip { required property var modelData; data_: modelData }
                }
            }

            // ── Steep/shallow legend ─────────────────────────────────────────────
            // The colour key for the one diverging scale used on this card (and,
            // via the same _deltaColor formula, the video overlay's reference
            // chips/trace) — T8 shipped the scale without a key; this is it.
            RowLayout {
                visible: zone._hasAnyValue
                Layout.fillWidth: true
                Layout.topMargin: Theme.sp(2)
                spacing: Theme.sp(6)

                Text {
                    text: qsTr("shallow")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.sp(4)
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.gradientCool }
                        GradientStop { position: 0.5; color: Theme.colorText3 }
                        GradientStop { position: 1.0; color: Theme.gradientWarm }
                    }
                }
                Text {
                    text: qsTr("steep")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }
            }
        }
    }

    // One metric = one compact chip: label above, signed/unit value below. No
    // corridor bar (the group carries no numeric bands — every descriptor here is
    // heuristic-only) and no RAG edge — the diverging tint (where it applies) IS
    // the tile's read.
    component ValueChip: Rectangle {
        property var data_: ({})

        implicitWidth: vCol.implicitWidth + Theme.sp(16)
        implicitHeight: vCol.implicitHeight + Theme.sp(12)
        radius: Theme.radius
        opacity: data_.has === true ? 1.0 : 0.6
        color: chipHover.hovered && zone.interactive ? Theme.colorBg2 : Theme.colorBg
        border.width: Theme.borderWidth
        border.color: chipHover.hovered && zone.interactive ? Theme.colorBorderMid : Theme.colorBorder
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        ColumnLayout {
            id: vCol
            anchors.centerIn: parent
            spacing: Theme.sp(3)

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: (data_.label || "").toUpperCase()
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.sp(2)
                Text {
                    text: {
                        if (data_.has !== true) return data_.emptyLabel || qsTr("NA")
                        var v = data_.value
                        var sign = (data_.signed_ === true && v >= 0) ? "+" : ""
                        return sign + zone._fmt(v)
                    }
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzData
                    font.weight: Font.Light
                    font.italic: data_.has !== true && data_.emptyLabel === qsTr("soon")
                    color: {
                        if (data_.has !== true) return Theme.colorText3
                        if (data_.deltaColored === true) return zone._deltaColor(data_.value)
                        return Theme.colorText
                    }
                }
                // Unit — omitted for refTempoDelta (its unit is the empty string,
                // a ratio delta with no dimension) as well as whenever there's no
                // value to attach it to.
                Text {
                    visible: data_.has === true && (data_.unit || "").length > 0
                    text: data_.unit || ""
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }
            }
        }

        HoverHandler { id: chipHover; enabled: zone.interactive }
    }
}
