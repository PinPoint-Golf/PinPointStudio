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

// PPCHKEYFIGURES — the key figures under a heavy rule (B's KeyFigs; design §2.1 "KeyFigures with B's place
// words"): a label with the metric's swatch, the figure with its unit, and a quiet foot that places it in
// the session ("median 72 · 3rd highest"). A warn figure is set in warn AND carries its word ("late",
// "out of its corridor") — never colour alone. Wraps n → n/2 → n/4, each figure at least 10 em
// (flows.keyFigure). Items are ChartsRelationsPresenter.thisShot.keyFigures.items, shown as given.
// Under a figure with a typicality result (item.typicality, R8: this shot against the other shots with its club, at
// least eight), a dense technical Typicality Icon badge: the band's word and the grader's basis ("this swing 82 mph ·
// median 72 mph"), the corridor drawn where the reading has one. The foot stays. A column is never narrower than the
// widest badge, so the figures wrap (n → n/2 → n/4) before a badge would run into its neighbour; when even one column
// is too narrow, the badge shows its word (its line the tooltip) and the basis takes a line of its own under it.
// objectName: chKeyFigures; each figure chKeyFigure (+ readingId), its badge chKeyFigureTypicality.
Item {
    id: kf
    objectName: "chKeyFigures"
    property QtObject roles: null
    property var items: []                       // [{ readingId, key, label, figure, unit, foot, warn, warnWord, typicality }]
    property bool atom: true
    readonly property var keyKinds: []
    // a figure a gate refused, bounded or withheld (item.trustSubject): its foot opens the reason
    signal trustClicked(string subject)
    readonly property int n: (items || []).length
    // the widest and tallest typicality badge with the tile's own insets, measured off the delegates (0: none shows)
    property real _badgeW: 0
    property real _badgeH: 0
    function _measureBadges() {
        let w = 0, h = 0
        for (let i = 0; i < figs.count; ++i) {
            const d = figs.itemAt(i)
            if (d && d.badgeShown) { w = Math.max(w, d.badgeW); h = Math.max(h, d.badgeH) }
        }
        _badgeW = w
        _badgeH = h
    }
    readonly property int fit: !roles ? 1 : Math.max(1, Math.min(roles.columns(width, roles.flows.keyFigure, 0),
                                                                _badgeW > 0 ? Math.floor(width / _badgeW) : n))
    readonly property int cols: roles ? roles.wrapColumns(n, fit) : 1
    readonly property real cw: width / cols
    readonly property real ch: roles ? roles.u(58) + (_badgeH > 0 ? _badgeH + roles.g(4) : 0) : 58
    visible: n > 0
    implicitHeight: n > 0 && roles ? roles.r.figureRuleTop + Math.ceil(n / cols) * ch + roles.u(2) : 0
    height: implicitHeight
    Rectangle { visible: kf.n > 0; width: parent.width; height: kf.roles ? kf.roles.r.figureRuleTop : 1; color: kf.roles ? kf.roles.rule : "black" }
    Repeater {
        id: figs
        model: kf.roles ? kf.items : []
        onItemAdded: kf._measureBadges()
        onItemRemoved: kf._measureBadges()
        delegate: Item {
            id: it
            objectName: "chKeyFigure"
            required property var modelData
            required property int index
            readonly property string readingId: modelData.readingId || ""
            readonly property string key: modelData.key || ""
            readonly property var typ: modelData.typicality || null
            readonly property bool badgeShown: !!typ && !!typ.values
            // the technical badge's room with the tile's insets (a column's left inset and its right margin), for kf's
            // columns: measured off a transparent copy, so it never depends on the layer the shown badge takes
            readonly property real badgeW: tbFull.implicitWidth + kf.roles.u(12) + kf.roles.u(4)
            // a column too narrow for the WIDEST badge's word and basis on one line (one column already): every
            // figure puts the word, then the basis on a line of its own under it, so the columns read alike and
            // nothing runs past the column
            readonly property bool fits: kf._badgeW <= kf.cw + 0.5
            readonly property real badgeH: tb.implicitHeight + (basisLine.visible ? kf.roles.g(2) + basisLine.implicitHeight : 0)
            onBadgeShownChanged: Qt.callLater(kf._measureBadges)
            onBadgeWChanged: Qt.callLater(kf._measureBadges)
            onBadgeHChanged: Qt.callLater(kf._measureBadges)
            readonly property int col: index % kf.cols
            readonly property int row: Math.floor(index / kf.cols)
            x: col * kf.cw; y: kf.roles.r.figureRuleTop + row * kf.ch; width: kf.cw; height: kf.ch
            readonly property real lx: col > 0 ? kf.roles.u(12) : 0
            Rectangle { visible: it.col > 0; width: kf.roles.hw; height: parent.height - kf.roles.u(14); y: kf.roles.u(7); color: kf.roles.hair }
            Row {
                x: it.lx; y: kf.roles.u(6); spacing: kf.roles.g(5)
                width: parent.width - x - kf.roles.u(4)
                Rectangle { visible: it.key !== ""; anchors.verticalCenter: lb.verticalCenter; width: kf.roles.u(kf.roles.ch.swatch); height: width
                            radius: kf.roles.square ? 0 : width / 2; color: kf.roles.hue(it.key) }
                Text { id: lb; width: parent.width - kf.roles.u(12); wrapMode: Text.WordWrap; text: it.modelData.label; font: kf.roles.fLabel; color: kf.roles.ink3 }
            }
            Row {
                x: it.lx; y: kf.roles.u(21); spacing: kf.roles.g(4)
                // a figure a gate refused (item.gated) is the gate's words, quiet, in the link colour; it opens the reason
                Text { id: fg; text: it.modelData.figure + (it.modelData.gated ? " ›" : "")
                       font: it.modelData.gated ? kf.roles.fBody : kf.roles.fFigL
                       color: it.modelData.gated ? kf.roles.link : it.modelData.warn ? kf.roles.warn : kf.roles.ink
                       MouseArea { enabled: !!it.modelData.gated && !!it.modelData.trustSubject; anchors.fill: parent; anchors.margins: -2
                                   cursorShape: Qt.PointingHandCursor; onClicked: kf.trustClicked(it.modelData.trustSubject) } }
                Text { text: it.modelData.unit || ""; font: kf.roles.fSmall; color: kf.roles.ink3; anchors.baseline: fg.baseline }
                Text { visible: it.modelData.warn && !!it.modelData.warnWord; text: it.modelData.warnWord || ""; font: kf.roles.fSmall; color: kf.roles.warn; anchors.baseline: fg.baseline }
            }
            Text { id: foot; x: it.lx; y: kf.roles.u(42); width: parent.width - x - kf.roles.u(4); wrapMode: Text.WordWrap
                   readonly property bool why: !!it.modelData.trustSubject
                   objectName: why ? "chKeyFigureWhy:" + it.modelData.trustSubject : ""
                   text: (it.modelData.foot || "") + (why && !it.modelData.gated ? "  ›" : "")
                   font: kf.roles.fSmall; color: why && !it.modelData.gated ? kf.roles.link : kf.roles.ink3
                   MouseArea { enabled: foot.why && !it.modelData.gated; anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor
                               onClicked: kf.trustClicked(it.modelData.trustSubject) } }
            // R8: this shot against its club's other shots, under the foot; no result, no badge (never a shape without its word)
            PpVariabilityBadge {
                id: tb
                objectName: "chKeyFigureTypicality"
                visible: it.badgeShown
                x: it.lx; y: foot.y + foot.height + kf.roles.g(4)
                variant: PpVariabilityMark.Typicality
                form: PpVariabilityMark.Icon
                dense: true
                contentLayer: it.fits ? "technical" : "golfer"
                values: it.badgeShown ? it.typ.values : null
                basisText: it.badgeShown ? (it.typ.basisText || "") : ""
                showCorridor: it.badgeShown && it.typ.showCorridor === true
                roles: kf.roles
            }
            // the basis on its own line when the column cannot hold it beside the word (the grader's string, as the
            // badge sets it: the figure face, ink2)
            Text {
                id: basisLine
                objectName: "chKeyFigureTypicalityBasis"
                visible: it.badgeShown && !it.fits && text !== ""
                x: it.lx; y: tb.y + tb.height + kf.roles.g(2); width: parent.width - x - kf.roles.u(4)
                wrapMode: Text.WordWrap
                text: it.badgeShown ? (it.typ.basisText || "") : ""
                font: kf.roles.fFig; color: kf.roles.ink2
            }
            // the measure: the technical badge as it would be drawn, transparent (never seen, never heard)
            PpVariabilityBadge {
                id: tbFull
                opacity: 0
                enabled: false
                Accessible.ignored: true
                variant: PpVariabilityMark.Typicality
                form: PpVariabilityMark.Icon
                dense: true
                contentLayer: "technical"
                values: it.badgeShown ? it.typ.values : null
                basisText: it.badgeShown ? (it.typ.basisText || "") : ""
                roles: kf.roles
            }
        }
    }
}
