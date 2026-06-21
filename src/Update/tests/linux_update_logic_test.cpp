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

// Characterization + capability tests for the Linux updater's pure decision logic
// (src/Update/linux_update_logic.*). These lock the version-comparison and asset-
// selection semantics that were previously inline-and-untested inside
// UpdateController, and additionally cover the new arch-parameterised selection
// (x86_64 AND aarch64) that the refactor unlocks.

#include "linux_update_logic.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

using namespace pp::update::linux_logic;

namespace {

const QString kPin  = QStringLiteral("C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0");
const QString kZero = QStringLiteral("0000000000000000000000000000000000000000");
const QString kX86  = QStringLiteral("x86_64");
const QString kArm  = QStringLiteral("aarch64");

// Build a GitHub /releases array element. `assets` is a list of {name,url} pairs.
QJsonObject release(bool draft, const QString &body,
                    const QList<QPair<QString, QString>> &assets)
{
    QJsonArray a;
    for (const auto &as : assets) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), as.first);
        o.insert(QStringLiteral("browser_download_url"), as.second);
        a.append(o);
    }
    QJsonObject rel;
    rel.insert(QStringLiteral("draft"), draft);
    rel.insert(QStringLiteral("body"), body);
    rel.insert(QStringLiteral("assets"), a);
    return rel;
}

} // namespace

// ── compareVersion ────────────────────────────────────────────────────────────

TEST(CompareVersion, MajorMinorOrdering)
{
    EXPECT_GT(compareVersion("v0.2", "v0.1"), 0);
    EXPECT_LT(compareVersion("v0.1", "v0.2"), 0);
    EXPECT_GT(compareVersion("v1.0", "v0.9"), 0);
    EXPECT_LT(compareVersion("v0.9", "v1.0"), 0);
    EXPECT_EQ(compareVersion("v0.1", "v0.1"), 0);
}

TEST(CompareVersion, PostfixRanking)
{
    // clean > rc > beta > alpha
    EXPECT_GT(compareVersion("v0.1", "v0.1-rc1"), 0);
    EXPECT_GT(compareVersion("v0.1-rc1", "v0.1-beta2"), 0);
    EXPECT_GT(compareVersion("v0.1-beta1", "v0.1-alpha9"), 0);
    EXPECT_LT(compareVersion("v0.1-alpha1", "v0.1"), 0);
}

TEST(CompareVersion, PostfixNumbering)
{
    EXPECT_GT(compareVersion("v0.1-alpha3", "v0.1-alpha2"), 0);
    EXPECT_LT(compareVersion("v0.1-alpha2", "v0.1-alpha3"), 0);
    EXPECT_EQ(compareVersion("v0.1-alpha2", "v0.1-alpha2"), 0);
}

TEST(CompareVersion, GarbageNeverNewer)
{
    // Unparseable strings must compare 0 so the updater never prompts on garbage.
    EXPECT_EQ(compareVersion("garbage", "v0.1"), 0);
    EXPECT_EQ(compareVersion("", "v0.1"), 0);
    EXPECT_EQ(compareVersion("v0.1", "not-a-version"), 0);
}

TEST(PostfixRank, Ordering)
{
    EXPECT_GT(postfixRank(""), postfixRank("rc1"));
    EXPECT_GT(postfixRank("rc1"), postfixRank("beta1"));
    EXPECT_GT(postfixRank("beta1"), postfixRank("alpha1"));
    EXPECT_EQ(postfixRank("nonsense"), 0);
}

// ── versionFromAssetName ──────────────────────────────────────────────────────

TEST(VersionFromAssetName, X86HappyPath)
{
    EXPECT_EQ(versionFromAssetName("PinPointStudio-v0.1-alpha3-x86_64.AppImage", kX86),
              QStringLiteral("v0.1-alpha3"));
    EXPECT_EQ(versionFromAssetName("PinPointStudio-v1.2-x86_64.AppImage", kX86),
              QStringLiteral("v1.2"));
}

TEST(VersionFromAssetName, Aarch64HappyPath)
{
    EXPECT_EQ(versionFromAssetName("PinPointStudio-v0.1-aarch64.AppImage", kArm),
              QStringLiteral("v0.1"));
}

TEST(VersionFromAssetName, CrossArchIsolation)
{
    // A backend must never offer a cross-arch asset: an aarch64 name with an
    // x86_64 token (and vice versa) yields no version.
    EXPECT_TRUE(versionFromAssetName("PinPointStudio-v0.1-aarch64.AppImage", kX86).isEmpty());
    EXPECT_TRUE(versionFromAssetName("PinPointStudio-v0.1-x86_64.AppImage", kArm).isEmpty());
}

TEST(VersionFromAssetName, NonMatching)
{
    EXPECT_TRUE(versionFromAssetName("PinPointStudioSetup-v0.1-core.exe", kX86).isEmpty());
    EXPECT_TRUE(versionFromAssetName("something-else.AppImage", kX86).isEmpty());
    EXPECT_TRUE(versionFromAssetName("PinPointStudio-v0.1-x86_64.AppImage", QString()).isEmpty());
}

// ── pickLatestAppImage ────────────────────────────────────────────────────────

TEST(PickLatestAppImage, NewestWithAppImageSkipsWindowsOnly)
{
    // Newest release is Windows-only (no AppImage); the next has the x86_64 build.
    QJsonArray feed;
    feed.append(release(false, "win notes",
                        {{"PinPointStudioSetup-v0.3-core.exe", "https://x/exe"}}));
    feed.append(release(false, "linux notes",
                        {{"PinPointStudio-v0.2-x86_64.AppImage", "https://x/app"},
                         {"PinPointStudio-v0.2-x86_64.AppImage.sig", "https://x/sig"}}));

    const auto pick = pickLatestAppImage(feed, kX86);
    EXPECT_TRUE(pick.found);
    EXPECT_EQ(pick.version, QStringLiteral("v0.2"));
    EXPECT_EQ(pick.appImageName, QStringLiteral("PinPointStudio-v0.2-x86_64.AppImage"));
    EXPECT_EQ(pick.sigUrl, QStringLiteral("https://x/sig"));
    EXPECT_EQ(pick.notes, QStringLiteral("linux notes"));
}

TEST(PickLatestAppImage, DraftsSkipped)
{
    QJsonArray feed;
    feed.append(release(true, "draft",   // draft, even though it has an AppImage
                        {{"PinPointStudio-v0.9-x86_64.AppImage", "https://x/draft"}}));
    feed.append(release(false, "real",
                        {{"PinPointStudio-v0.2-x86_64.AppImage", "https://x/app"}}));

    const auto pick = pickLatestAppImage(feed, kX86);
    EXPECT_TRUE(pick.found);
    EXPECT_EQ(pick.version, QStringLiteral("v0.2"));
}

TEST(PickLatestAppImage, NoAppImageAnywhere)
{
    QJsonArray feed;
    feed.append(release(false, "win", {{"PinPointStudioSetup-v0.3-core.exe", "https://x/exe"}}));
    const auto pick = pickLatestAppImage(feed, kX86);
    EXPECT_FALSE(pick.found);
    EXPECT_TRUE(pick.version.isEmpty());
}

TEST(PickLatestAppImage, ArchAwareSelection)
{
    // A release carrying both arches offers the one matching the running target.
    QJsonArray feed;
    feed.append(release(false, "dual",
                        {{"PinPointStudio-v0.4-x86_64.AppImage", "https://x/x86"},
                         {"PinPointStudio-v0.4-x86_64.AppImage.sig", "https://x/x86sig"},
                         {"PinPointStudio-v0.4-aarch64.AppImage", "https://x/arm"},
                         {"PinPointStudio-v0.4-aarch64.AppImage.sig", "https://x/armsig"}}));

    const auto x = pickLatestAppImage(feed, kX86);
    EXPECT_TRUE(x.found);
    EXPECT_EQ(x.appImageName, QStringLiteral("PinPointStudio-v0.4-x86_64.AppImage"));
    EXPECT_EQ(x.sigUrl, QStringLiteral("https://x/x86sig"));

    const auto a = pickLatestAppImage(feed, kArm);
    EXPECT_TRUE(a.found);
    EXPECT_EQ(a.appImageName, QStringLiteral("PinPointStudio-v0.4-aarch64.AppImage"));
    EXPECT_EQ(a.sigUrl, QStringLiteral("https://x/armsig"));
}

// ── isTrustedValidSig / isPlaceholderFingerprint ──────────────────────────────

TEST(IsTrustedValidSig, MatchingPin)
{
    const QString out =
        QStringLiteral("[GNUPG:] NEWSIG\n[GNUPG:] VALIDSIG ") + kPin +
        QStringLiteral(" 2026-06-20 1718900000 0 4 0 22 8 00 ") + kPin + QStringLiteral("\n");
    EXPECT_TRUE(isTrustedValidSig(out, kPin));
}

TEST(IsTrustedValidSig, CaseInsensitive)
{
    const QString out = QStringLiteral("[GNUPG:] VALIDSIG ") + kPin.toLower() + QStringLiteral(" x");
    EXPECT_TRUE(isTrustedValidSig(out, kPin));
}

TEST(IsTrustedValidSig, WrongFingerprint)
{
    const QString other = QStringLiteral("DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEF");
    const QString out = QStringLiteral("[GNUPG:] VALIDSIG ") + other + QStringLiteral(" x");
    EXPECT_FALSE(isTrustedValidSig(out, kPin));
}

TEST(IsTrustedValidSig, NoValidSigLine)
{
    EXPECT_FALSE(isTrustedValidSig(QStringLiteral("[GNUPG:] BADSIG something"), kPin));
    EXPECT_FALSE(isTrustedValidSig(QString(), kPin));
}

TEST(IsPlaceholderFingerprint, ZerosVsReal)
{
    EXPECT_TRUE(isPlaceholderFingerprint(kZero));
    EXPECT_FALSE(isPlaceholderFingerprint(kPin));
}
