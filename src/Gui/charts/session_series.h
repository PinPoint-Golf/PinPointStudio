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

// SessionSeries — THE SESSION'S SERIES, ONE PER SESSION (refactor 5.5a; information_model.md OBJ-SESSION-SERIES,
// CLS-SESSION-SERIES; F-01, F-02; J-74). Every shot of the session as the series, phases, kinematic sequence and the
// findings the Charts tabs, Consistency, the Trust views and the report's Charts pages read, published as one immutable
// SessionSnapshot (session_series_types.h). It replaces the five view-owned SessionSeriesModels: one per view, each
// fed by hand, so a Trust view fell behind (survey rows 3, 11).
//
// CREATED AND ENDED WITH ITS SESSION (J-74 Q1; OBJ-SESSION-SERIES' lifecycle row): the composition root attaches one to
// EVERY Session its Athletes load, through the same hook that attaches its SessionDiagnostics (DiagnosticsDoors::attach,
// main.cpp), as the Session's child. ITS CONTENTS ARE READ LAZILY: nothing is read until a reader first asks for the
// snapshot, so a session nobody views (a catch-up's, a sweep's) costs nothing. A view reaches it through its Session:
// `ChartsSeries.of(app.session)` (the QML door below); C++ through SessionSeries::of(session).
//
// ── What it hears (J-74 Q3; information_model.md §7.3 row OBJ-SESSION-SERIES) ─────────────────────────────────────
//   EVT-01 Session::shotAdded, EVT-03 shotTrashed, and each Shot's EVT-02 reanalysed, EVT-04 clubCorrected (the board's
//   club), EVT-05 readingExcluded, EVT-21 declaredClubChanged, EVT-22 captureCompleted: that shot is read again.
//   SessionDiagnostics::diagnosisApplied: the findings are joined again (no file read: the rows are the owner's).
//   EVT-13 RuleData::packChanged: the condition names and readings are taken from the new pack.
//
// ── The decisions worth knowing before reading the .cpp ─────────────────────────────────────────────────────────
// 1. ONE IDIOM (R-CON-5; J-74 Q6): every read runs on the app's WorkPool, on this series' own lane, and comes back
//    through the WorkReceiver (ChartsLane, charts_work.h). One job at a time; a change meanwhile marks it dirty and one
//    more job runs with the latest state. A job holds values only. Destroying the series never waits for its job.
// 2. NEVER THE POSE BLOB: a shot is read through SeriesReader::analysis (the root chunk and /analysis/metrics), keyed by
//    its ShotRef, and dropped as soon as its series are copied out (extractShot, session_series_extract.h).
// 3. CACHED BY WHAT WAS READ: a shot is read again only when its document's stamp moved, or when one of the events
//    above names it. Re-asking costs a stat per swing.
// 4. A PUBLISHED SHOT NEVER CHANGES: a re-read, a new place in the order or new findings make a new ShotSeries; every
//    other shot keeps its pointer. `revision` moves whenever the snapshot's content does.
// 5. THE FINDINGS ARE THE OWNER'S (J-74 Q2; F-02): each shot's rows are its SessionDiagnostics' diagnosisInput() (checked
//    against the flight and annotated by the board — never annotated again here), the tiers its diagnosis()'s ledgers,
//    the work-on order its diagnosis' uncapped order, and the "not graded yet" sentence its fileState(). Nothing here
//    reads diagnostics.json or reduces a ledger.
// 6. THE MEMORY BOUND (DUTIES-5.5 S9): the two most recently read series keep their shots; a third read empties the
//    oldest one that no view is listening to, which reads again (cold) when next asked.
//
// Ownership (information_model.md §10): owner the Session (its child); level session; derived, in memory only; no
// writer (nothing persisted); a view reaches it through its Session. Adapters ring (Gui/charts).
//
// Unit-tested in src/Gui/tests/session_series_test.cpp.

#include "charts_work.h"
#include "../../DomainModel/series_reader.h"
#include "../../Identity/identity.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace pinpoint {
class RuleData;
class Session;
class Shot;
}

namespace pinpoint::charts {

struct SeriesDoors {
    std::shared_ptr<const pinpoint::SeriesReader> reader;
    pinpoint::RuleData                           *ruleData = nullptr;   // null: RuleData::current()
    bool                                          synchronous = false;  // tools and tests: every job inline
};

// A shot left out of the snapshot, and why (counted, never hidden: "15 shots, 2 not analysed").
struct SeriesSkip {
    QString swingDir;
    int     shotId = -1;
    QString reason;
};

class SessionSeries : public SeriesSource
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("a session's series belongs to its Session: ChartsSeries.of(app.session)")

public:
    // The Session's series, made on the first call: its child. What the composition root's attach calls.
    static SessionSeries *attach(pinpoint::Session *session, const SeriesDoors &doors);
    // The series attached to `session` (a Session), or null.
    static SessionSeries *of(const QObject *session);
    ~SessionSeries() override;

    std::shared_ptr<const SessionSnapshot> snapshot() const override;   // first call starts the read
    QString sessionDir() const override { return m_sessionDir; }
    bool    busy() const override { return m_running || m_dirty; }
    int     skippedCount() const override { return int(m_skipped.size()); }
    const std::vector<SeriesSkip> &skipped() const { return m_skipped; }
    pinpoint::Session *session() const;

    // Spin the GUI thread's event loop until nothing is in flight. False on timeout. Tests and tools.
    bool waitForIdle(int msTimeout = 60000);

    // What the jobs have done: documents read, cache hits, time in the reads and the extraction.
    struct LoadStats {
        int    docReads  = 0;
        int    cacheHits = 0;
        qint64 loadNs    = 0;
        qint64 extractNs = 0;
    };
    LoadStats loadStats() const { return m_stats; }
    bool      active() const { return m_active; }

    struct Entry;   // one shot as read (the .cpp)

private:
    SessionSeries(pinpoint::Session *session, const SeriesDoors &doors);

    void activate();
    void deactivate();
    void hearShot(pinpoint::Shot *shot);
    void connectDiagnostics();
    void reread(int swing);
    void forget(int swing);
    void schedule();
    void start();

    QPointer<pinpoint::Session> m_session;
    SeriesDoors                 m_doors;
    QString                     m_sessionDir;
    ChartsLane                  m_lane{ this };

    bool m_active  = false;
    bool m_running = false;
    bool m_dirty   = false;
    bool m_diagConnected = false;
    QSet<int> m_heard;           // shots whose events are connected
    QSet<int> m_force;           // shots to read whatever their stamp says
    bool      m_rulesMoved = false;

    QHash<int, std::shared_ptr<const Entry>> m_entries;   // by swing number
    std::shared_ptr<const SessionSnapshot>  m_snapshot;
    std::vector<SeriesSkip>                 m_skipped;
    quint64                                 m_revision = 0;
    LoadStats                               m_stats;
};

} // namespace pinpoint::charts
