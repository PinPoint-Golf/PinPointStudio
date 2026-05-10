#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QMetaObject>
#include <QProcess>
#include <QVariant>
#include <QVideoFrame>

class QAudioOutput;
class QThread;
class QVideoSink;
class VideoPreprocessorBase;
class VideoOverlayBase;

#ifdef HAVE_OPENCV
#include "pose_estimator_base.h" // PoseResult
class PoseEstimatorBase;
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_SEGMENTER) && defined(HAVE_ONNXRUNTIME)
#include "person_segmenter.h"
#endif

class FilmController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    isDownloading      READ isDownloading      NOTIFY isDownloadingChanged)
    Q_PROPERTY(double  downloadProgress   READ downloadProgress   NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString downloadStatus     READ downloadStatus     NOTIFY downloadStatusChanged)
    Q_PROPERTY(bool    isPlaying          READ isPlaying          NOTIFY isPlayingChanged)
    Q_PROPERTY(bool    hasMedia           READ hasMedia           NOTIFY hasMediaChanged)
    Q_PROPERTY(qint64  position           READ position           NOTIFY positionChanged)
    Q_PROPERTY(qint64  duration           READ duration           NOTIFY durationChanged)
    Q_PROPERTY(double  preprocessAvgMs    READ preprocessAvgMs    NOTIFY preprocessAvgMsChanged)
    Q_PROPERTY(double  poseAvgMs          READ poseAvgMs          NOTIFY poseAvgMsChanged)
    Q_PROPERTY(double  poseFps            READ poseFps            NOTIFY poseFpsChanged)
    Q_PROPERTY(QString poseBackendLabel   READ poseBackendLabel   NOTIFY poseBackendLabelChanged)
    Q_PROPERTY(int     moveNetModel       READ moveNetModel       NOTIFY moveNetModelChanged)
    Q_PROPERTY(bool    isAnnotating       READ isAnnotating       NOTIFY isAnnotatingChanged)
    Q_PROPERTY(bool    poseAvailable      READ poseAvailable      CONSTANT)
    Q_PROPERTY(bool    moveNetThunderAvailable READ moveNetThunderAvailable CONSTANT)
    Q_PROPERTY(bool    ytdlpAvailable     READ ytdlpAvailable     CONSTANT)
    Q_PROPERTY(QVariantList cacheEntries READ cacheEntries NOTIFY cacheEntriesChanged)
    Q_PROPERTY(QString currentFilePath   READ currentFilePath    NOTIFY currentFilePathChanged)

public:
    explicit FilmController(QObject *parent = nullptr);
    ~FilmController() override;

    bool    isDownloading()           const;
    double  downloadProgress()        const;
    QString downloadStatus()          const;
    bool    isPlaying()               const;
    bool    hasMedia()                const;
    qint64  position()                const;
    qint64  duration()                const;
    double  preprocessAvgMs()         const;
    double  poseAvgMs()               const;
    double  poseFps()                 const;
    QString poseBackendLabel()        const;
    int     moveNetModel()            const;
    bool    isAnnotating()            const;
    bool         poseAvailable()           const;
    bool         moveNetThunderAvailable() const;
    bool         ytdlpAvailable()          const;
    QVariantList cacheEntries()            const;
    QString      currentFilePath()         const;

    Q_INVOKABLE void setVideoSink(QVideoSink *sink);
    Q_INVOKABLE void downloadUrl(const QString &url, const QString &browser);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void refreshCache();
    Q_INVOKABLE void openCacheFile(const QString &path);
    Q_INVOKABLE void deleteCacheFile(const QString &path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekTo(qint64 positionMs);
    Q_INVOKABLE void seekForward();
    Q_INVOKABLE void seekBack();
    Q_INVOKABLE void beginScrub();   // call when slider drag starts
    Q_INVOKABLE void endScrub();     // call when slider drag ends
    Q_INVOKABLE void annotate();     // run pose on the current paused frame
    Q_INVOKABLE void selectMoveNetModel(int variant);

signals:
    void isDownloadingChanged();
    void downloadProgressChanged();
    void downloadStatusChanged();
    void isPlayingChanged();
    void hasMediaChanged();
    void positionChanged();
    void durationChanged();
    void preprocessAvgMsChanged();
    void poseAvgMsChanged();
    void poseFpsChanged();
    void poseBackendLabelChanged();
    void moveNetModelChanged();
    void isAnnotatingChanged();
    void cacheEntriesChanged();
    void currentFilePathChanged();

private slots:
    void onCurrentFrame(const QVideoFrame &frame);
    void onYtdlpOutput();
    void onYtdlpFinished(int exitCode, QProcess::ExitStatus status);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 dur);
    void onAnnotatedFrame(const QVideoFrame &frame);
    void onPreprocessStats(double avgMs);
    void onPoseStats(double avgMs, double fps);
    void onPoseBackendReady(const QString &label);
#ifdef HAVE_OPENCV
    void onPoseEstimated(const PoseResult &result);
#endif

private:
    static QString ytdlpBinaryPath();
    void setDownloadStatus(const QString &msg);
    void openLocalFile(const QString &path);
    void clearOverlayPose();
    void scanCacheDir();
    void startNextProbe();
    void updateCacheEntry(const QString &path, const QString &thumbUrl, qint64 durationMs);

    // Cache list
    QVariantList  m_cacheEntries;
    QString       m_currentFilePath;
    QStringList   m_probeQueue;
    QString       m_probingPath;
    QMediaPlayer *m_probePlayer  = nullptr;
    QVideoSink   *m_probeSink    = nullptr;
    QMetaObject::Connection m_probeDurConn;
    QMetaObject::Connection m_probeFrameConn;

    // Playback
    QMediaPlayer *m_player       = nullptr;
    QAudioOutput *m_audio        = nullptr;
    QVideoSink   *m_captureSink  = nullptr;
    QVideoSink   *m_displaySink  = nullptr;

    // Current decoded frame (updated every frame, used by annotate())
    QVideoFrame   m_currentFrame;

    // Download
    QProcess *m_ytdlp            = nullptr;
    bool      m_downloading      = false;
    double    m_downloadProgress = 0.0;
    QString   m_downloadStatus;
    QString   m_pendingFilePath;
    int       m_downloadPhase    = 0;

    // Playback state
    bool    m_hasMedia   = false;
    qint64  m_position   = 0;
    qint64  m_duration   = 0;
    bool    m_wasPlaying = false; // saved across scrub drag

    // Annotation state
    bool        m_annotating    = false;
    QVideoFrame m_annotateFrame;
    QVideoFrame m_blurredFrame; // segmentation-blurred frame shown after annotation
#ifdef HAVE_OPENCV
    cv::Mat     m_rawMat;       // BGR mat of annotate frame
    cv::Mat     m_blurredMat;   // background-blurred mat fed to MoveNet
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_SEGMENTER) && defined(HAVE_ONNXRUNTIME)
    PersonSegmenter m_segmenter;
#endif

    // Pose pipeline
    QThread               *m_preprocessThread = nullptr;
    VideoPreprocessorBase *m_preprocessor     = nullptr;
    double                 m_preprocessAvgMs  = 0.0;

    QThread          *m_overlayThread  = nullptr;
    VideoOverlayBase *m_overlay        = nullptr;

#ifdef HAVE_OPENCV
    QThread           *m_poseThread    = nullptr;
    PoseEstimatorBase *m_poseEstimator = nullptr;
#endif
    double  m_poseAvgMs        = 0.0;
    double  m_poseFps          = 0.0;
    QString m_poseBackendLabel;
    int     m_moveNetModel     = 0;
};
