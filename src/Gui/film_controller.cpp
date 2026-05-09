#include "film_controller.h"

#include "video_preprocessor_base.h"
#include "video_overlay_base.h"

#ifdef HAVE_OPENCV
#include "video_preprocessor_opencv.h"
#include "video_overlay_pose.h"
#include "pose_estimator_base.h"
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
#include "pose_estimator_movenet.h"
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_SEGMENTER) && defined(HAVE_ONNXRUNTIME)
#include "person_segmenter.h"
#endif

#include <QAudioOutput>
#ifdef HAVE_OPENCV
#  include <opencv2/imgproc.hpp>
#endif
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSink>

static constexpr qint64 kSeekStepMs = 10'000;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString FilmController::ytdlpBinaryPath()
{
#ifdef HAVE_YTDLP
    return QCoreApplication::applicationDirPath()
         + QLatin1Char('/') + QStringLiteral(YTDLP_BINARY_NAME);
#else
    return QString();
#endif
}

static QString filmCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/film-cache");
}

// ---------------------------------------------------------------------------
// Static helpers — background blur for pre-MoveNet suppression
// ---------------------------------------------------------------------------

#ifdef HAVE_OPENCV
// Blends a sharp foreground (person mask ≈ 1) against a blurred background.
// `mask` is CV_32F [0,1] at the same size as `src`.
static cv::Mat blurBackgroundForPose(const cv::Mat &src, const cv::Mat &mask)
{
    // Strongly blur the full frame.
    cv::Mat blurred;
    cv::GaussianBlur(src, blurred, cv::Size(71, 71), 0);

    // Feather the segmentation mask edges so the blend is smooth.
    cv::Mat softMask = mask.clone();
    const int edgeBlur = (std::max(src.cols, src.rows) / 20) | 1; // odd, ~5% of longest dim
    cv::GaussianBlur(softMask, softMask, cv::Size(edgeBlur, edgeBlur), 0);

    cv::Mat invMask = 1.0f - softMask;
    std::vector<cv::Mat> mChs = { softMask, softMask, softMask };
    std::vector<cv::Mat> iChs = { invMask,  invMask,  invMask  };
    cv::Mat mask3, invMask3;
    cv::merge(mChs, mask3);
    cv::merge(iChs, invMask3);

    cv::Mat srcF, blurF;
    src.convertTo(srcF, CV_32FC3, 1.0 / 255.0);
    blurred.convertTo(blurF, CV_32FC3, 1.0 / 255.0);

    cv::Mat resultF = srcF.mul(mask3) + blurF.mul(invMask3);
    cv::Mat result;
    resultF.convertTo(result, CV_8UC3, 255.0);
    return result;
}

// Convert a BGR cv::Mat to a QVideoFrame for display.
static QVideoFrame bgrMatToFrame(const cv::Mat &bgr)
{
    cv::Mat bgra;
    cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
    // QImage does not take ownership; copy so the Mat can be released.
    QImage img(bgra.data, bgra.cols, bgra.rows,
               static_cast<qsizetype>(bgra.step[0]),
               QImage::Format_ARGB32);
    return QVideoFrame(img.copy());
}
#endif // HAVE_OPENCV

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

FilmController::FilmController(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
    , m_captureSink(new QVideoSink(this))
{
    m_player->setAudioOutput(m_audio);
    m_player->setVideoSink(m_captureSink);

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this,     &FilmController::onPlayerStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this,     &FilmController::onPlayerError);
    connect(m_player, &QMediaPlayer::positionChanged,
            this,     &FilmController::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,
            this,     &FilmController::onDurationChanged);

#ifdef HAVE_OPENCV
    // ── Preprocess thread ────────────────────────────────────────────────────
    m_preprocessThread = new QThread(this);
    m_preprocessThread->setObjectName(QStringLiteral("FilmPreprocessThread"));
    m_preprocessor = new VideoPreprocessorOpenCV();
    m_preprocessor->moveToThread(m_preprocessThread);
    connect(m_preprocessor, &VideoPreprocessorBase::preprocessStatsUpdated,
            this, &FilmController::onPreprocessStats, Qt::QueuedConnection);
    m_preprocessThread->start();

    // ── Overlay thread ───────────────────────────────────────────────────────
    m_overlayThread = new QThread(this);
    m_overlayThread->setObjectName(QStringLiteral("FilmOverlayThread"));
    m_overlay = new VideoOverlayPose();
    m_overlay->moveToThread(m_overlayThread);
    connect(m_overlay, &VideoOverlayBase::frameReady,
            this, &FilmController::onAnnotatedFrame, Qt::QueuedConnection);
    m_overlayThread->start();

    // Capture sink: store each frame for annotate(), and forward to overlay for display.
    // Frames are NOT automatically sent to the preprocessor — annotation is on-demand only.
    connect(m_captureSink, &QVideoSink::videoFrameChanged,
            this, &FilmController::onCurrentFrame, Qt::QueuedConnection);
    connect(m_captureSink, &QVideoSink::videoFrameChanged,
            m_overlay, &VideoOverlayBase::overlayFrame, Qt::QueuedConnection);

#if defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    // ── Pose estimator thread ────────────────────────────────────────────────
    m_poseThread = new QThread(this);
    m_poseThread->setObjectName(QStringLiteral("FilmPoseThread"));
    auto *mn = new PoseEstimatorMoveNet();
    m_poseEstimator = mn;
    m_poseEstimator->moveToThread(m_poseThread);
    connect(m_poseThread, &QThread::started, mn, &PoseEstimatorMoveNet::load);
    connect(m_poseEstimator, &PoseEstimatorBase::poseStatsUpdated,
            this, &FilmController::onPoseStats, Qt::QueuedConnection);
    connect(m_poseEstimator, &PoseEstimatorBase::poseBackendReady,
            this, &FilmController::onPoseBackendReady, Qt::QueuedConnection);

    // poseEstimated → FilmController::onPoseEstimated (not directly to overlay)
    // so we can sequence updatePose + overlayFrame on the stored annotation frame.
    connect(m_poseEstimator, &PoseEstimatorBase::poseEstimated,
            this, &FilmController::onPoseEstimated, Qt::QueuedConnection);

    m_poseThread->start();
#endif // HAVE_MOVENET && HAVE_ONNXRUNTIME

#if defined(HAVE_SEGMENTER)
    if (m_segmenter.load())
        qDebug() << "[Film] Person segmenter ready";
    else
        qDebug() << "[Film] Person segmenter unavailable — annotation runs without background suppression";
#endif

#endif // HAVE_OPENCV
}

FilmController::~FilmController()
{
    if (m_ytdlp) {
        m_ytdlp->kill();
        m_ytdlp->waitForFinished(2000);
    }

#ifdef HAVE_OPENCV
    auto drainThread = [](QThread *t, QObject *obj) {
        if (t && t->isRunning()) {
            QMetaObject::invokeMethod(obj, [obj]() {
                obj->moveToThread(QCoreApplication::instance()->thread());
            }, Qt::BlockingQueuedConnection);
            t->quit();
            t->wait();
        }
    };
    drainThread(m_preprocessThread, m_preprocessor);
    delete m_preprocessor;
    drainThread(m_poseThread,       m_poseEstimator);
    delete m_poseEstimator;
    drainThread(m_overlayThread,    m_overlay);
    delete m_overlay;
#endif
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

bool    FilmController::isDownloading()           const { return m_downloading; }
double  FilmController::downloadProgress()        const { return m_downloadProgress; }
QString FilmController::downloadStatus()          const { return m_downloadStatus; }
bool    FilmController::isPlaying()               const { return m_player->playbackState() == QMediaPlayer::PlayingState; }
bool    FilmController::hasMedia()                const { return m_hasMedia; }
qint64  FilmController::position()                const { return m_position; }
qint64  FilmController::duration()                const { return m_duration; }
double  FilmController::preprocessAvgMs()         const { return m_preprocessAvgMs; }
double  FilmController::poseAvgMs()               const { return m_poseAvgMs; }
double  FilmController::poseFps()                 const { return m_poseFps; }
QString FilmController::poseBackendLabel()        const { return m_poseBackendLabel; }
int     FilmController::moveNetModel()            const { return m_moveNetModel; }

bool FilmController::isAnnotating()            const { return m_annotating; }

bool FilmController::poseAvailable() const
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    return true;
#else
    return false;
#endif
}

bool FilmController::moveNetThunderAvailable() const
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    return PoseEstimatorMoveNet::isVariantAvailable(PoseEstimatorMoveNet::ModelVariant::Thunder);
#else
    return false;
#endif
}

bool FilmController::ytdlpAvailable() const
{
    return QFile::exists(ytdlpBinaryPath());
}

// ---------------------------------------------------------------------------
// Invokables
// ---------------------------------------------------------------------------

void FilmController::setVideoSink(QVideoSink *sink)
{
    m_displaySink = sink;
}

void FilmController::downloadUrl(const QString &url, const QString &browser)
{
    const QString bin = ytdlpBinaryPath();
    if (!QFile::exists(bin)) {
        setDownloadStatus(QStringLiteral("yt-dlp not found in app bundle"));
        return;
    }
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return;

    m_player->stop();
    m_player->setSource(QUrl());
    m_hasMedia = false;
    emit hasMediaChanged();

    if (m_ytdlp) {
        m_ytdlp->kill();
        m_ytdlp->waitForFinished(2000);
        m_ytdlp->deleteLater();
        m_ytdlp = nullptr;
    }

    QDir().mkpath(filmCacheDir());

    m_downloading      = true;
    m_downloadProgress = 0.0;
    m_downloadPhase    = 0;
    m_pendingFilePath.clear();
    emit isDownloadingChanged();
    emit downloadProgressChanged();
    setDownloadStatus(QStringLiteral("Starting download…"));

    m_ytdlp = new QProcess(this);
    connect(m_ytdlp, &QProcess::readyReadStandardOutput,
            this, &FilmController::onYtdlpOutput);
    connect(m_ytdlp, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FilmController::onYtdlpFinished);

    QStringList args;
    if (!browser.isEmpty()) {
#ifdef Q_OS_LINUX
        // Append +basictext so yt-dlp uses Chromium's hardcoded fallback key
        // instead of the system keyring — avoids the secretstorage dependency
        // that is not available in the bundled binary.
        const QString browserArg = browser + QStringLiteral("+basictext");
#else
        const QString browserArg = browser;
#endif
        args << QStringLiteral("--cookies-from-browser") << browserArg;
    }
    args << QStringLiteral("-f")
         // Prefer H.264 video + AAC audio; explicitly exclude AV1 (av01) which
         // Qt Multimedia's FFmpeg backend cannot software-decode on all platforms.
         << QStringLiteral("bestvideo[height<=1080][vcodec^=avc1]+bestaudio[ext=m4a]"
                            "/bestvideo[height<=1080][vcodec!*=av01][ext=mp4]+bestaudio[ext=m4a]"
                            "/best[height<=1080][ext=mp4][vcodec!*=av01]"
                            "/best[height<=1080][ext=mp4]/best")
         << QStringLiteral("--merge-output-format") << QStringLiteral("mp4")
         << QStringLiteral("--newline")
         << QStringLiteral("--no-overwrites")
         << QStringLiteral("-o") << (filmCacheDir() + QStringLiteral("/%(id)s.%(ext)s"))
         << trimmed;

    qDebug() << "[Film] Starting:" << bin << args;
    m_ytdlp->start(bin, args);
}

void FilmController::cancelDownload()
{
    if (m_ytdlp) {
        m_ytdlp->kill();
        m_ytdlp->deleteLater();
        m_ytdlp     = nullptr;
        m_downloading = false;
        emit isDownloadingChanged();
        setDownloadStatus(QStringLiteral("Download cancelled"));
    }
}

void FilmController::play()   { if (m_hasMedia) m_player->play(); }
void FilmController::pause()  { m_player->pause(); }
void FilmController::stop()   { m_player->stop(); }

void FilmController::seekTo(qint64 positionMs)
{
    clearOverlayPose();
    m_player->setPosition(positionMs);
}

void FilmController::seekForward()
{
    clearOverlayPose();
    if (m_duration > 0)
        m_player->setPosition(qMin(m_position + kSeekStepMs, m_duration));
}

void FilmController::seekBack()
{
    clearOverlayPose();
    m_player->setPosition(qMax(m_position - kSeekStepMs, qint64(0)));
}

void FilmController::beginScrub()
{
    m_wasPlaying = isPlaying();
    if (m_wasPlaying)
        m_player->pause();
    clearOverlayPose();
}

void FilmController::endScrub()
{
    if (m_wasPlaying)
        m_player->play();
    m_wasPlaying = false;
}

void FilmController::clearOverlayPose()
{
    if (m_overlay)
        QMetaObject::invokeMethod(m_overlay, "clearPose", Qt::QueuedConnection);
}

void FilmController::annotate()
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    if (m_annotating || !m_currentFrame.isValid() || !m_poseEstimator)
        return;

    m_annotateFrame = m_currentFrame;
    m_annotating    = true;
    emit isAnnotatingChanged();

    // Convert QVideoFrame → BGR cv::Mat.
    QImage img = m_annotateFrame.toImage().convertToFormat(QImage::Format_ARGB32);
    cv::Mat bgra(img.height(), img.width(), CV_8UC4,
                 const_cast<uchar *>(img.constBits()),
                 static_cast<size_t>(img.bytesPerLine()));
    cv::cvtColor(bgra, m_rawMat, cv::COLOR_BGRA2BGR);
    m_rawMat = m_rawMat.clone();

#if defined(HAVE_SEGMENTER)
    if (m_segmenter.isReady()) {
        // Segment the person → blur the background → feed clean frame to MoveNet.
        const cv::Mat mask = m_segmenter.segment(m_rawMat);
        if (!mask.empty()) {
            m_blurredMat   = blurBackgroundForPose(m_rawMat, mask);
            m_blurredFrame = bgrMatToFrame(m_blurredMat);
            QMetaObject::invokeMethod(m_poseEstimator, "estimatePose",
                                      Qt::QueuedConnection,
                                      Q_ARG(cv::Mat, m_blurredMat));
            return;
        }
    }
#endif
    // Segmenter unavailable — run MoveNet on the raw frame.
    m_blurredMat   = m_rawMat;
    m_blurredFrame = bgrMatToFrame(m_rawMat);
    QMetaObject::invokeMethod(m_poseEstimator, "estimatePose",
                              Qt::QueuedConnection,
                              Q_ARG(cv::Mat, m_rawMat));
#endif
}

void FilmController::selectMoveNetModel(int variant)
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    if (variant == m_moveNetModel)
        return;
    m_moveNetModel = variant;
    emit moveNetModelChanged();
    m_poseAvgMs = 0.0; emit poseAvgMsChanged();
    m_poseFps   = 0.0; emit poseFpsChanged();
    m_poseBackendLabel.clear(); emit poseBackendLabelChanged();
    if (m_poseEstimator)
        QMetaObject::invokeMethod(m_poseEstimator, "reloadModel",
                                  Qt::QueuedConnection, Q_ARG(int, variant));
#else
    Q_UNUSED(variant)
#endif
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void FilmController::setDownloadStatus(const QString &msg)
{
    if (m_downloadStatus == msg)
        return;
    m_downloadStatus = msg;
    emit downloadStatusChanged();
}

void FilmController::openLocalFile(const QString &path)
{
    qDebug() << "[Film] Opening local file:" << path;
    m_player->setSource(QUrl::fromLocalFile(path));
    m_hasMedia = true;
    emit hasMediaChanged();
    setDownloadStatus(QString());
    m_downloadProgress = 1.0;
    emit downloadProgressChanged();
    m_player->play();
}

// ---------------------------------------------------------------------------
// Slots — frame capture & annotation
// ---------------------------------------------------------------------------

void FilmController::onCurrentFrame(const QVideoFrame &frame)
{
    m_currentFrame = frame;
}

#ifdef HAVE_OPENCV
void FilmController::onPoseEstimated(const PoseResult &result)
{
    // Single pass — segmenter already cleaned the frame before MoveNet ran.
    // Display the (segmenter-blurred) frame with the skeleton on top.
    auto *poseOverlay = static_cast<VideoOverlayPose *>(m_overlay);
    QMetaObject::invokeMethod(poseOverlay, "updatePose",
                              Qt::QueuedConnection, Q_ARG(PoseResult, result));
    QMetaObject::invokeMethod(m_overlay, "overlayFrame",
                              Qt::QueuedConnection, Q_ARG(QVideoFrame, m_blurredFrame));
    m_annotating = false;
    emit isAnnotatingChanged();
}
#endif

// ---------------------------------------------------------------------------
// Slots — yt-dlp
// ---------------------------------------------------------------------------

void FilmController::onYtdlpOutput()
{
    static const QRegularExpression rePercent(QStringLiteral(R"(\[download\]\s+([\d.]+)%)"));
    static const QRegularExpression reDest(QStringLiteral(R"(\[download\] Destination: (.+))"));
    static const QRegularExpression reAlready(QStringLiteral(R"(\[download\] (.+) has already been downloaded)"));
    static const QRegularExpression reMerge(QStringLiteral(R"re(\[Merger\] Merging formats into "(.+)")re"));

    while (m_ytdlp && m_ytdlp->canReadLine()) {
        const QString line = QString::fromUtf8(m_ytdlp->readLine()).trimmed();
        qDebug() << "[yt-dlp]" << line;

        auto dm = reDest.match(line);
        if (dm.hasMatch()) {
            ++m_downloadPhase;
            m_pendingFilePath = dm.captured(1).trimmed();
            continue;
        }

        auto am = reAlready.match(line);
        if (am.hasMatch()) {
            m_pendingFilePath = am.captured(1).trimmed();
            setDownloadStatus(QStringLiteral("Already downloaded — loading…"));
            m_downloadProgress = 1.0;
            emit downloadProgressChanged();
            continue;
        }

        auto mm = reMerge.match(line);
        if (mm.hasMatch()) {
            m_pendingFilePath = mm.captured(1).trimmed();
            m_downloadProgress = 0.95;
            emit downloadProgressChanged();
            setDownloadStatus(QStringLiteral("Merging audio and video…"));
            continue;
        }

        auto pm = rePercent.match(line);
        if (pm.hasMatch()) {
            bool ok;
            const double pct = pm.captured(1).toDouble(&ok);
            if (ok) {
                // Map phase 1 (video) → [0, 0.45), phase 2 (audio) → [0.45, 0.9)
                const double base  = (m_downloadPhase >= 2) ? 0.45 : 0.0;
                const double scale = 0.45;
                m_downloadProgress = base + (pct / 100.0) * scale;
                emit downloadProgressChanged();

                const QString label = (m_downloadPhase >= 2)
                    ? QStringLiteral("Downloading audio… %1%")
                    : QStringLiteral("Downloading video… %1%");
                setDownloadStatus(label.arg(pct, 0, 'f', 1));
            }
        }
    }
}

void FilmController::onYtdlpFinished(int exitCode, QProcess::ExitStatus)
{
    const QString stderr = m_ytdlp
        ? QString::fromUtf8(m_ytdlp->readAllStandardError()).trimmed()
        : QString();

    if (m_ytdlp) {
        m_ytdlp->deleteLater();
        m_ytdlp = nullptr;
    }

    m_downloading = false;
    emit isDownloadingChanged();

    if (exitCode != 0) {
        qWarning() << "[Film] yt-dlp exited" << exitCode << "—" << stderr.left(200);
        setDownloadStatus(QStringLiteral("Download failed: ") + stderr.left(120));
        return;
    }

    if (!m_pendingFilePath.isEmpty() && QFile::exists(m_pendingFilePath)) {
        openLocalFile(m_pendingFilePath);
    } else {
        setDownloadStatus(QStringLiteral("Could not locate downloaded file"));
    }
}

// ---------------------------------------------------------------------------
// Slots — player
// ---------------------------------------------------------------------------

void FilmController::onPlayerStateChanged(QMediaPlayer::PlaybackState)
{
    emit isPlayingChanged();
}

void FilmController::onPlayerError(QMediaPlayer::Error, const QString &errorString)
{
    qWarning() << "[Film] Player error:" << errorString;
    setDownloadStatus(QStringLiteral("Playback error: ") + errorString);
}

void FilmController::onPositionChanged(qint64 pos)
{
    m_position = pos;
    emit positionChanged();
}

void FilmController::onDurationChanged(qint64 dur)
{
    m_duration = dur;
    emit durationChanged();
}

void FilmController::onAnnotatedFrame(const QVideoFrame &frame)
{
    if (m_displaySink && frame.isValid())
        m_displaySink->setVideoFrame(frame);
}

// ---------------------------------------------------------------------------
// Slots — pipeline stats
// ---------------------------------------------------------------------------

void FilmController::onPreprocessStats(double avgMs)
{
    if (qFuzzyCompare(m_preprocessAvgMs, avgMs)) return;
    m_preprocessAvgMs = avgMs;
    emit preprocessAvgMsChanged();
}

void FilmController::onPoseStats(double avgMs, double fps)
{
    if (!qFuzzyCompare(m_poseAvgMs, avgMs)) { m_poseAvgMs = avgMs; emit poseAvgMsChanged(); }
    if (!qFuzzyCompare(m_poseFps,   fps))   { m_poseFps   = fps;   emit poseFpsChanged();   }
}

void FilmController::onPoseBackendReady(const QString &label)
{
    if (m_poseBackendLabel == label) return;
    m_poseBackendLabel = label;
    emit poseBackendLabelChanged();
}
