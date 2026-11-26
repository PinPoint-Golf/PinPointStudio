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

#include "app_settings.h"

#include "../imu/imu_role_map.h"
#include "../../Vocabulary/Recording/perspective.h"   // the one perspective type: migrateImpactRoi reads Impact
#include "pp_debug.h"

#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

// ---------------------------------------------------------------------------
// The impact strip gets its own crop entry (camera_roi.h, 2026-10-08)
// ---------------------------------------------------------------------------

void AppSettings::migrateImpactRoi()
{
    QSettings s = ppSettings();
    if (s.value(QStringLiteral("camera/impactRoiMigrated"), false).toBool())
        return;
    bool changed = false;
    for (auto it = m_cameraPerspective.cbegin(); it != m_cameraPerspective.cend(); ++it) {
        const QString key = it.key();
        const QString strip = key + QStringLiteral("#impact");
        if (it.value().toInt() != int(pinpoint::recording::Perspective::Impact) || m_cameraRoi.contains(strip)
            || !m_cameraRoi.contains(key))
            continue;
        m_cameraRoi.insert(strip, m_cameraRoi.take(key));
        ppInfo() << "[AppSettings] impact crop for" << key << "moved to its own entry";
        changed = true;
    }
    if (changed)
        s.setValue(QStringLiteral("camera/roi"), m_cameraRoi);
    s.setValue(QStringLiteral("camera/impactRoiMigrated"), true);
}

// ---------------------------------------------------------------------------
// Role-keyed IMU placement (session_wizard_refactor_design.md §4.13)
// ---------------------------------------------------------------------------

void AppSettings::loadImuRoles()
{
    // ⚠ AppSettings IS CONSTRUCTED ALL OVER THE TREE (`AppSettings fallback;`), so this
    // runs many times per process. The marker makes every run after the first a plain read;
    // the migration itself writes only imu/roles and the marker, never imu/placement.
    const bool migrated = ppSettings().value(QStringLiteral("imu/rolesMigrated"), false).toBool();
    const pinpoint::imu_roles::RolesLoad load = pinpoint::imu_roles::loadRoles(
        migrated,
        ppSettings().value(QStringLiteral("imu/roles"),     QVariantMap{}).toMap(),
        migrated ? QVariantMap{}
                 : ppSettings().value(QStringLiteral("imu/placement"), QVariantMap{}).toMap());
    m_imuRoles = load.roles;
    if (load.migrated) {
        ppSettings().setValue(QStringLiteral("imu/roles"), m_imuRoles);
        ppSettings().setValue(QStringLiteral("imu/rolesMigrated"), true);
        for (const QString &line : load.log)
            ppInfo() << "[AppSettings] imu/placement → imu/roles:" << line;
    }
    m_imuPlacement = pinpoint::imu_roles::placementFromRoles(m_imuRoles);
}

void AppSettings::setImuRoles(const QVariantMap &v)
{
    if (m_imuRoles == v) return;
    m_imuRoles = v;
    ppSettings().setValue(QStringLiteral("imu/roles"), v);
    // A raw write before the first load (none today) must not be overwritten by a later
    // migration, so the marker is set here as well.
    ppSettings().setValue(QStringLiteral("imu/rolesMigrated"), true);
    m_imuPlacement = pinpoint::imu_roles::placementFromRoles(m_imuRoles);
    emit imuRolesChanged();
    // ⚠ UNCONDITIONALLY, even when the derived letters did not move (a trunk role
    // changed): the two signals are one event, so a reader of the legacy view never has to
    // know which half of the map moved.
    emit imuPlacementChanged();
}

StorageInfo AppSettings::queryStorageInfo() const
{
    StorageInfo info;
    const QString path = athleteLibraryPath();
    if (path.isEmpty()) return info;

    QStorageInfo si(path);
    if (si.isValid()) {
        info.totalBytes = si.bytesTotal();
        info.freeBytes  = si.bytesAvailable();
        info.volumeName = si.displayName();
    }

    // Whatever the last completed scan found. -1 until one has. NOT recomputed here — see the
    // header: this function is called from a QML binding at component completion, and the walk it
    // used to do turned every launch into a five-to-ten-second black window on a network library.
    info.sessionBytes = m_sessionBytes;

    return info;
}

void AppSettings::refreshSessionBytes()
{
    if (m_sessionBytesScanning)
        return;                          // already measuring; the answer would be the same
    const QString path = athleteLibraryPath();
    if (path.isEmpty())
        return;
    if (!m_librarySizer) {
        ppWarn() << "[AppSettings] no library sizer (housekeeping): the library was not measured";
        return;
    }

    m_sessionBytesScanning = true;
    emit sessionBytesChanged();

    auto abort = std::make_shared<std::atomic_bool>(false);
    m_sessionBytesAbort = abort;

    // Disconnect-then-connect: the watcher is reused across scans (library path changes, a manual
    // refresh), and a stale connection would deliver an old finish into the new one.
    m_sessionBytesWatcher.disconnect();
    connect(&m_sessionBytesWatcher, &QFutureWatcher<qint64>::finished, this, [this] {
        // A cancelled scan reports nothing rather than a partial sum: half a library measured is
        // not a smaller library, and showing it as one would be a wrong number where the honest
        // answer is the previous one.
        const qint64 v = m_sessionBytesWatcher.result();
        m_sessionBytesScanning = false;
        if (v >= 0)
            m_sessionBytes = v;
        emit sessionBytesChanged();
    });

    // Housekeeping's measure (DEC-188), copied into the job so the job touches no member: the walk itself is the
    // repositories' (SwingPaths::libraryBytes, through the composition root's door), every file under the library, the abort polled as it goes.
    m_sessionBytesWatcher.setFuture(QtConcurrent::run([path, abort, sizer = m_librarySizer]() -> qint64 {
        return sizer(path, abort.get());
    }));
}

AppSettings::~AppSettings()
{
    if (m_sessionBytesAbort)
        m_sessionBytesAbort->store(true);
    // The lambda captures `path`, `abort` and the sizer BY VALUE and touches no member, so it is safe on its
    // own — but the watcher is a member and would be destroyed under a running future. Waiting is
    // bounded by one poll interval because of the abort above, not by the length of the walk.
    m_sessionBytesWatcher.disconnect();
    if (m_sessionBytesWatcher.isRunning())
        m_sessionBytesWatcher.waitForFinished();
}
