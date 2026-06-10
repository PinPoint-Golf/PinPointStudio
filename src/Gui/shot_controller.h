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

#include <QObject>

#include "event_buffer.h"

class SessionController;

// Central application-level shot trigger. Every shot source — the toolbar
// SHOT button today; IMU impact, pose, and ball detection later — calls
// triggerShot(). shotDetected is the single signal ShotProcessor (post-roll
// → buffer freeze → shot window → analysis ∥ export → replay) is driven from.
//
// On every accepted trigger a shot-marker event is written into the
// EventBuffer (source "shot_controller", schema "shot_marker_v1") so the
// precise impact instant lives on the same steady_clock-µs timeline as the
// video frames and IMU samples. A later captureSwingWindow() contains the
// marker via entriesFor(), letting the processor align the window to impact.
class ShotController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)

public:
    enum class Source { Manual, Imu, Pose, Ball, Acoustic };
    Q_ENUM(Source)

    // Marker payload stored in the EventBuffer ring (schema "shot_marker_v1").
    struct ShotMarker {
        uint32_t version;       // 1
        uint16_t source;        // ShotController::Source
        int16_t  session_type;  // SessionController::Type (-1 = none)
        int64_t  impact_ts_us;  // same value as the entry timestamp
    };
    static_assert(sizeof(ShotMarker) == 16, "shot_marker_v1 is 16 bytes");

    explicit ShotController(pinpoint::EventBuffer *buffer,
                            SessionController     *session,
                            QObject               *parent = nullptr);

    // True while the buffer is capturing AND the shot processor is idle — the
    // only state a shot can fire in. Every source inherits the gate.
    bool armed() const;

    // timestampUs: precise impact instant in EventBuffer::nowMicros() domain
    // (steady_clock µs). Default -1 → "now" — what the manual button uses.
    // Future IMU/pose detectors pass the exact sample timestamp of impact.
    Q_INVOKABLE void triggerShot(Source source = Source::Manual,
                                 qint64 timestampUs = -1);

public slots:
    // Connected to CameraManager::bufferStateChanged (the single always-notified
    // buffer-state signal) so the armed property tracks every net transition.
    void reevaluateArmed();

    // Connected to ShotProcessor::busyChanged in main.cpp — disarms the
    // trigger for the whole post-roll → processing → replay pipeline.
    void setProcessorBusy(bool busy);

    // Connected to SessionReviewController::reviewActiveChanged in main.cpp.
    // Entering review already stops live capture (which disarms via the buffer
    // state); this is the explicit belt-and-braces gate so no shot source can
    // fire while a saved session is loaded.
    void setReviewActive(bool active);

signals:
    // sessionType: SessionController::Type active at the moment of the shot
    // (-1 = none — unreachable through the UI, where capture implies a session).
    void shotDetected(ShotController::Source source, qint64 timestampUs, int sessionType);
    void armedChanged();

private:
    void writeShotMarker(Source source, int64_t impactUs, int sessionType);

    pinpoint::EventBuffer *m_buffer   = nullptr;
    SessionController     *m_session  = nullptr;
    pinpoint::SourceId     m_sourceId = pinpoint::kInvalidSourceId;
    bool                   m_processorBusy = false;
    bool                   m_reviewActive  = false;
    bool                   m_lastArmed = false;
};
