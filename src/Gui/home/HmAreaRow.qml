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

// One area of "Your swing" (HmSwingAreas): a PpExpandRow on the area table's column template.
//
//   header   chevron · area · habits (a PpBandBar of the four counts and "N habits") · trend ·
//            biggest fault · work-ons ("1 active · 1 new")
//   detail   the area's items at Theme.itemIndent, one Theme.itemRowH row each: the condition, how
//            many swings it fired on of those that assessed it, the typical value with its unit and
//            corridor, the grade word, and an action — Practise on an active or easing work-on, a
//            "New" tag on a newly listed one, nothing otherwise; then "All of <area> →".
//
// `area` is one of WorkOnsController.areaItems (swing_areas.h AreaSummary, every string final):
//   { group, label, measurable, measured, habits, occasional, clean, unclear, habitTrend, hasTrend,
//     topFault, topFaultId, workOnsActive, workOnsNew, items: [{ conditionId, label, golfer, band,
//     share, fired, assessable, measureId, hasTypical, typical, corridorLo, corridorHi,
//     corridorShape, unit, grade, workOn, sessionsListed }] }
//
// An area the rig cannot measure says so in place of the bar, and does not open: no chevron, no
// press, no focus.
//
// Colour is never the only channel: the bar carries the count beside it, the trend its sign, the
// grade its word, the work-ons their words.
//
// The trend is habits gained (+) or cleared (−) since the newest substantive session; FEWER habits
// is better, so its tone is Theme.trendTone of the negated figure.
//
// workOnsActive counts every listed condition (active, easing AND new: swing_areas.h); the row says
// the new ones apart, so "active" is workOnsActive − workOnsNew.

import QtQuick
import QtQuick.Controls
import PinPointStudio

PpExpandRow {
    id: areaRow
    objectName: "areaRow"

    property var area: ({})

    signal practiseRequested(string conditionId, string label)
    signal allInAreaRequested(string group)

    // ── The words as drawn (what a probe reads) ─────────────────────────────
    readonly property string group:      areaRow.area && areaRow.area.group ? String(areaRow.area.group) : ""
    readonly property string areaLabel:  areaRow.area && areaRow.area.label ? String(areaRow.area.label) : ""
    readonly property bool   measurable: !!(areaRow.area && areaRow.area.measurable)
    readonly property var    items:      areaRow.area && areaRow.area.items ? areaRow.area.items : []
    readonly property int    habits:     areaRow.count("habits")
    readonly property string habitsText: areaRow.habits === 1 ? qsTr("1 habit") : qsTr("%1 habits").arg(areaRow.habits)
    readonly property bool   hasTrend:   !!(areaRow.area && areaRow.area.hasTrend)
    readonly property int    trend:      areaRow.count("habitTrend", true)
    readonly property string trendText:  !areaRow.hasTrend ? "–"
                                       : areaRow.trend > 0 ? "+" + areaRow.trend
                                       : areaRow.trend < 0 ? "−" + (-areaRow.trend)
                                       :                 "±0"
    readonly property string faultText:  areaRow.area && areaRow.area.topFault ? String(areaRow.area.topFault) : "—"
    readonly property int    newCount:    areaRow.count("workOnsNew")
    readonly property int    activeCount: Math.max(0, areaRow.count("workOnsActive") - areaRow.newCount)
    readonly property string activeText:  areaRow.activeCount > 0 ? qsTr("%1 active").arg(areaRow.activeCount) : ""
    readonly property string newText:     areaRow.newCount > 0 ? qsTr("%1 new").arg(areaRow.newCount) : ""
    readonly property string workOnsText: [areaRow.activeText, areaRow.newText].filter(s => s !== "").join(" · ")

    function count(key, signed) {
        const v = areaRow.area ? Number(areaRow.area[key]) : 0
        if (!isFinite(v)) return 0
        return signed ? Math.round(v) : Math.max(0, Math.round(v))
    }

    // ── Item figures ────────────────────────────────────────────────────────
    function figure(v) {
        const n = Number(v)
        return isFinite(n) ? n.toFixed(1).replace("-", "−") : ""
    }
    function withUnit(s, unit) {
        const u = unit ? String(unit) : ""
        if (u === "" || s === "") return s
        return (u === "°" || u === "%") ? s + u : s + " " + u
    }
    // The corridor the typical value was graded against: both edges, or the one a one-sided
    // corridor grades (a floor's low edge, a ceiling's high one).
    function corridorText(it) {
        const shape = it.corridorShape || ""
        if (shape === "twoSided") return areaRow.figure(it.corridorLo) + "–" + areaRow.figure(it.corridorHi)
        if (shape === "floor")    return "≥ " + areaRow.figure(it.corridorLo)
        if (shape === "ceiling")  return "≤ " + areaRow.figure(it.corridorHi)
        return ""
    }
    function typicalText(it) {
        if (!it || !it.hasTypical) return ""
        const v = areaRow.withUnit(areaRow.figure(it.typical), it.unit)
        const c = areaRow.corridorText(it)
        return c !== "" ? v + " · " + c : v
    }
    function practisable(it) { return it && (it.workOn === "active" || it.workOn === "easing") }

    // The item rows' own template, on the detail's width (PpCard.resolveColumns' rule: fixed
    // columns keep their width, fill columns share the rest by weight).
    // An item row asks for its template on its own width.
    readonly property int itemGap: Theme.gap(16)
    function itemLayout(w) {
        const cols = [{ fill: 1.2 }, { width: Theme.sp(56) }, { fill: 1.4 },
                      { width: Theme.sp(72) }, { width: Theme.sp(112), align: "right" }]
        let fixed = 0, weights = 0
        for (const c of cols) { if (c.fill) weights += c.fill; else fixed += c.width }
        const spare = Math.max(0, w - fixed - areaRow.itemGap * (cols.length - 1))
        const out = []
        let x = 0
        for (const c of cols) {
            const cw = c.fill ? Math.floor(spare * c.fill / weights) : c.width
            out.push({ x: x, width: cw, align: c.align || "left" })
            x += cw + areaRow.itemGap
        }
        return out
    }

    accessibleName: areaRow.measurable ? areaRow.areaLabel + ", " + areaRow.habitsText
                                   : areaRow.areaLabel + ", " + qsTr("not measured with this rig")
    // Not measurable: nothing to open.
    enabled:          areaRow.measurable
    activeFocusOnTab: areaRow.measurable
    detailIndent:     Theme.sp(Theme.itemIndent)
    detailRightInset: Theme.sp(Theme.rowPadH)

    // An area that cannot open shows no chevron.
    showChevron: areaRow.measurable

    // ── Header cells, in column order ───────────────────────────────────────
    Item { width: 1; height: 1 }                // the chevron's column

    Text {                                      // area
        objectName: "areaLabel"
        width: areaRow.headerRow.columnWidth(1)
        text:  areaRow.areaLabel
        elide: Text.ElideRight
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight:    Font.DemiBold
        color:          areaRow.measurable ? Theme.colorText : Theme.colorText3
    }

    Item {                                      // habits
        id: habitsCell
        objectName: "areaHabits"
        // Unmeasurable, the words run on over the trend and fault columns, which it leaves empty.
        width: areaRow.measurable ? areaRow.headerRow.columnWidth(2)
                              : areaRow.headerRow.columnX(4) + areaRow.headerRow.columnWidth(4) - areaRow.headerRow.columnX(2)
        height: Math.max(habitsWords.implicitHeight, Theme.sp(Theme.bandBarH))
        readonly property int wordsW: Math.max(habitsWords.implicitWidth, Theme.sp(64))
        PpBandBar {
            objectName: "areaBandBar"
            visible: areaRow.measurable
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, habitsCell.width - habitsCell.wordsW - Theme.sp(12))
            segments: [{ count: areaRow.count("habits"),     tone: Theme.bandTone("habit") },
                       { count: areaRow.count("occasional"), tone: Theme.bandTone("occasional") },
                       { count: areaRow.count("clean"),      tone: Theme.bandTone("clean") },
                       { count: areaRow.count("unclear"),    tone: Theme.bandTone("unclear") }]
        }
        Text {
            id: habitsWords
            objectName: "areaHabitsText"
            anchors.verticalCenter: parent.verticalCenter
            x: areaRow.measurable ? habitsCell.width - habitsCell.wordsW : 0
            width: areaRow.measurable ? habitsCell.wordsW : habitsCell.width
            elide: Text.ElideRight
            text:  areaRow.measurable ? areaRow.habitsText : qsTr("not measured with this rig")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          areaRow.measurable ? Theme.colorText2 : Theme.colorText3
        }
    }

    Text {                                      // trend
        objectName: "areaTrend"
        visible: areaRow.measurable
        text:  areaRow.trendText
        font.family:    Theme.fontData
        font.pixelSize: Theme.fontSzBody2
        color: areaRow.hasTrend ? Theme.trendTone(-areaRow.trend) : Theme.colorText3
    }

    Text {                                      // biggest fault
        objectName: "areaFault"
        visible: areaRow.measurable
        width: areaRow.headerRow.columnWidth(4)
        text:  areaRow.faultText
        elide: Text.ElideRight
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight:    Theme.fontBodyWeight
        color: areaRow.area && areaRow.area.topFault ? Theme.colorText : Theme.colorText3
    }

    Row {                                       // work-ons
        objectName: "areaWorkOns"
        visible: areaRow.measurable && areaRow.workOnsText !== ""
        spacing: 0
        Text {
            visible: areaRow.activeText !== ""
            text:  areaRow.activeText
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Font.DemiBold
            color: Theme.colorAccent
        }
        Text {
            visible: areaRow.activeText !== "" && areaRow.newText !== ""
            text:  " · "
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color: Theme.colorText3
        }
        Text {
            visible: areaRow.newText !== ""
            text:  areaRow.newText
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Font.DemiBold
            color: Theme.colorAttention
        }
    }

    // ── Detail: the items, then the way into the whole area ─────────────────
    detail: Component {
        Column {

            Repeater {
                model: areaRow.items
                PpTableRow {
                    id: itemRow
                    objectName: "areaItemRow"
                    required property var modelData
                    // The words as drawn.
                    readonly property string conditionId: itemRow.modelData.conditionId || ""
                    readonly property string label:       itemRow.modelData.label || ""
                    readonly property string countText:   (itemRow.modelData.fired || 0) + "/" + (itemRow.modelData.assessable || 0)
                    readonly property string typicalText: areaRow.typicalText(itemRow.modelData)
                    readonly property string gradeWord:   Theme.corridorWord(itemRow.modelData.grade || "")

                    width: parent ? parent.width : 0
                    implicitHeight: Theme.sp(Theme.itemRowH)
                    padH: 0
                    topRule: true
                    hoverEnabled: false
                    layout: areaRow.itemLayout(itemRow.width)

                    Text {                      // the condition; the golfer's phrase on hover
                        width: itemRow.columnWidth(0)
                        text:  itemRow.label
                        elide: Text.ElideRight
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        font.weight:    Theme.fontBodyWeight
                        color: Theme.colorText
                        HoverHandler { id: labelHover }
                        ToolTip.visible: labelHover.hovered && (itemRow.modelData.golfer || "") !== ""
                        ToolTip.text:    itemRow.modelData.golfer || ""
                        ToolTip.delay:   300
                    }
                    Text {                      // fired / assessable
                        text: itemRow.countText
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText2
                    }
                    Text {                      // typical · corridor
                        objectName: "areaItemTypical"
                        width: itemRow.columnWidth(2)
                        text:  itemRow.typicalText
                        elide: Text.ElideRight
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText2
                    }
                    Text {                      // the grade, in its word and its colour
                        objectName: "areaItemGrade"
                        text:  itemRow.gradeWord
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Font.DemiBold
                        color: Theme.corridorTone(itemRow.modelData.grade || "")
                    }
                    Item {                      // the action
                        width:  itemRow.columnWidth(4)
                        height: itemRow.height
                        PpButton {
                            objectName: "areaPractise"
                            visible: areaRow.practisable(itemRow.modelData)
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            glyph: "▶"
                            label: qsTr("Practise")
                            onClicked: areaRow.practiseRequested(itemRow.conditionId, itemRow.label)
                        }
                        PpChip {
                            objectName: "areaNewTag"
                            visible: itemRow.modelData.workOn === "new"
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            text: qsTr("New")
                            tone: Theme.colorAttention
                        }
                    }
                }
            }

            Item {
                width:  parent ? parent.width : 0
                height: Theme.sp(Theme.itemRowH)
                PpLink {
                    objectName: "areaAllLink"
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All of %1 →").arg(areaRow.areaLabel.toLowerCase())
                    onClicked: areaRow.allInAreaRequested(areaRow.group)
                }
            }
        }
    }
}
