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

// PPTRUSTSUMMARYFOLD — "How far can I trust this?" (docs/design/trust_and_gates_design.md §7.4): one quiet
// line under a head, CLOSED by default (Mark, 16 Oct 2026: the panels are busy enough for a novice), that
// opens to the summary trust_readings.h says once — the routes that timed the sequence, what passed in the
// Setup page's words, and per gate the readings it acted on, what it kept from being judged and what would
// lift it. A gated line's "Why ›" opens that reading in the Trust panel, or in place when the panel is off
// (TrustFocus, §7.0). Reads the session through its own SessionSeriesModel (the process-wide cache) and a
// TrustPresenter, as PpTrustInlineFor does.
//   scope     "shot" (swingDir picks the shot) | "session"
// objectNames: trustSummaryFold, trustSummaryToggle, trustSummaryLine:<kind>.
Column {
    id: fold
    objectName: "trustSummaryFold"
    property QtObject roles: null
    property string scope: "shot"
    property string sessionDir: ""
    property string swingDir: ""
    property bool open: false
    // PRINT: the report has nothing to click, so the summary is set open under its kicker, with no "Why"
    // links; one atom for the pager (PpPager), so it never splits across a page.
    property bool printed: false
    readonly property bool atom: printed
    readonly property bool _shown: open || printed
    property string openSubject: ""              // the gated line expanded in place (panel off)

    // Read imperatively, once the presenter exists and whenever it republishes: an AOT-compiled binding on
    // the presenter's id ran before the presenter was complete and dereferenced a null meta-object
    // (qml_ui SessionDiagnostics, and one app start).
    property var summary: null
    readonly property var lines: (summary && summary.lines) || []
    function _read() {
        const m = !_ready ? null : scope === "session" ? tp.session : tp.shot
        summary = (m && m.summary) || null
    }
    property bool _ready: false
    onScopeChanged: _read()
    visible: lines.length > 0
    spacing: roles ? roles.u(4) : 4

    function _folder(p) { return String(p || "").replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "") }
    function _sessionOf(d) { const s = _folder(d); return s.substring(0, s.lastIndexOf("/")) }
    readonly property string _session: sessionDir !== "" ? sessionDir : _sessionOf(swingDir)
    on_SessionChanged: _point()
    Component.onCompleted: { _ready = true; _point(); _read() }
    function _point() { if (_session !== "" && _folder(_session) !== _folder(sm.sessionDir)) sm.activateSession(_session) }

    SessionSeriesModel { id: sm }
    TrustPresenter { id: tp; model: sm; pickedSwingDir: fold.scope === "shot" ? fold.swingDir : ""
                     onResultsChanged: fold._read() }

    // the closed line: ▸ How far can I trust this?  pelvis and chest out of sight
    Item {
        objectName: "trustSummaryToggle"
        width: fold.width
        height: toggleRow.height
        Flow {
            id: toggleRow
            width: parent.width
            spacing: fold.roles ? fold.roles.u(7) : 7
            Text { text: fold.printed ? "How far can I trust this?" : (fold.open ? "▾" : "▸") + " How far can I trust this?"
                   font: fold.roles ? (fold.printed ? fold.roles.fSmallM : fold.roles.fSmallL) : Qt.font({})
                   color: fold.roles ? (fold.printed ? fold.roles.ink : fold.roles.link) : "grey" }
            Text { visible: !fold._shown && text !== ""
                   text: fold.summary ? (fold.summary.tail || "") : ""
                   font: fold.roles ? fold.roles.fSmall : Qt.font({}); color: fold.roles ? fold.roles.ink3 : "grey" }
        }
        MouseArea { anchors.fill: parent; enabled: !fold.printed; cursorShape: Qt.PointingHandCursor; onClicked: fold.open = !fold.open }
    }
    Repeater {
        model: fold._shown ? fold.lines : []
        delegate: Column {
            id: ln
            required property var modelData
            objectName: "trustSummaryLine:" + modelData.kind
            width: fold.width
            spacing: fold.roles ? fold.roles.u(4) : 4
            readonly property bool gated: modelData.kind === "gated" && !!modelData.subject && !fold.printed
            Text {
                width: fold.roles ? Math.min(fold.width, fold.roles.measureW(fold.width)) : fold.width
                leftPadding: fold.printed ? 0 : fold.roles ? fold.roles.u(14) : 14
                wrapMode: Text.WordWrap; textFormat: Text.StyledText
                text: (fold.roles ? fold.roles.esc(ln.modelData.text) : ln.modelData.text)
                      + (ln.gated && fold.roles ? "  <font color='" + fold.roles.link + "'>Why ›</font>" : "")
                font: fold.roles ? fold.roles.fSmall : Qt.font({}); color: fold.roles ? fold.roles.ink2 : "grey"
                MouseArea {
                    anchors.fill: parent
                    enabled: ln.gated
                    cursorShape: ln.gated ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (!TrustFocus.open(ln.modelData.subject, ln.modelData.swingDir))
                                   fold.openSubject = fold.openSubject === ln.modelData.subject ? "" : ln.modelData.subject
                }
            }
            PpTrustInlineFor {
                width: ln.width; roles: fold.roles
                subject: ln.gated && fold.openSubject === ln.modelData.subject ? ln.modelData.subject : ""
                swingDir: ln.modelData.swingDir || ""
                onClosed: fold.openSubject = ""
            }
        }
    }
}
