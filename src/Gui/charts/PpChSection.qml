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

// PPCHSECTION — the shell of a Charts section after the Summary (Posture, Wrist, Club delivery, on This shot and on
// Session): the register's contract (PpChViewBase), the section's fold head (PpChFoldHead "chFold:<key>": its title,
// remembered folded or open in body.collapsed.<key> by body.toggleSection, and while folded the one line `foldState`
// so nothing it held is silently gone), the gap under it, and the slot the section's own blocks go in (the default
// property), drawn only while the section has something to show (`has`) and is open. It is a Column, so its height is
// the head, and the slot's `mainHeight` under it while open; nothing at all while `has` is false.
//   No `key`: no head of its own — the Session sections, which PpChartsSessionView heads and folds.
//   seek(us): a press on a position, a frame or a ledger row moves the video there (PpChSeek owns the guard).
//   controlsOpen: the quiet controls opened below medium (PpChSectionControls reads and toggles it).
//   autoChoice / againstChoice: the two PpChChoices sets the sections share — the Auto-resolved one (Pattern, Bands:
//   Auto's label carries what it resolved to) and Against (address · previous · reference ★ · session median).
PpChViewBase {
    id: sec
    readonly property var ty: roles
    property string key: ""
    property string title: ""
    property string foldState: ""
    property bool has: true
    property real mainHeight: 0
    property bool controlsOpen: false
    readonly property bool folded: key !== "" && !!body && !!body.collapsed && body.collapsed[key] === true
    default property alias content: sectionBody.data
    spacing: roles ? roles.u(14) : 0

    function seek(us) { PpChSeek.to(us) }
    function autoChoice(key, label, current, list, after) {
        return { key: key, label: label, current: String(current !== undefined ? current : -1),
                 options: (list || []).map(function (a) {
                     return { key: String(a.id), label: a.id === -1 && a.current && after ? a.label + " " + after : a.label } }) }
    }
    function againstChoice(current, list) {
        return { key: "against", label: "Against", current: current || "address",
                 options: (list || []).map(function (a) { return { key: a.id, label: a.label, enabled: a.enabled } }) }
    }

    PpChFoldHead { visible: sec.has && sec.key !== ""; width: sec.width; roles: sec.ty; key: sec.key; title: sec.title
                   interactive: sec.interactive; collapsed: sec.folded
                   state_: sec.folded ? sec.foldState : ""
                   onToggled: if (sec.body) sec.body.toggleSection(sec.key) }
    Item {
        id: sectionBody
        visible: sec.has && !sec.folded
        width: sec.width
        height: sec.mainHeight
    }
}
