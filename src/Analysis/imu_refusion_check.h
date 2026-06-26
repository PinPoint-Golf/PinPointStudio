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

#include <algorithm>
#include <cstring>
#include <variant>
#include <vector>

#include "swing_window.h"
#include "imu_sample.h"
#include "format_descriptor.h"

#include "../IMU/orientation_filter.h"     // MadgwickFilter (header-only)
#include "../IMU/orientation_refuser.h"    // refuseOrientation / parity

// IMU data-integrity check via offline orientation RE-FUSION.
//
// Re-runs the Madgwick filter from each IMU source's recorded raw accel+gyro,
// warm-started from the stored quaternion at the window's first sample, and
// compares the re-fused trajectory to the stored live quaternion. Because
// Madgwick is deterministic and memoryless (state == the quaternion), warm-start
// re-fusion of a faithfully-recorded swing reproduces the stored quaternion to
// float precision. A LARGE disagreement therefore means the recorded raw vectors
// do NOT correspond to the stored quaternion — the IMU record is internally
// inconsistent, so the swing cannot be re-fused/re-analysed offline (and the
// stored orientation itself is suspect). The app surfaces this as a per-shot
// data-integrity warning on the carousel; SwingLab's swinglab_run exposes the
// same check as --refuse-orientation. See
// docs/validation/pipeline_validation_and_tuning.md §5.2/§5.3.1.
//
// Madgwick only: it is the production default and the only filter that warm-starts
// EXACTLY (ESKF's vendored reference quaternion is not settable). A swing captured
// under ESKF cannot be re-fused exactly, so the caller should only run this when
// the live orientation filter is Madgwick (else a false warning would fire).
//
// Header-only and free of Qt so it is reusable by the GUI (ShotProcessor) and the
// offline tools alike.
namespace pinpoint {

struct ImuRefusionVerdict {
    bool   ok             = true;   // false only when a checkable source exceeded the threshold
    int    sourcesChecked = 0;      // IMU sources we could warm-start and compare
    double worstMaxDeg    = 0.0;    // worst per-sample geodesic disagreement (deg)
    double thresholdDeg   = 0.5;    // fail boundary

    // A data warning is raised only when we actually checked a source and it failed.
    bool warns() const { return sourcesChecked > 0 && !ok; }
};

// Re-fuse every IMU source in the frozen window and judge parity vs the stored
// quaternion. `beta < 0` uses the Madgwick production default.
inline ImuRefusionVerdict checkImuRefusion(const SwingWindow &window,
                                           double thresholdDeg = 0.5,
                                           float  beta = -1.0f)
{
    ImuRefusionVerdict v;
    v.thresholdDeg = thresholdDeg;
    const float useBeta = beta > 0.0f ? beta : MadgwickFilter().beta();

    // Enumerate IMU sources straight from the window (binding-independent: works
    // even when analysis.bindings is empty — the samples are still present).
    std::vector<SourceId> imuIds;
    for (const IndexEntry &e : window.entries())
        if (std::holds_alternative<ImuFormat>(window.formatOf(e.source_id).format)
            && std::find(imuIds.begin(), imuIds.end(), e.source_id) == imuIds.end())
            imuIds.push_back(e.source_id);

    for (const SourceId sid : imuIds) {
        const std::vector<IndexEntry> entries = window.entriesFor(sid);
        if (entries.size() < 2)
            continue;

        // Nominal cadence -> dt = 1/rate, matching ImuBase::fuseRawImu.
        float rate = 200.0f;
        if (const auto *imf = std::get_if<ImuFormat>(&window.formatOf(sid).format))
            if (imf->sample_rate_hz > 0)
                rate = float(imf->sample_rate_hz);

        std::vector<RefuseSample> samples;
        samples.reserve(entries.size());
        for (const IndexEntry &e : entries) {
            const SourceRing::ReadHandle h = window.payloadOf(e);
            if (!h.data || h.bytes < sizeof(ImuSample))
                continue;
            ImuSample s;
            std::memcpy(&s, h.data, sizeof(ImuSample));   // alignment-safe
            samples.push_back(RefuseSample{ e.timestamp_us,
                                            s.accel_x, s.accel_y, s.accel_z,
                                            s.gyro_x,  s.gyro_y,  s.gyro_z,
                                            s.quat_w,  s.quat_x,  s.quat_y, s.quat_z });
        }
        if (samples.size() < 2)
            continue;

        RefuseConfig cfg;
        cfg.outputRateHz = rate;
        cfg.warmStart    = true;
        MadgwickFilter filt(useBeta);
        const RefuseResult r = refuseOrientation(filt, samples, cfg);
        if (!r.warmStarted)
            continue;   // not checkable (shouldn't happen for Madgwick)

        const ParityStats p = parity(samples, r);
        ++v.sourcesChecked;
        if (p.maxDeg > v.worstMaxDeg)
            v.worstMaxDeg = p.maxDeg;
        if (p.maxDeg >= thresholdDeg)
            v.ok = false;
    }
    return v;
}

} // namespace pinpoint
