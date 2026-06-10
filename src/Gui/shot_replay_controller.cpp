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

#include "shot_replay_controller.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <limits>

#include "../Core/pp_debug.h"

namespace {

// Slave players are nudged back onto the master clock only when they drift past
// this; below it, leaving them free-running keeps decode smooth.
constexpr qint64 kSlaveResyncMs = 120;

// Window-relative µs from an absolute analysis t_us (number), offset by t0.
qint64 relUs(const QJsonValue &v, qint64 t0)
{
    return static_cast<qint64>(v.toDouble()) - t0;
}

} // namespace

ShotReplayController::ShotReplayController(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(33);   // ~30 Hz playhead poll (reads, never seeks)
    connect(m_timer, &QTimer::timeout, this, &ShotReplayController::onTick);
}

ShotReplayController::~ShotReplayController()
{
    teardown();
}

// ---------------------------------------------------------------------------
// Load + start
// ---------------------------------------------------------------------------

bool ShotReplayController::start(int shotId, const QString &swingDir, const QString &mode,
                                const QString &target)
{
    if (swingDir.isEmpty())
        return false;

    QFile f(swingDir + QStringLiteral("/swing.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        ppWarn() << "[ShotReplay] cannot open" << f.fileName();
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty())
        return false;

    // Tear down any previous replay before adopting the new one.
    teardown();

    const qint64 t0 = static_cast<qint64>(
        root[QStringLiteral("clock")].toObject()[QStringLiteral("t0_us")].toDouble());

    // ── Video streams (window-relative frame tables) ───────────────────────
    qint64 spanStart = std::numeric_limits<qint64>::max();
    qint64 spanEnd   = std::numeric_limits<qint64>::min();
    struct PendingStream { QString file; double fps; std::vector<int64_t> tUs; };
    std::vector<PendingStream> pending;

    for (const QJsonValue &sv : root[QStringLiteral("streams")].toArray()) {
        const QJsonObject s = sv.toObject();
        if (s[QStringLiteral("kind")].toString() != QLatin1String("video"))
            continue;
        const QString file = s[QStringLiteral("file")].toString();
        if (file.isEmpty() || !QFile::exists(swingDir + QStringLiteral("/") + file))
            continue;

        PendingStream ps;
        ps.file = file;
        ps.fps  = s[QStringLiteral("playback")].toObject()
                   .value(QStringLiteral("fps")).toDouble(30.0);
        if (ps.fps <= 0.0) ps.fps = 30.0;
        for (const QJsonValue &t : s[QStringLiteral("frames")].toObject()
                                    [QStringLiteral("t_us")].toArray())
            ps.tUs.push_back(static_cast<int64_t>(t.toDouble()));
        if (ps.tUs.empty())
            continue;
        spanStart = std::min<qint64>(spanStart, ps.tUs.front());
        spanEnd   = std::max<qint64>(spanEnd,   ps.tUs.back());
        pending.push_back(std::move(ps));
    }

    if (pending.empty()) {
        ppWarn() << "[ShotReplay] no playable video stream in" << swingDir;
        // teardown() already ran: a previously-active replay has lost its
        // players, so clear the whole transport state — leaving m_active /
        // m_playing / the old shot's id and spans behind shows a zombie
        // black stage that only Close can dismiss.
        if (m_active) {
            m_shotId = -1;
            m_active = false;
            setPlaying(false);
            emit activeChanged();
        }
        return false;
    }

    m_shotId     = shotId;
    m_swingDir   = swingDir;
    m_target     = (target == QLatin1String("screen")) ? QStringLiteral("screen")
                                                       : QStringLiteral("panel");
    m_mode       = (mode == QLatin1String("slow")) ? QStringLiteral("slow")
                                                   : QStringLiteral("normal");
    m_startUs    = spanStart;
    m_endUs      = std::max(spanEnd, spanStart + 1);
    m_positionUs = m_startUs;

    // ── Analysis detail, offset to the window-relative (0-based) domain ─────
    m_analysisDetail = QVariantMap{};
    m_impactUs = -1;
    if (root.contains(QStringLiteral("analysis"))) {
        const QJsonObject an = root[QStringLiteral("analysis")].toObject();
        QVariantList series;
        for (const QJsonValue &mv : an[QStringLiteral("metrics")].toArray()) {
            const QJsonObject m = mv.toObject();
            QVariantList ts, vs, samples;
            for (const QJsonValue &t : m[QStringLiteral("t_us")].toArray())
                ts.append(static_cast<qlonglong>(relUs(t, t0)));
            for (const QJsonValue &v : m[QStringLiteral("value")].toArray())
                vs.append(v.toDouble());
            for (const QJsonValue &sv2 : m[QStringLiteral("phaseSamples")].toArray()) {
                const QJsonObject s2 = sv2.toObject();
                samples.append(QVariantMap{
                    { QStringLiteral("phase"), s2[QStringLiteral("phase")].toInt() },
                    { QStringLiteral("t_us"),  static_cast<qlonglong>(relUs(s2[QStringLiteral("t_us")], t0)) },
                    { QStringLiteral("value"), s2[QStringLiteral("value")].toDouble() },
                    { QStringLiteral("band"),  s2[QStringLiteral("band")].toString() } });
            }
            series.append(QVariantMap{
                { QStringLiteral("key"),   m[QStringLiteral("key")].toString() },
                { QStringLiteral("label"), m[QStringLiteral("label")].toString() },
                { QStringLiteral("unit"),  m[QStringLiteral("unit")].toString() },
                { QStringLiteral("t_us"),  ts },
                { QStringLiteral("value"), vs },
                { QStringLiteral("phaseSamples"), samples } });
        }
        QVariantList phases;
        for (const QJsonValue &pv : an[QStringLiteral("phases")].toArray()) {
            const QJsonObject p = pv.toObject();
            const int phase = p[QStringLiteral("phase")].toInt();
            const qint64 tr = relUs(p[QStringLiteral("t_us")], t0);
            if (phase == 5)   // Phase::Impact
                m_impactUs = tr;
            phases.append(QVariantMap{
                { QStringLiteral("phase"), phase },
                { QStringLiteral("t_us"),  static_cast<qlonglong>(tr) },
                { QStringLiteral("conf"),  p[QStringLiteral("conf")].toDouble() } });
        }
        m_analysisDetail = QVariantMap{
            { QStringLiteral("tier"),    an[QStringLiteral("tier")].toInt() },
            { QStringLiteral("overall"), an[QStringLiteral("score")].toInt() },
            { QStringLiteral("series"),  series },
            { QStringLiteral("phases"),  phases } };
    }
    if (m_impactUs < 0) {
        const QJsonObject thumb = root[QStringLiteral("thumbnail")].toObject();
        m_impactUs = thumb.contains(QStringLiteral("t_us"))
                         ? static_cast<qint64>(thumb[QStringLiteral("t_us")].toDouble())
                         : (m_startUs + m_endUs) / 2;
    }

    // ── Players. Each MP4 is 30 fps; applyPlaybackRates() picks a per-stream
    //    rate so the whole window replays over the same wall time → a fixed
    //    capture-time speed (¼× / ⅛×) independent of the camera's capture fps,
    //    and all streams finish together. ──────────────────────────────────
    for (size_t i = 0; i < pending.size(); ++i) {
        Stream st;
        st.tUs         = std::move(pending[i].tUs);
        st.playbackFps = pending[i].fps;
        st.player = new QMediaPlayer(this);
        if (int(i) < m_sinks.size() && m_sinks[int(i)])
            st.player->setVideoSink(m_sinks[int(i)]);

        // Surface decode failures instead of silently rendering black. The
        // exporter can now emit H.265 in mp4/mov/mkv; Qt's FFmpeg media backend
        // handles all of them, but a build/platform without that codec would
        // otherwise fail invisibly while the playhead kept ticking.
        const QString fileLabel = pending[i].file;
        connect(st.player, &QMediaPlayer::errorOccurred, this,
                [this, fileLabel](QMediaPlayer::Error err, const QString &msg) {
                    if (err == QMediaPlayer::NoError)
                        return;
                    ppWarn() << "[ShotReplay] media error for" << fileLabel << ":" << msg;
                    emit replayFailed(msg.isEmpty() ? fileLabel : msg);
                });

        st.player->setSource(QUrl::fromLocalFile(swingDir + QStringLiteral("/") + pending[i].file));
        if (i == 0)
            connect(st.player, &QMediaPlayer::mediaStatusChanged, this,
                    [this](QMediaPlayer::MediaStatus s) {
                        if (s == QMediaPlayer::EndOfMedia) {
                            setPlaying(false);
                        } else if (s == QMediaPlayer::InvalidMedia) {
                            ppWarn() << "[ShotReplay] master stream is invalid media — cannot replay";
                            emit replayFailed(QStringLiteral("unsupported or corrupt video"));
                            // Defer teardown out of the player's own callback.
                            QMetaObject::invokeMethod(this, [this] { stop(); },
                                                      Qt::QueuedConnection);
                        }
                    });
        m_streams.push_back(std::move(st));
    }
    applyPlaybackRates();
    for (Stream &st : m_streams)
        st.player->play();

    m_active  = true;
    setPositionUs(m_startUs);
    emit spanChanged();
    emit activeChanged();
    setPlaying(true);
    m_timer->start();

    ppInfo() << "[ShotReplay] replaying" << swingDir << "—" << int(m_streams.size())
             << "stream(s), window" << (m_endUs - m_startUs) / 1000 << "ms";
    return true;
}

void ShotReplayController::stop()
{
    if (!m_active)
        return;
    teardown();
    m_shotId   = -1;
    m_active   = false;
    setPlaying(false);
    emit activeChanged();
}

void ShotReplayController::teardown()
{
    m_timer->stop();
    for (Stream &s : m_streams) {
        if (s.player) {
            s.player->stop();
            s.player->setSource(QUrl());
            s.player->deleteLater();
        }
    }
    m_streams.clear();
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void ShotReplayController::togglePlay()
{
    if (!m_active || m_streams.empty())
        return;
    const bool atEnd = m_positionUs >= m_endUs - 1;
    if (atEnd) {
        seekPlayersTo(m_startUs);
        setPositionUs(m_startUs);
        for (Stream &s : m_streams) s.player->play();
        setPlaying(true);
        m_timer->start();
    } else if (m_playing) {
        for (Stream &s : m_streams) s.player->pause();
        setPlaying(false);
    } else {
        for (Stream &s : m_streams) s.player->play();
        setPlaying(true);
        m_timer->start();
    }
}

void ShotReplayController::seekToFraction(double frac)
{
    seekToUs(m_startUs + static_cast<qint64>(std::clamp(frac, 0.0, 1.0) * (m_endUs - m_startUs)));
}

void ShotReplayController::seekToUs(qint64 us)
{
    if (!m_active)
        return;
    us = std::clamp(us, m_startUs, m_endUs);
    seekPlayersTo(us);
    setPositionUs(us);
}

void ShotReplayController::stepFrame(int delta)
{
    if (!m_active || m_streams.empty() || delta == 0)
        return;
    // Pause first — frame-stepping is a deliberate, settled action.
    if (m_playing) {
        for (Stream &s : m_streams) s.player->pause();
        setPlaying(false);
    }
    const Stream &s = m_streams.front();
    if (s.tUs.empty())
        return;
    // Locate the current frame, then offset by delta and seek to its capture µs.
    long idx = std::lround(double(s.player->position()) / 1000.0 * s.playbackFps);
    idx = std::clamp<long>(idx + delta, 0, long(s.tUs.size()) - 1);
    seekToUs(s.tUs[idx]);
}

void ShotReplayController::setMode(const QString &mode)
{
    const QString m = (mode == QLatin1String("slow")) ? QStringLiteral("slow")
                                                      : QStringLiteral("normal");
    if (m == m_mode)
        return;
    m_mode = m;
    applyPlaybackRates();
    emit modeChanged();
}

void ShotReplayController::beginScrub()
{
    if (!m_active)
        return;
    m_wasPlayingBeforeScrub = m_playing;
    if (m_playing) {
        for (Stream &s : m_streams) s.player->pause();
        setPlaying(false);
    }
}

void ShotReplayController::endScrub()
{
    if (!m_active)
        return;
    if (m_wasPlayingBeforeScrub && m_positionUs < m_endUs - 1) {
        for (Stream &s : m_streams) s.player->play();
        setPlaying(true);
    }
    m_wasPlayingBeforeScrub = false;
}

void ShotReplayController::setVideoSink(int index, QVideoSink *sink)
{
    if (index < 0)
        return;
    if (index >= m_sinks.size())
        m_sinks.resize(index + 1);
    m_sinks[index] = sink;
    if (index < int(m_streams.size()) && m_streams[index].player)
        m_streams[index].player->setVideoSink(sink);
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

void ShotReplayController::onTick()
{
    if (!m_active || m_streams.empty())
        return;

    QMediaPlayer *master = m_streams.front().player;
    const qint64 captureUs = captureUsForStream(0, master->position());
    setPositionUs(captureUs);

    // Keep slave cameras on the master's capture clock (R4); cheap no-op for one.
    for (size_t i = 1; i < m_streams.size(); ++i) {
        const qint64 target = mp4MsForStream(int(i), captureUs);
        if (std::llabs(m_streams[i].player->position() - target) > kSlaveResyncMs)
            m_streams[i].player->setPosition(target);
    }

    if (!m_playing)
        m_timer->stop();   // settled (paused / ended) — idle until the next action
}

qint64 ShotReplayController::captureUsForStream(int streamIdx, qint64 mp4Ms) const
{
    const Stream &s = m_streams[streamIdx];
    if (s.tUs.empty())
        return m_startUs;
    long idx = std::lround(double(mp4Ms) / 1000.0 * s.playbackFps);
    idx = std::clamp<long>(idx, 0, long(s.tUs.size()) - 1);
    return s.tUs[idx];
}

qint64 ShotReplayController::mp4MsForStream(int streamIdx, qint64 captureUs) const
{
    const Stream &s = m_streams[streamIdx];
    if (s.tUs.empty())
        return 0;
    // Nearest frame to captureUs.
    auto it = std::lower_bound(s.tUs.begin(), s.tUs.end(), captureUs);
    long idx;
    if (it == s.tUs.begin())      idx = 0;
    else if (it == s.tUs.end())   idx = long(s.tUs.size()) - 1;
    else {
        const long hi = long(it - s.tUs.begin());
        idx = (captureUs - s.tUs[hi - 1] <= s.tUs[hi] - captureUs) ? hi - 1 : hi;
    }
    return std::lround(double(idx) / s.playbackFps * 1000.0);
}

void ShotReplayController::seekPlayersTo(qint64 captureUs)
{
    for (size_t i = 0; i < m_streams.size(); ++i) {
        QMediaPlayer *p = m_streams[i].player;
        // A stopped player (clip ran to its end) won't render a seek — pause it
        // first so scrubbing while not playing still updates the frame.
        if (p->playbackState() == QMediaPlayer::StoppedState)
            p->pause();
        p->setPosition(mp4MsForStream(int(i), captureUs));
    }
}

void ShotReplayController::applyPlaybackRates()
{
    // Each MP4 is 30 fps, so its native duration is frameCount/fps. Scale every
    // stream so the whole window replays over the same wall time → ¼× (normal)
    // or ⅛× (slow) of capture speed, fps-independent, all finishing together.
    const double factor = (m_mode == QLatin1String("slow")) ? 8.0 : 4.0;
    const double desiredWallSec = std::max(0.001, factor * double(m_endUs - m_startUs) / 1e6);
    for (Stream &s : m_streams) {
        const double nativeWallSec = double(s.tUs.size()) / s.playbackFps;
        s.player->setPlaybackRate(std::clamp(nativeWallSec / desiredWallSec, 0.05, 8.0));
    }
}

void ShotReplayController::setPlaying(bool p)
{
    if (m_playing == p)
        return;
    m_playing = p;
    if (p && !m_timer->isActive())
        m_timer->start();
    emit playingChanged();
}

void ShotReplayController::setPositionUs(qint64 us)
{
    us = std::clamp(us, m_startUs, m_endUs);
    if (us == m_positionUs)
        return;
    m_positionUs = us;
    emit positionChanged();
}
