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

#include "device_filter_model.h"

#include <QVariantMap>

#include "session_summary.h"

bool DeviceFilterModel::passesDeviceFilter(const QStringList &devices) const
{
    for (const QString &d : m_deviceFilter)
        if (!devices.contains(d))
            return false;
    return true;
}

void DeviceFilterModel::setDeviceFilter(const QStringList &tokens)
{
    QStringList t = tokens;
    t.removeDuplicates();
    pinpoint::sortDeviceTokens(t);
    if (t == m_deviceFilter)
        return;
    m_deviceFilter = t;
    applyDeviceFilter();
    emit deviceFilterChanged();
    emit availableDevicesChanged();         // a token kept only because it was selected may go
    emit countsChanged();
}

void DeviceFilterModel::toggleDevice(const QString &token)
{
    QStringList t = m_deviceFilter;
    if (!t.removeAll(token))
        t.append(token);
    setDeviceFilter(t);
}

QVariantList DeviceFilterModel::availableDevices() const
{
    const QVector<QStringList> saved = savedDeviceSets();
    QStringList tokens = m_deviceFilter;
    for (const QStringList &devices : saved)
        for (const QString &d : devices)
            if (!tokens.contains(d))
                tokens.append(d);
    pinpoint::sortDeviceTokens(tokens);

    QVariantList out;
    for (const QString &d : std::as_const(tokens)) {
        // Counted within the CURRENT filter, so each chip says how many sessions adding it would
        // leave (AND) — a fixed library count read as if chips widened the list. For a chip that is
        // already on, this is simply the number shown.
        int n = 0;
        for (const QStringList &devices : saved)
            if (passesDeviceFilter(devices) && devices.contains(d))
                ++n;
        out.append(QVariantMap{ { QStringLiteral("id"),    d },
                                { QStringLiteral("label"), pinpoint::deviceLabel(d) },
                                { QStringLiteral("count"), n } });
    }
    return out;
}

int DeviceFilterModel::savedCount() const
{
    return int(savedDeviceSets().size());
}

int DeviceFilterModel::shownSavedCount() const
{
    int n = 0;
    for (const QStringList &devices : savedDeviceSets())
        if (passesDeviceFilter(devices))
            ++n;
    return n;
}
