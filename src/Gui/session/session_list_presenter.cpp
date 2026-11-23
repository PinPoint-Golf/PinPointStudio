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

#include "session_list_presenter.h"

#include <QDir>
#include <QMetaObject>
#include <QTimer>

#include <utility>

namespace pinpoint {

namespace {
constexpr int kCoalesceMs = 50;   // events for one session within this are read once
}

SessionListPresenter::SessionListPresenter(QObject *parent)
    : QObject(parent)
    , m_read([](const QString &folder) { return SessionListReads::read(folder); })
{
    m_pool.setMaxThreadCount(1);
}

SessionListPresenter::~SessionListPresenter()
{
    // A read in flight posts its result to `this`: every generation is moved on, and the pool is waited for.
    for (Group &g : m_groups) ++g.generation;
    for (int &n : m_rowGeneration) ++n;
    m_pool.waitForDone();
}

void SessionListPresenter::runRead(std::function<void()> work)
{
    if (m_synchronous) { work(); return; }
    m_pool.start(std::move(work));
}

void SessionListPresenter::setSource(const AthleteId &athlete, IndexFn index)
{
    m_groups[athlete].index = std::move(index);
}

std::optional<SessionListRow> SessionListPresenter::row(const SessionId &session) const
{
    for (const Group &g : m_groups)
        for (const SessionListRow &r : g.rows)
            if (r.entry.id() == session) return r;
    return std::nullopt;
}

std::optional<SessionListRow> SessionListPresenter::rowAt(const QString &folder) const
{
    const QString f = QDir::cleanPath(folder);
    for (const Group &g : m_groups)
        for (const SessionListRow &r : g.rows)
            if (QDir::cleanPath(r.entry.folder) == f) return r;
    return std::nullopt;
}

void SessionListPresenter::reload(const AthleteId &athlete)
{
    auto it = m_groups.find(athlete);
    if (it == m_groups.end() || !it->index) return;
    // Coalesced (review B2): one more read is owed while one runs, however many are asked; the landing asks it. Nothing is
    // read here meanwhile, not even the index (a bulk job's refresh after every item, Archiving's of every athlete).
    if (it->loading && !m_synchronous) { it->reloadAgain = true; return; }
    const QList<SessionIndexEntry> entries = it->index();
    const int gen = ++it->generation;
    const bool was = it->loading;
    it->loading = true;
    it->entriesAgain = false;
    if (!was) emit loadingChanged();
    QStringList folders;
    for (const SessionIndexEntry &e : entries) folders.append(e.folder);
    const ReadFn read = m_read;
    runRead([this, athlete, gen, entries, folders, read] {
        QVector<SessionListFacts> facts;
        facts.reserve(folders.size());
        for (const QString &f : folders) facts.append(read ? read(f) : SessionListFacts{});
        if (m_synchronous) { landReload(athlete, gen, entries, facts); return; }
        QMetaObject::invokeMethod(this, [this, athlete, gen, entries, facts] { landReload(athlete, gen, entries, facts); },
                                  Qt::QueuedConnection);
    });
}

void SessionListPresenter::landReload(const AthleteId &athlete, int generation, const QList<SessionIndexEntry> &entries,
                                      const QVector<SessionListFacts> &facts)
{
    auto it = m_groups.find(athlete);
    if (it == m_groups.end() || it->generation != generation) return;   // superseded: the newer one lands
    QVector<SessionListRow> rows;
    rows.reserve(entries.size());
    // One pool thread runs the reads in the order they were asked, so they land in that order: a row read asked before this
    // reload landed before it, one asked after lands after it.
    for (qsizetype i = 0; i < entries.size(); ++i)
        rows.append({ athlete, entries.at(i), facts.value(i) });
    it->rows = std::move(rows);
    it->loading = false;
    it->loaded = true;
    const bool entriesAgain = std::exchange(it->entriesAgain, false);
    const bool reloadAgain = std::exchange(it->reloadAgain, false);
    // The index moved while the reload ran (a trash, a move: review B1): the rows are put right against it before any view
    // sees them, so a trashed row never shows again. Then the one more read owed, if one was asked (review B2).
    if (entriesAgain) entriesChanged(athlete);
    else emit rowsChanged(athlete);
    if (reloadAgain) reload(athlete);   // loading again: it emits loadingChanged
    else emit loadingChanged();
}

void SessionListPresenter::entriesChanged(const AthleteId &athlete)
{
    auto it = m_groups.find(athlete);
    if (it == m_groups.end() || !it->index) return;
    if (it->loading) { it->entriesAgain = true; return; }
    if (!it->loaded) return;   // nobody has asked for this group yet: its first reload reads it
    const QList<SessionIndexEntry> entries = it->index();
    QHash<SessionId, SessionListRow> before;
    for (const SessionListRow &r : std::as_const(it->rows)) before.insert(r.entry.id(), r);
    QVector<SessionListRow> rows;
    QList<SessionId> toRead;
    for (const SessionIndexEntry &e : entries) {
        const auto b = before.constFind(e.id());
        if (b == before.cend()) {
            rows.append({ athlete, e, SessionListFacts{} });
            toRead.append(e.id());
        } else {
            rows.append({ athlete, e, b->facts });
            // A folder that moved (a restore puts it back; a move gives it a new one) is read again.
            if (QDir::cleanPath(b->entry.folder) != QDir::cleanPath(e.folder) || b->entry.archived != e.archived
                || !b->facts.read)
                toRead.append(e.id());
        }
    }
    it->rows = std::move(rows);
    emit rowsChanged(athlete);
    for (const SessionId &id : std::as_const(toRead)) reread(id);
}

void SessionListPresenter::reread(const SessionId &session)
{
    if (m_synchronous) { readRow(session); return; }
    // Coalesced: a burst of events for one session (a re-analysis fires one per shot) reads it once.
    m_pendingReads.insert(session);
    if (m_flushArmed) return;
    m_flushArmed = true;
    QTimer::singleShot(kCoalesceMs, this, [this] {
        m_flushArmed = false;
        const QSet<SessionId> pending = std::exchange(m_pendingReads, {});
        for (const SessionId &id : pending) readRow(id);
    });
}

void SessionListPresenter::readRow(const SessionId &session)
{
    std::optional<SessionListRow> r = row(session);
    if (!r) return;
    const int gen = ++m_rowGeneration[session];
    const QString folder = r->entry.folder;
    const ReadFn read = m_read;
    runRead([this, session, gen, folder, read] {
        const SessionListFacts f = read ? read(folder) : SessionListFacts{};
        if (m_synchronous) { landFacts(session, gen, f); return; }
        QMetaObject::invokeMethod(this, [this, session, gen, f] { landFacts(session, gen, f); }, Qt::QueuedConnection);
    });
}

void SessionListPresenter::landFacts(const SessionId &session, int generation, const SessionListFacts &facts)
{
    if (m_rowGeneration.value(session) != generation) return;   // a newer read was asked
    for (Group &g : m_groups)
        for (SessionListRow &r : g.rows)
            if (r.entry.id() == session) {
                r.facts = facts;
                emit rowChanged(session);
                return;
            }
}

void SessionListPresenter::forget(const SessionId &session)
{
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        const qsizetype before = it->rows.size();
        it->rows.removeIf([&](const SessionListRow &r) { return r.entry.id() == session; });
        if (it->rows.size() != before) {
            ++m_rowGeneration[session];
            emit rowsChanged(it.key());
            return;
        }
    }
}

void SessionListPresenter::dropSource(const AthleteId &athlete)
{
    if (!m_groups.contains(athlete)) return;
    m_groups.remove(athlete);
    m_followed.remove(athlete);
    emit rowsChanged(athlete);
}

} // namespace pinpoint
