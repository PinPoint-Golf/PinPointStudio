/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// PlatformTarget arch-token mapping + compile-time current() resolution. The
// token table is the single place asset/feed arch strings are decided, so it is
// locked here; current() is a compile-time-constant sanity check.

#include "platform_target.h"

#include <gtest/gtest.h>

using namespace pp::update;

TEST(AssetArchToken, Linux)
{
    EXPECT_EQ((PlatformTarget{Os::Linux, Arch::X86_64}).assetArchToken(),
              QStringLiteral("x86_64"));
    EXPECT_EQ((PlatformTarget{Os::Linux, Arch::Arm64}).assetArchToken(),
              QStringLiteral("aarch64"));
}

TEST(AssetArchToken, MacOs)
{
    EXPECT_EQ((PlatformTarget{Os::MacOS, Arch::X86_64}).assetArchToken(),
              QStringLiteral("x86_64"));
    EXPECT_EQ((PlatformTarget{Os::MacOS, Arch::Arm64}).assetArchToken(),
              QStringLiteral("arm64"));
}

TEST(AssetArchToken, Windows)
{
    EXPECT_EQ((PlatformTarget{Os::Windows, Arch::X86_64}).assetArchToken(),
              QStringLiteral("x64"));
    EXPECT_EQ((PlatformTarget{Os::Windows, Arch::Arm64}).assetArchToken(),
              QStringLiteral("arm64"));
}

TEST(AssetArchToken, UnknownArchIsEmpty)
{
    EXPECT_TRUE((PlatformTarget{Os::Linux, Arch::Unknown}).assetArchToken().isEmpty());
}

TEST(Current, MatchesCompiledPlatform)
{
    const PlatformTarget t = PlatformTarget::current();

#if defined(Q_OS_LINUX)
    EXPECT_EQ(t.os, Os::Linux);
#elif defined(Q_OS_WIN)
    EXPECT_EQ(t.os, Os::Windows);
#elif defined(Q_OS_MACOS)
    EXPECT_EQ(t.os, Os::MacOS);
#endif

    // Arch is one of the known enumerators on any platform we build/test on.
    EXPECT_TRUE(t.arch == Arch::X86_64 || t.arch == Arch::Arm64 || t.arch == Arch::Unknown);

    // On a supported build the token is non-empty (x86_64/aarch64/arm64/x64).
    if (t.arch != Arch::Unknown)
        EXPECT_FALSE(t.assetArchToken().isEmpty());
}
