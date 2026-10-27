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
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <optional>

#include "tts_phrase_cache.h"

class AppSettings;
class AudioOutput;
class ModelDownloader;
class TtsWorker;

// Owns the TTS synthesis thread, audio output pipeline, and model downloader.
// Exposes the full TTS workflow to QML via Q_PROPERTYs and Q_INVOKABLEs.
//
// On construction the controller checks whether the Kokoro model files are
// present under modelDataDir().  If not, it downloads them automatically
// before loading the model.
//
// QML usage:
//   ttsController.speak("Hello world")
//   ttsController.stopSpeaking()
//   ttsController.ttsReady         // bool — model loaded, speak() may be called
//   ttsController.ttsActive        // bool — synthesis in progress
//   ttsController.downloading      // bool — model files being fetched
//   ttsController.downloadProgress // real 0.0–1.0
//   ttsController.downloadStatus   // human-readable status string
//   ttsController.voices           // string list of available voices
//   ttsController.voice            // current voice name (read/write)
//   ttsController.prepare("Turn back to address.")   // synthesise now, play later
//   ttsController.isPrepared("Turn back to address.") // its audio is cached for this voice
//
// PREPARED PHRASES (27 Oct 2026, Mark: "the phrases should be prepared in advance, ideally when the
// session setup is instantiated — makes no sense to do it at the last minute"). prepare(text)
// synthesises WITHOUT playing into a small cache keyed by (backend, voice, text) — tts_phrase_cache.h;
// speak(text) of a prepared phrase plays the cached audio at once, with no synthesis; any other text
// is synthesised as before. A voice or backend change empties the cache (the hosts re-prepare when
// ttsReady comes back; preparedChanged fires). Syntheses run ONE AT A TIME through a small job queue
// (the Azure engine would otherwise overlap replies): a speak() queued behind a prepare waits for it,
// and a speak() of the phrase being prepared plays it as it arrives. canReplay / replayLastAudio
// replay the last thing SPOKEN — a prepare never touches it.

class TtsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool        ttsReady                  READ ttsReady                  NOTIFY ttsReadyChanged)
    Q_PROPERTY(bool        ttsActive                 READ ttsActive                 NOTIFY ttsActiveChanged)
    Q_PROPERTY(bool        canReplay                 READ canReplay                 NOTIFY canReplayChanged)
    Q_PROPERTY(bool        downloading               READ downloading               NOTIFY downloadingChanged)
    Q_PROPERTY(qreal       downloadProgress          READ downloadProgress          NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString     downloadStatus            READ downloadStatus            NOTIFY downloadStatusChanged)
    Q_PROPERTY(QString     ttsBackend                READ ttsBackend                NOTIFY ttsBackendChanged)
    Q_PROPERTY(bool        cloudTtsFallbackAvailable READ cloudTtsFallbackAvailable NOTIFY cloudTtsFallbackAvailableChanged)
    Q_PROPERTY(QStringList voices                    READ voices                    CONSTANT)
    Q_PROPERTY(QString     voice                     READ voice                     WRITE setVoice NOTIFY voiceChanged)
    Q_PROPERTY(qint64      lastTtsLatencyMs          READ lastTtsLatencyMs          NOTIFY lastTtsLatencyMsChanged)

public:
    explicit TtsController(AppSettings *settings = nullptr, QObject *parent = nullptr);
    ~TtsController() override;

    bool        ttsReady()                  const { return m_ttsReady; }
    bool        ttsActive()                 const { return m_ttsActive; }
    bool        canReplay()                 const { return !m_lastAudioCache.isEmpty(); }
    bool        downloading()               const { return m_downloading; }
    qreal       downloadProgress()          const { return m_downloadProgress; }
    QString     downloadStatus()            const { return m_downloadStatus; }
    QString     ttsBackend()               const { return m_ttsBackend; }
    bool        cloudTtsFallbackAvailable() const { return m_cloudToggleAvailable; }
    QStringList voices()                    const;
    QString     voice()                     const { return m_voice; }
    void        setVoice(const QString &voice);
    qint64      lastTtsLatencyMs()          const { return m_lastTtsLatencyMs; }

    Q_INVOKABLE void speak(const QString &text);
    // Synthesise `text` into the phrase cache without playing it. Idempotent: a phrase already
    // cached, or being prepared, for this voice is left alone. Ignored while !ttsReady.
    Q_INVOKABLE void prepare(const QString &text);
    // True when `text`'s audio is cached for the current voice and backend (speak plays it at once).
    Q_INVOKABLE bool isPrepared(const QString &text) const;
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void replayLastAudio();
    Q_INVOKABLE void toggleTtsBackend();

    // Re-evaluate whether the Azure key is configured (cloud-fallback availability)
    // and re-apply the cloudFallbackTts preference. Wired to SecretsBridge::keysChanged.
    void refreshCloudAvailability();

signals:
    void ttsReadyChanged();
    void ttsActiveChanged();
    void canReplayChanged();
    void downloadingChanged();
    void downloadProgressChanged();
    void downloadStatusChanged();
    void ttsBackendChanged();
    void cloudTtsFallbackAvailableChanged();
    void voiceChanged();
    void lastTtsLatencyMsChanged();
    // A phrase was stored in, or the cache was emptied of, prepared audio.
    void preparedChanged();

private slots:
    void onModelReady();
    void onModelFailed(const QString &error);
    void onBackendChanged(const QString &backend);
    void onAudioReady(const QByteArray &data, const QAudioFormat &format);
    void onSynthesisStarted();
    void onSynthesisFinished();
    void onTtsError(const QString &message);

    void onDownloadProgress(int fileIndex, int fileCount,
                            qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFileComplete(const QString &path);
    void onDownloadFinished();
    void onDownloadFailed(const QString &error);

private slots:
    // Swap to/from Azure cloud TTS to match cloudFallbackTts && key-present.
    // Connected to AppSettings::cloudFallbackTtsChanged.
    void    applyCloudFallbackPref();

private:
    bool    modelFilesExist() const;
    void    startDownload();
    void    triggerModelLoad();
    void    setDownloadStatus(const QString &status);
    void    connectWorkerSignals();
    void    switchToAzure(const QString &apiKey);
    void    switchToKokoro();

    // ── Prepared phrases and the synthesis queue ──
    struct PreparedAudio { QByteArray pcm; QAudioFormat format; };
    struct TtsJob {
        QString       text;
        TtsPhraseKey  key;
        quint64       generation = 0;
        bool          prepare = false;   // store the audio in the phrase cache
        bool          play    = false;   // write the audio to the output as it arrives
        bool          started = false;
        bool          aborted = false;   // stopSpeaking() cut it short: never cached
        QByteArray    pcm;
        QAudioFormat  format;
        QElapsedTimer synthClock;        // dispatch → finished
        QElapsedTimer speakClock;        // speak() → finished (the latency the golfer hears)
    };
    TtsPhraseKey phraseKey(const QString &trimmedText) const;
    void    pumpJobs();
    void    beginPlaying(TtsJob &job);
    void    setActive(bool on);
    void    dropJobs(const char *why);
    void    invalidatePrepared(const char *why);
    void    onDispatchTimeout();

    // True when the Azure TTS key is configured.
    bool    ttsKeyPresent() const;

    QString modelDataDir()    const;
    QString resolveModelPath()  const;
    QString resolveVoicePath()  const;
    QString resolveTokensPath() const;

    AppSettings     *m_appSettings = nullptr;
    QThread         *m_workerThread;
    TtsWorker       *m_worker;
    AudioOutput     *m_audioOutput;
    ModelDownloader *m_downloader;

    bool          m_ttsReady             = false;
    bool          m_ttsActive            = false;
    bool          m_downloading          = false;
    bool          m_usingCloudTts        = false;
    bool          m_switchingToAzure     = false;
    bool          m_forceLocalTts        = false;
    bool          m_cloudToggleAvailable = false;
    QString       m_ttsCloudKey;            // Azure key currently loaded in the backend
    qreal         m_downloadProgress = 0.0;
    QString       m_downloadStatus;
    QString       m_ttsBackend;
    QString       m_voice = QStringLiteral("af_sky");
    QByteArray    m_lastAudioCache;
    QAudioFormat  m_lastAudioFormat;
    QElapsedTimer m_ttsStartTimer;
    qint64        m_lastTtsLatencyMs = 0;

    TtsPhraseCache<PreparedAudio> m_prepared;
    QList<TtsJob>                 m_jobs;          // waiting, oldest first
    std::optional<TtsJob>         m_job;           // dispatched to the worker
    QTimer                        m_dispatchWatchdog;
};
