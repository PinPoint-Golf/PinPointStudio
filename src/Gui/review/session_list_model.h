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

#include <QString>
#include <QStringList>
#include <QVector>

#include "device_filter_model.h"

// SessionListModel — the rows in the carousel's "CHOOSE A SESSION" drawer.
// Owned and populated by SessionReviewController (which aggregates each session's
// metadata via session_summary.h). The live session is pinned first, flagged
// isLive. Rebuilt wholesale on refresh() — rows are immutable summaries, so a
// full reset is simpler than incremental edits and cheap at this scale.
//
// DEVICE FILTER. Each row carries the devices its shots were recorded with (session_summary.h
// device tokens); DeviceFilterModel narrows the rows shown to the saved sessions that used every
// chosen device, the live row always kept.
class SessionListModel : public DeviceFilterModel
{
    Q_OBJECT

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

protected:
    QVector<QStringList> savedDeviceSets() const override;
    void applyDeviceFilter() override { rebuildShown(); }

private:
    bool passes(const Row &r) const;
    void rebuildShown();                    // m_rows = the rows of m_all that pass, in order

    QVector<Row> m_all;                     // every row setRows() was given
    QVector<Row> m_rows;                    // what the view shows
};
