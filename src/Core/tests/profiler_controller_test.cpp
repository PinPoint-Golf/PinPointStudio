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

#include "profiler_controller.h"
#include "pp_profiler.h"
#include "PpStatsLog.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>

using pinpoint::profiling::Profiler;

namespace {

QVariantMap rowNamed(const QVariantList &rows, const QString &name)
{
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() == name)
            return m;
    }
    return {};
}

} // namespace

TEST(ProfilerController, RefreshPopulatesFormattedRows)
{
    Profiler::instance().reset();
    for (int i = 0; i < 3; ++i) { PP_PROFILE_SCOPE("PC.scope"); }
    PP_PROFILE_MEM_ADD("PC.mem", 2048);

    ProfilerController c;
    c.refresh();

    const QVariantMap scope = rowNamed(c.scopes(), QStringLiteral("PC.scope"));
    ASSERT_FALSE(scope.isEmpty());
    EXPECT_EQ(scope.value(QStringLiteral("calls")).toULongLong(), 3u);
    EXPECT_FALSE(scope.value(QStringLiteral("callsStr")).toString().isEmpty());
    EXPECT_FALSE(scope.value(QStringLiteral("totalStr")).toString().isEmpty());

    const QVariantMap mem = rowNamed(c.memory(), QStringLiteral("PC.mem"));
    ASSERT_FALSE(mem.isEmpty());
    EXPECT_EQ(mem.value(QStringLiteral("currentBytes")).toLongLong(), 2048);
    EXPECT_EQ(mem.value(QStringLiteral("currentStr")).toString(), QStringLiteral("2 KB"));
}

TEST(ProfilerController, DeepEnabledRoundTrips)
{
    ProfilerController c;
    EXPECT_TRUE(c.available());
    EXPECT_TRUE(c.deepAvailable());     // built with PINPOINT_PROFILE=1

    Profiler::setDeepEnabled(false);
    EXPECT_FALSE(c.deepEnabled());
    c.setDeepEnabled(true);
    EXPECT_TRUE(c.deepEnabled());
    EXPECT_TRUE(Profiler::deepEnabled());
    c.setDeepEnabled(false);
    EXPECT_FALSE(Profiler::deepEnabled());
}

TEST(ProfilerController, DumpToLogGrowsStatsRing)
{
    Profiler::instance().reset();
    PP_PROFILE_MEM_ADD("DL.mem", 4096);

    int seq = PpStatsLog::instance()->currentSeq();

    ProfilerController c;
    c.dumpToLog();

    const auto entries = PpStatsLog::instance()->fetchSince(seq);
    EXPECT_GT(entries.size(), 0);
    bool foundMem = false;
    for (const auto &e : entries)
        if (e.category == QStringLiteral("MEM")
            && e.message.contains(QStringLiteral("DL.mem")))
            foundMem = true;
    EXPECT_TRUE(foundMem);

    // The controller surfaces the same ring (newest-first), unfiltered by default.
    EXPECT_FALSE(c.statsHistory().isEmpty());
}

TEST(ProfilerController, StatsCategoryFilter)
{
    ProfilerController c;
    c.dumpToLog();                      // produces GAUGE/SCOPE/MEM/THREAD lines
    c.refresh();
    const int withAll = c.statsHistory().size();
    ASSERT_GT(withAll, 0);

    // Drop the GAUGE category → fewer (or equal) rows, none of them GAUGE.
    c.toggleStatsCategory(QStringLiteral("GAUGE"));
    const QVariantList filtered = c.statsHistory();
    EXPECT_LE(filtered.size(), withAll);
    for (const QVariant &v : filtered)
        EXPECT_NE(v.toMap().value(QStringLiteral("category")).toString(),
                  QStringLiteral("GAUGE"));
    c.toggleStatsCategory(QStringLiteral("GAUGE"));   // restore
}

TEST(ProfilerController, RefreshReadsCachedGaugeWithoutResampling)
{
    ProfilerController c;                // ctor seeds the gauge once
    const quint64 rss0 = c.rssBytes();
    const double  cpu0 = c.cpuPercent();
    EXPECT_GT(rss0, 0u);

    for (int i = 0; i < 5; ++i) c.refresh();

    // refresh() must read the cached gauge, never re-sample osmetrics — so with
    // no sampler tick in between (no event loop running) the values are identical.
    EXPECT_EQ(c.rssBytes(), rss0);
    EXPECT_DOUBLE_EQ(c.cpuPercent(), cpu0);
}

// THE [GpuMem] LINE (refactor 3.7-I): the cabin run's reader parses it, so its format is pinned here.
TEST(ProfilerController, GpuMemLineDxgiAllFields)
{
    pinpoint::gpumetrics::GpuSample g;
    g.available = true;
    g.backend = "DXGI";
    g.processUsedBytes   = 1712ull * 1048576ull;
    g.processSharedBytes = 128ull * 1048576ull + 400000ull;   // rounds down
    g.peakProcessBytes   = 1800ull * 1048576ull + 600000ull;  // rounds up
    pinpoint::osmetrics::ProcessMemory m;
    m.private_bytes     = 4450ull * 1048576ull;
    m.working_set_bytes = 3900ull * 1048576ull;
    EXPECT_EQ(ProfilerController::gpuMemLine(QStringLiteral("tick"), g, m),
              QStringLiteral("[GpuMem] at=tick dedicated_mb=1712 shared_mb=128 peak_dedicated_mb=1801 private_mb=4450 "
                             "working_set_mb=3900 source=DXGI"));
}

TEST(ProfilerController, GpuMemLineMetalUnifiedHasNoShared)
{
    pinpoint::gpumetrics::GpuSample g;
    g.available = true;
    g.unified = true;
    g.backend = "Metal";
    g.processUsedBytes   = 2ull * 1048576ull;
    g.processSharedBytes = 99ull * 1048576ull;   // ignored: only DXGI has a shared segment
    g.peakProcessBytes   = 2ull * 1048576ull;
    pinpoint::osmetrics::ProcessMemory m;
    m.private_bytes     = 1180ull * 1048576ull;
    m.working_set_bytes = 1296ull * 1048576ull;
    EXPECT_EQ(ProfilerController::gpuMemLine(QStringLiteral("EVT-01 shot=abc/3"), g, m),
              QStringLiteral("[GpuMem] at=EVT-01 shot=abc/3 dedicated_mb=2 shared_mb=- peak_dedicated_mb=2 "
                             "private_mb=1180 working_set_mb=1296 source=Metal(unified)"));
}

TEST(ProfilerController, GpuMemLineUnavailableIsDashesNeverZero)
{
    const pinpoint::gpumetrics::GpuSample g;      // available = false
    const pinpoint::osmetrics::ProcessMemory m;   // nothing known
    EXPECT_EQ(ProfilerController::gpuMemLine(QStringLiteral("session-start"), g, m),
              QStringLiteral("[GpuMem] at=session-start dedicated_mb=- shared_mb=- peak_dedicated_mb=- private_mb=- "
                             "working_set_mb=- source=none"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // ProfilerController owns a QTimer
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
