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
        QQuaternion last;            // hold-last fallback for a momentary gap
        bool haveLast = false;
        for (const int64_t t : out.timeGrid) {
            ImuSample smp{};
            QQuaternion qAnat;
            if (window.interpolateImu(bd.b->source, t,
                                      reinterpret_cast<std::byte *>(&smp), sizeof(smp))) {
                const QQuaternion qRaw(smp.quat_w, smp.quat_x, smp.quat_y, smp.quat_z);
                qAnat    = (bd.b->alignA * qRaw * bd.b->mountM).normalized();
                last     = qAnat;
                haveLast = true;
            } else {
                qAnat = haveLast ? last : QQuaternion();   // identity until first valid sample
            }
            s.qAnat.push_back(qAnat);
        }
        out.segments.push_back(std::move(s));
    }
    return out;
}

} // namespace pinpoint::analysis
