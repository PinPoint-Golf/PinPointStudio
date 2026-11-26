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

#pragma once

// The battery band: the one rule for how low a device's battery is, for every device that reports a level
// (the sensors, Witmotion and HackMotion, and the phones). Every screen that shows a battery reads the band this
// rule gives and maps it to a theme token and its words; no screen compares a level itself (AD-52; R-QML-4).
// The band is a reading worked out from the level, never stored (information_model.md §6.1, NOTDOM-6).
// Vocabulary ring, context Acquisition. Header-only, the standard library only: no Qt, no file I/O.
//
// The bands, one set for every device:
//   50 % and up         Ok
//   21 to 49 %          Low
//   20 % and below      VeryLow
//   a device that reports it is charging is never Low or VeryLow (only phones report charging today)
// A level below 0 is "no reading yet" (the -1 every owner publishes): it has no band. A device that never reports a
// level (the launch monitor, a wired camera, a Witmotion on a cable) publishes no band at all.
//
// Who publishes it, beside the level each already publishes: the sensors (ImuDeviceBase::batteryBandKey, on each
// ImuInstance and HmInstance) and the lowest of them (ImuManager::lowBatteryBand); the phones (PpcpHostService's
// phones() rows and phoneHealth(), and the worst of them, phoneLowestBatteryBand); and the Resource Monitor's sensor
// rows (ResourceMonitorController). They publish batteryBandKey()'s spelling; the screens map it, by equality only.
// battery_band_test pins every edge.

#include <optional>
#include <span>

namespace pinpoint::rig {

enum class BatteryBand { Ok, Low, VeryLow };

// The band of one device's level, in whole percent; charging only for a device that reports it (a phone's
// tri-state "unknown" counts as not charging). No band for no reading.
constexpr std::optional<BatteryBand> batteryBand(int levelPercent, bool charging) noexcept
{
    if (levelPercent < 0) return std::nullopt;
    if (charging)         return BatteryBand::Ok;
    if (levelPercent >= 50) return BatteryBand::Ok;
    if (levelPercent > 20)  return BatteryBand::Low;
    return BatteryBand::VeryLow;
}

// The spelling every owner publishes to the screens, and the only one: "ok", "low", "veryLow"; "" for no band.
constexpr const char *batteryBandKey(std::optional<BatteryBand> band) noexcept
{
    if (!band) return "";
    switch (*band) {
    case BatteryBand::Ok:      return "ok";
    case BatteryBand::Low:     return "low";
    case BatteryBand::VeryLow: return "veryLow";
    }
    return "";
}

// One device's reading, as the "worst of several" below takes it.
struct BatteryReading
{
    int  levelPercent = -1;   // below 0: no reading yet
    bool charging     = false;
};

// Of several devices, the one whose band raises the most: VeryLow before Low before Ok; among equal bands, the
// lowest level. Its band is the group's band and its level is the level the screen names, so the two always describe
// the same device: a phone at 15 % on a cable (Ok) beside one at 40 % (Low) gives Low at 40 %, not Low at 15 %.
// With no reading among them, the result has no band and a level of -1.
struct WorstBattery
{
    std::optional<BatteryBand> band;
    int                        levelPercent = -1;
};

constexpr WorstBattery worstBattery(std::span<const BatteryReading> readings) noexcept
{
    WorstBattery worst;
    for (const BatteryReading &r : readings) {
        const std::optional<BatteryBand> b = batteryBand(r.levelPercent, r.charging);
        if (!b) continue;
        const bool worse = !worst.band
                           || static_cast<int>(*b) > static_cast<int>(*worst.band)
                           || (*b == *worst.band && r.levelPercent < worst.levelPercent);
        if (worse) worst = { b, r.levelPercent };
    }
    return worst;
}

}  // namespace pinpoint::rig
