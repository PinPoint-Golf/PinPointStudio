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

class AthleteController;
class SessionController;

class NavigationController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int  currentIndex  READ currentIndex  NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canGoBack     READ canGoBack     NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward  READ canGoForward  NOTIFY historyChanged)
    // True while a session is active (clock running with a session type set).
    // Every navigation path is then locked to System (8), Settings (9) and the
    // active session type's screen — enforced here, visualised in PpRail.
    Q_PROPERTY(bool sessionLocked READ sessionLocked NOTIFY sessionLockedChanged)

public:
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

    AthleteController *m_athletes;
    SessionController *m_session;

    int        m_current  = 0;
    QList<int> m_back;
    QList<int> m_forward;

    static constexpr int kMaxHistory = 20;
};
