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

// Wrist Motion screen (contentStack index 2). Hosts the persistent session
// toolbar at the top; the wrist body content arrives in a later prompt. The
// toolbar's device panels run calibration entirely in-panel, so this screen
// neither exposes nor routes any calibrate request.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PpSessionToolbar {
            Layout.fillWidth: true
        }

        // Body — wrist content arrives in a later prompt.
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ScreenPlaceholder { anchors.fill: parent; iconText: "⌖"; titleText: qsTr("Wrist") }
        }

        // Session-shot carousel — keys mirror the Wrist goal vocabulary
        // (goalDefsByType[1]); the stub model supplies placeholder values.
        PpShotCarousel {
            Layout.fillWidth: true
            metricKeys: ["wristAngleTop", "impactConditions", "trailWristExtension", "transition"]
            traceLabel: qsTr("LEAD-WRIST FLEXION · ADDRESS → IMPACT")
        }
    }
}
