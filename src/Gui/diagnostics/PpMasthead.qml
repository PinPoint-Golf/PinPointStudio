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

// MASTHEAD — B's report masthead, built entirely from the data: "Fifteen shots, eight habits" and a
// dek that names the club, the date, the shot read and how many of its conditions fired. Print opens
// with it; on screen it can head a cast or a report preview.
PpStandfirst {
    id: mh
    property var readout: null
    property var cards: []
    property var source: null
    readonly property var meta: roles.sessionMeta(source, readout, true)
    readonly property var conds: (readout && readout.conditions) || []
    readonly property int nFired: conds.filter(c => c.stateKind === "fired").length
    readonly property int nPats: conds.filter(c => c.stateKind === "fired" && c.tierTag === "pattern").length
    readonly property int shotCount: readout ? readout.shotCount : 0
    size: "feature"
    kicker: "Session report"
    head: roles.cap1(roles.word(shotCount)) + " shots, " + roles.word((cards || []).length) + " habits"
    standfirst: "A " + roles.esc(meta.club) + " session on " + roles.esc(meta.date) + ". Counted over all " + roles.figHtml(shotCount)
                + " shots, " + roles.word((cards || []).length) + " conditions recur often enough to call habits. The shot read here is number "
                + roles.figHtml(roles.shotNo(readout)) + ": " + roles.word(nFired) + " of its " + roles.figHtml(readout ? readout.measurableSetCount : 0)
                + " conditions went outside their corridor, " + roles.word(nPats) + " of them among those habits."
}
