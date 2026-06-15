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

// Thread-safe singleton ring of resource-profiler summary lines — a sibling of
// PpMessageLog, kept deliberately SEPARATE so periodic/session-end profiler
// dumps never pollute the main application log (and never ride along in its
// Export). The profiler's dumpToLog() feeds this; the profiler controller polls
// fetchSince() and surfaces it in the monitor's STATS HISTORY section with its
// own filter + export. Each entry carries a coarse category (GAUGE / THREAD /
// SCOPE / MEM) so the UI can filter without parsing the message text.
class PpStatsLog
{
public:
    struct Entry {
        QString timestamp;  // "HH:mm:ss"
        QString category;   // "GAUGE" | "THREAD" | "SCOPE" | "MEM"
        QString message;
        int     seq;        // monotonically increasing, used for polling
    };

    static PpStatsLog *instance();

    // Append a categorised stats line — safe from any thread (the profiler dumps
    // from the controller's sampler thread).
    void append(const QString &category, const QString &message);

    // Returns all entries with seq > afterSeq.  Updates *afterSeq to the latest
    // seq seen.  Safe to call from any thread.
    QList<Entry> fetchSince(int &afterSeq) const;

    // Current highest sequence number without fetching entries.
    int currentSeq() const;

    // Drop every retained entry (the sequence counter keeps climbing so pollers
    // are not confused).  Used by tests; the controller's "Clear" advances its
    // own read cursor instead, leaving the ring intact.
    void clear();

    static constexpr int kMaxEntries = 1000;

private:
    PpStatsLog() = default;

    mutable QMutex m_mutex;
    QList<Entry>   m_entries;
    int            m_nextSeq = 0;
};
