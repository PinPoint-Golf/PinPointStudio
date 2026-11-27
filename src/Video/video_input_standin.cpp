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

#include "video_input_standin.h"

#include "event_buffer.h"   // pinpoint::EventBuffer::nowMicros: the clock every frame is stamped on

#include <QTimer>
#include <QVideoFrame>

#include <cstdlib>

VideoInputStandIn::VideoInputStandIn(QObject *parent) : VideoInputBase(parent) {}

VideoInputStandIn::~VideoInputStandIn() = default;

bool VideoInputStandIn::start(const QString &)
{
    // Made here, on the thread start() runs on (the camera instance's capture thread), so its ticks land there.
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(int(1000.0 / kFps));
        connect(m_timer, &QTimer::timeout, this, &VideoInputStandIn::drawFrame);
    }
    m_timer->start();
    m_state = State::Active;
    emit stateChanged(m_state);
    return true;
}

void VideoInputStandIn::stop()
{
    if (m_timer) m_timer->stop();
    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(m_state);
    }
}

void VideoInputStandIn::suspend()
{
    if (m_timer) m_timer->stop();
    m_state = State::Suspended;
    emit stateChanged(m_state);
}

void VideoInputStandIn::resume()
{
    if (m_timer) m_timer->start();
    m_state = State::Active;
    emit stateChanged(m_state);
}

bool VideoInputStandIn::isActive() const { return m_state == State::Active; }

QVideoFrameFormat VideoInputStandIn::frameFormat() const
{
    return QVideoFrameFormat(QSize(kWidth, kHeight), QVideoFrameFormat::Format_BGRA8888);
}

CameraCapabilities VideoInputStandIn::capabilities()
{
    CameraCapabilities caps;
    caps.vendorName   = QStringLiteral("PinPoint");
    caps.modelName    = QStringLiteral("Stand-in camera");
    caps.serialNumber = QStringLiteral("standin-0");
    caps.connectionInterface = CameraCapabilities::Interface::Virtual;
    caps.isVirtual = true;
    caps.resolution.kind = CapabilityKind::Discrete;
    caps.resolution.presets = { Resolution{ kWidth, kHeight } };
    caps.resolution.defaultResolution = Resolution{ kWidth, kHeight };
    const PixelFormat bgra{ PixelEncoding::BGR8, 32, QStringLiteral("BGRA") };
    caps.pixelFormat.kind = CapabilityKind::Fixed;
    caps.pixelFormat.supported = { bgra };
    caps.pixelFormat.defaultFormat = bgra;
    caps.frameRate.kind = CapabilityKind::Fixed;
    caps.frameRate.fixedValue = kFps;
    caps.frameRate.range.min = caps.frameRate.range.max = caps.frameRate.range.defaultValue = kFps;
    caps.frameRate.readable = true;
    return caps;
}

CameraCapabilities VideoInputStandIn::queryCapabilities() const { return capabilities(); }

void VideoInputStandIn::drawFrame()
{
    QVideoFrame frame(frameFormat());
    if (!frame.map(QVideoFrame::WriteOnly))
        return;
    uchar *bits = frame.bits(0);
    const int stride = frame.bytesPerLine(0);
    const int bar    = int(m_frames % kWidth);   // the bright bar's column, one step a frame
    for (int y = 0; y < kHeight; ++y) {
        uchar *row = bits + y * stride;
        for (int x = 0; x < kWidth; ++x) {
            const bool lit = std::abs(x - bar) < 8;
            const uchar v = lit ? 230 : uchar(20 + (y * 40) / kHeight);
            row[4 * x + 0] = v; row[4 * x + 1] = v; row[4 * x + 2] = v; row[4 * x + 3] = 255;
        }
    }
    frame.unmap();
    m_lastFrameUs = pinpoint::EventBuffer::nowMicros();
    ++m_frames;
    emit videoFrameReady(frame, FrameTiming());
}
