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

// lm_device.h — the connector a reading arrived by, kept apart from the device that measured it.

#include "lm_device.h"

#include <gtest/gtest.h>

#include <QJsonObject>
#include <QSet>

using namespace pinpoint::lm;

namespace {
QString q(const char *s) { return QString::fromUtf8(s); }
}

// The one invariant the shared key space rests on: a connector token that is ALSO a device key
// reads exactly that device, so an old "lm:gcquad" and a new one can never mean two things.
TEST(LmDevice, ConnectorTokenThatIsADeviceKeyReadsThatDevice)
{
    for (const ConnectorDef &c : kConnectors)
        if (findDevice(q(c.kind)))
            EXPECT_STREQ(c.device, c.kind) << c.kind;
}

TEST(LmDevice, KeysAreUniqueAndConnectorsNameRealDevices)
{
    QSet<QString> keys;
    for (const DeviceDef &d : kDevices) {
        EXPECT_FALSE(keys.contains(q(d.key))) << d.key;
        keys.insert(q(d.key));
    }
    for (const ConnectorDef &c : kConnectors)
        if (*c.device)
            EXPECT_NE(findDevice(q(c.device)), nullptr) << c.kind;
}

// Every name the table hands out reads back as its own row: the live board identifies its device
// from the controller's name, and a row ordered after another whose alias it contains would lose.
TEST(LmDevice, EveryNameIdentifiesItsOwnRow)
{
    for (const DeviceDef &d : kDevices) {
        EXPECT_EQ(deviceKeyFromReported(q(d.name)), q(d.key)) << d.name;
        EXPECT_EQ(deviceName(q(d.key)), q(d.name));
    }
}

// THE BUG THIS EXISTS FOR: a GC Quad is one device whichever program held it.
TEST(LmDevice, BothForesightConnectorsAreOneGcQuad)
{
    EXPECT_EQ(resolveDeviceKey(q("gcquad")),  q("gcquad"));
    EXPECT_EQ(resolveDeviceKey(q("premier")), q("gcquad"));
    EXPECT_EQ(canonicalDeviceToken(q("lm:premier")), q("lm:gcquad"));   // an older summary
    EXPECT_EQ(canonicalDeviceToken(q("lm:gcquad")),  q("lm:gcquad"));
    EXPECT_EQ(deviceName(q("premier")), q("GC Quad"));
    EXPECT_EQ(deviceMaker(q("premier")), q("Foresight"));
    EXPECT_EQ(deviceMarkers(q("premier")), q("four"));
}

// GSPro Open Connect relays many devices: the client's name for itself decides, whatever its
// spelling, and a GC Quad through GSPro is the same GC Quad.
TEST(LmDevice, GsProClientsAreMappedToTheirDevice)
{
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("GSPro GCQuad")),        q("gcquad"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("Foresight GC Quad")),   q("gcquad"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("QuadMAX")),             q("quadmax"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("Garmin R10")),          q("r10"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("R10")),                 q("r10"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("MLM2PRO")),             q("mlm2pro"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("Rapsodo MLM2PRO")),     q("mlm2pro"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("SkyTrak+")),            q("skytrakplus"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("SkyTrak")),             q("skytrak"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("FlightScope Mevo+")),   q("mevoplus"));
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("OpenLaunch NOVA v1.0")), q("nova"));   // nova_launch_monitor.md
}

// A client the table does not know is recorded as "some device, via GSPro": nothing guessed, and
// still named and listed rather than dropped.
TEST(LmDevice, UnknownGsProClientStaysAnUnidentifiedLaunchMonitor)
{
    EXPECT_EQ(resolveDeviceKey(q("gspro"), q("PinPointLinkTest")), QString());
    EXPECT_EQ(resolveDeviceKey(q("gspro")), QString());
    EXPECT_EQ(canonicalDeviceKey(q("gspro")), q("gspro"));
    EXPECT_EQ(deviceName(q("gspro")), q("Launch monitor (GSPro)"));
    EXPECT_FALSE(isIdentifiedDevice(q("gspro")));
    EXPECT_TRUE(isIdentifiedDevice(q("premier")));
    EXPECT_EQ(deviceName(QString()), QString());
    EXPECT_EQ(deviceName(q("somethingnew")), q("Launch monitor"));
}

// A document block: its recorded device wins; without one, the connector implies it.
TEST(LmDevice, BlockReadsDeviceThenConnector)
{
    EXPECT_EQ(deviceKeyOfBlock(QJsonObject{ { "kind", "gspro" }, { "device", "r10" } }), q("r10"));
    EXPECT_EQ(deviceKeyOfBlock(QJsonObject{ { "kind", "premier" } }), q("gcquad"));
    EXPECT_EQ(deviceKeyOfBlock(QJsonObject{ { "kind", "gspro" } }), q("gspro"));
    EXPECT_EQ(deviceKeyOfBlock(QJsonObject{}), QString());
}

TEST(LmDevice, OtherTokensPassThrough)
{
    EXPECT_EQ(canonicalDeviceToken(q("faceOn")), q("faceOn"));
    EXPECT_EQ(canonicalDeviceToken(q("hackmotion")), q("hackmotion"));
}
