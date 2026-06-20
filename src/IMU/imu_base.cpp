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

#include <cmath>                        // std::sqrt / std::abs (stillness gate)

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
    m_initSeedAttempts = 0;   // re-evaluate the stillness gate for the new filter
}

ImuBase::QuaternionData ImuBase::fuseRawImu(float ax, float ay, float az,
                                            float gxRad, float gyRad, float gzRad)
{
    // Apply any pending runtime filter swap before touching m_fusion (this runs
    // on the packet-consumer thread, so the swap is race-free).
    applyPendingFilterSwap();

    // Integrate with the device's NOMINAL sample period rather than host-arrival
    // deltas: BLE delivers samples in bursts (several per connection interval),
    // so arrival deltas alias to ~0 within a burst and a large gap across it —
    // jitter injected exactly during fast motion. The configured rate is the true
    // average cadence and is burst-immune. (setNominalSampleRateHz keeps m_nominalDtS
    // in lockstep with the device rate.)
    const float dt = m_nominalDtS;

    // Stillness-gated seeding (R2-4): don't seed orientation from a sample taken
    // mid-motion — gravity would be wrong and the filter would converge slowly.
    // Wait for an approximately-still sample, with a bounded fallback so we always
    // seed eventually. Until seeded the filter holds identity (safe — see header).
    if (!m_fusion->initialized()) {
        const float aMag = std::sqrt(ax * ax + ay * ay + az * az);            // g
        const float gMag = std::sqrt(gxRad * gxRad + gyRad * gyRad + gzRad * gzRad); // rad/s
        const bool  still = std::abs(aMag - 1.0f) <= kInitAccelTolG
                            && gMag <= kInitGyroMaxRadps;
        if (!still && m_initSeedAttempts < kInitMaxSeedAttempts) {
            ++m_initSeedAttempts;
            return QuaternionData{ m_fusion->w(), m_fusion->x(),
                                   m_fusion->y(), m_fusion->z() };
        }
        m_fusion->initFromAccel(ax, ay, az);
        m_initSeedAttempts = 0;
    }
    m_fusion->update(ax, ay, az, gxRad, gyRad, gzRad, dt);

    return QuaternionData{ m_fusion->w(), m_fusion->x(), m_fusion->y(), m_fusion->z() };
}
