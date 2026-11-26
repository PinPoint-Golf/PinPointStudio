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

// TEST DOUBLES of the carousel's port onto a Session (refactor step 5.2; src/App/shot_source.h), for the tests of the
// views that link no Session (the carousel's and the launch monitor board's). They stand where a SessionShotSource
// stands in the app and behave as the owner does — and no more: a shot is announced (added) only when its document
// lands, the rating and the note send no event (DEC-70), the declared club, the club correction and the exclusion
// send one, a rewrite of the document sends one with `rewritten`. The real path, over a real Session, is tested by
// shot_commands_test (C12, V1–V8).
//   MemoryShotSource  shots held in memory (the board's tests: their readings come from setDetailReader)
//   FolderShotSource  a session folder on disk, through the one swing door (testdoc::repo()): the ledger's tests,
//                     whose edits must round-trip through the documents

#include "../../App/shot_source.h"
#include "../../Stores/swing_doc.h"
#include "../../Stores/swing_paths.h"
#include "../../Stores/tests/swing_doc_test_writes.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <functional>

namespace pinpoint::testdoc {

// Common to both: the one listener, and the events as the owner sends them.
class ShotSourceBase : public ShotSource
{
public:
    void watch(ShotSourceEvents events) override { m_events = std::move(events); }
    void announce(const ShotRef &shot) { if (m_events.added) m_events.added(shot); }
    void changed(const ShotRef &shot, bool rewritten) { if (m_events.changed) m_events.changed(shot, rewritten); }
    void trashedNow(const ShotRef &shot) { if (m_events.trashed) m_events.trashed(shot); }
    void goneNow() { if (m_events.gone) m_events.gone(); }
    int  summaryReads = 0;   // every summary read that went to the "document" (not the kept one)
protected:
    ShotSourceEvents m_events;
};

class MemoryShotSource final : public ShotSourceBase
{
public:
    explicit MemoryShotSource(const QString &folder = QStringLiteral("/mem/session"),
                              const SessionId &id = SessionId::create())
        : m_folder(folder), m_id(id) {}

    SessionId id() const { return m_id; }
    ShotRef   refOf(int swing) const { return ShotRef(m_id, swing); }
    // A shot with a document (summary.ok is set), in the collection; no event (a session shown already holds it).
    ShotRef put(int swing, ShotSummary s, const QString &dir = {})
    {
        s.ok = true;
        if (s.ordinal == 0) s.ordinal = swing;
        m_shots.insert(swing, { s, dir.isEmpty() ? m_folder + QStringLiteral("/swing_%1").arg(swing, 4, 10, QLatin1Char('0')) : dir });
        return refOf(swing);
    }
    // A shot allocated with no document yet (summary.ok false).
    ShotRef allocate(int swing)
    {
        m_shots.insert(swing, { ShotSummary{}, m_folder + QStringLiteral("/swing_%1").arg(swing, 4, 10, QLatin1Char('0')) });
        return refOf(swing);
    }
    // Its document lands: EVT-01.
    void land(const ShotRef &shot, ShotSummary s)
    {
        s.ok = true;
        if (s.ordinal == 0) s.ordinal = shot.swingNumber();
        m_shots[shot.swingNumber()].summary = s;
        announce(shot);
    }
    // Its document rewritten (a late part, a re-analysis): EVT-22 / EVT-02.
    void rewrite(const ShotRef &shot, const std::function<void(ShotSummary &)> &edit)
    {
        edit(m_shots[shot.swingNumber()].summary);
        changed(shot, true);
    }
    // Trashed by someone else (EVT-03).
    void trashElsewhere(const ShotRef &shot) { m_shots.remove(shot.swingNumber()); trashedNow(shot); }
    bool failSaves = false;   // every command's save fails (nothing changes, no event)
    QStringList failing;      // the commands, by name ("note", "rate", "setDeclaredClub", …), whose saves fail
    QMap<QString, int> calls; // every review command called, by name (a failed one too)
    int  trashRefusals = 0;   // the next N trashes are refused (a file open)

    QString folder() const override { return m_folder; }
    QVector<ShotRef> shots() override
    {
        QVector<ShotRef> out;
        for (auto it = m_shots.cbegin(); it != m_shots.cend(); ++it) out << refOf(it.key());
        return out;
    }
    const ShotSummary *summary(const ShotRef &shot, SummaryRead how) override
    {
        if (!held(shot)) return nullptr;
        if (how != SummaryRead::Kept && how != SummaryRead::KeptOnly) ++summaryReads;
        return &m_shots[shot.swingNumber()].summary;
    }
    QString swingFolder(const ShotRef &shot) const override
    { return held(shot) ? m_shots.value(shot.swingNumber()).dir : QString(); }

    WriteResult rate(const ShotRef &s, int stars) override
    { return edit("rate", s, [stars](ShotSummary &f) { f.rating = std::clamp(stars, 0, 5); }, false); }
    WriteResult note(const ShotRef &s, const QString &text) override
    { return edit("note", s, [&text](ShotSummary &f) { f.note = text; }, false); }
    WriteResult setDeclaredClub(const ShotRef &s, const QString &club) override
    { return edit("setDeclaredClub", s, [&club](ShotSummary &f) { f.club = club; }, true); }
    WriteResult correctClub(const ShotRef &s, const QString &club) override
    { return edit("correctClub", s, [&club](ShotSummary &f) { f.lmClubOverride = club; }, true); }
    WriteResult excludeReading(const ShotRef &s, bool excluded) override
    { return edit("excludeReading", s, [excluded](ShotSummary &f) { f.lmExcluded = excluded; }, true); }
    WriteResult trash(const ShotRef &s, QString *) override
    {
        if (!held(s)) return { WriteResult::Status::Failed, QStringLiteral("That shot is not in this session.") };
        if (trashRefusals > 0) { --trashRefusals; return { WriteResult::Status::Failed, QStringLiteral("refused") }; }
        trashElsewhere(s);
        return { WriteResult::Status::Written, {} };
    }

private:
    struct Entry { ShotSummary summary; QString dir; };
    bool held(const ShotRef &s) const { return s.session() == m_id && m_shots.contains(s.swingNumber()); }
    WriteResult edit(const char *command, const ShotRef &s, const std::function<void(ShotSummary &)> &apply, bool event)
    {
        ++calls[QString::fromLatin1(command)];
        if (!held(s)) return { WriteResult::Status::Failed, QStringLiteral("That shot is not in this session.") };
        if (failSaves || failing.contains(QString::fromLatin1(command))) return { WriteResult::Status::Failed, QStringLiteral("the disk is full") };
        apply(m_shots[s.swingNumber()].summary);
        if (event) changed(s, false);
        return { WriteResult::Status::Written, {} };
    }
    QString            m_folder;
    SessionId          m_id;
    QMap<int, Entry>   m_shots;
};

// A session folder through the one swing door (testdoc::repo(), the repository the app's Shots use): the summary as
// the Shot reads it (RecordingRepository::readSummary), kept per shot as the Shot keeps it; the edits saved by the door.
class FolderShotSource final : public ShotSourceBase
{
public:
    // The test edge (testdoc::repo()) makes any folder a shot of a session of its own, swing 1; a Session's shots are
    // one session's, numbered by their folders. So this source names each swing folder ShotRef(its session, its
    // folder's number) for the view, and reaches the door by the folder.
    explicit FolderShotSource(const QString &sessionFolder) : m_folder(sessionFolder), m_id(SessionId::create()) {}

    // The trash's move (the retry tests hold it); default: the app's move (the library's own trash).
    std::function<bool(const QString &swingDir)> trashMove;
    ShotRef refOf(const QString &swingDir)
    {
        const QString key = QDir::cleanPath(swingDir);
        const ShotRef r(m_id, SwingPaths::swingNumberOf(QFileInfo(key).fileName()));
        if (!r.isNull()) m_dirs.insert(r, key);
        return r;
    }
    // A swing's first document has landed in this folder (capture's join, a device-only document): EVT-01.
    void land(const QString &swingDir) { const ShotRef r = refOf(swingDir); m_kept.remove(r); announce(r); }
    // A swing's document rewritten (a late reading, a re-analysis): EVT-22 / EVT-02.
    void rewritten(const QString &swingDir) { const ShotRef r = refOf(swingDir); m_kept.remove(r); changed(r, true); }

    QString folder() const override { return m_folder; }
    QVector<ShotRef> shots() override
    {
        QMap<int, ShotRef> bySwing;
        if (QFileInfo(m_folder).isDir())
            for (const QString &sd : SwingDocReader::findSwingDirs(m_folder)) {
                const ShotRef r = refOf(sd);
                if (!r.isNull()) bySwing.insert(r.swingNumber(), r);
            }
        return QVector<ShotRef>(bySwing.cbegin(), bySwing.cend());
    }
    const ShotSummary *summary(const ShotRef &shot, SummaryRead how) override
    {
        const ShotRef door = doorRef(shot);
        if (door.isNull()) return nullptr;
        if (how == SummaryRead::KeptOnly && !m_kept.contains(shot)) return nullptr;
        if (how != SummaryRead::Kept && how != SummaryRead::KeptOnly) m_kept.remove(shot);
        if (!m_kept.contains(shot)) {
            ++summaryReads;
            m_kept.insert(shot, repo().readSummary(door));   // as the Shot reads it (a test's JSON document: parsed)
        }
        return &m_kept[shot];
    }
    QString swingFolder(const ShotRef &shot) const override { return m_dirs.value(shot); }
    // As the Shot reads them (RecordingRepository::readMeasurements; refactor 6.1a).
    analysis::MeasurementSet measurements(const ShotRef &shot) override
    {
        const ShotRef door = doorRef(shot);
        return door.isNull() ? analysis::MeasurementSet{} : repo().readMeasurements(door);
    }

    WriteResult rate(const ShotRef &s, int stars) override { ReviewEdit e; e.rating = stars; return edit(s, e, false); }
    WriteResult note(const ShotRef &s, const QString &text) override { ReviewEdit e; e.note = text; return edit(s, e, false); }
    WriteResult setDeclaredClub(const ShotRef &s, const QString &club) override { ReviewEdit e; e.club = club; return edit(s, e, true); }
    WriteResult correctClub(const ShotRef &s, const QString &club) override
    { ReviewEdit e; e.clubOverride = club; return edit(s, e, true); }
    WriteResult excludeReading(const ShotRef &s, bool excluded) override
    { ReviewEdit e; e.excluded = excluded; return edit(s, e, true); }
    WriteResult trash(const ShotRef &s, QString *) override
    {
        const QString dir = m_dirs.value(s);
        if (dir.isEmpty()) return { WriteResult::Status::Failed, QStringLiteral("That shot is not in this session.") };
        if (QFileInfo::exists(dir)) {   // gone counts as gone
            const bool moved = trashMove ? trashMove(dir) : SwingPaths::trashPath(dir);
            if (!moved) return { WriteResult::Status::Failed, QStringLiteral("refused") };
        }
        m_kept.remove(s);
        m_dirs.remove(s);
        trashedNow(s);   // EVT-03
        return { WriteResult::Status::Written, {} };
    }

private:
    ShotRef doorRef(const ShotRef &s) const
    {
        const QString dir = m_dirs.value(s);
        return dir.isEmpty() ? ShotRef() : repo().shotOfFolder(dir);
    }
    WriteResult edit(const ShotRef &s, const ReviewEdit &e, bool event)
    {
        const ShotRef door = doorRef(s);
        if (door.isNull()) return { WriteResult::Status::Failed, QStringLiteral("That shot is not in this session.") };
        const WriteResult w = repo().editReview(door, e);
        if (w.ok()) {
            m_kept.remove(s);   // the Shot keeps its summary in step with its saves
            if (event) changed(s, false);
        }
        return w;
    }
    QString                     m_folder;
    SessionId                   m_id;
    QHash<ShotRef, QString>     m_dirs;
    QHash<ShotRef, ShotSummary> m_kept;
};

} // namespace pinpoint::testdoc
