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

// Settings → Launch Monitor (panelIndex 6).
//
// Connect a launch monitor and say where it writes. The GC Quad has two connectors, one per
// program that can hold it: FSX2020's LastShot.CSV out of a folder it is pointed at, and
// Foresight Premier's session files out of its own data folder.
//
// NO FILESYSTEM WORK ON COMPLETION. This panel is a direct StackLayout child, so it
// is built at launch whether or not anybody opens Settings — see the warning in
// StoragePanel.qml, written after a library walk on this thread cost 5-10 s of black
// window on an SMB mount. Everything shown here is a property of the controller,
// which its own poller maintains off this code path.

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import PinPointStudio

SettingsPanelBase {
    id: root

    // ── Folder dialog ─────────────────────────────────────────────────────────

    FolderDialog {
        id: folderDialog
        title: root.isPremier ? qsTr("Select Foresight Premier's data folder")
                              : qsTr("Select the folder FSX2020 writes LastShot.CSV into")
        // urlToLocalFile, never a string strip: QML's url type has no toLocalFile()
        // and trimming "file://" leaves a stray slash before a Windows drive letter,
        // which is exactly the platform this folder usually lives on.
        // Each connector keeps its own folder, so choosing one never loses the other's.
        onAccepted: {
            const p = appSettings.urlToLocalFile(selectedFolder)
            if (root.isPremier) appSettings.launchMonitorPremierPath = p
            else                appSettings.launchMonitorPath = p
        }
    }

    // ── Option model ──────────────────────────────────────────────────────────

    readonly property var deviceOptions: [
        { label: qsTr("None"),                        value: "none"   },
        // One device, two connectors: only one program can hold the Quad, so the user
        // picks the one they are running.
        { label: qsTr("GC Quad — FSX2020"),             value: "gcquad"  },
        { label: qsTr("GC Quad — Foresight Premier"),   value: "premier" },
        // ⚠ NAMED FOR THE PROTOCOL, NOT A DEVICE. One connector receives from
        // everything that has an Open Connect bridge, so naming any single monitor
        // would send everyone else looking for their own. The three in brackets are
        // the best-evidenced bridges — deliberately not Uneekor or Bushnell, which
        // ship no such client at all and are reached, if ever, the GCQuad way.
        { label: qsTr("GSPro Connect (R10, MLM2PRO, SkyTrak+, …)"), value: "gspro" }
    ]

    // ⚠ THREE DIFFERENT QUESTIONS, and the panel needs all three. `chosen` is what
    // the dropdown says; `enabled` is the switch; `configured` is both, which is
    // the only one that means "there is a connector running". The editable rows
    // follow `chosen` — a dormant link must stay editable, since setting it up
    // while GSPro has the port is exactly when somebody would.
    // ⚠ NOT `enabled`: that is Item's own property, and shadowing it here made Qt
    // warn — rightly. A root Item whose `enabled` follows a setting would hand the
    // whole panel's input handling to that setting, so switching the connector off
    // would grey out the switch that turns it back on.
    readonly property bool chosen:     appSettings.launchMonitorKind !== "none"
    readonly property bool switchedOn: appSettings.launchMonitorEnabled
    readonly property bool configured: root.chosen && root.switchedOn
    readonly property bool isGcQuad:   appSettings.launchMonitorKind === "gcquad"
    readonly property bool isGsPro:    appSettings.launchMonitorKind === "gspro"
    readonly property bool isPremier:  appSettings.launchMonitorKind === "premier"
    // The folder the chosen connector reads. Premier's setting is empty until someone
    // changes it, and empty means Premier's own data folder for this account.
    readonly property string folderPath: root.isPremier
        ? (appSettings.launchMonitorPremierPath !== "" ? appSettings.launchMonitorPremierPath
                                                       : launchMonitor.premierDefaultFolder)
        : appSettings.launchMonitorPath

    readonly property color statusColor:
        launchMonitor.state === "ready"   ? Theme.colorGood
      : launchMonitor.state === "error"   ? Theme.colorError
      : launchMonitor.state === "waiting" ? Theme.colorAttention
      :                                     Theme.colorText3

    // ── Page header ───────────────────────────────────────────────────────────

    PpPageHeader {
        Layout.fillWidth: true
        spacing:     root.contentSpacing
        eyebrow:     qsTr("Devices")
        title:       qsTr("Launch Monitor")
        description: qsTr("A launch monitor measures ball and club data we cannot see optically — face angle, spin, strike location — and measures several we estimate ourselves. Both are kept: its readings are stored alongside our own, never in place of them, so the two can be compared shot by shot.")
    }

    // ── Group 1 — Connection ──────────────────────────────────────────────────

    PpMicro { kind: "body"; text: qsTr("Connection") }

    // Device
    SettingsRow {
        objectName: "setting_lmDevice"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Device")
                subtitle: root.isGsPro
                          ? qsTr("Launch monitors connect TO PinPoint over the network. Point the device's own app at this machine's address and the port below")
                          : root.isPremier
                          ? qsTr("Run Foresight Premier on this PC, under this Windows account. Shots arrive from the range, not from training drills, and each shot's club data follows a shot later")
                          : qsTr("FSX2020 is Windows-only, but the folder it writes to can be a share — so this works from any machine that can see it")
            }
            PpComboBox {
                Layout.preferredWidth: Theme.sp(240)
                model: root.deviceOptions.map(function (o) { return o.label })
                currentIndex: {
                    for (var i = 0; i < root.deviceOptions.length; ++i)
                        if (root.deviceOptions[i].value === appSettings.launchMonitorKind)
                            return i
                    return 0
                }
                onActivated: (index) => appSettings.launchMonitorKind = root.deviceOptions[index].value
            }
        }
    }

    // ── The switch ────────────────────────────────────────────────────────────
    // Separate from the dropdown because "which device" and "is it running"
    // are different decisions with different lifetimes. The case that forces
    // it: GSPro itself wants port 921, so PinPoint's listener has to go
    // dormant for an hour — and nobody should have to dismantle a working
    // configuration to do that, or rebuild it afterwards.
    SettingsRow {
        objectName: "setting_lmEnabled"
        visible: root.chosen
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Enabled")
                subtitle: root.isGsPro
                          ? qsTr("Switch the link off to hand the port back — GSPro cannot use 921 while PinPoint is listening on it. Everything below is remembered, so switching it on again needs no setting up.")
                          : qsTr("Switch the connector off without losing its configuration. Nothing is watched and no readings arrive while it is off.")
            }
            PpSwitch {
                checked:   appSettings.launchMonitorEnabled
                enabled:   root.chosen
                opacity:   root.chosen ? 1.0 : 0.4
                onToggled: (v) => appSettings.launchMonitorEnabled = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Folder — the GCQuad's source. Hidden rather than disabled for the GSPro
    // link: a listener has no folder, and a greyed-out folder picker invites
    // the question "what should I put there?".
    SettingsRow {
        objectName: "setting_lmPath"
        visible: !root.isGsPro
        opacity: root.chosen ? 1.0 : 0.45
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(3)
                SettingsRowTitle {
                    title:    qsTr("Shot data folder")
                    subtitle: root.isPremier
                              ? qsTr("Foresight Premier's data folder for this Windows account, found for you. Change it only if Premier keeps its data somewhere else")
                              : qsTr("The folder FSX2020 writes LastShot.CSV into — the folder, not the file")
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight:   Theme.sp(28)
                    color:            Theme.colorBg2
                    radius:           Theme.radius
                    border.width:     1
                    border.color:     Theme.colorBorderMid
                    Text {
                        anchors.fill:            parent
                        anchors.leftMargin:      Theme.gap(8)
                        anchors.rightMargin:     Theme.gap(8)
                        verticalAlignment:       Text.AlignVCenter
                        elide:                   Text.ElideLeft
                        text:                    root.folderPath !== ""
                                                 ? root.folderPath
                                                 : qsTr("No folder selected")
                        font.family:             Theme.fontData
                        font.pixelSize:          Theme.fontSzMicro
                        color:                   root.folderPath !== ""
                                                 ? Theme.colorText : Theme.colorText3
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.gap(6)
                PpButton {
                    label:     qsTr("Change…")
                    enabled:   root.chosen
                    onClicked: folderDialog.open()
                }
                PpButton {
                    label:     qsTr("Open")
                    enabled:   root.chosen && root.folderPath !== ""
                    onClicked: Qt.openUrlExternally(appSettings.fileUrlFor(root.folderPath))
                }
            }
        }
    }

    // ── The GSPro link: where devices connect ─────────────────────────────────
    // Port and interface, and then the devices actually on the link. A folder
    // is configured and that is the end of it; a launch monitor CONNECTS, says
    // who it is, and can go away again — so this half of the panel answers a
    // question the GCQuad half does not have.
    SettingsRow {
        objectName: "setting_lmGsProLink"
        visible: root.isGsPro
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            // ⚠ 921 is the protocol's own port and it is BELOW 1024, which
            // costs something on two of the three platforms. The panel says
            // so here rather than only in an error nobody sees until it
            // fails: on macOS "All interfaces" is what makes 921 work at
            // all, and on Linux there is one sysctl or a higher port.
            SettingsRowTitle {
                title:    qsTr("Link")
                subtitle: qsTr("Launch monitors connect to this port. 921 is the protocol's default; below 1024 it needs \"All interfaces\" on macOS, or a sysctl on Linux — any port above 1024 works everywhere, set the same one in the device's app")
            }

            ColumnLayout {
                spacing: Theme.gap(6)
                RowLayout {
                    spacing: Theme.gap(8)
                    Text {
                        text:           qsTr("Port")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                    PpTextField {
                        Layout.preferredWidth: Theme.sp(80)
                        text:                  String(appSettings.gsProPort)
                        inputMethodHints:      Qt.ImhDigitsOnly
                        validator:             IntValidator { bottom: 1; top: 65535 }
                        onEditingFinished:     appSettings.gsProPort = parseInt(text)
                    }
                }
                PpComboBox {
                    Layout.preferredWidth: Theme.sp(160)
                    model: [ qsTr("All interfaces"), qsTr("This machine only") ]
                    currentIndex: appSettings.gsProInterface === "loopback" ? 1 : 0
                    onActivated: (index) => appSettings.gsProInterface = (index === 1 ? "loopback" : "any")
                }
            }
        }
    }

    // Status — the whole point of this row is answering "did I get the path right"
    // without having to go and hit a ball.
    SettingsRow {
        objectName: "setting_lmStatus"
        visible: root.configured
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            Rectangle {
                width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                color: root.statusColor
                Layout.alignment: Qt.AlignVCenter
            }

            SettingsRowTitle {
                title:    launchMonitor.stateLabel
                subtitle: launchMonitor.errorText !== "" ? launchMonitor.errorText
                        : launchMonitor.lastReading !== "" ? qsTr("Last reading — %1").arg(launchMonitor.lastReading)
                        : launchMonitor.sourceText !== "" ? launchMonitor.sourceText
                        : qsTr("Nothing read yet")
            }
        }
    }

    PpDivider { Layout.fillWidth: true }

    // ── Group 2 — Behaviour ───────────────────────────────────────────────────

    PpMicro { kind: "body"; text: qsTr("Behaviour") }

    // Arrival chime
    SettingsRow {
        objectName: "setting_lmChime"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Chime when a reading arrives")
                subtitle: qsTr("A short, quiet tone a few seconds after the shot chime, when the monitor's data has been folded into the swing. The fourth dot in the capture toolbar shows the same thing silently.")
            }
            PpSwitch {
                checked:   appSettings.launchMonitorChimeEnabled
                enabled:   root.configured
                opacity:   root.configured ? 1.0 : 0.4
                onToggled: (v) => appSettings.launchMonitorChimeEnabled = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Standalone shots — the one row here that changes what a shot IS.
    SettingsRow {
        objectName: "setting_lmStandalone"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Record shots the monitor sees on its own")
                subtitle: qsTr("Creates a swing from the monitor's reading alone, with no video and no analysis — only its own measurements. Only while CAPTURE IS ACTIVE, with an athlete selected and a session running. Leave this off unless you are hitting into the monitor without cameras: while capture is running it records every ball the monitor sees, including another player's.")
            }
            PpSwitch {
                checked:   appSettings.launchMonitorStandalone
                enabled:   root.configured
                opacity:   root.configured ? 1.0 : 0.4
                onToggled: (v) => appSettings.launchMonitorStandalone = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Premier's club becomes the session's club. Premier only: FSX2020 names just a family
    // ("Irn") and is usually left unchanged, so following it would make the club worse.
    SettingsRow {
        objectName: "setting_lmFollowClub"
        visible: root.isPremier
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Use the club selected in Premier")
                subtitle: qsTr("Each shot sets the session's club to the one chosen in Foresight Premier, so you change club in one place. A club PinPoint has no name for (a 7 wood, say) leaves the session's club as it is, and a club you set on a shot yourself is never changed.")
            }
            PpSwitch {
                checked:   appSettings.launchMonitorFollowClub
                enabled:   root.configured
                opacity:   root.configured ? 1.0 : 0.4
                onToggled: (v) => appSettings.launchMonitorFollowClub = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Poll interval
    SettingsRow {
        objectName: "setting_lmPoll"
        opacity: root.configured ? 1.0 : 0.45
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Check for new shots every")
                subtitle: qsTr("Raise this only if the folder is on a slow or busy network share")
            }
            PpComboBox {
                Layout.preferredWidth: Theme.sp(140)
                enabled: root.configured
                readonly property var msValues: [100, 250, 500, 1000, 2000]
                model: [qsTr("0.1 s"), qsTr("0.25 s"), qsTr("0.5 s"), qsTr("1 s"), qsTr("2 s")]
                currentIndex: Math.max(0, msValues.indexOf(appSettings.launchMonitorPollMs))
                onActivated: (index) => appSettings.launchMonitorPollMs = msValues[index]
            }
        }
    }
}
