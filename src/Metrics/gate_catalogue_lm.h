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

// The gate catalogue's LaunchMonitor entries (trust stage 6, docs/design/trust_stage6_plan.md). Included by
// gate_catalogue.h after GateEntry and the detail::add / detail::rule helpers; not a standalone header.

namespace pinpoint::gates::detail {

// The launch monitor's import drops a field rather than show a number no golfer can act on
// (src/LaunchMonitor/gcquad_csv_parser.cpp). The import keeps the list; the swing stores it in its
// `launchMonitor` block; lm_gates.h turns it into these records on every write of analysis.gates.
// Subject `lm.<field>`. Swings imported before the list was kept have no records (Mark, 16 Oct).
// GSPro readings are taken as sent: no range checks, so nothing is dropped and nothing recorded.
inline void appendLaunchMonitorGates(std::vector<GateEntry> &c)
{
    const QString area = QStringLiteral("Launch monitor");
    const char *prod = "src/LaunchMonitor/lm_gates.h";
    const char *design = "docs/design/trust_stage6_plan.md (Launch monitor); src/LaunchMonitor/gcquad_csv_parser.cpp kNoData";

    {
        GateEntry &e = add(c, "lm.noData", area,
            "Not measured by the launch monitor", "not measured",
            QStringLiteral("A GC Quad reads the club from the reflective stickers on its face. With none fitted, or none "
                           "it could see, the ball numbers are still measured and good but every club number is unknown. "
                           "The device does not leave those cells empty: it fills them with a 'no data' marker "
                           "(16777215, sometimes converted into other units). Taken at face value that is nonsense "
                           "that looks like a number, so the reading is left out."),
            "The launch monitor sent its no-data marker[ ({raw})] instead of a value.",
            { GateEffect::Refused }, prod, design);
        e.tuning = { tune("lm.noDataMarker", 16777215.0, "") };
    }
    {
        add(c, "lm.outOfRange", area,
            "Outside what is physically possible", "impossible value",
            QStringLiteral("Every launch monitor field is held to a range several times wider than anything golf "
                           "produces (a clubhead speed of 0 to 300 mph, an angle of −90° to 90°, spin "
                           "within 30000 rpm). A value outside it is not a real outlier but a marker or a corrupt cell, "
                           "so it is left out rather than shown."),
            "The launch monitor sent {value}[ {unit}], outside {lo} to {hi}[ {unit}].",
            { GateEffect::Refused }, prod, design);
    }
    {
        add(c, "lm.noInputs", area,
            "Face and path not measured", "face and path not measured",
            QStringLiteral("The launch monitor works face to path out from face angle and club path. When it measured "
                           "neither, it still prints their difference as a clean 0, which a golfer would read as a face "
                           "square to the path. That number is left out."),
            "The launch monitor printed face to path as {value:deg} without measuring face angle or club path.",
            { GateEffect::Refused }, prod, design);
    }
}

} // namespace pinpoint::gates::detail
