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

#include <atomic>

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

// THE MODEL FILE'S FINGERPRINT, READ OUTSIDE THE STAGE (P11; refactor step 7.2). The pose stage names
// its model file (PoseEstimatorViTPose::modelPath) but never stats it: a composition root installs the
// reader once, at start-up (src/Export/model_file_fingerprint.h, readModelFileFingerprint: "<file>@<bytes>",
// 0 bytes when the file is missing), and the stage asks it each time it stamps versions.pose.model, so a
// model downloaded mid-session reads as it did before. With no reader installed the answer is empty, and
// PoseRunner::modelIdentity warns once and stamps "<file>@?", which no reader can take for a missing
// model ("@0"). The pattern is the norm slot's factory (7.1, J-104).
using ModelFileFingerprint = QString (*)(const QString &path);

inline std::atomic<ModelFileFingerprint> &modelFileFingerprintSlot()
{
    static std::atomic<ModelFileFingerprint> slot{ nullptr };
    return slot;
}

inline void installModelFileFingerprint(ModelFileFingerprint reader)
{
    modelFileFingerprintSlot().store(reader);
}

// The installed reader's answer for `path`; empty when none is installed.
inline QString modelFileFingerprint(const QString &path)
{
    const ModelFileFingerprint reader = modelFileFingerprintSlot().load();
    return reader ? reader(path) : QString();
}

} // namespace pinpoint::pose
