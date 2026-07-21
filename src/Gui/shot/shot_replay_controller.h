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

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class AppSettings;
class QVideoSink;

// ShotReplayController (QML context property `shotReplay`) — the QML-facing facade
// for the unified Review stage. It owns a polymorphic ReplaySource (today a
// DiskReplaySource: MP4(s) + swing.json) and forwards QML calls to it, the same
// abstract-base + factory shape as the IMU/STT/TTS backends. There is ONE replay
// surface — the session stage; the old "panel"/"screen" target split is gone.
//
// The controller owns only the QML-shot identity (shotId / swingDir / active) and
// the `streams` list the Review camera tiles enumerate; everything time/transport
// related is delegated to the source. A reviewed swing may have been recorded on a
// different camera rig, so `streams` is built from the swing's OWN swing.json.
class ShotReplayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool         active         READ active         NOTIFY activeChanged)
    Q_PROPERTY(bool         playing        READ playing        NOTIFY playingChanged)
    Q_PROPERTY(int          shotId         READ shotId         NOTIFY activeChanged)
    // On-disk folder of the focused (replaying/scrubbing) shot, for the data viewer.
    // Empty when no replay is active — the viewer then falls back to the carousel's
    // current selection. NOTIFY activeChanged covers both start() and stop().
    Q_PROPERTY(QString      swingDir       READ swingDir       NOTIFY activeChanged)
    Q_PROPERTY(int          streamCount    READ streamCount    NOTIFY activeChanged)
    // Per-stream metadata for the Review camera tiles, from the swing's own
    // swing.json: list of { index, perspective, aspect, hasAnalysis }.
    Q_PROPERTY(QVariantList streams        READ streams        NOTIFY activeChanged)
    Q_PROPERTY(double       speed          READ speed          NOTIFY speedChanged)
    Q_PROPERTY(qint64       positionUs     READ positionUs     NOTIFY positionChanged)
    Q_PROPERTY(qint64       startUs        READ startUs        NOTIFY spanChanged)
    Q_PROPERTY(qint64       endUs          READ endUs          NOTIFY spanChanged)
    Q_PROPERTY(qint64       impactUs       READ impactUs       NOTIFY spanChanged)
    Q_PROPERTY(QVariantMap  analysisDetail READ analysisDetail NOTIFY activeChanged)

public:
    // appSettings supplies the global replay-trim flag (replayTrimToSwing), read at
    // each start(); may be null in tests/tools (then trimming is off).
    explicit ShotReplayController(AppSettings *appSettings = nullptr, QObject *parent = nullptr);
    ~ShotReplayController() override;

    bool         active()         const { return m_active; }
    bool         playing()        const { return m_source->playing(); }
    int          shotId()         const { return m_shotId; }
    QString      swingDir()       const { return m_active ? m_swingDir : QString(); }
    int          streamCount()    const { return m_source->streamCount(); }
    QVariantList streams()        const;
    qint64       positionUs()     const { return m_source->positionUs(); }
    qint64       startUs()        const { return m_source->startUs(); }
    qint64       endUs()          const { return m_source->endUs(); }
    qint64       impactUs()       const { return m_source->impactUs(); }
    QVariantMap  analysisDetail() const { return m_source->analysisDetail(); }
    double       speed()          const { return m_source->speed(); }

    // Start replaying the shot at `swingDir` for carousel row `shotId`. `speed`
    // is the capture-time multiplier (1.0 = real time), clamped to 0.1..1.
    // Returns false if the doc has no playable video stream.
    Q_INVOKABLE bool start(int shotId, const QString &swingDir, double speed = 0.25);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlay()                 { m_source->togglePlay(); }
    Q_INVOKABLE void seekToFraction(double frac)  { m_source->seekToFraction(frac); }
    Q_INVOKABLE void seekToUs(qint64 us)          { m_source->seekToUs(us); }
    Q_INVOKABLE void stepFrame(int delta)         { m_source->stepFrame(delta); }
    Q_INVOKABLE void setSpeed(double speed)       { m_source->setSpeed(speed); }
    Q_INVOKABLE void beginScrub()                 { m_source->beginScrub(); }
    Q_INVOKABLE void endScrub()                   { m_source->endScrub(); }

    // Bind a QML VideoOutput's sink to stream `index` (face-on = 0). Persists
    // across shots (the VideoOutput outlives a single replay), so it may be called
    // before start(); a live player at that index is rebound immediately.
    Q_INVOKABLE void setVideoSink(int index, QVideoSink *sink) { m_source->setVideoSink(index, sink); }

    // Builds the metric-catalogue ShotContext map for the focused shot from its
    // analysisDetail (tier + pose/club/ball presence + calibration bindings) and
    // its own stream list, tagged with `sessionType` (the caller's screen type).
    // Shape: { tier, sessionType, hasFaceOn, hasClubTrack, hasBallTrack,
    // imuRoles:[roleName…] } — consumed by MetricCatalog.query/descriptor. Sourced
    // from the DISK shot, so analysis.bindings[] (hence imuRoles) are present,
    // unlike the live transient. archetype/club/shape are not persisted in
    // swing.json today, so they are left at their catalogue defaults (0).
    Q_INVOKABLE QVariantMap shotContext(int sessionType) const;

signals:
    void activeChanged();
    void playingChanged();
    void positionChanged();
    void spanChanged();
    void speedChanged();
    // A stream failed to decode — carries a human-readable reason for the UI.
    void replayFailed(const QString &error);
    // Forwarded from the source: the replay played to its natural end. The host
    // uses it to auto-return to Capture after a post-shot auto-replay.
    void playbackEnded();

private:
    void onAborted();

    AppSettings                  *m_appSettings = nullptr;
    bool                          m_active = false;
    int                           m_shotId = -1;
    QString                       m_swingDir;
    std::unique_ptr<ReplaySource> m_source;
};
