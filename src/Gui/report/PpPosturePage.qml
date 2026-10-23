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

// YOUR POSTURE — the Posture page (docs/design/posture_chart_design.md §5.7, stage 3b): the Charts Posture section's
// SESSION reading, printed. After the work-on pages and before "Between now and next time": it is a client page about
// this session's swings, like the work-ons, and the back matter (practice and numbers, About, the appendices) follows it.
//   · the head: the session's head line and its pattern line;
//   · the session profile: one row per posture reading, P1…P8, the pattern's band as a wash in the reading's OWN metric
//     colour (identity, not judgement), the fault line as a hairline, the session's box per position (PpChBoxCell →
//     PpBoxPlot, the ONE box plot, with box_stats' numbers from the presenter), and under each column the median ("-"
//     where nothing was read), how wide the middle half is, or — a median outside the band — the side it falls on,
//     in warn and underlined (a word, never colour alone); a reading under a ⚠ keeps its figure and says the ⚠'s
//     few words, and is never ringed; under trunk bend at P3–P5 a second, smaller figure — the back line, the session's
//     median of it — with its own ⚠ words (decision 16 of session_posture.h: not matched, no band of its own);
//   · at address (§7.4.2): each rig-sensitive address reading, the session's box against the good-player band, the
//     golfer's earlier sessions from the same set-up, and the ⚠ "depends on where the camera stands" under each;
//   · what moved, at the top and at impact;
//   · the pattern by shot (a mark per shot, ringed where it is not the session's pattern), the departures as counts,
//     how repeatable it is;
//   · the picture strip (stage 5): the session's typical swing at address, the top and impact, with the frozen address
//     lines and the bands at impact (PpChPostureFrame, the screen's frame, in the report's inks);
//   · the trust note, said once, open (paper has no fold).
//
// The profile is PpReportProfile (shared with the Wrist page): a PRINT-ONLY SIBLING of PpChPostureProfile, reading the same rows. The screen profile's geometry is a screen's
// (rows of 160 px with labels above at this width: the six rows would need two pages on their own); the numbers, the
// words, the bands, the boxes and the scale (each row's yLo…yHi) are the presenter's and are not re-made here.
// FLOWED (§9.1 rules 1, 2): the section starts a fresh page; the profile is a printGroup whose head is the column
// heads (repeated on a page that opens inside it) and each reading one atom; every other block is one atom, and a
// heading keeps with what follows. Every sentence is the map's `posture` (report_posture.h); the register sets only
// the fixed headings. FOLIO LIGHT through `ro` (the report's palette) and `cr` (the Charts print roles in the same
// fixed aesthetic, for the box): nothing reads a live theme token. No affordance: nothing here is a link or a fold.
// objectNames: reportPosture (the head), reportPostureProfile, reportPostureRow (+ key), reportPostureCell (+ tag,
// text, warned, outside; its second reading reportPostureAltValue: tag, rowKey, text, warned), reportPostureAddress (+ reportPostureAddressRow: measureId, text, warned, hasHistory), reportPostureMoved, reportPostureStrip, reportPostureDepartures, reportPostureRepeat,
// reportPosturePicture (+ reportPosturePictureGroup — one pager atom per camera —, reportPosturePictureFrame), reportPostureTrust.
Column {
    id: pp
    property QtObject ro: null
    property var po: null
    readonly property var m: po || ({})
    readonly property bool on: !!po
    readonly property var prof: m.profile || ({})
    readonly property var rows: (prof.rows || []).slice(0, 6)                       // the map's cap
    readonly property var positions: (prof.positions || []).slice(0, 8)
    readonly property var moved: (m.moved || []).slice(0, 2)
    readonly property var strip: m.strip || null
    readonly property var marks: strip ? (strip.marks || []).slice(0, 120) : []
    readonly property var deps: (m.departures || []).slice(0, 6)
    readonly property var trust: (m.trust || []).slice(0, 16)
    visible: on
    width: 600

    // the address block's label column (the profile's own geometry is PpReportProfile's: the same label column)
    readonly property real labelW: 122
    function fin(v) { return v !== null && v !== undefined && isFinite(v) }

    // ═══ the head ═══
    Column {
        objectName: "reportPosture"
        property bool atom: true
        property bool keepNext: true
        property bool breakBefore: true
        property string section: "Posture"
        visible: pp.on
        width: pp.width
        Text { width: parent.width; text: pp.m.kicker || ""; font: pp.ro.fKicker; color: pp.ro.accent; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: "Posture"; font: pp.ro.fH1; color: pp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 10 }
        Text { width: parent.width; text: pp.m.head || ""; font: pp.ro.fStand; color: pp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.2 }
        Item { width: 1; height: 8; visible: patLine.visible }
        Item {
            id: patLine
            visible: !!pp.m.patternLine
            width: parent.width; height: visible ? Math.max(patLab.height, patText.height) : 0
            Text { id: patLab; y: 1; text: "Pattern"; font: pp.ro.fLabel; color: pp.ro.ink3 }
            Text { id: patText; x: 64; width: parent.width - x; text: pp.m.patternLine || ""; font: pp.ro.fBody; color: pp.ro.ink; wrapMode: Text.WordWrap }
        }
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "Positions across the session"; font: pp.ro.fH3; color: pp.ro.ink; wrapMode: Text.WordWrap }
        Item { width: 1; height: 3 }
        Text { width: parent.width; visible: text !== ""; text: pp.prof.sub || ""; font: pp.ro.fSmall; color: pp.ro.ink3; wrapMode: Text.WordWrap }
        Item { width: 1; height: 8 }
    }

    // ═══ the profile: the column heads (repeated on a continued page), then one atom per reading, then the caption
    //     (PpReportProfile, shared with the Wrist page; the names stay reportPostureProfile / Row / Cell / AltValue) ═══
    PpReportProfile {
        ro: pp.ro; roles: cr
        prefix: "reportPosture"; printGroup: "posture"
        on: pp.on; rows: pp.rows; positions: pp.positions; caption: pp.prof.caption || ""
        width: pp.width
    }

    // ═══ at address (§7.4.2): the rig-sensitive address readings — the session's box against the good-player band, the
    //     golfer's earlier sessions from the same set-up, and the ⚠ under every reading. A heading atom kept with the
    //     first reading, then one atom per reading. ═══
    Column {
        id: addr
        objectName: "reportPostureAddress"
        readonly property var block: pp.m.address || null
        readonly property var arows: block ? (block.rows || []).slice(0, 8) : []
        readonly property real laneH: 13
        readonly property real laneW: 92
        readonly property real aPlotW: pp.width - pp.labelW - laneW
        visible: pp.on && arows.length > 0
        width: pp.width
        Column {
            property bool atom: true
            property bool keepNext: true
            width: pp.width
            Item { width: 1; height: 18 }
            Text { width: parent.width; text: addr.block ? (addr.block.title || "At address") : "At address"; font: pp.ro.fH3; color: pp.ro.ink }
            Item { width: 1; height: 3 }
            Text { width: parent.width; visible: text !== ""; text: addr.block ? (addr.block.sub || "") : ""; font: pp.ro.fSmall; color: pp.ro.ink3; wrapMode: Text.WordWrap }
            Item { width: 1; height: 6 }
        }
        Repeater {
            model: addr.arows
            delegate: Item {
                id: ar
                required property var modelData
                objectName: "reportPostureAddressRow"
                property bool atom: true
                readonly property var r: modelData
                readonly property string measureId: r.measureId || ""
                readonly property string text: r.text || "-"
                readonly property bool warned: r.warned === true
                readonly property bool hasHistory: r.hasHistory === true
                readonly property color hue: pp.ro.hue(r.hueKey || r.metricKey || "")
                readonly property real lo: pp.fin(r.xLo) ? r.xLo : 0
                readonly property real hi: pp.fin(r.xHi) ? r.xHi : 1
                function vx(v) { return pp.labelW + addr.aPlotW * (v - lo) / Math.max(1e-9, hi - lo) }
                readonly property real plotH: 2 * addr.laneH + 3
                width: pp.width
                height: Math.max(alab.height + 6, 4 + plotH + 3 + afig.height + (awarn.visible ? awarn.height + 1 : 0) + 8)

                Column {
                    id: alab
                    y: 4; width: pp.labelW - 12; spacing: 2
                    Row { spacing: 6
                        Rectangle { width: 7; height: 7; radius: 3.5; color: ar.hue; anchors.verticalCenter: parent.verticalCenter }
                        Text { width: pp.labelW - 25; text: ar.r.label || ""; font: pp.ro.fBodyDM; color: pp.ro.ink; wrapMode: Text.WordWrap } }
                    Text { width: parent.width; text: (ar.r.camera || "") + (ar.r.unit ? ", " + ar.r.unit : ""); font: pp.ro.fMicro; color: pp.ro.ink3; wrapMode: Text.WordWrap }
                }
                // the band across both lanes, in the reading's own colour
                Rectangle {
                    visible: ar.r.banded === true && pp.fin(ar.r.bandLo) && pp.fin(ar.r.bandHi)
                    x: visible ? Math.max(pp.labelW, ar.vx(ar.r.bandLo)) : 0
                    width: visible ? Math.max(1, Math.min(pp.labelW + addr.aPlotW, ar.vx(ar.r.bandHi)) - x) : 0
                    y: 4; height: ar.plotH
                    color: Qt.alpha(ar.hue, 0.16)
                }
                PpChBoxCell {
                    x: pp.labelW; width: addr.aPlotW; y: 4; height: addr.laneH
                    lo: ar.lo; hi: ar.hi
                    roles: cr; brush: null; interactive: false; showPicked: false
                    box: ar.r.box || null; values: ar.r.values || []; fired: []
                    color: ar.hue
                }
                PpChBoxCell {
                    visible: ar.hasHistory
                    x: pp.labelW; width: addr.aPlotW; y: 4 + addr.laneH + 3; height: addr.laneH
                    lo: ar.lo; hi: ar.hi
                    roles: cr; brush: null; interactive: false; showPicked: false; muted: true
                    box: ar.hasHistory ? ar.r.history : null; values: []; fired: []
                }
                Text { visible: !ar.hasHistory && text !== ""; x: pp.labelW + 4; width: addr.aPlotW - 8; y: 4 + addr.laneH + 3
                       text: ar.r.historyWhy || ""; font: pp.ro.fMicro; color: pp.ro.ink3; wrapMode: Text.WordWrap }
                Text { x: pp.labelW + addr.aPlotW + 6; y: 4; width: addr.laneW - 6; text: "this session"; font: pp.ro.fMicro; color: pp.ro.ink3 }
                Text { x: pp.labelW + addr.aPlotW + 6; y: 4 + addr.laneH + 3; width: addr.laneW - 6; text: "earlier sessions"; font: pp.ro.fMicro; color: pp.ro.ink3 }
                // the figures, then the ⚠ and its few words
                Text {
                    id: afig
                    x: pp.labelW; y: 4 + ar.plotH + 3; width: pp.width - pp.labelW; wrapMode: Text.WordWrap
                    text: [ar.text + (ar.r.valueWord ? " " + ar.r.valueWord : ""), ar.r.bandText || "", ar.r.boxText || "", ar.r.historyText || ""]
                          .filter(function (x) { return x !== "" }).join("   ·   ")
                    font: pp.ro.fSmall; color: pp.ro.ink2; lineHeight: 1.2
                }
                Text {
                    id: awarn
                    visible: ar.warned
                    x: pp.labelW; y: afig.y + afig.height + 1; width: pp.width - pp.labelW; wrapMode: Text.WordWrap
                    text: ar.warned ? (ar.r.warn || "⚠") + " " + (ar.r.warnShort || "") : ""
                    font: pp.ro.fMicro; color: pp.ro.ink2
                }
                Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; width: parent.width; height: pp.ro.hw; color: pp.ro.faint }
            }
        }
        Item {
            property bool atom: true
            property bool keepPrev: true
            visible: !!addr.block && !!addr.block.legend
            width: pp.width; height: visible ? alg.height + 8 : 0
            Text { id: alg; y: 4; width: parent.width; text: addr.block ? (addr.block.legend || "") : ""; font: pp.ro.fMicro; color: pp.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
        }
    }

    // ═══ what moved ═══
    Column {
        objectName: "reportPostureMoved"
        property bool atom: true
        visible: pp.on && pp.moved.length > 0
        width: pp.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "What moved"; font: pp.ro.fH3; color: pp.ro.ink }
        Item { width: 1; height: 6 }
        Row {
            spacing: 24
            Repeater {
                model: pp.moved
                delegate: Column {
                    required property var modelData
                    width: (pp.width - 24 * (pp.moved.length - 1)) / Math.max(1, pp.moved.length); spacing: 3
                    Text { width: parent.width; text: modelData.when || ""; font: pp.ro.fLabel; color: pp.ro.ink2; wrapMode: Text.WordWrap }
                    Text { width: parent.width; text: modelData.line || ""; font: pp.ro.fBody; color: pp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.25 }
                }
            }
        }
    }

    // ═══ the pattern by swing: a mark per swing, thirty to a line; ringed = not the session's pattern, or between ═══
    Column {
        id: stripBlock
        objectName: "reportPostureStrip"
        property bool atom: true
        visible: pp.on && pp.marks.length > 0
        width: pp.width
        readonly property int perLine: 30
        readonly property real pitch: Math.min(22, pp.width / perLine)
        readonly property var lines: {
            const out = []
            for (let i = 0; i < pp.marks.length; i += perLine) out.push(pp.marks.slice(i, i + perLine))
            return out
        }
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "Pattern by swing"; font: pp.ro.fH3; color: pp.ro.ink }
        Item { width: 1; height: 3 }
        Text { width: parent.width; visible: text !== ""; text: pp.strip ? (pp.strip.legend || "") : ""; font: pp.ro.fSmall; color: pp.ro.ink3; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
        Repeater {
            model: stripBlock.lines
            delegate: Item {
                required property var modelData
                width: pp.width; height: 34
                Repeater {
                    model: modelData
                    delegate: Item {
                        required property var modelData
                        required property int index
                        x: index * stripBlock.pitch; width: stripBlock.pitch; height: 34
                        // the session's pattern: a filled bar; another pattern (or between): a hollow ringed bar; none: a hairline
                        Rectangle {
                            x: (parent.width - width) / 2; y: 2
                            width: Math.max(4, stripBlock.pitch * 0.5); height: 16
                            radius: 1
                            color: !modelData.ok ? "transparent" : modelData.differs ? pp.ro.paper : pp.ro.ink2
                            border.width: !modelData.ok ? 0.75 : modelData.differs ? 1.5 : 0
                            border.color: !modelData.ok ? pp.ro.hair : pp.ro.warn
                        }
                        Text { visible: modelData.swing === 1 || modelData.swing % 5 === 0
                               width: parent.width; y: 21; horizontalAlignment: Text.AlignHCenter
                               text: modelData.swing; font: pp.ro.fFigS; color: pp.ro.ink3 }
                    }
                }
            }
        }
        Text { width: parent.width; visible: text !== ""; text: pp.strip ? (pp.strip.line || "") : ""; font: pp.ro.fSmall; color: pp.ro.ink2; wrapMode: Text.WordWrap }
        Text { width: parent.width; visible: text !== ""; text: pp.strip ? (pp.strip.more || "") : ""; font: pp.ro.fMicro; color: pp.ro.ink3; wrapMode: Text.WordWrap }
    }

    // ═══ the departures: counts across the shots ═══
    Column {
        objectName: "reportPostureDepartures"
        visible: pp.on && pp.deps.length > 0
        width: pp.width
        Column {
            property bool atom: true
            property bool keepNext: true
            width: pp.width
            Item { width: 1; height: 18 }
            Text { width: parent.width; text: "Departures"; font: pp.ro.fH3; color: pp.ro.ink }
            Item { width: 1; height: 3 }
            Text { width: parent.width; visible: text !== ""; text: pp.m.departuresSub || ""; font: pp.ro.fSmall; color: pp.ro.ink3; wrapMode: Text.WordWrap }
            Item { width: 1; height: 4 }
        }
        Repeater {
            model: pp.deps
            delegate: Item {
                required property var modelData
                property bool atom: true
                width: pp.width; height: Math.max(dn.height, dd.height) + 9
                Text { id: dn; y: 4; width: pp.width * 0.42; text: modelData.name || ""; font: pp.ro.fBodyDM; color: pp.ro.ink; wrapMode: Text.WordWrap }
                Text { id: dd; x: pp.width * 0.42 + 12; y: 5; width: pp.width - x; text: modelData.detail || ""; font: pp.ro.fSmall; color: pp.ro.ink2; wrapMode: Text.WordWrap }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: pp.ro.hw; color: pp.ro.hair }
            }
        }
        Item {
            property bool atom: true
            property bool keepPrev: true
            visible: !!pp.m.departuresMore
            width: pp.width; height: visible ? dm.height + 6 : 0
            Text { id: dm; y: 4; width: parent.width; text: pp.m.departuresMore || ""; font: pp.ro.fMicro; color: pp.ro.ink3; wrapMode: Text.WordWrap }
        }
    }

    // ═══ repeatable? ═══
    Column {
        objectName: "reportPostureRepeat"
        property bool atom: true
        visible: pp.on && !!pp.m.repeat
        width: pp.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "Repeatable?"; font: pp.ro.fH3; color: pp.ro.ink }
        Item { width: 1; height: 4 }
        Text { width: parent.width; text: pp.m.repeat || ""; font: pp.ro.fBody; color: pp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    }

    // ═══ the picture strip (§5.4, stage 5): the session's typical swing at address, the top and impact, down the line
    //     then face-on, with its frozen address lines, the bands at impact and the butt line at impact. ONE PAGER ATOM PER
    //     CAMERA, each with its own heading, so the pager can break between them rather than leave a page a quarter empty;
    //     the legend keeps with the last. With no frames: one atom, the heading and why. Each row's frames share one
    //     height so the row fills the text width. ═══
    Column {
        id: picBlock
        objectName: "reportPosturePicture"
        readonly property var pic: pp.m.picture || null
        readonly property var groups: pic ? (pic.groups || []) : []
        readonly property var whyLines: pic ? (pic.why || []) : (pp.m.pictureWhy || [])
        readonly property string title: "Address, the top and impact"
        readonly property string typical: pic ? (pic.typical || "") + (pic.typicalWhy ? "  ·  " + pic.typicalWhy : "") : ""
        visible: pp.on && (groups.length > 0 || whyLines.length > 0)
        width: pp.width
        Repeater {
            model: picBlock.groups
            delegate: Column {
                id: pg
                objectName: "reportPosturePictureGroup"
                property bool atom: true
                required property var modelData
                required property int index
                readonly property string camera: modelData.camera || ""
                readonly property string heading: picBlock.title + (modelData.label ? ": " + modelData.label : "")
                readonly property var frames: modelData.frames || []
                readonly property real gap: 10
                // the row's height: the frames side by side across the text width, capped
                readonly property real aspects: frames.reduce(function (a, f) { return a + (f.height > 0 ? f.width / f.height : 0.6) }, 0)
                readonly property real rowH: aspects > 0 ? Math.min(230, (pp.width - gap * Math.max(0, frames.length - 1)) / aspects) : 200
                width: pp.width
                Item { width: 1; height: 18 }
                Text { objectName: "reportPosturePictureHeading"; width: parent.width; text: pg.heading; font: pp.ro.fH3; color: pp.ro.ink; wrapMode: Text.WordWrap }
                Item { width: 1; height: 3 }
                Text { width: parent.width; visible: text !== ""; font: pp.ro.fSmall; color: pp.ro.ink3; wrapMode: Text.WordWrap; text: picBlock.typical }
                Item { width: 1; height: 8 }
                Row {
                    spacing: pg.gap
                    Repeater {
                        model: pg.frames
                        delegate: PpChPostureFrame {
                            objectName: "reportPosturePictureFrame"
                            required property var modelData
                            frame: modelData
                            sets: ({ posture: true, plane: true, headHips: true })   // the map's marks are already the default sets
                            frameH: pg.rowH
                            coachColor: Theme._themes.folio.light.colorGood || pp.ro.ink2
                            paper: "#2A2A2A"
                            captionColor: pp.ro.ink
                            noteColor: pp.ro.ink3
                            captionFont: pp.ro.fSmall
                            noteFont: pp.ro.fMicro
                            hueOf: function (k) { return pp.ro.hue(k) }
                            pen: 1.4
                            interactive: false
                            asyncImage: false
                        }
                    }
                }
                Item { width: 1; height: 8 }
            }
        }
        // the legend and any reasons: kept with the last camera's frames; with no frames, the heading and why alone
        Column {
            property bool atom: true
            property bool keepPrev: picBlock.groups.length > 0
            visible: legendText.visible || picBlock.whyLines.length > 0
            width: pp.width
            Item { visible: picBlock.groups.length === 0; width: 1; height: 18 }
            Text { visible: picBlock.groups.length === 0; width: parent.width; text: picBlock.title; font: pp.ro.fH3; color: pp.ro.ink; wrapMode: Text.WordWrap }
            Item { visible: picBlock.groups.length === 0; width: 1; height: 3 }
            Text { id: legendText; width: parent.width; visible: text !== ""; text: picBlock.pic ? (picBlock.pic.legend || "") : ""; font: pp.ro.fSmall; color: pp.ro.ink2; wrapMode: Text.WordWrap }
            Repeater {
                model: picBlock.whyLines
                delegate: Text { required property var modelData; width: pp.width; topPadding: 2; text: String(modelData); font: pp.ro.fSmall; color: pp.ro.ink3; wrapMode: Text.WordWrap }
            }
        }
    }

    // ═══ how far can I trust this? said once, open ═══
    Column {
        objectName: "reportPostureTrust"
        property bool atom: true
        visible: pp.on && pp.trust.length > 0
        width: pp.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "How far can I trust this?"; font: pp.ro.fH3; color: pp.ro.ink }
        Item { width: 1; height: 4 }
        Repeater {
            model: pp.trust
            delegate: Text {
                required property var modelData
                width: pp.width; leftPadding: 12; bottomPadding: 3
                text: String(modelData); font: pp.ro.fSmall; color: pp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25
            }
        }
        Text { width: parent.width; visible: text !== ""; leftPadding: 12; text: pp.m.trustMore || ""; font: pp.ro.fMicro; color: pp.ro.ink3; wrapMode: Text.WordWrap }
    }

    // the Charts print roles in Folio light: the box's design values and its inks (PpChBoxCell reads a PpTypeset).
    // LAST in the Column: children are destroyed first-declared first, so the boxes above go before their roles
    // (declared first, it went first, and every PpBoxReading / PpBoxPlot logged a null `cr` on teardown).
    PpChRoles { id: cr; medium: "print"; fixedAesthetic: "folio" }
}
