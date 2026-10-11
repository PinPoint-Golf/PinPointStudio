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

// FEATUREHEAD — B's detail opening: kicker (No. 1 · P4 · measure), the condition's name as a feature
// head, the standfirst (direction + consequence), the pull figures (this shot in warn when it fired,
// the session median) and the fact file (corridor, recurrence, bookends, measures, reads, norm).
// One row from roles.flows.openingRow em; standfirst then pull | facts from 36 em; all stacked below.
// `longForm` false gives the short standfirst; the rest of the consequence then belongs to PpPathChains("effects").
// Round 4: showPull / showFacts (false when PpKeyFigures carries the figures, so they are not said twice);
// the HONESTY MARK of a non-card condition (detail.header.mark, F56: "ghost · authored as asserted, no
// measure") under the kicker; the norm's citation INLINE (no tooltip): "the source" opens it in place on
// screen, print prints it.
Item {
    id: op
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property var detail: null
    property var readout: null
    property var source: null
    property bool longForm: medium === "print" || roles.shows("longStandfirst", width)
    property bool showPull: true
    property bool showFacts: true
    property bool citationOpen: medium === "print"
    property bool interactive: medium === "screen"
    property bool atom: true
    property bool keepNext: true
    readonly property var keyKinds: []
    signal shotActivated(string swingDir, int shotId)

    readonly property var dh: (detail && detail.header) || null
    readonly property var dsp: (dh && dh.spread) || null
    readonly property int shotNo: roles.shotNo(readout)
    readonly property real gap: roles.u(40)
    readonly property int mode: width >= roles.flows.openingRow * roles.em ? 2 : width >= 36 * roles.em ? 1 : 0
    readonly property real pullW: showPull ? Math.max(pullRow.width, roles.u(150)) : 0
    readonly property string citation: (dsp && dsp.normCitation) || ""
    objectName: "sdFeatureHead"
    implicitHeight: Math.max(opening.y + opening.height, showPull ? pullBox.y + pullBox.height : 0, showFacts ? facts.y + facts.height : 0)
    height: implicitHeight

    Column {
        id: opening
        width: op.mode === 2 ? 34 * op.roles.em : op.roles.measureW(op.width)
        spacing: op.roles.u(8)
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: op.dh ? [String(op.dh.rankText || "").replace("#", "No. "), op.dh.phase, op.dh.measure].filter(x => x).join(" · ") : ""
            font: op.roles.fKicker; color: op.roles.ink3
        }
        Text {
            objectName: "sdDetailMark"
            visible: text.length > 0
            width: parent.width; wrapMode: Text.Wrap
            text: op.dh ? [op.dh.kindText || "", op.dh.mark || ""].filter(x => !!x).join(" · ") : ""
            font: op.roles.fSmallI; color: op.roles.ink3
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: (op.detail && op.detail.name) || ""
            font: op.roles.fFeature; color: op.roles.ink
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; textFormat: Text.RichText
            topPadding: op.roles.u(2)
            text: op.dh ? op.roles.esc(op.roles.sentence(op.dh.directionText)) + " "
                          + op.roles.esc(op.longForm ? (op.dh.consequence || "") : op.roles.consequenceSplit(op.dh)[0]) : ""
            font: op.roles.fStand; color: op.roles.ink2; lineHeight: op.roles.r.leading
        }
    }
    Column {
        id: pullBox
        visible: op.showPull
        x: op.mode === 2 ? opening.width + op.gap : 0
        y: op.mode === 2 ? 0 : opening.height + op.roles.u(20)
        spacing: op.roles.u(5)
        Rectangle { width: op.pullW; height: op.roles.hw; color: op.roles.rule }
        Row {
            id: pullRow
            spacing: op.roles.u(30)
            Repeater {
                model: op.roles && op.dsp ? [
                    { n: op.dsp.currentNumber, l: "shot " + op.shotNo, warn: op.dh.thisShot === "fired" },
                    { n: op.dsp.medianNumber, l: "session median", warn: false }
                ] : []
                delegate: Column {
                    required property var modelData
                    Text {
                        text: modelData.n === undefined || modelData.n === "" ? "-" : modelData.n
                        font: op.roles.fPull; color: modelData.warn ? op.roles.warn : op.roles.ink
                    }
                    Text { text: modelData.l; font: op.roles.fSmall; color: op.roles.ink3 }
                }
            }
        }
        Text {
            width: op.pullW; wrapMode: Text.Wrap
            text: {
                const sp = op.dsp
                if (!sp || !sp.unit) return ""
                let t = "in " + (sp.unit === "°" ? "degrees" : sp.unit)
                if (op.dh && sp.measure && sp.measure !== op.dh.measure) t += ", " + op.roles.lower1(sp.measure)
                return t
            }
            font: op.roles.fSmall; color: op.roles.ink3
        }
    }
    Column {
        id: facts
        visible: op.showFacts
        x: op.mode === 0 || !op.showPull ? (op.mode === 2 ? opening.width + op.gap : 0) : pullBox.x + op.pullW + op.gap
        y: !op.showPull ? (op.mode === 2 ? 0 : opening.height + op.roles.u(20)) : op.mode === 0 ? pullBox.y + pullBox.height + op.roles.u(18) : pullBox.y
        width: Math.max(op.roles.u(120), Math.min(op.mode === 0 ? op.width : op.width - x, 30 * op.roles.em))
        spacing: op.roles.u(5)
        Rectangle { width: parent.width; height: op.roles.hw; color: op.roles.rule }
        Repeater {
            model: {
                if (!op.roles || !op.dh) return []
                const h = op.dh, R = op.roles, out = []
                const cc = R.corridorClause(h.corridorText)
                if (cc) out.push({ k: "Corridor", v: R.esc(R.cap1(cc)) + (op.dsp && op.dsp.measure && op.dsp.measure !== h.measure ? ", on " + R.esc(R.lower1(h.measure)) : "") + "." })
                out.push({ k: "Recurrence", v: R.cap1(R.recWords(h.recurrence, op.readout ? op.readout.shotCount : 0)) + ", " + R.trendWords(h) + "; " + R.esc(h.firingsAfterText || h.recencyText || "") + "." })
                const b = R.byId((op.source && op.source.bookends) || [], h.id)
                if (b) out.push({ k: "Bookends", v: "Typical " + R.figHtml(b.representativeShotId, R.ink) + ", worst " + R.figHtml(b.worstShotId, R.ink) + ", best " + R.figHtml(b.bestShotId, R.ink) + "." })
                const ms = (op.detail && op.detail.measures) || []
                if (ms.length > 1) {
                    const shown = op.dsp ? op.dsp.measure : ""
                    out.push({ k: "Measures", v: ms.map(m => R.esc(R.cap1(m.label)) + " (" + R.esc(m.countText) + ")" + (m.label === shown ? ", shown below" : "")).join("; ") + "." })
                }
                if (op.dsp && op.dsp.highMeans && !R.shows("highMeans", op.width)) out.push({ k: "Reads", v: "high means " + R.esc(op.dsp.highMeans) + "." })
                if (op.dsp && op.dsp.normTag) out.push({ k: "Norm", norm: true, v: R.esc(R.cap1(String(op.dsp.normTag).replace(/^norm · /, ""))) + ", not fitted to your shots."
                    + (op.citation && op.interactive && !op.roles.isPrint ? "&nbsp;&nbsp;<span style=\"color:" + R.hex(R.link) + ";" + (R.r.linkUnderline ? " text-decoration:underline;" : "") + "\">" + (op.citationOpen ? "Hide the source" : "The source") + "</span>" : "") })
                return out
            }
            delegate: Text {
                required property var modelData
                width: facts.width; wrapMode: Text.Wrap; textFormat: Text.RichText
                text: op.roles.strongHtml(modelData.k) + "&nbsp;&nbsp;" + modelData.v
                font: op.roles.fSmall; color: op.roles.ink2; lineHeight: op.roles.r.smallLeading
                MouseArea { enabled: !!modelData.norm && !!op.citation && op.interactive && !op.roles.isPrint; visible: enabled
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: op.citationOpen = !op.citationOpen }
            }
        }
        // the norm's provenance, in place (it was a tooltip, which the design bans)
        Text {
            objectName: "sdNormCitation"
            visible: op.citationOpen && op.citation.length > 0
            width: facts.width; wrapMode: Text.Wrap
            text: op.citation; font: op.roles.fMicro; color: op.roles.ink3; lineHeight: op.roles.r.smallLeading
        }
    }
}
