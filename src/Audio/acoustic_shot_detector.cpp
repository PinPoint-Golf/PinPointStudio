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

#include "acoustic_shot_detector.h"

#include "event_buffer.h"

#include <cstdint>

namespace {

// Channel-0 sample → float in [-1, 1]. The onset math only needs one
// channel; mixing adds nothing for a transient detector.
inline float sampleToFloat(const char *frame, QAudioFormat::SampleFormat fmt)
{
    switch (fmt) {
    case QAudioFormat::Float:
        return *reinterpret_cast<const float *>(frame);
    case QAudioFormat::Int16:
        return static_cast<float>(*reinterpret_cast<const int16_t *>(frame)) / 32768.0f;
    case QAudioFormat::Int32:
        return static_cast<float>(*reinterpret_cast<const int32_t *>(frame)) / 2147483648.0f;
    case QAudioFormat::UInt8:
        return (static_cast<float>(*reinterpret_cast<const uint8_t *>(frame)) - 128.0f) / 128.0f;
    default:
        return 0.0f;
    }
}

} // namespace

AcousticShotDetector::AcousticShotDetector(QObject *parent)
    : QObject(parent)
{
}

void AcousticShotDetector::onAudioData(const QByteArray &data, const QAudioFormat &format)
{
    // Stamp FIRST — recvNow anchors every sample in the buffer to the
    // steady_clock timeline before any processing cost is incurred.
    const qint64 recvNow =
        static_cast<qint64>(pinpoint::EventBuffer::nowMicros());

    if (!format.isValid() || format.sampleRate() <= 0)
        return;
    if (format != m_format) {
        m_format = format;
        m_detector.reset(format.sampleRate());
    }

    const int bytesPerSample = format.bytesPerSample();
    const int bytesPerFrame  = format.bytesPerFrame();
    if (bytesPerSample <= 0 || bytesPerFrame <= 0)
        return;
    const int frames = static_cast<int>(data.size()) / bytesPerFrame;
    const QAudioFormat::SampleFormat sfmt = format.sampleFormat();
    const char *p = data.constData();

    // Collect first, emit after the whole buffer is pushed: est_t* measures
    // the onset back from the buffer END (whose capture time recvNow
    // approximates, minus the device latency).
    struct Pending { int64_t sample; float confidence; };
    Pending pending[4];
    int     nPending = 0;

    for (int f = 0; f < frames; ++f, p += bytesPerFrame) {
        const pinpoint::OnsetResult r = m_detector.push(sampleToFloat(p, sfmt));
        if (r.onset && nPending < 4)
            pending[nPending++] = { r.onsetSample, r.confidence };
    }

    const int64_t end     = m_detector.samplesProcessed();
    const qint64  latency = m_deviceLatencyUs.load(std::memory_order_relaxed);
    for (int i = 0; i < nPending; ++i) {
        const qint64 est = pinpoint::estimateImpactUs(
            recvNow, end - pending[i].sample, m_detector.sampleRate(), latency);
        emit impactDetected(est, pending[i].confidence);
    }
}
