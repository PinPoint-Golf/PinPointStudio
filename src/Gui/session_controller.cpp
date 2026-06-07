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

#include "session_controller.h"

SessionController::SessionController(QObject *parent) : QObject(parent)
{
    m_ticker.setInterval(1000);
    m_ticker.setTimerType(Qt::CoarseTimer);
    connect(&m_ticker, &QTimer::timeout, this, &SessionController::tick);
}

void SessionController::start()
{
    m_clock.restart();
    if (!m_running) { m_running = true; emit runningChanged(); }
    tick();
    m_ticker.start();
}

void SessionController::stop()
{
    m_ticker.stop();
    if (m_running) { m_running = false; emit runningChanged(); }
}

void SessionController::reset()
{
    stop();
    m_label = QStringLiteral("00:00:00");
    emit elapsedLabelChanged();
}

void SessionController::tick()
{
    const qint64 s = m_clock.elapsed() / 1000;
    const QString next = QStringLiteral("%1:%2:%3")
        .arg(s / 3600,      2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60,        2, 10, QChar('0'));
    if (next != m_label) { m_label = next; emit elapsedLabelChanged(); }
}
