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

// battery_band_test — the one battery rule (src/Vocabulary/Rig/battery_band.h): every band edge (100, 50, 49, 21,
// 20, 0), charging at each level, no reading, the spelling the screens read, and the worst of several devices (the
// toolbar's lowest phone and lowest sensor), where a phone on a cable never takes the place of one that is low.

#include "../../Vocabulary/Rig/battery_band.h"

#include <cstdio>
#include <string>
#include <vector>

using pinpoint::rig::BatteryBand;
using pinpoint::rig::BatteryReading;
using pinpoint::rig::batteryBand;
using pinpoint::rig::batteryBandKey;
using pinpoint::rig::worstBattery;

namespace {

int g_run = 0, g_fail = 0;

void check(const std::string &what, bool ok)
{
    ++g_run;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what.c_str()); }
}

std::string key(int level, bool charging) { return batteryBandKey(batteryBand(level, charging)); }

void testEdges()
{
    struct Edge { int level; const char *band; };
    // The ruling: 50 % and up ok; 21 to 49 % low; 20 % and below very low.
    const Edge edges[] = { { 100, "ok" }, { 50, "ok" }, { 49, "low" }, { 21, "low" }, { 20, "veryLow" }, { 0, "veryLow" } };
    for (const Edge &e : edges)
        check("level " + std::to_string(e.level) + " is " + e.band, key(e.level, false) == e.band);
}

void testCharging()
{
    // A device that reports it is charging is never raised to low or very low, at any level.
    for (int level : { 100, 50, 49, 21, 20, 0 })
        check("level " + std::to_string(level) + " charging is ok", key(level, true) == "ok");
}

void testNoReading()
{
    // -1 is the "no reading yet" every owner publishes; any level below 0 has no band, charging or not.
    for (int level : { -1, -100 }) {
        check("level " + std::to_string(level) + " has no band", !batteryBand(level, false).has_value());
        check("level " + std::to_string(level) + " charging has no band", !batteryBand(level, true).has_value());
        check("level " + std::to_string(level) + " spells \"\"", key(level, false).empty());
    }
}

void testSpelling()
{
    check("Ok spells ok", std::string(batteryBandKey(BatteryBand::Ok)) == "ok");
    check("Low spells low", std::string(batteryBandKey(BatteryBand::Low)) == "low");
    check("VeryLow spells veryLow", std::string(batteryBandKey(BatteryBand::VeryLow)) == "veryLow");
    check("no band spells \"\"", std::string(batteryBandKey(std::nullopt)).empty());
}

void testWorst()
{
    {
        const std::vector<BatteryReading> none;
        const auto w = worstBattery(none);
        check("no devices: no band", !w.band.has_value());
        check("no devices: level -1", w.levelPercent == -1);
    }
    {
        const std::vector<BatteryReading> noReadings = { { -1, false }, { -1, true } };
        const auto w = worstBattery(noReadings);
        check("no readings: no band, level -1", !w.band.has_value() && w.levelPercent == -1);
    }
    {
        // Two sensors (no charging): the band of the lowest level, and that level.
        const std::vector<BatteryReading> sensors = { { 75, false }, { 30, false }, { -1, false }, { 60, false } };
        const auto w = worstBattery(sensors);
        check("sensors: low", w.band == BatteryBand::Low);
        check("sensors: the lowest level, 30", w.levelPercent == 30);
    }
    {
        // A phone at 15 % on a cable is ok; the other, at 40 %, is low: the group is low, and names 40, not 15.
        const std::vector<BatteryReading> phones = { { 15, true }, { 40, false } };
        const auto w = worstBattery(phones);
        check("phones, one charging: low", w.band == BatteryBand::Low);
        check("phones, one charging: names the low phone's 40", w.levelPercent == 40);
    }
    {
        // Every phone charging: ok, at the lowest level.
        const std::vector<BatteryReading> phones = { { 30, true }, { 12, true } };
        const auto w = worstBattery(phones);
        check("phones all charging: ok at 12", w.band == BatteryBand::Ok && w.levelPercent == 12);
    }
    {
        // Very low beats low, whatever the order; among two very low, the lower level.
        const std::vector<BatteryReading> a = { { 45, false }, { 20, false }, { 5, false }, { 21, false } };
        const auto w = worstBattery(a);
        check("very low at 5 beats low and the other very low", w.band == BatteryBand::VeryLow && w.levelPercent == 5);
    }
}

}  // namespace

int main()
{
    std::printf("battery_band_test — the one battery rule (src/Vocabulary/Rig/battery_band.h)\n");
    testEdges();
    testCharging();
    testNoReading();
    testSpelling();
    testWorst();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
