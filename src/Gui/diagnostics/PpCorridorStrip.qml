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

// THE CORRIDOR STRIP: every shot of the session placed on the measure's own axis, against the
// bands that grade it. The tick run beside it says WHETHER each shot was out; this says HOW FAR —
// which is the whole difference between "46 of 50 fired" on a session that improved by nine
// points and the same words on one that did not.
//
// WHAT IS ON IT, back to front:
//   · the grade bands, Ideal / Good / Watch / Action, off the norm and the session's policy. On a
//     one-sided corridor the side that does not grade is NEUTRAL — background, not green — because
//     the norm says nothing about it and green would read as praise;
//   · the norm's own curve, faint, over the graded side only, with a tag saying whether that norm
//     is coaching judgement or data. It is the NORM'S claim and never a fit to these shots;
//   · the fault line (where Action begins), labelled in units;
//   · one dot per ASSESSABLE shot, a small beeswarm where they collide. A shot that could not be
//     measured is not on the axis at all — it is counted in the caption, which is this picture's
//     version of the tick run's short outlined tick;
//   · the shot being read, large and ringed; the last few slightly louder than the rest;
//   · a reading too far out to fit is pinned to the edge as a chevron, and the CAPTION says how
//     far (never a label in the plot, where it sat on the dots);
//   · which way is better along the top ("← better   worse →"), off the corridor's shape;
//   · a hover that lights the same shot in the value run (the owner holds it — see `hoverIndex`).
//
// EVERY POSITION IS THE MODEL'S. SessionDiagnosticsModel::spreadFor() publishes each band, line,
// curve point and dot as a fraction of the axis, plus each dot's beeswarm row; this file
// multiplies by its own width and alternates the rows above and below the line. It decides
// nothing about which shot is where.

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // One card's `spread` map (SessionDiagnosticsModel::spreadFor). Null or empty draws nothing.
    property var spread: null
    // The panel's fit scale. See PpSessionDiagnosticsBody._fitFor().
    property real fit: 1.0
    // The condition detail's larger drawing: bigger dots, more room to stack, the caption wrapped.
    property bool large: false
    // THE PLOT ALONE — no label row, no caption — for a card too short to hold the words. The
    // fault line is still drawn; only its label goes, and the card's own recurrence line is the
    // count. Better a strip without its words than no strip (PpPatternCard's drop ladder).
    property bool bare: false
    // The caption row (session median · past the fault line · …) alone can be dropped, for a card
    // with room for the direction and fault words but not for both lines of text under the plot.
    property bool showCaption: true
    // Off on the auto-closing cast, where nothing on the panel is pressable.
    property bool interactive: true

    // ── the linked hover ─────────────────────────────────────────────────────
    //
    // THE HOVERED SHOT LIVES WITH THE OWNER (the card, or the condition detail), not here. This
    // strip reports what is under the pointer through `hovered` and draws whatever `hoverIndex`
    // the owner hands back; the value run does the same against the same property. Neither chart
    // can see the other, so neither can disagree with it about which shot is lit. A hover is not
    // a selection — the carousel's pick (the ringed dot) is untouched by it.
    property int hoverIndex: -1          // a `run` index (= the shot's position in the session)
    signal hovered(int index)            // -1 when the pointer is on no dot
    // A click on a dot: the same request the value run makes, carried up to the carousel.
    signal shotPicked(int index, int shotId, string swingDir)

    objectName: "sdCorridorStrip"

    function px(n) { return Math.round(n * Theme.fontScale * root.fit) }
    readonly property int tzCaption: Math.max(1, Math.round(Theme.sp(8) * fit))
    readonly property int tzMicro:   Math.max(1, Math.round(Theme.fontSzMicro * fit))

    readonly property var  _bands:  spread ? (spread.bands || []) : []
    readonly property var  _faults: spread ? (spread.faultLines || []) : []
    readonly property var  _dots:   spread ? (spread.dots || []) : []
    readonly property var  _curve:  spread ? (spread.curve || []) : []
    readonly property int  _stackMax: spread ? (spread.stackMax || 0) : 0
    readonly property bool _known: !!spread && spread.corridorKnown === true
    readonly property string _dirL: spread ? (spread.dirLeft || "") : ""
    readonly property string _dirC: spread ? (spread.dirCentre || "") : ""
    readonly property string _dirR: spread ? (spread.dirRight || "") : ""

    // THE BAND COLOURS, by meaning and in both themes. The status family the rest of the panel
    // already reads — green inside, amber where it fires, the faults' coral past the fault line —
    // at a fill
    // alpha low enough that a dot of the same family still reads on top. Good is the Ideal green
    // at half strength, which is what it is: inside the corridor, not at its centre.
    function bandColor(grade) {
        if (grade === "ideal")  return Qt.alpha(Theme.colorGood, 0.20)
        if (grade === "good")   return Qt.alpha(Theme.colorGood, 0.09)
        if (grade === "watch")  return Qt.alpha(Theme.colorAttention, 0.20)
        if (grade === "action") return Qt.alpha(Theme.colorWarn, 0.16)
        return "transparent"   // `open`: the side the norm does not grade is background
    }
    // ...and their names, so a band is never told by its colour alone.
    function bandWord(grade) {
        return grade === "ideal"  ? qsTr("IDEAL")
             : grade === "good"   ? qsTr("GOOD")
             : grade === "watch"  ? qsTr("WATCH")
             : grade === "action" ? qsTr("ACTION")
             :                      ""
    }
    function dotColor(state) {
        // A firing is a fault, colorWarn — not the alarm red (13.2).
        return state === "fired" ? Theme.colorWarn
             : state === "clean" ? Theme.colorGood
                                 : Theme.colorText3
    }

    // ── geometry ─────────────────────────────────────────────────────────────
    readonly property int _labelH: bare ? 0 : tzCaption + px(3)
    // The direction row, LARGE only: its own line directly above the strip, in readable type,
    // rather than the faint corner words a card-sized strip could afford.
    readonly property int _dirRowH: (large && !bare && (_dirL !== "" || _dirR !== "")) ? tzMicro + px(5) : 0
    readonly property int _captionH: (bare || !showCaption) ? 0
                                   : caption.implicitHeight + (large ? normTag.implicitHeight + px(1) : 0)
    // The two rows' heights as a card's drop ladder needs them, whatever is showing right now.
    readonly property int labelRowHeight: tzCaption + px(3)
    readonly property int captionRowHeight: caption.implicitHeight + px(2)
    readonly property real _plotH: Math.max(px(10), height - _dirRowH - _labelH - _captionH - px(2))
    readonly property real _dotD: large ? px(7) : px(5)
    // Half the stacked rows go above the line and half below, so the pitch is whatever fits
    // ceil(rows / 2) dots into half the plot — and never more than a dot, so a sparse strip does
    // not spread its few collisions further than they collide.
    readonly property real _pitch: {
        const half = Math.ceil(_stackMax / 2)
        if (half <= 0) return _dotD
        return Math.max(1, Math.min(_dotD * 0.9, (_plotH / 2 - _dotD / 2) / half))
    }
    function _rowOffset(stack) {
        const k = (stack + 1) >> 1
        return (stack % 2 === 1 ? -1 : 1) * k * root._pitch
    }

    // Large is the condition detail's: TWICE the first round's 58, because it sits in a scrolling
    // page where height is not scarce and fifty stacked dots need the room to separate.
    implicitHeight: _dirRowH + _labelH + (large ? px(116) : px(16)) + _captionH + ((bare || !showCaption) ? 0 : px(2))
    // What the strip needs WITH its words, whether or not it is bare right now — the card's drop
    // ladder decides between the two and must not read a number that its own decision moved.
    readonly property real fullImplicitHeight: _dirRowH + tzCaption + px(3) + (large ? px(116) : px(16))
                                               + caption.implicitHeight + px(2)
    visible: !!spread && _dots.length + (spread.notAssessable || 0) > 0

    // ── the label row: which way is better, and the fault line's words ───────
    // ── which way is better, large: its own row, split where the corridor splits ──
    //
    // A one-sided corridor splits at the aspiration point: "← better" ends just left of it and
    // "worse →" starts just right of it on a ceiling, the mirror on a floor — so the arrows point
    // away from the line they are about. A two-sided corridor puts "better" over mu and each
    // "worse" just outside its own fault line. Clamped inside the strip.
    Item {
        id: dirRow
        objectName: "sdStripDirRow"
        width: root.width
        height: root._dirRowH
        visible: height > 0
        readonly property real splitX: (root.spread && root.spread.muF !== undefined ? root.spread.muF : 0.5) * width
        readonly property real loX: root._dirC !== "" && root._faults.length > 0 ? root._faults[0].f * width : splitX
        readonly property real hiX: root._dirC !== "" && root._faults.length > 1 ? root._faults[root._faults.length - 1].f * width : splitX
        Text {
            id: bigLeft
            objectName: "sdStripDirBigLeft"
            text: root._dirL
            x: Math.max(0, dirRow.loX - root.px(6) - implicitWidth)
            font.family: Theme.fontData
            font.pixelSize: root.tzMicro
            font.weight: Font.DemiBold
            color: Theme.colorText2
        }
        Text {
            objectName: "sdStripDirBigCentre"
            visible: text !== ""
            text: root._dirC
            x: Math.max(bigLeft.x + bigLeft.implicitWidth + root.px(4),
                        Math.min(dirRow.splitX - implicitWidth / 2,
                                 bigRight.x - implicitWidth - root.px(4)))
            font.family: Theme.fontData
            font.pixelSize: root.tzMicro
            font.weight: Font.DemiBold
            color: Theme.colorText2
        }
        Text {
            id: bigRight
            objectName: "sdStripDirBigRight"
            text: root._dirR
            x: Math.min(root.width - implicitWidth, dirRow.hiX + root.px(6))
            font.family: Theme.fontData
            font.pixelSize: root.tzMicro
            font.weight: Font.DemiBold
            color: Theme.colorText2
            HoverHandler { id: bigHighHover; enabled: !!root.spread && (root.spread.highMeans || "") !== "" }
            ToolTip.visible: bigHighHover.hovered
            ToolTip.delay: 400
            ToolTip.text: root.spread && root.spread.highMeans ? qsTr("higher: %1").arg(root.spread.highMeans) : ""
        }
    }

    Item {
        id: labelRow
        y: root._dirRowH
        width: root.width
        height: root._labelH
        visible: !root.bare

        // WHICH WAY IS BETTER, at the two ends, off the corridor's SHAPE (the model's words): a
        // ceiling reads "← better   worse →", a floor the mirror, a two-sided corridor
        // "← worse   better   worse →" with "better" over the aspiration point. Never "high is
        // bad" by assumption. The high end's arrow carries the measure's own highMeans on hover,
        // so "worse →" can say what worse IS on this measure.
        Text {
            id: dirLeft
            objectName: "sdStripDirLeft"
            anchors.left: parent.left
            visible: text !== "" && !root.large
            text: root._dirL
            font.family: Theme.fontData
            font.pixelSize: root.tzCaption
            color: Theme.colorText3
        }
        Text {
            id: dirRight
            objectName: "sdStripDirRight"
            anchors.right: parent.right
            visible: text !== "" && !root.large
            text: root._dirR
            font.family: Theme.fontData
            font.pixelSize: root.tzCaption
            color: Theme.colorText3
            HoverHandler { id: highHover; enabled: !!root.spread && (root.spread.highMeans || "") !== "" }
            ToolTip.visible: highHover.hovered
            ToolTip.delay: 400
            ToolTip.text: root.spread && root.spread.highMeans ? qsTr("higher: %1").arg(root.spread.highMeans) : ""
        }
        Text {
            id: dirCentre
            objectName: "sdStripDirCentre"
            visible: !root.large && text !== "" && x >= dirLeft.implicitWidth + root.px(4)
                     && x + implicitWidth <= root.width - dirRight.implicitWidth - root.px(4)
            text: root._dirC
            x: (root.spread && root.spread.muF !== undefined ? root.spread.muF : 0.5) * root.width - implicitWidth / 2
            font.family: Theme.fontData
            font.pixelSize: root.tzCaption
            color: Theme.colorText3
        }
        // The room the fault labels may not enter: each end's direction words, and a gap.
        readonly property real _leftEdge:  dirLeft.visible  ? dirLeft.implicitWidth  + root.px(6) : 0
        readonly property real _rightEdge: dirRight.visible ? dirRight.implicitWidth + root.px(6) : 0

        // EACH LABEL OUTSIDE ITS OWN LINE. Two fault lines (a two-sided corridor) each take the
        // side facing away from the other — the low one's words to its left, the high one's to its
        // right — so they can never be printed over each other however close the lines sit. A
        // lone line takes whichever side has the room. Clamped to the strip either way.
        Repeater {
            model: root._faults
            Text {
                required property var modelData
                required property int index
                objectName: "sdStripFaultLabel"
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorWarn
                readonly property real lx: (modelData.f || 0) * root.width
                readonly property bool leftOfLine: root._faults.length > 1 ? index === 0
                                                                          : lx > root.width / 2
                // The room on its side of the line. The sentence when it fits, the number alone
                // when it does not (the model publishes both), elided only past that. With two
                // lines each label keeps strictly to its own side, so they cannot overlap.
                readonly property real room: Math.max(0, leftOfLine ? lx - root.px(3) - labelRow._leftEdge
                                                                    : root.width - labelRow._rightEdge - lx - root.px(3))
                readonly property bool fitsLong: longMetrics.advanceWidth <= room
                TextMetrics {
                    id: longMetrics
                    font.family: Theme.fontData
                    font.pixelSize: root.tzCaption
                    text: modelData.text || ""
                }
                text: fitsLong ? (modelData.text || "") : (modelData.shortText || modelData.text || "")
                width: Math.min(implicitWidth, room)
                elide: Text.ElideRight
                // Dropped outright when there is not room for even the bare number: a label
                // squeezed into the arrows' space would print over them.
                visible: room >= root.px(14)
                x: leftOfLine ? lx - root.px(3) - width : lx + root.px(3)
                y: 0
            }
        }
    }

    // ── the plot ─────────────────────────────────────────────────────────────
    Item {
        id: plot
        objectName: "sdStripPlot"
        y: root._dirRowH + root._labelH
        width: root.width
        height: root._plotH
        clip: false

        Repeater {
            model: root._bands
            Rectangle {
                required property var modelData
                objectName: "sdStripBand"
                readonly property string grade: modelData.grade || ""
                x: (modelData.f0 || 0) * plot.width
                width: Math.max(0, ((modelData.f1 || 0) - (modelData.f0 || 0)) * plot.width)
                height: plot.height
                color: root.bandColor(grade)

                // The band's name in Micro words at its foot, where it has the room for them.
                PpMicro {
                    objectName: "sdStripBandWord"
                    x: root.px(3)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: root.px(1)
                    visible: !root.bare && text !== "" && parent.width >= implicitWidth + root.px(6)
                    text: root.bandWord(parent.grade)
                    font.pixelSize: root.tzCaption
                    font.letterSpacing: Theme.trackingData
                }
            }
        }
        // The baseline, so the open side still reads as part of the same axis.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 1
            color: Theme.colorBorderMid
        }

        // The norm's curve: faint, and drawn from the plot's floor up — a shape, not a reading.
        Shape {
            objectName: "sdStripCurve"
            anchors.fill: parent
            visible: root._curve.length > 1
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Qt.alpha(Theme.colorText2, 0.45)
                strokeWidth: Math.max(1, root.px(1))
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2]
                fillColor: "transparent"
                PathPolyline {
                    path: root._curve.map(p => Qt.point(p.f * plot.width,
                                                        plot.height - p.d * plot.height * 0.92))
                }
            }
        }

        // The aspiration point, as a hairline.
        Rectangle {
            visible: root._known && root.spread.muF !== undefined && root.spread.muF >= 0
            x: (root.spread ? (root.spread.muF || 0) : 0) * plot.width
            width: 1
            height: plot.height
            color: Theme.colorText3
            opacity: 0.6
        }

        Repeater {
            model: root._faults
            Rectangle {
                required property var modelData
                objectName: "sdStripFaultLine"
                x: (modelData.f || 0) * plot.width - width / 2
                width: Math.max(1, root.px(1.5))
                height: plot.height
                color: Theme.colorWarn
            }
        }

        // ── the dots ─────────────────────────────────────────────────────────
        Repeater {
            id: dotRep
            model: root._dots
            Item {
                id: dotItem
                required property var modelData
                objectName: "sdStripDot"
                readonly property bool current: modelData.current === true
                readonly property bool recent:  modelData.recent === true
                readonly property bool hot:     root.hoverIndex >= 0 && modelData.index === root.hoverIndex
                readonly property int  clipped: modelData.clipped || 0
                readonly property real d: (current || hot) ? root._dotD * 1.9
                                        : recent           ? root._dotD * 1.2
                                                           : root._dotD
                width: d
                height: d
                x: (modelData.f || 0) * plot.width - d / 2
                // The ringed current shot sits ON the line, whatever its row: it is the one dot
                // the reader is looking for, and a beeswarm that moved it would hide it.
                y: plot.height / 2 + (current ? 0 : root._rowOffset(modelData.stack || 0)) - d / 2
                z: hot ? 5 : current ? 3 : (recent ? 2 : 1)

                Rectangle {
                    anchors.fill: parent
                    visible: dotItem.clipped === 0
                    radius: width / 2
                    color: root.dotColor(dotItem.modelData.state)
                    opacity: dotItem.current || dotItem.recent || dotItem.hot ? 1.0 : 0.62
                    // The hovered shot is ringed in the ACCENT, the picked one in the text colour:
                    // two different facts, never one mark.
                    border.width: (dotItem.current || dotItem.hot) ? Math.max(1, root.px(dotItem.hot ? 2 : 1.5)) : 0
                    border.color: dotItem.hot ? Theme.colorAccent : Theme.colorText
                }
                // The hover ring on a pinned chevron, which has no disc to ring.
                Rectangle {
                    anchors.fill: parent
                    visible: dotItem.clipped !== 0 && dotItem.hot
                    radius: width / 2
                    color: "transparent"
                    border.width: Math.max(1, root.px(2))
                    border.color: Theme.colorAccent
                }
                // Off the scale: a chevron at the edge in the state's colour, pointing outward.
                Text {
                    anchors.centerIn: parent
                    visible: dotItem.clipped !== 0
                    text: dotItem.clipped > 0 ? "▸" : "◂"
                    font.pixelSize: Math.round(dotItem.d * 1.6)
                    color: root.dotColor(dotItem.modelData.state)
                }
            }
        }

        // ⚠ NO WORDS INSIDE THE PLOT. The pinned readings' values used to be printed beside their
        // chevrons and sat on top of the dots they described; they are in the caption now
        // ("2 off scale, to 273 ms"), and hovering a chevron reads its value in full.

        // ── picking: the NEAREST dot centre, within a radius ─────────────────
        //
        // The beeswarm stacks dots, so "the delegate under the pointer" is whichever was declared
        // last, not the one nearest — and a 5 px dot is not a target anyone hits first time. So one
        // area over the whole plot measures the pointer against every dot's centre and takes the
        // nearest inside the radius; chevrons are dots for this purpose, so a pinned outlier is as
        // pickable as any other shot.
        //
        // A LINEAR SCAN, deliberately not a quadtree or a grid. A session is tens of shots and a
        // long one a few hundred; one pass over that on a pointer move is microseconds in QML, and
        // a spatial index would have to be rebuilt every time the strip is resized or re-stacked —
        // more work than the scan it replaces, and a second structure to keep in step with the
        // delegates. Revisit only if a ledger ever holds thousands of shots.
        //
        // Nothing near → the press is REFUSED, so it falls through to the card underneath and
        // opens the condition, as a press on empty strip always has.
        MouseArea {
            id: pick
            objectName: "sdStripPick"
            anchors.fill: parent
            anchors.margins: -root.px(4)
            z: 10
            enabled: root.interactive && root._dots.length > 0
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            readonly property real radius: Math.max(root.px(10), root._dotD * 1.5)
            property int near: -1
            cursorShape: near >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            function nearestAt(mx, my) {
                const px0 = mx + anchors.margins, py0 = my + anchors.margins   // into plot space
                let best = -1, bestD = radius * radius
                for (let i = 0; i < dotRep.count; ++i) {
                    const it = dotRep.itemAt(i)
                    if (!it) continue
                    const dx = it.x + it.width / 2 - px0, dy = it.y + it.height / 2 - py0
                    const d2 = dx * dx + dy * dy
                    if (d2 <= bestD) { bestD = d2; best = i }
                }
                return best
            }
            // What THIS chart last reported. Leaving clears the owner's hover only if it is still
            // ours: the pointer crossing straight into the value run must not have that chart's
            // fresh hover wiped by this one's late exit.
            property int sent: -1
            function track(mx, my) {
                near = nearestAt(mx, my)
                const idx = near >= 0 ? (root._dots[near].index !== undefined ? root._dots[near].index : -1) : -1
                if (idx >= 0 || root.hoverIndex === sent) { sent = idx; root.hovered(idx) }
            }
            onEntered: track(mouseX, mouseY)
            onPositionChanged: (mouse) => track(mouse.x, mouse.y)
            onExited: { near = -1; if (sent >= 0 && root.hoverIndex === sent) root.hovered(-1); sent = -1 }
            onPressed: (mouse) => { if (nearestAt(mouse.x, mouse.y) < 0) mouse.accepted = false }
            onClicked: (mouse) => root.pickDot(nearestAt(mouse.x, mouse.y))
        }
    }

    // The click's one action — callable, so a probe can drive the real path without a pointer.
    function pickDot(i) {
        if (i < 0 || i >= root._dots.length) return
        const d = root._dots[i]
        root.shotPicked(d.index !== undefined ? d.index : -1,
                        d.shotId !== undefined ? d.shotId : -1, d.swingDir || "")
    }
    // The nearest dot to a point in the STRIP's coordinates, for a probe or a test.
    function nearestDot(x, y) {
        return pick.nearestAt(x - plot.x - pick.anchors.margins, y - plot.y - pick.anchors.margins)
    }

    // ── the caption, in words, and what the curve is ─────────────────────────
    //
    // THE NORM TAG RIDES THE CAPTION ROW, right-aligned, and the caption gives way to it. It used
    // to share the label row with the fault line's words and the two collided whenever the line
    // sat on the tag's side; down here nothing else competes for the right-hand end.
    Text {
        id: caption
        objectName: "sdStripCaption"
        visible: !root.bare && root.showCaption
        y: root._dirRowH + root._labelH + root._plotH + root.px(2)
        // Large, the caption has the full width and the tag takes its own row beneath it; on a
        // card the two share one row and the caption gives way.
        width: root.large ? root.width
                          : Math.max(0, root.width - (normTag.visible ? normTag.implicitWidth + root.px(10) : 0))
        text: root.spread ? (root.large ? (root.spread.caption || "")
                                        : (root.spread.captionShort || root.spread.caption || ""))
                          : ""
        // Large, the whole sentence wraps onto a second line rather than losing its end; on a
        // card it elides, and the card's recurrence line above carries the count it loses.
        wrapMode: root.large ? Text.WordWrap : Text.NoWrap
        maximumLineCount: root.large ? 3 : 1
        elide: Text.ElideRight
        font.family: Theme.fontData
        font.pixelSize: root.large ? root.tzMicro : root.tzCaption
        color: Theme.colorText2
    }
    Text {
        id: normTag
        objectName: "sdStripNormTag"
        anchors.right: parent.right
        y: root.large ? caption.y + caption.height + root.px(1) : caption.y
        visible: !root.bare && root.showCaption && root._known && text !== ""
        text: root.spread ? (root.spread.normTag || "") : ""
        font.family: Theme.fontData
        font.pixelSize: root.tzCaption
        color: Theme.colorText3
        HoverHandler { id: tagHover }
        ToolTip.visible: tagHover.hovered && !!root.spread
        ToolTip.delay: 400
        ToolTip.text: root.spread
                      ? ((root.spread.normTagLong || "")
                         + (root.spread.normCitation ? "\n\n" + root.spread.normCitation : ""))
                      : ""
    }
}
