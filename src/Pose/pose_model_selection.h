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

// Tier -> pose-model decision for the offline analysis pose pass. A pure function
// of the "Motion capture quality" setting (AppSettings::motionCaptureQuality:
// "Medium"/"High" — the vestigial "Low" tier was removed 2026-07-13) and whether
// the ViTPose++-L model has been downloaded.
//
// Deliberately ORT/OpenCV-free so it can be unit-tested standalone (the Pose test
// suite excludes ONNX Runtime) and shared between PoseRunner (which builds the
// estimator) and MotionCaptureProbe (which benchmarks the tiers).

#include <QString>

namespace pinpoint::pose {

// True iff the High tier should run ViTPose++-L. Falls back to ViTPose-B when the
// quality is not "High" (case-insensitive) or the L model is not present yet
// (e.g. the user has not accepted the one-time on-demand download). Keeping the
// fallback here means every caller degrades identically and safely.
inline bool useVitPoseLarge(const QString &quality, bool largeAvailable)
{
    return largeAvailable
           && quality.compare(QLatin1String("High"), Qt::CaseInsensitive) == 0;
}

} // namespace pinpoint::pose
