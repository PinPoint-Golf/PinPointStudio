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

#include "PpStatsLog.h"

#include <QMutexLocker>
#include <QTime>

PpStatsLog *PpStatsLog::instance()
{
    static PpStatsLog s_instance;
    return &s_instance;
}

void PpStatsLog::append(const QString &category, const QString &message)
{
    QMutexLocker lk(&m_mutex);
    Entry e;
    e.timestamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    e.category  = category;
    e.message   = message;
    e.seq       = m_nextSeq++;
    m_entries.append(e);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();
}

QList<PpStatsLog::Entry> PpStatsLog::fetchSince(int &afterSeq) const
{
    QMutexLocker lk(&m_mutex);
    QList<Entry> result;
    for (const Entry &e : m_entries) {
        if (e.seq > afterSeq)
            result.append(e);
    }
    if (!result.isEmpty())
        afterSeq = result.last().seq;
    return result;
}

int PpStatsLog::currentSeq() const
{
    QMutexLocker lk(&m_mutex);
    return m_nextSeq - 1;
}

void PpStatsLog::clear()
{
    QMutexLocker lk(&m_mutex);
    m_entries.clear();
}
