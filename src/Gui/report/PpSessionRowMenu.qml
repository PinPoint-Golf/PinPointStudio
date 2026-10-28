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

// A SESSION'S ⋯ MENU (docs/design/sessions_screen_redesign.md §2.5, §8 S-D5) — one for the whole
// Sessions table, opened by PpSessionReportList on the row whose ⋯ was pressed. It acts on THAT ROW
// ONLY, never the selection.
//
//   Review in Live                     ("Go to Live" on the running session; "Restore and review")
//   ─
//   Make report | Open report + Remake report | Remake report + Open old report
//   Email to Nick                      or why not: "make report first" (inactive) / "add Nick's
//                                      email" (the golfer page)
//   Copy address
//   ─
//   Export…   Show in Finder   Archive | Restore
//   ─
//   Delete…                            the warn colour
//
// Export, Archive / Restore and Delete go to the list's askBulk with this row alone: its confirm, then the
// bulk queue (PpSessionBulkJobs) with one dir. The live session is never offered to archive or delete —
// the item stays, inactive, and says why; so do Archive / Restore with no archive folder set, and all
// three while a bulk job runs. A session that cannot report says why on its report item rather than
// hiding it.
//
// The Popup + PpPopoverCard pattern of the carousel drawer's row menu (session/PpSessionDrawer.qml):
// 34-px hover rows, hairline separators, the destructive item in colorWarn.

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

Popup {
    id: menu
    objectName: "sessionRowMenu"

    // ── The row it is open on ────────────────────────────────────────────────
    property string dir:         ""
    property bool   isLive:      false
    property bool   archived:    false
    property bool   canReport:   false
    property string reportState: ""
    property string pdfPath:     ""
    property bool   making:      false
    property bool   hasSwings:   true
    // ── The list's ───────────────────────────────────────────────────────────
    property bool   archiveReady: true      // Settings → Archiving has a folder
    property bool   jobRunning:   false     // a bulk job holds the queue
    // ── The golfer and the mailer (the list's) ───────────────────────────────
    property string greeting:    ""
    property string email:       ""
    property bool   sendBusy:    false

    signal reviewRequested()
    signal makeReportRequested()
    signal openReportRequested()
    signal openOldReportRequested()
    signal emailRequested()
    signal golferDetailsRequested()
    signal copyAddressRequested()
    signal revealRequested()
    signal exportRequested()
    signal archiveRequested()
    signal deleteRequested()

    readonly property bool _reportable: menu.canReport && !menu.isLive && !menu.archived
    readonly property bool _hasPdf: menu.pdfPath !== "" && (menu.reportState === "current" || menu.reportState === "stale")
    readonly property string _who: menu.greeting !== "" ? menu.greeting : qsTr("the golfer")
    readonly property string _notReportable: menu.isLive   ? qsTr("still recording")
                                           : menu.archived ? qsTr("restore it first")
                                           :                 qsTr("needs 3 swings")
    readonly property string _revealWords: Qt.platform.os === "osx"     ? qsTr("Show in Finder")
                                         : Qt.platform.os === "windows" ? qsTr("Show in Explorer")
                                         :                                qsTr("Show folder")

    padding: Theme.gap(5)
    margins: Theme.gap(8)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Theme.sp(236)
    background: PpPopoverCard {}

    // One menu line: its words, an optional quiet note after them, inactive or destructive. (An inline
    // component cannot reach `menu`, so each use closes the menu in its own onTriggered.)
    component MenuLine: Rectangle {
        id: it
        property string text:     ""
        property string note:     ""
        property bool   active:   true
        property bool   warn:     false
        property bool   linkNote: false       // the note is the way to fix it (accent)
        signal triggered()

        width: parent ? parent.width : 0
        height: Math.max(Theme.sp(34), words.implicitHeight + Theme.sp(14))
        radius: Theme.radius
        color: !it.active ? "transparent"
             : itPress.containsMouse ? (it.warn ? Theme.colorWarnLight : Theme.colorBg2) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Text {
            id: words
            anchors { left: parent.left; right: parent.right; leftMargin: Theme.gap(10); rightMargin: Theme.gap(10)
                      verticalCenter: parent.verticalCenter }
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            readonly property color ink: it.warn ? (it.active ? Theme.colorWarn : Qt.alpha(Theme.colorWarn, 0.5))
                                       : it.active ? Theme.colorText : Theme.colorText3
            readonly property color noteInk: it.linkNote ? Theme.colorAccent : Theme.colorText3
            text: it.text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                  + (it.note !== "" ? " <font color=\"" + noteInk + "\">· " + it.note.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font>" : "")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
            font.weight: Theme.fontBodyWeight
            color: ink
        }
        PpPressable {
            id: itPress
            enabled: it.active
            hoverScale: 1.0       // full-width menu item — press-dip only
            onClicked: it.triggered()
        }
    }
    component Rule: Rectangle {
        width: parent ? parent.width - Theme.sp(12) : 0
        x: Theme.sp(6)
        height: 1
        color: Theme.colorBorderMid
        opacity: Theme.borderOpacityNormal
    }
    component Gap: Item { width: 1; height: Theme.gap(4) }

    contentItem: Column {
        MenuLine {
            objectName: "menuReview"
            text: menu.isLive ? qsTr("Go to Live") : menu.archived ? qsTr("Restore and review") : qsTr("Review in Live")
            onTriggered: { menu.close(); menu.reviewRequested() }
        }
        Gap {}
        Rule {}
        Gap {}

        // The report: make it, open it, remake it.
        MenuLine {
            objectName: "menuReport"
            text: menu.reportState === "current" ? qsTr("Open report")
                : menu.reportState === "stale"   ? qsTr("Remake report")
                :                                  qsTr("Make report")
            active: menu._reportable && !menu.making && (menu.reportState !== "current" || menu.pdfPath !== "")
            note: !menu._reportable ? menu._notReportable : menu.making ? qsTr("making it now") : ""
            onTriggered: { menu.close(); menu.reportState === "current" ? menu.openReportRequested() : menu.makeReportRequested() }
        }
        // A current report can always be made again (a model change leaves it current but out of step).
        MenuLine {
            objectName: "menuRemake"
            visible: menu._reportable && menu.reportState === "current"
            text: qsTr("Remake report")
            active: !menu.making
            onTriggered: { menu.close(); menu.makeReportRequested() }
        }
        MenuLine {
            objectName: "menuOpenOld"
            visible: menu._reportable && menu.reportState === "stale" && menu.pdfPath !== ""
            text: qsTr("Open old report")
            onTriggered: { menu.close(); menu.openOldReportRequested() }
        }
        // Email to Nick — or why not. A missing address is the fix the coach can make from here.
        MenuLine {
            objectName: "menuEmail"
            readonly property bool noAddress: menu.email === ""
            text: qsTr("Email to %1").arg(menu._who)
            active: menu._reportable && !menu.making && !menu.sendBusy && (noAddress || menu._hasPdf)
            linkNote: noAddress && active
            note: !menu._reportable ? ""
                : noAddress          ? qsTr("add %1's email").arg(menu._who)
                : !menu._hasPdf      ? qsTr("make report first")
                : menu.sendBusy      ? qsTr("another is sending")
                :                      ""
            onTriggered: { menu.close(); noAddress ? menu.golferDetailsRequested() : menu.emailRequested() }
        }
        MenuLine {
            objectName: "menuCopyAddress"
            text: qsTr("Copy address")
            active: menu.email !== ""
            onTriggered: { menu.close(); menu.copyAddressRequested() }
        }
        Gap {}
        Rule {}
        Gap {}

        MenuLine {
            objectName: "menuExport"
            text: qsTr("Export…")
            active: !menu.isLive && menu.hasSwings && !menu.jobRunning
            note: menu.isLive     ? qsTr("export it from Live")
                : !menu.hasSwings ? qsTr("no swings")
                : menu.jobRunning ? qsTr("another job is running") : ""
            onTriggered: { menu.close(); menu.exportRequested() }
        }
        MenuLine {
            objectName: "menuReveal"
            text: menu._revealWords
            active: menu.dir !== ""
            onTriggered: { menu.close(); menu.revealRequested() }
        }
        MenuLine {
            objectName: "menuArchive"
            text: menu.archived ? qsTr("Restore") : qsTr("Archive")
            active: !menu.isLive && menu.archiveReady && !menu.jobRunning
            note: menu.isLive        ? qsTr("still recording")
                : !menu.archiveReady ? qsTr("set an archive folder in Settings → Archiving")
                : menu.jobRunning    ? qsTr("another job is running") : ""
            onTriggered: { menu.close(); menu.archiveRequested() }
        }
        Gap {}
        Rule {}
        Gap {}

        MenuLine {
            objectName: "menuDelete"
            text: qsTr("Delete…")
            warn: true
            active: !menu.isLive && !menu.jobRunning
            note: menu.isLive     ? qsTr("still recording")
                : menu.jobRunning ? qsTr("another job is running") : ""
            onTriggered: { menu.close(); menu.deleteRequested() }
        }
    }
}
