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
#include <QObject>

#include "app_screen.h"

class AthleteController;
class SessionController;

// Screen history (back/forward) and the session lock. Every index is an AppScreen value
// (app_screen.h); the invokables take int because QML passes enum values as ints and the
// probes call navigate(n).
class NavigationController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int  currentIndex  READ currentIndex  NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canGoBack     READ canGoBack     NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward  READ canGoForward  NOTIFY historyChanged)
    // True while a session is active (clock running with a session type set).
    // Every navigation path is then locked to AppScreen::Session, SwingModel,
    // System and Settings, whatever the session type — enforced here,
    // visualised in PpRail.
    Q_PROPERTY(bool sessionLocked READ sessionLocked NOTIFY sessionLockedChanged)

public:
    // `athletes` is accepted for main.cpp's sake and not kept: nothing here reads it.
    explicit NavigationController(AthleteController *athletes,
                                  SessionController *session,
                                  QObject           *parent = nullptr);

    int  currentIndex()  const;
    bool canGoBack()     const;
    bool canGoForward()  const;
    bool sessionLocked() const;

    Q_INVOKABLE void navigate(int index);
    Q_INVOKABLE void navigateRail(int index);
    Q_INVOKABLE void navigateHome();
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();

signals:
    void currentIndexChanged();
    void historyChanged();
    void sessionLockedChanged();

private:
    void push(int index);
    void setCurrentIndex(int index);
    bool blockedDuringSession(int index) const;

    SessionController *m_session;

    int        m_current  = AppScreen::Home;
    QList<int> m_back;
    QList<int> m_forward;

    static constexpr int kMaxHistory = 20;
};
