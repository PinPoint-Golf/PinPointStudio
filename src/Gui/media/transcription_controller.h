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
#include <QVariantList>

class AcousticShotDetector;
class AppSettings;
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
    explicit TranscriptionController(AppSettings *settings = nullptr, QObject *parent = nullptr);
    ~TranscriptionController() override;

    QString transcript()              const { return m_transcript; }
    bool    isListening()             const { return m_capturing; }
    QString sttBackend()              const { return m_sttBackend; }
    bool    cloudSttFallbackAvailable() const { return m_sttCloudToggleAvailable; }
    qint64  lastSttLatencyMs()        const { return m_lastSttLatencyMs; }

public slots:
    void startListening();
    void stopListening();

    Q_INVOKABLE void toggleSttBackend();

    // Re-evaluate whether the Azure key is configured (cloud-fallback availability)
    // and re-apply the cloudFallbackStt preference. Wired to SecretsBridge::keysChanged.
    void refreshCloudAvailability();

    // Acoustic shot detection (P2): forwards AppSettings' device-latency
    // constant to the detector (atomic — safe to call from the GUI thread).
    void setAcousticLatencyUs(qint64 us);

    // ── Microphone selection + calibration ────────────────────────────────
    // Enumerate input devices for the Microphone settings panel:
    // [{ id: QString, description: QString, isDefault: bool }, …].
    Q_INVOKABLE QVariantList availableInputDevices() const;

    // Select the capture device by its stable id (empty = OS default). Applied
    // on the audio thread; restarts capture in place if currently listening.
    Q_INVOKABLE void setInputDevice(const QString &deviceId);

    // Live acoustic sensitivity knobs (atomic — safe from the GUI thread).
    // minLevel: absolute amplitude gate — the candidate-open threshold floor,
    // the primary calibration lever (below it, nothing fires). thresholdFactor:
    // relative trigger vs the adaptive noise floor. main.cpp derives the gate
    // from the single acousticSensitivity setting.
    Q_INVOKABLE void setAcousticThresholdFactor(double factor);
    Q_INVOKABLE void setAcousticMinLevel(double level);

    // Hold audio capture open while the calibration view is visible, even
    // outside a session. Restores the prior listening state when released.
    Q_INVOKABLE void setCalibrationActive(bool active);

    // Hold audio capture open while live shot detection is active (a capturing
    // session with acoustic detection enabled). Without this the mic never runs
    // during a session and the acoustic modality — and its calibration — does
    // nothing. main.cpp drives it from the camera capture intent.
    void setShotDetectionActive(bool active);

signals:
    void transcriptChanged();
    void transcriptionReceived(const QString &text);  // fires once per completed utterance
    void isListeningChanged();
    void sttBackendChanged();
    void cloudSttFallbackAvailableChanged();
    void lastSttLatencyMsChanged();

    // Acoustic shot detection (P2). NOTE: forwarded signal-to-signal from the
    // AcousticShotDetector, so it is emitted on the AUDIO thread with est_t*
    // already computed — connect with a receiver context to get the queued
    // hop onto your thread.
    void impactDetected(qint64 estImpactUs, float confidence);

    // Per-buffer calibration meter tick (forwarded from AcousticShotDetector,
    // emitted on the AUDIO thread): peak envelope, current noise floor, and the
    // live trigger threshold — all in the same linear units.
    void audioLevel(float level, float noiseFloor, float threshold);

private slots:
    void onTranscriptionReceived(const QString &text);
    void onTranscriptionDispatched();
    void onBackendLabelReady(const QString &label);
    void onAudioError(const QString &message);
    void onSTTError(const QString &message);
    void startAudio();   // called once microphone permission is confirmed
    // Swap the STT backend to match cloudFallbackStt && key-present. Connected to
    // AppSettings::cloudFallbackSttChanged.
    void applyCloudFallbackPref();

private:
    // Start/stop the underlying audio device to match the OR of all listen
    // reasons (manual voice, calibration, shot detection). Idempotent — the
    // device starts once when the first reason activates and stops when the
    // last clears, so per-shot capture-intent churn never restarts the device.
    void updateCapture();

    // STT only transcribes while the voice reason is active — capture opened
    // for calibration/shot detection shares the mic but must not feed whisper.
    void setSttGate(bool enabled);

    // Effective Azure key for STT: the STT-specific key, or the shared TTS key.
    QString sttCloudKey() const;
    // True when an Azure key (STT-specific, or the shared TTS key) is configured.
    bool    sttKeyPresent() const;

private:
    AppSettings          *m_appSettings = nullptr;
    QThread              *m_audioThread;
    QThread              *m_processorThread;
    AudioInput           *m_audioInput;
    AudioStreamSaver     *m_streamSaver;
    AcousticShotDetector *m_acousticDetector;
    STTProcessor         *m_stt;
    QString           m_transcript;
    QString           m_sttBackend;
    // Listen reasons — audio captures while ANY is true (see updateCapture).
    bool              m_voiceWanted             = false;   // manual (Audio page)
    bool              m_calibrationWanted       = false;   // calibration view open
    bool              m_shotDetectionWanted     = false;   // capturing session
    bool              m_capturing               = false;   // actual device state
    bool              m_sttUsingCloud           = false;
    bool              m_sttCloudToggleAvailable = false;
    QString           m_sttCloudKey;            // Azure key currently loaded in the backend
    QElapsedTimer     m_sttDispatchTimer;
    qint64            m_lastSttLatencyMs        = 0;
};
