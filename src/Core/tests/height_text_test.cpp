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

// The athlete form's Height field (height_text.h): 5'11" is five feet eleven, not five feet — the
// placeholder's own example was stored as whole feet until 14 Nov 2026. Every spelling the header
// lists reads, the value the form shows back reads as itself, the other unit converts, and text that
// is not a height is 0 (unknown), as it was.

#include "../../Vocabulary/Common/height_text.h"

#include <gtest/gtest.h>

using pinpoint::parseHeightText;

namespace {
const QString kFt = QStringLiteral("ft"), kCm = QStringLiteral("cm");
constexpr double kFiveEleven = 5.0 + 11.0 / 12.0;   // ft
double ft(const char *s) { return parseHeightText(QString::fromUtf8(s), kFt); }
double cm(const char *s) { return parseHeightText(QString::fromUtf8(s), kCm); }
} // namespace

TEST(HeightText, ThePlaceholdersFeetAndInchesAreNotWholeFeet)
{
    EXPECT_DOUBLE_EQ(ft("5'11\""), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5' 11\""), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5'11"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5'11''"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft(" 5 ' 11 \" "), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5'-11\""), kFiveEleven);
}

TEST(HeightText, TypographicAndWordedMarks)
{
    EXPECT_DOUBLE_EQ(ft("5′11″"), kFiveEleven);   // prime, double prime
    EXPECT_DOUBLE_EQ(ft("5’11”"), kFiveEleven);   // autocorrected quotes
    EXPECT_DOUBLE_EQ(ft("5’11’’"), kFiveEleven);   // autocorrected 5'11'': two curly singles
    EXPECT_DOUBLE_EQ(ft("5‘11‘’"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5ft 11in"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5 ft 11 in"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5ft11"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("5 Feet 11 Inches"), kFiveEleven);
    EXPECT_DOUBLE_EQ(ft("6 foot 1 inch"), 6.0 + 1.0 / 12.0);
}

TEST(HeightText, WholeFeetAndFractionalInches)
{
    EXPECT_DOUBLE_EQ(ft("6'"), 6.0);
    EXPECT_DOUBLE_EQ(ft("6ft"), 6.0);
    EXPECT_DOUBLE_EQ(ft("6'0\""), 6.0);
    EXPECT_DOUBLE_EQ(ft("5'11.5\""), 5.0 + 11.5 / 12.0);
    EXPECT_DOUBLE_EQ(ft("71\""), 71.0 / 12.0);
    EXPECT_DOUBLE_EQ(ft("71 in"), 71.0 / 12.0);
}

TEST(HeightText, ABareNumberIsInTheFieldsUnit)
{
    // What the form shows back for a stored height (toFixed(2) ft, toFixed(1) cm) reads as itself,
    // so opening and saving an athlete does not move the height.
    EXPECT_DOUBLE_EQ(ft("5.92"), 5.92);
    EXPECT_DOUBLE_EQ(ft("5"), 5.0);
    EXPECT_DOUBLE_EQ(ft(".5"), 0.5);
    EXPECT_DOUBLE_EQ(cm("180"), 180.0);
    EXPECT_DOUBLE_EQ(cm("180.3"), 180.3);
    EXPECT_DOUBLE_EQ(cm("180,3"), 180.3);
}

TEST(HeightText, MetricReadsInEitherUnit)
{
    EXPECT_DOUBLE_EQ(cm("180 cm"), 180.0);
    EXPECT_DOUBLE_EQ(cm("180cm"), 180.0);
    EXPECT_DOUBLE_EQ(cm("1.80 m"), 180.0);
    EXPECT_DOUBLE_EQ(cm("1,80m"), 180.0);
    EXPECT_DOUBLE_EQ(cm("1.8 metres"), 180.0);
    EXPECT_NEAR(ft("180 cm"), 180.0 / 30.48, 1e-12);
    EXPECT_NEAR(ft("1.80 m"), 180.0 / 30.48, 1e-12);
}

TEST(HeightText, FeetAndInchesReadWithTheToggleOnCm)
{
    EXPECT_NEAR(cm("5'11\""), kFiveEleven * 30.48, 1e-9);
    EXPECT_NEAR(cm("71 in"), 71 * 2.54, 1e-9);
}

TEST(HeightText, NotAHeightIsUnknown)
{
    for (const char *s : {"", "   ", "abc", "5'13\"", "5'12", "5.5'6\"", "-5", "0", "0'0\"", "5 11",
                          "5-11", "tall", "5'11\" ish", "180 mm", "1.80.2"})
        EXPECT_EQ(ft(s), 0.0) << s;
    EXPECT_EQ(cm("abc"), 0.0);
}
