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

#include "../../DomainModel/rig.h"

namespace pinpoint {

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

RigPresenter::RigPresenter(const Rig &rig, QObject *parent)
    : QObject(parent), m_rig(rig), m_cameras(new RigDeviceListModel(this)), m_sensors(new RigDeviceListModel(this))
{
    refresh();
    connect(&m_rig, &Rig::rigChanged, this, &RigPresenter::refresh);
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

} // namespace pinpoint
