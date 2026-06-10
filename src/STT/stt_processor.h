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

#include "audio_processor_base.h"
#include <QAudioFormat>
#include <QByteArray>

class QThread;
class QTimer;
class STTWorker;

// Buffers PCM audio from the capture pipeline and dispatches it to an STT
// worker thread for transcription.  Audio is accumulated for chunkDurationMs()
// milliseconds, silence-gated, converted to 16 kHz mono float32 via
// AudioConverter, then handed to STTWorker::transcribe() via a queued
// invocation so the backend's transcription call never blocks this thread.
//
// Typical wiring:
//   auto *stt = new STTProcessor(this);
//   input->connectProcessor(stt);
//   connect(stt, &STTProcessor::transcriptionReceived,
//           this, &MyClass::onText);
//   input->start();

class STTProcessor : public AudioProcessorBase
{
    Q_OBJECT

public:
    explicit STTProcessor(QObject *parent = nullptr);
    ~STTProcessor() override;

    // Audio is dispatched to the worker in chunks of this duration (default: 3000 ms).
    void setChunkDurationMs(int ms);
    int  chunkDurationMs() const { return m_chunkDurationMs; }

    // Chunks whose RMS amplitude (normalised 0.0–1.0) falls below this value
    // are discarded without being dispatched (default: 0.01 ≈ -40 dBFS).
    void   setSilenceThreshold(double threshold) { m_silenceThreshold = threshold; }
    double silenceThreshold() const { return m_silenceThreshold; }

public slots:
    // Call after moveToThread() to start the flush timer in the correct thread.
    void start();
    void processAudio(const QByteArray &data, const QAudioFormat &format) override;
    void stopStreaming();
    // Gate on the voice listen reason. The capture pipeline also feeds this
    // processor when the mic is open only for acoustic calibration / shot
    // detection; audio arriving while disabled is discarded, not transcribed.
    void setVoiceEnabled(bool enabled);
    // Tear down the current backend and restart with a cloud or local backend.
    // forceCloud=true → Azure STT; false → platform default (e.g. whisper.cpp).
    void swapBackend(bool forceCloud);

signals:
    void transcriptionReceived(const QString &text);
    void transcriptionDispatched();
    void errorOccurred(const QString &message);
    void modelReady();
    void modelNotFound(const QStringList &searchedPaths);
    void backendLabelReady(const QString &label);

private slots:
    void onFlushTimer();

private:
    // Resolution helpers — see resolveModelPath() in .cpp for the search order.
    QStringList modelCandidates(const QString &filename) const;
    QString     resolveModelPath(const QString &filename) const;
    double      computeRms(const QByteArray &pcm, const QAudioFormat &fmt) const;

    QTimer      *m_flushTimer;
    QThread     *m_workerThread     = nullptr;
    STTWorker   *m_worker           = nullptr;
    QByteArray   m_buffer;
    QAudioFormat m_format;
    QString      m_modelPath;        // resolved in ctor, used in start()
    QStringList  m_searchedPaths;    // populated when model is not found
    bool         m_needsModelFile    = true;  // false for OS-native backends
    bool         m_silenceGating    = true;  // false for cloud backends (need silence for end-of-turn)
    bool         m_voiceEnabled     = false; // true only while voice listening is wanted
    int          m_chunkDurationMs  = 3000;
    double       m_silenceThreshold = 0.01;
};
