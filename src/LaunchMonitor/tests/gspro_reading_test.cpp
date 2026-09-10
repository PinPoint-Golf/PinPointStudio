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

// gsp_message → LaunchMonitorReading, the pure half of the GSPro connector.
//
// ⚠ EVERY FIXTURE HERE IS A REAL CLIENT'S BYTE PATTERN, not a tidied version of
// one — the same rule as gcquad_csv_parser_test embedding the actual FSX2020 row
// with its trailing comma. The quirks ARE the thing most likely to break: one
// client sends only Backspin/Sidespin and no total, one misspells a key's case,
// one omits ClubData entirely, and one puts a different quantity in
// HorizontalFaceImpact. A fixture retyped into what the protocol "obviously" says
// would test the author's belief rather than the device.
//
// The socket, the connection table and the replies are GsProMonitor's and are
// tested in gspro_monitor_test.cpp; nothing here opens anything.

#include "gspro_reading.h"

#include <gspro/gspro.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

using pinpoint::lm::readingFromGsProMessage;

namespace {

// Decode JSON the way the connector will — through the library — so the fixtures
// below are wire bytes rather than a struct filled in by hand. A hand-filled
// struct would skip the very step that turns a client's quirk into a presence bit.
gsp_message decode(const std::string &json)
{
    gsp_message m;
    std::memset(&m, 0, sizeof(m));
    EXPECT_EQ(gsp_message_decode(reinterpret_cast<const uint8_t *>(json.data()),
                                 json.size(), &m),
              GSP_OK)
        << json;
    return m;
}

// [GSP] — the vendor's own example shape: ball and club, everything present.
const char *kFullShot =
    "{\"DeviceID\":\"Exemplar\",\"Units\":\"Yards\",\"ShotNumber\":13,"
    "\"APIversion\":\"1\","
    "\"BallData\":{\"Speed\":147.5,\"SpinAxis\":-13.2,\"TotalSpin\":3250.0,"
    "\"BackSpin\":3163.0,\"SideSpin\":-742.0,\"HLA\":2.3,\"VLA\":13.7,"
    "\"CarryDistance\":256.5},"
    "\"ClubData\":{\"Speed\":100.1,\"AngleOfAttack\":-2.2,\"FaceToTarget\":1.1,"
    "\"Lie\":0.5,\"Loft\":13.9,\"Path\":-0.7,\"SpeedAtImpact\":99.8,"
    "\"VerticalFaceImpact\":-2.0,\"HorizontalFaceImpact\":3.5,\"ClosureRate\":95.0},"
    "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":true,"
    "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";

} // namespace

TEST(GsProReading, MapsEveryFieldTheVendorDocuments)
{
    const gsp_message m = decode(kFullShot);
    QString why;
    const auto r = readingFromGsProMessage(m, &why);
    ASSERT_TRUE(r.has_value()) << why.toStdString();

    EXPECT_EQ(r->deviceShotId.toStdString(), "13");
    EXPECT_DOUBLE_EQ(*r->ballSpeed, 147.5);
    EXPECT_DOUBLE_EQ(*r->spinAxis, -13.2);
    EXPECT_DOUBLE_EQ(*r->spinRate, 3250.0);
    EXPECT_DOUBLE_EQ(*r->backSpin, 3163.0);
    EXPECT_DOUBLE_EQ(*r->sideSpin, -742.0);
    EXPECT_DOUBLE_EQ(*r->launchDirection, 2.3);
    EXPECT_DOUBLE_EQ(*r->launchAngle, 13.7);
    EXPECT_DOUBLE_EQ(*r->carryDistance, 256.5);

    EXPECT_DOUBLE_EQ(*r->clubheadSpeed, 100.1);
    EXPECT_DOUBLE_EQ(*r->attackAngle, -2.2);
    EXPECT_DOUBLE_EQ(*r->faceAngle, 1.1);
    EXPECT_DOUBLE_EQ(*r->clubPath, -0.7);
    EXPECT_DOUBLE_EQ(*r->lieAngle, 0.5);
    EXPECT_DOUBLE_EQ(*r->dynamicLoft, 13.9);
    EXPECT_DOUBLE_EQ(*r->closureRate, 95.0);
    EXPECT_DOUBLE_EQ(*r->strikeHeight, -2.0);
    EXPECT_DOUBLE_EQ(*r->strikeLocation, 3.5);

    // ⚠ THE CLUB IS NOT ON THE WIRE. It travels the other way (server → client),
    // so a reading that claimed one would be inventing it.
    EXPECT_TRUE(r->deviceClub.isEmpty());
}

TEST(GsProReading, DerivesTheSameThreeValuesTheGcQuadPathDoes)
{
    const gsp_message m = decode(kFullShot);
    const auto r = readingFromGsProMessage(m);
    ASSERT_TRUE(r.has_value());

    // face-to-path = face − path, and the reading means the same thing whichever
    // connector produced it.
    EXPECT_NEAR(*r->faceToPath, 1.1 - (-0.7), 1e-9);
    EXPECT_NEAR(*r->smashFactor, 147.5 / 100.1, 1e-9);
    EXPECT_NEAR(*r->spinLoft, 13.9 - (-2.2), 1e-9);
}

TEST(GsProReading, RecoversTotalSpinAndAxisFromABackAndSideOnlyClient)
{
    // [MLM] sends Backspin and Sidespin and NO total and NO axis — and it spells
    // the key "Backspin" where the vendor writes "BackSpin", which is exactly the
    // sort of thing a stricter reader drops on the floor.
    const std::string json =
        "{\"DeviceID\":\"MLM2PRO\",\"Units\":\"Yards\",\"ShotNumber\":1,"
        "\"BallData\":{\"Speed\":120.0,\"Backspin\":3000.0,\"Sidespin\":-1000.0,"
        "\"HLA\":1.0,\"VLA\":15.0},"
        "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":false,"
        "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";
    const gsp_message m = decode(json);
    const auto r = readingFromGsProMessage(m);
    ASSERT_TRUE(r.has_value());

    EXPECT_DOUBLE_EQ(*r->backSpin, 3000.0);
    EXPECT_DOUBLE_EQ(*r->sideSpin, -1000.0);
    // Derived by the library from the pair — a reading that lacked these because
    // the device did not restate them would be a reading missing its spin.
    ASSERT_TRUE(r->spinRate.has_value());
    EXPECT_NEAR(*r->spinRate, std::hypot(3000.0, 1000.0), 1e-6);
    ASSERT_TRUE(r->spinAxis.has_value());
    EXPECT_LT(*r->spinAxis, 0.0) << "side spin left of centre must tilt the axis left";

    // No ClubData at all: every club field absent rather than zero.
    EXPECT_FALSE(r->clubheadSpeed.has_value());
    EXPECT_FALSE(r->attackAngle.has_value());
    EXPECT_FALSE(r->smashFactor.has_value()) << "no club speed, so no smash factor";
}

TEST(GsProReading, AMeasuredZeroIsNotAnAbsentField)
{
    // ⚠ THE BUG THIS WHOLE MAPPING IS SHAPED AGAINST. A putt at 0.0° of attack and
    // a square face are measurements; a mapping that tested `!= 0.0` would report
    // both as "the device did not say", and the panel would show a gap where the
    // most interesting number in a putting session should be.
    const std::string json =
        "{\"DeviceID\":\"Putter\",\"Units\":\"Yards\",\"ShotNumber\":2,"
        "\"BallData\":{\"Speed\":0.0,\"SpinAxis\":0.0,\"TotalSpin\":0.0,"
        "\"HLA\":0.0,\"VLA\":0.0},"
        "\"ClubData\":{\"Speed\":4.5,\"AngleOfAttack\":0.0,\"FaceToTarget\":0.0},"
        "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":true,"
        "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";
    const gsp_message m = decode(json);
    const auto r = readingFromGsProMessage(m);
    ASSERT_TRUE(r.has_value());

    ASSERT_TRUE(r->ballSpeed.has_value());
    EXPECT_DOUBLE_EQ(*r->ballSpeed, 0.0);
    ASSERT_TRUE(r->attackAngle.has_value());
    EXPECT_DOUBLE_EQ(*r->attackAngle, 0.0);
    ASSERT_TRUE(r->faceAngle.has_value());
    EXPECT_DOUBLE_EQ(*r->faceAngle, 0.0);
    // And a field that genuinely was not sent stays absent beside them.
    EXPECT_FALSE(r->closureRate.has_value());
    EXPECT_FALSE(r->carryDistance.has_value());
}

TEST(GsProReading, AHeartbeatIsNotAReading)
{
    // ⚠ Every client sends these, some every second, and GSPro answers all of them
    // with the same 200 as a shot — so the reply is the library's business and the
    // distinction is ours. A connector that mapped one would attribute an empty
    // shot to a swing every few seconds.
    const std::string json =
        "{\"DeviceID\":\"R10\",\"Units\":\"Yards\",\"ShotNumber\":0,"
        "\"ShotDataOptions\":{\"ContainsBallData\":false,\"ContainsClubData\":false,"
        "\"LaunchMonitorIsReady\":true,\"LaunchMonitorBallDetected\":false,"
        "\"IsHeartBeat\":true}}";
    const gsp_message m = decode(json);
    QString why;
    EXPECT_FALSE(readingFromGsProMessage(m, &why).has_value());
    EXPECT_FALSE(why.isEmpty()) << "a refusal must say why";
}

TEST(GsProReading, ConvertsDistanceUnderMetersAndLeavesSpeedsAlone)
{
    // ⚠ THE HONEST HALF-CONVERSION, and the reason is in the header: what `Units`
    // governs is not stated by the vendor. Every client that can be read sends mph
    // for speed whatever it declares, so converting a speed on a guess would
    // corrupt the one number this connector exists to check the camera against —
    // while leaving a carry 9% wrong buys nothing. When that unknown closes
    // against real hardware, this test is the thing that changes with it.
    const std::string json =
        "{\"DeviceID\":\"Metric\",\"Units\":\"Meters\",\"ShotNumber\":3,"
        "\"BallData\":{\"Speed\":65.0,\"SpinAxis\":0.0,\"TotalSpin\":3000.0,"
        "\"HLA\":0.0,\"VLA\":14.0,\"CarryDistance\":200.0},"
        "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":false,"
        "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";
    const gsp_message m = decode(json);
    ASSERT_EQ(m.units, GSP_UNITS_METERS);
    const auto r = readingFromGsProMessage(m);
    ASSERT_TRUE(r.has_value());

    EXPECT_NEAR(*r->carryDistance, 200.0 / 0.9144, 1e-6) << "metres became yards";
    EXPECT_DOUBLE_EQ(*r->ballSpeed, 65.0) << "speed is left exactly as it arrived";
}

TEST(GsProReading, YardsIsTheIdentity)
{
    const gsp_message m = decode(kFullShot);
    ASSERT_EQ(m.units, GSP_UNITS_YARDS);
    const auto r = readingFromGsProMessage(m);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(*r->carryDistance, 256.5) << "nothing is converted under Yards";
}

TEST(GsProReading, AShotWithNoNumbersIsRefused)
{
    // ContainsBallData true and no BallData object — a real client bug the library
    // flags rather than rejects, because the message is well-formed. There is
    // simply nothing to attribute to a swing.
    const std::string json =
        "{\"DeviceID\":\"Empty\",\"Units\":\"Yards\",\"ShotNumber\":4,"
        "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":false,"
        "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";
    const gsp_message m = decode(json);
    ASSERT_EQ(m.kind, GSP_MSG_SHOT);
    QString why;
    EXPECT_FALSE(readingFromGsProMessage(m, &why).has_value());
    EXPECT_FALSE(why.isEmpty());
}
