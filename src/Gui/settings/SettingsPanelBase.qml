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

// The skeleton every Settings panel stands on: a ScrollView holding one content column (32 px in,
// 28 px down, 28 px of room under the last row), and the settings-search contract ScreenSettings
// drives. A panel's own rows are its children — they go straight into the content column:
//
//     SettingsPanelBase {
//         id: root
//         contentSpacing: Theme.gap(16)              // gap(20) unless set (Cameras/Imus/Mics/Phones: 16)
//
//         PpPageHeader {                             // the page's eyebrow, title and description
//             Layout.fillWidth: true
//             spacing: root.contentSpacing
//             eyebrow: Theme.caps(qsTr("Library")); title: qsTr("Archiving"); description: qsTr("…")
//         }
//         PpMicro { kind: "body"; text: Theme.caps(qsTr("Space")) }      // a section head
//         SettingsRow { objectName: "setting_archiveAge"; … }
//     }
//
// Non-visual helpers (Timer, Connections, a Popup that parents itself to the overlay) may sit among
// them: a ColumnLayout lays out Items only. A visual overlay that must NOT scroll (a toast) sets
// `parent: root`. Functions, properties and inline components declared on the panel stay on the
// panel, as before.
//
// THE SEARCH CONTRACT (ScreenSettings.navigateToResult → scrollWithRetry, keyed by SettingsIndex):
//   scrollToItem(itemId) → bool   "" answers true (nothing to find). Otherwise the first item under
//                                 the content column whose objectName is itemId is scrolled to 24 px
//                                 below the top, its `searchHighlight` set for 1.8 s, and true
//                                 returned; false when there is no such item yet (the caller retries).
//                                 The target must have a `searchHighlight` property: SettingsRow does.
//   findChild(parent, name)       the depth-first objectName search it uses, for the panel's own use.
//   lastHighlightId               the last id highlighted. The light goes out on the item actually lit
//                                 (the landing), and a new search puts out the one before.
//   landingFor(target) → item     HOOK: the item to land on and light up instead of `target`. Default:
//                                 the target. Settings → Studio lands on its "Send from" row when the
//                                 target is a sending row hidden until a provider is chosen:
//                                     landingFor: function (t) { return t.visible ? t : (root.findChild(root.contentColumn, "setting_sendProvider") || t) }
//
// Layout knobs: `contentSpacing` (the column's spacing), `maxContentWidth` (0 = the panel's width less
// 64 px; Studio caps its column at 760 px), `contentEnabled` (Studio greys its rows when there is no
// profile). `contentColumn` and `scrollView` are exposed for panels that measure or scroll themselves.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Item {
    id: base

    default property alias content: contentCol.data
    property real contentSpacing:  Theme.gap(20)
    property real maxContentWidth: 0
    property bool contentEnabled:  true
    readonly property alias contentColumn: contentCol
    readonly property alias scrollView:    scroller

    // ── Search-scroll contract (ScreenSettings → SettingsIndex) ───────────────

    property string lastHighlightId: ""

    property var landingFor: function (target) { return target }

    // The item lit now — the landing, which is not always the item named by lastHighlightId.
    property var _lit: null

    function findChild(parent, name) {
        for (var i = 0; i < parent.children.length; i++) {
            var child = parent.children[i]
            if (child.objectName === name) return child
            var found = findChild(child, name)
            if (found) return found
        }
        return null
    }

    function scrollToItem(itemId) {
        if (!itemId) return true
        var target = findChild(contentCol, itemId)
        if (!target) return false
        target = base.landingFor(target) || target
        var mapped = target.mapToItem(contentCol, 0, 0)
        scroller.contentItem.contentY = Math.max(0, Math.min(
            mapped.y - Theme.sp(24),
            scroller.contentItem.contentHeight - scroller.height
        ))
        if (base._lit && base._lit !== target) base._lit.searchHighlight = false
        target.searchHighlight = true
        base._lit = target
        lastHighlightId = itemId
        highlightTimer.restart()
        return true
    }

    Timer {
        id: highlightTimer
        interval: 1800
        onTriggered: {
            if (base._lit) base._lit.searchHighlight = false
            base._lit = null
        }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentCol.y + contentCol.implicitHeight + Theme.gap(28)

        ColumnLayout {
            id: contentCol
            x:       Theme.gap(32)
            y:       Theme.gap(28)
            width:   base.maxContentWidth > 0 ? Math.min(parent.width - Theme.gap(64), base.maxContentWidth)
                                              : parent.width - Theme.gap(64)
            spacing: base.contentSpacing
            enabled: base.contentEnabled
        }
    }
}
