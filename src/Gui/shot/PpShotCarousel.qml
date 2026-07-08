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

// Session-shot carousel — the film-strip rail docked at the bottom of the
// mode screens. Reusable: each screen supplies its own metricKeys (the panel's
// summary metrics) and traceLabel; the shot data itself is the session-global
// `shotModel` context property, filtered per-screen through this carousel's
// own ShotFilterProxyModel. Rating/note/trash go straight to the shotModel
// invokables; replay/export/face-on are the host screen's to wire.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // Ordered metric keys the review panel shows for this screen
    // (e.g. Wrist: wristAngleTop, impactConditions, trailWristExtension, transition).
    property var    metricKeys: []
    // Caption over the panel's trace chart (e.g. "LEAD-WRIST FLEXION · ADDRESS → IMPACT").
    property string traceLabel: ""

    // Optional host-injected control tucked into the carousel's top strip (right side) —
    // e.g. the replay transport on review screens. null leaves the strip as just the
    // session/filter chips.
    property Component transport: null
    // Host drives this true when the transport should show (e.g. Replay/Analyse) — this is
    // the "hide/show by view" gate. Kept explicit (rather than the carousel peeking at the
    // loaded item's visibility) so the Loader and the dock-height grow stay in lockstep.
    property bool transportActive: false

    property int  selectedShotId: -1
    property Item selectedCard:   null   // live delegate; nulled by QML when it is destroyed

    // The carousel shows the live shotModel normally, or a loaded past session's
    // private model while reviewing. Both are ShotListModel (identical roles +
    // mutation invokables), so the filter proxy, cards and panel work unchanged;
    // every mutation routes through activeModel so trash/rating hit the right one.
    readonly property bool reviewing: sessionReviewController.reviewActive
    readonly property var  activeModel: reviewing ? sessionReviewController.shots
                                                  : shotModel

    // Shared popup width for the sessions drawer (~⅓ of the carousel width,
    // clamped) — wide enough that its content doesn't wrap.
    readonly property real drawerWidth:
        Math.round(Math.max(Theme.sp(420), Math.min(width / 3, Theme.sp(560))))

    // When the host injects a transport and it is showing, it sits centered in the top strip
    // — an overlay over the chips row (so it centers on the full dock width, not the space
    // left of the chips). The chips row reserves _stripBandHeight so the filmstrip clears it;
    // the dock grows to fit. Nothing showing (e.g. Capture) → the base height, unchanged.
    readonly property bool _transportShown: root.transport !== null && root.transportActive
    readonly property real _stripTopMargin:    Theme.sp(3.5)   // halved top/bottom padding
    readonly property real _stripBottomMargin: Theme.sp(4)
    readonly property real _stripBandHeight:   Theme.sp(40)    // header band height when the transport shows

    // Bumped after each in-place edit (club/rating/note) so _focusSummary re-resolves
    // — those mutations emit dataChanged (not activeCountChanged), which the invokable
    // read below wouldn't otherwise observe, leaving the identity chip stale.
    property int _editTick: 0

    // Live state for the sticky "Re-analysing…" toast (see the reanalysisController
    // Connections block below): total shots queued across the batch and elapsed
    // wall-time since it started, so a multi-swing batch (each swing runs ViTPose
    // sequentially and can take a while) never leaves the user wondering if it's stuck.
    property int _reanalyseQueuedCount: 0
    property int _reanalyseElapsedS: 0

    // Focused-shot metadata for the action bar. Re-resolved when the focused id
    // changes, the shot set mutates (touch activeCount) so a trashed focused shot
    // drops the bar's focus identity instead of lingering stale, or a field is
    // edited (touch _editTick).
    readonly property var _focusSummary: {
        void root.activeModel.activeCount
        void root._editTick
        return root.activeModel.shotSummary(SessionMode.focusedShotId)
    }
    // The scope-aware action bar shows when there is a focused shot OR a filter
    // is active; hidden otherwise (fresh Capture, nothing picked). Gating on the
    // summary's validity (not the raw focused id) means a just-trashed focused
    // shot hides the bar cleanly rather than leaving a stale identity.
    readonly property bool _barShown: (_focusSummary.valid === true) || filterProxy.filterActive
    readonly property real _barBandHeight: Theme.sp(36)

    // Grow for the transport band (chips overlay) AND the action-bar band — same
    // mechanism, summed. The bar band adds its height plus the railCol spacing it
    // introduces above the chips row, so neither clips.
    implicitHeight: Theme.carouselHeight
                    + (_transportShown ? Theme.sp(20) : 0)
                    + (_barShown ? _barBandHeight + railCol.spacing : 0)

    // Opens the export options sheet for a set of swing dirs. The ⋯ "export all
    // selected" action routes through here, sharing one options panel, one
    // exporter call and one toast.
    function _openExportSheet(dirs, emptyMsg) {
        if (dirs.length === 0) {            // analysis-only shots have no on-disk files
            toast.showUndo = false
            toast.copyText = ""
            toast.sticky   = false   // in case a re-analysis batch left the toast pinned
            toast.glyph    = "ℹ"
            toast.show(emptyMsg)
            return
        }
        exportSheet.swingDirs   = dirs
        exportSheet.cameras     = swingExporter.camerasForShots(dirs)
        exportSheet.shotCount   = dirs.length
        exportSheet.includeJson = true
        exportSheet.open()
    }

    ShotFilterProxyModel {
        id: filterProxy
        sourceModel: root.activeModel
    }

    Rectangle {   // rail background
        anchors.fill: parent
        color: Theme.colorBg2
    }
    Rectangle {   // top hairline
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
    }

    ColumnLayout {
        id: railCol
        anchors { fill: parent
                  leftMargin: Theme.sp(16); rightMargin: Theme.sp(16)
                  topMargin: root._stripTopMargin; bottomMargin: root._stripBottomMargin }
        spacing: Theme.sp(7)

        // ── Scope-aware action bar — focused-shot header + folded-in filtered-set
        //    actions. Shown only when a shot is focused or a filter is active. ──
        PpShotActionBar {
            id: actionBar
            visible: root._barShown
            Layout.fillWidth: true
            Layout.preferredHeight: root._barBandHeight

            summary:      root._focusSummary
            filterActive: filterProxy.filterActive
            visibleCount: filterProxy.visibleCount
            sourceCount:  filterProxy.sourceCount
            filterLabel:  filterProxy.filterActive ? filterProxy.filterSummary : ""

            // Clicking the focused identity chip opens the swing-edit popover.
            onEditRequested: editPopup.open()

            onExportShot: root._openExportSheet(
                              root.activeModel.swingDirsForIds([SessionMode.focusedShotId]),
                              qsTr("Nothing to export"))
            onExportShown: root._openExportSheet(
                              root.activeModel.swingDirsForIds(filterProxy.visibleShotIds()),
                              qsTr("No saved shots to export"))

            onTrashShot: {
                const ok = root.activeModel.moveToTrash(SessionMode.focusedShotId)
                toast.showUndo = false   // OS trash is the recovery path, not an in-app undo
                toast.copyText = ""
                toast.sticky   = false   // in case a re-analysis batch left the toast pinned
                toast.glyph    = "🗑"
                toast.show(ok ? qsTr("Shot moved to trash")
                              : qsTr("Could not move shot to trash"))
            }
            onTrashShown: {
                const ids = filterProxy.visibleShotIds()
                const n = root.activeModel.moveAllToTrash(ids)
                toast.showUndo = false   // OS trash is the recovery path, not an in-app undo
                toast.copyText = ""
                toast.sticky   = false   // in case a re-analysis batch left the toast pinned
                toast.glyph    = "🗑"
                toast.show(ids.length === 0 ? qsTr("No shots to trash")
                           : n === ids.length ? qsTr("%1 shots moved to trash").arg(n)
                           : qsTr("Moved %1 of %2 shots to trash").arg(n).arg(ids.length))
            }

            // Re-analyse: focused shot, or every shot in the filtered set ("all
            // shown"). Resolve swing dirs from the ACTIVE model (live or the loaded
            // session under review) — passing ids would resolve against the wrong
            // model and silently no-op. The controller drains them sequentially,
            // writing each fresh analysis back to swing.json.
            onReanalyseShot:  reanalysisController.reanalyse(
                                  root.activeModel.swingDirsForIds([SessionMode.focusedShotId]))
            onReanalyseShown: reanalysisController.reanalyse(
                                  root.activeModel.swingDirsForIds(filterProxy.visibleShotIds()))
        }

        // ── Session chip + filter combo. The transport overlays this row, centered
        //    (the Loader below railCol); reserve the band height so the filmstrip
        //    clears it when the transport shows. ──
        RowLayout {
            id: chipsRow
            Layout.alignment: Qt.AlignLeft
            Layout.minimumHeight: root._transportShown ? root._stripBandHeight : 0
            spacing: Theme.sp(4)

            Rectangle {   // session chooser chip — live name or loaded-session label
                id: sessChip
                Layout.alignment: Qt.AlignVCenter
                implicitWidth:  sessRow.implicitWidth + Theme.sp(20)
                implicitHeight: Theme.sp(22)
                radius: Theme.radius
                color:  "transparent"
                border.width: 1
                border.color: sessionsPopup.opened || sessMa.containsMouse
                                  ? Theme.colorBorderMid : "transparent"
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                Row {
                    id: sessRow
                    anchors.centerIn: parent
                    spacing: Theme.sp(7)

                    Rectangle {   // live dot — only in live mode
                        visible: !root.reviewing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.sp(7); height: Theme.sp(7); radius: Theme.sp(3.5)
                        color: Theme.colorError
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.reviewing
                                  ? (sessionReviewController.activeDayLabel
                                     + (sessionReviewController.activeTimeLabel
                                            ? " · " + sessionReviewController.activeTimeLabel : "")
                                     + qsTr(" · %1 shots").arg(sessionReviewController.activeShotCount))
                                  : qsTr("LIVE · %1 shots").arg(shotModel.activeCount)
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           "▾"
                        font.family:    Theme.fontSymbol
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }
                PpPressable {
                    id: sessMa
                    held: sessionsPopup.opened       // stay grown while the drawer is up
                    onClicked: {
                        if (sessionsPopup.opened) {
                            sessionsPopup.close()
                        } else {
                            sessionReviewController.refresh()   // re-enumerate disk + live
                            sessionsPopup.open()
                        }
                    }
                }
            }

            Rectangle {   // hairline between the session chip and the filter pill
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 1; implicitHeight: Theme.sp(14)
                color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
            }

            Rectangle {   // "N shots" / "N of M shots" when filtered
                id: filterPill
                Layout.alignment: Qt.AlignVCenter
                implicitWidth:  pillRow.implicitWidth + Theme.sp(12)
                implicitHeight: Theme.sp(22)
                radius: Theme.radius
                color:  "transparent"
                border.width: 1
                border.color: filterProxy.filterActive || pillMa.containsMouse
                                  ? Theme.colorBorderMid : "transparent"
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: Theme.sp(6)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           filterProxy.countLabel
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          filterProxy.filterActive ? Theme.colorText : Theme.colorText3
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           "▾"
                        font.family:    Theme.fontSymbol
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }
                PpPressable {
                    id: pillMa
                    held: filterPopup.opened       // stay grown while the filter popover is up
                    onClicked: filterPopup.opened ? filterPopup.close() : filterPopup.open()
                }
            }
        }

        // ── Film strip ───────────────────────────────────────────────────────
        ListView {
            id: strip
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(Theme.sp(139) * 9 / 16)
            orientation: ListView.Horizontal
            spacing:     Theme.sp(5)
            clip:        true
            model:       filterProxy

            delegate: PpShotCard {
                id: cardDelegate
                // Highlight follows the swing currently on the stage (filmstrip
                // → loupe). Single click promotes this swing into Replay; clicking
                // the already-focused card again deselects it and resets the
                // Replay/Analyse stage to its empty state.
                selected: shotId === SessionMode.focusedShotId
                onTapped: {
                    if (shotId === SessionMode.focusedShotId) {
                        root.selectedShotId = -1
                        root.selectedCard   = null
                        SessionMode.clearFocus()
                    } else {
                        root.selectedShotId = shotId
                        root.selectedCard   = cardDelegate
                        SessionMode.enterReplay(shotId, swingDir)
                    }
                }
                onRated: (n) => root.activeModel.setRating(shotId, n)
            }
        }
    }

    // Host-injected transport, centered across the dock. An overlay (not in the chips
    // row's flow) so it centers on the full width; the chips row reserves _stripBandHeight
    // so the filmstrip clears it. The transport's own content is centre-anchored, so the
    // buttons land on the dock's horizontal centre.
    Loader {
        id: transportLoader
        // Centred across the dock, but clamped so it never rides over the chips on the left:
        // true-centre on a wide dock; tucked just right of the chips on a narrow one.
        // When the action bar shows, the chips row shifts down by the bar band (+ the
        // railCol spacing it introduces) — the transport follows so it stays centred on
        // the chips row, not the bar. (x clamp keys off chipsRow.x, unchanged horizontally.)
        y: root._stripTopMargin
           + (root._barShown ? root._barBandHeight + railCol.spacing : 0)
           + (root._stripBandHeight - height) / 2
        x: {
            var centreX = root.width / 2 - width / 2
            var minX    = chipsRow.x + chipsRow.width + Theme.sp(12)
            var maxX    = root.width - Theme.sp(16) - width
            return Math.min(Math.max(centreX, minX), maxX)
        }
        active:  root._transportShown
        visible: active
        sourceComponent: root.transport
    }

    // ── Filter popover — opens upward over the left cap ──────────────────────
    Popup {
        id: filterPopup
        parent: root
        x: Theme.sp(16)
        y: -height - Theme.sp(10)
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpShotFilter { proxy: filterProxy }
    }

    // ── Swing-edit popover — club / rating / note for the focused shot ───────
    //    Opens upward over the left cap (same convention as the filter popover);
    //    edits route to the ACTIVE model (live or the session under review) and
    //    bump _editTick so the action-bar identity chip refreshes in place.
    Popup {
        id: editPopup
        parent: root
        x: Theme.sp(16)
        y: -height - Theme.sp(10)
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpSwingEditPanel {
            summary:     root._focusSummary
            clubOptions: root.activeModel.clubOptions

            onClubChosen:  (c) => { root.activeModel.setClub(SessionMode.focusedShotId, c);   root._editTick++ }
            onRated:       (n) => { root.activeModel.setRating(SessionMode.focusedShotId, n);  root._editTick++ }
            onNoteChanged: (t) => { root.activeModel.setNote(SessionMode.focusedShotId, t);    root._editTick++ }
            onCloseRequested: editPopup.close()
        }
    }

    // ── Sessions drawer — rises above the carousel, never reaches the toolbar ─
    Popup {
        id: sessionsPopup
        parent: root
        x: 0
        width: root.drawerWidth   // shared with the shot panel (siblings)
        y: -height - Theme.sp(10)

        // Clamp so the top stays below the toolbar. Window height (the overlay)
        // minus the header, toolbar and this carousel leaves the body band; the
        // drawer fills it less small gaps. Fits content when sessions are few.
        readonly property real _winH: Overlay.overlay ? Overlay.overlay.height : Theme.sp(600)
        readonly property real _bodyH: _winH - Theme.headerHeight - Theme.sp(60) - root.height
        height: Math.max(Theme.sp(120),
                         Math.min(sessDrawer.implicitHeight, _bodyH - Theme.sp(20)))

        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpSessionDrawer {
            id: sessDrawer
            onCloseRequested: sessionsPopup.close()

            // A freshly chosen session shows all its shots — drop any filter
            // left over from the previously viewed session.
            onSessionSelected: filterProxy.clearAll()

            // Per-session '...' actions reuse the carousel's shared export sheet
            // and toast. The drawer stays open so the user can act on several
            // sessions in turn; for trash the row drops out of the list in place.
            onExportRequested: (sessionId) => {
                root._openExportSheet(
                    sessionReviewController.swingDirsForSession(sessionId),
                    qsTr("No saved shots to export"))
            }
            onTrashRequested: (sessionId) => {
                const ok = sessionReviewController.trashSession(sessionId)
                toast.showUndo = false   // OS trash is the recovery path, not an in-app undo
                toast.copyText = ""
                toast.sticky   = false   // in case a re-analysis batch left the toast pinned
                toast.glyph    = "🗑"
                toast.show(ok ? qsTr("Session moved to trash")
                              : qsTr("Could not move session to trash"))
            }
        }
    }

    // ── Bulk export options — opens upward over the left cap ─────────────────
    PpExportOptionsSheet {
        id: exportSheet
        parent: root
        x: Theme.sp(16)
        y: -height - Theme.sp(10)
        onConfirmed: (selectedVideoFiles, includeJson) =>
            swingExporter.exportShots(exportSheet.swingDirs, selectedVideoFiles, includeJson)
    }

    // Completion notice for the bulk export — the copy icon copies the zip path.
    Connections {
        target: swingExporter
        function onExportFinished(ok, zipPath, error) {
            toast.showUndo = false
            toast.sticky   = false   // in case a re-analysis batch left the toast pinned
            if (ok) {
                toast.glyph    = "✓"
                toast.copyText = zipPath
                toast.show(qsTr("Exported %1").arg(zipPath.split('/').pop()))
            } else {
                toast.glyph    = "⚠"
                toast.copyText = ""
                toast.show(qsTr("Export failed: %1").arg(error))
            }
        }
    }

    // Re-analyse feedback — a sticky notice toast for the whole batch (pinned, with a
    // live elapsed-time readout, instead of PpToast's usual ~7s auto-dismiss — a batch
    // can run several sequential ViTPose passes and take much longer than that), then a
    // normal auto-dismissing outcome toast once it drains. Each finished swing refreshes
    // its row in the active model.
    Connections {
        target: reanalysisController
        function onReanalyseQueued(count) {
            // A fresh batch (not already reanalysing) resets the running total/elapsed;
            // a mid-batch top-up (user queues more while one is in flight) accumulates.
            root._reanalyseQueuedCount = reanalysisController.reanalysing
                ? root._reanalyseQueuedCount + count : count
            toast.showUndo = false
            toast.copyText = ""
            toast.glyph    = "↻"
            toast.sticky   = true
            toast.show(root._reanalyseQueuedCount === 1
                ? qsTr("Re-analysing · 1 shot")
                : qsTr("Re-analysing · %1 shots").arg(root._reanalyseQueuedCount))
        }
        function onReanalysed(swingDir) {
            root.activeModel.refreshShot(swingDir)
        }
        function onReanalyseFinished(succeeded, failed, lastError) {
            toast.sticky   = false
            root._reanalyseQueuedCount = 0
            toast.showUndo = false
            toast.copyText = ""
            toast.glyph    = "↻"
            const total = succeeded + failed
            if (total === 1) {
                // Single shot: show the actual outcome/reason, not a "0 of 1" count.
                toast.show(failed === 0
                    ? qsTr("Shot re-analysed")
                    : (lastError && lastError.length ? lastError
                                                     : qsTr("Couldn't re-analyse this shot")))
            } else {
                toast.show(failed === 0 ? qsTr("Re-analysed %1 shots").arg(succeeded)
                                        : qsTr("Re-analysed %1, %2 failed").arg(succeeded).arg(failed))
            }
        }
    }

    // Elapsed-time readout for the sticky re-analysing toast, mirroring
    // PpAnalysingBadge's elapsedLabel format (Ns, or M:SS past a minute).
    Timer {
        id: reanalyseElapsedTimer
        interval: 1000; repeat: true
        running: reanalysisController.reanalysing
        onRunningChanged: if (running) root._reanalyseElapsedS = 0
        onTriggered: {
            root._reanalyseElapsedS++
            const s = root._reanalyseElapsedS
            const label = s >= 60 ? Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
                                  : s + "s"
            toast.message = root._reanalyseQueuedCount === 1
                ? qsTr("Re-analysing · 1 shot · %1").arg(label)
                : qsTr("Re-analysing · %1 shots · %2").arg(root._reanalyseQueuedCount).arg(label)
        }
    }

    // ── Notice toast (export result / trash confirmation; no in-app undo) ─────
    PpToast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - Theme.sp(14)
        showUndo: false
    }
}
