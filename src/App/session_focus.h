/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE DECLARED FOCUS AT A SESSION'S START (refactor 4-d; build/refactor/step-4/FOCUS-TRACE.md). The Session owns its
// intent (OBJ-SESSION-INTENT, R-INPUT-3, FILE-SESS-02): the focus the wizard declared, or the one a D7 restart carries
// (DEC-154), goes to the session that has just started through its existing command, Session::setSessionIntent
// (EVT-10), from its first shot ({id, 0}). Only the Session is reached, never its repository (P11). A session no
// Athlete holds (N1's no-golfer path) has no Session: its caller keeps today's repository path (ShotProcessor).
//
//   declareStartFocus(session, conditionId)   "" declares nothing (an open assessment: no write)
//   declaredFocusOf(session)                  the focus its file holds now ("" for none), for the D7 carry; nothing
//                                             when the file is not there or cannot be read (a library that has gone:
//                                             the caller falls back on what the session's owner last diagnosed against)
//
// Application ring: it names no store type, no repository and no Gui type.

#include "../DomainModel/session.h"

#include <QString>

#include <optional>

namespace pinpoint {

inline WriteResult declareStartFocus(Session *session, const QString &conditionId)
{
    if (conditionId.isEmpty())
        return { WriteResult::Status::Unchanged, {} };
    if (!session)
        return { WriteResult::Status::Failed, QStringLiteral("No session to declare the focus on.") };
    SessionIntentEdit edit;
    edit.focus = DeclaredFocus{ conditionId, 0 };
    return session->setSessionIntent(edit);   // EVT-10
}

inline std::optional<QString> declaredFocusOf(const Session *session)
{
    if (!session)
        return std::nullopt;
    const SessionInputRead r = session->readSessionIntent();
    if (r.state != SessionInputRead::State::Read)
        return std::nullopt;
    return r.input.declaredFocus ? r.input.declaredFocus->conditionId : QString();
}

} // namespace pinpoint
