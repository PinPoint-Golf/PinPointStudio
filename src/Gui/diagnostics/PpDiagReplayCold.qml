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

// REPLAYCOLD — the Session view while the model is Cold (S2-S4, F41-F42), for both registers.
// "Say the n is too small": the model's coldLine as the head ("Too few swings to call a pattern.") and its
// countLine under it. Then the lib's PpExpectations (dashed: the one thing here that is not evidence from
// this session), drawn ONLY when the athlete has a fault profile — an empty heading would read as the
// model having looked and found none.
Column {
    id: cb
    property QtObject roles: null
    property var header: null
    property var expectations: []
    spacing: roles.u(14)
    objectName: "sdColdBody"

    readonly property var list: expectations || []

    Column {
        width: cb.width
        spacing: cb.roles.u(3)
        Text { objectName: "sdColdHeadline"; width: parent.width; wrapMode: Text.WordWrap
               text: cb.header ? (cb.header.coldLine || "") : ""; font: cb.roles.fTitle; color: cb.roles.ink }
        Text { objectName: "sdColdSubline"; width: parent.width; wrapMode: Text.WordWrap
               text: cb.header ? (cb.header.countLine || "") : ""; font: cb.roles.fSmall; color: cb.roles.ink3 }
    }
    // the expectations: the lib's PpExpectations block, which draws NOTHING when the profile is empty
    PpExpectations {
        objectName: "sdExpectations"
        width: cb.width
        roles: cb.roles
        items: cb.list
    }
}
