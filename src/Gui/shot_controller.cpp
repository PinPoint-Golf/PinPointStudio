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
const char *sourceName(ShotController::Source s)
{
    return QMetaEnum::fromType<ShotController::Source>()
        .valueToKey(static_cast<int>(s));
}

// ShotController::Source ↔ the arbiter's modality set. Manual has no arbiter
// modality (it commits directly); Pose rides the vision slot with Ball.
bool toArbSource(ShotController::Source s, pinpoint::ArbSource &out)
{
    switch (s) {
    case ShotController::Source::Acoustic: out = pinpoint::ArbSource::Acoustic; return true;
    case ShotController::Source::Imu:      out = pinpoint::ArbSource::Imu;      return true;
    case ShotController::Source::Ball:
    case ShotController::Source::Pose:     out = pinpoint::ArbSource::Ball;     return true;
    case ShotController::Source::Manual:   break;
    }
    return false;
}

ShotController::Source fromArbSource(pinpoint::ArbSource a)
{
    switch (a) {
    case pinpoint::ArbSource::Acoustic: return ShotController::Source::Acoustic;
    case pinpoint::ArbSource::Imu:      return ShotController::Source::Imu;
    case pinpoint::ArbSource::Ball:     return ShotController::Source::Ball;
    }
    return ShotController::Source::Manual;
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

    m_arbTimer.setSingleShot(true);
    connect(&m_arbTimer, &QTimer::timeout, this, &ShotController::onArbHoldExpired);

    m_lastArmed = armed();
}

bool ShotController::armed() const
{
    return m_buffer && m_buffer->isCapturing() && !m_processorBusy && !m_reviewActive;
}

void ShotController::setProcessorBusy(bool busy)
{
    if (m_processorBusy == busy)
        return;
    m_processorBusy = busy;
    reevaluateArmed();
}

void ShotController::setReviewActive(bool active)
{
    if (m_reviewActive == active)
        return;
    m_reviewActive = active;
    reevaluateArmed();
}

void ShotController::reevaluateArmed()
{
    const bool now = armed();
    if (now == m_lastArmed)
        return;
    m_lastArmed = now;
    // A hold window opened while armed is void once disarmed (capture
    // stopped, processor busy, review entered) — drop it.
    if (!now) {
        m_arbTimer.stop();
        m_arbiter.cancel();
    }
    emit armedChanged();
}

void ShotController::triggerShot(Source source, qint64 timestampUs)
{
    if (!armed()) {
        ppDebug() << "[ShotController] trigger ignored (not capturing or busy) — source"
                  << sourceName(source);
        return;
    }

    // A direct trigger supersedes any pending hold window, and the arbiter
    // refractory must still cover subsequent auto candidates.
    m_arbTimer.stop();
    m_arbiter.cancel();
    m_arbiter.noteCommit(static_cast<int64_t>(pinpoint::EventBuffer::nowMicros()));

    commitShot(source, timestampUs);
}

void ShotController::reportCandidate(Source source, qint64 estImpactUs, float confidence)
{
    pinpoint::ArbSource arbSrc;
    if (!toArbSource(source, arbSrc)) {
        triggerShot(source, estImpactUs);   // Manual never holds
        return;
    }

    if (!armed()) {
        ppDebug() << "[ShotController] candidate ignored (not capturing or busy) — source"
                  << sourceName(source);
        return;
    }

    const auto nowUs =
        static_cast<int64_t>(pinpoint::EventBuffer::nowMicros());
    const bool opened = m_arbiter.report(
        {arbSrc, static_cast<int64_t>(estImpactUs), confidence}, nowUs);

    ppDebug() << "[ShotController] candidate — source" << sourceName(source)
              << "est_t_us" << estImpactUs << "conf" << confidence
              << (opened ? "(hold window opened)" : "(joined window)");

    if (opened)
        m_arbTimer.start(m_arbiter.config().holdMs);
}

void ShotController::onArbHoldExpired()
{
    const auto nowUs =
        static_cast<int64_t>(pinpoint::EventBuffer::nowMicros());
    const pinpoint::ShotArbiter::Decision d = m_arbiter.decide(nowUs);
    if (!d.commit) {
        ppDebug() << "[ShotController] hold window expired — no commit";
        return;
    }

    ppInfo() << "[ShotController] arbiter commit —" << d.modalities
             << "modalit" << (d.modalities == 1 ? "y" : "ies")
             << "authoritative" << sourceName(fromArbSource(d.src))
             << "conf" << d.conf;
    commitShot(fromArbSource(d.src), d.t_us);
}

void ShotController::commitShot(Source source, qint64 timestampUs)
{
    if (!armed()) {
        ppDebug() << "[ShotController] commit ignored (not capturing or busy) — source"
                  << sourceName(source);
        return;
    }

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
