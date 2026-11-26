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

// SessionListPresenter: THE ONE SESSION LIST (refactor step 5.7a; information_model.md R-QML-6, OBJ-SESSION-INDEX, §11.2
// "a presenter over SessionIndex"). The Sessions drawer (SessionReviewController → SessionListModel), the report list
// (SessionReportListModel) and the Archiving tab (ArchiveController) used to scan the library each on its own, and a trash, a
// move or a club correction in one was not seen in the others until they scanned again. Each now reads its rows here,
// and keeps only its view state: order, filters, the live row, the selection, the checked rows.
//
// A ROW is one session of an athlete's SessionIndex: its entry (the typed SessionIndexEntry: identity, folder, status,
// archived, and its summary — the swing roll-up, the size on disk, the lead work-on: proposal P1, refactor step 5.7a-P1),
// and the one fact the entry does not hold, the folder's modification time (the kept read, Stores/session_list_reads.h).
// The summary is read through the Athlete (SessionRepository::deferredSummary), off this thread. Typed throughout, no
// QVariant shape: QML sees each screen's own model, never this.
//
// ONE GROUP PER ATHLETE it is told to follow (follow, below; setSource for a test with no Athlete). The drawer and the report list
// read the current athlete's group, Archiving every library athlete's.
//
// IT FOLLOWS THE EVENTS (session_list_follow.cpp), so every list changes at once:
//   EVT-17 started, EVT-23 resumed, EVT-09 ended      the index read again (a row added, a status)
//   EVT-20 trashed, the move out                      the row goes, and the index read again (a reload in flight lands without it)
//   the move in                                       the index read again: the row joins this athlete's group
//   EVT-18 archived, EVT-19 restored                  the index read again and the row's summary read again
//   EVT-01, EVT-03, Session::shotChanged              the row's summary read again (its swings, clubs, devices, size)
// A session nobody holds (no Session, so no event) is read again when a screen asks: reload (the report list's refresh,
// the Archiving tab shown), or entriesChanged (an unheld trash).
//
// THREADS. The index is read on this (the GUI) thread: Athlete::index() is the identity map's, and so is the folder each
// deferred summary takes. The summaries and the kept read run on a pool
// thread of its own, one at a time; each result is posted back and dropped when a newer read of the same row (or group)
// was asked since. A reload's facts all land together, so a list never shows half a scan. Reloads are coalesced: while one
// runs, any number asked make one more.

#include "../../DomainModel/athlete.h"           // SessionIndexEntry, through the Athlete (a view never names the repository)
#include "../../Stores/session_list_reads.h"     // the kept read's value (SessionListFacts: the modification time)

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QVector>

#include <functional>
#include <optional>

namespace pinpoint {

class Athlete;
class Session;

struct SessionListRow {
    AthleteId         owner;     // the group it is listed in (the athlete whose index holds it)
    SessionIndexEntry entry;     // entry.summary.read false: its summary still to come
    SessionListFacts  facts;     // facts.read false: still to come
};

class SessionListPresenter : public QObject
{
    Q_OBJECT
public:
    using IndexFn = std::function<QList<SessionIndexEntry>()>;
    // The kept read of one folder; any thread. Default: SessionListReads::read.
    using ReadFn  = std::function<SessionListFacts(const QString &folder)>;
    // A session's summary read, taken on this thread and run on the pool (Athlete::deferredSummary). Empty: none.
    using SummaryFn = std::function<std::function<SessionIndexSummary()>(const SessionId &session)>;

    explicit SessionListPresenter(QObject *parent = nullptr);
    ~SessionListPresenter() override;

    void setReader(ReadFn read) { m_read = std::move(read); }
    // Tests: the kept reads on the calling thread.
    void setSynchronous(bool on) { m_synchronous = on; }

    // A group: `athlete`'s sessions, as `index` lists them, their summaries read by `summary`. Replaces a group of that id
    // (its rows kept until read again).
    void setSource(const AthleteId &athlete, IndexFn index, SummaryFn summary = {});
    // setSource over the Athlete's own index, and its events (session_list_follow.cpp). Again for the same Athlete: nothing.
    void follow(Athlete *athlete);
    bool has(const AthleteId &athlete) const { return m_groups.contains(athlete); }
    // Whether the group has been read once (its first reload landed).
    bool loaded(const AthleteId &athlete) const { return m_groups.value(athlete).loaded; }
    bool loading(const AthleteId &athlete) const { return m_groups.value(athlete).loading; }

    // The rows, in the index's order.
    QVector<SessionListRow> rowsOf(const AthleteId &athlete) const { return m_groups.value(athlete).rows; }
    std::optional<SessionListRow> row(const SessionId &session) const;
    // The row of a session folder, among every group (a view that still holds a folder: the edge, DEC-90).
    std::optional<SessionListRow> rowAt(const QString &folder) const;

public slots:
    // The group's index and every row's facts read again; the rows land together, then rowsChanged.
    void reload(const pinpoint::AthleteId &athlete);
    // The group's index read again: new rows added (their facts then read), gone ones dropped, the others' entries updated.
    void entriesChanged(const pinpoint::AthleteId &athlete);
    // One row's summary and facts read again.
    void reread(const pinpoint::SessionId &session);
    // The row goes (a trash, a move out).
    void forget(const pinpoint::SessionId &session);

signals:
    // A group's rows were added, removed or read again.
    void rowsChanged(const pinpoint::AthleteId &athlete);
    // One row's entry or facts changed.
    void rowChanged(const pinpoint::SessionId &session);
    void loadingChanged();

private:
    struct Group {
        IndexFn                 index;
        SummaryFn               summary;
        QVector<SessionListRow> rows;
        int                     generation = 0;   // of the reload in flight
        bool                    loading = false;
        bool                    loaded  = false;
        bool                    entriesAgain = false;   // entriesChanged asked while a reload was in flight
        bool                    reloadAgain = false;    // reload asked while one was in flight: one more is owed
    };
    void runRead(std::function<void()> work);
    void landReload(const AthleteId &athlete, int generation, const QList<SessionIndexEntry> &entries,
                    const QVector<SessionIndexSummary> &summaries, const QVector<SessionListFacts> &facts);
    void landFacts(const SessionId &session, int generation, const SessionIndexSummary &summary,
                   const SessionListFacts &facts);
    void readRow(const SessionId &session);
    // session_list_follow.cpp
    void followSession(const AthleteId &athlete, Session *session);
    void dropSource(const AthleteId &athlete);

    QHash<AthleteId, Group>          m_groups;
    QHash<SessionId, int>            m_rowGeneration;
    QHash<AthleteId, QPointer<Athlete>> m_followed;
    QSet<Session *>                  m_followedSessions;
    QSet<SessionId>                  m_pendingReads;
    bool                             m_flushArmed = false;
    ReadFn                           m_read;
    QThreadPool                      m_pool;
    bool                             m_synchronous = false;
};

} // namespace pinpoint
