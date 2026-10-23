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

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// DeviceFilterModel — the "Recorded with" filter shared by every list of sessions: the carousel's
// session drawer (SessionListModel) and the golfer's sessions on Home and the golfer page
// (SessionReportListModel). One QML bar (PpDeviceFilterBar) drives any of them.
//
// deviceFilter narrows the saved sessions to those that used EVERY listed device ("HackMotion" and
// "DTL" = sessions with both); the live row is never filtered out — it is the way back to the session
// in progress, not a search result. The filter lives on the model, so it survives a refresh and the
// view going away, and is held for the life of the app only.
//
// A subclass says which device tokens each saved session used (savedDeviceSets) and re-applies the
// filter to its rows when it changes (applyDeviceFilter); it emits availableDevicesChanged and
// countsChanged when its rows change.
class DeviceFilterModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList  deviceFilter     READ deviceFilter     WRITE setDeviceFilter NOTIFY deviceFilterChanged)
    // [{ id, label, count }] — every device a saved session used (plus any token still in the
    // filter, so a chip can always be switched off), in sortDeviceTokens order; count = saved
    // sessions that pass the current filter AND used it — what turning that chip on would leave.
    Q_PROPERTY(QVariantList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(int          savedCount       READ savedCount       NOTIFY countsChanged)   // saved sessions in the library
    Q_PROPERTY(int          shownSavedCount  READ shownSavedCount  NOTIFY countsChanged)   // …that pass the filter

public:
    using QAbstractListModel::QAbstractListModel;

    QStringList  deviceFilter() const { return m_deviceFilter; }
    void         setDeviceFilter(const QStringList &tokens);
    QVariantList availableDevices() const;
    int          savedCount() const;
    int          shownSavedCount() const;

    Q_INVOKABLE void toggleDevice(const QString &token);
    Q_INVOKABLE void clearDeviceFilter() { setDeviceFilter({}); }

signals:
    void deviceFilterChanged();
    void availableDevicesChanged();
    void countsChanged();

protected:
    // Every saved (not live) session's device tokens — the library the chips count over.
    virtual QVector<QStringList> savedDeviceSets() const = 0;
    // The filter changed: re-apply it to the rows.
    virtual void applyDeviceFilter() = 0;

    // A saved session with these devices passes the filter (the caller lets the live row through).
    bool passesDeviceFilter(const QStringList &devices) const;

private:
    QStringList m_deviceFilter;
};
