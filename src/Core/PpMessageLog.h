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

#include <QList>
#include <QMutex>
#include <QString>

#include <QtLogging>

// Thread-safe singleton that captures pinpoint-category log messages.
// The Qt message handler (pp_debug.cpp) feeds into this; any component
// can read from it by polling fetchSince().
class PpMessageLog
{
public:
    struct Entry {
        QString timestamp;  // "HH:mm:ss"
        QString severity;   // "DEBUG" | "INFO" | "WARN" | "ERROR" | "FATAL"
        QString message;
        int     seq;        // monotonically increasing, used for polling
    };

    static PpMessageLog *instance();

    // Called from the Qt message handler — safe from any thread.
    void append(QtMsgType type, const QString &msg);

    // Returns all entries with seq > afterSeq.  Updates *afterSeq to the
    // latest seq seen.  Safe to call from any thread.
    QList<Entry> fetchSince(int &afterSeq) const;

    // Returns the current highest sequence number without fetching entries.
    int currentSeq() const;

    static constexpr int kMaxEntries = 500;

private:
    PpMessageLog() = default;

    mutable QMutex m_mutex;
    QList<Entry>   m_entries;
    int            m_nextSeq = 0;
};
