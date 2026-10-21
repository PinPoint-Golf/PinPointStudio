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

// THE COVER — the session on one page (design §3, page 1). A golfer who reads nothing else reads this.
//
// THE ONE FITTED PAGE (§9.1 rule 3). Everything else in the report flows; the cover must stand alone, so it is
// fitted, not flowed: every list is hard-capped (3 strengths, 5 habits, 5 work-ons, 8 ball figures; several clubs:
// 4 clubs × 4 figures), every line stops at two lines and ends in an ellipsis (the full text is always inside:
// the work-on pages and the grid say it again), the coach's note is capped by the sheet at about 80 words and
// here at eight lines, and the space between the three columns and the ball block takes up whatever is left.
// So the cover is ONE atom exactly a page tall, with the ball block at its foot. If even within the caps the cover
// would overflow (a test proves it does not), the ball block leaves for a page of its own after the cover, rather
// than the columns being squeezed. Only Texts marked `fitted` may elide: tst_session_report fails any other.
//
// The masthead itself is furniture (PpSessionReport draws it above the pager's window); the cover leaves room under
// it (mastheadH) so a two-line studio name pushes the page down instead of colliding.
//
// The note (`kind` "note": fewer than 3 swings, §3.1) is the cover alone, flowed: the kicker, the name, the
// headline, the ball and the swings' numbers, on one page.
Item {
    id: cv
    property QtObject ro: null
    property var rep: ({})
    property var host: null                             // PpSessionReport: woPage(rank) for the page references
    property real pageH: 900
    property real mastheadH: 40
    objectName: "reportCoverBlock"

    readonly property bool isNote: (rep.kind || "report") === "note"
    readonly property var brand: rep.brand || ({})
    readonly property var meta: rep.meta || ({})
    readonly property bool hasBall: !!rep.ball && ((rep.ball.groups || []).length > 0)
    readonly property real gapToBall: 28
    readonly property bool ballFits: !hasBall || isNote || top.height + gapToBall + ballMeasure.height <= pageH - 1
    width: 600
    implicitHeight: col.height
    height: implicitHeight

    // The studio's mark: its logo (a file URL), else a monogram generated from its name in the accent.
    component Mark: Item {
        id: mk
        property QtObject ro: null
        property var brand: ({})
        property real d: 40
        readonly property bool hasLogo: !!brand && !!brand.logo
        readonly property real aspect: logo.implicitHeight > 0 ? logo.implicitWidth / logo.implicitHeight : 1
        width: hasLogo ? Math.min(d * 3, d * aspect) : d
        height: d
        Rectangle {
            visible: !mk.hasLogo
            anchors.fill: parent; radius: width / 2; color: mk.ro ? mk.ro.accent : "transparent"
            Text {
                anchors.centerIn: parent
                text: (mk.brand && mk.brand.monogram) || ""
                color: mk.ro ? mk.ro.paper : "transparent"
                font.family: mk.ro ? mk.ro.serif : ""; font.pixelSize: Math.max(6, Math.round(mk.d * 0.42)); font.letterSpacing: 0.5
            }
        }
        Image {
            id: logo
            visible: mk.hasLogo
            anchors.fill: parent
            // the logo keeps one file name when the studio replaces it: never cached, and the studio's stamp in the URL
            source: mk.hasLogo ? mk.brand.logo + (mk.brand.stamp ? "?v=" + mk.brand.stamp : "") : ""
            cache: false
            fillMode: Image.PreserveAspectFit; horizontalAlignment: Image.AlignLeft
            sourceSize.height: Math.round(mk.d * 4)
            smooth: true; mipmap: true; asynchronous: false
        }
    }

    // the ball block, measured off the galley (a zero-height holder the pager skips) to decide whether it fits
    Item {
        width: cv.width; height: 0
        PpBallTiles { id: ballMeasure; opacity: 0; width: cv.width; ro: cv.ro; ball: cv.rep.ball || null }
    }

    Column {
        id: col
        width: cv.width

        // ═══ the cover page: one atom, a page tall (the note: as tall as it is) ═══
        Item {
            id: coverAtom
            objectName: "reportCover"
            property bool atom: true
            width: cv.width
            height: cv.isNote ? top.height + (noteBall.visible ? cv.gapToBall + noteBall.height : 0)
                              : (cv.ballFits && cv.hasBall ? cv.pageH - 1 : top.height)

            Column {
                id: top
                width: parent.width
                // room under the masthead (drawn as furniture from y 30): at least 28 px clear of it
                Item { width: 1; height: Math.max(22, 30 + cv.mastheadH + 30 - 76) }
                Text {
                    objectName: "reportKicker"
                    width: parent.width; text: cv.meta.kicker || ""
                    font: cv.ro.fKicker; color: cv.ro.accent; wrapMode: Text.WordWrap
                    maximumLineCount: 2; elide: Text.ElideRight; property bool fitted: true
                }
                Item { width: 1; height: 8 }
                Text {
                    width: parent.width; text: cv.meta.golfer || ""
                    font: cv.ro.fName; color: cv.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.02
                    maximumLineCount: 2; elide: Text.ElideRight; property bool fitted: true
                }
                Item { width: 1; height: 12 }
                Text {
                    objectName: "reportHeadline"
                    width: Math.min(parent.width, 62 * cv.ro.em); text: cv.rep.headline || ""
                    font: cv.ro.fHead; color: cv.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.3
                    maximumLineCount: 3; elide: Text.ElideRight; property bool fitted: true
                }
                // the coach's note: typed at generation, optional, signed
                Item { width: 1; height: 20; visible: noteRow.visible }
                Row {
                    id: noteRow
                    objectName: "reportCoachNote"
                    visible: !!cv.rep.coachNote
                    spacing: 14
                    Rectangle { width: 2; height: noteCol.height; color: cv.ro.accent }
                    Column {
                        id: noteCol
                        width: cv.width - 16; spacing: 6
                        Text { width: parent.width; text: cv.rep.coachNote || ""; font: cv.ro.fNote; color: cv.ro.ink
                               wrapMode: Text.WordWrap; lineHeight: 1.35; maximumLineCount: 8; elide: Text.ElideRight; property bool fitted: true }
                        Text { visible: !!cv.brand.coach; text: "— " + (cv.brand.coach || ""); font: cv.ro.fSmall; color: cv.ro.ink2 }
                    }
                }
                Item { width: 1; height: 26; visible: three.visible }

                // three columns: going well · what we found · what to work on (each present only with something in it)
                Row {
                    id: three
                    visible: lists.length > 0
                    width: cv.width; spacing: 22
                    readonly property var lists: {
                        const out = []
                        const s = (cv.rep.strengths || []).slice(0, 3)
                        if (s.length) out.push({ key: "strengths", title: "Going well", rule: "accent", items: s })
                        const f = (cv.rep.found || []).slice(0, 5)
                        const w = (cv.rep.watching || []).slice(0, 5)
                        if (f.length) out.push({ key: "found", title: "What we found", rule: "ink", items: f })
                        else if (w.length) out.push({ key: "watching", title: "Seen, not yet habits", rule: "ink", items: w })
                        const b = (cv.rep.workOnsBrief || []).slice(0, 5)
                        if (b.length && !cv.isNote) out.push({ key: "workOns", title: "What to work on", rule: "accent", items: b })
                        return out
                    }
                    readonly property real cw: (width - (lists.length - 1) * spacing) / Math.max(1, lists.length)
                    Repeater {
                        model: three.lists
                        delegate: Column {
                            id: list
                            required property var modelData
                            objectName: "reportCover_" + modelData.key
                            width: three.cw; spacing: 8
                            Rectangle { width: parent.width; height: 1.5; color: list.modelData.rule === "accent" ? cv.ro.accent : cv.ro.ink }
                            Text { text: list.modelData.title; font: cv.ro.fH3; color: cv.ro.ink }
                            Repeater {
                                model: list.modelData.items
                                delegate: Row {
                                    id: it
                                    required property var modelData
                                    readonly property string key: list.modelData.key
                                    readonly property bool ranked: key === "workOns"
                                    readonly property bool marked: key === "found" || key === "watching"
                                    spacing: ranked ? 8 : 7
                                    Text {
                                        visible: it.ranked
                                        width: 16
                                        text: it.ranked ? it.modelData.rank : ""
                                        font.family: cv.ro.serif; font.pixelSize: it.ranked && it.modelData.rank === 1 ? 22 : 15
                                        color: it.ranked && it.modelData.rank === 1 ? cv.ro.accent : cv.ro.ink3
                                    }
                                    PpSwingGrid.SwingMark { visible: it.marked; ro: cv.ro; state: "fired"; d: 7; y: 6 }
                                    Column {
                                        width: three.cw - (it.ranked ? 24 : (it.marked ? 14 : 0)); spacing: 1
                                        Text {
                                            width: parent.width
                                            text: it.ranked ? (it.modelData.goal || "") : (it.key === "strengths" ? it.modelData.text : it.modelData.golfer) || ""
                                            font: it.ranked && it.modelData.rank === 1 ? cv.ro.fItemM : cv.ro.fItem
                                            color: cv.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32
                                            maximumLineCount: 2; elide: Text.ElideRight; property bool fitted: true
                                        }
                                        // the second line: the count, or (a work-on) its first drill and its page. The page
                                        // reference sits in a column of its own width, so a page number that changes on
                                        // repagination can never re-wrap the line and move the layout it was computed from.
                                        Item {
                                            width: parent.width; height: subText.height
                                            Text {
                                                id: subText
                                                width: parent.width - (it.ranked ? refW.width + 6 : 0)
                                                text: it.ranked ? (it.modelData.drill ? "Drill: " + it.modelData.drill : "") : (it.modelData.count || "")
                                                font: cv.ro.fMicro; color: cv.ro.ink3; wrapMode: Text.WordWrap
                                                maximumLineCount: 2; elide: Text.ElideRight; property bool fitted: true
                                            }
                                            TextMetrics { id: refW; font: cv.ro.fMicro; text: "p. 00" }
                                            Text {
                                                visible: it.ranked
                                                x: parent.width - width; width: refW.width; horizontalAlignment: Text.AlignRight
                                                text: it.ranked && cv && cv.host ? "p. " + cv.host.woPage(it.modelData.rank) : ""
                                                font: cv.ro.fMicro; color: cv.ro.ink3
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // the ball, at the foot of the page (the space above it absorbs the rest)
            PpBallTiles {
                objectName: "reportCoverBall"
                visible: cv.hasBall && cv.ballFits && !cv.isNote
                anchors.bottom: parent.bottom
                width: parent.width; ro: cv.ro; ball: cv.rep.ball || null
            }
            PpBallTiles {
                id: noteBall
                objectName: "reportNoteBall"
                visible: cv.hasBall && cv.isNote
                y: top.height + cv.gapToBall
                width: parent.width; ro: cv.ro; ball: cv.rep.ball || null
            }
        }

        // ═══ the ball block on a page of its own, when even within the caps the cover cannot hold it ═══
        Item {
            objectName: "reportBallMoved"
            property bool atom: true
            property bool breakBefore: true
            property string section: "The ball"
            visible: cv.hasBall && !cv.ballFits
            width: cv.width; height: movedBall.height
            PpBallTiles { id: movedBall; width: parent.width; ro: cv.ro; ball: cv.rep.ball || null }
        }

        // ═══ the note: its swings' numbers on the same page ═══
        Item { width: 1; height: 26; visible: noteNumbers.visible }
        PpNumbersTable {
            id: noteNumbers
            visible: cv.isNote && !!cv.rep.numbers
            width: cv.width
            ro: cv.ro; numbers: cv.rep.numbers || null; practice: null; fresh: false
        }
    }
}
