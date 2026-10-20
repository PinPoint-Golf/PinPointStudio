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
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// SessionListModel — the rows in the carousel's "CHOOSE A SESSION" drawer.
// Owned and populated by SessionReviewController (which aggregates each session's
// metadata via session_summary.h). The live session is pinned first, flagged
// isLive. Rebuilt wholesale on refresh() — rows are immutable summaries, so a
// full reset is simpler than incremental edits and cheap at this scale.
//
// DEVICE FILTER. Each row carries the devices its shots were recorded with (session_summary.h
// device tokens). deviceFilter narrows the saved sessions to those that used EVERY listed device
// ("HackMotion" and "DTL" = sessions with both); the live row is never filtered out — it is the
// way back to the session in progress, not a search result. The filter lives on the model, so it
// survives refresh() and the drawer closing, and is held for the life of the app only.
class SessionListModel : public QAbstractListModel
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
    enum Roles {
        SessionIdRole = Qt::UserRole + 1,   // absolute session dir path; empty for the live row
        DayLabelRole,
        TimeLabelRole,
        ClubMixRole,
        ShotCountRole,
        LengthLabelRole,
        AvgQualityRole,
        IsLiveRole,
        PreviewThumbsRole,                  // QStringList of thumbnail URLs (may be empty)
        IndexedRole,                        // false → summary not built yet; detail fields are blank
        DevicesRole,                        // QStringList of device tokens, display order
        DeviceLabelsRole,                   // QStringList of their short names, same order
    };

    struct Row {
        QString     sessionId;
        QString     dayLabel;
        QString     timeLabel;
        QString     clubMix;
        int         shotCount  = 0;
        QString     lengthLabel;
        int         avgQuality = 0;
        bool        isLive     = false;
        QStringList previewThumbs;
        // A session is "indexed" once every swing has a summary sidecar. Un-indexed rows
        // carry only the cheap facts (shot count, day/time from the folder) — building the
        // rest would mean parsing tens of MB of swing.json on the GUI thread, which is
        // exactly the stall this avoids. Opening the session fills it in.
        bool        indexed    = true;
        QStringList devices;                // device tokens (session_summary.h), display order
    };

    explicit SessionListModel(QObject *parent = nullptr);

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QVector<Row> &rows);

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

private:
    bool passes(const Row &r) const;
    void rebuildShown();                    // m_rows = the rows of m_all that pass, in order

    QVector<Row> m_all;                     // every row setRows() was given
    QVector<Row> m_rows;                    // what the view shows
    QStringList  m_deviceFilter;
};
