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

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <vector>

class QMediaPlayer;
class QVideoSink;
class QTimer;

// ShotReplayController (QML context property `shotReplay`) — replays a shot from
// disk (its MP4(s) + swing.json), the disk-backed counterpart to ShotProcessor's
// live ¼× replay. A reloaded shot (or any older carousel entry) has no in-memory
// SwingWindow, so it cannot use ShotProcessor; this controller owns one
// QMediaPlayer per recorded video stream and reports a synchronised playhead so
// the metric graph (PpMetricGraph) tracks the video.
//
// Domain: everything the graph sees is WINDOW-RELATIVE µs (capture time, t0
// subtracted) — startUs..endUs span the captured frames, positionUs is the
// playhead, analysisDetail.series/phases are offset to the same 0-based domain on
// load. (Exporter stream frames.t_us are already window-relative; analysis t_us
// are absolute, so they are offset by clock.t0_us here on load.)
//
// Each MP4 plays at its baked-in 30 fps, which already slows high-fps captures —
// so each stream's playbackRate is computed to make the whole window replay at a
// fixed capture-time speed (¼× / ⅛×) regardless of the camera's capture fps. The
// graph playhead is derived by mapping the master player's reported position back
// to capture time via that stream's frames.t_us table.
class ShotReplayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool        active         READ active         NOTIFY activeChanged)
    Q_PROPERTY(bool        playing        READ playing        NOTIFY playingChanged)
    Q_PROPERTY(int         shotId         READ shotId         NOTIFY activeChanged)
    Q_PROPERTY(int         streamCount    READ streamCount    NOTIFY activeChanged)
    // Which surface renders the replay — "panel" (inline review popup) or
    // "screen" (the full main-screen PpReplayStage). One QMediaPlayer has one
    // sink, so only the matching surface binds its VideoOutputs.
    Q_PROPERTY(QString     target         READ target         NOTIFY activeChanged)
    Q_PROPERTY(QString     mode           READ mode           NOTIFY modeChanged)
    Q_PROPERTY(qint64      positionUs     READ positionUs     NOTIFY positionChanged)
    Q_PROPERTY(qint64      startUs        READ startUs        NOTIFY spanChanged)
    Q_PROPERTY(qint64      endUs          READ endUs          NOTIFY spanChanged)
    Q_PROPERTY(qint64      impactUs       READ impactUs       NOTIFY spanChanged)
    Q_PROPERTY(QVariantMap analysisDetail READ analysisDetail NOTIFY activeChanged)

public:
    explicit ShotReplayController(QObject *parent = nullptr);
    ~ShotReplayController() override;

    bool        active()         const { return m_active; }
    bool        playing()        const { return m_playing; }
    int         shotId()         const { return m_shotId; }
    int         streamCount()    const { return int(m_streams.size()); }
    qint64      positionUs()     const { return m_positionUs; }
    qint64      startUs()        const { return m_startUs; }
    qint64      endUs()          const { return m_endUs; }
    qint64      impactUs()       const { return m_impactUs; }
    QVariantMap analysisDetail() const { return m_analysisDetail; }
    QString     target()         const { return m_target; }
    QString     mode()           const { return m_mode; }

    // Start replaying the shot at `swingDir` for carousel row `shotId`. mode
    // "slow" replays at ⅛× capture speed, anything else at ¼× (matching the live
    // replay); target "screen" renders on the main-screen stage, else the review
    // panel. Returns false if the doc has no playable video stream.
    Q_INVOKABLE bool start(int shotId, const QString &swingDir, const QString &mode,
                           const QString &target = QStringLiteral("panel"));
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seekToFraction(double frac);   // 0..1 over the window
    Q_INVOKABLE void seekToUs(qint64 us);
    Q_INVOKABLE void stepFrame(int delta);          // ±N frames on the master stream
    Q_INVOKABLE void setMode(const QString &mode);  // switch ¼×/⅛× mid-replay
    Q_INVOKABLE void beginScrub();                  // pause on slider grab
    Q_INVOKABLE void endScrub();                    // restore play state on release

    // Bind a QML VideoOutput's sink to stream `index` (face-on = 0). Persists
    // across shots (the VideoOutput outlives a single replay), so it may be
    // called before start(); a live player at that index is rebound immediately.
    Q_INVOKABLE void setVideoSink(int index, QVideoSink *sink);

signals:
    void activeChanged();
    void playingChanged();
    void positionChanged();
    void spanChanged();
    void modeChanged();

private slots:
    void onTick();

private:
    struct Stream {
        QMediaPlayer        *player = nullptr;
        std::vector<int64_t> tUs;            // window-relative frame stamps (µs)
        double               playbackFps = 30.0;
    };

    void teardown();
    void setPlaying(bool p);
    void setPositionUs(qint64 us);
    void seekPlayersTo(qint64 captureUs);    // map capture µs → each MP4 and setPosition
    void applyPlaybackRates();               // per-stream rate from m_mode + span
    qint64 captureUsForStream(int streamIdx, qint64 mp4Ms) const;
    qint64 mp4MsForStream(int streamIdx, qint64 captureUs) const;

    bool        m_active     = false;
    bool        m_playing    = false;
    int         m_shotId     = -1;
    QString     m_swingDir;
    QString     m_target     = QStringLiteral("panel");
    QString     m_mode       = QStringLiteral("normal");
    bool        m_wasPlayingBeforeScrub = false;
    qint64      m_startUs    = 0;
    qint64      m_endUs      = 0;
    qint64      m_impactUs   = -1;
    qint64      m_positionUs = 0;
    QVariantMap m_analysisDetail;

    std::vector<Stream>               m_streams;
    QVector<QPointer<QVideoSink>>     m_sinks;   // bound from QML, by stream index
    QTimer                           *m_timer = nullptr;
};
