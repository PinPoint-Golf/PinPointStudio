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

// PPCHQUESTIONS — at most three sharper questions (spec 15.7), each the presenter's (ChartsTracesPresenter
// .session.questions, ChartsRelationsPresenter .thisShot.asks / .relations.asks), each shown only because the
// data answers it. A press asks it: the register applies it IN PLACE (lights the `lit` shots on the Brush,
// sets the facet or band it names, or steps the view `go`), and the asked one stands in ink with its answer
// under it. Co-occurrence answers carry their caveat (the presenters write it). Print sets them as footnotes.
// objectName: chQuestions; each question chQuestion (+ questionId).
Column {
    id: qs
    objectName: "chQuestions"
    property QtObject roles: null
    property var questions: []                   // [{ id, text, answer (Sentence | string), lit, go, upId, setFacet, setBand }]
    property string active: ""
    property bool interactive: true
    property bool atom: true
    readonly property var keyKinds: []
    signal asked(var q)
    visible: (questions || []).length > 0
    spacing: roles ? roles.u(3) : 3
    topPadding: roles ? roles.u(8) : 8
    Text { text: "Ask"; font: qs.roles ? qs.roles.fLabel : Qt.font({}); color: qs.roles ? qs.roles.ink3 : "grey"; bottomPadding: qs.roles ? qs.roles.u(1) : 1 }
    Repeater {
        model: qs.roles ? (qs.questions || []).slice(0, 3) : []
        delegate: Column {
            id: q
            objectName: "chQuestion"
            required property var modelData
            readonly property string questionId: modelData.id
            readonly property bool on: qs.active === modelData.id
            width: qs.width; spacing: qs.roles.u(2)
            Text {
                width: Math.min(implicitWidth, parent.width); wrapMode: Text.WordWrap
                text: q.modelData.text
                font: q.on || !qs.interactive ? qs.roles.fSmall : qs.roles.fSmallL
                color: q.on || !qs.interactive ? qs.roles.ink : qs.roles.link
                MouseArea { anchors.fill: parent; enabled: qs.interactive; cursorShape: Qt.PointingHandCursor; onClicked: qs.asked(q.on ? null : q.modelData) }
            }
            Text {
                readonly property var a: q.modelData.answer
                readonly property string html: !a ? "" : typeof a === "string" ? qs.roles.esc(a) : qs.roles.html(a)
                visible: q.on && html !== ""
                width: Math.min(parent.width, qs.roles.flows.measure * qs.roles.em); wrapMode: Text.WordWrap; textFormat: Text.RichText
                text: html; font: qs.roles.fSmallI; color: qs.roles.ink2; bottomPadding: qs.roles.u(3)
            }
        }
    }
}
