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

#include "imu_base.h"
#include <QList>

// Factory for creating IMU device instances.
//
// Usage:
//   ImuBase *imu = ImuFactory::create(ImuBase::Transport::Ble, this);
//   connect(imu, &ImuBase::quaternionUpdated, ...);

class ImuFactory
{
public:
    static ImuBase *create(ImuBase::Transport transport, QObject *parent = nullptr);
    static QList<ImuBase::Transport> available();
};
