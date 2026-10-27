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

// NavigationController — screen history and the session lock.
//
// ⚠ THE ORACLE IS THE LOCK RULE OF THE FOUR-MODES REFACTOR
// (docs/design/ux_refactor_away_from_4_modes.md §6 Phase 1): while a session
// runs, ONLY AppScreen::Session, System and Settings are reachable, whatever
// the session type, and on every path — navigate, back, forward and the Home
// reset. Settings stays open because it is how the Swing model (Settings slot
// 10) is reached mid-session; there is no Swing model screen (Mark, 28 Oct).
// Before the refactor the allowed screen was "type + 1"; a screen index
// computed from a session type is exactly what this suite exists to catch
// coming back.
//
// A real SessionController drives the lock (no athlete: the club seed is
// stubbed by session_clock_stubs.cpp, as session_clock_test does).

#include "shell/app_screen.h"
#include "shell/navigation_controller.h"
#include "session/session_controller.h"

#include <QCoreApplication>
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

static const char *nameOf(int screen)
{
    switch (screen) {
    case AppScreen::Home:       return "Home";
    case AppScreen::Session:    return "Session";
    case AppScreen::Sessions:   return "Sessions";
    case AppScreen::WorkOns:    return "WorkOns";
    case AppScreen::Athlete:    return "Athlete";
    case AppScreen::Athletes:   return "Athletes";
    case AppScreen::System:     return "System";
    case AppScreen::Settings:   return "Settings";
    case AppScreen::Developer:  return "Developer";
    case AppScreen::Wizard:     return "Wizard";
    }
    return "?";
}

static void checkScreen(const QString &label, int got, int want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label, QLatin1String(nameOf(got)),
                                                       QLatin1String(nameOf(want))),
          got == want);
}

static void startSession(SessionController &s, SessionController::Type type)
{
    s.start(static_cast<int>(type));
}

// ---------------------------------------------------------------------------
// H — history with no session
// ---------------------------------------------------------------------------

static void testHistory()
{
    std::printf("\nH — back and forward, no session\n");

    SessionController   session;
    NavigationController nav(nullptr, &session);

    checkScreen(QStringLiteral("H1 a fresh controller is on Home"), nav.currentIndex(),
                AppScreen::Home);
    check(QStringLiteral("H2 a fresh controller has no history"),
          !nav.canGoBack() && !nav.canGoForward());
    check(QStringLiteral("H3 no session, no lock"), !nav.sessionLocked());

    QSignalSpy indexChanged(&nav, &NavigationController::currentIndexChanged);
    nav.navigate(AppScreen::Sessions);
    checkScreen(QStringLiteral("H4 navigate moves to the screen"), nav.currentIndex(),
                AppScreen::Sessions);
    check(QStringLiteral("H5 navigate announces the move"), indexChanged.count() == 1);
    check(QStringLiteral("H6 navigate pushes the old screen"), nav.canGoBack());

    nav.navigate(AppScreen::WorkOns);
    nav.back();
    checkScreen(QStringLiteral("H7 back returns to the previous screen"), nav.currentIndex(),
                AppScreen::Sessions);
    check(QStringLiteral("H8 back makes forward available"), nav.canGoForward());
    nav.forward();
    checkScreen(QStringLiteral("H9 forward replays it"), nav.currentIndex(), AppScreen::WorkOns);

    nav.back();
    nav.navigate(AppScreen::Athletes);
    check(QStringLiteral("H10 a fresh navigate drops the forward history"), !nav.canGoForward());

    const int before = indexChanged.count();
    nav.navigate(AppScreen::Athletes);
    check(QStringLiteral("H11 navigating to the current screen is not a move"),
          indexChanged.count() == before);

    // Every screen is reachable when no session runs.
    bool all = true;
    for (int i = 0; i < AppScreen::Count; ++i) {
        nav.navigate(i);
        all = all && nav.currentIndex() == i;
    }
    check(QStringLiteral("H12 every screen is reachable with no session"), all);
}

// ---------------------------------------------------------------------------
// R — navigateRail(Home) is the history-clearing reset
// ---------------------------------------------------------------------------

static void testHomeReset()
{
    std::printf("\nR — the Home rail button clears history\n");

    SessionController   session;
    NavigationController nav(nullptr, &session);

    nav.navigateRail(AppScreen::Sessions);
    nav.navigateRail(AppScreen::Settings);
    nav.back();
    check(QStringLiteral("R1 (setup) there is back and forward history"),
          nav.canGoBack() && nav.canGoForward());

    nav.navigateRail(AppScreen::Home);
    checkScreen(QStringLiteral("R2 navigateRail(Home) lands on Home"), nav.currentIndex(),
                AppScreen::Home);
    check(QStringLiteral("R3 navigateRail(Home) clears both histories"),
          !nav.canGoBack() && !nav.canGoForward());

    // A rail press on any other screen extends history rather than resetting it.
    nav.navigateRail(AppScreen::WorkOns);
    check(QStringLiteral("R4 any other rail press pushes"), nav.canGoBack());

    // Already on Home with history behind it: the reset still clears it, and says so.
    nav.navigate(AppScreen::Home);
    QSignalSpy historyChanged(&nav, &NavigationController::historyChanged);
    nav.navigateRail(AppScreen::Home);
    check(QStringLiteral("R5 resetting from Home clears its history"), !nav.canGoBack());
    check(QStringLiteral("R6 ... and announces the change"), historyChanged.count() >= 1);
}

// ---------------------------------------------------------------------------
// L — the session lock
// ---------------------------------------------------------------------------

static void testSessionLock(SessionController::Type type, const char *typeName)
{
    std::printf("\nL — the session lock, %s session\n", typeName);

    SessionController   session;
    NavigationController nav(nullptr, &session);
    QSignalSpy lockChanged(&nav, &NavigationController::sessionLockedChanged);

    // Build some history first: Home → Sessions → Session (as the wizard does,
    // navigating to the Session screen BEFORE start()).
    nav.navigate(AppScreen::Sessions);
    nav.navigate(AppScreen::Session);

    startSession(session, type);
    check(QStringLiteral("L1 (setup) the session really started"), session.running());
    check(QStringLiteral("L2 a running session locks navigation"), nav.sessionLocked());
    check(QStringLiteral("L3 the lock is announced"), lockChanged.count() >= 1);

    // Refused targets: the current screen must not move.
    const int refused[] = { AppScreen::Home, AppScreen::Sessions, AppScreen::WorkOns,
                            AppScreen::Athletes, AppScreen::Athlete, AppScreen::Wizard,
                            AppScreen::Developer };
    for (int target : refused) {
        nav.navigate(target);
        checkScreen(QStringLiteral("L4 navigate(%1) is refused").arg(QLatin1String(nameOf(target))),
                    nav.currentIndex(), AppScreen::Session);
    }
    nav.navigateRail(AppScreen::Home);
    checkScreen(QStringLiteral("L5 navigateRail(Home) is refused"), nav.currentIndex(),
                AppScreen::Session);
    check(QStringLiteral("L6 ... and the refused reset keeps history"), nav.canGoBack());
    nav.navigateHome();
    checkScreen(QStringLiteral("L7 navigateHome() is refused"), nav.currentIndex(),
                AppScreen::Session);

    // Allowed targets.
    const int allowed[] = { AppScreen::System, AppScreen::Settings, AppScreen::Session };
    for (int target : allowed) {
        nav.navigate(target);
        checkScreen(QStringLiteral("L8 navigate(%1) is allowed").arg(QLatin1String(nameOf(target))),
                    nav.currentIndex(), target);
    }

    // History now ends ... Sessions, Session, System, Settings, [Session].
    // Walk back through the allowed screens, then hit Sessions, which is refused.
    nav.back(); checkScreen(QStringLiteral("L9 back to Settings"),  nav.currentIndex(), AppScreen::Settings);
    nav.back(); checkScreen(QStringLiteral("L10 back to System"),   nav.currentIndex(), AppScreen::System);
    nav.back(); checkScreen(QStringLiteral("L11 back to Session"),  nav.currentIndex(), AppScreen::Session);

    nav.back();
    checkScreen(QStringLiteral("L12 back() to Sessions is refused"), nav.currentIndex(),
                AppScreen::Session);
    check(QStringLiteral("L13 ... without losing the back history"), nav.canGoBack());
    check(QStringLiteral("L14 ... or the forward history"), nav.canGoForward());

    nav.forward();
    checkScreen(QStringLiteral("L15 forward to an allowed screen works"), nav.currentIndex(),
                AppScreen::System);
    nav.back();

    // The session ends: everything is reachable again, and the history the lock
    // protected is still there.
    session.endSession();
    check(QStringLiteral("L16 endSession() lifts the lock"), !nav.sessionLocked());
    nav.back();
    checkScreen(QStringLiteral("L17 back() to Sessions now works"), nav.currentIndex(),
                AppScreen::Sessions);
    bool all = true;
    for (int i = 0; i < AppScreen::Count; ++i) {
        nav.navigate(i);
        all = all && nav.currentIndex() == i;
    }
    check(QStringLiteral("L18 every screen is reachable after the session"), all);
    nav.navigateRail(AppScreen::Home);
    check(QStringLiteral("L19 navigateRail(Home) resets again after the session"),
          nav.currentIndex() == AppScreen::Home && !nav.canGoBack() && !nav.canGoForward());
}

// A stopped clock is not a session: stop() keeps the type but the lock follows running().
static void testStoppedClock()
{
    std::printf("\nP — a stopped clock\n");

    SessionController   session;
    NavigationController nav(nullptr, &session);
    startSession(session, SessionController::Type::Wrist);
    check(QStringLiteral("P1 (setup) locked while running"), nav.sessionLocked());
    session.stop();
    check(QStringLiteral("P2 a stopped clock unlocks"), !nav.sessionLocked());
    nav.navigate(AppScreen::Home);
    checkScreen(QStringLiteral("P3 Home is reachable again"), nav.currentIndex(), AppScreen::Home);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("navigation_controller_test — screen history and the session lock\n");
    testHistory();
    testHomeReset();
    // The lock must not depend on the session type: run it for every type.
    testSessionLock(SessionController::Type::Swing, "Swing");
    testSessionLock(SessionController::Type::Wrist, "Wrist");
    testSessionLock(SessionController::Type::Grf,   "GRF");
    testSessionLock(SessionController::Type::Coach, "Coach");
    testStoppedClock();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
