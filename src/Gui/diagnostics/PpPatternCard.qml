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

// One pattern, as the session has it so far. Brief §4.2, top to bottom: name, the fresh
// tag, the state pill for THIS shot, the recurrence count, direction-or-dispersion, the
// run of ticks, trend + recency, one line of evidence prose.
//
// A ROW OF THE CARD, NOT A CARD IN IT. The panel's card is the frame; a pattern is one of its
// rows in the FAULTS idiom (HmWorkOns, design §13.7): a hairline above it, a target badge in
// the card's colorWarn leading the name (an easing arrow in colorGood when the session reads
// it as improving), the strength meter and the verdict in words at the right of the name line,
// the two figures in the data type under them.
//
// EVERY STRING IS THE MODEL'S. `recurrence` is a count over assessable shots and never a
// percentage; `directionText` is either the direction claim or the sentence saying why the
// claim is withheld, and this file cannot tell which and does not need to. The one thing
// decided here is which of the two lines gets dropped when the card is short — the evidence
// prose, per 12c ("cards lose their evidence sentence") — because that is a fact about the
// space, not about the evidence.

import QtQuick
import PinPointStudio

Item {
    id: root

    // One entry of SessionDiagnosticsModel::cards().
    property var card: null
    // The panel's fit scale. See PpSessionDiagnosticsBody._fitFor().
    property real fit: 1.0

    // Off on the auto-closing cast, where arming a tap on something about to vanish under the
    // pointer is a trap (PpSessionDiagnosticsWindow.interactive).
    property bool interactive: true

    // ── the focus contract (design §A6) ──────────────────────────────────────
    //
    // DESIGN-REVIEW: the affordance is MINIMAL by intent — a micro-label, and now the micro-label
    // ALONE. It used to be the whole card, and the whole card is what a user asked to open the
    // condition's causes and effects with; two verbs cannot share one hit target, so the six
    // characters that already said "FOCUS ▸" became the focus target and the card became the
    // drill-in. That is a reassignment of an existing affordance rather than a new one, and it
    // wants a design pass with the rest of the drill-in. What has ALSO not been designed is the
    // moment AFTER declaring: the split point is invisible on the run (the ticks before and after
    // the declaration look identical), and there is no way to see what the contract has bought
    // yet beyond a link quietly becoming Moved-together further down the rail.
    //
    // THE CARD ASKS AND THE MODEL DECIDES. Tapping publishes the request; declareFocus() /
    // clearFocus() live on SessionDiagnosticsModel and this file cannot reach one. `focused`
    // is read back off the model's own card map, so a tap that the model declined to honour
    // leaves the card exactly as it was — which is the only arrangement in which the accent
    // bar is a statement about the ledger rather than about a click.
    //
    // WHAT DECLARING FOCUS DOES, so the affordance is not mistaken for a filter: it records a
    // split at the current shot count, which is what puts the Moved-together link grade on
    // offer at all. It shapes ATTENTION and EXPERIMENT STRUCTURE. It is not an input to
    // detection, to a corridor or to a tier, and the model is what enforces that.
    readonly property bool focused: card ? card.focused === true : false
    signal focusToggled(string conditionId, bool nowFocused)

    // ── the drill-in (user request; brief §9 has no design for it) ───────────
    //
    // THE WHOLE CARD OPENS THE CONDITION. Same rule as the focus tap it replaces: the card ASKS
    // and the model decides — openDetail() lives on SessionDiagnosticsModel and this file cannot
    // reach one, so a request the model declines leaves the panel exactly as it was.
    signal detailRequested(string conditionId)

    // ── how far, in two figures (SessionDiagnosticsModel::spreadFor) ─────────
    //
    // THE CARD CARRIES NUMBERS, NOT CHARTS. The corridor strip and the value run were on every
    // card for one round and made a twelve-card row far too busy; they live in the condition
    // detail now (tap the card), where there is room to read them. What the card keeps of them is
    // the two figures a golfer actually compares between balls: where the session sits (its
    // MEDIAN — the same figure the detail's caption and the across-sessions columns quote, and
    // one outlier cannot drag it) and where THIS shot landed, coloured by its verdict.
    //
    // Every string is the model's (spread.medianNumber / currentNumber / currentState / unit);
    // "-" for a shot that was not measured, never 0. A card with no reading behind it — a
    // fixture, a condition measured on no shot — shows no figures at all.
    readonly property var _spread: (card && card.spread && (card.spread.placed || 0) > 0) ? card.spread : null

    // ── the after-shot pulse (§B3, §B8) ──────────────────────────────────────
    //
    // A CUE, not a channel: { token, ids, focusId }, republished by the body on every
    // shotIngested. One property so the ids can never arrive after the token that is supposed
    // to describe them. A card whose id is not in `ids` did not change on this swing and does
    // not move.
    property var pulseCue: null
    // The animation's own answer, so a test can assert that reduceMotion left nothing running
    // rather than assert on a duration constant.
    readonly property bool pulsing: pulseSeq.running
    property real _pulseT: 0.0

    // Design pixels through the app's type scale and the panel's fit — the panel's own
    // px(), repeated here so the card is usable on its own.
    function px(n) { return Math.round(n * Theme.fontScale * root.fit) }
    // The model spells the state pill in capitals (its C++ and tests own that spelling); the
    // shouted words go back to sentence case so Theme.caps sets the case, exactly as before in
    // the capital themes.
    function _unshout(s) {
        const t = String(s || "").replace(/\b[A-Z]{2,}\b/g, function (w) { return w.toLowerCase() })
        return t.charAt(0).toUpperCase() + t.slice(1)
    }

    readonly property int tzCaption: Math.max(1, Math.round(Theme.sp(8) * fit))
    readonly property int tzMicro:   Math.max(1, Math.round(Theme.fontSzMicro  * fit))
    readonly property int tzLabel:   Math.max(1, Math.round(Theme.fontSzLabel  * fit))
    readonly property int tzBody:    Math.max(1, Math.round(Theme.fontSzBody2  * fit))
    readonly property int tzData:    Math.max(1, Math.round(Theme.fontSzDataSm * fit))
    // The two figures' size: the heading token, a step above everything else on the card.
    readonly property int tzFigure:  Math.max(1, Math.round(Theme.fontSzHeading * 1.25 * fit))

    readonly property string _pillState: card ? (card.thisShot || "") : ""
    // A firing is a fault, colorWarn — never the alarm red (13.2).
    readonly property color _pillColor: _pillState === "fired" ? Theme.colorWarn
                                      : _pillState === "clean" ? Theme.colorGood
                                                               : Theme.colorText3
    readonly property color _trendColor: !card ? Theme.colorText3
                                       : card.trend === "worsening" ? Theme.colorWarn
                                       : card.trend === "improving" ? Theme.colorGood
                                                                    : Theme.colorText3
    readonly property bool _easing: card ? card.trend === "improving" : false

    // The badge, and the indent the row's words take from it (13.5).
    readonly property int _badge:  px(18)
    readonly property int _indent: _badge + px(10)

    objectName: "sdPatternCard"
    clip: true

    // ── the row carries the firing in its words ──────────────────────────────
    //
    // THE CARD ROW IS A DOZEN PATTERNS AND THE EYE NEEDS A WAY IN. It used to be a frame and a
    // wash in the strength colour round each card; as rows of the panel's card that would put a
    // box back round every pattern — and on a session where most of them fired on the shot being
    // read, a box round nearly every one, which emphasises nothing. So a firing is said where the
    // FAULTS row says it: FIRED HERE in the tone at the right of the name, the strength meter
    // beside it for how far, and this shot's figure in the tone. A clean row says CLEAN HERE in
    // colorGood; a row with no reading says so in grey, never louder than a finding.

    // The pulse lifts a wash in the accent on a shot that fired this condition; a focused row
    // holds a little of it. The two share one channel on purpose — a focused card that fired is
    // the headline of the moment (§B3) and should not need two marks to say so.
    Rectangle {
        objectName: "sdCardAccentWash"
        anchors.fill: parent
        color: Qt.alpha(Theme.colorAccent, Theme.dark ? 0.10 : 0.08)
        opacity: root.focused ? 0.35 + 0.65 * root._pulseT : root._pulseT
        visible: opacity > 0
    }

    // The hairline that opens the row (13.7).
    Rectangle {
        width: parent.width; height: 1
        color: Theme.colorBorder
    }

    // ── the focus bar ────────────────────────────────────────────────────────
    // The row's own rule, in the accent: a mark ON the row rather than a thing in it, and the
    // one place the declared focus stays visible at rest.
    Rectangle {
        objectName: "sdCardFocusBar"
        visible: root.focused
        width: parent.width
        height: Math.max(2, root.px(2))
        color: Theme.colorAccent
        opacity: 0.7 + 0.3 * root._pulseT
    }

    // ── the pulse ────────────────────────────────────────────────────────────
    //
    // ONE PASS, NOTHING MOVES. Opacity only: no size, no position, and emphatically no
    // reordering — the display order is hysteretic and model-side, and a card that slid to a new
    // seat while it flashed would be reporting a rank change the ledger did not make.
    //
    // reduceMotion IS NOT A SHORTER PULSE, it is no pulse: the stagger goes too, so nothing
    // is waiting to happen either. The card still redraws with its new pill, its new tick and
    // its NEW tag — the state change was never carried by the animation.
    SequentialAnimation {
        id: pulseSeq
        PauseAnimation { id: pulseWait; duration: 0 }
        NumberAnimation { target: root; property: "_pulseT"; to: 1.0
                          duration: Theme.durationFast; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "_pulseT"; to: 0.0
                          duration: Theme.durationSlow; easing.type: Easing.InQuad }
    }

    // A CARD CREATED AFTER THE SHOT MUST NOT REPLAY IT. The cue is a property, so a delegate
    // built later — a resize that changed how many fit, a stage that swapped the body — binds
    // to whatever cue is current and would flash for a swing it was not there for.
    property bool _ready: false
    Component.onCompleted: root._ready = true

    onPulseCueChanged: {
        if (!root._ready) return
        // STOPPED FIRST, ALWAYS. A new shot has landed, so whatever this card was saying about
        // the last one is over — including when this card is not in the new cue at all. A card
        // left flashing from the previous swing would be pointing at the wrong ball.
        pulseSeq.stop()
        root._pulseT = 0.0
        const cue = root.pulseCue
        const id  = root.card ? (root.card.id || "") : ""
        if (!cue || !cue.ids || id === "") return
        const slot = cue.ids.indexOf(id)
        if (slot < 0) return          // this condition did not change on this swing
        if (Theme.reduceMotion) return
        // Stagger in CARD ORDER, which is the model's order — the eye reads the change down
        // the row the way the swing runs rather than all at once.
        pulseWait.duration = slot * 70
        pulseSeq.start()
    }

    // THE WHOLE ROW IS THE DRILL-IN'S TARGET, and it is declared HERE — before the content —
    // rather than last. Declaration order is hit order in QtQuick: the focus tag's own MouseArea
    // lives inside the Column below and is therefore ON TOP of this one, so the six characters
    // that toggle focus keep their clicks and the rest of the row opens the condition. A
    // TapHandler could not do this — its default gesture policy takes a PASSIVE grab, so both
    // would fire on one press and every tap on FOCUS would also open the page.
    MouseArea {
        id: cardTap
        objectName: "sdCardTap"
        anchors.fill: parent
        enabled: root.interactive && !!root.card
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.detailRequested(root.card.id || "")
    }

    // The badge: a pattern is a fault to work on, so a target in the card's tone — or the easing
    // arrow where the session reads it as getting better. A shape each, so the tone is never
    // the only channel.
    PpBadge {
        objectName: "sdCardBadge"
        y: Math.round(col.y + nameText.height / 2 - height / 2)
        size: root._badge
        kind: root._easing ? "easing" : "target"
        tone: root._easing ? Theme.colorGood : Theme.colorWarn
    }

    Column {
        id: col
        anchors.fill: parent
        anchors.leftMargin:   root._indent
        anchors.topMargin:    root.px(10)
        anchors.bottomMargin: root.px(8)
        spacing: root.px(5)

        // ── name · NEW · meter · verdict ─────────────────────────────────────
        Item {
            width: col.width
            height: nameText.implicitHeight

            // ── where it sits in the row ─────────────────────────────────────
            //
            // FIRST WORD ON THE ROW, because it is the answer to the question the card row is
            // being read for. The order was already the panel's judgement about what matters
            // most; saying it out loud costs four characters and saves the golfer counting
            // rows. Equal scores share a rank and the badge says so with a trailing "=", which
            // is the one case where the row's position would lie about the model's opinion.
            PpMicro {
                id: rankTag
                objectName: "sdCardRank"
                anchors.left: parent.left
                anchors.baseline: nameText.baseline
                visible: text !== ""
                text: root.card ? (root.card.rankText || "") : ""
                font.pixelSize: root.tzMicro
                font.letterSpacing: Theme.trackingData
            }
            Text {
                id: nameText
                objectName: "sdCardName"
                anchors.left: rankTag.visible ? rankTag.right : parent.left
                anchors.leftMargin: rankTag.visible ? root.px(7) : 0
                // Past the NEW tag only while there is one — collapsing the tag's own width
                // instead would make its implicitWidth depend on its width.
                anchors.right: freshTag.visible ? freshTag.left
                             : meter.visible    ? meter.left
                                                : pill.left
                anchors.rightMargin: root.px(8)
                anchors.verticalCenter: parent.verticalCenter
                text: root.card ? root.card.name : ""
                elide: Text.ElideRight
                font.family: Theme.fontBody
                font.pixelSize: root.tzBody
                font.weight: Theme.fontBodyWeight
                // The row answers the pointer the way a FAULTS row does: its name lights.
                color: cardTap.containsMouse ? Theme.colorAccent : Theme.colorText
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
            PpMicro {
                id: freshTag
                objectName: "sdCardFresh"
                anchors.right: meter.visible ? meter.left : pill.left
                anchors.rightMargin: root.px(8)
                anchors.baseline: nameText.baseline
                visible: root.card ? root.card.fresh === true : false
                text: Theme.caps(qsTr("New"))
                font.pixelSize: root.tzMicro
                color: Theme.colorAccent
            }
            // HOW FAR OUT, immediately left of WHETHER it was out. The two belong in one
            // glance: the words are the verdict and the meter is its size.
            PpStrengthMeter {
                id: meter
                objectName: "sdCardStrength"
                anchors.right: pill.left
                anchors.rightMargin: root.px(8)
                anchors.verticalCenter: parent.verticalCenter
                level:   root.card ? (root.card.strength || 0) : 0
                known:   root.card ? root.card.strengthKnown === true : false
                caption: root.card ? (root.card.strengthText || "") : ""
                fit: root.fit
            }
            // The verdict for THIS shot, in words — FIRED HERE, CLEAN HERE, NOT MEASURED.
            Item {
                id: pill
                objectName: "sdStatePill"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width:  pillText.implicitWidth
                height: pillText.implicitHeight

                PpMicro {
                    id: pillText
                    text: root.card ? Theme.caps(root._unshout(root.card.statePill)) : ""
                    font.pixelSize: root.tzMicro
                    color: root._pillColor
                }
            }
        }

        // ── recurrence: a count over assessable shots, the thing being asserted (12b's note,
        //    which holds at every size).
        Text {
            id: recurrenceTxt
            objectName: "sdCardRecurrence"
            width: col.width - figures.reserve
            text: root.card ? (root.card.recurrence || "") : ""
            elide: Text.ElideRight
            font.family: Theme.fontData
            font.pixelSize: root.tzData
            color: Theme.colorText2
        }

        // ── what was read, and what it was tested against ────────────────────
        //
        // THE RECEIPT, and it is here because the row is the drill-in's doorway. The meter says
        // HOW FAR out this swing sat; this says out of WHAT, in the measure's own units, which is
        // the difference between a verdict and a reading.
        //
        // It outranks the direction prose in the drop order below: a sentence about the session
        // is what goes when the row is short, never the number this swing produced.
        Text {
            id: readingTxt
            // ⚠ EVERY OPTIONAL LINE PLACES ITSELF FROM THE VISIBLE LINES ABOVE IT, NEVER FROM
            // ITS OWN `y`. A Column does not reposition a hidden child, so a line that hid once
            // kept a stale y and measured its room from there, judged it had no room, and never
            // came back. `_top` is where the line WOULD go, from its predecessors' heights and
            // visibility only.
            readonly property real _top: recurrenceTxt.y + recurrenceTxt.height + col.spacing
            objectName: "sdCardReading"
            width: col.width - figures.reserve
            visible: text !== "" && col.height - _top - height >= run.height + trendRow.height + col.spacing
            text: {
                if (!root.card) return ""
                const v = root.card.valueText || ""
                const b = root.card.corridorText || ""
                return (v === "" || b === "") ? (v || b) : (v + "  " + b)
            }
            elide: Text.ElideRight
            font.family: Theme.fontData
            font.pixelSize: root.tzMicro
            color: root._pillState === "fired" ? Theme.colorWarn
                 : root._pillState === "clean" ? Theme.colorText2
                                               : Theme.colorText3
        }

        // ── direction, or the sentence saying the direction claim is withheld ─
        //
        // THE FIRST THING TO GO WHEN THE ROW IS SHORT, and it goes before the run does. Dropped
        // whole, never half-drawn: a tick run cut through the middle reads as a run of short
        // ticks, which is a claim about the session that the ledger never made.
        Text {
            id: directionTxt
            readonly property real _top: readingTxt.visible
                                         ? readingTxt._top + readingTxt.height + col.spacing
                                         : readingTxt._top
            objectName: "sdCardDirection"
            width: col.width - figures.reserve
            text: root.card ? (root.card.directionText || "") : ""
            visible: text !== ""
                     && col.height - _top - height
                        >= run.height + trendRow.height + 2 * col.spacing
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.family: Theme.fontBody
            font.pixelSize: root.tzLabel
            font.weight: Theme.fontBodyWeight
            color: Theme.colorText2
        }

        // ── the run ──────────────────────────────────────────────────────────
        // Last to go, and only when the row cannot hold it whole.
        PpTickRun {
            id: run
            readonly property real _top: directionTxt.visible
                                         ? directionTxt._top + directionTxt.height + col.spacing
                                         : directionTxt._top
            objectName: "sdTickRun"
            width: col.width
            visible: col.height - _top >= height
            ticks: root.card ? root.card.ticks : []
            fit: root.fit
        }

        // ── trend + recency, and the focus affordance ────────────────────────
        Item {
            id: trendRow
            readonly property real _top: run._top + run.height + col.spacing
            width: col.width
            // Never above the run it annotates: a trend arrow with no run under it is a claim
            // with its evidence removed.
            visible: run.visible && col.height - _top >= height
            height: Math.max(trendTxt.implicitHeight, focusTag.implicitHeight)

            Text {
                id: trendTxt
                objectName: "sdCardTrend"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.card ? ((root.card.trendArrow || "") + (root.card.trendText || "")) : ""
                font.family: Theme.fontData
                font.pixelSize: root.tzMicro
                color: root._trendColor
            }
            // IN REVIEW THE RECENCY SLOT CHANGES TENSE, not position. "last fired 2 measurable
            // shots ago" is a statement about now and means nothing on a finished session;
            // what the reader is asking is where the swing they picked sits in the run, which
            // is "3 more firings after this shot". The model publishes firingsAfterText only
            // while reviewing or closed, so its presence IS the tense.
            Text {
                objectName: "sdCardRecency"
                anchors.left: trendTxt.right
                anchors.leftMargin: root.px(8)
                anchors.right: focusTag.visible ? focusTag.left : parent.right
                anchors.rightMargin: focusTag.visible ? root.px(8) : 0
                anchors.verticalCenter: parent.verticalCenter
                text: root.card ? (root.card.firingsAfterText || root.card.recencyText || "") : ""
                elide: Text.ElideRight
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText3
            }
            // THE FOCUS CONTRACT, in the house micro-label style — a caret invites, a middot
            // reports. QUIET: shown on the row under the pointer, and always once it IS the
            // focus — a declared focus is state worth seeing at rest; the offer to make one is
            // not. Opacity rather than visibility so it keeps its place and its tap target.
            PpMicro {
                id: focusTag
                objectName: "sdCardFocusTag"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.interactive && !!root.card
                opacity: (root.focused || cardTap.containsMouse || focusTap.containsMouse) ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                text: root.focused ? Theme.caps(qsTr("Focused ·")) : Theme.caps(qsTr("Focus ▸"))
                font.pixelSize: root.tzCaption
                color: root.focused ? Theme.colorAccent
                                    : (focusTap.containsMouse ? Theme.colorAccent : Theme.colorText3)
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                // THE FOCUS TARGET, and the only one. Grown past the glyphs by a few pixels
                // because six characters at 8 px is not a thumb target — but not by so much that
                // it eats the recency line beside it, which is the row's, not the contract's.
                MouseArea {
                    id: focusTap
                    objectName: "sdCardFocusTap"
                    anchors.fill: parent
                    anchors.margins: -root.px(5)
                    enabled: root.interactive && !!root.card
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.focusToggled(root.card.id || "", !root.focused)
                }
            }
        }

        // ── one line of evidence prose ───────────────────────────────────────
        // Dropped, not shrunk, when the row is too short to hold it: an evidence sentence
        // clipped mid-clause reads as a different claim than the one the model made.
        Text {
            id: evidence
            objectName: "sdCardEvidence"
            width: col.width
            readonly property real _top: trendRow._top + trendRow.height + col.spacing
            height: Math.max(0, col.height - _top)
            visible: trendRow.visible && height >= root.tzMicro * lineHeight
            text: root.card ? (root.card.evidence || "") : ""
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            lineHeight: 1.4
            font.family: Theme.fontBody
            font.pixelSize: root.tzMicro
            font.weight: Theme.fontBodyWeight
            color: Theme.colorText3
        }
    }

    // ── the two figures ──────────────────────────────────────────────────────
    //
    // Right of the recurrence line, in the large data type. The width it RESERVES from the lines
    // beside it is its widest form, always — so dropping a caption for height never changes how
    // those lines wrap, and the two decisions cannot chase each other round a loop.
    //
    // WHEN THE ROW IS SHORT the captions go and the unit moves up beside the numbers, small, on
    // their baseline — the unit is never dropped and the numbers never shrink (the brief: "drop
    // the captions before shrinking the numbers"). The room is what lies between the recurrence
    // line's top and the run under it.
    Item {
        id: figures
        objectName: "sdCardFigures"
        visible: root._spread !== null
        readonly property real gap: root.px(10)
        readonly property real _stackedW: Math.max(medianNum.implicitWidth, medianCap.implicitWidth) + gap
                                          + Math.max(currentNum.implicitWidth, currentCap.implicitWidth)
        readonly property real _inlineW: medianNum.implicitWidth + gap + currentNum.implicitWidth
                                         + root.px(4) + unitTxt.implicitWidth
        readonly property real reserve: visible ? Math.max(_stackedW, _inlineW) + root.px(8) : 0
        readonly property real _numH: medianNum.implicitHeight
        readonly property real _capH: medianCap.implicitHeight
        readonly property real _room: (run.visible ? run._top - col.spacing : col.height) - recurrenceTxt.y
        readonly property bool showCaptions: _room >= _numH + 2 * _capH
        x: col.x + col.width - width
        y: col.y + recurrenceTxt.y
        width: showCaptions ? medianCol.width + gap + currentCol.width
                            : medianCol.width + gap + currentCol.width + root.px(4) + unitTxt.implicitWidth
        height: _numH + (showCaptions ? 2 * _capH : 0)

        Column {
            id: medianCol
            width: figures.showCaptions ? Math.max(medianNum.implicitWidth, medianCap.implicitWidth)
                                        : medianNum.implicitWidth
            Text {
                id: medianNum
                objectName: "sdCardMedian"
                anchors.right: parent.right
                text: root._spread ? (root._spread.medianNumber || "-") : ""
                font.family: Theme.fontData
                font.pixelSize: root.tzFigure
                color: Theme.colorText
            }
            Text {
                id: medianCap
                objectName: "sdCardMedianCaption"
                anchors.right: parent.right
                visible: figures.showCaptions
                text: qsTr("session median")
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText3
            }
        }
        Column {
            id: currentCol
            x: medianCol.width + figures.gap
            width: figures.showCaptions ? Math.max(currentNum.implicitWidth, currentCap.implicitWidth)
                                        : currentNum.implicitWidth
            Text {
                id: currentNum
                objectName: "sdCardCurrent"
                readonly property string state: root._spread ? (root._spread.currentState || "") : ""
                anchors.right: parent.right
                text: root._spread ? (root._spread.currentNumber || "-") : ""
                font.family: Theme.fontData
                font.pixelSize: root.tzFigure
                // The verdict's colour, as everywhere on the panel; the quiet grey for "-".
                color: state === "fired" ? Theme.colorWarn
                     : state === "clean" ? Theme.colorGood
                                         : Theme.colorText3
            }
            Text {
                id: currentCap
                objectName: "sdCardCurrentCaption"
                anchors.right: parent.right
                visible: figures.showCaptions
                text: qsTr("this shot")
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText3
            }
        }
        // The unit, once, for both figures: its own line under the captions, or — with no room
        // for that — small beside the second number, sitting on the numbers' baseline.
        Text {
            id: unitTxt
            objectName: "sdCardFiguresUnit"
            visible: text !== ""
            x: figures.showCaptions ? figures.width - width : currentCol.x + currentCol.width + root.px(4)
            y: figures.showCaptions ? figures._numH + figures._capH
                                    : currentNum.baselineOffset - baselineOffset
            width: figures.showCaptions ? Math.min(implicitWidth, figures.width) : implicitWidth
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            text: root._spread ? (root._spread.unit || "") : ""
            font.family: Theme.fontData
            font.pixelSize: root.tzCaption
            color: Theme.colorText3
        }
    }
}
