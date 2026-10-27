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

// SessionController — the live session clock and the active session type.
//
// ⚠ THE ORACLES ARE THE INVARIANTS THE CLASS STATES ABOUT ITSELF.  From its
// header: "A session is *active* while the clock is running; start(type) sets
// which session type owns it. The single-active-session invariant is enforced
// here, not in the UI." From start()'s own comment: the sessionStarted signal
// means "A session began, of this type", and main.cpp's session-start preflight
// hangs off it. From the type enum: the values "match the QML session-type
// indices ... use the enum names in QML, never the raw integers."
//
// Those sentences have consequences that nothing has ever checked — one session
// is one clock and one announcement, and a session type is one of four things.
// Since the four-modes refactor every session starts as defaultType (Wrist), and
// the controller carries the session's focus: set before start(), kept by it,
// released by endSession().

#include "session/session_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <cstdio>

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static void checkInt(const QString &label, long long got, long long want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label).arg(got).arg(want), got == want);
}

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    check(QStringLiteral("%1 (got \"%2\", want \"%3\")").arg(label, got, want), got == want);
}

// The label only advances from tick(), which the 1 Hz ticker drives, so these
// cases need a real event loop. Spin until the condition holds rather than for a
// fixed span: a coarse timer may be adjusted or coalesced by the platform, and a
// fixed wait makes the difference between "the clock advances" and "this machine
// was busy" invisible.
template <typename Predicate>
static bool spinUntil(Predicate done, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (done())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

// Spin for a fixed span — for the cases asserting that nothing happens.
static void spin(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static int typeOf(const SessionController &s) { return s.activeSessionType(); }

// ---------------------------------------------------------------------------
// I — the idle state
// ---------------------------------------------------------------------------

static void testIdleState()
{
    std::printf("\nI — before anything starts\n");

    SessionController s;
    check(QStringLiteral("I1 a fresh controller is not running"), !s.running());
    checkInt(QStringLiteral("I2 no session type is active"), typeOf(s),
             static_cast<int>(SessionController::Type::None));
    checkStr(QStringLiteral("I3 the clock reads zero"), s.elapsedLabel(),
             QStringLiteral("00:00:00"));
    check(QStringLiteral("I4 no club is in play"), s.activeClub().isEmpty());
    check(QStringLiteral("I5 no focus: an open assessment"), s.focusConditionId().isEmpty());
    checkInt(QStringLiteral("I6 the focus applies from the first shot"), s.focusFromShot(), 0);
}

// ---------------------------------------------------------------------------
// S — the single-active-session invariant
// ---------------------------------------------------------------------------

static void testSingleActiveSession()
{
    std::printf("\nS — one session at a time\n");

    SessionController s;
    QSignalSpy started(&s, &SessionController::sessionStarted);
    QSignalSpy runChanged(&s, &SessionController::runningChanged);
    QSignalSpy typeChanged(&s, &SessionController::activeSessionTypeChanged);

    s.start(static_cast<int>(SessionController::Type::Wrist));
    check(QStringLiteral("S1 starting a session runs the clock"), s.running());
    checkInt(QStringLiteral("S2 the type is the one asked for"), typeOf(s),
             static_cast<int>(SessionController::Type::Wrist));
    checkInt(QStringLiteral("S3 it announced one session start"), started.count(), 1);
    checkInt(QStringLiteral("S4 with the right type"),
             started.at(0).at(0).toInt(), static_cast<int>(SessionController::Type::Wrist));
    checkInt(QStringLiteral("S5 running changed once"), runChanged.count(), 1);
    checkInt(QStringLiteral("S6 the type changed once"), typeChanged.count(), 1);

    // A start for a DIFFERENT type is refused — the stated invariant.
    s.start(static_cast<int>(SessionController::Type::Grf));
    checkInt(QStringLiteral("S7 a different type is refused, the first still owns it"),
             typeOf(s), static_cast<int>(SessionController::Type::Wrist));
    checkInt(QStringLiteral("S8 and a refused start announces nothing"), started.count(), 1);

    // ⭐ A second start of the SAME type. Nothing new began — the running
    // session already owns the clock — so it must not be announced as a new
    // session. main.cpp's preflight runs off this signal; a second one runs the
    // preflight again against a session that is already under way.
    s.start(static_cast<int>(SessionController::Type::Wrist));
    checkInt(QStringLiteral("S9 re-starting a running session announces nothing new"),
             started.count(), 1);
    checkInt(QStringLiteral("S10 and does not re-announce running"), runChanged.count(), 1);
}

// ---------------------------------------------------------------------------
// C — the clock measures the session
// ---------------------------------------------------------------------------

static void testClock()
{
    std::printf("\nC — the clock measures one session, from its start\n");

    SessionController s;
    s.start(static_cast<int>(SessionController::Type::Swing));
    const bool advanced = spinUntil(
        [&] { return s.elapsedLabel() != QStringLiteral("00:00:00"); }, 3000);
    check(QStringLiteral("C1 the clock advances (reached \"%1\")").arg(s.elapsedLabel()),
          advanced);
    const QString afterOneSecond = s.elapsedLabel();

    // ⭐ The golfer presses Capture again mid-session (or the toolbar and the
    // wizard both reach this). The session did not restart, so its duration did
    // not either — the number in the toolbar is how long they have been hitting.
    // Read immediately: a restart shows up at once, because start() re-ticks.
    s.start(static_cast<int>(SessionController::Type::Swing));
    checkStr(QStringLiteral("C2 re-starting a running session does not reset the clock"),
             s.elapsedLabel(), afterOneSecond);

    // stop() is documented as "freeze the clock (type untouched)".
    s.stop();
    const QString frozen = s.elapsedLabel();
    check(QStringLiteral("C3 stop() stops the clock"), !s.running());
    checkInt(QStringLiteral("C4 stop() leaves the type alone"), typeOf(s),
             static_cast<int>(SessionController::Type::Swing));
    spin(1200);
    checkStr(QStringLiteral("C5 a stopped clock does not advance"), s.elapsedLabel(), frozen);

    // reset() is documented as "back to 00:00:00, stopped".
    s.reset();
    checkStr(QStringLiteral("C6 reset() zeroes the clock"), s.elapsedLabel(),
             QStringLiteral("00:00:00"));
    check(QStringLiteral("C7 reset() leaves it stopped"), !s.running());
}

// ---------------------------------------------------------------------------
// T — a session type is one of four things
// ---------------------------------------------------------------------------
//
// The type is an index into the QML session-type list, it selects the session
// folder's type token (ShotProcessor::beginSessionFolder -> sessionTypeLabel),
// and it gates navigation. A value outside the enum is none of those things.

static void testTypeValidation()
{
    std::printf("\nT — the active session type is always a real type\n");

    auto isKnown = [](int t) {
        return t == static_cast<int>(SessionController::Type::None)  ||
               t == static_cast<int>(SessionController::Type::Swing) ||
               t == static_cast<int>(SessionController::Type::Wrist) ||
               t == static_cast<int>(SessionController::Type::Grf)   ||
               t == static_cast<int>(SessionController::Type::Coach);
    };

    for (int bad : { 99, -7, 4, 1000 }) {
        SessionController s;
        QSignalSpy started(&s, &SessionController::sessionStarted);
        s.start(bad);
        check(QStringLiteral("T start(%1) leaves a valid active type (got %2)")
                  .arg(bad).arg(typeOf(s)),
              isKnown(typeOf(s)));
        check(QStringLiteral("T start(%1) does not announce a session of an unknown type")
                  .arg(bad),
              started.count() == 0 || isKnown(started.at(0).at(0).toInt()));
    }

    // Every real type is accepted.
    for (auto t : { SessionController::Type::Swing, SessionController::Type::Wrist,
                    SessionController::Type::Grf,   SessionController::Type::Coach }) {
        SessionController s;
        s.start(static_cast<int>(t));
        checkInt(QStringLiteral("T a real session type is accepted"), typeOf(s),
                 static_cast<int>(t));
    }
}

// ---------------------------------------------------------------------------
// E — ending a session
// ---------------------------------------------------------------------------

static void testEndSession()
{
    std::printf("\nE — ending a session releases everything it held\n");

    SessionController s;
    s.start(static_cast<int>(SessionController::Type::Coach));
    s.setActiveClub(QStringLiteral("Driver"));

    s.endSession();
    check(QStringLiteral("E1 the clock stops"), !s.running());
    checkInt(QStringLiteral("E2 no type is active — navigation unlocks"), typeOf(s),
             static_cast<int>(SessionController::Type::None));
    check(QStringLiteral("E3 the club is released for the next session"),
          s.activeClub().isEmpty());

    // A second endSession must be harmless and silent.
    QSignalSpy runChanged(&s, &SessionController::runningChanged);
    QSignalSpy typeChanged(&s, &SessionController::activeSessionTypeChanged);
    s.endSession();
    checkInt(QStringLiteral("E4 ending twice announces nothing"),
             runChanged.count() + typeChanged.count(), 0);

    // And a session can be started again afterwards.
    s.start(static_cast<int>(SessionController::Type::Swing));
    check(QStringLiteral("E5 a new session can begin after the last one ended"), s.running());
    checkInt(QStringLiteral("E6 with its own type"), typeOf(s),
             static_cast<int>(SessionController::Type::Swing));
}

// ---------------------------------------------------------------------------
// K — the club in play
// ---------------------------------------------------------------------------

static void testActiveClub()
{
    std::printf("\nK — the club in play\n");

    // The stated rule: "Seed the club from the athlete's preferred club unless
    // one was already set for this session (e.g. the Home CLUB chip picked a
    // specific club)." With no athlete wired up, an explicit choice must simply
    // survive the start.
    SessionController s;
    QSignalSpy clubChanged(&s, &SessionController::activeClubChanged);
    s.setActiveClub(QStringLiteral("5-iron"));
    checkInt(QStringLiteral("K1 setting the club announces it"), clubChanged.count(), 1);
    s.start(static_cast<int>(SessionController::Type::Swing));
    checkStr(QStringLiteral("K2 starting a session keeps the chosen club"), s.activeClub(),
             QStringLiteral("5-iron"));

    // Setting the same club again is not a change.
    const int before = clubChanged.count();
    s.setActiveClub(QStringLiteral("5-iron"));
    checkInt(QStringLiteral("K3 setting the same club announces nothing"),
             clubChanged.count(), before);

    // Changing club mid-session is allowed — the golfer swaps clubs.
    s.setActiveClub(QStringLiteral("Driver"));
    checkStr(QStringLiteral("K4 the club can change mid-session"), s.activeClub(),
             QStringLiteral("Driver"));
}

// ---------------------------------------------------------------------------
// D — the one default session type
// ---------------------------------------------------------------------------

static void testDefaultType()
{
    std::printf("\nD — every session starts as the one default type\n");

    // The wizard and the toolbar's Capture button both start defaultType; if it
    // moved, every new session folder would change its _<Type>_ token.
    SessionController s;
    checkInt(QStringLiteral("D1 the default type is Wrist"), s.defaultType(),
             static_cast<int>(SessionController::Type::Wrist));

    QSignalSpy started(&s, &SessionController::sessionStarted);
    s.start(s.defaultType());
    check(QStringLiteral("D2 the default type is a type start() accepts"), s.running());
    checkInt(QStringLiteral("D3 and it is the type the session announces"),
             started.count() == 1 ? started.at(0).at(0).toInt() : -99,
             static_cast<int>(SessionController::Type::Wrist));
}

// ---------------------------------------------------------------------------
// F — the session's focus
// ---------------------------------------------------------------------------

static void testFocus()
{
    std::printf("\nF — the focus is set before the session and held for it\n");

    SessionController s;
    QSignalSpy focusChanged(&s, &SessionController::focusChanged);

    // Whoever starts the session sets the focus first, then calls start().
    s.setFocusConditionId(QStringLiteral("early_extension"));
    checkInt(QStringLiteral("F1 setting the focus announces it"), focusChanged.count(), 1);
    checkStr(QStringLiteral("F2 the focus can be set before the session starts"),
             s.focusConditionId(), QStringLiteral("early_extension"));
    s.setFocusFromShot(0);
    checkInt(QStringLiteral("F3 setting the same split announces nothing"),
             focusChanged.count(), 1);

    s.start(s.defaultType());
    checkStr(QStringLiteral("F4 starting the session keeps the focus"), s.focusConditionId(),
             QStringLiteral("early_extension"));
    checkInt(QStringLiteral("F5 and its split"), s.focusFromShot(), 0);
    checkInt(QStringLiteral("F6 start() announces no focus change"), focusChanged.count(), 1);

    // Mid-session the golfer changes focus from shot 7 (the diagnostics panel writes back).
    s.setFocusConditionId(QStringLiteral("trail_knee_straighten"));
    s.setFocusFromShot(7);
    checkStr(QStringLiteral("F7 the focus can change mid-session"), s.focusConditionId(),
             QStringLiteral("trail_knee_straighten"));
    checkInt(QStringLiteral("F8 from a later shot"), s.focusFromShot(), 7);
    checkInt(QStringLiteral("F9 each change is announced"), focusChanged.count(), 3);

    const int before = focusChanged.count();
    s.setFocusConditionId(QStringLiteral("trail_knee_straighten"));
    checkInt(QStringLiteral("F10 setting the same focus announces nothing"),
             focusChanged.count(), before);

    s.endSession();
    check(QStringLiteral("F11 ending the session clears the focus"),
          s.focusConditionId().isEmpty());
    checkInt(QStringLiteral("F12 and its split"), s.focusFromShot(), 0);
    check(QStringLiteral("F13 and announces it"), focusChanged.count() > before);

    // ⭐ The next session must not inherit the last one's focus: the toolbar's
    // Capture button starts a session without setting one.
    s.start(s.defaultType());
    check(QStringLiteral("F14 a session started with no focus is an open assessment"),
          s.focusConditionId().isEmpty());

    // Ending with no focus held is silent.
    s.endSession();
    QSignalSpy quiet(&s, &SessionController::focusChanged);
    s.endSession();
    checkInt(QStringLiteral("F15 clearing an empty focus announces nothing"), quiet.count(), 0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("session_clock_test — the session clock and the active session type\n");
    testIdleState();
    testSingleActiveSession();
    testClock();
    testTypeValidation();
    testEndSession();
    testActiveClub();
    testDefaultType();
    testFocus();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
