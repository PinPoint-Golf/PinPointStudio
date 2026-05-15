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

#include "buffer_controller.h"
#include "pp_debug.h"

BufferController::BufferController(pinpoint::EventBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_buffer(buffer)
{
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &BufferController::refresh);
    m_timer.start();
}

void BufferController::refresh()
{
    if (!m_buffer) return;
    m_snapshot = m_buffer->diagnostics();

    ppWarn() << "[Buffer] state=" << state()
             << " timeline=" << m_snapshot.timeline_entries;
    for (const auto &src : m_snapshot.sources) {
        ppWarn() << "  source=" << QString::fromStdString(src.name)
                 << " written=" << src.events_written
                 << " overwritten=" << src.events_overwritten
                 << " stalled=" << src.stalled;
    }

    emit diagnosticsChanged();
    emit stateChanged();
}

QString BufferController::state() const
{
    if (!m_buffer) return QStringLiteral("unavailable");
    switch (m_snapshot.state) {
    case pinpoint::BufferState::Idle:      return QStringLiteral("idle");
    case pinpoint::BufferState::Capturing: return QStringLiteral("capturing");
    case pinpoint::BufferState::Paused:    return QStringLiteral("paused");
    case pinpoint::BufferState::Stopping:  return QStringLiteral("stopping");
    }
    return QStringLiteral("unknown");
}

QVariantList BufferController::sources() const
{
    QVariantList result;
    for (const auto &s : m_snapshot.sources) {
        QVariantMap m;
        m[QStringLiteral("name")]          = QString::fromStdString(s.name);
        m[QStringLiteral("eventsWritten")] = quint64(s.events_written);
        m[QStringLiteral("overwritten")]   = quint64(s.events_overwritten);
        m[QStringLiteral("stalled")]       = s.stalled;
        result.append(m);
    }
    return result;
}

quint64 BufferController::totalEvents() const
{
    return m_snapshot.timeline_entries;
}
