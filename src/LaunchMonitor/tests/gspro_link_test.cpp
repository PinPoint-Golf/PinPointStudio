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

// Build-dependency smoke test for libgspro (github.com/PinPoint-Golf/libgspro).
//
//   cmake -S src/LaunchMonitor/tests -B build/lm-tests
//   cmake --build build/lm-tests -j && ctest --test-dir build/lm-tests -R gspro
//
// WHY THIS EXISTS. Until the GSPro connector lands, no PinPoint code references
// a single libgspro symbol — so a library that configures and compiles but fails
// to LINK would go unnoticed on every platform, and the failure would surface
// mid-integration rather than at the point the dependency was added. The same
// reasoning, and the same shape, as hackmotion_link_test.cpp: it deliberately
// calls ACROSS the C↔C++ boundary rather than merely including a header.
//
// It is also the guard against the failure specific to a dependency that tracks
// a branch and can be overridden by a local checkout: HEADERS FROM ONE BUILD AND
// AN ARCHIVE FROM ANOTHER. gsp_abi_check() compares this compiler's view of
// every public struct against the sizes the library was built with, which is
// exactly that skew — and, incidentally, any MSVC/gcc/clang layout disagreement
// in the POD types the connector will be copying out of the event ring.
//
// ⚠ IT MUST NOT OPEN A SOCKET, and the fact that it cannot is the property being
// checked. libgspro owns no socket, thread, timer, clock or file: PinPoint
// supplies the QTcpServer and the host clock. So a whole session runs here on a
// synthetic counter with byte strings — which is the same reason the library's
// own suite needs no network, and the reason this test is safe in CI.

#include <gspro/gspro.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

// One shot, in the shape the vendor documents — enough to prove the decoder in
// the linked archive is the decoder these headers describe.
const char *kShotJson =
    "{\"DeviceID\":\"PinPointLinkTest\",\"Units\":\"Yards\",\"ShotNumber\":7,"
    "\"APIversion\":\"1\","
    "\"BallData\":{\"Speed\":148.2,\"SpinAxis\":-13.2,\"TotalSpin\":3250.0,"
    "\"HLA\":2.3,\"VLA\":13.7,\"CarryDistance\":256.5},"
    "\"ClubData\":{\"Speed\":100.1,\"AngleOfAttack\":-2.2,\"FaceToTarget\":1.1,"
    "\"Path\":-0.7,\"Loft\":13.9},"
    "\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":true,"
    "\"LaunchMonitorIsReady\":true,\"IsHeartBeat\":false}}";

} // namespace

TEST(GsProLink, AbiMatchesTheHeadersThisWasCompiledWith)
{
    // ⚠ THE ONE CHECK THAT CATCHES A MIXED BUILD. Sizes only — the library's own
    // header says so — but a header/archive skew is precisely a size change, and
    // it is the failure mode a branch-tracking dependency actually produces.
    gsp_abi_sizes expected;
    std::memset(&expected, 0, sizeof(expected));
    expected.abi_version            = GSP_ABI_VERSION;
    expected.message                = sizeof(gsp_message);
    expected.ball_data              = sizeof(gsp_ball_data);
    expected.club_data              = sizeof(gsp_club_data);
    expected.shot_options           = sizeof(gsp_shot_options);
    expected.player_info            = sizeof(gsp_player_info);
    expected.event                  = sizeof(gsp_event);
    expected.write_request          = sizeof(gsp_write_request);
    expected.wire_chunk             = sizeof(gsp_wire_chunk);
    expected.connection_info        = sizeof(gsp_connection_info);
    expected.server_config          = sizeof(gsp_server_config);
    expected.message_layout_version = GSP_MESSAGE_LAYOUT_VERSION;

    EXPECT_EQ(gsp_abi_check(&expected), GSP_OK)
        << "libgspro headers and archive disagree — a mixed build. Header version "
        << GSP_VERSION_STRING << ", library version " << gsp_version_string();

    EXPECT_EQ(gsp_abi_version(), static_cast<uint32_t>(GSP_ABI_VERSION));
    EXPECT_STREQ(gsp_version_string(), GSP_VERSION_STRING);
}

TEST(GsProLink, TheDecoderInTheArchiveReadsAShot)
{
    gsp_message m;
    ASSERT_EQ(gsp_message_decode(reinterpret_cast<const uint8_t *>(kShotJson),
                                 std::strlen(kShotJson), &m),
              GSP_OK);

    EXPECT_EQ(m.kind, GSP_MSG_SHOT);
    EXPECT_STREQ(m.device_id, "PinPointLinkTest");
    EXPECT_EQ(m.shot_number, 7);

    // ⚠ PRESENCE IS A BITMASK AND ZERO IS NOT ABSENCE (library design §4.3). The
    // connector must gate every field on its bit rather than on a value, so the
    // discipline is asserted here before there is a connector to get it wrong.
    ASSERT_NE(m.ball.present & GSP_BALL_SPEED, 0u);
    EXPECT_NEAR(m.ball.speed, 148.2, 1e-9);
    ASSERT_NE(m.club.present & GSP_CLUB_SPEED, 0u);
    EXPECT_NEAR(m.club.speed, 100.1, 1e-9);
    EXPECT_EQ(m.club.present & GSP_CLUB_CLOSURE_RATE, 0u)
        << "a field that was not on the wire must not be reported present";

    EXPECT_EQ(m.units, GSP_UNITS_YARDS);
}

TEST(GsProLink, AWholeSessionRunsWithNoSocketAndNoClock)
{
    // The transport contract, exercised end to end: open a connection with an id
    // WE chose, hand over bytes, take back the reply and the event. If this
    // compiles and passes, the connector has everything it needs from the
    // library and the remaining work is Qt's side of the line.
    gsp_server_config cfg = gsp_server_config_default();
    gsp_server *s = nullptr;
    ASSERT_EQ(gsp_server_create(&cfg, &s), GSP_OK);
    ASSERT_NE(s, nullptr);

    const gsp_time_us now = 1'000'000;   // synthetic; the library reads no clock
    ASSERT_EQ(gsp_server_on_connection_opened(s, 1, "203.0.113.7:51022", now), GSP_OK);
    ASSERT_EQ(gsp_server_on_bytes(s, 1, reinterpret_cast<const uint8_t *>(kShotJson),
                                  std::strlen(kShotJson), now),
              GSP_OK);

    gsp_write_request w[4];
    const size_t writes = gsp_server_poll_writes(s, w, 4);
    ASSERT_EQ(writes, 1u) << "exactly one reply per message";
    gsp_response reply;
    ASSERT_EQ(gsp_response_decode(w[0].data, w[0].length, &reply), GSP_OK);
    EXPECT_EQ(reply.code, 200);

    gsp_event ev[8];
    const size_t events = gsp_server_poll_events(s, ev, 8);
    ASSERT_GT(events, 0u);
    bool sawShot = false;
    for (size_t i = 0; i < events; ++i) {
        if (ev[i].type == GSP_EV_SHOT) {
            sawShot = true;
            EXPECT_EQ(ev[i].conn, 1u);
            EXPECT_NEAR(ev[i].u.message.ball.speed, 148.2, 1e-9);
        }
    }
    EXPECT_TRUE(sawShot);

    // ⚠ Nothing is due: the protocol has no deadline of its own, so a host that
    // arms a timer from this gets GSP_TIME_NEVER and arms nothing. The connector
    // depends on that — it is why an idle GSPro link costs no wakeups.
    EXPECT_EQ(gsp_server_next_due_us(s), GSP_TIME_NEVER);

    gsp_server_close(s);
    gsp_server_destroy(s);
}

TEST(GsProLink, TheClubVocabularyTheConnectorWillSendIsLive)
{
    // Club selection flows the OTHER way: PinPoint's club becomes a 201 so a
    // connector switches to putting mode when the coach selects the putter
    // (library design §6). These are the codes that carries, and a typo in the
    // connector's mapping is a silently ignored club rather than an error.
    EXPECT_EQ(gsp_club_parse("PT"), GSP_CLUB_PT);
    EXPECT_EQ(gsp_club_parse("DR"), GSP_CLUB_DR);
    EXPECT_STREQ(gsp_club_code(GSP_CLUB_PT), "PT");
    EXPECT_EQ(gsp_club_parse("not-a-club"), GSP_CLUB_UNKNOWN);

    // And the port PinPoint will bind, from the library rather than retyped.
    EXPECT_EQ(GSP_DEFAULT_PORT, 921);
}
