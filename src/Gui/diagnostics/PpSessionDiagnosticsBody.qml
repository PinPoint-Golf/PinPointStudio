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

// EVERYTHING THE SESSION DIAGNOSTICS PANEL LOOKS LIKE. Chrome, header, the after-shot
// strip, and the body the stage machine chooses — one component that MATURES from Cold to
// Closing rather than four panels that replace each other, because the golfer is watching
// one object accumulate evidence and a panel that swapped itself out would break that.
//
// IT TAKES A `source` AND OWNS NOTHING. Every string, every count and every tick state on
// this panel is read off one object shaped like SessionDiagnosticsModel; the wiring that
// produces one — the session directory, the shot processor, review — lives next door in
// PpSessionDiagnosticsPanel. That split is the same one PpLaunchMonitorPanel makes with
// PpLmGraphicsBody and it buys the same thing: this file can be loaded offscreen and
// pressed with a fixture, so the states that matter most (Cold with no expectations, the
// quiet strip, a not-assessable tick) are asserted rather than eyeballed. It also means
// there is exactly one answer to "where does this number come from" — `source` — and no
// second place for a zone to compute one.
//
// ── HOW IT SIZES ITSELF ──────────────────────────────────────────────────────────────
//
// The design is quoted at 1168 × 560, and the panel's homes are that stage, a 396-wide
// split beside another panel, and a 1920 second screen. So every metric on it is one scale
// factor `k` away from the design's own proportions, and _fitFor() takes the largest k at
// which the WHOLE design still fits both axes. k never drops below 1: below the design size
// the panel does not shrink, it REDUCES — `compact` drops the captions the narrow
// arrangement drops (12c) and the card row takes what fits and counts the rest. A
// recurrence count too small to read is not a smaller panel, it is a useless one, and this
// is the launch monitor board's rule applied to a second surface for the same reason.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: root

    // Anything with SessionDiagnosticsModel's read surface. Null draws the empty chrome.
    property var source: null

    // SessionDiagnosticsModel::shotReadout(selectedShotId), for the shot the carousel has
    // focused — null when nothing is selected. THE ONE THING THIS BODY TAKES THAT IS NOT A
    // PROPERTY OF `source`, and it is because shotReadout() is an INVOKABLE: it cannot be
    // bound, so somebody has to call it and re-call it when the selection or the surface
    // changes. That somebody is the panel, which is where the model's signals are (see
    // PpSessionDiagnosticsPanel). A test hands a fixture map in directly, exactly as it hands
    // `source` in — the body still computes nothing.
    property var readout: null

    // Panel-local, and per brief §8 the only state this panel owns beyond the carousel's
    // selection and the cadence setting. Two collapsed regions, both of which open into the
    // SAME content rather than a different view: Watching (n) is the one that remains, now
    // that the collapsed chain rows have gone with the rail. (The miss picker below is a
    // second, and it is a transient rather than a state: it is open only while it is being
    // answered.)
    property bool watchingExpanded: false

    // Off on the auto-closing cast, where every affordance on the panel is a trap: the
    // surface is a glance and it is about to vanish under the pointer
    // (PpSessionDiagnosticsWindow.interactive). Nothing about what the panel SAYS changes.
    property bool interactive: true

    // ── the two declarations the golfer can make (design §A6) ────────────────
    //
    // BOTH GO STRAIGHT TO THE MODEL AND NEITHER IS MIRRORED HERE. declareFocus, clearFocus and
    // declareMiss are the model's, the read-back is the model's (`focused` on each card,
    // `declaredMiss` on the surface), and this file holds no copy that could disagree with it.
    // That is what makes a declined declaration — the model is reviewing, the id is not in the
    // pack — leave the panel exactly as it was rather than showing a state the ledger is not in.
    //
    // GUARDED ON THE FUNCTION, not on the source being non-null: the offscreen test presses
    // this body with a plain fixture object, and a fixture that wants to assert the tap
    // arrived supplies a spy of the same name. A fixture that does not gets a no-op instead of
    // a TypeError that would fail an unrelated assertion three tests later.
    function _declareFocus(conditionId, on) {
        if (!source) return
        if (on && typeof source.declareFocus === "function")
            source.declareFocus(conditionId)
        else if (!on && typeof source.clearFocus === "function")
            source.clearFocus()
    }
    function _declareMiss(missId) {
        if (source && typeof source.declareMiss === "function")
            source.declareMiss(missId)
        missPicker.close()
    }
    function _missCandidates() {
        if (source && typeof source.missCandidates === "function")
            return source.missCandidates() || []
        return []
    }

    // ── the condition detail (user request; brief §9 has no design for it) ───
    //
    // A BODY SWAP, NOT A LOADER AND NOT A STACK. The composition behind the detail is HIDDEN,
    // never torn down, which is what makes BACK a restoration rather than a rebuild: the
    // watching row comes back expanded if it was, chain B comes back open if it was, the cards
    // come back in the order hystereticOrder() had them. A Loader would rebuild every delegate
    // on the way back and quietly reset all three, and would also re-run the pulse machinery on
    // cards that were not there for the shot (see PpPatternCard's `_ready`).
    //
    // ONE STEP BACK, ALWAYS. Opening a cause or an effect from inside the detail RE-TARGETS this
    // page; BACK still returns to the panel. That is a deliberate simplification of the obvious
    // alternative — a breadcrumb stack — because the panel is a glance between balls and a
    // reader four levels deep with no way to see where they are is worse than a reader who has
    // to tap the card again.
    //
    // GUARDED ON THE FUNCTION, exactly as the two declarations are: the offscreen test presses
    // this body with a plain fixture, and a fixture that wants to assert the tap arrived supplies
    // a spy of the same name. One that does not gets a no-op rather than a TypeError.
    function _openDetail(conditionId) {
        if (!conditionId) return
        if (source && typeof source.openDetail === "function")
            source.openDetail(conditionId)
    }
    function _closeDetail() {
        if (source && typeof source.closeDetail === "function")
            source.closeDetail()
    }

    // READ BACK OFF THE MODEL, never remembered here — the same rule as `focused` and
    // `declaredMiss`. A request the model declined leaves the panel exactly as it was.
    readonly property string detailConditionId:
        source ? (source.detailConditionId || "") : ""
    readonly property var detail: source ? source.detail : null
    readonly property bool detailOpen: detailConditionId !== "" && !!detail && !!detail.header

    // ── the after-shot pulse (§B3, §B8) ──────────────────────────────────────
    //
    // ONE CUE OBJECT, PUBLISHED DOWNWARD: { token, ids, focusId }. Single-property so a card
    // can never see ids that belong to a different shot, and a token so the same set of ids
    // firing twice in a row is still two events.
    //
    // CALLED, NOT BOUND. shotIngested is a SIGNAL on SessionDiagnosticsModel and this file has
    // no model — the panel next door does, and it calls this when one lands, exactly as it
    // calls shotReadout() and hands the result down. It is also the seam the offscreen test
    // uses: a synthetic after-shot moment is a fixture plus this call, with no event loop and
    // no ingest in it.
    property var pulseCue: null
    property int _pulseToken: 0

    function pulseAfterShot() {
        const d = source ? source.afterShotDelta : null
        const fired = (d && d.fired) ? d.fired : []
        if (fired.length === 0) { root.pulseCue = null; return }

        const wanted = {}
        for (let i = 0; i < fired.length; ++i) wanted[fired[i]] = true

        // ORDER IS THE PANEL'S DRAWING ORDER, never the delta's. `fired` comes out in the
        // shot's own row order; what the eye is following is the sweep down the arrangement in
        // front of it, and a stagger that ran in some third order would read as a shuffle.
        // So the body on screen decides: the rail's nodes when the rail is the body, the card
        // row's cards when it is not, and whatever fired that the chosen body does not draw
        // brings up the rear rather than being dropped — it still happened.
        const ids = []
        function take(id) {
            if (id && wanted[id] && ids.indexOf(id) < 0) ids.push(id)
        }
        function takeCards() {
            const cs = root.cards || []
            for (let i = 0; i < cs.length; ++i) take(cs[i].id)
        }
        function takeRail() {
            const chs = root.chains || []
            for (let i = 0; i < chs.length; ++i) {
                const ns = chs[i].nodes || []
                for (let j = 0; j < ns.length; ++j) take(ns[j].id)
            }
        }
        // The card row is the body at every stage now, so it leads the sweep; anything that
        // fired and is only on a rail (which is to say, only inside a drill-in) brings up the
        // rear rather than being dropped — it still happened.
        takeCards(); takeRail()
        for (let i = 0; i < fired.length; ++i) take(fired[i])

        // THE FOCUSED NODE LEADS. Under a focus contract its marker is the headline of the
        // moment (§B3), and leading the stagger is how a sweep says so without a second mark:
        // it is the one that moves first and the rest follow it.
        const focusId = d.focusId || ""
        const at = ids.indexOf(focusId)
        if (at > 0) { ids.splice(at, 1); ids.unshift(focusId) }

        root._pulseToken += 1
        root.pulseCue = { token: root._pulseToken, ids: ids, focusId: focusId }
    }

    // A screen was asked for, from the driver footer's CTA or from a screened root on the
    // rail. `screenRef` is the model's when it recommended that screen and empty when it did
    // not; `conditionId` always names what the screen would settle, so a host can route on
    // whichever it has. THIS PANEL RUNS NO SCREEN: the protocol UI is not designed yet
    // (brief §9) and inventing one here would be inventing a design.
    signal screenRequested(string screenRef, string conditionId)

    objectName: "sdBody"

    radius: Theme.radius
    color: Theme.colorBg2
    border.width: 1
    border.color: Theme.colorBorderMid
    clip: true

    // ── the design's own proportions, at k = 1 ───────────────────────────────
    readonly property int baseW: 1168
    readonly property int baseH: 560
    // 12b doubles the type at 1920; past ~2.4 the panel is being read from further away
    // than it was drawn for and more scale stops buying legibility.
    readonly property real kMax: 2.4
    // Below this the wide arrangement gives way to 12c's reductions.
    readonly property int baseCompactW: 640

    function _fitFor(w, h) {
        if (w <= 0 || h <= 0)
            return 1
        const kw = w / (baseW * Theme.fontScale)
        const kh = h / (baseH * Theme.fontScale)
        return Math.max(1, Math.min(kMax, Math.min(kw, kh)))
    }

    readonly property real k: _fitFor(width, height)
    readonly property bool compact: width < Math.round(baseCompactW * Theme.fontScale)

    // A design pixel, through the app's type scale and then the panel's fit. The one place
    // a number from the mock is allowed to appear.
    function px(n) { return Math.round(n * Theme.fontScale * root.k) }

    // The mock's 8–15 px type, on the Theme scale, at the panel's fit. fontSzMicro is the
    // smallest token there is, so the 8 px captions go through Theme.sp() — the same
    // fontScale, one step below the scale's floor.
    readonly property int tzCaption: Math.max(1, Math.round(Theme.sp(8) * k))
    readonly property int tzMicro:   Math.max(1, Math.round(Theme.fontSzMicro   * k))
    readonly property int tzLabel:   Math.max(1, Math.round(Theme.fontSzLabel   * k))
    readonly property int tzBody:    Math.max(1, Math.round(Theme.fontSzBody2   * k))
    readonly property int tzData:    Math.max(1, Math.round(Theme.fontSzDataSm  * k))
    readonly property int tzHead:    Math.max(1, Math.round(Theme.fontSzHeading * k))

    // ── the source, read once ────────────────────────────────────────────────
    readonly property var header:       source ? source.headerInfo    : null
    readonly property string stage:     source ? (source.stage || "") : ""
    readonly property var cards:        source ? source.cards         : []
    readonly property var expectations: source ? source.expectations  : []
    readonly property var bookends:     source ? source.bookends      : []
    readonly property var chains:       source ? source.chains        : []
    readonly property var driver:       source ? source.driver        : null

    readonly property bool isCold:      stage === "cold"
    readonly property bool isForming:   stage === "forming"
    readonly property bool isEstablished: stage === "established"
    readonly property bool isClosing:   stage === "closing"

    // ── the declared miss ────────────────────────────────────────────────────
    //
    // DESIGN-REVIEW: minimal by intent. The chip asks and the picker answers, and neither
    // says anything about what the declaration DID — `missChain` is published (everything
    // authored upstream of the outcome, pre-armed) and nothing on this panel draws it yet,
    // so the golfer declares a miss and sees the chip change and nothing else. There is also
    // no verification tense: the model checks the declaration against launch-monitor data
    // where the session has any, and the panel does not report the answer. Both want a
    // design pass.
    //
    // The session's opening question — "what is the bad shot?" — and the panel ASKS it only
    // while the answer can still shape the session: Cold and Forming, when there is not yet a
    // picture to read. Past that the invitation stands down and the chip reports whatever was
    // declared, because a declaration is a fact about the session and does not expire with the
    // stage that took it.
    readonly property string declaredMiss:     source ? (source.declaredMiss || "")     : ""
    readonly property string declaredMissName: source ? (source.declaredMissName || "") : ""
    readonly property bool _missInvited:
        interactive && declaredMiss === "" && (isCold || isForming) && !reviewing

    // ── review (13a, brief §6) ───────────────────────────────────────────────
    // READ OFF THE PUBLISHED SURFACE, never off a controller: the model is the one that
    // decides the panel is in review (it also freezes the stage at Closing when it is), and a
    // body that asked something else could disagree with the counts it is drawing. The badge
    // string is non-empty exactly when the model is reviewing a ledger with shots in it.
    readonly property bool reviewing:
        !!header && (header.reviewBadge || "") !== ""
    // ...and a SHOT IS BEING READ only once the carousel has focused one. Review without a
    // selection is the finished session's own summary — bookends and all — because the panel
    // holds the final state and selection is what enters shot-reading (brief §6).
    readonly property bool reviewingShot:
        reviewing && !!readout && !!readout.conditions && readout.conditions.length > 0

    // ── how many cards fit, and how many are left over ───────────────────────
    // The MODEL decides which cards come first (hystereticOrder), so taking a prefix is a
    // decision about space and never about importance. What does not fit is COUNTED rather
    // than dropped silently — a pattern that scrolled off the end still happened.
    readonly property int baseCardMinW: 300
    readonly property int _cardGap: px(8)
    readonly property int _cardCols: {
        if (width <= 0) return 1
        const avail = width - 2 * px(10)
        return Math.max(1, Math.floor((avail + _cardGap) / (px(baseCardMinW) + _cardGap)))
    }
    // ⚠ THE CARD ROW IS A GRID NOW, and that follows from the rail leaving the front page.
    // One row of three was the right answer while the rail was the body and the cards were the
    // Forming stage's preface to it; with the observed faults promoted to the front page, a
    // single row showed three of a real session's twelve and counted the other nine in 8 px
    // type — the panel's whole middle left empty under them. The cards take the height the rail
    // used to, and "+N more" now means the session genuinely has more than the panel can hold.
    readonly property int _cardH: px(150)
    readonly property int _cardRows: {
        if (_cardsAvailH <= 0) return 1
        return Math.max(1, Math.floor((_cardsAvailH + _cardGap) / (_cardH + _cardGap)))
    }
    // What the body has left for cards once the SESSION PICTURE header is out of it. Bound to
    // the body's own height rather than measured off the Grid, which would close a loop.
    property int _cardsAvailH: 0
    readonly property int _cardsShown: {
        const n = cards ? cards.length : 0
        if (n <= 0 || width <= 0) return 0
        return Math.min(n, _cardCols * _cardRows)
    }
    readonly property int _cardsHidden: (cards ? cards.length : 0) - _cardsShown

    // ── how many bookends the closing row draws ──────────────────────────────
    // Same decision as the card row's and for the same reason, arrived at the hard way: the
    // row divided its width between EVERY pattern, so a twelve-pattern session got twelve
    // cells of about 160 px holding three lines of 8 px type, and every one of them elided
    // into "most repr…". A cell that cannot be read is not a disclosure. The model orders the
    // bookends as it orders everything else, so a prefix is a decision about space.
    readonly property int _bookendMinW: 190
    readonly property int _bookendsShown: {
        const n = bookends ? bookends.length : 0
        if (n <= 0 || width <= 0) return 0
        const avail = width - 2 * px(10) - px(90)      // the row's margins and its label
        return Math.max(1, Math.min(n, Math.floor(avail / px(_bookendMinW))))
    }
    readonly property int _bookendsHidden: (bookends ? bookends.length : 0) - _bookendsShown

    // driver.screenConditionId / screenRef — the only place the published surface carries a
    // screen ref for the screened-root node the rail draws.
    readonly property string _screenConditionId: driver ? (driver.screenConditionId || "") : ""
    readonly property string _screenRef:         driver ? (driver.screenRef || "") : ""

    // The footer exists in Established and Closing, which is where the mock has it.
    readonly property bool _hasDriverFooter: (isEstablished || isClosing) && !!driver
    // ...but it only carries the coverage line in the wide arrangement with a driver to sit
    // beside. A waiting footer and 12c's three-line footer both drop the right-hand column,
    // and the coverage line is never dropped with it (brief §1) — it goes back to the bottom.
    readonly property bool _footerCarriesCoverage:
        _hasDriverFooter && !compact && driver.eligible === true

    // 12c's collapsed chain, from the model's own node names — the arrow and the separator are
    // the only things composed here, the same contribution PpWatchingRow makes to its line.
    function _chainSummary(c) {
        if (!c || !c.nodes) return ""
        const parts = []
        for (let i = 0; i < c.nodes.length; ++i)
            parts.push(c.nodes[i].name || "")
        return parts.join(" → ")
    }
    // ...and the marks, which are what make a collapsed chain B worth opening: it is mostly
    // ghosts and a screened root, and the summary must not hide that.
    function _chainNote(c) {
        if (!c || !c.nodes) return ""
        const parts = []
        for (let i = 0; i < c.nodes.length; ++i)
            if (c.nodes[i].mark)
                parts.push(c.nodes[i].mark)
        return parts.join(" · ")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin:   root.px(10)
        anchors.rightMargin:  root.px(10)
        anchors.bottomMargin: root.px(10)
        anchors.topMargin:    0
        spacing: root.px(8)

        // ── header ───────────────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.px(34)

            Row {
                anchors.left: parent.left
                anchors.leftMargin: root.px(2)
                // Anchored past the cadence note only while there IS one. Collapsing the
                // note's own width instead would make its implicitWidth depend on its width.
                anchors.right: cadenceNote.visible ? cadenceNote.left : parent.right
                anchors.rightMargin: root.px(9)
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.px(9)

                // ── back, out of the condition detail ────────────────────────
                // LEFT OF THE PANEL'S OWN NAME, in the house mono micro-chip: the panel is still
                // the panel, and this is the one control that says the body under it is a
                // detour. Drawn in the accent because it is the only thing on the header a tap
                // does something with.
                Rectangle {
                    id: backChip
                    objectName: "sdDetailBack"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.detailOpen
                    width:  backText.implicitWidth + root.px(12)
                    height: backText.implicitHeight + root.px(4)
                    radius: Math.max(1, root.px(3))
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g,
                                          Theme.colorAccent.b, backMouse.containsMouse ? 0.55 : 0.30)
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    Text {
                        id: backText
                        anchors.centerIn: parent
                        text: qsTr("◂ BACK")
                        font.family: Theme.fontData
                        font.pixelSize: root.tzCaption
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorAccent
                    }

                    MouseArea {
                        id: backMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root._closeDetail()
                    }
                }

                Text {
                    objectName: "sdTitle"
                    anchors.verticalCenter: parent.verticalCenter
                    // 12c abbreviates rather than eliding: the panel's own name is the last
                    // thing that should be half a word.
                    text: root.compact ? qsTr("SESSION DIAG.") : qsTr("SESSION DIAGNOSTICS")
                    font.family: Theme.fontData
                    font.pixelSize: root.tzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText2
                }
                Text {
                    objectName: "sdShotLabel"
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.header ? (root.header.shotLabel || "") : ""
                    font.family: Theme.fontData
                    font.pixelSize: root.tzMicro
                    color: Theme.colorText3
                }
                Rectangle {
                    objectName: "sdStageChip"
                    anchors.verticalCenter: parent.verticalCenter
                    width:  stageText.implicitWidth + root.px(14)
                    height: stageText.implicitHeight + root.px(4)
                    radius: Math.max(1, root.px(3))
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.colorBorderMid
                    visible: stageText.text !== ""

                    Text {
                        id: stageText
                        anchors.centerIn: parent
                        text: root.header ? (root.header.stageLabel || "") : ""
                        font.family: Theme.fontData
                        font.pixelSize: root.tzCaption
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText
                    }
                }
                // ── the tense ────────────────────────────────────────────────
                // REVIEWING · shot 9 of 14, framed in the accent at ~35% and lettered in it.
                // It sits beside the stage chip rather than replacing it because they say
                // different things — the stage is what the ledger matured to, the badge is
                // which tense the panel is being read in.
                Rectangle {
                    objectName: "sdReviewBadge"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: reviewBadgeText.text !== ""
                    width:  reviewBadgeText.implicitWidth + root.px(14)
                    height: reviewBadgeText.implicitHeight + root.px(4)
                    radius: Math.max(1, root.px(3))
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g,
                                          Theme.colorAccent.b, 0.35)

                    Text {
                        id: reviewBadgeText
                        anchors.centerIn: parent
                        text: root.header ? (root.header.reviewBadge || "") : ""
                        font.family: Theme.fontData
                        font.pixelSize: root.tzCaption
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorAccent
                    }
                }
                // ── the declared miss ────────────────────────────────────────
                // A chip, in the header, beside the stage — because it is a statement about
                // the SESSION and not about a pattern, and because it is the one thing on
                // this panel the golfer supplies rather than the model. It is deliberately
                // NOT tinted like a finding: intent shapes attention and pre-arms chains, and
                // it is never evidence (§A6). The accent is the app's "you can act here"
                // colour, which is exactly what it is.
                // The condition the detail is open on, named in the header — because the body
                // below it is one condition's page and the panel's own title no longer says
                // what is on screen.
                Text {
                    objectName: "sdDetailTitle"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.detailOpen
                    text: root.detail ? (root.detail.name || "") : ""
                    elide: Text.ElideRight
                    font.family: Theme.fontBody
                    font.pixelSize: root.tzLabel
                    font.weight: Theme.fontBodyWeight
                    color: Theme.colorText
                }

                Rectangle {
                    id: missChip
                    objectName: "sdMissChip"
                    anchors.verticalCenter: parent.verticalCenter
                    // The declaration is about the SESSION, and the detail is about one
                    // condition. Asking "what is the bad shot?" over a page that is not the
                    // session picture is an invitation with nowhere to land.
                    visible: !root.detailOpen && (root._missInvited || root.declaredMiss !== "")
                    width:  missText.implicitWidth + root.px(14)
                    height: missText.implicitHeight + root.px(4)
                    radius: Math.max(1, root.px(3))
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g,
                                          Theme.colorAccent.b,
                                          missHover.hovered ? 0.55 : 0.30)
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    Text {
                        id: missText
                        anchors.centerIn: parent
                        // The name once there is one, and the invitation until then. The
                        // caret says it opens something; the middot says it is settled and
                        // can still be changed.
                        text: root.declaredMiss !== ""
                              ? qsTr("MISS · %1").arg(root.declaredMissName || root.declaredMiss)
                                + (root.interactive ? qsTr(" ▸") : "")
                              : qsTr("DECLARE MISS ▸")
                        font.family: Theme.fontData
                        font.pixelSize: root.tzCaption
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorAccent
                    }

                    HoverHandler { id: missHover; enabled: root.interactive
                                   cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: root.interactive
                        onTapped: missPicker.openBelow(missChip)
                    }
                }

                Text {
                    objectName: "sdReviewNote"
                    anchors.verticalCenter: parent.verticalCenter
                    // "final session state · this shot read inside the finished ledger" — the
                    // sentence that stops the counts beside it being read as this shot's.
                    visible: !root.compact && text !== ""
                    text: root.header ? (root.header.reviewNote || "") : ""
                    font.family: Theme.fontData
                    font.pixelSize: root.tzMicro
                    color: Theme.colorText2
                }
                Text {
                    objectName: "sdStageNote"
                    anchors.verticalCenter: parent.verticalCenter
                    // In review the count line moves to the right-hand end (13a), where it
                    // reads as the session's total rather than as a note on the stage.
                    visible: !root.compact && !root.reviewing && text !== ""
                    text: root.header ? (root.header.countLine || "") : ""
                    font.family: Theme.fontData
                    font.pixelSize: root.tzMicro
                    color: Theme.colorText2
                }
            }

            Text {
                id: cadenceNote
                objectName: "sdCadenceNote"
                anchors.right: parent.right
                anchors.rightMargin: root.px(2)
                anchors.verticalCenter: parent.verticalCenter
                // The right-hand end carries ONE of them, and never both: cadence is a live
                // statement (there is no cadence gating in review, brief §6) and the count
                // line is the reviewed session's total. So the slot changes tense with the
                // panel instead of stacking two captions nobody asked to compare.
                visible: !root.compact && text !== ""
                text: root.header
                      ? (root.reviewing ? (root.header.countLine || "")
                                        : (root.header.cadenceNote || ""))
                      : ""
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText3
            }
        }

        // ── one condition, drilled into ──────────────────────────────────────
        // Takes the whole body when it is open: the strips above it and the footers below it
        // are the session picture's, and this page is one condition's. The composition is still
        // there, hidden, and BACK is a visibility change.
        PpConditionDetail {
            objectName: "sdDetailBody"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.detailOpen
            detail: root.detail
            fit: root.k
            compact: root.compact
            interactive: root.interactive
            screenConditionId: root._screenConditionId
            screenRef:         root._screenRef
            onScreenRequested: (ref, cond) => root.screenRequested(ref, cond)
            onFocusToggled:    (id, on) => root._declareFocus(id, on)
            // Re-targets the page in place; BACK still returns to the panel in one step.
            onConditionActivated: (id) => root._openDetail(id)
            onCloseRequested: root._closeDetail()
        }

        // ── the after-shot strip, the bookends, or the reviewed shot ─────────
        // ONE SLOT, THREE TENSES. A live session reports the moment after the swing; a closed
        // one has no after-shot moment, so the strip that reported one is REPLACED rather than
        // emptied; and a closed one with a shot picked off the carousel reports that shot,
        // read inside the finished ledger. They are the same slot because they are the same
        // question — "what does this panel have to say about a swing" — asked in three tenses.
        PpThisShotStrip {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: !root.detailOpen && !root.isClosing && !root.reviewingShot
            chips:   root.source ? root.source.thisShot : []
            delta:   root.source ? root.source.afterShotDelta : null
            quiet:   root.source ? root.source.quiet === true : false
            fit:     root.k
            compact: root.compact
        }

        PpReviewShotStrip {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: !root.detailOpen && root.reviewingShot
            readout: root.readout
            fit:     root.k
            compact: root.compact
            // The strip bounds ITSELF to the mock's two-row band and scrolls inside it, so this
            // is a backstop rather than the working limit: whatever the strip asks for — a
            // taller cell at a large font scale, the tail opened on a thirty-condition set — it
            // never takes half the panel away from the body it is the preface to.
            maxHeight: Math.round(root.height * 0.45)
        }

        Rectangle {
            objectName: "sdBookends"
            Layout.fillWidth: true
            Layout.preferredHeight: root.px(56)
            visible: !root.detailOpen && root.isClosing && !root.reviewingShot
            color: Theme.colorSurface
            radius: Theme.radius
            border.width: 1
            border.color: Theme.colorBorderMid
            clip: true

            Row {
                id: bookendsRow
                anchors.fill: parent
                anchors.leftMargin:   root.px(10)
                anchors.rightMargin:  root.px(10)
                anchors.topMargin:    root.px(7)
                anchors.bottomMargin: root.px(7)
                spacing: root.px(14)

                Text {
                    id: bookendsLabel
                    anchors.verticalCenter: parent.verticalCenter
                    // The tail is COUNTED in the label rather than dropped silently — the same
                    // rule the card row's "+N more" keeps. A pattern whose bookends did not fit
                    // still had a worst swing and a best one.
                    text: root._bookendsHidden > 0
                          ? qsTr("SESSION\nBOOKENDS · +%1").arg(root._bookendsHidden)
                          : qsTr("SESSION\nBOOKENDS")
                    font.family: Theme.fontData
                    font.pixelSize: root.tzCaption
                    font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText2
                }

                Repeater {
                    model: root._bookendsShown

                    Item {
                        required property int index
                        readonly property var modelData: root.bookends[index]
                        objectName: "sdBookend"

                        // Even shares of what is left after the label and the gaps. Named
                        // through the Row's id: a Repeater delegate's `parent` is the Row at
                        // run time but the Repeater to anything reading the file.
                        readonly property int _n: root._bookendsShown
                        width: Math.max(0, (bookendsRow.width - bookendsLabel.width
                                            - bookendsRow.spacing * _n) / Math.max(1, _n))
                        height: bookendsRow.height

                        // ⚠ A BOOKEND OPENS ITS CONDITION, exactly as a card does.
                        //
                        // It carried a "▸" from the day it was drawn and did nothing when it was
                        // pressed, which is worse than having no caret at all: the closing row
                        // names conditions the card row may never have shown — the ones that
                        // ranked below the fold — so it is often the only place a golfer meets
                        // them, and it was the one place they could not follow one up.
                        //
                        // Same verb, same target, same destination: the card ASKS and the model
                        // decides (_openDetail), and the page replaces the body in the middle of
                        // the frame the way it does from anywhere else. A second route to one
                        // page, not a second page.
                        MouseArea {
                            id: bookendTap
                            objectName: "sdBookendTap"
                            anchors.fill: parent
                            enabled: root.interactive && !!modelData && !!modelData.id
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root._openDetail(modelData.id || "")
                        }

                        Rectangle {
                            anchors.left: parent.left
                            width: 1
                            height: parent.height
                            color: Theme.colorBorderMid
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: root.px(12)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: root.px(2)

                            Text {
                                objectName: "sdBookendName"
                                width: parent.width
                                text: (modelData.name || "") + " ▸"
                                elide: Text.ElideRight
                                font.family: Theme.fontData
                                font.pixelSize: root.tzCaption
                                font.letterSpacing: Theme.trackingLabel
                                // Lit on hover like the card's TRACE caret, so the affordance
                                // answers the pointer rather than only claiming to.
                                color: bookendTap.containsMouse ? Theme.colorText
                                                                : Theme.colorAccent
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }
                            Text {
                                width: parent.width
                                text: modelData.representativeText || ""
                                elide: Text.ElideRight
                                font.family: Theme.fontData
                                font.pixelSize: root.tzMicro
                                color: Theme.colorText
                            }
                            Text {
                                width: parent.width
                                text: (modelData.worstText || "") + " · " + (modelData.bestText || "")
                                elide: Text.ElideRight
                                font.family: Theme.fontData
                                font.pixelSize: root.tzCaption
                                color: Theme.colorText3
                            }
                        }
                    }
                }
            }

        }

        // ── the body the stage machine chooses ───────────────────────────────
        Item {
            id: stageBody
            Layout.fillWidth: true
            Layout.fillHeight: true
            // THE SWAP. Hidden, never torn down — see root._openDetail().
            visible: !root.detailOpen

            // ── Cold ─────────────────────────────────────────────────────────
            Rectangle {
                objectName: "sdColdBody"
                anchors.fill: parent
                visible: root.isCold
                color: Theme.colorSurface
                radius: Theme.radius
                border.width: 1
                border.color: Theme.colorBorderMid
                clip: true

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin:  root.px(18)
                    anchors.rightMargin: root.px(18)
                    anchors.topMargin:   root.px(16)
                    spacing: root.px(14)

                    Column {
                        width: parent.width
                        spacing: root.px(5)

                        Text {
                            objectName: "sdColdHeadline"
                            width: parent.width
                            text: root.header ? (root.header.coldLine || "") : ""
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontBody
                            font.pixelSize: root.tzHead
                            font.weight: Theme.fontBodyWeight
                            color: Theme.colorText
                        }
                        Text {
                            objectName: "sdColdSubline"
                            width: parent.width
                            text: root.header ? (root.header.countLine || "") : ""
                            elide: Text.ElideRight
                            font.family: Theme.fontData
                            font.pixelSize: root.tzMicro
                            color: Theme.colorText2
                        }
                    }

                    // USUALLY YOURS. Omitted ENTIRELY when the athlete has no fault profile
                    // yet — an empty "expectations" heading over nothing would read as the
                    // model having looked and found none, which is a different claim.
                    Column {
                        objectName: "sdExpectations"
                        width: parent.width
                        spacing: root.px(7)
                        visible: root.expectations && root.expectations.length > 0

                        Text {
                            text: qsTr("USUALLY YOURS · EXPECTATIONS TO TEST, NOT FINDINGS")
                            font.family: Theme.fontData
                            font.pixelSize: root.tzCaption
                            font.letterSpacing: Theme.trackingMicro
                            color: Theme.colorText3
                        }

                        Flow {
                            width: parent.width
                            spacing: root.px(8)

                            Repeater {
                                model: root.expectations

                                Item {
                                    required property var modelData
                                    objectName: "sdExpectationCard"

                                    // Design width, but never wider than the panel — in the
                                    // narrow arrangement the cards become one column.
                                    width: Math.min(root.px(250), parent.width)
                                    height: expectCol.implicitHeight + 2 * root.px(9)

                                    // Dashed, and that is the whole point: this is the one
                                    // thing on the panel that is not evidence from this
                                    // session (brief §3.3).
                                    PpDashedFrame {
                                        anchors.fill: parent
                                        frameRadius: Theme.radius
                                        strokeColor: Theme.colorBorderMid
                                        dashOn:  Math.max(1, root.px(3))
                                        dashOff: Math.max(1, root.px(3))
                                    }

                                    Column {
                                        id: expectCol
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin:  root.px(11)
                                        anchors.rightMargin: root.px(11)
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: root.px(3)

                                        Text {
                                            width: parent.width
                                            text: modelData.name || ""
                                            elide: Text.ElideRight
                                            font.family: Theme.fontBody
                                            font.pixelSize: root.tzBody
                                            font.weight: Theme.fontBodyWeight
                                            color: Theme.colorText2
                                        }
                                        Text {
                                            width: parent.width
                                            text: modelData.text || ""
                                            elide: Text.ElideRight
                                            font.family: Theme.fontData
                                            font.pixelSize: root.tzMicro
                                            color: Theme.colorText3
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // The sentence that has to stay true in the implementation as well as on
                    // the screen (brief §3.3, §5.5). It is here because the profile is on
                    // screen; it is true because profileBias() has two call sites.
                    Text {
                        width: parent.width
                        visible: root.expectations && root.expectations.length > 0
                        text: qsTr("from your fault profile · biases ranking and presentation only, never firing or corridors")
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontData
                        font.pixelSize: root.tzCaption
                        color: Theme.colorText3
                    }
                }
            }

            // ── Forming, and a Closing session the model drew no chain for ───
            Item {
                objectName: "sdCardsBody"
                anchors.fill: parent
                visible: !root.isCold

                Item {
                    id: pictureHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin:  root.px(2)
                    anchors.rightMargin: root.px(2)
                    height: pictureLabel.implicitHeight

                    Text {
                        id: pictureLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: qsTr("SESSION PICTURE")
                        font.family: Theme.fontData
                        font.pixelSize: root.tzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText2
                    }
                    Text {
                        objectName: "sdFormingNote"
                        anchors.left: pictureLabel.right
                        anchors.leftMargin: root.px(9)
                        anchors.right: moreTail.visible ? moreTail.left : parent.right
                        anchors.rightMargin: root.px(9)
                        anchors.baseline: pictureLabel.baseline
                        // THE MODEL'S SENTENCE IF IT HAS ONE, AND THE WAY IN IF IT DOES NOT.
                        //
                        // The closing sentence is stated ONCE, along the bottom in review, where
                        // 13a puts it — saying it here as well would be the same disclosure at
                        // two weights. What fills the slot the rest of the time is the route to
                        // the causal chain, which left this page and needs saying: the whole
                        // card is the door, and TRACE ▸ on each card is the handle. An
                        // affordance nobody can see is one nobody uses, which is the argument
                        // the FOCUS micro-label was already here on.
                        //
                        // It is static UI text rather than the model's, and that is the line
                        // §6.2 actually draws: the model owns every CLAIM ABOUT THE SESSION, and
                        // this claims nothing about one. It says where the button is.
                        text: {
                            const closing = (root.header && !root.reviewing)
                                            ? (root.header.closingLine || "") : ""
                            if (closing !== "") return closing
                            return root.interactive && root._cardsShown > 0
                                   ? qsTr("tap a card to trace what the model says causes it")
                                   : ""
                        }
                        elide: Text.ElideRight
                        font.family: Theme.fontData
                        font.pixelSize: root.tzMicro
                        color: Theme.colorText3
                    }
                    Text {
                        id: moreTail
                        objectName: "sdMoreTail"
                        anchors.right: parent.right
                        anchors.baseline: pictureLabel.baseline
                        visible: root._cardsHidden > 0
                        text: qsTr("+%1 more").arg(root._cardsHidden)
                        font.family: Theme.fontData
                        font.pixelSize: root.tzMicro
                        color: Theme.colorText3
                    }
                }

                Grid {
                    id: cardRow
                    objectName: "sdCardsRow"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: pictureHeader.bottom
                    anchors.topMargin: root.px(8)
                    height: Math.max(0, parent.height - pictureHeader.height - root.px(8))
                    columns: root._cardCols
                    spacing: root._cardGap
                    // The one place the available height is measured, and it is measured off the
                    // Item this Grid is anchored inside — never off the Grid, whose own height
                    // would then depend on the count it is being used to decide.
                    onHeightChanged: root._cardsAvailH = height
                    Component.onCompleted: root._cardsAvailH = height

                    Repeater {
                        model: root._cardsShown

                        PpPatternCard {
                            required property int index
                            card: root.cards[index]
                            fit: root.k
                            interactive: root.interactive
                            pulseCue: root.pulseCue
                            onFocusToggled: (id, on) => root._declareFocus(id, on)
                            onDetailRequested: (id) => root._openDetail(id)
                            width: Math.max(0, (cardRow.width - (root._cardCols - 1) * cardRow.spacing)
                                               / Math.max(1, root._cardCols))
                            // The LAST ROW ABSORBS what the division left over, so the grid ends
                            // flush with the body instead of on a strip of dead space — which is
                            // the complaint that moved the rail off this page in the first place.
                            height: {
                                const rows = Math.max(1, Math.ceil(root._cardsShown / root._cardCols))
                                const mine = Math.floor(index / root._cardCols)
                                const even = Math.floor((cardRow.height - (rows - 1) * cardRow.spacing) / rows)
                                if (mine < rows - 1) return even
                                return Math.max(even, cardRow.height - (rows - 1) * (even + cardRow.spacing))
                            }
                        }
                    }
                }
            }

            // ⚠ THE CHAIN RAIL WAS A BODY HERE, and it is gone from this file rather than
            // hidden in it. 215 lines: two stacked rails wide, the vertical form, the collapsed
            // chain rows and the "+N more chains" tail.
            //
            // It did not survive contact with a real session. The mock's six patterns and one
            // tidy chain became fifteen patterns and eighteen authored chains (build findings
            // §7), and what filled the panel was everything the capture could NOT measure — a
            // screened root carrying four lines of prose, a ghost card with an empty run, two
            // unanchored links — while the conditions that actually fired were pushed into a
            // third of the width. The unmeasured out-ranked the observed.
            //
            // THE CHAIN IS NOT LOST AND THIS IS NOT A DELETION OF THE IDEA. It is one tap
            // behind any card, in PpConditionDetail, which draws the SAME rails out of the SAME
            // components over a RICHER neighbourhood — the full authored ancestry and descent
            // rather than the session's two best paths, with the unmeasured paths collapsed to
            // one line apiece. A golfer who wants to know what caused a fault asks about that
            // fault; they do not ask the panel to guess which two chains to open with.
            //
            // `chains` stays published and stays read — the unchained line and the driver
            // footer are reductions over it. What ended is its claim on the panel's middle.
        }

        // ── unchained pattern ────────────────────────────────────────────────
        // A pattern the model authors no edge for gets its OWN line — reported, never
        // forced onto a chain (brief §5.3).
        Rectangle {
            objectName: "sdUnchainedRow"
            Layout.fillWidth: true
            Layout.preferredHeight: root.px(26)
            visible: !root.detailOpen && unchainedText.text !== ""
            color: "transparent"
            radius: Theme.radius
            border.width: 1
            border.color: Theme.colorBorderMid

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin:  root.px(10)
                anchors.rightMargin: root.px(10)
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.px(9)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("UNCHAINED PATTERN")
                    font.family: Theme.fontData
                    font.pixelSize: root.tzCaption
                    font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorAttention
                }
                Text {
                    id: unchainedText
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.source ? (root.source.unchainedLine || "") : ""
                    elide: Text.ElideRight
                    font.family: Theme.fontBody
                    font.pixelSize: root.tzLabel
                    font.weight: Theme.fontBodyWeight
                    color: Theme.colorText
                }
            }
        }

        // ── watching, then coverage ──────────────────────────────────────────
        PpWatchingRow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            items: root.source ? root.source.watching : []
            expanded: root.watchingExpanded
            suppressed: root.detailOpen
            fit: root.k
            onToggled: root.watchingExpanded = !root.watchingExpanded
            // A watched condition has a ledger like any other; opening one is the same verb the
            // cards and the rail nodes take.
            onItemActivated: (id) => root._openDetail(id)
        }

        // STATED EXACTLY ONCE. In Established and Closing the driver footer carries the
        // coverage line, because the mock puts it in the footer's right-hand column beside
        // the rival it could not adjudicate — the two are the same disclosure. Drawing it in
        // both places would not be twice as honest, it would read as two different numbers.
        PpCoverageLine {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            Layout.leftMargin: root.px(4)
            // Stated on the session picture. The detail is one condition's page and the
             // coverage line is a fact about the capture, which belongs to the panel behind it.
            line: root.detailOpen ? ""
                                  : (root._footerCarriesCoverage
                                     ? "" : (root.source ? (root.source.coverageLine || "") : ""))
            fit: root.k
        }

        // ── likely driver ────────────────────────────────────────────────────
        PpDriverFooter {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            visible: !root.detailOpen && root._hasDriverFooter
            driver: root.driver
            coverageLine: root.source ? (root.source.coverageLine || "") : ""
            fit: root.k
            compact: root.compact
            onScreenRequested: (ref, cond) => root.screenRequested(ref, cond)
        }

        // ── the tense, stated in words ───────────────────────────────────────
        // 13a's footer, and it is not a caption. Everything above it in review is a SESSION
        // total with one wide tick in it, and that is only unambiguous to a reader who has
        // been told the panel does not rewind to what it knew at the selected shot. It is
        // model copy (headerInfo.reviewFootLine), because it names the shot count.
        Text {
            objectName: "sdTenseFooter"
            Layout.fillWidth: true
            Layout.leftMargin: root.px(4)
            Layout.preferredHeight: visible ? implicitHeight : 0
            visible: !root.detailOpen && text !== ""
            text: root.header ? (root.header.reviewFootLine || "") : ""
            elide: Text.ElideRight
            font.family: Theme.fontData
            font.pixelSize: root.tzCaption
            color: Theme.colorText3
        }
    }

    // ── the miss picker ──────────────────────────────────────────────────────
    //
    // A LOCAL, PLAIN-QtQuick POPUP and deliberately not the model browser's ModelCausePicker,
    // whose idiom this borrows: that one lives in a different module, carries a search field,
    // a create-a-new-condition row and a legality contract, and importing it here would drag
    // the whole authoring surface onto a panel that is not an editor. What is needed is a list
    // of the outcomes the pack already authors, which is a list.
    //
    // THE CANDIDATES ARE THE MODEL'S — SessionDiagnosticsModel::missCandidates(), which asks
    // the pack's ConditionKind and not this file's idea of what a miss looks like. A body that
    // kept its own list of "slice, hook, thin, fat" would be authoring content in the QML, and
    // it would go stale the first time the pack gained an outcome.
    Item {
        id: missPicker
        objectName: "sdMissPicker"
        anchors.fill: parent
        visible: _open
        z: 100

        property bool _open: false
        property var  _rows: []
        property real _anchorX: 0
        property real _anchorY: 0

        function openBelow(item) {
            _rows = root._missCandidates()
            const p = item.mapToItem(missPicker, 0, item.height)
            _anchorX = p.x
            _anchorY = p.y + root.px(6)
            _open = true
        }
        function close() { _open = false }

        // Anywhere else is "not now". No modal veil: the panel behind it is still the answer
        // to a different question and dimming it would say the picker had taken the surface
        // over, which it has not.
        //
        // A MouseArea rather than a TapHandler, here and on the sheet below, and the reason is
        // the dismiss: TapHandler's default gesture policy takes a PASSIVE grab, so a scrim
        // handler and a row handler both fire on the same press and every click inside the
        // sheet would also be a click outside it. MouseArea accepts the event and stops it.
        MouseArea { anchors.fill: parent; onClicked: missPicker.close() }

        Rectangle {
            objectName: "sdMissPickerSheet"
            // Kept inside the panel on both axes — the header chip can sit near the right-hand
            // edge in the narrow arrangement, and a sheet half off the panel is clipped away
            // by root's own `clip` rather than drawn outside it.
            x: Math.max(root.px(6), Math.min(missPicker._anchorX,
                                             missPicker.width - width - root.px(6)))
            y: Math.max(root.px(6), Math.min(missPicker._anchorY,
                                             missPicker.height - height - root.px(6)))
            width: root.px(260)
            // SIZED FROM THE ROW COUNT, not from the ListView's contentHeight: the list's own
            // height comes from this one through the anchors, so reading contentHeight back
            // would be a loop the engine breaks by leaving the sheet at whatever it had — a
            // three-row list showing one row.
            readonly property int _rowH: root.px(24)
            height: Math.min(root.px(300),
                             head.height
                             + Math.max(1, missPicker._rows.length) * _rowH
                             + (clearRow.visible ? clearRow.height : 0)
                             + 2 * root.px(6))
            color: Theme.colorSurface
            radius: Theme.radius
            border.width: 1
            border.color: Theme.colorAccent
            clip: true

            // The sheet eats its own clicks, so a tap on its background is not also a tap on
            // the scrim. FIRST child, so the rows above it get theirs first.
            MouseArea { anchors.fill: parent }

            Text {
                id: head
                objectName: "sdMissPickerLabel"
                anchors { left: parent.left; right: parent.right; top: parent.top }
                anchors.margins: root.px(9)
                height: implicitHeight + root.px(6)
                // WHAT THE DECLARATION IS FOR, said where it is made. It pre-arms the chains
                // upstream of the outcome; it is not a filter and it is not evidence (§A6).
                text: qsTr("WHAT IS THE BAD SHOT?")
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            ListView {
                id: list
                objectName: "sdMissPickerList"
                anchors { left: parent.left; right: parent.right; top: head.bottom
                          bottom: clearRow.visible ? clearRow.top : parent.bottom }
                anchors.leftMargin:  root.px(4)
                anchors.rightMargin: root.px(4)
                anchors.bottomMargin: root.px(4)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: missPicker._rows

                delegate: Rectangle {
                    required property var modelData
                    objectName: "sdMissCandidate"
                    width: list.width
                    height: list.parent._rowH
                    radius: Theme.radius
                    readonly property bool _isCurrent: modelData.id === root.declaredMiss
                    color: _isCurrent ? Theme.colorAccentLight
                                      : (rowMouse.containsMouse ? Theme.colorBg3 : "transparent")

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin:  root.px(7)
                        anchors.rightMargin: root.px(7)
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name || modelData.id || ""
                        elide: Text.ElideRight
                        font.family: Theme.fontBody
                        font.pixelSize: root.tzLabel
                        font.weight: Theme.fontBodyWeight
                        color: parent._isCurrent ? Theme.colorAccent : Theme.colorText
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root._declareMiss(parent.modelData.id || "")
                    }
                }
            }

            // Withdrawing is declaring nothing, and it is the same call with an empty id — the
            // model treats "" as a clear, so there is no second verb to keep in step.
            Item {
                id: clearRow
                objectName: "sdMissClear"
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: visible ? root.px(26) : 0
                visible: root.declaredMiss !== ""

                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.leftMargin:  root.px(9)
                    anchors.rightMargin: root.px(9)
                    height: 1
                    color: Theme.colorBorderMid
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: root.px(11)
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("NO DECLARED MISS")
                    font.family: Theme.fontData
                    font.pixelSize: root.tzCaption
                    font.letterSpacing: Theme.trackingLabel
                    color: clearMouse.containsMouse ? Theme.colorText : Theme.colorText3
                }
                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root._declareMiss("")
                }
            }
        }
    }
}
