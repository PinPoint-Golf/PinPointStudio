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

#include "rig_presenter.h"

#include "../../Core/pp_debug.h"
#include "../../DomainModel/rig.h"
#include "../../IMU/imu_role_map.h"   // the rig state as today's roles map and the derived placement view

namespace pinpoint {

namespace {

// A camera key as QML hands it; a null key (with a warning) when it is not one.
rig::DeviceKey cameraFrom(const QString &cameraKey, const char *command)
{
    const rig::DeviceKey k = rig::DeviceKey::fromPersistedCameraKey(cameraKey);
    if (!k.isCamera()) ppWarn() << "[app.rig]" << command << ": not a camera key, nothing changed:" << cameraKey;
    return k;
}

const rig::CameraSetup *cameraEntry(const Rig &r, const QString &cameraKey)
{
    for (const rig::CameraSetup &c : r.parts().state.cameras)
        if (c.key().persistedCameraKey() == cameraKey) return &c;
    return nullptr;
}

} // namespace

void RigDeviceListModel::setKeys(const QStringList &keys)
{
    if (keys == m_keys) return;
    beginResetModel();
    m_keys = keys;
    endResetModel();
}

int RigDeviceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_keys.size());
}

QVariant RigDeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_keys.size() || role != KeyRole) return {};
    return m_keys.at(index.row());
}

QHash<int, QByteArray> RigDeviceListModel::roleNames() const
{
    return { { KeyRole, QByteArrayLiteral("key") } };
}

RigPresenter::RigPresenter(Rig &rig, QObject *parent)
    : QObject(parent), m_rig(rig), m_cameras(new RigDeviceListModel(this)), m_sensors(new RigDeviceListModel(this))
{
    refresh();
    m_sensorRoles = imu_roles::rolesMapOf(m_rig.parts().state);
    connect(&m_rig, &Rig::rigChanged, this, [this] {
        refresh();
        emit camerasChanged();
        const QVariantMap roles = imu_roles::rolesMapOf(m_rig.parts().state);
        if (roles != m_sensorRoles) {
            m_sensorRoles = roles;
            emit sensorRolesChanged();
        }
    });
    connect(&m_rig, &Rig::rigDeviceLabelChanged, this, [this](const rig::DeviceKey &device, RigDeviceLabel which) {
        if (device.isCamera()) emit camerasChanged();
        if (!device.isSensor()) return;
        if (which == RigDeviceLabel::Alias) emit sensorAliasesChanged();
        else emit sensorsDisabledChanged();
    });
}

QString RigPresenter::rigId() const { return m_rig.id().toString(); }

void RigPresenter::refresh()
{
    QStringList cameras, sensors;
    for (const auto &c : m_rig.parts().state.cameras) cameras << c.key().persistedCameraKey();
    for (const auto &s : m_rig.parts().state.sensors) sensors << s.key.persisted();
    m_cameras->setKeys(cameras);
    m_sensors->setKeys(sensors);
}

QStringList RigPresenter::camerasMirrored() const
{
    QStringList out;
    for (const auto &c : m_rig.parts().state.cameras)
        if (c.mirrored) out << c.key().persistedCameraKey();
    return out;
}

QStringList RigPresenter::camerasFixedInPlace() const
{
    QStringList out;
    for (const auto &c : m_rig.parts().state.cameras)
        if (c.fixedInPlace) out << c.key().persistedCameraKey();
    return out;
}

QStringList RigPresenter::camerasDisabled() const
{
    QStringList out;
    for (const auto &e : m_rig.parts().other.enabledOnThisMachine)
        if (e.first.isCamera() && !e.second) out << e.first.persistedCameraKey();
    return out;
}

int RigPresenter::perspectiveOf(const QString &cameraKey) const
{
    const rig::CameraSetup *c = cameraEntry(m_rig, cameraKey);
    return c ? c->perspectiveCode() : 0;
}

QRectF RigPresenter::hittingAreaOf(const QString &cameraKey) const
{
    const rig::CameraSetup *c = cameraEntry(m_rig, cameraKey);
    return c && c->hittingArea ? *c->hittingArea : QRectF();
}

QString RigPresenter::aliasOf(const QString &cameraKey) const
{
    for (const auto &a : m_rig.parts().other.aliases)
        if (a.first.isCamera() && a.first.persistedCameraKey() == cameraKey) return a.second;
    return {};
}

void RigPresenter::assignPerspective(const QString &cameraKey, int perspective)
{
    if (const auto k = cameraFrom(cameraKey, "assignPerspective"); k.isCamera()) m_rig.assignPerspective(k, perspective);
}

void RigPresenter::setMirrored(const QString &cameraKey, bool mirrored)
{
    if (const auto k = cameraFrom(cameraKey, "setMirrored"); k.isCamera()) m_rig.setMirrored(k, mirrored);
}

void RigPresenter::setFixedInPlace(const QString &cameraKey, bool fixedInPlace)
{
    if (const auto k = cameraFrom(cameraKey, "setFixedInPlace"); k.isCamera()) m_rig.setFixedInPlace(k, fixedInPlace);
}

void RigPresenter::setHittingArea(const QString &cameraKey, const QRectF &box)
{
    if (const auto k = cameraFrom(cameraKey, "setHittingArea"); k.isCamera()) m_rig.setHittingArea(k, box);
}

void RigPresenter::clearHittingArea(const QString &cameraKey)
{
    if (const auto k = cameraFrom(cameraKey, "clearHittingArea"); k.isCamera()) m_rig.clearHittingArea(k);
}

void RigPresenter::setAlias(const QString &cameraKey, const QString &alias)
{
    if (const auto k = cameraFrom(cameraKey, "setAlias"); k.isCamera()) m_rig.setAlias(k, alias);
}

void RigPresenter::setEnabledOnThisMachine(const QString &cameraKey, bool enabled)
{
    if (const auto k = cameraFrom(cameraKey, "setEnabledOnThisMachine"); k.isCamera()) m_rig.setEnabledOnThisMachine(k, enabled);
}

// ── The sensors (C-6b.1) ──

QStringList RigPresenter::sensorRoles() const
{
    QStringList out;
    const QVariantMap roles = imu_roles::rolesMapOf(m_rig.parts().state);
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) out << it.value().toString();
    return out;
}

QStringList RigPresenter::placementPairs() const
{
    QStringList out;
    const QVariantMap view = imu_roles::placementViewOf(m_rig.parts().state);
    for (auto it = view.cbegin(); it != view.cend(); ++it) out << it.key() << it.value().toString();
    return out;
}

QStringList RigPresenter::sensorsDisabled() const
{
    QStringList out;
    for (const auto &e : m_rig.parts().other.enabledOnThisMachine)
        if (e.first.isSensor() && !e.second && !out.contains(e.first.persistedId())) out << e.first.persistedId();
    return out;
}

QStringList RigPresenter::sensorAliasKeys() const
{
    QStringList out;
    for (const auto &a : m_rig.parts().other.aliases)
        if (a.first.isSensor()) out << a.first.persistedWithDescription();
    return out;
}

QString RigPresenter::sensorAliasOf(const QString &aliasKey) const
{
    for (const auto &a : m_rig.parts().other.aliases)
        if (a.first.isSensor() && a.first.persistedWithDescription() == aliasKey) return a.second;
    return {};
}

void RigPresenter::setSensorEnabledOnThisMachine(const QString &deviceId, bool enabled)
{
    const rig::DeviceKey k = rig::DeviceKey::fromPersistedId(deviceId);
    if (k.isNull()) {
        ppWarn() << "[app.rig] setSensorEnabledOnThisMachine: no sensor id, nothing changed";
        return;
    }
    m_rig.setEnabledOnThisMachine(k, enabled);
}

} // namespace pinpoint
