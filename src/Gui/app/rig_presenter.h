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

// RigPresenter: `app.rig`, the Rig as QML reaches it (information_model.md §2.3 OBJ-RIG "How a view reaches it", §9.2
// R-QML-1, R-QML-2; docs/design/rig_design.md §4.1, §4.3, §4.9). Adapters ring, in src/Gui/app beside GuiApp, which hangs
// it as a QObject * (gui_app.h). It shows the machine's RigId and the cameras and the sensors of the rig state as list
// models with declared roles, and since the cameras' cut-over (platform refactor C-5b) each camera's set-up and forwards
// the Rig's camera commands, and since the sensors' (C-6b.1) the sensors' roles, aliases and enabled flags and the
// derived placement view: the settings' camera and sensor keys are no longer what a view reads or writes (SET-09).
//
//   READ, by the camera key every camera row carries (CameraManager's cameraList `cameraKey`):
//     camerasMirrored, camerasFixedInPlace, camerasDisabled   lists of camera keys (a binding re-reads them on
//                                                               camerasChanged)
//     perspectiveOf(key) · hittingAreaOf(key) · aliasOf(key) one camera's perspective code (0 = none), its
//                                                               hitting-area box (empty = none) and its alias
//   WRITE, each one Rig command (rig_commands.h): assignPerspective, setMirrored, setFixedInPlace, setHittingArea,
//     clearHittingArea, setAlias, setEnabledOnThisMachine. A key that is not a camera key changes nothing (ppWarn).
//   camerasChanged follows the Rig's RigChanged and a camera's RigDeviceLabelChanged.
//
// The views that also act on a live camera (the VIEW combo, the Mirrored chip with a camera connected, the hitting-area
// editor, the alias field, the Enable switch) go through the camera manager, which asks the Rig the same way
// (camera_rig_link.h).
//
//   THE SENSORS (C-6b.1; today's imu/roles, imu/alias and imu/excluded, and the derived imuPlacement):
//     sensorRoles       the role's settings name held by each placement, in placement-key order (what
//                       Object.values(appSettings.imuRoles) read); NOTIFY sensorRolesChanged, which fires when the roles
//                       change and never for a camera's change. The views that resolve a role through imuManager read it
//                       as their reactive dependency: imuManager has taken the new roles before it fires (the composition
//                       root connects the manager first).
//     placementPairs    the derived placement view (the arm roles as the legacy letters A, B, C; trunk roles left out) as
//                       flat [key, letter, key, letter, …] pairs in key order, for the one reader that labels an old
//                       swing's stream (review's SwingDataSource.imuPlacementPairs); NOTIFY sensorRolesChanged
//     sensorsDisabled   the bare ids of the sensors not enabled on this machine; NOTIFY sensorsDisabledChanged
//     sensorAliasKeys · sensorAliasOf(aliasKey)
//                       the alias map's spellings ("<description>|<deviceId>") that hold an alias, and one's alias;
//                       NOTIFY sensorAliasesChanged
//     setSensorEnabledOnThisMachine(deviceId, enabled)   the Enable switch: one Rig command (the manager switches the
//                       sensor off for the session, one turn later, when it hears the change)
//   A sensor's mount is set through imuManager.setRoleForDevice (it needs the live facts) and its alias through
//   imuManager.setImuAlias, each of which asks the Rig (imu_rig_link.h). Lists and strings only: lint A6 keeps a
//   QVariantMap off this boundary.
//
// One of the three kinds of file that may include rig.h (lint A10): the composition root, this, and the Rig's store.

#include <QAbstractListModel>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace pinpoint {

class Rig;

// One device per row, by its persisted key (a camera's camera key; a sensor placement's key).
class RigDeviceListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1 };

    explicit RigDeviceListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    void setKeys(const QStringList &keys);

    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QStringList m_keys;
};

class RigPresenter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rigId READ rigId CONSTANT)
    Q_PROPERTY(QAbstractListModel *cameras READ cameras CONSTANT)
    Q_PROPERTY(QAbstractListModel *sensors READ sensors CONSTANT)
    Q_PROPERTY(QStringList camerasMirrored     READ camerasMirrored     NOTIFY camerasChanged)
    Q_PROPERTY(QStringList camerasFixedInPlace READ camerasFixedInPlace NOTIFY camerasChanged)
    Q_PROPERTY(QStringList camerasDisabled     READ camerasDisabled     NOTIFY camerasChanged)
    Q_PROPERTY(QStringList sensorRoles         READ sensorRoles         NOTIFY sensorRolesChanged)
    Q_PROPERTY(QStringList placementPairs      READ placementPairs      NOTIFY sensorRolesChanged)
    Q_PROPERTY(QStringList sensorsDisabled     READ sensorsDisabled     NOTIFY sensorsDisabledChanged)
    Q_PROPERTY(QStringList sensorAliasKeys     READ sensorAliasKeys     NOTIFY sensorAliasesChanged)
public:
    // Made by the composition root after the Rig's load; the Rig outlives it.
    explicit RigPresenter(Rig &rig, QObject *parent = nullptr);

    QString             rigId() const;
    QAbstractListModel *cameras() const { return m_cameras; }
    QAbstractListModel *sensors() const { return m_sensors; }

    QStringList camerasMirrored() const;
    QStringList camerasFixedInPlace() const;
    QStringList camerasDisabled() const;   // not enabled on this machine

    Q_INVOKABLE int     perspectiveOf(const QString &cameraKey) const;
    Q_INVOKABLE QRectF  hittingAreaOf(const QString &cameraKey) const;
    Q_INVOKABLE QString aliasOf(const QString &cameraKey) const;

    Q_INVOKABLE void assignPerspective(const QString &cameraKey, int perspective);
    Q_INVOKABLE void setMirrored(const QString &cameraKey, bool mirrored);
    Q_INVOKABLE void setFixedInPlace(const QString &cameraKey, bool fixedInPlace);
    Q_INVOKABLE void setHittingArea(const QString &cameraKey, const QRectF &box);
    Q_INVOKABLE void clearHittingArea(const QString &cameraKey);
    Q_INVOKABLE void setAlias(const QString &cameraKey, const QString &alias);
    Q_INVOKABLE void setEnabledOnThisMachine(const QString &cameraKey, bool enabled);

    // ── The sensors (C-6b.1) ──
    QStringList sensorRoles() const;
    QStringList placementPairs() const;
    QStringList sensorsDisabled() const;
    QStringList sensorAliasKeys() const;
    Q_INVOKABLE QString sensorAliasOf(const QString &aliasKey) const;
    Q_INVOKABLE void    setSensorEnabledOnThisMachine(const QString &deviceId, bool enabled);

signals:
    void camerasChanged();
    void sensorRolesChanged();
    void sensorsDisabledChanged();
    void sensorAliasesChanged();

private:
    void refresh();

    Rig                &m_rig;
    QVariantMap         m_sensorRoles;   // the roles as last notified, so a camera's change notifies no sensor reader
    RigDeviceListModel *m_cameras;   // children of this object
    RigDeviceListModel *m_sensors;
};

} // namespace pinpoint
