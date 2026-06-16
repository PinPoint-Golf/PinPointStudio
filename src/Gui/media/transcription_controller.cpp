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

#include "transcription_controller.h"

#include "acoustic_shot_detector.h"
#include "app_settings.h"
#include "audio_input.h"
#include "audio_input_base.h"
#include "audio_stream_saver.h"
#include "SecretsManager.h"
#include "stt_processor.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QMetaObject>
#include "pp_debug.h"
#include <QThread>
#include <QVariantMap>

#ifdef Q_OS_MACOS
#include "macos_permissions.h"
#endif


TranscriptionController::TranscriptionController(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_appSettings(settings)
    , m_audioThread(new QThread(this))
    , m_processorThread(new QThread(this))
    , m_audioInput(new AudioInput)
    , m_streamSaver(nullptr)
    , m_acousticDetector(new AcousticShotDetector)
    , m_stt(new STTProcessor(settings && settings->cloudFallbackStt()))
{
    m_audioThread->setObjectName(QStringLiteral("AudioThread"));
    m_processorThread->setObjectName(QStringLiteral("ProcessorThread"));

    // Cloud-fallback availability is gated on an Azure key being configured; the
    // user's force-cloud preference (cloudFallbackStt) selects the backend. The
    // initial backend was chosen in the STTProcessor ctor above.
    m_sttCloudToggleAvailable = sttKeyPresent();
    m_sttUsingCloud           = (settings && settings->cloudFallbackStt() && m_sttCloudToggleAvailable);
    if (m_sttUsingCloud)
        m_sttCloudKey = sttCloudKey();   // matches the key the STTProcessor ctor just used
    if (m_appSettings)
        connect(m_appSettings, &AppSettings::cloudFallbackSttChanged,
                this, &TranscriptionController::applyCloudFallbackPref);

    m_audioInput->moveToThread(m_audioThread);
    m_stt->moveToThread(m_processorThread);

    m_audioInput->connectProcessor(m_stt);

    // connect(m_audioInput, &AudioInputBase::audioDataReady,
    //         m_streamSaver, &AudioStreamSaver::onAudioData);

    // Acoustic shot detection (P2) — second consumer of audioDataReady (the
    // AudioStreamSaver pattern above); never disturbs the STT pipeline. The
    // detector lives on the audio thread so the nowMicros receipt stamp is
    // taken where the buffer arrives; the signal-to-signal forward emits our
    // impactDetected on that same thread (documented on the signal).
    m_acousticDetector->moveToThread(m_audioThread);
    connect(m_audioInput, &AudioInputBase::audioDataReady,
            m_acousticDetector, &AcousticShotDetector::onAudioData);
    connect(m_acousticDetector, &AcousticShotDetector::impactDetected,
            this, &TranscriptionController::impactDetected);
    // Calibration meter — forwarded straight through (queued onto the GUI
    // thread by the receiver context). Harmless when nothing is connected.
    connect(m_acousticDetector, &AcousticShotDetector::levelSample,
            this, &TranscriptionController::audioLevel);

    connect(m_stt, &STTProcessor::transcriptionReceived,
            this, &TranscriptionController::onTranscriptionReceived);
    connect(m_stt, &STTProcessor::transcriptionDispatched,
            this, &TranscriptionController::onTranscriptionDispatched);
    connect(m_stt, &STTProcessor::backendLabelReady,
            this, &TranscriptionController::onBackendLabelReady);
    connect(m_stt, &STTProcessor::errorOccurred,
            this, &TranscriptionController::onSTTError);
    connect(m_audioInput, &AudioInputBase::errorOccurred,
            this, &TranscriptionController::onAudioError);

    connect(m_processorThread, &QThread::started,
            m_stt, &STTProcessor::start);

    // Re-evaluate capture once the audio thread is actually running, so any
    // listen reason set before then (e.g. shot detection enabled at startup, or
    // on macOS where startAudio() is deferred until mic permission) takes effect.
    connect(m_audioThread, &QThread::started,
            this, &TranscriptionController::updateCapture);

#ifdef Q_OS_MACOS
    // On macOS both permissions must be resolved before their respective pipelines
    // start — otherwise the permission dialogs race with background thread startup.
    auto *self = this;

    // Speech recognition: start the STT processor thread only after the system
    // has determined the authorization status (dialog answered or pre-existing).
    requestSpeechRecognitionPermission([self](bool sttGranted) {
        QMetaObject::invokeMethod(self, [self, sttGranted]() {
            if (!sttGranted)
                ppError() << "[TranscriptionController] Speech recognition permission denied."
                           << "Grant access in System Settings → Privacy & Security → Speech Recognition.";
            self->m_processorThread->start();
        }, Qt::QueuedConnection);
    });

    // Microphone: start the audio capture thread only after permission is granted.
    requestMicrophonePermission([self](bool micGranted) {
        QMetaObject::invokeMethod(self, [self, micGranted]() {
            if (micGranted)
                self->startAudio();
            else
                ppError() << "[TranscriptionController] Microphone permission denied."
                           << "Grant access in System Settings → Privacy & Security → Microphone.";
        }, Qt::QueuedConnection);
    });
#else
    m_processorThread->start();
    startAudio();
#endif
}

TranscriptionController::~TranscriptionController()
{
    if (m_audioThread->isRunning()) {
        // Stop capture and move the object back to the main thread while the
        // audio thread is still running — moveToThread must be called from the
        // object's current thread, so we do both steps in the same invoke.
        QMetaObject::invokeMethod(m_audioInput, [this]() {
            m_audioInput->stop();
            m_audioInput->moveToThread(QCoreApplication::instance()->thread());
            m_acousticDetector->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);

        m_audioThread->quit();
        m_audioThread->wait();
    }

    delete m_audioInput;
    m_audioInput = nullptr;

    delete m_acousticDetector;
    m_acousticDetector = nullptr;

    // m_streamSaver->stopSaving();

    if (m_processorThread->isRunning()) {
        // Move m_stt (and its child m_flushTimer) back to the main thread
        // while the processor thread is still running, so the QTimer destructor
        // runs on the correct thread and does not trigger killTimer warnings.
        QMetaObject::invokeMethod(m_stt, [this]() {
            m_stt->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);

        m_processorThread->quit();
        m_processorThread->wait();
    }
    delete m_stt;
    m_stt = nullptr;
}

void TranscriptionController::onBackendLabelReady(const QString &label)
{
    m_sttUsingCloud = (label == QLatin1String("Cloud"));
    if (m_sttUsingCloud && !m_sttCloudToggleAvailable) {
        m_sttCloudToggleAvailable = true;
        emit cloudSttFallbackAvailableChanged();
    }
    m_sttBackend = label;
    emit sttBackendChanged();
}

void TranscriptionController::toggleSttBackend()
{
    // The persisted preference is the single source of truth — flipping it drives
    // applyCloudFallbackPref() (and keeps the Settings toggle in sync).
    if (!m_sttCloudToggleAvailable || !m_appSettings)
        return;
    m_appSettings->setCloudFallbackStt(!m_appSettings->cloudFallbackStt());
}

void TranscriptionController::applyCloudFallbackPref()
{
    if (!m_appSettings)
        return;
    const QString key  = sttCloudKey();
    const bool    want = m_appSettings->cloudFallbackStt() && !key.isEmpty();
    // Rebuild when the desired backend changes, OR when staying on cloud but the
    // key value changed (e.g. the user corrected a bad key) — swapBackend re-reads it.
    if (want == m_sttUsingCloud && (!want || key == m_sttCloudKey))
        return;
    m_sttUsingCloud = want;   // optimistic; confirmed by onBackendLabelReady
    m_sttCloudKey   = want ? key : QString();
    STTProcessor *stt = m_stt;
    QMetaObject::invokeMethod(stt, [stt, want]() {
        stt->swapBackend(want);
    }, Qt::QueuedConnection);
}

void TranscriptionController::refreshCloudAvailability()
{
    const bool avail = sttKeyPresent();
    if (avail != m_sttCloudToggleAvailable) {
        m_sttCloudToggleAvailable = avail;
        emit cloudSttFallbackAvailableChanged();
    }
    // A newly-entered (or corrected) key may enable a force-cloud preference that
    // was waiting on it, or change the key of the live cloud backend.
    applyCloudFallbackPref();
}

QString TranscriptionController::sttCloudKey() const
{
    QString k = SecretsManager::read(QStringLiteral("azureSttApiKey"));
    if (k.isEmpty())
        k = SecretsManager::read(QStringLiteral("azureTtsApiKey"));
    return k;
}

bool TranscriptionController::sttKeyPresent() const
{
    return !sttCloudKey().isEmpty();
}

void TranscriptionController::setAcousticLatencyUs(qint64 us)
{
    m_acousticDetector->setDeviceLatencyUs(us);
}

QVariantList TranscriptionController::availableInputDevices() const
{
    QVariantList out;
    const QByteArray defId = QMediaDevices::defaultAudioInput().id();
    for (const QAudioDevice &dev : AudioInput::availableDevices()) {
        QVariantMap m;
        m[QStringLiteral("id")]          = QString::fromUtf8(dev.id());
        m[QStringLiteral("description")] = dev.description();
        m[QStringLiteral("isDefault")]   = (dev.id() == defId);
        out.append(m);
    }
    return out;
}

void TranscriptionController::setInputDevice(const QString &deviceId)
{
    // Applied on the audio thread (where m_audioInput lives). start() stops the
    // prior source first, so a restart-in-place is just a fresh start(). The
    // queued call also serialises correctly with startListening()'s start().
    const bool wasCapturing = m_capturing;
    QMetaObject::invokeMethod(m_audioInput, [this, deviceId, wasCapturing]() {
        m_audioInput->setDevice(deviceId);
        if (wasCapturing)
            m_audioInput->start();
    }, Qt::QueuedConnection);
}

void TranscriptionController::setAcousticThresholdFactor(double factor)
{
    m_acousticDetector->setThresholdFactor(static_cast<float>(factor));
}

void TranscriptionController::setAcousticMinLevel(double level)
{
    m_acousticDetector->setMinLevel(static_cast<float>(level));
}

void TranscriptionController::setCalibrationActive(bool active)
{
    m_calibrationWanted = active;
    updateCapture();
}

void TranscriptionController::onTranscriptionDispatched()
{
    m_sttDispatchTimer.restart();
}

void TranscriptionController::onTranscriptionReceived(const QString &text)
{
    if (m_sttDispatchTimer.isValid()) {
        m_lastSttLatencyMs = m_sttDispatchTimer.elapsed();
        m_sttDispatchTimer.invalidate();
        emit lastSttLatencyMsChanged();
    }
    if (!m_transcript.isEmpty())
        m_transcript += QLatin1Char('\n');
    m_transcript += text;
    emit transcriptChanged();
    emit transcriptionReceived(text);
}

void TranscriptionController::onAudioError(const QString &message)
{
    ppWarn() << "[Audio]" << message;
}

void TranscriptionController::onSTTError(const QString &message)
{
    ppWarn() << "[STT]" << message;
}

void TranscriptionController::startListening()
{
    m_voiceWanted = true;
    setSttGate(true);
    updateCapture();
}

void TranscriptionController::stopListening()
{
    m_voiceWanted = false;
    // Gate STT off even when capture stays open for calibration/shot detection
    // — those reasons must not feed whisper.
    setSttGate(false);
    updateCapture();
}

void TranscriptionController::setSttGate(bool enabled)
{
    STTProcessor *stt = m_stt;
    QMetaObject::invokeMethod(stt, [stt, enabled]() {
        stt->setVoiceEnabled(enabled);
    }, Qt::QueuedConnection);
}

void TranscriptionController::setShotDetectionActive(bool active)
{
    m_shotDetectionWanted = active;
    updateCapture();
}

void TranscriptionController::updateCapture()
{
    const bool want = (m_voiceWanted || m_calibrationWanted || m_shotDetectionWanted)
                      && m_audioThread->isRunning();
    if (want == m_capturing)
        return;
    m_capturing = want;
    QMetaObject::invokeMethod(m_audioInput, [this, want]() {
        if (want) m_audioInput->start();
        else      m_audioInput->stop();
    }, Qt::QueuedConnection);
    if (!want)
        QMetaObject::invokeMethod(m_stt, "stopStreaming", Qt::QueuedConnection);
    emit isListeningChanged();
}

void TranscriptionController::startAudio()
{
    m_audioThread->start();
}
