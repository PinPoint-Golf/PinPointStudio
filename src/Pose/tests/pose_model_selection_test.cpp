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

// Standalone test for the tier -> pose-model decision (src/Pose/pose_model_selection.h).
// Pure QString logic, no ONNX Runtime — locks the rule that ViTPose++-L runs ONLY
// when the "High" tier is selected AND the (on-demand) L model has been downloaded,
// and that every other combination falls back to ViTPose-B.

#include "../pose_model_selection.h"

#include <cstdio>

using pinpoint::pose::useVitPoseLarge;

static int g_fail = 0;

#define CHECK(label, cond)                                             \
    do {                                                               \
        const bool ok = (cond);                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);       \
        if (!ok) ++g_fail;                                             \
    } while (0)

int main()
{
    std::printf("=== pose_model_selection_test (tier -> model) ===\n");

    // High + model present -> use L.
    CHECK("High + available -> L",           useVitPoseLarge(QStringLiteral("High"),   true)  == true);

    // High but the model has not been downloaded yet -> fall back to B.
    CHECK("High + unavailable -> B",          useVitPoseLarge(QStringLiteral("High"),   false) == false);

    // Medium / Low never use L, regardless of availability.
    CHECK("Medium + available -> B",          useVitPoseLarge(QStringLiteral("Medium"), true)  == false);
    CHECK("Medium + unavailable -> B",        useVitPoseLarge(QStringLiteral("Medium"), false) == false);
    CHECK("Low + available -> B",             useVitPoseLarge(QStringLiteral("Low"),    true)  == false);

    // Case-insensitive on the tier name (defensive against stored casing drift).
    CHECK("high (lowercase) + available -> L", useVitPoseLarge(QStringLiteral("high"),   true)  == true);
    CHECK("HIGH (uppercase) + available -> L", useVitPoseLarge(QStringLiteral("HIGH"),   true)  == true);

    // Empty / unknown tiers are safe (fall back to B).
    CHECK("empty tier -> B",                  useVitPoseLarge(QString(),                 true)  == false);
    CHECK("unknown tier -> B",                useVitPoseLarge(QStringLiteral("Ultra"),   true)  == false);

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
