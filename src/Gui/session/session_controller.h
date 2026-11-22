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

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

class AthleteController;

// Owns the live session clock shown in the session toolbar, the active
// session type and the wizard's pick of a focus for the next start. Capture state lives on CameraManager
// (user intent via startCapture()/stopCapture(); QML truth via the bufferState
// property).
//
// A session is *active* while the clock is running; start(type) sets which
// session type owns it. Every session starts as defaultType: the type is
// internal (it names the session folder and keys per-type settings) and the
// UI never shows or chooses it. The single-active-session invariant is
// enforced here: start() with a different type while running is refused.
// Navigation gating derives from (running && activeSessionType >= 0) in
// NavigationController.
//
// The focus ("working on X": one condition id) is NOT the session's here: the
// Session owns its declared focus (OBJ-SESSION-INTENT; refactor 4-d). This holds
// only the pick until a Session takes it (J-64): Main sets it at the wizard's
// Start (and D7's restart sets the carried one); AppState::handOverFocus declares
// it on the first Session the capture service opens and only then clears it
// (src/App/session_focus.h). endSession() drops what is left. It never changes
// what is captured, detected or graded.
class SessionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SessionController is a context property; the type exists for enum access")
    Q_PROPERTY(bool    running           READ running           NOTIFY runningChanged)
    Q_PROPERTY(QString elapsedLabel      READ elapsedLabel      NOTIFY elapsedLabelChanged)
    Q_PROPERTY(int     activeSessionType READ activeSessionType NOTIFY activeSessionTypeChanged)
    // The club in play for this session (canonical vocabulary id). Seeded from the
    // current athlete's preferred club on start() when unset; the Home CLUB chip
    // (and future toolbar) can override it. Read by ShotProcessor at capture time.
    Q_PROPERTY(QString activeClub        READ activeClub        WRITE setActiveClub NOTIFY activeClubChanged)
    // The session type every session starts as. Named once, here, so the wizard
    // and the toolbar's Capture button cannot drift apart. Always Wrist: every
    // session folder ever saved is _Wrist_, and no rig chooses another.
    Q_PROPERTY(int     defaultType       READ defaultType       CONSTANT)
    // The pick waiting for a Session: a condition id, "" for an open assessment.
    // Cleared once a Session took it (AppState::handOverFocus, from its first
    // shot), by Extend (the session keeps its own) and by endSession().
    Q_PROPERTY(QString focusConditionId  READ focusConditionId  WRITE setFocusConditionId NOTIFY focusChanged)

public:
    // The internal session type. Every session starts as defaultType (Wrist);
    // the others survive for old session folders (the _<Type>_ token and
    // capture.sessionType) and the per-session-type AppSettings they key. It is
    // NOT a screen index: every type shares the one AppScreen::Session screen
    // (shell/app_screen.h). Registered with QML (SessionController.Wrist etc.) —
    // use defaultType or the enum names in QML, never the raw integers.
    enum class Type { None = -1, Swing = 0, Wrist = 1, Grf = 2, Coach = 3 };
    Q_ENUM(Type)

    explicit SessionController(AthleteController *athlete = nullptr, QObject *parent = nullptr);

    bool    running()           const { return m_running; }
    QString elapsedLabel()      const { return m_label; }
    int     activeSessionType() const { return static_cast<int>(m_sessionType); }
    QString activeClub()        const { return m_activeClub; }
    int     defaultType()       const { return static_cast<int>(Type::Wrist); }
    QString focusConditionId()  const { return m_focusConditionId; }

    void setActiveClub(const QString &club);
    void setFocusConditionId(const QString &id);

    Q_INVOKABLE void start(int sessionType);  // begin a session of this type + start the clock
    Q_INVOKABLE void endSession();             // stop the clock + clear the type, club and focus pick (unlocks navigation)
    Q_INVOKABLE void stop();    // freeze the clock (type untouched)
    Q_INVOKABLE void reset();   // back to 00:00:00, stopped

signals:
    // A session began, of this type.  Emitted after the clock starts and after
    // ShotProcessor::beginSessionFolder() has run, so a listener sees the real
    // outcome of the folder allocation rather than a guess at it.
    void sessionStarted(int sessionType);
    void runningChanged();
    void elapsedLabelChanged();
    void activeSessionTypeChanged();
    void activeClubChanged();
    void focusChanged();

private:
    void tick();

    AthleteController *m_athlete = nullptr;   // preferred-club seed source (not owned)
    QElapsedTimer m_clock;
    QTimer        m_ticker;
    bool          m_running     = false;
    Type          m_sessionType = Type::None;
    QString       m_label       = QStringLiteral("00:00:00");
    QString       m_activeClub;               // "" until seeded/set; cleared on endSession
    QString       m_focusConditionId;         // the pick waiting for a Session; "" = open assessment
};
