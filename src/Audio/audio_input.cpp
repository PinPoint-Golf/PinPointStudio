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

#include "audio_input.h"
#include "../Core/device_enumerator.h"

#include <QAudioSource>
#include <QMediaDevices>

// ---------------------------------------------------------------------------
// AudioCaptureDevice
//
// A write-only QIODevice wired between QAudioSource and our signal.
// QAudioSource::start(device) calls writeData() on whichever thread the
// platform audio backend uses; the signal is connected with AutoConnection
// so cross-thread delivery becomes a queued connection automatically.
// ---------------------------------------------------------------------------

class AudioCaptureDevice : public QIODevice
{
    Q_OBJECT

public:
    explicit AudioCaptureDevice(QObject *parent = nullptr)
        : QIODevice(parent) {}

    bool open()
    {
        return QIODevice::open(QIODevice::WriteOnly);
    }

signals:
    void dataReady(const QByteArray &data);

protected:
    qint64 readData(char *, qint64) override
    {
        return -1;
    }

    qint64 writeData(const char *data, qint64 len) override
    {
        emit dataReady(QByteArray(data, static_cast<int>(len)));
        return len;
    }
};

// AudioCaptureDevice is defined in this .cpp, so its MOC output must be
// included here for the meta-object system to pick it up.
#include "audio_input.moc"

// ---------------------------------------------------------------------------
// AudioInput
// ---------------------------------------------------------------------------

AudioInput::AudioInput(QObject *parent)
    : AudioInputBase(parent)
{
    // Register devices the first time an instance is created
    availableDevices();

    // Leave m_preferredFormat default (invalid) so start() uses the device's
    // own preferred format and the FFmpeg backend does no conversion.
    // Forced resampling (e.g. 44100 Float → 16000 Int16) in small chunks
    // produces aliasing artefacts on the built-in MacBook microphone.
}

AudioInput::~AudioInput()
{
    stop();
}

QList<QAudioDevice> AudioInput::availableDevices()
{
    const QList<QAudioDevice> devs = QMediaDevices::audioInputs();
    for (const auto &dev : devs) {
        DeviceEnumerator::instance()->registerDevice(
            DeviceType::AudioInput, VideoInputFactory::Backend::QtMultimedia,
            dev.id(), dev.description());
    }
    return devs;
}

bool AudioInput::start(const QString &deviceName)
{
    stop();

    // ---- Select device -------------------------------------------------------
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (devices.isEmpty()) {
        emit errorOccurred(tr("No audio input devices available"));
        return false;
    }

    m_activeDevice = QMediaDevices::defaultAudioInput();
    if (!deviceName.isEmpty()) {
        for (const QAudioDevice &dev : devices) {
            if (dev.description() == deviceName) {
                m_activeDevice = dev;
                break;
            }
        }
    }

    // ---- Negotiate format ----------------------------------------------------
    QAudioFormat fmt;
    if (m_preferredFormat.isValid() && m_activeDevice.isFormatSupported(m_preferredFormat))
        fmt = m_preferredFormat;
    else
        fmt = m_activeDevice.preferredFormat();

    // ---- Open capture pipeline -----------------------------------------------
    m_captureDevice = new AudioCaptureDevice(this);
    if (!m_captureDevice->open()) {
        emit errorOccurred(tr("Failed to open internal capture device"));
        delete m_captureDevice;
        m_captureDevice = nullptr;
        return false;
    }
    connect(m_captureDevice, &AudioCaptureDevice::dataReady,
            this, &AudioInput::onCapturedData);

    m_source = new QAudioSource(m_activeDevice, fmt, this);
    connect(m_source, &QAudioSource::stateChanged,
            this, &AudioInput::onSourceStateChanged);

    m_source->start(m_captureDevice);

    if (m_source->error() != QAudio::NoError) {
        emit errorOccurred(tr("QAudioSource failed to start (error %1)")
                               .arg(static_cast<int>(m_source->error())));
        stop();
        return false;
    }

    return true;
}

void AudioInput::stop()
{
    if (m_source) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
    }
    if (m_captureDevice) {
        m_captureDevice->close();
        delete m_captureDevice;
        m_captureDevice = nullptr;
    }
    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void AudioInput::suspend()
{
    if (m_source && m_state == State::Active)
        m_source->suspend();
}

void AudioInput::resume()
{
    if (m_source && m_state == State::Suspended)
        m_source->resume();
}

bool AudioInput::isActive() const
{
    return m_state == State::Active;
}

QAudioFormat AudioInput::format() const
{
    return m_source ? m_source->format() : m_preferredFormat;
}

void AudioInput::setPreferredFormat(const QAudioFormat &format)
{
    m_preferredFormat = format;
}

void AudioInput::setVolume(qreal volume)
{
    if (m_source)
        m_source->setVolume(volume);
}

qreal AudioInput::volume() const
{
    return m_source ? m_source->volume() : 1.0;
}

void AudioInput::onSourceStateChanged(QAudio::State qtState)
{
    State next = m_state;

    switch (qtState) {
    case QAudio::ActiveState:
        next = State::Active;
        break;
    case QAudio::SuspendedState:
        next = State::Suspended;
        break;
    case QAudio::StoppedState:
        next = (m_source && m_source->error() != QAudio::NoError)
                   ? State::Error
                   : State::Stopped;
        break;
    case QAudio::IdleState:
        return;
    }

    if (next == State::Error)
        emit errorOccurred(tr("Audio source error %1")
                               .arg(static_cast<int>(m_source->error())));

    if (m_state != next) {
        m_state = next;
        emit stateChanged(m_state);
    }
}

void AudioInput::onCapturedData(const QByteArray &data)
{
    emit audioDataReady(data, m_source->format());
}
