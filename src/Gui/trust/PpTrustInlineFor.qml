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

// PPTRUSTINLINEFOR — the in-place reason (PpTrustInline) for a surface that holds no readings of its own:
// given a swing and a subject ("ks.pelvis"), it reads the swing's session's own series (refactor 5.5a:
// the one the Charts panel reads, ChartsSeries.of) through a TrustPresenter, and
// shows that one reading. Built only while open. Session diagnostics uses it for a finding a gate kept
// from being judged, when the Trust panel is off (TrustFocus).
// objectName: trustInlineFor.
Item {
    id: tf
    objectName: "trustInlineFor"
    property QtObject roles: null
    property string subject: ""
    property string swingDir: ""
    signal closed()
    readonly property bool open: subject !== "" && swingDir !== ""
    width: parent ? parent.width : 0
    implicitHeight: open && ld.item ? ld.item.implicitHeight : 0
    height: implicitHeight
    visible: open

    function _sessionOf(d) { const s = SessionMode.folderKey(d); return s.substring(0, s.lastIndexOf("/")) }
    readonly property string _session: open ? _sessionOf(swingDir) : ""
    // THE SWING'S SESSION'S OWN SERIES (refactor 5.5a): the session on screen's, else the Session that holds the folder
    // (app.sessionAt: asked only for a folder that is not the one on screen).
    readonly property var _series: (typeof ChartsSeries === "undefined" || typeof app === "undefined" || !app || tf._session === "") ? null
        : ChartsSeries.of(SessionMode.sameFolder(tf._session, app.state.sessionOnScreenFolder) ? app.session : app.sessionAt(tf._session))

    Loader {
        id: ld
        active: tf.open
        width: tf.width
        sourceComponent: Item {
            width: tf.width
            implicitHeight: inl.implicitHeight
            TrustPresenter { id: tp; model: tf._series; pickedSwingDir: tf.swingDir }
            PpTrustInline {
                id: inl
                width: parent.width
                roles: tf.roles
                swingDir: tf.swingDir
                readings: ((tp.shot && tp.shot.readings) || []).filter(function (r) { return r.subject === tf.subject })
                onClosed: tf.closed()
            }
        }
    }
}
