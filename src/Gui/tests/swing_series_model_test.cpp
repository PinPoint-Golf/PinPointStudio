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
static void checkEqS(const char *label, const QString &got, const QString &want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-32s got %-8s want %-8s\n", ok ? "PASS" : "FAIL", label,
                got.toUtf8().constData(), want.toUtf8().constData());
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

    // ── Ball lane: x/y/r value columns + one shared conf; holes past last sample ──
    std::printf("\n=== SwingSeriesModel ball columns ===\n\n");
    {
        SwingSeries ball;
        ball.kind            = SwingSeries::Ball;
        ball.header          = QStringLiteral("Ball");
        ball.colorKey        = QStringLiteral("green");
        ball.t               = { 0, 10000, 20000 };
        ball.value           = { 0.500, 0.510, 0.520 };   // x
        ball.ballY           = { 0.800, 0.800, 0.800 };   // y
        ball.ballR           = { 0.007, 0.007, 0.006 };   // radius
        ball.conf            = { 1.00, 1.00, 0.90 };
        ball.nominalPeriodUs = 10000;
        ball.medianDeltaUs   = 10000;

        // A metric with a later sample so the row grid reaches 30 ms — the ball has no
        // sample there, so its columns must render as holes (the ball vanished at launch).
        SwingSeries met;
        met.kind            = SwingSeries::Metric;
        met.header          = QStringLiteral("X");
        met.colorKey        = QStringLiteral("amber");
        met.t               = { 0, 10000, 20000, 30000 };
        met.value           = { 1.0, 2.0, 3.0, 4.0 };
        met.nominalPeriodUs = 10000;
        met.medianDeltaUs   = 10000;

        QVector<SwingSeries> series{ ball, met };
        SwingSeriesModel bm;
        bm.configure(series, /*windowStartUs*/ 0, /*windowEndUs*/ 30000, QStringLiteral("off"));

        // Columns: Time, Ball x, Ball y, Ball r, conf, X → 6 (no primary IMU ⇒ no Δt/state).
        checkEqI("columnCount", bm.columnCount(), 6);
        auto hdr = [&](int c) { return bm.headerData(c, Qt::Horizontal, Qt::DisplayRole).toString(); };
        checkEqS("col1 header", hdr(1), QStringLiteral("Ball x"));
        checkEqS("col2 header", hdr(2), QStringLiteral("Ball y"));
        checkEqS("col3 header", hdr(3), QStringLiteral("Ball r"));
        checkEqS("col4 header", hdr(4), QStringLiteral("conf"));

        auto cell    = [&](int r, int c) { return bm.data(bm.index(r, c), Qt::DisplayRole).toString(); };
        auto stateOf = [&](int r, int c) { return bm.data(bm.index(r, c), SwingSeriesModel::StateRole).toInt(); };

        std::printf("-- row 1 (t=10 ms): 3-decimal channels --\n");
        checkEqS("r1 Ball x", cell(1, 1), QStringLiteral("0.510"));
        checkEqS("r1 Ball y", cell(1, 2), QStringLiteral("0.800"));
        checkEqS("r1 Ball r", cell(1, 3), QStringLiteral("0.007"));
        checkEqS("r1 conf",   cell(1, 4), QStringLiteral("1.00"));

        std::printf("-- row 3 (t=30 ms): ball gone → holes, metric present --\n");
        checkEqS("r3 Ball x hole",      cell(3, 1), QStringLiteral("—"));
        checkEqI("r3 Ball x = Missing", stateOf(3, 1), SwingSeriesModel::Missing);
        checkEqS("r3 X present",        cell(3, 5), QStringLiteral("4.0"));
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
