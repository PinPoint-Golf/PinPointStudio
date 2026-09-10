/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "launch_monitor_base.h"
#include "shot_pairing.h"
#include "../../Export/swing_paths.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

class AppSettings;
class AthleteController;
class CameraManager;
class SessionController;
class ShotListModel;

// The launch monitor's place in the app (QML context property `launchMonitor`).
//
// Owns the connector, applies the attribution rule, and writes a claimed reading into
// the shot's swing.json. The ImuManager analogue, and much smaller than one, because a
// file-polling source has nothing to enumerate, select or register with the buffer.
//
// Three things wire into it from main.cpp:
//   ShotController::shotDetected   → arm for this swing
//   ShotProcessor::shotProcessed   → a swingDir exists; flush anything parked
//   ShotProcessor::shotFailed      → drop anything parked for a swing that died
//
// State (the enum → string mapping) is translated here and nowhere else, so the QML
// contract cannot drift — the same arrangement UpdateController has.
class LaunchMonitorController : public QObject
{
    Q_OBJECT

    // Whether a connector is configured at all. The toolbar dot and the chime are both
    // gated on this: on a machine with no monitor, neither should ever appear.
    Q_PROPERTY(bool    configured   READ configured   NOTIFY stateChanged)
    Q_PROPERTY(QString state        READ stateName    NOTIFY stateChanged)
    Q_PROPERTY(QString stateLabel   READ stateLabel   NOTIFY stateChanged)
    Q_PROPERTY(QString errorText    READ errorText    NOTIFY stateChanged)
    // Where readings are being read from, for the settings panel to show back.
    Q_PROPERTY(QString sourceText   READ sourceText   NOTIFY stateChanged)
    // The short device name ("GC Quad") for panels that name the device in passing.
    // Empty when nothing is configured, so a caller can join it into a list unguarded.
    Q_PROPERTY(QString deviceName   READ deviceName   NOTIFY stateChanged)
    // A one-line summary of the last reading applied ("283 · Irn · 87.2 mph"), so the
    // settings panel can prove the connection works without opening a shot.
    Q_PROPERTY(QString lastReading  READ lastReading  NOTIFY lastReadingChanged)
    // ── The devices actually on the link ────────────────────────────────────
    // ⚠ A CONFIGURED CONNECTOR AND A CONNECTED DEVICE ARE DIFFERENT FACTS, and
    // only the GSPro link can tell them apart: a folder is configured and that is
    // the end of it, but a launch monitor CONNECTS, says who it is, and can go
    // away again. So this is a list rather than a name, it is empty for the GCQuad
    // path, and the panel shows it beside the cameras and the IMUs.
    //
    // Each entry: { deviceId, peer, shots, messages, identified, connectedAtMs,
    // lastMessageAtMs, label }. `label` is what to show — the DeviceID once the
    // device has spoken, "connecting…" before that.
    Q_PROPERTY(QVariantList devices     READ devices     NOTIFY devicesChanged)
    Q_PROPERTY(int          deviceCount READ deviceCount NOTIFY devicesChanged)
    // True when this connector enumerates at all, so a panel can show the list for
    // the GSPro link and the configured-folder line for the GCQuad without asking
    // which kind it is holding.
    Q_PROPERTY(bool         enumerates  READ enumerates  NOTIFY stateChanged)

public:
    // athleteController and sessionController are needed only by the standalone path —
    // a device-only shot still has to belong to somebody and land in a session folder.
    // Both may be null, in which case standalone shots are simply never created.
    LaunchMonitorController(AppSettings *settings, ShotListModel *shotModel,
                            AthleteController *athleteController,
                            SessionController *sessionController,
                            CameraManager *cameraManager,
                            QObject *parent = nullptr);
    ~LaunchMonitorController() override;

    bool    configured() const;
    QString stateName()  const;
    QString stateLabel() const;
    QString errorText()  const;
    QString sourceText() const;
    QString deviceName() const;
    QString lastReading() const { return m_lastReading; }
    QVariantList devices() const;
    // Split out only because stateLabel() would otherwise nest two questions.
    QString      waitingLabel() const;
    int          deviceCount() const { return devices().size(); }
    bool         enumerates() const;

    // ── PinPoint → the device ───────────────────────────────────────────────
    // Club selection and session state travel OUT on a link that carries them:
    // GSPro tells a launch monitor which club is in play so it can switch to
    // putting mode, and an OSP-style client does not arm until it has seen a
    // session start. Both are no-ops on a connector that cannot send (the GCQuad
    // reads a file; there is nothing to tell it), so callers need not ask which
    // connector is configured.
    //
    // `club` is a GSPro club code — "PT", "DR", "I7". Anything else clears it.
    Q_INVOKABLE void setPlayerClub(const QString &club, bool leftHanded);
    Q_INVOKABLE void setSessionActive(bool active);

public slots:
    // ShotController::shotDetected. The extra arguments are ignored — which swing a
    // reading belongs to is decided by ORDER, not by timestamp, since the file gives
    // us no timestamp we could trust anyway.
    void onShotDetected();
    void onShotProcessed(int shotId, const QString &swingDir);
    // The shot produced no document. `swingDir` is the folder the processor allocated
    // before it gave up — empty only when even that failed. A reading for this shot is
    // still worth keeping: a ball WAS struck and the monitor measured it, so the folder
    // becomes a device-only swing rather than being left empty.
    void onShotFailed(const QString &swingDir, int shotId);

    // Rebuild the connector from settings. Called on construction and whenever the
    // configured kind or path changes.
    void reconfigure();

signals:
    void stateChanged();
    void lastReadingChanged();
    void devicesChanged();
    // A reading has been WRITTEN to a shot. The toolbar dot and the chime hang off
    // this rather than off the file changing: the light means "brought in", not
    // "noticed", and a reading we could not attribute must not flash anything.
    void readingApplied(const QString &swingDir);
    // A shot the capture pipeline could not record was saved from the monitor's reading
    // alone. Distinct from readingApplied so the UI can say which happened — "analysis
    // failed" is the wrong thing to tell somebody whose shot was saved.
    void deviceOnlyShotSaved(const QString &swingDir);

private:
    void onReadingAvailable(const pinpoint::lm::LaunchMonitorReading &reading);
    // Create a whole shot from the reading, because nothing else saw it. Returns false
    // — quietly, it is not an error — whenever the preconditions are not met.
    bool createStandaloneShot(const pinpoint::lm::LaunchMonitorReading &reading);
    // Write a device-only document into an ALREADY ALLOCATED folder and put it on the
    // carousel. Shared by the rescue path and the standalone path.
    // `existingShotId` >= 0 points an EXISTING carousel row at the folder instead of
    // adding one — the rescue path, where the processor already made a row for a shot
    // that produced nothing. -1 adds a row, which is the standalone path.
    bool writeDeviceOnly(const QString &swingDir, const pinpoint::lm::LaunchMonitorReading &r,
                         const QString &swingId, int swingIndex, const QString &sessionId,
                         int existingShotId);
    // Write to swing.json and refresh the carousel row. Returns false (and logs) when
    // the document cannot be updated.
    bool applyToSwing(const QString &swingDir, const pinpoint::lm::LaunchMonitorReading &r);

    AppSettings                   *m_settings  = nullptr;
    ShotListModel                 *m_shotModel = nullptr;
    AthleteController             *m_athletes  = nullptr;
    SessionController             *m_session   = nullptr;
    // Read for ONE thing: captureIntent(), the toolbar Capture/Stop state. See
    // createStandaloneShot — a shot is only ours to record while the user says they are
    // hitting, and no buffer state can answer that when there are no devices to register.
    CameraManager                 *m_cameras   = nullptr;
    // Its own allocator. NOT shared with ShotProcessor's: that one caches the session
    // folder for a capture session, and a standalone shot must not be able to disturb
    // the folder a live capture is writing into.
    pinpoint::SwingPaths           m_paths;
    pinpoint::lm::LaunchMonitorBase *m_monitor = nullptr;   // never null — see makeLaunchMonitor
    pinpoint::lm::ShotPairing      m_pairing;
    // Whether the destination the pairing knows about actually holds a document. False
    // when the processor allocated a folder and then wrote nothing into it, which is the
    // case a launch monitor can rescue.
    bool                           m_destinationHasDoc = true;
    // The row the processor already created for a shot that produced no document.
    int                            m_destinationShotId = -1;
    QString                        m_lastReading;
    // Remembered so a connector rebuilt from a settings change is told again
    // without waiting for the next club selection.
    QString                        m_club;
    bool                           m_leftHanded    = false;
    bool                           m_sessionActive = false;
};
