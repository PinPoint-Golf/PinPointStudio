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

#include <QAudioFormat>
#include <QObject>

#include "onset_detector.h"

#include <atomic>

// Acoustic shot detector (shot detection P2) — thin QObject around the pure
// OnsetDetector math. A *second consumer* of AudioInputBase::audioDataReady
// (the AudioStreamSaver pattern), running at the device's native rate in
// parallel with STT; adding it never disturbs the STT pipeline.
//
// Owned by TranscriptionController and moved to its audio thread, so
// onAudioData runs as a same-thread direct call: nowMicros() is stamped at
// buffer receipt (small, stable latency — this is what makes audio the
// pinpoint modality) and est_t* is computed before any thread hop.
// impactDetected is emitted on the audio thread; consumers connect with
// their own context object and get the queued hop automatically.
class AcousticShotDetector : public QObject
{
    Q_OBJECT

public:
    explicit AcousticShotDetector(QObject *parent = nullptr);

    // Fixed capture-chain delay between a sound reaching the microphone and
    // its buffer arriving here (device + driver period). Atomic — set from
    // the GUI thread (AppSettings) while onAudioData reads it on the audio
    // thread. A measured-ish constant until P4 auto-calibration.
    void setDeviceLatencyUs(qint64 us) { m_deviceLatencyUs.store(us, std::memory_order_relaxed); }

public slots:
    // Audio-thread context (direct connection from audioDataReady).
    void onAudioData(const QByteArray &data, const QAudioFormat &format);

signals:
    // estImpactUs is the back-dated true-impact estimate in
    // EventBuffer::nowMicros() domain — sample-accurate up to the device
    // latency constant.
    void impactDetected(qint64 estImpactUs, float confidence);

private:
    pinpoint::OnsetDetector m_detector;
    QAudioFormat            m_format;       // last-seen; reset() on change
    std::atomic<qint64>     m_deviceLatencyUs { 20'000 };
};
