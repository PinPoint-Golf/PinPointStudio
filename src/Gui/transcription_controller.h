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
#include <QString>
#include <QThread>
#include <QElapsedTimer>

class AudioInput;
class AudioInputBase;
class AudioStreamSaver;
class STTProcessor;

// Owns the audio-capture and STT-processor threads and exposes the growing
// transcript as a Q_PROPERTY so QML can bind to it directly.
class TranscriptionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString transcript              READ transcript              NOTIFY transcriptChanged)
    Q_PROPERTY(bool   isListening             READ isListening             NOTIFY isListeningChanged)
    Q_PROPERTY(QString sttBackend             READ sttBackend              NOTIFY sttBackendChanged)
    Q_PROPERTY(bool   cloudSttFallbackAvailable READ cloudSttFallbackAvailable NOTIFY cloudSttFallbackAvailableChanged)
    Q_PROPERTY(qint64 lastSttLatencyMs        READ lastSttLatencyMs        NOTIFY lastSttLatencyMsChanged)

public:
    explicit TranscriptionController(QObject *parent = nullptr);
    ~TranscriptionController() override;

    QString transcript()              const { return m_transcript; }
    bool    isListening()             const { return m_listening; }
    QString sttBackend()              const { return m_sttBackend; }
    bool    cloudSttFallbackAvailable() const { return m_sttCloudToggleAvailable; }
    qint64  lastSttLatencyMs()        const { return m_lastSttLatencyMs; }

public slots:
    void startListening();
    void stopListening();

    Q_INVOKABLE void toggleSttBackend();

signals:
    void transcriptChanged();
    void isListeningChanged();
    void sttBackendChanged();
    void cloudSttFallbackAvailableChanged();
    void lastSttLatencyMsChanged();

private slots:
    void onTranscriptionReceived(const QString &text);
    void onTranscriptionDispatched();
    void onBackendLabelReady(const QString &label);
    void onAudioError(const QString &message);
    void onSTTError(const QString &message);
    void startAudio();   // called once microphone permission is confirmed

private:
    QThread          *m_audioThread;
    QThread          *m_processorThread;
    AudioInput       *m_audioInput;
    AudioStreamSaver *m_streamSaver;
    STTProcessor     *m_stt;
    QString           m_transcript;
    QString           m_sttBackend;
    bool              m_listening               = false;
    bool              m_sttUsingCloud           = false;
    bool              m_sttCloudToggleAvailable = false;
    QElapsedTimer     m_sttDispatchTimer;
    qint64            m_lastSttLatencyMs        = 0;
};
