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

// The carousel's port (shot_source.h) over a real Session (refactor step 5.2; R-QML-5). It answers from the Session and
// its Shots on every call and keeps nothing of theirs. While it lives it holds a use of the Session (Session::addUse:
// Athlete::releaseSession refuses a session in use, Q-18), and it hears:
//   - the Session's EVT-01 (shotAdded) and EVT-03 (shotTrashed);
//   - each Shot's events (EVT-21, 04, 05, 06, 07: changed; EVT-22, EVT-02: changed, the document rewritten);
//   - the Session going: released, destroyed, trashed by its Athlete (EVT-20) or moved to another (§5.11).

#include "shot_source.h"

#include <QObject>
#include <QPointer>
#include <QSet>

namespace pinpoint {

class Session;
class Shot;

class SessionShotSource final : public ShotSource
{
public:
    explicit SessionShotSource(Session *session);
    ~SessionShotSource() override;

    QString folder() const override { return m_folder; }
    QVector<ShotRef> shots() override;
    const ShotSummary *summary(const ShotRef &shot, SummaryRead how) override;
    QString swingFolder(const ShotRef &shot) const override;
    analysis::MeasurementSet measurements(const ShotRef &shot) override;

    WriteResult rate(const ShotRef &shot, int stars) override;
    WriteResult note(const ShotRef &shot, const QString &text) override;
    WriteResult setDeclaredClub(const ShotRef &shot, const QString &club) override;
    WriteResult correctClub(const ShotRef &shot, const QString &club) override;
    WriteResult excludeReading(const ShotRef &shot, bool excluded) override;
    WriteResult trash(const ShotRef &shot, QString *where) override;

    void watch(ShotSourceEvents events) override { m_events = std::move(events); }

private:
    Shot *shotOf(const ShotRef &shot) const;
    void  hear(Shot *shot);
    void  goneNow();

    QPointer<Session>  m_session;
    QString            m_folder;
    bool               m_using = false;
    bool               m_gone  = false;
    ShotSourceEvents   m_events;
    QSet<const Shot *> m_heard;
    QObject            m_context;   // every connection's context: they go with this source
};

// The source over `session`, or null for none.
std::unique_ptr<ShotSource> shotSourceOf(Session *session);

} // namespace pinpoint
