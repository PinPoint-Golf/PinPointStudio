/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "swing_window.h"
#include "imu_sample.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pinpoint {

// The resume/deregister guard (EventBuffer::swing_window_live_) is owned by the
// ring backing (RingPayloadSource), set/cleared from its ctor/dtor — so a moved
// window keeps the guard held until its source is finally destroyed, with no
// special-casing here. A disk-backed source has no such guard.
SwingWindow::SwingWindow(std::unique_ptr<const SwingPayloadSource> source,
                         std::vector<IndexEntry> entries,
                         int64_t start_us,
                         int64_t end_us)
    : source_(std::move(source))
    , entries_(std::move(entries))
    , start_us_(start_us)
    , end_us_(end_us)
{
}

SwingWindow::~SwingWindow() = default;

SwingWindow::SwingWindow(SwingWindow&& o) noexcept = default;

SwingWindow& SwingWindow::operator=(SwingWindow&& o) noexcept = default;

std::vector<IndexEntry> SwingWindow::entriesFor(SourceId id) const {
    std::vector<IndexEntry> out;
    for (const auto& e : entries_) {
        if (e.source_id == id) out.push_back(e);
    }
    return out;
}

size_t SwingWindow::frameCount(SourceId camera_id) const noexcept {
    size_t n = 0;
    for (const auto& e : entries_)
        if (e.source_id == camera_id) ++n;
    return n;
}

size_t SwingWindow::imuSampleCount(SourceId imu_id) const noexcept {
    return frameCount(imu_id); // same logic — count by source
}

SourceRing::ReadHandle SwingWindow::payloadOf(const IndexEntry& e) const noexcept {
    if (!source_) return {};
    return source_->payloadOf(e.source_id, e.source_sequence);
}

const FormatDescriptor& SwingWindow::formatOf(SourceId id) const noexcept {
    return source_->formatOf(id);
}

bool SwingWindow::interpolateImu(SourceId imu_id, int64_t target_us,
                                  std::byte* out, size_t out_bytes) const noexcept {
    if (!out || out_bytes != sizeof(ImuSample)) return false;

    // Find the nearest entry before and after target_us for this source.
    const IndexEntry* prev = nullptr;
    const IndexEntry* next = nullptr;

    for (const auto& e : entries_) {
        if (e.source_id != imu_id) continue;
        if (e.timestamp_us <= target_us) {
            if (!prev || e.timestamp_us > prev->timestamp_us) prev = &e;
        } else {
            if (!next || e.timestamp_us < next->timestamp_us) next = &e;
        }
    }

    if (!prev || !next) return false;

    SourceRing::ReadHandle prev_h = payloadOf(*prev);
    SourceRing::ReadHandle next_h = payloadOf(*next);

    if (!prev_h.data || !next_h.data) return false;
    if (prev_h.bytes != sizeof(ImuSample) || next_h.bytes != sizeof(ImuSample)) return false;

    int64_t denom = next->timestamp_us - prev->timestamp_us;
    if (denom == 0) {
        std::memcpy(out, prev_h.data, sizeof(ImuSample));
        return true;
    }

    float t = static_cast<float>(target_us - prev->timestamp_us)
            / static_cast<float>(denom);

    const auto& p = *reinterpret_cast<const ImuSample*>(prev_h.data);
    const auto& n = *reinterpret_cast<const ImuSample*>(next_h.data);
    auto&       o = *reinterpret_cast<ImuSample*>(out);

    // Linear interpolation for accel and gyro (linear quantities).
    o.accel_x = p.accel_x + t * (n.accel_x - p.accel_x);
    o.accel_y = p.accel_y + t * (n.accel_y - p.accel_y);
    o.accel_z = p.accel_z + t * (n.accel_z - p.accel_z);
    o.gyro_x  = p.gyro_x  + t * (n.gyro_x  - p.gyro_x);
    o.gyro_y  = p.gyro_y  + t * (n.gyro_y  - p.gyro_y);
    o.gyro_z  = p.gyro_z  + t * (n.gyro_z  - p.gyro_z);

    // Spherical linear interpolation for the unit quaternion.
    // Ensures the shortest-path arc is taken and the result stays normalised.
    float pw = p.quat_w, px = p.quat_x, py = p.quat_y, pz = p.quat_z;
    float nw = n.quat_w, nx = n.quat_x, ny = n.quat_y, nz = n.quat_z;

    float dot = pw*nw + px*nx + py*ny + pz*nz;
    if (dot < 0.0f) { nw=-nw; nx=-nx; ny=-ny; nz=-nz; dot=-dot; }

    if (dot > 0.9995f) {
        // Quaternions nearly identical — normalised lerp avoids acos instability.
        float rw = pw + t*(nw-pw), rx = px + t*(nx-px);
        float ry = py + t*(ny-py), rz = pz + t*(nz-pz);
        float len = std::sqrt(rw*rw + rx*rx + ry*ry + rz*rz);
        o.quat_w = rw/len; o.quat_x = rx/len;
        o.quat_y = ry/len; o.quat_z = rz/len;
    } else {
        float theta0    = std::acos(dot);
        float sinTheta0 = std::sin(theta0);
        float s0 = std::sin((1.0f - t) * theta0) / sinTheta0;
        float s1 = std::sin(t * theta0)           / sinTheta0;
        o.quat_w = s0*pw + s1*nw; o.quat_x = s0*px + s1*nx;
        o.quat_y = s0*py + s1*ny; o.quat_z = s0*pz + s1*nz;
    }

    return source_->validate(imu_id, prev_h)
        && source_->validate(imu_id, next_h);
}

} // namespace pinpoint
