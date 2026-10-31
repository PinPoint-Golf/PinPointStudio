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

// PPCHSECTIONCONTROLS — a Charts section's quiet controls (Posture: Pattern · Against; Wrist: Bands · Against · Curves
// behind), in Analyse only (never at the glance or on the cast): the choices in one line at medium and up, folded
// below medium to a "Controls" head carrying the one-line `summary` (decision rule 5), opened and closed with the
// section's controlsOpen. The section builds the `sets` and answers `picked`; anything else the controls say (the
// wrist's ★ reference link and its style line) goes in as children, shown with the choices (`shown`).
// objectName: the section's (chPostureControls, chWristControls); the fold below medium: `foldName`.
Column {
    id: ctl
    property var section: null                   // the PpChSection these belong to
    property string foldName: ""
    property string key: ""                      // the fold's key (not remembered: controlsOpen is the section's)
    property string summary: ""
    property bool wide: false                    // medium and up: no fold, the choices in one line
    property var sets: []
    readonly property var ty: section ? section.ty : null
    readonly property bool shown: wide || (!!section && section.controlsOpen)
    signal picked(string set, string option)
    visible: !!section && section.has && !section.replay && !section.cast
    spacing: ty ? ty.u(4) : 0

    PpChFoldHead {
        objectName: ctl.foldName
        visible: !ctl.wide
        width: parent.width; roles: ctl.ty; key: ctl.key; title: "Controls"; interactive: !!ctl.section && ctl.section.interactive
        collapsed: !ctl.section || !ctl.section.controlsOpen
        state_: ctl.summary
        onToggled: if (ctl.section) ctl.section.controlsOpen = !ctl.section.controlsOpen
    }
    PpChChoices {
        visible: ctl.shown
        width: parent.width; roles: ctl.ty; interactive: !!ctl.section && ctl.section.interactive
        sets: ctl.sets
        onPicked: (set, opt) => ctl.picked(set, opt)
    }
}
