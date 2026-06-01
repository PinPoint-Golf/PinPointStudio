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

#include "imu_base.h"

#include "orientation_filter.h"        // MadgwickFilter (concrete)
#include "eskf_orientation_filter.h"   // EskfOrientationFilter (concrete)
#include "event_buffer.h"              // nowMicros()

ImuBase::ImuBase(QObject *parent)
    : QObject(parent)
    , m_fusion(std::make_unique<MadgwickFilter>())   // default; swappable at runtime
{}

void ImuBase::applyPendingFilterSwap()
{
    if (!m_filterSwapPending.exchange(false, std::memory_order_acquire))
        return;
    switch (m_pendingFilterType.load(std::memory_order_relaxed)) {
    case OrientationFilterType::Eskf:
        m_fusion = std::make_unique<EskfOrientationFilter>();
        break;
    case OrientationFilterType::Madgwick:
        m_fusion = std::make_unique<MadgwickFilter>();
        break;
    }
    m_lastFusionUs = 0;   // re-seed dt and (via uninitialised filter) orientation
}

ImuBase::QuaternionData ImuBase::fuseRawImu(float ax, float ay, float az,
                                            float gxRad, float gyRad, float gzRad)
{
    // Apply any pending runtime filter swap before touching m_fusion (this runs
    // on the packet-consumer thread, so the swap is race-free).
    applyPendingFilterSwap();

    // Per-sample dt from the software clock; clamp to absorb transport stalls/gaps.
    const qint64 nowUs = static_cast<qint64>(pinpoint::EventBuffer::nowMicros());
    float dt = (m_lastFusionUs != 0) ? (nowUs - m_lastFusionUs) * 1e-6f : 0.01f;
    m_lastFusionUs = nowUs;
    if (dt < 0.0005f) dt = 0.0005f;
    if (dt > 0.05f)   dt = 0.05f;

    if (!m_fusion->initialized())
        m_fusion->initFromAccel(ax, ay, az);
    m_fusion->update(ax, ay, az, gxRad, gyRad, gzRad, dt);

    return QuaternionData{ m_fusion->w(), m_fusion->x(), m_fusion->y(), m_fusion->z() };
}
