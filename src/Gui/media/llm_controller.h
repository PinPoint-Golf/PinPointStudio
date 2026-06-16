/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QThread>

class AppSettings;
class LlmWorker;
class ModelDownloader;

// Owns the LLM inference thread and model downloader.
// Exposes the full coaching-chat workflow to QML via Q_PROPERTYs and Q_INVOKABLEs.
//
// On construction the controller checks whether the Phi-4-mini model files are
// present. If not, it downloads them automatically before loading the model.
//
// When LocalLlmEngine reports CPU-only execution (gpuBackend() == ""), the
// controller hot-swaps to GeminiLlmEngine — identical to the TtsController →
// AzureTTSEngine pattern — provided a GEMINI_API_KEY is configured.
//
// QML usage:
//   llmController.sendMessage("That swing felt rushed")
//   llmController.llmReady          // bool
//   llmController.llmActive         // bool — generating
//   llmController.llmBackend        // "CUDA" | "CoreML" | "Cloud" | "" (CPU)
//   llmController.conversation      // QVariantList of {role, text, partial}
//   llmController.voiceInputEnabled // bool — route STT transcriptions to chat
//   llmController.voiceOutputEnabled// bool — speak LLM responses via TTS

class LlmController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool         llmReady                  READ llmReady                  NOTIFY llmReadyChanged)
    Q_PROPERTY(bool         llmActive                 READ llmActive                 NOTIFY llmActiveChanged)
    Q_PROPERTY(bool         downloading               READ downloading               NOTIFY downloadingChanged)
    Q_PROPERTY(qreal        downloadProgress          READ downloadProgress          NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString      downloadStatus            READ downloadStatus            NOTIFY downloadStatusChanged)
    Q_PROPERTY(QString      llmBackend                READ llmBackend                NOTIFY llmBackendChanged)
    Q_PROPERTY(bool         cloudLlmFallbackAvailable READ cloudLlmFallbackAvailable NOTIFY cloudLlmFallbackAvailableChanged)
    // True when a local GPU exists. When false the AI Coach has no local path and
    // the Cloud Fallback toggle is locked ON in the UI. Fixed at startup.
    Q_PROPERTY(bool         localGpuAvailable         READ localGpuAvailable         CONSTANT)
    Q_PROPERTY(QVariantList conversation              READ conversation              NOTIFY conversationChanged)
    Q_PROPERTY(bool         voiceInputEnabled         READ voiceInputEnabled         NOTIFY voiceInputEnabledChanged)
    Q_PROPERTY(bool         voiceOutputEnabled        READ voiceOutputEnabled        NOTIFY voiceOutputEnabledChanged)
    Q_PROPERTY(qint64       lastLlmLatencyMs          READ lastLlmLatencyMs          NOTIFY lastLlmLatencyMsChanged)

public:
    explicit LlmController(AppSettings *settings = nullptr, QObject *parent = nullptr);
    ~LlmController() override;

    bool         llmReady()                  const { return m_llmReady; }
    bool         llmActive()                 const { return m_llmActive; }
    bool         downloading()               const { return m_downloading; }
    qreal        downloadProgress()          const { return m_downloadProgress; }
    QString      downloadStatus()            const { return m_downloadStatus; }
    QString      llmBackend()                const { return m_llmBackend; }
    bool         cloudLlmFallbackAvailable() const { return m_cloudToggleAvailable; }
    bool         localGpuAvailable()         const { return m_localGpuAvailable; }
    QVariantList conversation()              const;
    bool         voiceInputEnabled()         const { return m_voiceInputEnabled; }
    bool         voiceOutputEnabled()        const { return m_voiceOutputEnabled; }
    qint64       lastLlmLatencyMs()          const { return m_lastLlmLatencyMs; }

    Q_INVOKABLE void sendMessage(const QString &text);
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void toggleLlmBackend();
    Q_INVOKABLE void setVoiceInput(bool enabled);
    Q_INVOKABLE void setVoiceOutput(bool enabled);

    // Re-evaluate whether the Gemini key is configured (cloud-fallback availability)
    // and re-apply the cloudFallbackLlm preference. Wired to SecretsBridge::keysChanged.
    void refreshCloudAvailability();

signals:
    void llmReadyChanged();
    void llmActiveChanged();
    void downloadingChanged();
    void downloadProgressChanged();
    void downloadStatusChanged();
    void llmBackendChanged();
    void cloudLlmFallbackAvailableChanged();
    void conversationChanged();
    void voiceInputEnabledChanged();
    void voiceOutputEnabledChanged();
    void lastLlmLatencyMsChanged();

    // Emitted when a full response is ready — connect in main.cpp to TtsController::speak().
    void responseReady(const QString &text);

private slots:
    void onModelReady();
    void onModelFailed(const QString &error);
    void onBackendChanged(const QString &backend);
    // Swap to/from Gemini cloud to match (cloudFallbackLlm || no-GPU) && key-present.
    // Connected to AppSettings::cloudFallbackLlmChanged.
    void applyCloudFallbackPref();
    void onTokenReady(const QString &token);
    void onResponseReady(const QString &full);
    void onLlmError(const QString &message);

    void onDownloadProgress(int fileIndex, int fileCount,
                            qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFileComplete(const QString &path);
    void onDownloadFinished();
    void onDownloadFailed(const QString &error);

private:
    bool   modelFilesExist() const;
    void   startDownload();
    void   triggerModelLoad();
    void   setDownloadStatus(const QString &status);
    void   connectWorkerSignals();
    void   switchToGemini(const QString &apiKey);
    void   switchToLocal();

    QString modelDataDir() const;

    AppSettings     *m_appSettings = nullptr;
    QThread         *m_workerThread;
    LlmWorker       *m_worker;
    ModelDownloader *m_downloader;

    // Conversation stored as list of {role, text, partial} maps.
    QList<QVariantMap> m_history;

    bool    m_llmReady             = false;
    bool    m_llmActive            = false;
    bool    m_downloading          = false;
    bool    m_usingCloudLlm        = false;
    bool    m_switchingToGemini    = false;
    bool    m_forceLocalLlm        = false;
    bool    m_cloudToggleAvailable = false;
    bool    m_localGpuAvailable    = false;
    bool    m_voiceInputEnabled    = false;
    QString m_llmCloudKey;            // Gemini key currently loaded in the backend
    bool    m_voiceOutputEnabled   = false;
    qreal   m_downloadProgress     = 0.0;
    QString m_downloadStatus;
    QString m_llmBackend;
    QElapsedTimer m_latencyTimer;
    qint64        m_lastLlmLatencyMs = 0;
};
