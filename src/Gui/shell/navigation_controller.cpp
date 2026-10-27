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

#include "navigation_controller.h"
#include "session_controller.h"

NavigationController::NavigationController(AthleteController * /*athletes*/,
                                           SessionController *session,
                                           QObject           *parent)
    : QObject(parent), m_session(session)
{
    if (m_session) {
        connect(m_session, &SessionController::runningChanged,
                this, &NavigationController::sessionLockedChanged);
        connect(m_session, &SessionController::activeSessionTypeChanged,
                this, &NavigationController::sessionLockedChanged);
    }
}

int  NavigationController::currentIndex() const { return m_current; }
bool NavigationController::canGoBack()    const { return !m_back.isEmpty(); }
bool NavigationController::canGoForward() const { return !m_forward.isEmpty(); }

bool NavigationController::sessionLocked() const
{
    return m_session && m_session->running() && m_session->activeSessionType() >= 0;
}

// While a session is active, the only reachable screens are the one Session
// screen, System (the Resource Monitor, reached from within Settings) and
// Settings. Settings stays open so the Swing model (Settings slot 10) can be
// reached mid-session — an author argues with a corridor against the live
// swing. There is one Session screen for every session type, so the session
// type plays no part here.
bool NavigationController::blockedDuringSession(int index) const
{
    if (!sessionLocked()) return false;
    switch (index) {
    case AppScreen::Session:
    case AppScreen::System:
    case AppScreen::Settings:
        return false;
    default:
        return true;
    }
}

void NavigationController::navigate(int index) { push(index); }

void NavigationController::navigateRail(int index)
{
    // Home rail button is a hard reset — clears history rather than extending it
    if (index == AppScreen::Home) { navigateHome(); return; }
    push(index);
}

void NavigationController::navigateHome()
{
    if (blockedDuringSession(AppScreen::Home)) return;
    const bool hadHistory = !m_back.isEmpty() || !m_forward.isEmpty();
    m_back.clear();
    m_forward.clear();
    // Already on Home, setCurrentIndex() announces nothing — but the cleared
    // history still changes canGoBack/canGoForward.
    if (m_current == AppScreen::Home) {
        if (hadHistory) emit historyChanged();
        return;
    }
    setCurrentIndex(AppScreen::Home);
}

void NavigationController::back()
{
    if (m_back.isEmpty()) return;
    if (blockedDuringSession(m_back.last())) return;   // history kept intact
    m_forward.append(m_current);
    setCurrentIndex(m_back.takeLast());
}

void NavigationController::forward()
{
    if (m_forward.isEmpty()) return;
    if (blockedDuringSession(m_forward.last())) return;
    m_back.append(m_current);
    setCurrentIndex(m_forward.takeLast());
}

void NavigationController::push(int index)
{
    if (blockedDuringSession(index)) return;

    if (index == m_current) return;

    if (m_back.size() >= kMaxHistory)
        m_back.removeFirst();

    m_back.append(m_current);
    m_forward.clear();
    setCurrentIndex(index);
}

void NavigationController::setCurrentIndex(int index)
{
    if (m_current == index) return;
    m_current = index;
    emit currentIndexChanged();
    emit historyChanged();
}
