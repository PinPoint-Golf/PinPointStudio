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

pragma Singleton
import QtQuick
import PinPoint

// Full-text search index for all settings panels.
//
// panelIndex → panel name mapping:
//   0  General        4  IMUs
//   1  Appearance     5  Launch Monitor (placeholder — no entries yet)
//   2  Displays       6  Storage
//   3  Cameras        7  Archiving      (placeholder — no entries yet)
//
// label / subtitle / groupLabel / panelLabel are wrapped in qsTr() so they
// are extracted by lupdate and searched against the user's active locale.
// Language changes require restart; the singleton re-evaluates with the
// new locale on next launch.
//
// actions is intentionally NOT translated — it is a bag of supplementary
// English keyword hints that help catch button and chip labels. When
// translations are added, per-language keyword bags can be added here.
//
// itemId is the objectName on the searchable row in its panel file.
// An empty itemId navigates to the panel without scrolling.
// If a row's objectName changes, the entry here must change to match.

QtObject {
    id: root

    readonly property var entries: [

        // ── General (panelIndex: 0) ───────────────────────────────────────────

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Localisation"),
          label: qsTr("Language"),                        subtitle: qsTr("Restart required to apply"),
          itemId: "setting_language" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Localisation"),
          label: qsTr("Units"),                           subtitle: qsTr("Speed and distance displayed throughout"),
          itemId: "setting_units" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Session behaviour"),
          label: qsTr("Auto-detect swing start"),         subtitle: qsTr("Begins capture when motion exceeds threshold"),
          itemId: "setting_autoDetect" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Session behaviour"),
          label: qsTr("Swing detection sensitivity"),     subtitle: qsTr("Lower values trigger on slower movements"),
          itemId: "setting_swingSensitivity" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Session behaviour"),
          label: qsTr("AI coaching on session end"),      subtitle: qsTr("Automatically generate a Claude observation"),
          itemId: "setting_aiCoaching" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Application"),
          label: qsTr("Check for updates automatically"), subtitle: qsTr("Checks on launch — never downloads without confirmation"),
          itemId: "setting_checkUpdates" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Application"),
          label: qsTr("Send anonymous diagnostics"),      subtitle: qsTr("Crash reports and performance data only"),
          itemId: "setting_diagnostics" },

        { panelIndex: 0, panelLabel: qsTr("General"),    groupLabel: qsTr("Application"),
          label: qsTr("Version"),                         subtitle: qsTr("Installed application version and update status"),
          actions: "Version app version up to date release build number",
          itemId: "setting_version" },

        // ── Appearance (panelIndex: 1) ────────────────────────────────────────

        { panelIndex: 1, panelLabel: qsTr("Appearance"),  groupLabel: qsTr("Aesthetic & colour mode"),
          label: qsTr("Aesthetic"),                        subtitle: qsTr("Visual theme applied across all screens"),
          itemId: "setting_aesthetic" },

        { panelIndex: 1, panelLabel: qsTr("Appearance"),  groupLabel: qsTr("Type scale"),
          label: qsTr("Text size"),                        subtitle: qsTr("Scales all fonts and spacing proportionally"),
          itemId: "setting_textSize" },

        { panelIndex: 1, panelLabel: qsTr("Appearance"),  groupLabel: qsTr("Interface"),
          label: qsTr("Density"),                          subtitle: qsTr("Compact reduces rail and panel padding"),
          itemId: "setting_density" },

        { panelIndex: 1, panelLabel: qsTr("Appearance"),  groupLabel: qsTr("Interface"),
          label: qsTr("Reduce motion"),                    subtitle: qsTr("Disables transitions and animations throughout"),
          itemId: "setting_reduceMotion" },

        { panelIndex: 1, panelLabel: qsTr("Appearance"),  groupLabel: qsTr("Interface"),
          label: qsTr("Overlay opacity"),                  subtitle: qsTr("Skeleton and angle guide overlays"),
          itemId: "setting_overlayOpacity" },

        // ── Displays (panelIndex: 2) ──────────────────────────────────────────

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Main window"),
          label: qsTr("Launch on"),                        subtitle: qsTr("Which display opens the main Pinpoint window"),
          itemId: "setting_launchOn" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Main window"),
          label: qsTr("Remember window size and position"), subtitle: qsTr("Restores last session geometry on next launch"),
          itemId: "setting_rememberGeometry" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Main window"),
          label: qsTr("Launch in full screen"),            subtitle: qsTr("Overrides saved window size if enabled"),
          itemId: "setting_fullScreen" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Post-shot display"),
          label: qsTr("Secondary display"),                subtitle: qsTr("Golfer-facing screen shown after each swing"),
          itemId: "setting_secondaryDisplay" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Post-shot display"),
          label: qsTr("Post-shot content"),                subtitle: qsTr("What the golfer sees on the secondary display"),
          itemId: "setting_postShotContent" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Post-shot display"),
          label: qsTr("Display delay"),                    subtitle: qsTr("Pause before showing post-shot content"),
          itemId: "setting_postShotDelay" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Post-shot display"),
          label: qsTr("Mirror main window"),               subtitle: qsTr("Shows the full Pinpoint interface on the secondary display"),
          itemId: "setting_mirrorMain" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Rendering"),
          label: qsTr("Frame rate cap"),                   subtitle: qsTr("UI rendering — independent of camera capture rate"),
          itemId: "setting_frameRateCap" },

        { panelIndex: 2, panelLabel: qsTr("Displays"),    groupLabel: qsTr("Rendering"),
          label: qsTr("Hardware acceleration"),            subtitle: qsTr("Uses GPU for video decode and overlay rendering"),
          itemId: "setting_hwAccel" },

        // ── Cameras (panelIndex: 3) ───────────────────────────────────────────

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Global capture"),
          label: qsTr("Pre-roll buffer"),                  subtitle: qsTr("Seconds of frames held before swing trigger fires"),
          actions: "Refresh enumerate rescan detected cameras hardware",
          itemId: "setting_preroll" },

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Global capture"),
          label: qsTr("Synchronise cameras"),              subtitle: qsTr("Lock frame timing across all enabled cameras"),
          itemId: "setting_camSync" },

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Camera device settings"),
          label: qsTr("Camera view assignment"),           subtitle: qsTr("Assign each camera to a perspective"),
          actions: "View Face-on Down-the-line perspective unassigned Other",
          itemId: "" },

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Camera device settings"),
          label: qsTr("Camera frame rate"),                subtitle: qsTr("Set the capture frame rate for each camera"),
          actions: "Frame rate fps 30 60 120 240",
          itemId: "" },

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Camera device settings"),
          label: qsTr("Camera trigger mode"),              subtitle: qsTr("Choose between free-run and hardware-synchronised capture"),
          actions: "Trigger free-run HW sync hardware synchronise",
          itemId: "" },

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Camera device settings"),
          label: qsTr("Fixed in place"),                   subtitle: qsTr("Mark a camera as wall-mounted"),
          actions: "Fixed in place wall mounted immovable",
          itemId: "" },

        { panelIndex: 3, panelLabel: qsTr("Cameras"),     groupLabel: qsTr("Camera crop & ROI"),
          label: qsTr("Camera crop region"),               subtitle: qsTr("Define a crop to reduce frame storage and ring buffer allocation"),
          actions: "Set crop Reset to full frame Default crop Full frame 16:9 ROI region origin size presets",
          itemId: "" },

        // ── IMUs (panelIndex: 4) ──────────────────────────────────────────────

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("Global IMU settings"),
          label: qsTr("Auto-connect on session start"),    subtitle: qsTr("Connects all enabled devices before recording begins"),
          actions: "enumerated devices hardware",
          itemId: "setting_imuAutoConnect" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("Global IMU settings"),
          label: qsTr("Auto-reconnect on signal loss"),    subtitle: qsTr("Attempts reconnect if the BLE link drops during a session"),
          itemId: "setting_imuAutoReconnect" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("Global IMU settings"),
          label: qsTr("Save calibration to device flash"), subtitle: qsTr("Persists zero-orientation and mag calibration across power cycles"),
          itemId: "setting_imuFlash" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("Global IMU settings"),
          label: qsTr("Use 9-axis fusion by default"),     subtitle: qsTr("Magnetometer-aided orientation"),
          itemId: "setting_imuFusion" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("IMU device settings"),
          label: qsTr("IMU body placement"),               subtitle: qsTr("Assign each IMU to a body segment"),
          actions: "Placement body thorax lumbar spine shoulder wrist hand segment A B C D",
          itemId: "" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("IMU device settings"),
          label: qsTr("IMU output rate"),                  subtitle: qsTr("Set the data rate for each IMU device in Hz"),
          actions: "Output rate Hz 50 100 200 500",
          itemId: "" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("IMU device settings"),
          label: qsTr("IMU fusion algorithm"),             subtitle: qsTr("Choose between 6-axis and 9-axis magnetometer-aided fusion"),
          actions: "Fusion 9-axis 6-axis magnetometer gyro algorithm",
          itemId: "" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("IMU device settings"),
          label: qsTr("IMU mount orientation"),            subtitle: qsTr("Specify whether the device is mounted horizontally or vertically"),
          actions: "Mount orientation horizontal vertical",
          itemId: "" },

        { panelIndex: 4, panelLabel: qsTr("IMUs"),        groupLabel: qsTr("IMU test panel"),
          label: qsTr("Test IMU connection"),              subtitle: qsTr("Verify orientation and live sensor data from a connected device"),
          actions: "Test Connect Disconnect Zero orientation Calibrate magnetometer Save to flash Scan Euler angles roll pitch yaw battery calibration",
          itemId: "" },

        // ── Storage (panelIndex: 6) ───────────────────────────────────────────

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Athlete library"),
          label: qsTr("Library location"),                 subtitle: qsTr("Root directory for all athlete profiles and session archives"),
          actions: "Change Reveal open folder",
          itemId: "setting_libraryPath" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Athlete library"),
          label: qsTr("Session folder naming"),            subtitle: qsTr("Pattern used when creating a new session directory"),
          itemId: "setting_sessionNaming" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Athlete library"),
          label: qsTr("Auto-save session on completion"),  subtitle: qsTr("Writes session data to the library immediately when recording ends"),
          itemId: "setting_autoSave" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Video recording"),
          label: qsTr("Recording resolution"),             subtitle: qsTr("Applies to all cameras — must be within sensor ROI bounds"),
          itemId: "setting_videoRes" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Video recording"),
          label: qsTr("Video codec"),                      subtitle: qsTr("Encoding applied when saving swing clips to disk"),
          itemId: "setting_videoCodec" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Video recording"),
          label: qsTr("Encoding quality"),                 subtitle: qsTr("Higher quality produces larger files"),
          itemId: "setting_videoQuality" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Video recording"),
          label: qsTr("Save raw camera frames"),           subtitle: qsTr("Stores unprocessed Bayer data alongside encoded clips"),
          itemId: "setting_saveRaw" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Video recording"),
          label: qsTr("Container format"),                 subtitle: qsTr("File format for saved swing clips"),
          itemId: "setting_container" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Sensor data"),
          label: qsTr("Save pose keypoints"),              subtitle: qsTr("MoveNet skeleton data stored as JSON"),
          itemId: "setting_savePose" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Sensor data"),
          label: qsTr("Save IMU streams"),                 subtitle: qsTr("Full quaternion and accelerometer data for all enabled IMUs"),
          itemId: "setting_saveImu" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Sensor data"),
          label: qsTr("IMU data format"),                  subtitle: qsTr("File format for saved IMU streams"),
          itemId: "setting_imuFormat" },

        { panelIndex: 6, panelLabel: qsTr("Storage"),     groupLabel: qsTr("Sensor data"),
          label: qsTr("Save launch monitor data"),         subtitle: qsTr("Ball-flight data from connected launch monitor"),
          itemId: "setting_saveLaunchMon" }

        // TODO: add entries when Launch Monitor panel is implemented (panelIndex: 5)
        // TODO: add entries when Archiving panel is implemented (panelIndex: 7)
    ]
}
