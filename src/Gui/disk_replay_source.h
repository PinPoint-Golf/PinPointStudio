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

#include "replay_source.h"

#include <QPointer>
#include <QString>
#include <QVector>
#include <vector>

class QMediaPlayer;
class QVideoSink;
class QTimer;

// Disk-backed ReplaySource: replays a shot from its MP4(s) + swing.json. Owns one
// QMediaPlayer per recorded video stream and reports a synchronised window-relative
// playhead. Each MP4 plays at its baked-in 30 fps, which already slows high-fps
// captures — so each stream's playbackRate is computed to make the whole window
// replay at a fixed capture-time `speed` regardless of the camera's capture fps.
// The playhead is derived by mapping the master player's reported position back to
// capture time via that stream's frames.t_us table.
class DiskReplaySource : public ReplaySource
{
    Q_OBJECT
public:
    explicit DiskReplaySource(QObject *parent = nullptr);
    ~DiskReplaySource() override;

    bool load(const QString &swingDir, double speed) override;
    void unload() override;

    int                       streamCount()    const override { return int(m_streams.size()); }
    QVector<ReplayStreamInfo> streams()        const override { return m_streamInfo; }
    QVariantMap               analysisDetail() const override { return m_analysisDetail; }
    qint64 startUs()    const override { return m_startUs; }
    qint64 endUs()      const override { return m_endUs; }
    qint64 impactUs()   const override { return m_impactUs; }
    qint64 positionUs() const override { return m_positionUs; }
    bool   playing()    const override { return m_playing; }
    double speed()      const override { return m_speed; }

    void setVideoSink(int index, QVideoSink *sink) override;
    void togglePlay() override;
    void seekToFraction(double frac) override;
    void seekToUs(qint64 us) override;
    void stepFrame(int delta) override;
    void setSpeed(double speed) override;
    void beginScrub() override;
    void endScrub() override;

private slots:
    void onTick();

private:
    struct Stream {
        QMediaPlayer        *player = nullptr;
        std::vector<int64_t> tUs;            // window-relative frame stamps (µs)
        double               playbackFps = 30.0;
    };

    void setPlaying(bool p);
    void setPositionUs(qint64 us);
    void seekPlayersTo(qint64 captureUs);    // map capture µs → each MP4 and setPosition
    void applyPlaybackRates();               // per-stream rate from m_speed + span
    qint64 captureUsForStream(int streamIdx, qint64 mp4Ms) const;
    qint64 mp4MsForStream(int streamIdx, qint64 captureUs) const;

    bool        m_loaded   = false;
    bool        m_playing  = false;
    double      m_speed    = 0.25;   // capture-time multiplier, 0.1..1
    bool        m_wasPlayingBeforeScrub = false;
    qint64      m_startUs    = 0;
    qint64      m_endUs      = 0;
    qint64      m_impactUs   = -1;
    qint64      m_positionUs = 0;
    QVariantMap m_analysisDetail;

    QVector<ReplayStreamInfo>         m_streamInfo;   // per-stream metadata for the tiles
    std::vector<Stream>               m_streams;
    QVector<QPointer<QVideoSink>>     m_sinks;   // bound from QML, by stream index
    QTimer                           *m_timer = nullptr;
};
