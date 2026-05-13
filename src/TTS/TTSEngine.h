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
#include <QString>

// Abstract base for text-to-speech synthesis backends.
//
// Subclasses implement model loading, phonemisation, and neural inference,
// then emit audioReady() with the resulting raw PCM.
//
// The audioReady signal signature matches AudioOutputBase::writeAudio() so
// the two can be wired with a direct queued connection.
//
// Typical wiring (via TtsWorker on a background QThread):
//   auto engine = TTSEngineFactory::create(TTSEngineFactory::Backend::Kokoro);
//   engine->loadModel(modelPath, voicePath, tokensPath);
//   engine->synthesise("Hello world");
//   // → audioReady() emitted when inference completes

class TTSEngine : public QObject
{
    Q_OBJECT

public:
    explicit TTSEngine(QObject *parent = nullptr);
    ~TTSEngine() override = default;

    // Load model weights, voice style vector, and phoneme token vocabulary.
    // Must succeed before synthesise() can be called.
    virtual bool loadModel(const QString &modelPath,
                           const QString &voicePath,
                           const QString &tokensPath) = 0;

    // Begin synthesis on the calling thread.
    // Emits synthesisStarted → (audioReady…) → synthesisFinished.
    // Move the engine to a QThread to keep the calling thread free.
    virtual void synthesise(const QString &text) = 0;

    // Abort any synthesis in progress. Safe to call from any thread.
    virtual void stop() = 0;

    virtual bool isReady() const = 0;

    // Returns the active GPU execution provider ("CoreML", "CUDA") or empty for CPU-only.
    virtual QString gpuBackend() const { return {}; }

    // Playback speed multiplier (1.0 = normal). Applied on next synthesise().
    virtual void  setSpeed(float speed) { m_speed = speed; }
    virtual float speed()         const { return m_speed; }

signals:
    // Raw PCM audio ready for playback.
    // Format reflects the engine's native output (Kokoro: 24 kHz, mono, float32).
    // Compatible with AudioOutputBase::writeAudio() for direct signal-slot wiring.
    void audioReady(const QByteArray &pcmData, const QAudioFormat &format);

    void synthesisStarted();
    void synthesisFinished();
    void modelLoaded();
    void errorOccurred(const QString &message);

protected:
    float m_speed = 1.0f;
};
