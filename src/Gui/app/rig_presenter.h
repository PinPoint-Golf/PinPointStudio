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
// R-QML-1, R-QML-2; docs/design/rig_design.md §4.1). Adapters ring, in src/Gui/app beside GuiApp, which hangs it as a
// QObject * (gui_app.h). Read-only for now: the machine's RigId, and the cameras and the sensors of the rig state as
// list models with declared roles. It forwards no command yet (C-5b, C-6b.1), and it follows the Rig's RigChanged.
// In the running app both lists stay empty until the cameras' and the sensors' keys are copied into the Rig (C-5b,
// C-6b.1): AppSettings is still their one writer.
//
// One of the three kinds of file that may include rig.h (lint A10): the composition root, this, and the Rig's store.

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>

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
public:
    // Made by the composition root after the Rig's load; the Rig outlives it.
    explicit RigPresenter(const Rig &rig, QObject *parent = nullptr);

    QString             rigId() const;
    QAbstractListModel *cameras() const { return m_cameras; }
    QAbstractListModel *sensors() const { return m_sensors; }

private:
    void refresh();

    const Rig          &m_rig;
    RigDeviceListModel *m_cameras;   // children of this object
    RigDeviceListModel *m_sensors;
};

} // namespace pinpoint
