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

// THE SESSIONS SELECTION BAR (docs/design/sessions_screen_redesign.md §2.4) — floats at the foot of the
// Sessions screen while anything is selected, or while a bulk job runs:
//
//   3 selected  3.6 GB · 2 reports out of date  Clear     [Make / remake reports] Email to Nick  Export…  Archive  Delete…
//   Making reports · 2 of 5  Cancel                                                 (while a job runs)
//
// The summary is the list's selectionInfo. Each button is live when ANY selected row can take it, and
// otherwise says why in its tooltip; the rows that cannot are skipped by the job, never failed. Archive
// reads Restore when every selected row is archived. Everything goes through the list (askBulk), which
// owns the confirms, the export sheet and the queue (PpSessionBulkJobs).

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

Rectangle {
    id: bar
    objectName: "sessionsSelectionBar"

    // The PpSessionReportList it acts for.
    property var list: null

    readonly property var info: bar.list ? bar.list.selectionInfo : ({ count: 0 })
    readonly property var jobs: bar.list ? bar.list.jobs : null
    readonly property bool running: !!(bar.jobs && bar.jobs.running)
    readonly property string who: bar.jobs ? bar.jobs.who(bar.list) : ""

    visible: (bar.info.count || 0) > 0 || bar.running
    implicitHeight: Theme.sp(56)
    radius: Theme.radiusLg
    color: Theme.colorBg2
    border.width: 1
    border.color: Qt.alpha(Theme.colorAccent, 0.45)

    // ── What the selection can take, and why not (the queue's rules: PpSessionBulkJobs.whyNot) ──
    readonly property int  n:          bar.info.count || 0
    readonly property bool allArchived: bar.n > 0 && (bar.info.archived || 0) === bar.n
    // The selection's tally, and what else the reasons turn on.
    readonly property var facts: Object.assign({}, bar.info, {
        greeting:       bar.list ? bar.list.greeting : "",
        email:          bar.list ? bar.list.email : undefined,
        sendBusy:       !!(bar.list && bar.list.sendBusy),
        reportsBusy:    !!(bar.list && bar.list.reports && bar.list.reports.busy),
        archiveReady:   !!(bar.list && bar.list.archiveReady),
        archiverBusy:   !!(bar.list && bar.list.archiver && bar.list.archiver.busy),
        archiverStatus: bar.list && bar.list.archiver ? (bar.list.archiver.status || "") : ""
    })
    function whyNot(verb) { return bar.jobs ? bar.jobs.whyNot(verb, bar.facts, bar.n) : "" }
    readonly property string whyNotMake:    bar.whyNot("make")
    readonly property string whyNotEmail:   bar.whyNot("send")
    readonly property string whyNotExport:  bar.whyNot("export")
    readonly property string whyNotArchive: bar.whyNot("archive")
    readonly property string whyNotDelete:  bar.whyNot("trash")

    readonly property var verbWords: ({
        make: qsTr("Making reports"), send: qsTr("Sending reports"), export: qsTr("Exporting"),
        archive: qsTr("Archiving"), restore: qsTr("Restoring"), trash: qsTr("Moving to the trash")
    })

    // A PpButton that says why it is off: the hover is the wrapper's, as a disabled button takes none.
    component BarButton: Item {
        id: bb
        property string label: ""
        property bool   primary: false
        property bool   destructive: false
        property string why: ""                 // "" = it can
        readonly property bool can: bb.why === ""
        signal clicked()
        implicitWidth:  btn.implicitWidth
        implicitHeight: btn.implicitHeight
        PpButton {
            id: btn
            anchors.fill: parent
            label: bb.label
            primary: bb.primary
            destructive: bb.destructive
            enabled: bb.can
            onClicked: bb.clicked()
        }
        HoverHandler { id: bbHover }
        ToolTip.visible: bbHover.hovered && !bb.can
        ToolTip.text:    bb.why
        ToolTip.delay:   300
    }

    // ── The summary, or the job's progress line ──────────────────────────────
    Row {
        id: summary
        visible: !bar.running
        anchors { left: parent.left; leftMargin: Theme.sp(Theme.rowPadH); verticalCenter: parent.verticalCenter }
        spacing: Theme.gap(12)
        Text {
            id: selCount
            objectName: "sessionsSelectedCount"
            text: qsTr("%1 selected").arg(bar.n)
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
            font.weight: Font.DemiBold
            color: Theme.colorText
        }
        Text {
            objectName: "sessionsSelectedSummary"
            anchors.baseline: selCount.baseline
            // Whatever room the buttons leave.
            width: Math.max(0, Math.min(implicitWidth, buttons.x - summary.x - selCount.width - clearLink.width
                                                       - 2 * summary.spacing - Theme.gap(16)))
            elide: Text.ElideRight
            text: [Theme.formatBytes(bar.info.bytes)]
                  .concat((bar.info.stale || 0) > 0 ? [Theme.plural(bar.info.stale, qsTr("%1 report out of date"),
                                                                    qsTr("%1 reports out of date"))] : [])
                  .join(" · ")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
            font.letterSpacing: Theme.trackingData
            color: Theme.colorText3
        }
        PpLink {
            id: clearLink
            objectName: "sessionsClearSelection"
            anchors.baseline: selCount.baseline
            text: qsTr("Clear")
            pressMargin: Theme.sp(3)
            onClicked: if (bar.list) bar.list.clearSelection()
        }
    }

    Row {
        id: progress
        objectName: "sessionsBulkProgress"
        visible: bar.running
        anchors { left: parent.left; leftMargin: Theme.sp(Theme.rowPadH); verticalCenter: parent.verticalCenter }
        spacing: Theme.gap(12)
        Text {
            id: progressWords
            objectName: "sessionsBulkProgressWords"
            text: !bar.jobs ? ""
                : (bar.verbWords[bar.jobs.verb] || "") + " · "
                  + qsTr("%1 of %2").arg(Math.min(bar.jobs.total, bar.jobs.done + 1)).arg(bar.jobs.total)
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
            font.weight: Font.DemiBold
            color: Theme.colorText
        }
        PpLink {
            objectName: "sessionsBulkCancel"
            anchors.baseline: progressWords.baseline
            text: bar.jobs && bar.jobs.cancelling ? qsTr("Stopping after this one…") : qsTr("Cancel")
            interactive: !!bar.jobs && !bar.jobs.cancelling
            pressMargin: Theme.sp(3)
            onClicked: if (bar.jobs) bar.jobs.cancel()
        }
    }

    // ── The actions ──────────────────────────────────────────────────────────
    Row {
        id: buttons
        visible: !bar.running
        anchors { right: parent.right; rightMargin: Theme.sp(Theme.rowPadH) - Theme.gap(6); verticalCenter: parent.verticalCenter }
        spacing: Theme.gap(10)
        BarButton {
            objectName: "sessionsBulkMake"
            label: qsTr("Make / remake reports")
            primary: true
            why: bar.whyNotMake
            onClicked: bar.list.askBulk("make", bar.list.selection)
        }
        BarButton {
            objectName: "sessionsBulkEmail"
            label: qsTr("Email to %1").arg(bar.who)
            why: bar.whyNotEmail
            onClicked: bar.list.askBulk("send", bar.list.selection)
        }
        BarButton {
            objectName: "sessionsBulkExport"
            label: qsTr("Export…")
            why: bar.whyNotExport
            onClicked: bar.list.askBulk("export", bar.list.selection)
        }
        BarButton {
            objectName: "sessionsBulkArchive"
            label: bar.allArchived ? qsTr("Restore") : qsTr("Archive")
            why: bar.whyNotArchive
            onClicked: bar.list.askBulk(bar.allArchived ? "restore" : "archive", bar.list.selection)
        }
        BarButton {
            objectName: "sessionsBulkDelete"
            label: qsTr("Delete…")
            destructive: true
            why: bar.whyNotDelete
            onClicked: bar.list.askBulk("trash", bar.list.selection)
        }
    }
}
