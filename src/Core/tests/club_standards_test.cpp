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

// The standard club table (club_standards.h): every club in the vocabulary but the putter's
// neighbours resolves for men and women, lengths fall and lies rise down the bag, a women's club is
// an inch shorter, and a junior's length follows their height.

#include "club_standards.h"
#include "club_vocabulary.h"

#include <gtest/gtest.h>

using namespace pinpoint;

TEST(ClubStandards, EveryVocabularyClubHasMenAndWomenFigures)
{
    for (const QString &id : clubVocabulary()) {
        const ClubStandard m = standardClub(id, ClubSet::Men), w = standardClub(id, ClubSet::Women);
        ASSERT_TRUE(m.valid) << id.toStdString();
        ASSERT_TRUE(w.valid) << id.toStdString();
        EXPECT_NEAR(m.lengthMm - w.lengthMm, 25.4, 1.0) << id.toStdString();   // one inch, each rounded
        EXPECT_DOUBLE_EQ(m.lieDeg, w.lieDeg) << id.toStdString();
    }
}

TEST(ClubStandards, MensSevenIronIsThirtySevenInches)
{
    const ClubStandard s = standardClub(QStringLiteral("7 IRON"), ClubSet::Men);
    EXPECT_EQ(s.lengthMm, 940);
    EXPECT_DOUBLE_EQ(s.lieDeg, 62.0);
    EXPECT_EQ(standardClub(QStringLiteral("driver"), ClubSet::Men).lengthMm, 1156);   // case-blind
}

TEST(ClubStandards, LengthsFallAndLiesRiseDownTheBag)
{
    int prevLen = 1 << 30;
    double prevLie = 0;
    for (const QString &id : clubVocabulary()) {
        const ClubStandard s = standardClub(id, ClubSet::Men);
        EXPECT_LE(s.lengthMm, prevLen) << id.toStdString();
        EXPECT_GE(s.lieDeg, prevLie) << id.toStdString();
        prevLen = s.lengthMm;
        prevLie = s.lieDeg;
    }
}

TEST(ClubStandards, JuniorLengthFollowsHeight)
{
    EXPECT_FALSE(standardClub(QStringLiteral("7 IRON"), ClubSet::Junior, 0.0).valid);   // no height, no standard
    EXPECT_EQ(standardClub(QStringLiteral("7 IRON"), ClubSet::Junior, 48 * 0.0254).lengthMm, 737);   // 29"
    EXPECT_EQ(standardClub(QStringLiteral("7 IRON"), ClubSet::Junior, 66 * 0.0254).lengthMm, 940);   // 37"
    EXPECT_EQ(standardClub(QStringLiteral("7 IRON"), ClubSet::Junior, 49.5 * 0.0254).lengthMm, 756); // 29.75"
    EXPECT_EQ(standardClub(QStringLiteral("7 IRON"), ClubSet::Junior, 2.0).lengthMm, 940);           // clamped
    EXPECT_EQ(standardClub(QStringLiteral("4 IRON"), ClubSet::Junior, 66 * 0.0254).lengthMm, 978);  // 5 iron + ½"
}

TEST(ClubStandards, SetForAthlete)
{
    EXPECT_EQ(clubSetFor(QStringLiteral("female"), false), ClubSet::Women);
    EXPECT_EQ(clubSetFor(QStringLiteral("male"), false), ClubSet::Men);
    EXPECT_EQ(clubSetFor(QStringLiteral("declined"), false), ClubSet::Men);
    EXPECT_EQ(clubSetFor(QStringLiteral("female"), true), ClubSet::Junior);
    EXPECT_FALSE(standardClub(QStringLiteral("CHIPPER"), ClubSet::Men).valid);
}
