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

    bool load(const QString &swingDir, double speed, bool trimToSwing) override;
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
    bool impactLoopPlaying() const override { return !m_loopHeld; }
    void toggleImpactLoop() override;
    qint64 impactPositionUs() const override { return m_impactPosUs; }
    qint64 impactLoopStartUs() const override;
    qint64 impactLoopEndUs() const override;
    void seekImpactToUs(qint64 us) override;
    void stepImpactFrame(int delta) override;

private slots:
    void onTick();

private:
    struct Stream {
        QMediaPlayer        *player = nullptr;
        std::vector<int64_t> tUs;            // window-relative frame stamps (µs)
        double               playbackFps = 30.0;
        QString              file;           // current source filename (for error logs across reuse)
        // The impact camera's clip (setup.perspective == 4): a few hundred ms
        // around impact at ~600 fps. It is never the master, never widens the
        // span, and plays as an infinite LOOP at the same capture-time speed as
        // the rest, re-phased each tick so its impact frame is on screen when
        // the playhead crosses impact (impact_camera_design.md §10.2).
        bool                 loop       = false;
        double               captureFps = 0.0; // from the stream's own t_us
        // The loop's own band, window-relative µs (defaults to the clip's first
        // and last frame): trimmed to the frames the club and the ball are in
        // view on when analysis.impact says where those are, so a 300 ms keep
        // band does not loop 230 ms of empty mat.
        qint64               loopStartUs = 0;
        qint64               loopEndUs   = 0;
    };

    void setPlaying(bool p);
    void setPositionUs(qint64 us);
    void seekPlayersTo(qint64 captureUs);    // map capture µs → each MP4 and setPosition
    void applyPlaybackRates();               // per-stream rate from m_speed + span
    qint64 captureUsForStream(int streamIdx, qint64 mp4Ms) const;
    qint64 mp4MsForStream(int streamIdx, qint64 captureUs) const;
    qint64 loopClipUsFor(int streamIdx, qint64 captureUs) const;   // playhead → looping clip time

    bool        m_loaded   = false;
    bool        m_playing  = false;
    // The impact clip's own hold (toggleImpactLoop): its player is paused and
    // the tick stops re-phasing it until released. Cleared on load.
    bool        m_loopHeld = false;
    void playPlayers();   // play every stream, honouring the loop hold
    // The looping clip's own playhead (see impactPositionUs), refreshed with
    // the tick from its player's position; -1 = no looping stream.
    qint64      m_impactPosUs = -1;
    void refreshImpactPosition();
    int  loopStreamIndex() const;   // -1 when no looping stream
    double      m_speed    = 0.25;   // capture-time multiplier, 0.1..1
    bool        m_wasPlayingBeforeScrub = false;
    qint64      m_startUs    = 0;
    qint64      m_endUs      = 0;
    qint64      m_impactUs   = -1;
    qint64      m_positionUs = 0;
    // Playback trim bounds (Address → Finish when trimToSwing is on, else the full
    // span). These restrict where auto-play starts and stops; the scrub range stays
    // [m_startUs, m_endUs] so the timeline/phases remain fully reachable by hand.
    qint64      m_playStartUs = 0;
    qint64      m_playEndUs   = 0;
    // Pending initial seek applied once the master reaches Loaded/Buffered media
    // (setPosition before load is dropped by the FFmpeg backend). -1 = none.
    qint64      m_pendingStartSeekUs = -1;
    // Guards the trim-end auto-stop so playbackEnded fires once per playthrough.
    bool        m_endedAtTrim = false;
    QVariantMap m_analysisDetail;

    QVector<ReplayStreamInfo>         m_streamInfo;   // per-stream metadata for the tiles
    std::vector<Stream>               m_streams;
    QVector<QPointer<QVideoSink>>     m_sinks;   // bound from QML, by stream index
    QTimer                           *m_timer = nullptr;
};
