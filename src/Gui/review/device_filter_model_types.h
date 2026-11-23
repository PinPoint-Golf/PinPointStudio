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

// THE DEVICE FILTER'S TYPED CHIP (refactor 5.8a, lint A6): one entry of DeviceFilterModel::availableDevices, named
// as the old map's keys. moc reaches this header through device_filter_model.cpp's #include of its moc file.

#include <QMetaType>
#include <QString>

namespace pinpoint::reviewui {

// A device a saved session used: its token, its label, and how many sessions turning it on would leave.
struct DeviceChip
{
    Q_GADGET
    Q_PROPERTY(QString id    MEMBER id    FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(int     count MEMBER count FINAL)
public:
    QString id;
    QString label;
    int     count = 0;
    friend bool operator==(const DeviceChip &, const DeviceChip &) = default;
};

} // namespace pinpoint::reviewui
