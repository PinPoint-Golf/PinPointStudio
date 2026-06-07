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

#include "shot_controller.h"
#include "session_controller.h"
#include "pp_debug.h"

#include <QMetaEnum>
#include <chrono>
#include <cstring>

namespace {
// Ignore re-triggers inside this window — placeholder until the shot
// processor adds a real busy state.
constexpr qint64 kDebounceMs = 500;

const char *sourceName(ShotController::Source s)
{
    return QMetaEnum::fromType<ShotController::Source>()
        .valueToKey(static_cast<int>(s));
}
} // namespace

ShotController::ShotController(pinpoint::EventBuffer *buffer,
                               SessionController     *session,
                               QObject               *parent)
    : QObject(parent)
    , m_buffer(buffer)
    , m_session(session)
{
    if (m_buffer) {
        pinpoint::SourceDescriptor desc;
        desc.name       = "Shot Marker";
        desc.identifier = "shot_controller";   // singleton app-level source

        pinpoint::ImuFormat fmt{};             // fixed-size-packet descriptor
        fmt.device         = pinpoint::DeviceKind::Marker_App;
        fmt.sample_rate_hz = 2;                // sizes ring: ceil(2×5s)=10 → 16 slots
        fmt.packet_bytes   = sizeof(ShotMarker);
        fmt.packet_schema  = "shot_marker_v1";

        desc.format.device            = pinpoint::DeviceKind::Marker_App;
        desc.format.format            = fmt;
        desc.window_duration          = std::chrono::milliseconds(5000);
        desc.expected_interarrival_us = std::chrono::microseconds(0); // sporadic — no stall watchdog
        desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;

        // registerSource requires Idle or Paused. Registering the first source
        // auto-resumes the buffer — main.cpp restores the user capture intent
        // via cameraManager.applyCaptureIntent() right after construction.
        if (m_buffer->state() == pinpoint::BufferState::Capturing)
            m_buffer->pause();
        m_sourceId = m_buffer->registerSource(desc);
    }

    m_lastArmed = armed();
}

bool ShotController::armed() const
{
    return m_buffer && m_buffer->isCapturing();
}

void ShotController::reevaluateArmed()
{
    const bool now = armed();
    if (now == m_lastArmed)
        return;
    m_lastArmed = now;
    emit armedChanged();
}

void ShotController::triggerShot(Source source, qint64 timestampUs)
{
    if (!armed()) {
        ppDebug() << "[ShotController] trigger ignored (not capturing) — source"
                  << sourceName(source);
        return;
    }
    if (m_sinceLastShot.isValid() && m_sinceLastShot.elapsed() < kDebounceMs) {
        ppDebug() << "[ShotController] trigger debounced — source" << sourceName(source);
        return;
    }
    m_sinceLastShot.restart();

    const qint64 impactUs = timestampUs >= 0 ? timestampUs
                                             : pinpoint::EventBuffer::nowMicros();

    // Session type captured at the moment of the shot (-1 = no active session).
    const int sessionType = m_session ? m_session->activeSessionType() : -1;

    // Marker must be in the ring before the signal — the shot processor will
    // pause/freeze the buffer from shotDetected.
    writeShotMarker(source, impactUs, sessionType);

    ppInfo() << "[ShotController] shot detected — source" << sourceName(source)
             << "impact_ts_us" << impactUs << "sessionType" << sessionType;
    emit shotDetected(source, impactUs, sessionType);
}

void ShotController::writeShotMarker(Source source, int64_t impactUs, int sessionType)
{
    if (!m_buffer || m_sourceId == pinpoint::kInvalidSourceId)
        return;

    auto slot = m_buffer->acquireWriteSlot(m_sourceId);
    if (!slot.valid || slot.capacity < sizeof(ShotMarker)) {
        ppWarn() << "[ShotController] shot marker dropped — no valid write slot";
        return;
    }

    const ShotMarker marker{1, static_cast<uint16_t>(source),
                            static_cast<int16_t>(sessionType), impactUs};
    std::memcpy(slot.data, &marker, sizeof marker);
    *slot.bytes_written = sizeof(ShotMarker);
    *slot.timestamp_us  = impactUs;            // entry timestamp IS the impact instant
    m_buffer->publish(m_sourceId, slot.sequence);
}
