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
import QtQuick.Controls
import PinPointStudio

// REPLAY — the glance register (Replay, Capture, and the second-screen cast). It sits beside the video
// for the whole replay, so it gives the answer in one look and then stays still.
//   This shot  C's PpDotPlot of the FIRED conditions with their values; one quiet line for the rest;
//              the shot ruler (how many went outside on each shot: the view's one shot axis).
//              Live with nothing picked it reads the NEWEST swing (D3); a quiet shot says so (D2);
//              with nothing to read it says why, in the old panel's words for the tense (F19).
//   Session    one PpSparkRow per pattern (or the small multiples when all fit the height); Cold says the
//              n is too small and shows the expectations (F41-F42); the coverage line at the foot (F39).
//   Watching   C's PpBarcode, emerging first; empty, the old sentence (F45).
//   Detail     never shown (D9). A detail opened elsewhere leaves the reader on their tab with one quiet
//              line pointing to Analyse; a tab press closes the detail first.
// A click on a condition is the glance's one door into depth: it opens that condition and switches to
// Analyse (never from the cast). A click on a shot moves the carousel.
// Cast (surface "cast", always this register, D4): a running head names the shot; the WHOLE surface is
// zoomed for distance. An auto-closing cast (interactive false) takes no clicks at all and never flips the
// stage; a persistent cast (coach, kiosk) keeps its tabs.
// Live plumbing (D1, D8): the after-shot pulse washes the rows that fired (PpBrush.pulse); each view is its own named
// Flickable, built on first show and never torn down, so scroll and hover survive a republish; demo code
// runs only when demoState is non-empty.
Item {
    id: root
    objectName: "sdRegister"
    readonly property string register: "Replay"
    // ── contract ──
    property string view: "shot"
    property var readout: null
    property var cards: []
    property var source: null
    property var header: null
    property var detail: null
    property var body: null
    property string demoState: ""
    property string surface: "panel"            // "panel" | "cast"
    property string medium: "screen"
    property string printOrientation: ""
    property int printPage: 0
    readonly property int printPageCount: 1     // the glance never prints; PpDiagPrint does
    property bool interactive: true              // body.interactive: false on the auto-closing cast
    property string cardFilter: "all"            // body.cardFilter: an Analyse tool; the glance shows every pattern
    property var pulseCue: null                  // body.pulseCue
    readonly property var demoStates: ["", "hoverShot12", "hoverCondition", "hoverWatching", "missPicker"]

    readonly property bool cast: surface === "cast"
    // D9: a glance never drills. "detail" (opened in Analyse, or restored by navigation) shows the tab the
    // reader is on, and the tab row stays live.
    readonly property string bodyTab: body && body.tab ? String(body.tab) : "shot"
    readonly property string shown: (view === "shot" || view === "session" || view === "watching") ? view
                                  : (["shot", "session", "watching"].indexOf(bodyTab) >= 0 ? bodyTab : "shot")
    readonly property bool detailParked: view === "detail" && !cast

    // ── data ──
    readonly property var shot: ss.shot
    readonly property var conds: (shot && shot.conditions) || []
    readonly property var fired: conds.filter(c => c.stateKind === "fired")
    readonly property var clean: conds.filter(c => c.stateKind === "clean")
    readonly property var notMeasured: conds.filter(c => c.stateKind === "notAssessable")
    readonly property int cleanN: shot && shot.partial ? shot.cleanCount : clean.length
    readonly property int notMeasuredN: shot && shot.partial ? shot.notAssessableCount : notMeasured.length
    readonly property int firedPatterns: fired.filter(c => c.tierTag === "pattern").length
    readonly property var watching: (source && source.watching) || []
    readonly property var watchingSorted: watching.slice().sort((a, b) => (ro.rate(b.recurrence) - ro.rate(a.recurrence)) || a.name.localeCompare(b.name))
    readonly property int watchingFiredHere: watching.filter(w => { const c = ro.byId(conds, w.id); return c && c.stateKind === "fired" }).length
    readonly property int sessionFiredHere: (cards || []).filter(c => c.thisShot === "fired").length
    readonly property string shotWord: shot ? "shot " + ro.shotNo(shot) : ""
    readonly property var focusCard: ss.focusId ? ro.byId(cards || [], ss.focusId) : null
    readonly property string focusShotLine: {
        if (!ss.focusId || !shot) return ""
        const c = ro.byId(conds, ss.focusId), card = focusCard
        const name = card ? card.name : (c ? c.name : "")
        return name ? "focus: " + ss.focusLine(name, c ? c.stateKind : (ss.partial ? "clean" : "notAssessable")) : ""
    }
    function n(x) { return ro.word(x) }              // words to ten, figures above (house style)

    // ── wiring: every click goes somewhere real ──
    // A condition opens in Analyse. Open FIRST on the body we hold, then switch: Analyse comes up already on
    // that condition. Never from the cast: the cast is a second screen and must not flip the main stage.
    function openCondition(id) {
        const b = root.body
        if (!b || !id || root.cast || !root.interactive) return
        b._openDetail(id)
        SessionMode.enterAnalyse()
    }
    function pickShot(dir, id) { if (root.body && root.interactive && id > 0) root.body.shotRequested(dir || br.dirOf(id), id) }
    // D9: a tab press while a detail is open closes the detail first, then goes to the tab.
    function pickView(key) {
        if (!root.interactive) return
        if (root.body) {
            if (root.body.detailOpen && typeof root.body._closeDetail === "function") root.body._closeDetail()
            root.body.tab = key
        } else root.view = key
    }
    property bool missOpen: false
    // D1: the after-shot pulse. The body publishes a cue ({ token, ids, focusId }) when a shot surfaces; the
    // PpBrush carries it and every row block washes its own rows once (lib PpPulseWash), the focus first.
    property int _pulseToken: -1
    onPulseCueChanged: firePulse()
    function firePulse() {
        const c = root.pulseCue
        if (!c) { br.pulseHold = false; br.pulseIds = []; _pulseToken = -1; return }   // a cue withdrawn: nothing held
        if (c.token === undefined || c.token === _pulseToken) return
        _pulseToken = c.token
        br.pulseHold = !!c.hold                   // renders only: hold the wash for a still
        // ids may arrive as afterShotDelta chips ({ id, … }) rather than ids: take the id either way
        br.pulse((c.ids || []).map(x => x && typeof x === "object" ? x.id : x).filter(x => !!x), c.focusId || "")
    }

    // ── demo states (renderable interaction): only ever when demoState is non-empty (D8) ──
    property string _lastDemo: ""
    onDemoStateChanged: applyDemo()
    function applyDemo() {
        if (demoState === "" && _lastDemo === "") return
        _lastDemo = demoState
        br.clear()
        missOpen = demoState === "missPicker"
        if (demoState === "hoverShot12") br.hoveredShot = 12
        else if (demoState === "hoverCondition") br.hoveredCondition = "early_extension"
        else if (demoState === "hoverWatching") br.hoveredCondition = "sway"
    }

    // ── a view's head: the answer as a title, the context as one quiet line beside it (or under it) ──
    component Head: Item {
        id: hd
        property string title: ""
        property string sub: ""
        readonly property bool side: tt.implicitWidth + ro.u(14) + st.implicitWidth <= width
        implicitHeight: title === "" ? st.implicitHeight : side ? tt.implicitHeight : tt.implicitHeight + ro.u(2) + st.implicitHeight
        height: implicitHeight
        Text { id: tt; visible: hd.title !== ""; text: hd.title; font: ro.fTitle; color: ro.ink; width: Math.min(implicitWidth, hd.width); wrapMode: Text.WordWrap }
        Text { id: st; text: hd.sub; font: ro.fSmall; color: ro.ink3; wrapMode: Text.WordWrap
               width: hd.side && hd.title !== "" ? implicitWidth : hd.width
               x: hd.side && hd.title !== "" ? tt.implicitWidth + ro.u(14) : 0
               y: hd.title === "" ? 0 : hd.side ? tt.y + tt.baselineOffset - baselineOffset : tt.implicitHeight + ro.u(2) }
    }
    // the review note, folded to the foot of a view below medium (F7, RULES 3)
    component ReviewFoot: Text {
        objectName: "sdReviewNote"
        visible: !root.cast && !!root.header && String(root.header.reviewNote || "") !== "" && (!ro.shows("reviewNote", width) || ss.missShown)
        wrapMode: Text.WordWrap
        text: root.header ? ro.cap1(root.header.reviewNote) + "." : ""
        font: ro.fSmall; color: ro.ink3
    }

    // ── one named Flickable per view, built on first show and never torn down (D8, §0.2-0.3) ──
    component ViewFlick: Flickable {
        id: vf
        property string key: ""
        property Component view: null
        readonly property bool on: root.shown === key
        property bool built: false
        onOnChanged: if (on) built = true
        Component.onCompleted: if (on) built = true
        visible: on
        anchors { top: under.bottom; left: surfaceItem.left; right: surfaceItem.right; bottom: surfaceItem.bottom; topMargin: ro.u(12) }
        contentWidth: width; contentHeight: (ld.item ? ld.item.height : 0) + ro.u(16)
        clip: true; boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Loader { id: ld; active: vf.built; width: vf.width - ro.u(8); sourceComponent: vf.view }
        }
    // ═══════════════════════════ THE SURFACE (zoomed as a whole on the cast) ═══════════════════════════
    // The cast is read from across the room: the whole surface scales (like browser zoom) so that the
    // page it lays out is about one compact panel, never type growing inside a panel. The zoom is chosen
    // from the DATA (an em estimate of the view's height), clamped to 1…1.6, never below compact (42 em).
    readonly property bool castTabs: cast && interactive
    readonly property real castEm: (shown === "shot" ? 9 + fired.length * 1.25
                                  : shown === "session" ? 6 + (cards || []).length * 2.3
                                  : 8 + watching.length * 1.95) + (castTabs ? 2.2 : 0)
    readonly property real zoom: cast ? Math.max(1, Math.min(1.6, width / (42 * ro.em), height / ((castEm + 4) * ro.em))) : 1
    Item {
        id: surfaceItem
        width: root.width / root.zoom; height: root.height / root.zoom
        scale: root.zoom; transformOrigin: Item.TopLeft

        // the cast's running head (an auto cast opens on This shot: the body's own _tab rule): the shot,
        // and which view this is. The register draws the cast's title line; the body frame stands down (S30).
        Item {
            id: castHead
            objectName: "sdCastHead"
            visible: root.cast
            width: parent.width; height: visible ? ro.u(24) : 0
            Text { anchors.bottom: parent.bottom; anchors.bottomMargin: ro.u(6)
                   text: ro.cap1(ro.shotOfText(ss.shot)) + (ss.club ? " · " + ss.club : "")
                   font: ro.fKicker; color: ro.ink2 }
            Text { visible: !root.castTabs
                   anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: ro.u(6)
                   text: root.shown === "shot" ? "This shot" : root.shown === "session" ? "Session" : "Watching"
                   font: ro.fLabel; color: ro.ink3 }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ro.hw; color: ro.hair }
        }
        PpTabs {
            id: tabs
            objectName: "sdTabBar"
            visible: !root.cast || root.castTabs
            anchors.top: castHead.bottom
            width: parent.width; height: visible ? implicitHeight : 0
            roles: ro; brush: br
            current: root.shown
            // tier 3 on the right; below medium, or when the miss chip takes the right, the foot of the view
            note: root.header && !ss.missShown ? ro.cap1(root.header.reviewNote) : ""
            interactive: root.interactive
            // F66: the declared-miss chip at the right of the tab row (not on the cast)
            chip: !root.cast && ss.missShown ? ss.missText : ""
            chipOn: root.missOpen
            onChipActivated: root.missOpen = !root.missOpen
            // no counts: in a glance each view's head states its own; the toggles alone stay (RULES 11)
            tabs: [{ key: "shot", label: "This shot" }, { key: "session", label: "Session" }, { key: "watching", label: "Watching" }]
            onTabActivated: k => root.pickView(k)
        }
        Column {
            id: under
            anchors.top: tabs.bottom
            width: parent.width
            // F67: the picker opens IN PLACE under the tab row; a pick goes to the model, "" clears
            PpInlinePicker {
                objectName: "sdMissPicker"
                visible: root.missOpen && ss.missShown && !root.cast
                open: visible
                width: under.width
                roles: ro; brush: br; interactive: root.interactive
                items: visible ? ss.missCandidates().map(m => ({ id: m.id, label: m.name || m.id })) : []
                current: ss.declaredMiss
                onPicked: id => { if (root.body && typeof root.body._declareMiss === "function") root.body._declareMiss(id); root.missOpen = false }
            }
            Text {
                objectName: "sdDetailParked"
                visible: root.detailParked && !!root.detail
                topPadding: ro.u(8)
                width: under.width; wrapMode: Text.WordWrap
                text: (root.detail ? root.detail.name || "" : "") + " is open in Analyse"
                      + (root.interactive ? "  ·  <a href='analyse'>Read it there</a>" : "")
                textFormat: Text.StyledText; linkColor: ro.link
                font: ro.fSmall; color: ro.ink3
                onLinkActivated: SessionMode.enterAnalyse()
            }
        }

        ViewFlick { id: shotFlick; objectName: "sdShotCardsFlick"; key: "shot"; view: shotC }
        ViewFlick { id: sessionFlick; objectName: "sdCardsFlick"; key: "session"; view: sessionC }
        ViewFlick { id: watchingFlick; objectName: "sdWatchingFlick"; key: "watching"; view: watchingC }
    }
    // the auto-closing cast takes no clicks, no hovers, no wheel: every affordance is a trap on a surface
    // about to vanish under the pointer (old body, interactive false)
    MouseArea {
        anchors.fill: parent; z: 1000
        visible: root.cast && !root.interactive; enabled: visible
        hoverEnabled: true; acceptedButtons: Qt.AllButtons; preventStealing: true
        onWheel: wheel => wheel.accepted = true
    }

    // ── THIS SHOT ──
    Component {
        id: shotC
        Column {
            id: sc
            objectName: "sdShotView"
            spacing: ro.u(10)
            // nothing to read: the tense says why (F19); the live one only while a swing can still come
            Column {
                objectName: "sdShotEmpty"
                visible: !ss.shot && ss.tense !== "none"
                width: sc.width
                spacing: ro.u(2)
                Text { visible: ss.emptyTitle !== ""; text: ss.emptyTitle; font: ro.fTitle; color: ro.ink }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: ss.emptyLine
                       font: ss.emptyTitle !== "" ? ro.fSmall : ro.fBody; color: ss.emptyTitle !== "" ? ro.ink3 : ro.ink2 }
            }
            // D2: a quiet shot says so in one calm line, keeps its ruler, and leaves the tabs live
            Column {
                objectName: "sdQuietState"
                visible: ss.quiet
                width: sc.width
                spacing: ro.u(2)
                Text { objectName: "sdQuietLabel"; width: parent.width; wrapMode: Text.WordWrap
                       text: "Quiet shot — nothing new surfaced on this swing"; font: ro.fTitle; color: ro.ink }
                Text { objectName: "sdQuietLine"; width: parent.width; wrapMode: Text.WordWrap
                       text: (root.cast ? "" : ro.shotOfText(ss.shot) + " · ") + (ss.delta ? ss.delta.headline : "") + " · the ledger is still counting"
                       font: ro.fSmall; color: ro.ink3 }
            }
            Head {
                id: shotHead
                objectName: "sdShotHead"
                visible: !!ss.shot && !ss.quiet
                width: sc.width
                title: root.fired.length ? ro.cap1(root.n(root.fired.length)) + " outside the corridor" : "Inside every corridor"
                sub: [ (root.cast ? "" : (ss.partial ? "newest swing · " : "") + ro.shotOfText(ss.shot) + (ss.club ? " · " + ss.club : "")),
                       (!root.fired.length || !(root.cards || []).length ? ""
                        : root.firedPatterns === root.fired.length ? (root.fired.length === 1 ? "a session pattern" : "all session patterns")
                        : root.firedPatterns === 0 ? "none of them session patterns"
                        : root.firedPatterns + " of them session patterns"),
                       root.focusShotLine ].filter(s => s !== "").join(" · ")
            }
            PpDotPlot {
                id: dp
                objectName: "sdDotPlot"
                visible: root.fired.length > 0 && !ss.quiet
                width: sc.width
                roles: ro; brush: br; readout: root.shot; filter: "fired"
                interactive: root.interactive && !root.cast
                // Spare height admits the next tier: after the fired rows, the clean ones in order of how
                // close they came to their edge, as many as fit — never bigger type, never a second block.
                readonly property real room: shotFlick.height - ro.u(16) - shotHead.height - foot.height - 2 * sc.spacing
                // rows that fit, measured by the plot itself (names may wrap once the tier folds)
                readonly property int fit: rowsFitting(room)
                availableHeight: room
                rowsOverride: !ss.partial && fit > root.fired.length ? allRows.slice(0, Math.min(allRows.length, fit)) : null
                readonly property int shownClean: rowsOverride ? rowsOverride.length - root.fired.length : 0
                onConditionActivated: id => root.openCondition(id)
            }
            // the rest, as one quiet line; the shot ruler beside it while it fits, else under it.
            // Its size is worked out from the LONGEST form of the line, never the line itself, so the plot's
            // room does not depend on how many clean rows the plot then admits (no binding loop).
            Item {
                id: foot
                visible: !!ss.shot
                width: sc.width
                readonly property bool hasRuler: !ss.partial
                readonly property real lineH: fmFoot.height
                // fmFoot.font is read so the measure follows a theme change (advanceWidth() is a call, untracked)
                readonly property real longest: fmFoot.font.pixelSize <= 0 ? 0 : fmFoot.advanceWidth(root.cleanN + " more inside their corridor · " + root.notMeasuredN + " not measurable")
                // the ruler's one-line width at its wanted pitch (an estimate: never read off the ruler, no loop)
                readonly property real rulerNatural: ro.u(60) + ro.u(19) * (ss.shot ? ss.shot.shotCount : 0)
                readonly property bool side: !ss.quiet && hasRuler && longest + ro.u(32) + rulerNatural <= width
                height: !hasRuler ? (quiet.visible ? lineH : 0)
                       : side ? Math.max(lineH, ruler.height)
                       : (quiet.visible ? lineH + ro.u(10) : 0) + ruler.height
                FontMetrics { id: fmFoot; font: ro.fSmall }
                Text {
                    id: quiet
                    objectName: "sdQuietCount"
                    visible: !ss.quiet
                    y: foot.side ? ruler.height - ro.u(13) + (ro.u(13) - foot.lineH) / 2 : 0
                    text: (dp.shownClean > 0 ? (root.cleanN - dp.shownClean) + " more inside their corridor" : root.cleanN + " clean")
                          + (root.notMeasuredN ? " · " + root.notMeasuredN + " not measurable" : "")
                    font: ro.fSmall; color: ro.ink3
                }
                PpShotRuler {
                    id: ruler
                    visible: foot.hasRuler
                    x: foot.side ? foot.width - width : 0
                    y: foot.side ? 0 : (quiet.visible ? foot.lineH + ro.u(10) : 0)
                    availableWidth: foot.side ? foot.rulerNatural : foot.width
                    roles: ro; brush: br; readout: root.shot; interactive: root.interactive
                    onShotActivated: (d, s) => root.pickShot(d, s)
                }
            }
            ReviewFoot { width: sc.width }
        }
    }

    // ── SESSION ──
    Component {
        id: sessionC
        Column {
            id: sv
            objectName: "sdSessionView"
            spacing: ro.u(10)
            PpDiagReplayCold {
                visible: ss.cold
                width: sv.width
                roles: ro; header: root.header; expectations: root.source ? root.source.expectations : []
            }
            Head {
                id: sessHead
                objectName: "sdSessionHead"
                visible: !ss.cold && ss.tense !== "none"
                width: sv.width
                title: ro.cap1(root.n((root.cards || []).length)) + ((root.cards || []).length === 1 ? " pattern" : " patterns")
                sub: [ "counted over " + ss.sessionShots + (ss.sessionShots === 1 ? " shot" : " shots"),
                       (root.shot ? root.shotWord + " outside on " + ro.countWord(root.sessionFiredHere) : ""),
                       (root.focusCard ? "focus: " + root.focusCard.name : "") ].filter(s => s !== "").join(" · ")
            }
            // HEIGHT IS A RESOURCE TOO: when the panel is tall enough for every pattern's small multiple, the
            // glance shows the tiles (the same run against the same corridor, larger); otherwise one sparkline
            // row per pattern. Measured, never guessed: the tiles lay out (hidden) to say how tall they are.
            readonly property real room: sessionFlick.height - ro.u(16) - sessHead.height - sv.spacing - ro.u(24) - cov.height - sv.spacing
            readonly property bool hasCards: !ss.cold && (root.cards || []).length > 0
            readonly property bool useTiles: hasCards && !root.cast && tiles.height > 0 && tiles.height <= room
            PpSmallMultiples {
                id: tiles
                width: sv.width
                visible: sv.useTiles
                roles: ro; brush: br; cards: sv.hasCards ? root.cards : []; readout: root.shot; source: root.source
                interactive: root.interactive
                conditionsInteractive: !root.cast
                onConditionActivated: id => root.openCondition(id)
                onShotActivated: (d, s) => root.pickShot(d, s)
            }
            PpDiagReplaySessionRows {
                width: sv.width
                visible: sv.hasCards && !sv.useTiles
                roles: ro; brush: br; cards: sv.hasCards ? root.cards : []; readout: root.shot
                interactive: root.interactive
                conditionsInteractive: !root.cast
                onConditionActivated: id => root.openCondition(id)
                onShotActivated: (d, s) => root.pickShot(d, s)
            }
            // F39: the capture's coverage, one quiet line, never omitted while there is a session
            Text {
                id: cov
                objectName: "sdCoverageLine"
                visible: !!root.source && String(root.source.coverageLine || "") !== ""
                width: sv.width; wrapMode: Text.WordWrap
                text: root.source ? ro.cap1(root.source.coverageLine) + "." : ""
                font: ro.fSmall; color: ro.ink3
            }
            ReviewFoot { width: sv.width }
        }
    }

    // ── WATCHING ──
    Component {
        id: watchingC
        Column {
            id: wv
            objectName: "sdWatchingView"
            spacing: ro.u(10)
            Head {
                objectName: "sdWatchingHead"
                visible: ss.tense !== "none" && root.watching.length > 0
                width: wv.width
                title: ro.cap1(root.n(root.watching.length)) + " watched"
                sub: "outside at least once, not yet a pattern" + (root.shot ? " · " + root.shotWord + " outside on " + ro.countWord(root.watchingFiredHere) : "")
            }
            Text {
                objectName: "sdWatchingEmpty"
                visible: ss.tense !== "none" && root.watching.length === 0
                width: wv.width; wrapMode: Text.WordWrap
                text: "Nothing below the pattern gate yet — a condition that fires but has not recurred enough to be a pattern is listed here."
                font: ro.fBody; color: ro.ink2
            }
            // with no shot picked, or live with nothing picked (D3), there is no run per watched condition to
            // draw: its names and counts (a barcode of blanks would be a gap, never "not measured")
            PpLedgerTable {
                visible: (!ss.shot || ss.partial) && root.watching.length > 0
                width: wv.width; roles: ro; brush: br; readout: null
                rows: visible ? root.watchingSorted : []
                spec: ss.watchSpec
                getter: (w, key) => key === "name" ? { t: w.name } : { t: ro.frac(w.recurrence) }
                interactive: root.interactive && !root.cast
                onConditionActivated: id => root.openCondition(id)
            }
            PpBarcode {
                objectName: "sdBarcode"
                visible: root.watching.length > 0 && !!ss.shot && !ss.partial
                width: wv.width
                roles: ro; brush: br; rows: root.watchingSorted; readout: root.shot; source: root.source
                showMarginNotes: false
                interactive: root.interactive
                conditionsInteractive: !root.cast
                onConditionActivated: id => root.openCondition(id)
                onShotActivated: (d, s) => root.pickShot(d, s)
            }
            ReviewFoot { width: wv.width }
        }
    }

    // ── the shared objects, made once and passed down; declared LAST so they are destroyed last ──
    PpDiagReplayState { id: ss; source: root.source; header: root.header; readout: root.readout; cards: root.cards || []; interactive: root.interactive }
    PpDiagRoles { id: ro; medium: root.medium }
    PpBrush {
        id: br
        cards: root.cards || []
        pickedShot: ss.shot ? ss.shot.shotId : -1
        pickedCondition: ""
    }
}
