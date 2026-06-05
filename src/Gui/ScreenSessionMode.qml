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

// Reusable session-mode screen: the persistent session toolbar pinned at the top
// with a placeholder body below. Used by the Swing, GRF and Coach rail screens
// (the Wrist screen has its own ScreenWrist, which will gain bespoke content).
// Bespoke per-mode content replaces the placeholder in later prompts.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property string iconText:  ""
    property string titleText: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PpSessionToolbar {
            Layout.fillWidth: true
        }

        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ScreenPlaceholder {
                anchors.fill: parent
                iconText:  root.iconText
                titleText: root.titleText
            }
        }
    }
}
