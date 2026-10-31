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

// PPCHVIEWBASE — what every Charts view and section takes from its register (PpChartsRegister → PpChartsShotView,
// PpChartsSessionView, PpChartsRelationsView, PpChartsReadingsView, PpChartsKindsView; and, through PpChSection, the
// Posture / Wrist / Club delivery sections): the roles it is set in, the shared brush, the body whose presenters'
// words it shows (each bound ONCE there), the register ("Analyse" the read, "Replay" the glance at the mat), whether
// it answers the pointer, whether it is the cast, and the page height it may plan for. A Column, as the views are;
// it draws nothing of its own.
Column {
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    property real pageH: 500
    readonly property bool replay: reg === "Replay"
}
