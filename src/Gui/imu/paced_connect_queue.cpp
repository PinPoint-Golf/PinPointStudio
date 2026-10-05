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

#include "paced_connect_queue.h"

PacedConnectQueue::PacedConnectQueue(SelectFn select, QObject *parent)
    : QObject(parent)
    , m_select(std::move(select))
{
    m_timer.setSingleShot(true);
    // ⚠ PRECISE: a coarse timer may fire up to 5 % EARLY, and early is the one thing the
    // gap exists to prevent (100 ms off a 2 s gap).
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &PacedConnectQueue::drain);
}

void PacedConnectQueue::start(const QStringList &deviceIds, int gapMs)
{
    // ⚠ REPLACE, NOT APPEND. The two callers each build "every assigned, unconnected
    // device" afresh, so a second press means "this is the list now"; appending would
    // queue a device the first press already connected, and a duplicate select is a real
    // second connect attempt on the same peripheral, not a cosmetic glitch.
    m_timer.stop();
    m_pending = deviceIds;
    m_pending.removeDuplicates();
    m_timer.setInterval(qMax(0, gapMs));
    drain();
}

void PacedConnectQueue::cancel()
{
    m_timer.stop();
    m_pending.clear();
    setActive(false);
}

void PacedConnectQueue::drain()
{
    while (!m_pending.isEmpty()) {
        const QString id = m_pending.takeFirst();
        // The callback may re-enter (start()/cancel() from a slot it triggers); whatever
        // it leaves in m_pending is the truth afterwards, so nothing is cached across it.
        if (m_select && m_select(id))
            break;
    }
    if (!m_pending.isEmpty()) {
        if (!m_timer.isActive()) m_timer.start();
        setActive(true);
    } else {
        m_timer.stop();
        setActive(false);
    }
}

void PacedConnectQueue::setActive(bool on)
{
    if (m_active == on) return;
    m_active = on;
    emit activeChanged();
}
