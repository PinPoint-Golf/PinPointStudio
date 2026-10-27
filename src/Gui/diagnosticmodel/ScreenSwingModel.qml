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

// The Swing model screen (AppScreen.SwingModel): the rail's way to the Diagnostic Model editor,
// which was Settings panel 10 until the four-modes refactor (docs/design/ux_refactor_away_from_4_modes.md
// §2.2). A screen of its own so the session lock can let it through while Settings stays Settings.
//
// ⚠ THE ONLY DiagnosticModel IN THE APP. Each instance owns a ModelBrowser, and each ModelBrowser
// holds its own unsaved working copy — two would mean two sets of edits, and whichever saved last
// would silently discard the other's. Every deep link (MetricRoute, Settings search, the Settings
// slot that used to host it) comes here through Main.qml rather than growing a second one.
Item {
    id: root
    objectName: "swingModelScreen"

    DiagnosticModel {
        id: diagnosticModel
        anchors.fill: parent
    }

    // Forwarders, so callers address the screen and never reach inside it.
    function showMetric(key)                 { diagnosticModel.showMetric(key) }
    function showCharacteristic(conditionId) { diagnosticModel.showCharacteristic(conditionId) }
    function showMeasure(measureId)          { diagnosticModel.showMeasure(measureId) }
    function scrollToItem(itemId)            { return diagnosticModel.scrollToItem(itemId) }
}
