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

// THE IN-APP FILE PICKER — a sheet in the app's own card style for choosing one file, because this
// application opens no native dialogs (no FileDialog, no FolderDialog: every action is an in-screen
// control). Built for the studio's logo and the coach's photo (session report §7.1), and generic:
// the caller sets `title`, `nameFilters` and where it starts, and listens for chosen(path).
//
//   ┌ CHOOSE A LOGO ────────────────────────────────────── ✕ ┐
//   │ Home  Desktop  Downloads  Pictures        ↑ Up one level │
//   │ /Users/mark/Pictures                                     │
//   │ ┌ folders, then matching files ┐ ┌ preview ───────────┐ │
//   │ │ ▸ Holidays                    │ │                    │ │
//   │ │   logo.png            12 KB   │ │      (image)       │ │
//   │ └───────────────────────────────┘ └ logo.png · 12 KB ──┘ │
//   │                                     Cancel   [Choose]    │
//   └──────────────────────────────────────────────────────────┘
//
// Press a folder to open it; press a file to select it (a second press, or Choose, picks it).
// Probes drive it through goTo(path), select(path) and choose(); the controls carry objectNames.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtCore
import Qt.labs.folderlistmodel
import PinPointStudio

Popup {
    id: root
    objectName: "ppFilePicker"

    property string title:       qsTr("Choose a file")
    // Files shown (folders are always shown). Globs, matched case-insensitively.
    property var    nameFilters: ["*.png", "*.jpg", "*.jpeg", "*.svg"]
    // A one-line hint under the list, e.g. what the file is used for. Empty = none.
    property string hint:        ""
    // Draw the selected file as an image on the right. Off for a picker of non-image files.
    property bool   previewImages: true
    // Where it opens: a local folder path. Empty = Pictures (else Desktop, else Home). The last folder
    // visited is remembered while the app runs, so a second pick starts where the first ended.
    property string startFolder: ""

    // The selected file's local path, "" when none.
    property string selectedPath: ""
    readonly property string currentFolder: root._toLocal(folderModel.folder)

    signal chosen(string path)
    signal cancelled()

    // ── where things are ─────────────────────────────────────────────────────
    readonly property var roots: [
        { key: "home",      label: qsTr("Home"),      url: StandardPaths.writableLocation(StandardPaths.HomeLocation) },
        { key: "desktop",   label: qsTr("Desktop"),   url: StandardPaths.writableLocation(StandardPaths.DesktopLocation) },
        { key: "downloads", label: qsTr("Downloads"), url: StandardPaths.writableLocation(StandardPaths.DownloadLocation) },
        { key: "pictures",  label: qsTr("Pictures"),  url: StandardPaths.writableLocation(StandardPaths.PicturesLocation) }
    ]
    property string _lastFolder: ""

    // file:///Users/x → /Users/x ; file:///C:/x → C:/x. A local path passes through.
    function _toLocal(u) {
        var s = String(u)
        if (s.indexOf("file://") !== 0) return s
        s = decodeURIComponent(s.substring(7))
        if (/^\/[A-Za-z]:\//.test(s)) s = s.substring(1)
        return s
    }
    function _toUrl(p) {
        var s = String(p)
        if (s.indexOf("file:") === 0) return s
        return "file://" + (/^[A-Za-z]:\//.test(s) ? "/" : "") + encodeURI(s)
    }
    function _formatSize(n) {
        if (n === undefined || n < 0) return ""
        if (n >= 1e6) return (n / 1e6).toFixed(1) + " MB"
        if (n >= 1e3) return Math.round(n / 1e3) + " KB"
        return n + " B"
    }
    function _basename(p) { var s = String(p); var i = s.lastIndexOf("/"); return i >= 0 ? s.substring(i + 1) : s }

    // ── the probe / caller API ───────────────────────────────────────────────
    function goTo(path) {
        root.selectedPath = ""
        folderModel.folder = root._toUrl(path)
        root._lastFolder = root._toLocal(folderModel.folder)
    }
    function up() {
        var parentUrl = folderModel.parentFolder
        if (String(parentUrl).length > 0) root.goTo(root._toLocal(parentUrl))
    }
    function select(path) { root.selectedPath = String(path) }
    function choose() {
        if (root.selectedPath.length === 0) return
        var p = root.selectedPath
        root.chosen(p)
        root.close()
    }

    onAboutToShow: {
        root.selectedPath = ""
        var start = root.startFolder.length > 0 ? root.startFolder
                  : root._lastFolder.length > 0 ? root._lastFolder
                  : root._toLocal(root.roots[3].url)
        if (start.length === 0) start = root._toLocal(root.roots[1].url)
        if (start.length === 0) start = root._toLocal(root.roots[0].url)
        root.goTo(start)
    }

    parent:  Overlay.overlay
    anchors.centerIn: parent
    width:   Math.min(parent ? parent.width - Theme.gap(48) : Theme.sp(760), Theme.sp(760))
    height:  Math.min(parent ? parent.height - Theme.gap(48) : Theme.sp(540), Theme.sp(540))
    padding: Theme.gap(18)
    modal:   true
    focus:   true
    closePolicy: Popup.CloseOnEscape
    onClosed: if (!root._choosing) root.cancelled()
    property bool _choosing: false
    onChosen: root._choosing = true
    onOpened: root._choosing = false

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.45 : 0.25) }
    background: PpPopoverCard {}

    FolderListModel {
        id: folderModel
        showDirs:        true
        showDirsFirst:   true
        showDotAndDotDot: false
        showHidden:      false
        caseSensitive:   false
        sortCaseSensitive: false
        nameFilters:     root.nameFilters
    }

    // A quiet text control: a root, or "Up one level".
    component PlainLink: Text {
        id: pl
        property bool on: false
        property bool active: true
        signal activated()
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    pl.on ? Font.DemiBold : Theme.fontBodyWeight
        color: !pl.active ? Theme.colorText3
             : pl.on ? Theme.colorText
             : (plMa.containsMouse ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent)
        MouseArea {
            id: plMa
            anchors.fill: parent
            anchors.margins: -Theme.sp(4)
            enabled: pl.active
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pl.activated()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.gap(10)

        // ── header ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            PpMicro { text: Theme.caps(root.title); Layout.fillWidth: true }
            Text {
                objectName: "filePickerClose"
                text: "✕"
                font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzBody
                color: closeMa.containsMouse ? Theme.colorText : Theme.colorText3
                MouseArea { id: closeMa; anchors.fill: parent; anchors.margins: -Theme.sp(6); hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: root.close() }
            }
        }

        // ── the roots and up one level ───────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(18)
            Repeater {
                model: root.roots
                delegate: PlainLink {
                    required property var modelData
                    objectName: "filePickerRoot_" + modelData.key
                    text: modelData.label
                    visible: String(modelData.url).length > 0
                    on: root._toLocal(modelData.url) === root.currentFolder
                    onActivated: root.goTo(root._toLocal(modelData.url))
                }
            }
            Item { Layout.fillWidth: true }
            PlainLink {
                objectName: "filePickerUp"
                text: "↑ " + qsTr("Up one level")
                active: String(folderModel.parentFolder).length > 0
                onActivated: root.up()
            }
        }
        Text {
            objectName: "filePickerFolder"
            Layout.fillWidth: true
            text: root.currentFolder
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
            elide: Text.ElideMiddle
        }

        // ── the list and the preview ─────────────────────────────────────────
        RowLayout {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            spacing: Theme.gap(14)

            Rectangle {
                Layout.fillWidth:  true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                radius: Theme.radius
                color:  Theme.colorSurface
                border.width: 1
                border.color: Theme.colorBorderMid
                clip: true

                ListView {
                    id: list
                    objectName: "filePickerList"
                    anchors.fill: parent
                    anchors.margins: 1
                    model: folderModel
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: row
                        required property string fileName
                        required property string filePath
                        required property bool   fileIsDir
                        required property var    fileSize
                        readonly property bool   sel: !fileIsDir && root.selectedPath === filePath
                        objectName: "filePickerRow"
                        width:  list.width
                        height: Theme.sp(30)
                        color:  row.sel ? Theme.colorAccentLight : (rowMa.containsMouse ? Theme.colorBg2 : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.gap(10)
                            anchors.rightMargin: Theme.gap(10)
                            spacing: Theme.gap(8)
                            Text {
                                text: row.fileIsDir ? "▸" : ""
                                Layout.preferredWidth: Theme.sp(12)
                                font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzMicro
                                color: Theme.colorText3
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.fileName
                                elide: Text.ElideMiddle
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                color: row.sel ? Theme.colorAccent : Theme.colorText
                            }
                            Text {
                                visible: !row.fileIsDir
                                text: root._formatSize(row.fileSize)
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                                color: Theme.colorText3
                            }
                        }
                        MouseArea {
                            id: rowMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (row.fileIsDir) root.goTo(row.filePath)
                                else if (root.selectedPath === row.filePath) root.choose()
                                else root.select(row.filePath)
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - Theme.gap(32)
                        visible: list.count === 0 && folderModel.status === FolderListModel.Ready
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Nothing here to choose — no folders, and no %1 files.")
                                .arg(root.nameFilters.map(function (f) { return f.replace("*.", "").toUpperCase() })
                                         .filter(function (f, i, a) { return a.indexOf(f) === i }).join(", "))
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                }
            }

            // The preview: what was selected, as it will be used.
            ColumnLayout {
                visible: root.previewImages
                Layout.fillHeight: true
                Layout.fillWidth:  true
                Layout.preferredWidth: 2
                spacing: Theme.gap(6)
                Rectangle {
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                    radius: Theme.radius
                    color:  Theme.colorBg2
                    Image {
                        id: preview
                        objectName: "filePickerPreview"
                        anchors.fill: parent
                        anchors.margins: Theme.gap(12)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        smooth: true
                        mipmap: true
                        sourceSize.width:  Theme.sp(600)
                        sourceSize.height: Theme.sp(600)
                        source: root.selectedPath.length > 0 ? root._toUrl(root.selectedPath) : ""
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - Theme.gap(24)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: root.selectedPath.length === 0 || preview.status === Image.Error
                        text: root.selectedPath.length === 0 ? qsTr("Select a file to see it here")
                                                             : qsTr("This file cannot be shown")
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.selectedPath.length > 0
                    // The name only: the preview is decoded at a capped size, so its dimensions are not the file's.
                    text: root._basename(root.selectedPath)
                    elide: Text.ElideMiddle
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText2
                }
            }
        }

        // ── foot ─────────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(8)
            Text {
                Layout.fillWidth: true
                visible: root.hint.length > 0
                text: root.hint
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                color: Theme.colorText3
            }
            Item { Layout.fillWidth: true; visible: root.hint.length === 0 }
            PpButton {
                objectName: "filePickerCancel"
                label: qsTr("Cancel")
                onClicked: root.close()
            }
            PpButton {
                objectName: "filePickerChoose"
                label:   qsTr("Choose")
                primary: true
                enabled: root.selectedPath.length > 0
                onClicked: root.choose()
            }
        }
    }
}
