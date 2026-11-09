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
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio
import "../typeset/figure_format.js" as Figures

// Per-athlete club records — a card group for ScreenAthleteForm (edit mode).
// Records live under athletes/<uuid>/clubs keyed by the canonical club name
// (the same id markup writes to truth.json meta.club). Club length feeds the
// shaft-tracker search radius; the band fields describe retroreflective tape
// (optional, high value in dark rooms) for the instrumented-club pipeline —
// docs/validation/instrumented_club_protocol.md. Band positions are the
// CENTRES of each tape band, measured from the butt end of the grip, in mm.
//
// Unlike the scalar athlete fields (buffered until Save), club edits commit
// IMMEDIATELY via athleteController — the section is only shown in edit mode.
//
// Above the club tabs, YOUR DISTANCES: a table of carry, a row per club (docs/design/
// shot_outcomes_design.md §7.5); inside the selected club's card, the same club in full. Read from
// the stored reduction, not workOns, because the form can edit a golfer who is not the current one.
// The band and the session marks are PpBoxPlot with its five numbers collapsed to average ± spread,
// which is not a quartile box: the words say so beside each.
PpGroupCard {
    id: root

    title:   qsTr("Clubs")
    hint:    qsTr("Lengths and retro-band positions — saved as you edit")
    spacing: Theme.gap(14)

    property string athleteUuid: ""

    // Selected tab. Kept valid against the configured list: first club by
    // default; follows removals to a surviving neighbour.
    property string selectedClubId: ""
    onConfiguredIdsChanged: _fixSelection()
    Component.onCompleted:  _fixSelection()
    function _fixSelection() {
        if (configuredIds.indexOf(selectedClubId) < 0)
            selectedClubId = configuredIds.length > 0 ? configuredIds[0] : ""
    }

    // The stored distances for this golfer, read again when a catch-up lands or the golfer changes.
    property int _distancesRevision: 0
    Connections {
        target: athleteController
        function onClubDistancesChanged() { root._distancesRevision++ }
    }
    readonly property var distances: root._distancesRevision >= 0
                                     ? athleteController.clubDistances(root.athleteUuid) : ({})
    readonly property var distanceItems: root.distances.items || []
    readonly property var selectedDistance: {
        for (const it of root.distanceItems)
            if (it.club === root.selectedClubId) return it
        return null
    }
    // One axis for every row, at whole tens so the two ends read cleanly.
    readonly property var distanceAxis: root._axis(root.distanceItems.map(it => root._box(it.carryMean, it.carrySd)), 10)

    function _fin(v) { return typeof v === "number" && isFinite(v) }
    // Average ± spread, as PpBoxPlot's five numbers. All five finite or the average vanishes, so a
    // mean with no spread gives the mean five times; no mean, no box.
    function _box(mean, sd) {
        if (!root._fin(mean)) return null
        const s = root._fin(sd) ? sd : 0
        return { lo: mean - s, q1: mean - s, med: mean, q3: mean + s, hi: mean + s }
    }
    function _axis(boxes, step) {
        let lo = Infinity, hi = -Infinity
        for (const b of boxes) {
            if (!b) continue
            lo = Math.min(lo, b.lo)
            hi = Math.max(hi, b.hi)
        }
        if (!isFinite(lo)) return { lo: 0, hi: step }
        lo = Math.floor(lo / step) * step
        hi = Math.ceil(hi / step) * step
        return { lo: lo, hi: hi > lo ? hi : lo + step }
    }
    function _figure(v, unit, dec)  { return root._fin(v) ? Figures.withUnit(Figures.figure(v, dec), unit) : "—" }
    function _spread(sd, unit, dec) { return root._fin(sd) ? "± " + Figures.withUnit(Figures.figure(sd, dec), unit) : "—" }
    function _meanSd(mean, sd, unit, dec) {
        if (!root._fin(mean)) return "—"
        return Figures.figure(mean, dec) + " ± " + Figures.withUnit(root._fin(sd) ? Figures.figure(sd, dec) : "—", unit)
    }
    // The six figures of the selected club.
    function _distanceCells(it) {
        return [{ key: qsTr("Carry"),      val: it.carryText || "—" },
                { key: qsTr("Total"),      val: root._meanSd(it.totalMean, it.totalSd, "yd", 0) },
                { key: qsTr("Left–right"), val: root._spread(it.offlineSd, "yd", 0) },
                { key: qsTr("Club speed"), val: root._meanSd(it.speedMean, it.speedSd, "mph", 0) },
                { key: qsTr("Ball speed"), val: root._meanSd(it.ballSpeedMean, it.ballSpeedSd, "mph", 0) },
                { key: qsTr("Smash"),      val: root._meanSd(it.smashMean, it.smashSd, "", 2) }]
    }
    // "2026-11-08" as "8 Nov". Split by hand: new Date(iso) would read it as UTC and can land a day early.
    function _sessionDay(iso) {
        const p = String(iso || "").split("-")
        return p.length === 3 ? Qt.formatDate(new Date(+p[0], +p[1] - 1, +p[2]), "d MMM") : ""
    }
    // What the last column says: too few shots first, then an overlap, else the trend.
    function _distanceNote(it) { return it.fewShotsText || it.gapWith || it.trendWords || "—" }
    function _distanceNoteTone(it) {
        if (it.fewShotsText) return Theme.colorText3
        if (it.gapWith)      return Theme.colorWarn
        return Theme.trendTone(it.trend === "up" || it.trend === "tighter" ? 1
                             : it.trend === "down" || it.trend === "wider" ? -1 : 0)
    }
    // A row selects its club's tab, stealing focus first so an in-edit field commits against the
    // OUTGOING club. A club hit but no longer in the bag has no tab.
    function _selectFromDistances(id) {
        if (root.configuredIds.indexOf(id) < 0) return
        root.forceActiveFocus()
        root.selectedClubId = id
    }

    // Re-evaluates whenever the controller reloads (every commit calls reload()).
    readonly property var clubs: {
        void athleteController.athletes
        return athleteController.clubsFor(root.athleteUuid)
    }
    readonly property var configuredIds: {
        void athleteController.athletes
        return athleteController.bagClubIds(root.athleteUuid)
    }
    readonly property var unconfiguredIds: {
        var out = []
        var vocab = athleteController.clubOptions()
        for (var i = 0; i < vocab.length; i++)
            if (!(vocab[i] in root.clubs))
                out.push(vocab[i])
        return out
    }

    function recField(id, field, dflt) {
        var rec = root.clubs[id]
        if (!rec || rec[field] === undefined) return dflt
        return rec[field]
    }
    function commitField(id, field, value) {
        var rec = root.clubs[id] || {}
        var copy = {}
        for (var k in rec) copy[k] = rec[k]
        copy[field] = value
        athleteController.setClubRecord(root.athleteUuid, id, copy)
    }
    // Parse "270, 320, 555" -> sorted [270,320,555]; null on any invalid token.
    function parseCenters(text) {
        var t = String(text).trim()
        if (t === "") return []
        var parts = t.split(",")
        var out = []
        for (var i = 0; i < parts.length; i++) {
            var v = parseInt(parts[i].trim(), 10)
            if (isNaN(v) || v <= 0) return null
            out.push(v)
        }
        out.sort(function(a, b) { return a - b })
        return out
    }
    // Describe a band layout for the helper caption, e.g. -> "2–1–3".
    function bandPattern(centers) {
        if (!centers || centers.length === 0) return ""
        var groups = [1]
        for (var i = 1; i < centers.length; i++) {
            if (centers[i] - centers[i - 1] > 120) groups.push(1)   // >120 mm gap = new group
            else groups[groups.length - 1]++
        }
        return groups.join("–")
    }

    // ── Body (PpGroupCard's default content) ───────────────────────────────
    Text {
        width: parent.width
        text: qsTr("Club length sets the shaft-tracker search radius. Shaft length (bottom of grip to top of hosel) and hands end (bottom of your hands from the butt) let the tracker measure an unmarked club. Band centres describe retroreflective tape (optional), measured from the butt end of the grip in millimetres.")
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorText3
        wrapMode:       Text.WordWrap
    }

    // Add club
    Row {
        spacing: Theme.gap(10)

        PpComboBox {
            id: addCombo
            implicitWidth: Theme.sp(220)
            model: root.unconfiguredIds
            displayFn: ClubFormat.display
            enabled: root.unconfiguredIds.length > 0
        }

        PpButton {
            label:   qsTr("Add club")
            primary: true
            enabled: root.unconfiguredIds.length > 0
            onClicked: {
                var id = root.unconfiguredIds[addCombo.currentIndex]
                if (!id) return
                // factory spec defaults (loft/length/shaft) per club
                athleteController.setClubRecord(root.athleteUuid, id,
                    athleteController.defaultClubRecord(id))
                root.selectedClubId = id
            }
        }
    }

    Text {
        visible: root.configuredIds.length === 0
        width: parent.width
        text: qsTr("No clubs recorded for this athlete yet.")
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        color:          Theme.colorText3
        wrapMode:       Text.WordWrap
    }

    // Your distances: carry per club, one row each, on one shared axis.
    PpCard {
        id: distanceCard
        objectName: "athleteDistanceCard"
        visible: root.distanceItems.length > 0
        width:   parent.width
        height:  visible ? implicitHeight : 0
        table:   true
        title:   Theme.caps(qsTr("Your distances"))
        aside:   qsTr("Full swings · last 10 sessions per club")

        // Fixed columns as tight as their words, so the shared axis keeps room for its two labels.
        columns: [{ title: qsTr("Club"),             width: Theme.sp(130) },
                  { title: qsTr("Carry"),            width: Theme.sp(84) },
                  { title: qsTr("Average ± spread"), fill: true },
                  { title: qsTr("Total"),            width: Theme.sp(52) },
                  { title: qsTr("Left–right"),       width: Theme.sp(60) },
                  { title: qsTr("Shots"),            width: Theme.sp(40) },
                  { title: qsTr("Trend"),            width: Theme.sp(104) }]

        Repeater {
            model: root.distanceItems
            PpTableRow {
                id: distanceRow
                required property var modelData
                readonly property string club:     distanceRow.modelData.club || ""
                readonly property bool   selected: distanceRow.club === root.selectedClubId
                readonly property bool   inBag:    root.configuredIds.indexOf(distanceRow.club) >= 0
                objectName: "athleteDistanceRow:" + distanceRow.club

                Accessible.role: Accessible.Button
                Accessible.name: distanceRow.modelData.label || ""
                Accessible.onPressAction: root._selectFromDistances(distanceRow.club)

                Row {                               // club; the one showing below says so
                    width:   distanceRow.columnWidth(0)
                    spacing: Theme.gap(6)
                    Text {
                        id: clubWords
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, Math.max(0, parent.width
                                        - (shownWord.visible ? shownWord.implicitWidth + parent.spacing : 0)))
                        text:  distanceRow.modelData.label || ""
                        elide: Text.ElideRight
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        font.weight:    Theme.fontBodyWeight
                        color:          Theme.colorText
                    }
                    PpCaption {
                        id: shownWord
                        visible: distanceRow.selected
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("shown below")
                    }
                }
                Text {                              // carry
                    objectName: "athleteDistanceCarry"
                    width: distanceRow.columnWidth(1)
                    text:  distanceRow.modelData.carryText || "—"
                    elide: Text.ElideRight
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText
                }
                Item {                              // the band, on the card's one axis
                    width:  distanceRow.columnWidth(2)
                    height: Theme.sp(18)
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 1
                        color: Theme.colorHairline
                    }
                    PpBoxPlot {
                        anchors.fill: parent
                        roles:      distancesTs
                        namePrefix: "athleteDistanceBand"
                        map:   function (v) { return (v - root.distanceAxis.lo) / (root.distanceAxis.hi - root.distanceAxis.lo) * width }
                        stats: root._box(distanceRow.modelData.carryMean, distanceRow.modelData.carrySd)
                        bodyW: Theme.sp(10)
                        capW:  0
                    }
                }
                Text {                              // total
                    width: distanceRow.columnWidth(3)
                    text:  root._figure(distanceRow.modelData.totalMean, "yd", 0)
                    elide: Text.ElideRight
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText2
                }
                Text {                              // left–right
                    width: distanceRow.columnWidth(4)
                    text:  root._spread(distanceRow.modelData.offlineSd, "yd", 0)
                    elide: Text.ElideRight
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText2
                }
                Text {                              // shots
                    width: distanceRow.columnWidth(5)
                    text:  String(distanceRow.modelData.shots || 0)
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText2
                }
                Text {                              // few shots, an overlap, or the trend: in words
                    objectName: "athleteDistanceNote"
                    width: distanceRow.columnWidth(6)
                    text:  root._distanceNote(distanceRow.modelData)
                    elide: Text.ElideRight
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          root._distanceNoteTone(distanceRow.modelData)
                }
                // Past the template's columns, so the row leaves it where it is put: over the whole row.
                MouseArea {
                    x: -distanceRow.padH
                    width:  distanceRow.width
                    height: distanceRow.height
                    enabled: distanceRow.inBag
                    cursorShape: distanceRow.inBag ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root._selectFromDistances(distanceRow.club)
                }
            }
        }

        PpTableRow {                                // the shared axis
            id: distanceAxisRow
            hoverEnabled: false
            height: Theme.sp(26)
            Item { width: 1; height: 1 }
            Item { width: 1; height: 1 }
            Item {
                width:  distanceAxisRow.columnWidth(2)
                height: axisLow.implicitHeight
                PpMicro {
                    id: axisLow
                    text: Figures.withUnit(Figures.figure(root.distanceAxis.lo, 0), "yd")
                }
                PpMicro {
                    anchors.right: parent.right
                    text: Figures.withUnit(Figures.figure(root.distanceAxis.hi, 0), "yd")
                }
            }
        }

        PpTableRow {
            id: distanceFootRow
            hoverEnabled: false
            height: Math.max(Theme.sp(Theme.rowH), distanceFoot.implicitHeight + Theme.gap(18))
            PpCardNote {
                id: distanceFoot
                width: distanceFootRow.cellBox.width
                text:  qsTr("A row opens that club's tab below. Fat, thin, topped and shanked shots are left out of the average. Spread is one standard deviation either side, not a quartile box. Clubs overlap when their ± bands do.")
            }
        }
    }

    PpCardNote {
        objectName: "athleteDistanceNoteLine"
        visible: text !== ""
        width:   parent.width
        text:    root.distances.note || ""
    }

    // Club tabs — the athlete's configured clubs in the app's stage-tab
    // language (see PpModeStage on the Wrist screen): no border, no
    // pill; the active club carries a 2px accent underline (theme
    // accent) and brighter text — the chip group's "tab" look, which wraps,
    // so a full bag — up to 17 clubs — fits. Selecting a tab shows ONE detail card.
    PpChipGroup {
        width:   parent.width
        spacing: Theme.gap(5)
        wrap:    true
        style:   "tab"
        options:  root.configuredIds.map(id => ({ label: ClubFormat.display(id), value: id }))
        selected: root.selectedClubId
        onSelectionChanged: (id) => {
            // Steal focus first so an in-edit field commits
            // against the OUTGOING club before the switch.
            root.forceActiveFocus()
            root.selectedClubId = id
        }
    }

    // Detail card for the selected club
    Rectangle {
        visible: root.selectedClubId !== ""
        width:  parent.width
        height: visible ? clubCol.implicitHeight + Theme.gap(28) : 0
        radius: Theme.radius
        color:  Theme.colorBg2
        border.width: 1
        border.color: Theme.colorBorder

        ColumnLayout {
            id: clubCol
            anchors { fill: parent; margins: Theme.gap(14) }
            spacing: Theme.gap(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(10)

                    Text {
                        text: root.selectedClubId
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        font.weight:    Font.DemiBold
                        color:          Theme.colorText
                    }
                    Text {
                        visible: root.recField(root.selectedClubId, "tapedOn", "") !== ""
                        text: qsTr("taped %1").arg(root.recField(root.selectedClubId, "tapedOn", ""))
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                    Item { Layout.fillWidth: true }
                    PpButton {
                        label: qsTr("Remove")
                        destructive: true
                        onClicked: athleteController.removeClubRecord(root.athleteUuid, root.selectedClubId)
                    }
                }

                // Distances with this club: its six figures, its carry session by session, and where
                // "short" begins.
                PpCard {
                    id: clubDistanceCard
                    objectName: "athleteClubDistances"
                    visible: root.selectedDistance !== null
                    Layout.fillWidth: true
                    title: Theme.caps(qsTr("Distances with this club"))
                    aside: root.selectedDistance
                           ? qsTr("%1 · %2").arg(Theme.plural(root.selectedDistance.shots || 0, qsTr("%1 shot"), qsTr("%1 shots")))
                                            .arg(Theme.plural(root.selectedDistance.sessions || 0, qsTr("%1 session"), qsTr("%1 sessions")))
                           : ""
                    itemGap: Theme.gap(12)

                    PpFactStrip {
                        objectName: "athleteClubFigures"
                        width: parent.width
                        cells: root.selectedDistance ? root._distanceCells(root.selectedDistance) : []
                    }
                    PpCaption {
                        objectName: "athleteClubMishits"
                        visible:  text !== ""
                        width:    parent.width
                        elide:    Text.ElideNone
                        wrapMode: Text.WordWrap
                        text:     root.selectedDistance ? (root.selectedDistance.mishitsText || "") : ""
                    }

                    Grid {
                        id: distanceGrid
                        width: parent.width
                        readonly property bool stacked: width < Theme.sp(560)
                        readonly property real cellW: stacked ? width : Math.floor((width - columnSpacing) / 2)
                        columns: stacked ? 1 : 2
                        columnSpacing: Theme.gap(24)
                        rowSpacing:    Theme.gap(14)

                        // Carry by session: one mark each, on one shared axis.
                        Column {
                            id: sessionChart
                            objectName: "athleteClubCarryBySession"
                            width:   distanceGrid.cellW
                            spacing: Theme.gap(6)
                            readonly property var  points: root.selectedDistance ? (root.selectedDistance.perSession || []) : []
                            readonly property var  axis:   root._axis(sessionChart.points.map(p => root._box(p.mean, p.sd)), 5)
                            readonly property int  gutter: Theme.sp(48)
                            readonly property real cellW:  sessionChart.points.length > 0
                                                           ? (sessionChart.width - sessionChart.gutter) / sessionChart.points.length : 0
                            visible: sessionChart.points.length > 0

                            PpMicro { text: Theme.caps(qsTr("Carry by session · average ± spread")) }

                            Item {
                                width:  parent.width
                                height: Theme.sp(90)
                                PpMicro { text: Figures.withUnit(Figures.figure(sessionChart.axis.hi, 0), "yd") }
                                PpMicro {
                                    anchors.bottom: parent.bottom
                                    text: Figures.withUnit(Figures.figure(sessionChart.axis.lo, 0), "yd")
                                }
                                Row {
                                    x: sessionChart.gutter
                                    Repeater {
                                        model: sessionChart.points
                                        PpBoxPlot {
                                            required property var modelData
                                            width:  sessionChart.cellW
                                            height: Theme.sp(90)
                                            roles:      distancesTs
                                            namePrefix: "athleteSessionBand"
                                            vertical: true
                                            map:   function (v) { return height - (v - sessionChart.axis.lo) / (sessionChart.axis.hi - sessionChart.axis.lo) * height }
                                            stats: root._box(modelData.mean, modelData.sd)
                                            bodyW: Theme.sp(8)
                                            capW:  0
                                        }
                                    }
                                }
                            }
                            Row {
                                x: sessionChart.gutter
                                Repeater {
                                    model: sessionChart.points
                                    Item {
                                        required property var modelData
                                        required property int index
                                        width:  sessionChart.cellW
                                        height: dayLabel.implicitHeight
                                        PpMicro {
                                            id: dayLabel
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            visible: parent.index === 0 || parent.index === sessionChart.points.length - 1
                                                     || sessionChart.cellW >= Theme.sp(46)
                                            text: root._sessionDay(parent.modelData.date)
                                        }
                                    }
                                }
                            }
                        }

                        Column {
                            width:   distanceGrid.cellW
                            spacing: Theme.gap(10)
                            PpInset {
                                objectName: "athleteClubShortBelow"
                                visible: root.selectedDistance !== null && (root.selectedDistance.shortBelowText || "") !== ""
                                width:   parent.width
                                PpMicro { text: Theme.caps(qsTr("When a shot counts as short")) }
                                Text {
                                    width: parent.width
                                    text:  root.selectedDistance ? (root.selectedDistance.shortBelowText || "") : ""
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText2
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.4
                                }
                            }
                            PpCaption {
                                objectName: "athleteClubGaps"
                                visible:  text !== ""
                                width:    parent.width
                                elide:    Text.ElideNone
                                wrapMode: Text.WordWrap
                                text:     root.selectedDistance ? (root.selectedDistance.gapsText || "") : ""
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.gap(20)
                    rowSpacing: Theme.gap(8)

                    PpFieldLabel { text: Theme.caps(qsTr("Shaft")) }
                    PpComboBox {
                        implicitWidth: Theme.sp(150)
                        model: [qsTr("steel"), qsTr("graphite")]
                        currentIndex: root.recField(root.selectedClubId, "shaftType", "steel") === "graphite" ? 1 : 0
                        onActivated: (index) => root.commitField(root.selectedClubId, "shaftType",
                                                                 index === 1 ? "graphite" : "steel")
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Loft"), "°") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "loftDeg", 0) > 0
                              ? String(root.recField(root.selectedClubId, "loftDeg", 0)) : ""
                        placeholderText: qsTr("e.g. 34")
                        validator: DoubleValidator { bottom: 0; top: 80; decimals: 1 }
                        onEditingFinished: {
                            var v = parseFloat(text)
                            root.commitField(root.selectedClubId, "loftDeg", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Club length"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "lengthMm", 0) > 0
                              ? String(root.recField(root.selectedClubId, "lengthMm", 0)) : ""
                        placeholderText: qsTr("e.g. 1120")
                        validator: IntValidator { bottom: 0; top: 1400 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "lengthMm", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Shaft length"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "shaftLengthMm", 0) > 0
                              ? String(root.recField(root.selectedClubId, "shaftLengthMm", 0)) : ""
                        placeholderText: qsTr("e.g. 617")
                        validator: IntValidator { bottom: 0; top: 1400 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "shaftLengthMm", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Hands end from butt"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "handsEndMm", 0) > 0
                              ? String(root.recField(root.selectedClubId, "handsEndMm", 0)) : ""
                        placeholderText: qsTr("e.g. 180")
                        validator: IntValidator { bottom: 0; top: 400 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "handsEndMm", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Band width"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: String(root.recField(root.selectedClubId, "bandWidthMm", 25))
                        validator: IntValidator { bottom: 0; top: 100 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "bandWidthMm", isNaN(v) ? 25 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Band centres from butt"), "mm") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap(4)

                        PpTextField {
                            id: centersField
                            Layout.fillWidth: true
                            placeholderText: qsTr("e.g. 270, 320, 555, 760, 810, 860")
                            text: {
                                var c = root.recField(root.selectedClubId, "bandCentersMm", [])
                                return c.length ? c.join(", ") : ""
                            }
                            hasError: root.parseCenters(text) === null
                            onEditingFinished: {
                                var parsed = root.parseCenters(text)
                                if (parsed === null) return   // keep hasError showing
                                root.commitField(root.selectedClubId, "bandCentersMm", parsed)
                                if (parsed.length > 0)
                                    root.commitField(root.selectedClubId, "tapedOn",
                                                     Qt.formatDate(new Date(), "yyyy-MM-dd"))
                            }
                        }

                        Text {
                            readonly property var centers: root.recField(root.selectedClubId, "bandCentersMm", [])
                            text: centersField.hasError
                                  ? qsTr("Invalid — comma-separated whole millimetres only")
                                  : centers.length === 0
                                    ? qsTr("Empty = untaped club (passive tracking only)")
                                    : qsTr("%1 bands, pattern %2").arg(centers.length)
                                                                  .arg(root.bandPattern(centers))
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          centersField.hasError ? Theme.colorWarn : Theme.colorText3
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Hosel from butt"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "hoselFromButtMm", 0) > 0
                              ? String(root.recField(root.selectedClubId, "hoselFromButtMm", 0)) : ""
                        placeholderText: qsTr("e.g. 1090")
                        validator: IntValidator { bottom: 0; top: 1400 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "hoselFromButtMm", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.caps(qsTr("Retro patch on head")) }
                    PpSwitch {
                        id: patchPill
                        checked:   root.recField(root.selectedClubId, "headPatch", false)
                        onToggled: (v) => root.commitField(root.selectedClubId, "headPatch", v)
                    }

                    PpFieldLabel { text: Theme.caps(qsTr("Notes")) }
                    PpTextField {
                        Layout.fillWidth: true
                        text: root.recField(root.selectedClubId, "notes", "")
                        placeholderText: qsTr("e.g. tape replaced, measured twice")
                        onEditingFinished: root.commitField(root.selectedClubId, "notes", text)
                    }
                }
        }
    }

    // Declared last: children are destroyed in creation order, and the distance bands read it to the end.
    PpTypeset { id: distancesTs }
}
