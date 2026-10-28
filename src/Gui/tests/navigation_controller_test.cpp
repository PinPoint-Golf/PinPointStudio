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

// NavigationController — screen history.
//
// There is no session lock (Mark, 28 Oct): a running session leaves every screen reachable and the
// rail's Live is the way back to it, so nothing here depends on a session.

#include "shell/app_screen.h"
#include "shell/navigation_controller.h"

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

// ---------------------------------------------------------------------------
// H — history
// ---------------------------------------------------------------------------

static void testHistory()
{
    std::printf("\nH — back and forward\n");

    NavigationController nav;

    checkScreen(QStringLiteral("H1 a fresh controller is on Home"), nav.currentIndex(),
                AppScreen::Home);
    check(QStringLiteral("H2 a fresh controller has no history"),
          !nav.canGoBack() && !nav.canGoForward());
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

    // Every screen is reachable.
    bool all = true;
    for (int i = 0; i < AppScreen::Count; ++i) {
        nav.navigate(i);
        all = all && nav.currentIndex() == i;
    }
    check(QStringLiteral("H12 every screen is reachable"), all);
}

// ---------------------------------------------------------------------------
// R — navigateRail(Home) is the history-clearing reset
// ---------------------------------------------------------------------------

static void testHomeReset()
{
    std::printf("\nR — the Home rail button clears history\n");

    NavigationController nav;

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("navigation_controller_test — screen history\n");
    testHistory();
    testHomeReset();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
