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

#pragma once

// CameraRoleProbe — role changes tested in the running app, on the real
// cameras, with no one at the keyboard (flir_camera_settings.md §7.2).
//
// PINPOINT_PROBE_CAMERA_ROLES=<report file> [PINPOINT_PROBE_CAMERA_SERIAL=<serial>]
//
// For each impact-capable camera (or the one named): connect it and capture,
// then move it original role → Impact → DownTheLine → Impact → original role
// through exactly the calls Settings → Cameras makes (the VIEW combo's impact
// seeding, then CameraManager::assignPerspective, which reconnects a camera
// that moves into or out of Impact). After each move it waits for the camera
// to settle and reads back what the DEVICE holds (CameraInstance::
// readBackSettings) and what it delivers (cameraFps, frame size), and checks
// it against the role. The operator's camera settings, connection and capture
// state are snapshotted first and restored after. Writes a text report, logs
// one line per check, and quits the app.

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <vector>

class AppSettings;
class CameraInstance;
class CameraManager;

class CameraRoleProbe : public QObject
{
    Q_OBJECT
public:
    CameraRoleProbe(CameraManager *cameras, AppSettings *settings, QString reportPath,
                    QString serial, QObject *parent = nullptr);
    void start();

private:
    struct Observation {
        QString     step;
        int         role = -1;
        int         roleSeen = -1;
        QPointer<CameraInstance> instance;   // null once a reconnect has destroyed it
        bool        reconnected = false;   // the previous step's instance was gone by now
        double      cameraFps = 0.0;
        int         width = 0, height = 0;
        int         expectW = 0, expectH = 0;   // the role's crop as stored when observed
        QVariantMap nodes;
    };
    struct Target {
        int         index = -1;
        QString     key, serial, description;
        double      maxW = 0, maxH = 0, gainMaxDb = -1;
        QVariantList impactModes;
        int         originalRole = 0;
        bool        wasSelected = false;
    };

    void then(int delayMs, std::function<void()> step);
    void runNext();
    void findCameras(int attempt);
    void planCamera(const Target &t);
    void seedImpactLikeTheViewCombo(const Target &t);
    void observe(const Target &t, const QString &step, int role);
    // Frame size this role's stored crop gives (camera_roi.h), full sensor if none.
    static void expectedSize(const Target &t, const QVariantMap &roiMap, int role, int *w, int *h);
    void evaluate(const Target &t);
    void check(bool ok, const QString &what);
    void finish();
    CameraInstance *instanceFor(const QString &key) const;
    QVariantMap entryFor(const QString &key) const;

    QPointer<CameraManager> m_cameras;
    AppSettings            *m_settings = nullptr;
    QString                 m_reportPath;
    QString                 m_serial;
    std::vector<std::pair<int, std::function<void()>>> m_queue;
    std::vector<Observation> m_obs;   // the current camera's
    QStringList             m_report;
    int                     m_failures = 0;
    bool                    m_wasRecording = false;
    QList<int>              m_deselect;   // cameras the probe connected, disconnected at the end
    // The operator's camera settings, restored at the end.
    QVariantMap m_perspective, m_targetFps, m_exposureUs, m_roi, m_tuning;
};
