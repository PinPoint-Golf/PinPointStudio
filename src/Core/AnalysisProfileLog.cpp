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

#include "AnalysisProfileLog.h"

#include <QMutexLocker>
#include <QTime>

AnalysisProfileLog *AnalysisProfileLog::instance()
{
    static AnalysisProfileLog s_instance;
    return &s_instance;
}

void AnalysisProfileLog::append(const AnalysisRun &run)
{
    QMutexLocker lk(&m_mutex);
    AnalysisRun e = run;
    e.timestamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    e.seq       = m_nextSeq++;
    m_runs.append(e);
    while (m_runs.size() > kMaxRuns)
        m_runs.removeFirst();
}

QList<AnalysisProfileLog::AnalysisRun> AnalysisProfileLog::fetchSince(int &afterSeq) const
{
    QMutexLocker lk(&m_mutex);
    QList<AnalysisRun> result;
    for (const AnalysisRun &e : m_runs) {
        if (e.seq > afterSeq)
            result.append(e);
    }
    if (!result.isEmpty())
        afterSeq = result.last().seq;
    return result;
}

int AnalysisProfileLog::currentSeq() const
{
    QMutexLocker lk(&m_mutex);
    return m_nextSeq - 1;
}

void AnalysisProfileLog::clear()
{
    QMutexLocker lk(&m_mutex);
    m_runs.clear();
}
