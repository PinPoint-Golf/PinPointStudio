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

#include "imu_vision_fuser.h"

#include <algorithm>
#include <cstddef>

#include "imu_calibration.h"   // toAnatomical (shared A*q*M)
#include "imu_sample.h"
#include "swing_window.h"

namespace pinpoint::analysis {

FusedStreams ImuVisionFuser::fuse(const SwingWindow &window,
                                  const std::vector<ImuSegmentBinding> &bindings,
                                  double gridHz)
{
    FusedStreams out;

    // Gather usable bindings (known role + ≥2 samples) and intersect their coverage
    // with the window span so every grid instant is interpolatable for every segment.
    struct Bound { const ImuSegmentBinding *b; };
    std::vector<Bound> bound;
    int64_t gridStart = window.startTimestampUs();
    int64_t gridEnd   = window.endTimestampUs();
    for (const ImuSegmentBinding &b : bindings) {
        if (b.role == SegmentRole::Unknown)
            continue;
        const std::vector<IndexEntry> entries = window.entriesFor(b.source);
        if (entries.size() < 2)
            continue;
        gridStart = std::max(gridStart, entries.front().timestamp_us);
        gridEnd   = std::min(gridEnd,   entries.back().timestamp_us);
        bound.push_back({ &b });
    }
    if (bound.empty() || gridEnd <= gridStart)
        return out;

    const int64_t dt = static_cast<int64_t>(1.0e6 / gridHz + 0.5);
    if (dt <= 0)
        return out;
    for (int64_t t = gridStart; t <= gridEnd; t += dt)
        out.timeGrid.push_back(t);

    for (const Bound &bd : bound) {
        SegmentStream s;
        s.role = bd.b->role;
        s.qAnat.reserve(out.timeGrid.size());
        s.gyroDps.reserve(out.timeGrid.size());
        s.accelG.reserve(out.timeGrid.size());
        // ImuSample vectors are RAW sensor-frame (imu_sample.h v2); rotate them
        // into the anatomical segment frame (v_anat = M⁻¹·v_sensor) so they share
        // qAnat's body frame. M is unit, so conjugated() is its inverse.
        const QQuaternion mountInv = bd.b->mountM.conjugated();
        QQuaternion last;            // hold-last fallback for a momentary gap
        QVector3D   lastGyro, lastAccel;
        bool haveLast = false;
        for (const int64_t t : out.timeGrid) {
            ImuSample smp{};
            QQuaternion qAnat;
            QVector3D   gyro, accel;
            if (window.interpolateImu(bd.b->source, t,
                                      reinterpret_cast<std::byte *>(&smp), sizeof(smp))) {
                const QQuaternion qRaw(smp.quat_w, smp.quat_x, smp.quat_y, smp.quat_z);
                qAnat     = imu_calibration::toAnatomical(bd.b->alignA, qRaw, bd.b->mountM);
                gyro      = mountInv.rotatedVector(QVector3D(smp.gyro_x, smp.gyro_y, smp.gyro_z));
                accel     = mountInv.rotatedVector(QVector3D(smp.accel_x, smp.accel_y, smp.accel_z));
                last      = qAnat;
                lastGyro  = gyro;
                lastAccel = accel;
                haveLast  = true;
            } else {
                qAnat = haveLast ? last : QQuaternion();   // identity until first valid sample
                gyro  = lastGyro;                          // zero until first valid sample
                accel = lastAccel;
            }
            s.qAnat.push_back(qAnat);
            s.gyroDps.push_back(gyro);
            s.accelG.push_back(accel);
        }
        out.segments.push_back(std::move(s));
    }
    return out;
}

} // namespace pinpoint::analysis
