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

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include "event_buffer.h"

class BufferController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString      state       READ state       NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sources     READ sources     NOTIFY diagnosticsChanged)
    Q_PROPERTY(quint64      totalEvents READ totalEvents NOTIFY diagnosticsChanged)

public:
    explicit BufferController(pinpoint::EventBuffer *buffer,
                              QObject *parent = nullptr);

    QString      state()       const;
    QVariantList sources()     const;
    quint64      totalEvents() const;

signals:
    void stateChanged();
    void diagnosticsChanged();

private:
    void refresh();

    pinpoint::EventBuffer            *m_buffer;
    QTimer                            m_timer;
    pinpoint::EventBuffer::DiagnosticsSnapshot m_snapshot;
};
