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

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include "PpMessageLog.h"
#include "event_buffer.h"

class CameraManager;
class ImuController;

class ResourceMonitorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString      bufferState     READ bufferState     NOTIFY snapshotChanged)
    Q_PROPERTY(quint64      totalEvents     READ totalEvents     NOTIFY snapshotChanged)
    Q_PROPERTY(quint64      timelineEntries READ timelineEntries NOTIFY snapshotChanged)
    Q_PROPERTY(int          sourceCount     READ sourceCount     NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList sources         READ sources         NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList devices         READ devices         NOTIFY snapshotChanged)
    Q_PROPERTY(QStringList  warnings        READ warnings        NOTIFY snapshotChanged)
    Q_PROPERTY(int          snapshotAgeMs      READ snapshotAgeMs      NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList timelineHistory    READ timelineHistory    NOTIFY snapshotChanged)
    Q_PROPERTY(QString      totalEventsStr     READ totalEventsStr     NOTIFY snapshotChanged)
    Q_PROPERTY(QString      timelineEntriesStr READ timelineEntriesStr NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList messageLog         READ messageLog         NOTIFY snapshotChanged)

public:
    explicit ResourceMonitorController(
        pinpoint::EventBuffer *buffer,
        CameraManager         *cameras,
        ImuController         *imu,
        QObject               *parent = nullptr);

    QString      bufferState()        const;
    quint64      totalEvents()        const;
    quint64      timelineEntries()    const;
    int          sourceCount()        const;
    QVariantList sources()            const;
    QVariantList devices()            const;
    QStringList  warnings()           const;
    int          snapshotAgeMs()      const;
    QVariantList timelineHistory()    const;
    QString      totalEventsStr()     const;
    QString      timelineEntriesStr() const;
    QVariantList messageLog()         const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearLog();

signals:
    void snapshotChanged();

private:
    pinpoint::EventBuffer *m_buffer;
    CameraManager         *m_cameras;
    ImuController         *m_imu;

    QElapsedTimer m_ageTimer;

    pinpoint::EventBuffer::DiagnosticsSnapshot m_snapshot;

    quint64        m_prevTimelineEntries = 0;

    QVariantList   m_sources;
    QVariantList   m_devices;
    QStringList    m_warnings;
    QList<quint64> m_timelineHistory;
    QVariantList   m_messageLog;
    QSet<QString>  m_activeWarnings;  // for stall edge-detection only
    int            m_logSeq = -1;     // last PpMessageLog seq fetched

    static constexpr int kHistoryCount  = 15;
    static constexpr int kMaxLogEntries = 500;
};
