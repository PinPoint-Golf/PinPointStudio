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

#include "PpMessageLog.h"

#include <QMutexLocker>
#include <QTime>

PpMessageLog *PpMessageLog::instance()
{
    static PpMessageLog s_instance;
    return &s_instance;
}

static QString severityString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO");
    case QtWarningMsg:  return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}

void PpMessageLog::append(QtMsgType type, const QString &msg)
{
    QMutexLocker lk(&m_mutex);
    Entry e;
    e.timestamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    e.severity  = severityString(type);
    e.message   = msg;
    e.seq       = m_nextSeq++;
    m_entries.append(e);
    // Drop oldest when we exceed the cap
    while (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();
}

QList<PpMessageLog::Entry> PpMessageLog::fetchSince(int &afterSeq) const
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

int PpMessageLog::currentSeq() const
{
    QMutexLocker lk(&m_mutex);
    return m_nextSeq - 1;
}
