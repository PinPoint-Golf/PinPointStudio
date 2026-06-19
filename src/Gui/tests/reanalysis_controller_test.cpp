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

// ReanalysisController seam — the single entry point the carousel forwards to.
// Asserts that reanalyse() emits reanalyseQueued with the right count for the
// focused-shot path ([id]) and for a wider id list (the funnel is scope-agnostic
// even though v1's bar only feeds it the focused shot), and that an empty list
// is a no-op. reanalysing stays false in this stubbed phase.

#include "shot/reanalysis_controller.h"

#include <QObject>
#include <QVariantList>
#include <cstdio>

static int g_fail = 0;
static void checkInt(const char *label, long long got, long long want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-44s got %4lld  want %4lld\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}
static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-44s got %-5s want %-5s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

int main()
{
    std::printf("=== ReanalysisController seam ===\n\n");

    ReanalysisController ctrl;

    int  emissions = 0;
    int  lastCount = -1;
    QObject::connect(&ctrl, &ReanalysisController::reanalyseQueued,
                     [&](int count) { ++emissions; lastCount = count; });

    std::printf("-- focused-shot path: reanalyse([42]) --\n");
    ctrl.reanalyse(QVariantList{ 42 });
    checkInt ("emissions",     emissions, 1);
    checkInt ("queued count",  lastCount, 1);
    checkBool("reanalysing stays false", ctrl.reanalysing(), false);

    std::printf("\n-- scope-agnostic funnel: reanalyse([7,8,9]) --\n");
    ctrl.reanalyse(QVariantList{ 7, 8, 9 });
    checkInt ("emissions",    emissions, 2);
    checkInt ("queued count", lastCount, 3);

    std::printf("\n-- empty list is a no-op --\n");
    ctrl.reanalyse(QVariantList{});
    checkInt ("emissions unchanged", emissions, 2);

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
