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

// SwingSeriesModel::rowForTimeUs — the playhead→row map that backs the table
// cross-highlight. A single Metric series at 10 ms cadence over [0, 30 ms];
// asserts nearest-within-window, exact hits, tie→earlier, and -1 outside.

#include "swing_series_model.h"

#include <QVector>
#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static void checkEqI(const char *label, long long got, long long want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-32s got %4lld  want %4lld\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}

int main()
{
    std::printf("=== SwingSeriesModel::rowForTimeUs ===\n\n");

    // Empty (never configured) → always -1.
    {
        SwingSeriesModel empty;
        checkEqI("empty -> -1", empty.rowForTimeUs(0), -1);
    }

    // Rows at 0,10000,20000,30000 (dedupTol = medianDeltaUs/2 = 5000 keeps all four).
    SwingSeries s;
    s.kind          = SwingSeries::Metric;
    s.header        = QStringLiteral("X");
    s.colorKey      = QStringLiteral("teal");
    s.t             = { 0, 10000, 20000, 30000 };
    s.value         = { 1.0, 2.0, 3.0, 4.0 };
    s.medianDeltaUs = 10000;

    QVector<SwingSeries> series;
    series.append(s);

    SwingSeriesModel m;
    m.configure(series, /*windowStartUs*/ 0, /*windowEndUs*/ 30000, QStringLiteral("off"));

    std::printf("-- exact hits --\n");
    checkEqI("0      -> 0", m.rowForTimeUs(0), 0);
    checkEqI("10000  -> 1", m.rowForTimeUs(10000), 1);
    checkEqI("20000  -> 2", m.rowForTimeUs(20000), 2);
    checkEqI("30000  -> 3 (last)", m.rowForTimeUs(30000), 3);

    std::printf("-- nearest --\n");
    checkEqI("14000  -> 1 (10k nearer)", m.rowForTimeUs(14000), 1);
    checkEqI("16000  -> 2 (20k nearer)", m.rowForTimeUs(16000), 2);
    checkEqI("15000  -> 1 (tie->earlier)", m.rowForTimeUs(15000), 1);

    std::printf("-- outside window --\n");
    checkEqI("-1       -> -1 (below)", m.rowForTimeUs(-1), -1);
    checkEqI("31000    -> -1 (above)", m.rowForTimeUs(31000), -1);

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
