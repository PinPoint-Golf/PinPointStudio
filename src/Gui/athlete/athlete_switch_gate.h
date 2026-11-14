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

#include <QString>

// AthleteSwitchGate — "is this currentAthleteChanged a real switch?"
//
// AthleteController::currentAthleteChanged fires on every reload(): a switch of athlete, and also
// every edit of any record (one form Save emits it five or six times; each club field commit once).
// A listener that RELOADS DATA on it — the live carousel re-pointed at the athlete's latest session
// (main.cpp), the session review dropped and the session list rebuilt (SessionReviewController) —
// must act only on a switch, or a club-length edit mid-session clears the live carousel (rows lose
// their detail, failed shots vanish, every id changes, the selection names nothing) and drops a
// review (SWE audit E-03).
//
// switched(uuid) answers true when `uuid` differs from the one it last answered true for, and
// remembers it; prime it with the uuid the listener's data was loaded for, so the first edit after
// start-up is not mistaken for a switch. Unprimed, the first call is a switch.
//
// This is the Stage 0.8g guard. Splitting the signal into a switch and an edit (and moving the
// other listeners) is refactor step 2.
class AthleteSwitchGate
{
public:
    AthleteSwitchGate() = default;
    explicit AthleteSwitchGate(const QString &primedUuid) { prime(primedUuid); }

    void prime(const QString &uuid) { m_last = uuid; m_primed = true; }

    bool switched(const QString &uuid)
    {
        if (m_primed && uuid == m_last) return false;
        m_last   = uuid;
        m_primed = true;
        return true;
    }

    QString last() const { return m_last; }

private:
    QString m_last;
    bool    m_primed = false;
};
