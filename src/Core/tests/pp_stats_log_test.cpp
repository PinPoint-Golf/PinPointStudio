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

#include "PpStatsLog.h"

#include <gtest/gtest.h>

TEST(PpStatsLog, AppendFetchSinceAndCategory)
{
    auto *log = PpStatsLog::instance();
    int seq = log->currentSeq();

    log->append(QStringLiteral("GAUGE"), QStringLiteral("RSS 10 MB"));
    log->append(QStringLiteral("SCOPE"), QStringLiteral("Pose.run n=3"));

    const auto fresh = log->fetchSince(seq);
    ASSERT_EQ(fresh.size(), 2);
    EXPECT_EQ(fresh[0].category, QStringLiteral("GAUGE"));
    EXPECT_EQ(fresh[1].category, QStringLiteral("SCOPE"));
    EXPECT_TRUE(fresh[1].message.contains(QStringLiteral("Pose.run")));
    EXPECT_FALSE(fresh[0].timestamp.isEmpty());

    // seq advanced → a second fetch with the updated cursor returns nothing new.
    EXPECT_TRUE(log->fetchSince(seq).isEmpty());
}

TEST(PpStatsLog, ClearDropsEntriesButKeepsSeqMonotonic)
{
    auto *log = PpStatsLog::instance();
    log->append(QStringLiteral("MEM"), QStringLiteral("ONNX.Pose cur=1 MB"));
    const int before = log->currentSeq();

    log->clear();
    int seq = -1;
    EXPECT_TRUE(log->fetchSince(seq).isEmpty());   // nothing retained

    log->append(QStringLiteral("MEM"), QStringLiteral("again"));
    EXPECT_GT(log->currentSeq(), before);          // sequence kept climbing
}

TEST(PpStatsLog, RingCaps)
{
    auto *log = PpStatsLog::instance();
    log->clear();

    const int over = PpStatsLog::kMaxEntries + 50;
    for (int i = 0; i < over; ++i)
        log->append(QStringLiteral("THREAD"), QStringLiteral("t%1").arg(i));

    int seq = -1;
    const auto all = log->fetchSince(seq);
    EXPECT_LE(all.size(), PpStatsLog::kMaxEntries);
    // Oldest were dropped: the last retained entry is the most recent append.
    ASSERT_FALSE(all.isEmpty());
    EXPECT_EQ(all.last().message, QStringLiteral("t%1").arg(over - 1));
}
