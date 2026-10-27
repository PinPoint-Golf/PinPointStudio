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

#include "tts_controller.h"

#include "app_settings.h"
#include "audio_output.h"
#include "AzureTTSEngine.h"
#include "ModelDownloader.h"
#include "SecretsManager.h"
#include "TTSEngineFactory.h"
#include "TtsWorker.h"

#include <QCoreApplication>
#include <QFile>
#include "pp_debug.h"
#include <QMetaObject>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>

// ---------------------------------------------------------------------------
// Kokoro model download URLs
//
// Files are hosted on HuggingFace.  The direct-download URL format is:
//   https://huggingface.co/<owner>/<repo>/resolve/<branch>/<path>
//
// UPDATE THESE if the upstream repository moves the files.
// ---------------------------------------------------------------------------
static const char kBaseUrl[]   = "https://huggingface.co/onnx-community/Kokoro-82M-ONNX/resolve/main/";
static const char kModelUrl[]  = "https://huggingface.co/onnx-community/Kokoro-82M-ONNX/resolve/main/onnx/model_quantized.onnx";
static const char kTokensUrl[] = "https://huggingface.co/onnx-community/Kokoro-82M-ONNX/resolve/main/tokenizer.json";

// Voice style vectors available in onnx-community/Kokoro-82M-ONNX.
static const struct { const char *name; const char *file; } kVoiceUrls[] = {
    { "af",          "voices/af.bin"          },
    { "af_bella",    "voices/af_bella.bin"    },
    { "af_nicole",   "voices/af_nicole.bin"   },
    { "af_sarah",    "voices/af_sarah.bin"    },
    { "af_sky",      "voices/af_sky.bin"      },
    { "am_adam",     "voices/am_adam.bin"     },
    { "am_michael",  "voices/am_michael.bin"  },
    { "bf_emma",     "voices/bf_emma.bin"     },
    { "bf_isabella", "voices/bf_isabella.bin" },
    { "bm_george",   "voices/bm_george.bin"   },
    { "bm_lewis",    "voices/bm_lewis.bin"    },
};

// ---------------------------------------------------------------------------

TtsController::TtsController(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_appSettings(settings)
    , m_workerThread(new QThread(this))
    , m_audioOutput(new AudioOutput(this))
    , m_downloader(new ModelDownloader(this))
{
    m_workerThread->setObjectName(QStringLiteral("TtsWorkerThread"));

    // A dispatched synthesis that never starts (an engine that dropped it without a word) must not
    // stall every later one behind it.
    m_dispatchWatchdog.setSingleShot(true);
    m_dispatchWatchdog.setInterval(20000);
    connect(&m_dispatchWatchdog, &QTimer::timeout, this, &TtsController::onDispatchTimeout);

    // Cloud-fallback availability is gated on the Azure key being configured; the
    // cloudFallbackTts preference selects the backend (see applyCloudFallbackPref).
    m_cloudToggleAvailable = ttsKeyPresent();
    if (m_appSettings)
        connect(m_appSettings, &AppSettings::cloudFallbackTtsChanged,
                this, &TtsController::applyCloudFallbackPref);

    // ---- Engine + worker ----------------------------------------------------
    auto engine = TTSEngineFactory::create(TTSEngineFactory::Backend::Kokoro);
    m_worker    = new TtsWorker(engine.release());
    m_worker->moveToThread(m_workerThread);

    // ---- Audio output -------------------------------------------------------
    QAudioFormat ttsFormat;
    ttsFormat.setSampleRate(24000);
    ttsFormat.setChannelCount(1);
    ttsFormat.setSampleFormat(QAudioFormat::Float);
    m_audioOutput->setPreferredFormat(ttsFormat);
    m_audioOutput->start();

    // ---- Worker signals (also used when hot-swapping to Azure) --------------
    connectWorkerSignals();

    // ---- Downloader signals -------------------------------------------------
    connect(m_downloader, &ModelDownloader::progress,
            this, &TtsController::onDownloadProgress);
    connect(m_downloader, &ModelDownloader::fileComplete,
            this, &TtsController::onDownloadFileComplete);
    connect(m_downloader, &ModelDownloader::finished,
            this, &TtsController::onDownloadFinished);
    connect(m_downloader, &ModelDownloader::failed,
            this, &TtsController::onDownloadFailed);

    // ---- Start worker thread ------------------------------------------------
    m_workerThread->start();

    // ---- Initial backend ----------------------------------------------------
    // The cloudFallbackTts preference is authoritative: when set (and an Azure
    // key exists) go straight to cloud TTS and skip the Kokoro model download.
    if (m_appSettings && m_appSettings->cloudFallbackTts() && m_cloudToggleAvailable) {
        const QString apiKey = SecretsManager::read(QStringLiteral("azureTtsApiKey"));
        m_usingCloudTts    = true;
        m_switchingToAzure = true;
        m_ttsCloudKey      = apiKey;
        QTimer::singleShot(0, this, [this, apiKey]() { switchToAzure(apiKey); });
    } else if (modelFilesExist()) {
        triggerModelLoad();
    } else {
        startDownload();
    }
}

TtsController::~TtsController()
{
    m_downloader->abort();

    if (m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, [this]() {
            m_worker->stop();
            m_worker->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);

        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete m_worker;
    m_worker = nullptr;

    m_audioOutput->stop();
}

QStringList TtsController::voices() const
{
    return {
        QStringLiteral("af"),
        QStringLiteral("af_bella"),
        QStringLiteral("af_nicole"),
        QStringLiteral("af_sarah"),
        QStringLiteral("af_sky"),
        QStringLiteral("am_adam"),
        QStringLiteral("am_michael"),
        QStringLiteral("bf_emma"),
        QStringLiteral("bf_isabella"),
        QStringLiteral("bm_george"),
        QStringLiteral("bm_lewis"),
    };
}

void TtsController::setVoice(const QString &voice)
{
    if (m_voice == voice)
        return;
    m_voice = voice;

    // The prepared phrases are in the old voice (the cloud speaks in its one fixed voice, so there it
    // changes nothing); and the model reload (ttsReady false) happens BEFORE voiceChanged, so a host
    // re-preparing on it waits for ttsReady instead of synthesising in the old voice under the new
    // voice's key.
    if (!m_usingCloudTts) {
        invalidatePrepared("voice changed");
        if (modelFilesExist())
            triggerModelLoad();
    }
    emit voiceChanged();
}

// The cloud engine (AzureTTSEngine) speaks in one fixed voice whatever `voice` says.
TtsPhraseKey TtsController::phraseKey(const QString &trimmedText) const
{
    return m_usingCloudTts ? TtsPhraseKey{ QStringLiteral("cloud"), QString(), trimmedText }
                           : TtsPhraseKey{ QStringLiteral("local"), m_voice, trimmedText };
}

void TtsController::speak(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || m_ttsActive)
        return;
    const TtsPhraseKey key = phraseKey(t);

    // Prepared: play the cached audio now — no synthesis.
    if (const PreparedAudio *a = m_prepared.find(key)) {
        QElapsedTimer clock;
        clock.start();
        const bool hadReplay = !m_lastAudioCache.isEmpty();
        m_lastAudioCache  = a->pcm;
        m_lastAudioFormat = a->format;
        m_audioOutput->writeAudio(a->pcm, a->format);
        m_lastTtsLatencyMs = clock.elapsed();
        ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" from cache").arg(t));
        if (!hadReplay)
            emit canReplayChanged();
        // Queued: the caller has returned from speak() before it hears the latency.
        QMetaObject::invokeMethod(this, [this]() { emit lastTtsLatencyMsChanged(); }, Qt::QueuedConnection);
        return;
    }

    if (!m_ttsReady)
        return;

    // Being prepared right now: play it as it arrives (and still cache it).
    const quint64 gen = m_prepared.generation();
    if (m_job && m_job->key == key && m_job->generation == gen && !m_job->play) {
        m_job->speakClock.start();
        beginPlaying(*m_job);
        ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" while it is being prepared — playing as it arrives").arg(t));
        return;
    }
    for (TtsJob &j : m_jobs) {
        if (j.key == key && j.generation == gen && !j.play) {
            j.play = true;
            j.speakClock.start();
            setActive(true);
            ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" queued to be prepared — playing as it arrives").arg(t));
            return;
        }
    }

    TtsJob job;
    job.text       = t;
    job.key        = key;
    job.generation = gen;
    job.play       = true;
    job.speakClock.start();
    m_jobs.append(job);
    setActive(true);
    ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" not prepared — synthesising now").arg(t));
    pumpJobs();
}

void TtsController::prepare(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return;
    if (!m_ttsReady) {
        ppInfo() << qUtf8Printable(QStringLiteral("[TTS] prepare \"%1\" skipped: voice not ready").arg(t));
        return;
    }
    const TtsPhraseKey key = phraseKey(t);
    if (!m_prepared.beginPrepare(key))
        return;   // already cached, or already being prepared
    TtsJob job;
    job.text       = t;
    job.key        = key;
    job.generation = m_prepared.generation();
    job.prepare    = true;
    m_jobs.append(job);
    pumpJobs();
}

bool TtsController::isPrepared(const QString &text) const
{
    const QString t = text.trimmed();
    return !t.isEmpty() && m_prepared.contains(phraseKey(t));
}

void TtsController::stopSpeaking()
{
    // What is already written to the output stops too: a prepared phrase is written whole at speak()
    // and never sets ttsActive, so without this it would play on — and a line spoken next would wait
    // behind it (TrunkRoutine's turn-back cue cuts off its start cue this way).
    m_audioOutput->clearPending();
    // Waiting speeches go; waiting prepares stay. A queued speak of a phrase being prepared just
    // stops being played.
    for (int i = m_jobs.size() - 1; i >= 0; --i) {
        if (!m_jobs[i].play) continue;
        if (m_jobs[i].prepare) m_jobs[i].play = false;
        else                   m_jobs.removeAt(i);
    }
    if (m_job && m_job->play) {
        if (m_job->prepare) {
            // A prepare that was also being played: stop playing it, keep synthesising it.
            m_job->play = false;
        } else {
            m_job->aborted = true;
            QMetaObject::invokeMethod(m_worker, [this]() {
                m_worker->stop();
            }, Qt::QueuedConnection);
            return;   // ttsActive drops when the worker reports the synthesis finished
        }
    }
    setActive(false);
}

void TtsController::beginPlaying(TtsJob &job)
{
    job.play = true;
    setActive(true);
    if (!job.started)
        return;   // onSynthesisStarted takes over the replay buffer
    const bool hadReplay = !m_lastAudioCache.isEmpty();
    m_lastAudioCache  = job.pcm;
    m_lastAudioFormat = job.format;
    if (!job.pcm.isEmpty())
        m_audioOutput->writeAudio(job.pcm, job.format);
    if (hadReplay != !m_lastAudioCache.isEmpty())
        emit canReplayChanged();
}

void TtsController::setActive(bool on)
{
    if (m_ttsActive == on)
        return;
    m_ttsActive = on;
    emit ttsActiveChanged();
}

// One synthesis at a time: the next waiting job goes to the worker once the last has finished and
// the model is ready. A waiting PREPARE from before a voice/backend change is dropped (its phrase
// is re-prepared by the host); a waiting SPEAK is re-keyed to the voice now.
void TtsController::pumpJobs()
{
    while (!m_job && !m_jobs.isEmpty() && m_ttsReady) {
        TtsJob job = m_jobs.takeFirst();
        if (job.generation != m_prepared.generation()) {
            if (!job.play)
                continue;
            job.key        = phraseKey(job.text);
            job.generation = m_prepared.generation();
            job.prepare    = job.prepare && m_prepared.beginPrepare(job.key);
        }
        job.synthClock.start();
        m_job = job;
        const QString t = job.text;
        QMetaObject::invokeMethod(m_worker, [this, t]() {
            m_worker->synthesise(t);
        }, Qt::QueuedConnection);
        m_dispatchWatchdog.start();
    }
}

void TtsController::onDispatchTimeout()
{
    if (!m_job || m_job->started)
        return;
    ppWarn() << qUtf8Printable(QStringLiteral("[TTS] \"%1\" never started synthesising — dropped").arg(m_job->text));
    if (m_job->prepare)
        m_prepared.abandon(m_job->key, m_job->generation);
    const bool wasPlay = m_job->play;
    m_job.reset();
    if (wasPlay && !std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const TtsJob &j) { return j.play; }))
        setActive(false);
    pumpJobs();
}

void TtsController::dropJobs(const char *why)
{
    m_dispatchWatchdog.stop();
    if (m_job || !m_jobs.isEmpty())
        ppInfo() << "[TTS] dropped" << (m_jobs.size() + (m_job ? 1 : 0)) << "synthesis job(s):" << why;
    m_job.reset();
    m_jobs.clear();
    setActive(false);
}

void TtsController::invalidatePrepared(const char *why)
{
    const bool had = m_prepared.size() > 0;
    m_prepared.invalidate();
    // Waiting prepares are for the old voice; a waiting speak is re-keyed when it is dispatched.
    for (int i = m_jobs.size() - 1; i >= 0; --i)
        if (!m_jobs[i].play) m_jobs.removeAt(i);
    if (had)
        ppInfo() << "[TTS] prepared phrases cleared:" << why;
    emit preparedChanged();
}

void TtsController::toggleTtsBackend()
{
    // The persisted preference is the single source of truth — flipping it drives
    // applyCloudFallbackPref() (and keeps the Settings toggle in sync).
    if (!m_cloudToggleAvailable || !m_appSettings)
        return;
    m_appSettings->setCloudFallbackTts(!m_appSettings->cloudFallbackTts());
}

void TtsController::applyCloudFallbackPref()
{
    if (!m_appSettings)
        return;
    const QString apiKey = SecretsManager::read(QStringLiteral("azureTtsApiKey"));
    const bool    want   = m_appSettings->cloudFallbackTts() && !apiKey.isEmpty();
    // Rebuild when the desired backend changes, OR when staying on cloud but the
    // key value changed (e.g. the user corrected a bad key).
    if (want == m_usingCloudTts && (!want || apiKey == m_ttsCloudKey))
        return;
    if (want) {
        m_forceLocalTts    = false;
        m_usingCloudTts    = true;
        m_switchingToAzure = true;
        m_ttsCloudKey      = apiKey;
        QTimer::singleShot(0, this, [this, apiKey]() { switchToAzure(apiKey); });
    } else {
        m_forceLocalTts = true;   // prevent any implicit cloud re-engagement
        m_ttsCloudKey.clear();
        switchToKokoro();
    }
}

void TtsController::refreshCloudAvailability()
{
    const bool avail = ttsKeyPresent();
    if (avail != m_cloudToggleAvailable) {
        m_cloudToggleAvailable = avail;
        emit cloudTtsFallbackAvailableChanged();
    }
    // A newly-entered key may enable a force-cloud preference that was waiting on it.
    applyCloudFallbackPref();
}

bool TtsController::ttsKeyPresent() const
{
    return !SecretsManager::read(QStringLiteral("azureTtsApiKey")).isEmpty();
}

void TtsController::replayLastAudio()
{
    if (m_lastAudioCache.isEmpty() || m_ttsActive)
        return;
    m_audioOutput->writeAudio(m_lastAudioCache, m_lastAudioFormat);
}

// ── Worker slots ─────────────────────────────────────────────────────────────

void TtsController::onAudioReady(const QByteArray &data, const QAudioFormat &format)
{
    if (!m_job) {
        ppWarn() << "[TTS] audio with no synthesis in flight — dropped";
        return;
    }
    m_job->format = format;
    m_job->pcm.append(data);
    if (!m_job->play || m_job->aborted)
        return;   // a prepare: kept, never played; or stopped: its tail is not played either
    m_lastAudioFormat = format;
    m_lastAudioCache.append(data);
    m_audioOutput->writeAudio(data, format);
}

void TtsController::onModelReady()
{
    if (m_switchingToAzure)
        return;  // Kokoro loaded on CPU; Azure switch is pending — don't signal ready yet
    m_ttsReady = true;
    emit ttsReadyChanged();
    pumpJobs();
}

void TtsController::onModelFailed(const QString &error)
{
    ppError() << "[TTS] Model load failed:" << error;
    m_ttsReady = false;
    emit ttsReadyChanged();
}

void TtsController::onBackendChanged(const QString &backend)
{
    // The cloudFallbackTts preference (applied at startup and via
    // applyCloudFallbackPref) is authoritative — there is no implicit CPU→cloud
    // auto-fallback here. Just clear the in-flight switch flag and record the label
    // so OFF genuinely means "use the local engine" (Kokoro GPU or CPU).
    if (m_switchingToAzure)
        m_switchingToAzure = false;
    if (m_ttsBackend == backend)
        return;
    m_ttsBackend = backend;
    emit ttsBackendChanged();
}

void TtsController::onSynthesisStarted()
{
    if (!m_job) {
        ppWarn() << "[TTS] a synthesis started with none in flight";
        return;
    }
    m_dispatchWatchdog.stop();
    m_job->started = true;
    if (!m_job->play)
        return;
    const bool hadCache = !m_lastAudioCache.isEmpty();
    m_lastAudioCache.clear();
    m_ttsStartTimer.restart();
    if (hadCache)
        emit canReplayChanged();
}

void TtsController::onSynthesisFinished()
{
    if (!m_job) {
        ppWarn() << "[TTS] a synthesis finished with none in flight";
        return;
    }
    m_dispatchWatchdog.stop();
    TtsJob job = *m_job;
    m_job.reset();
    const qint64 synthMs = job.synthClock.isValid() ? job.synthClock.elapsed() : -1;

    if (job.prepare) {
        if (!job.aborted && !job.pcm.isEmpty()
            && m_prepared.store(job.key, PreparedAudio{ job.pcm, job.format }, job.generation)) {
            ppInfo() << qUtf8Printable(QStringLiteral("[TTS] prepared \"%1\" in %2 ms").arg(job.text).arg(synthMs));
            emit preparedChanged();
        } else {
            m_prepared.abandon(job.key, job.generation);
            ppInfo() << qUtf8Printable(QStringLiteral("[TTS] prepare \"%1\" not kept (%2)")
                                           .arg(job.text,
                                                job.generation != m_prepared.generation() ? QStringLiteral("voice changed meanwhile")
                                                : job.pcm.isEmpty()                        ? QStringLiteral("no audio")
                                                                                           : QStringLiteral("stopped")));
        }
    }

    if (job.play) {
        m_ttsStartTimer.invalidate();
        m_lastTtsLatencyMs = job.speakClock.isValid() ? job.speakClock.elapsed() : synthMs;
        if (job.aborted)
            ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" stopped").arg(job.text));
        else if (!job.prepare)
            ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" synthesised now (%2 ms)").arg(job.text).arg(m_lastTtsLatencyMs));
        else
            ppInfo() << qUtf8Printable(QStringLiteral("[TTS] speak \"%1\" played as it was prepared (%2 ms)").arg(job.text).arg(m_lastTtsLatencyMs));
        emit lastTtsLatencyMsChanged();
        if (!m_lastAudioCache.isEmpty())
            emit canReplayChanged();
    }
    if (job.play || job.aborted) {
        if (!std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const TtsJob &j) { return j.play; }))
            setActive(false);
    }
    pumpJobs();
}

void TtsController::onTtsError(const QString &message)
{
    ppWarn() << "[TTS]" << message;
}

// ── Worker helpers ────────────────────────────────────────────────────────────

void TtsController::connectWorkerSignals()
{
    connect(m_worker, &TtsWorker::audioReady,
            this, &TtsController::onAudioReady,
            Qt::QueuedConnection);
    connect(m_worker, &TtsWorker::modelReady,        this, &TtsController::onModelReady);
    connect(m_worker, &TtsWorker::modelFailed,       this, &TtsController::onModelFailed);
    connect(m_worker, &TtsWorker::backendChanged,    this, &TtsController::onBackendChanged);
    connect(m_worker, &TtsWorker::synthesisStarted,  this, &TtsController::onSynthesisStarted);
    connect(m_worker, &TtsWorker::synthesisFinished, this, &TtsController::onSynthesisFinished);
    connect(m_worker, &TtsWorker::errorOccurred,     this, &TtsController::onTtsError);
}

void TtsController::switchToAzure(const QString &apiKey)
{
    // Abort any in-progress Kokoro model download.
    m_downloader->abort();
    if (m_downloading) {
        m_downloading = false;
        emit downloadingChanged();
    }

    // The worker (and anything it was synthesising) goes; the prepared audio was the local voice's.
    dropJobs("switching to cloud speech");
    invalidatePrepared("switching to cloud speech");

    // Shut down the Kokoro worker thread.
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_worker;
    m_worker = nullptr;

    // Spin up an Azure TTS worker on the same thread.
    auto engine = std::make_unique<AzureTTSEngine>(apiKey);
    m_worker    = new TtsWorker(engine.release());
    m_worker->moveToThread(m_workerThread);
    connectWorkerSignals();
    m_workerThread->start();
    triggerModelLoad();
}

void TtsController::switchToKokoro()
{
    m_usingCloudTts = false;
    m_ttsReady = false;
    emit ttsReadyChanged();

    dropJobs("switching to local speech");
    invalidatePrepared("switching to local speech");

    m_workerThread->quit();
    m_workerThread->wait();
    delete m_worker;
    m_worker = nullptr;

    auto engine = TTSEngineFactory::create(TTSEngineFactory::Backend::Kokoro);
    m_worker    = new TtsWorker(engine.release());
    m_worker->moveToThread(m_workerThread);
    connectWorkerSignals();
    m_workerThread->start();
    triggerModelLoad();
}

// ── Downloader slots ──────────────────────────────────────────────────────────

void TtsController::onDownloadProgress(int fileIndex, int fileCount,
                                        qint64 bytesReceived, qint64 bytesTotal)
{
    const qreal fileShare = 1.0 / fileCount;
    const qreal fileDone  = (bytesTotal > 0)
                            ? static_cast<qreal>(bytesReceived) / bytesTotal
                            : 0.0;
    m_downloadProgress = (fileIndex * fileShare) + (fileDone * fileShare);
    emit downloadProgressChanged();
}

void TtsController::onDownloadFileComplete(const QString &path)
{
    setDownloadStatus(tr("Downloaded %1").arg(QFileInfo(path).fileName()));
}

void TtsController::onDownloadFinished()
{
    m_downloading      = false;
    m_downloadProgress = 1.0;
    emit downloadingChanged();
    emit downloadProgressChanged();
    setDownloadStatus(tr("Download complete"));
    triggerModelLoad();
}

void TtsController::onDownloadFailed(const QString &error)
{
    m_downloading = false;
    emit downloadingChanged();
    setDownloadStatus(tr("Download failed: %1").arg(error));
    ppError() << "[TTS] Download failed:" << error;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool TtsController::modelFilesExist() const
{
    return QFile::exists(resolveModelPath())
        && QFile::exists(resolveVoicePath())
        && QFile::exists(resolveTokensPath());
}

void TtsController::startDownload()
{
    const QString dir = modelDataDir();

    QList<ModelDownloader::Item> items;
    items.append({ QUrl(QString::fromLatin1(kModelUrl)),
                   dir + QStringLiteral("kokoro-v0_19-quantized.onnx") });
    items.append({ QUrl(QString::fromLatin1(kTokensUrl)),
                   dir + QStringLiteral("tokens.json") });

    for (const auto &v : kVoiceUrls) {
        items.append({ QUrl(QString::fromLatin1(kBaseUrl) + QString::fromLatin1(v.file)),
                       dir + QStringLiteral("voices/") + QString::fromLatin1(v.name) + QStringLiteral(".bin") });
    }

    m_downloading      = true;
    m_downloadProgress = 0.0;
    emit downloadingChanged();
    emit downloadProgressChanged();
    setDownloadStatus(tr("Downloading Kokoro model…"));

    m_downloader->download(items);
}

void TtsController::triggerModelLoad()
{
    m_ttsReady = false;
    emit ttsReadyChanged();

    const QString model  = resolveModelPath();
    const QString voice  = resolveVoicePath();
    const QString tokens = resolveTokensPath();
    QMetaObject::invokeMethod(m_worker, [this, model, voice, tokens]() {
        m_worker->loadModel(model, voice, tokens);
    }, Qt::QueuedConnection);
}

void TtsController::setDownloadStatus(const QString &status)
{
    m_downloadStatus = status;
    emit downloadStatusChanged();
}

// Model files are stored in the platform-standard app data directory so they
// survive across rebuilds and are not bundled inside the executable package.
//   Linux:   ~/.local/share/PinPointStudio/models/kokoro/
//   macOS:   ~/Library/Application Support/PinPointStudio/models/kokoro/
//   Windows: %APPDATA%/PinPointStudio/models/kokoro/

QString TtsController::modelDataDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/models/kokoro/");
}

QString TtsController::resolveModelPath() const
{
    const QString base = modelDataDir();
    for (const QString &name : {
             QStringLiteral("kokoro-v0_19-quantized.onnx"),
             QStringLiteral("kokoro-v0_19.onnx") }) {
        if (QFile::exists(base + name))
            return base + name;
    }
    return base + QStringLiteral("kokoro-v0_19-quantized.onnx");
}

QString TtsController::resolveVoicePath() const
{
    return modelDataDir() + QStringLiteral("voices/") + m_voice + QStringLiteral(".bin");
}

QString TtsController::resolveTokensPath() const
{
    return modelDataDir() + QStringLiteral("tokens.json");
}
