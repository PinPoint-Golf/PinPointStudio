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

#include "disk_replay_source.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <limits>

#include "../Core/pp_debug.h"
#include "../../Export/swing_doc.h"   // takeJustWritten; analysisDetailForDocument — the one projection
#include "../../Analysis/swing_analysis.h"            // Phase — the trim's Address / Finish, the impact
#include "../markup/markup_truth.h"                   // streamLooksDownTheLine — the setup-less DTL rule

namespace {

// Slave players are nudged back onto the master clock only when they drift past
// this; below it, leaving them free-running keeps decode smooth.
constexpr qint64 kSlaveResyncMs = 120;

constexpr int kPerspectiveFaceOn = 2;   // CameraInstance::FaceOn
constexpr int kPerspectiveDtl    = 1;   // CameraInstance::DownTheLine
constexpr int kPerspectiveImpact = 4;   // CameraInstance::Impact — the looping impact clip
// The impact clip runs this much slower than the window's capture-time speed
// (applyPlaybackRates): the interesting part is ~40 ms of capture time.
// The impact clip's replay pace, as a fraction of real time — FIXED, not the
// transport's speed: the club is in the strip for ~60 ms of capture time, and
// at the window's ×1 or ×¼ that is a flicker (the rate then hit the player's
// clamp either way, which is why "3× slower" changed nothing, 2026-09-15).
// 1/50 puts each 591 fps frame on screen for ~85 ms — a readable flipbook.
constexpr double kImpactLoopSpeed = 1.0 / 50.0;
// Frames of context kept either side of the action when the loop is trimmed.
constexpr int    kImpactLoopPadFrames = 4;

} // namespace

std::unique_ptr<ReplaySource> makeDiskReplaySource()
{
    return std::make_unique<DiskReplaySource>();
}

DiskReplaySource::DiskReplaySource(QObject *parent)
    : ReplaySource(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(33);   // ~30 Hz playhead poll (reads, never seeks)
    connect(m_timer, &QTimer::timeout, this, &DiskReplaySource::onTick);
}

DiskReplaySource::~DiskReplaySource()
{
    unload();
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

bool DiskReplaySource::load(const QString &swingDir, double speed, bool trimToSwing)
{
    if (swingDir.isEmpty())
        return false;

    // The document, without fetching and re-parsing it when we are the ones who just wrote it. On the
    // live path this runs microseconds after ShotProcessor committed ~28 MB to the library, and parsing
    // it again here was a third full pass over the same pose track on the GUI thread — the freeze the
    // user sees at the end of a shot. An empty answer means any other caller, and we read the file.
    QJsonObject root = pinpoint::SwingDocWriter::takeJustWritten(swingDir);
    const bool usedCachedDocument = !root.isEmpty();
    if (root.isEmpty()) {
        QString err;
        root = pinpoint::SwingStore::load(swingDir, &err);
        if (root.isEmpty()) {
            ppWarn() << "[ShotReplay]" << err;
            return false;   // bad path: leave any current replay intact
        }
    }
    if (root.isEmpty())
        return false;

    // NOTE: previous replay is NOT torn down here. Destroying/recreating the
    // QMediaPlayers per reload synchronously joined the old decode threads on the
    // UI thread (≈2–4 s) and made the new open contend with that teardown. The
    // players are reused below (just setSource); see the player-pool section.

    // ── Video streams (window-relative frame tables + per-stream metadata) ──
    qint64 spanStart = std::numeric_limits<qint64>::max();
    qint64 spanEnd   = std::numeric_limits<qint64>::min();
    struct PendingStream {
        QString file; double fps; std::vector<int64_t> tUs;
        int perspective; double aspect; double viewGain = 1.0;
    };
    std::vector<PendingStream> pending;

    // Streams with no `setup` block (the 2026-06-11 session) declare no perspective. The
    // down-the-line one is then recognised by markup_truth's whole-token name rule — the
    // SAME rule the Markup panel uses — and the face-on pick is never eligible, so the DTL
    // overlay can never land on the face-on camera. Only DTL is inferred: a setup-less
    // face-on stream stays undeclared, exactly as before.
    QVector<QJsonObject> videoStreams;
    for (const QJsonValue &sv : root[QStringLiteral("streams")].toArray())
        if (sv.toObject()[QStringLiteral("kind")].toString() == QLatin1String("video"))
            videoStreams.push_back(sv.toObject());
    const int faceOnVideoIdx = pinpoint::markup::faceOnStreamIndex(videoStreams);
    int videoIdx = -1;

    for (const QJsonValue &sv : root[QStringLiteral("streams")].toArray()) {
        const QJsonObject s = sv.toObject();
        if (s[QStringLiteral("kind")].toString() != QLatin1String("video"))
            continue;
        ++videoIdx;
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

        // Per-stream metadata from the SWING's own doc (cross-machine safe).
        ps.perspective = s[QStringLiteral("setup")].toObject()
                          .value(QStringLiteral("perspective")).toInt(-1);
        if (ps.perspective < 0 && !s.contains(QStringLiteral("setup"))
            && videoIdx != faceOnVideoIdx && pinpoint::markup::streamLooksDownTheLine(s))
            ps.perspective = kPerspectiveDtl;
        const QJsonObject src = s[QStringLiteral("source")].toObject();
        const double sw = src.value(QStringLiteral("width")).toDouble();
        const double sh = src.value(QStringLiteral("height")).toDouble();
        ps.aspect = (sw > 0.0 && sh > 0.0) ? sw / sh : 16.0 / 9.0;
        // The impact clip's recorded display stretch (capture.viewGain); a
        // clip from before the field, or any other stream, gets none.
        ps.viewGain = s[QStringLiteral("capture")].toObject()
                       .value(QStringLiteral("viewGain")).toDouble(1.0);
        if (!(ps.viewGain >= 1.0)) ps.viewGain = 1.0;

        pending.push_back(std::move(ps));
    }

    if (pending.empty()) {
        ppWarn() << "[ShotReplay] no playable video stream in" << swingDir;
        unload();        // fully clear → streamCount() == 0 (controller clears active)
        return false;
    }

    // The impact camera's clip loops on its own clock (Stream::loop): it must
    // not become the master (stream 0 drives the playhead, the trim logic and
    // frame stepping) and must not widen the span. Order the full-window
    // streams first, and take the span from them alone. A swing whose ONLY
    // stream is the impact clip plays it as an ordinary stream.
    std::stable_partition(pending.begin(), pending.end(),
                          [](const PendingStream &p) { return p.perspective != kPerspectiveImpact; });
    const bool haveFullStream = pending.front().perspective != kPerspectiveImpact;
    for (const PendingStream &p : pending) {
        if (haveFullStream && p.perspective == kPerspectiveImpact)
            continue;
        spanStart = std::min<qint64>(spanStart, p.tUs.front());
        spanEnd   = std::max<qint64>(spanEnd,   p.tUs.back());
    }

    m_speed      = std::clamp(speed, 0.1, 1.0);
    m_startUs    = spanStart;
    m_endUs      = std::max(spanEnd, spanStart + 1);
    m_positionUs = m_startUs;

    // ⏱ The replay's own rebuild of the same pose payload the shot processor just built — the other
    // half of the end-of-shot cost, and on the GUI thread like everything else here. Timed so the next
    // cut is chosen from numbers (Mark, 2026-09-16: the freeze survived moving the write).
    QElapsedTimer replayPhase;
    replayPhase.start();

    // ── Analysis detail: the one projection of the document's analysis block (analysisDetailFor,
    //    swing_doc.h), the map the live shot and the reloaded row carry too — window time, so the
    //    overlays scrub with this window-relative playhead. The trim and the impact come off its
    //    phases (deriveFromAnalysis). At load it comes from the document parsed for the streams;
    //    after the shot is re-analysed, from its Shot (applyAnalysis; refactor 5.7c). ────────────
    m_analysisDetail = pinpoint::analysisDetailForDocument(root);
    {
        const QJsonObject thumb = root[QStringLiteral("thumbnail")].toObject();
        m_fallbackImpactUs = thumb.contains(QStringLiteral("t_us"))
                                 ? static_cast<qint64>(thumb[QStringLiteral("t_us")].toDouble())
                                 : (m_startUs + m_endUs) / 2;
    }
    m_trimToSwing = trimToSwing;
    deriveFromAnalysis();
    m_endedAtTrim = false;
    // Seek to the trim start once media is ready (a pre-load setPosition is dropped
    // by the FFmpeg backend). Skip when playback starts at the span start anyway.
    m_pendingStartSeekUs = (m_playStartUs > m_startUs) ? m_playStartUs : -1;

    // ── Players + per-stream metadata. The player pool is REUSED across reloads:
    //    each MP4 swap is just setSource() on a persistent QMediaPlayer, which the
    //    backend handles asynchronously — load() stays near-instant. (Recreating
    //    players per reload joined the previous decode threads on the UI thread,
    //    ≈2–4 s, and starved the new open.) Signals are wired once, on creation;
    //    the per-stream file/fps/frame-table are refreshed every load. Each MP4 is
    //    30 fps; applyPlaybackRates() picks a per-stream rate so the whole window
    //    replays over the same wall time → a fixed capture-time speed. ───────────
    m_streamInfo.clear();

    // Retire pool players beyond the new stream count (rare — only a shot with
    // fewer cameras). deleteLater keeps the join off this call.
    while (m_streams.size() > pending.size()) {
        Stream &dead = m_streams.back();
        if (dead.player) {
            dead.player->setVideoSink(nullptr);
            dead.player->deleteLater();
        }
        m_streams.pop_back();
    }

    for (size_t i = 0; i < pending.size(); ++i) {
        const bool faceOn = pending[i].perspective == kPerspectiveFaceOn;
        m_streamInfo.append(ReplayStreamInfo{
            int(i), pending[i].perspective, pending[i].aspect,
            faceOn && m_hasAnalysis, pending[i].viewGain });

        if (i >= m_streams.size())
            m_streams.push_back(Stream{});
        Stream &st = m_streams[i];
        st.tUs         = std::move(pending[i].tUs);
        st.playbackFps = pending[i].fps;
        st.file        = pending[i].file;
        st.loop        = haveFullStream && pending[i].perspective == kPerspectiveImpact;
        st.loopStartUs = st.tUs.front();
        st.loopEndUs   = st.tUs.back();
        st.captureFps  = (st.tUs.size() > 1 && st.tUs.back() > st.tUs.front())
                             ? double(st.tUs.size() - 1) * 1e6 / double(st.tUs.back() - st.tUs.front())
                             : 0.0;

        if (!st.player) {
            st.player = new QMediaPlayer(this);
            const int idx = int(i);

            // Surface decode failures instead of silently rendering black. The
            // file is read from the (index-stable) stream slot so the handler
            // stays correct after a source swap.
            connect(st.player, &QMediaPlayer::errorOccurred, this,
                    [this, idx](QMediaPlayer::Error err, const QString &msg) {
                        if (err == QMediaPlayer::NoError)
                            return;
                        const QString f = idx < int(m_streams.size()) ? m_streams[idx].file : QString();
                        ppWarn() << "[ShotReplay] media error for" << f << ":" << msg;
                        emit failed(msg.isEmpty() ? f : msg);
                    });

            if (idx == 0)
                connect(st.player, &QMediaPlayer::mediaStatusChanged, this,
                        [this](QMediaPlayer::MediaStatus s) {
                            if ((s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)
                                && m_pendingStartSeekUs >= 0) {
                                // Apply the trim-start seek now the backend accepts it.
                                seekPlayersTo(m_pendingStartSeekUs);
                                setPositionUs(m_pendingStartSeekUs);
                                m_pendingStartSeekUs = -1;
                            }
                            if (s == QMediaPlayer::EndOfMedia) {
                                setPlaying(false);
                                emit playbackEnded();   // natural end, not a user pause
                            } else if (s == QMediaPlayer::InvalidMedia) {
                                ppWarn() << "[ShotReplay] master stream is invalid media — cannot replay";
                                emit failed(QStringLiteral("unsupported or corrupt video"));
                                // Defer teardown out of the player's own callback.
                                QMetaObject::invokeMethod(this, [this] { unload(); emit aborted(); },
                                                          Qt::QueuedConnection);
                            }
                        });
        }

        if (int(i) < m_sinks.size() && m_sinks[int(i)])
            st.player->setVideoSink(m_sinks[int(i)]);

        // Loops must be set before the source for the backend to honour them.
        st.player->setLoops(st.loop ? QMediaPlayer::Infinite : QMediaPlayer::Once);
        st.player->setSource(QUrl::fromLocalFile(swingDir + QStringLiteral("/") + pending[i].file));
    }
    applyPlaybackRates();
    if (m_loopHeld) {           // a hold never outlives the clip it was on
        m_loopHeld = false;
        emit impactLoopPlayingChanged();
    }
    playPlayers();

    deriveLoopBands();

    m_loaded = true;
    setPositionUs(m_playStartUs);
    emit spanChanged();
    emit speedChanged();   // load() may have changed m_speed
    setPlaying(true);
    m_timer->start();

    ppInfo() << "[ShotReplay] replaying" << swingDir << "—" << int(m_streams.size())
             << "stream(s), window" << (m_endUs - m_startUs) / 1000 << "ms"
             << "| load on the GUI thread:" << replayPhase.elapsed() << "ms"
             << (usedCachedDocument ? "(document already in hand)" : "(parsed from disk)");
    return true;
}

void DiskReplaySource::unload()
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
    m_streamInfo.clear();
    m_analysisDetail = QVariantMap{};
    m_hasAnalysis = false;
    m_fallbackImpactUs = -1;
    m_impactUs   = -1;
    m_startUs    = 0;
    m_endUs      = 0;
    m_positionUs = 0;
    m_playStartUs = 0;
    m_playEndUs   = 0;
    m_pendingStartSeekUs = -1;
    m_endedAtTrim = false;
    m_loaded     = false;
    if (m_loopHeld) {
        m_loopHeld = false;
        emit impactLoopPlayingChanged();
    }
    setPlaying(false);
    emit spanChanged();
}

// What the slice decides (deriveFromAnalysis, deriveLoopBands): the same rules at load and on a refresh.
void DiskReplaySource::deriveFromAnalysis()
{
    m_impactUs = -1;
    qint64 addressUs = -1, finishUs = -1;   // Phase::Address / Phase::Finish, for the trim
    m_hasAnalysis = !m_analysisDetail.value(QStringLiteral("series")).toList().isEmpty();
    for (const QVariant &pv : m_analysisDetail.value(QStringLiteral("phases")).toList()) {
        using pinpoint::analysis::Phase;
        const QVariantMap p = pv.toMap();
        const int phase = p.value(QStringLiteral("phase")).toInt();
        const qint64 t  = p.value(QStringLiteral("t_us")).toLongLong();
        if (phase == int(Phase::Address))     addressUs  = t;
        else if (phase == int(Phase::Impact)) m_impactUs = t;
        else if (phase == int(Phase::Finish)) finishUs   = t;
    }
    if (m_impactUs < 0)
        m_impactUs = m_fallbackImpactUs;

    // ── Playback trim (Address → Finish) ────────────────────────────────────
    // Default to the full span; only narrow it when trimming is requested AND the
    // swing carries a usable Address/Finish pair. The scrub range [m_startUs,
    // m_endUs] is untouched — this only moves where auto-play starts and stops.
    m_playStartUs = m_startUs;
    m_playEndUs   = m_endUs;
    if (m_trimToSwing && addressUs >= 0 && finishUs > addressUs) {
        const qint64 lo = std::clamp(addressUs, m_startUs, m_endUs);
        const qint64 hi = std::clamp(finishUs,  m_startUs, m_endUs);
        if (hi > lo) {
            m_playStartUs = lo;
            m_playEndUs   = hi;
        }
    }
}

void DiskReplaySource::deriveLoopBands()
{
    // Trim the loop to the action when the impact track says where it is:
    // the first frame with a club to the last with a club or a departed ball,
    // padded a few frames either side. Else the clip's first to last frame.
    const QVariantMap im = m_analysisDetail.value(QStringLiteral("impact")).toMap();
    const QVariantList samples = im.value(QStringLiteral("samples")).toList();
    const qint64 leave = im.value(QStringLiteral("ballLeaveTUs"), -1).toLongLong();
    qint64 lo = -1, hi = -1;
    for (const QVariant &v : samples) {
        const QVariantMap sm = v.toMap();
        const qint64 t = sm.value(QStringLiteral("t_us")).toLongLong();
        const bool action = sm.contains(QStringLiteral("hx"))
                         || (leave >= 0 && t >= leave && sm.contains(QStringLiteral("bx")));
        if (!action) continue;
        if (lo < 0 || t < lo) lo = t;
        if (hi < 0 || t > hi) hi = t;
    }
    for (Stream &st : m_streams) {
        if (!st.loop || st.tUs.empty()) continue;
        st.loopStartUs = st.tUs.front();
        st.loopEndUs   = st.tUs.back();
        if (lo < 0 || hi <= lo || st.tUs.size() < 2) continue;
        const qint64 period = std::max<qint64>(1, (st.tUs.back() - st.tUs.front()) / qint64(st.tUs.size() - 1));
        st.loopStartUs = std::max<qint64>(st.tUs.front(), lo - kImpactLoopPadFrames * period);
        st.loopEndUs   = std::min<qint64>(st.tUs.back(),  hi + kImpactLoopPadFrames * period);
    }
}

bool DiskReplaySource::applyAnalysis(const QJsonObject &analysis, qint64 originUs)
{
    if (!m_loaded)
        return false;
    m_analysisDetail = pinpoint::analysisDetailFor(analysis, originUs);   // the one projection, as at load
    deriveFromAnalysis();
    for (ReplayStreamInfo &si : m_streamInfo)
        si.hasAnalysis = si.perspective == kPerspectiveFaceOn && m_hasAnalysis;
    deriveLoopBands();
    emit spanChanged();   // impactUs and the impact loop's band
    return true;
}

void DiskReplaySource::refreshImpactPosition()
{
    qint64 pos = -1;
    for (size_t i = 0; i < m_streams.size(); ++i)
        if (m_streams[i].loop && m_streams[i].player) {
            pos = captureUsForStream(int(i), m_streams[i].player->position());
            break;
        }
    m_impactPosUs = pos;   // positionChanged (emitted by the caller) carries it
}

int DiskReplaySource::loopStreamIndex() const
{
    for (size_t i = 0; i < m_streams.size(); ++i)
        if (m_streams[i].loop && m_streams[i].player) return int(i);
    return -1;
}

qint64 DiskReplaySource::impactLoopStartUs() const
{
    const int i = loopStreamIndex();
    return i < 0 ? -1 : m_streams[i].loopStartUs;
}

qint64 DiskReplaySource::impactLoopEndUs() const
{
    const int i = loopStreamIndex();
    return i < 0 ? -1 : m_streams[i].loopEndUs;
}

void DiskReplaySource::seekImpactToUs(qint64 us)
{
    const int i = loopStreamIndex();
    if (!m_loaded || i < 0)
        return;
    Stream &s = m_streams[i];
    us = std::clamp<qint64>(us, s.tUs.front(), s.tUs.back());
    if (!m_loopHeld) {
        m_loopHeld = true;          // scrubbing by hand is a hold
        emit impactLoopPlayingChanged();
    }
    if (s.player->playbackState() != QMediaPlayer::PausedState)
        s.player->pause();
    s.player->setPosition(mp4MsForStream(i, us));
    // Report the frame asked for, not the player's lagging read: the slider
    // and the overlay follow this the instant the drag moves.
    m_impactPosUs = captureUsForStream(i, mp4MsForStream(i, us));
    emit positionChanged();
}

void DiskReplaySource::stepImpactFrame(int delta)
{
    const int i = loopStreamIndex();
    if (!m_loaded || i < 0 || delta == 0)
        return;
    const Stream &s = m_streams[i];
    auto it = std::lower_bound(s.tUs.begin(), s.tUs.end(), m_impactPosUs);
    long idx = (it == s.tUs.end()) ? long(s.tUs.size()) - 1 : long(it - s.tUs.begin());
    idx = std::clamp<long>(idx + delta, 0, long(s.tUs.size()) - 1);
    seekImpactToUs(s.tUs[size_t(idx)]);
}

void DiskReplaySource::playPlayers()
{
    for (Stream &s : m_streams)
        if (!(s.loop && m_loopHeld))
            s.player->play();
}

void DiskReplaySource::toggleImpactLoop()
{
    if (!m_loaded)
        return;
    bool any = false;
    for (Stream &s : m_streams) any = any || s.loop;
    if (!any)
        return;
    m_loopHeld = !m_loopHeld;
    // Hold ON the frame worth looking at: the last one with the ball still at
    // rest and the head at it (analysis.impact.ballLeaveTUs, one frame back),
    // else the arbiter's instant. A loop caught wherever the click landed
    // showed an empty strip more often than not (2026-09-15).
    qint64 holdUs = m_impactUs;
    const QVariantMap im = m_analysisDetail.value(QStringLiteral("impact")).toMap();
    if (im.contains(QStringLiteral("ballLeaveTUs")))
        holdUs = im.value(QStringLiteral("ballLeaveTUs")).toLongLong();
    for (size_t i = 0; i < m_streams.size(); ++i) {
        Stream &s = m_streams[i];
        if (!s.loop) continue;
        if (m_loopHeld) {
            if (holdUs >= 0 && s.tUs.size() > 1) {
                const qint64 period = std::max<qint64>(1, (s.tUs.back() - s.tUs.front()) / qint64(s.tUs.size() - 1));
                s.player->pause();
                s.player->setPosition(mp4MsForStream(int(i), holdUs - period));
            } else {
                s.player->pause();
            }
        } else if (m_playing) {
            // Release re-phased to the playhead, then let it run.
            s.player->setPosition(mp4MsForStream(int(i), loopClipUsFor(int(i), m_positionUs)));
            s.player->play();
        }
    }
    refreshImpactPosition();
    emit positionChanged();
    emit impactLoopPlayingChanged();
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void DiskReplaySource::togglePlay()
{
    if (!m_loaded || m_streams.empty())
        return;
    const bool atEnd = m_positionUs >= m_playEndUs - 1;
    if (atEnd) {
        // Restart the trimmed window from its Address start.
        m_endedAtTrim = false;
        seekPlayersTo(m_playStartUs);
        setPositionUs(m_playStartUs);
        playPlayers();
        setPlaying(true);
        m_timer->start();
    } else if (m_playing) {
        for (Stream &s : m_streams) s.player->pause();
        setPlaying(false);
    } else {
        playPlayers();
        setPlaying(true);
        m_timer->start();
    }
}

void DiskReplaySource::seekToFraction(double frac)
{
    seekToUs(m_startUs + static_cast<qint64>(std::clamp(frac, 0.0, 1.0) * (m_endUs - m_startUs)));
}

void DiskReplaySource::seekToUs(qint64 us)
{
    if (!m_loaded)
        return;
    us = std::clamp(us, m_startUs, m_endUs);
    seekPlayersTo(us);
    refreshImpactPosition();
    setPositionUs(us);
}

void DiskReplaySource::stepFrame(int delta)
{
    if (!m_loaded || m_streams.empty() || delta == 0)
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
    long idx = long(std::floor(double(s.player->position()) / 1000.0 * s.playbackFps));
    idx = std::clamp<long>(idx + delta, 0, long(s.tUs.size()) - 1);
    seekToUs(s.tUs[idx]);
}

void DiskReplaySource::setSpeed(double speed)
{
    speed = std::clamp(speed, 0.1, 1.0);
    if (qFuzzyCompare(speed, m_speed))
        return;
    m_speed = speed;
    applyPlaybackRates();   // live players pick the new rate up immediately
    emit speedChanged();
}

void DiskReplaySource::beginScrub()
{
    if (!m_loaded)
        return;
    m_wasPlayingBeforeScrub = m_playing;
    if (m_playing) {
        for (Stream &s : m_streams) s.player->pause();
        setPlaying(false);
    }
}

void DiskReplaySource::endScrub()
{
    if (!m_loaded)
        return;
    if (m_wasPlayingBeforeScrub && m_positionUs < m_playEndUs - 1) {
        m_endedAtTrim = false;   // scrubbing back inside the window re-arms the trim stop
        playPlayers();
        setPlaying(true);
    }
    m_wasPlayingBeforeScrub = false;
}

void DiskReplaySource::setVideoSink(int index, QVideoSink *sink)
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

void DiskReplaySource::onTick()
{
    if (!m_loaded || m_streams.empty())
        return;

    QMediaPlayer *master = m_streams.front().player;
    const qint64 captureUs = captureUsForStream(0, master->position());
    refreshImpactPosition();
    setPositionUs(captureUs);

    // Trim-end auto-stop: only when the play window is genuinely narrower than the
    // full span (the untrimmed case still ends naturally on the master's
    // EndOfMedia, preserving prior behaviour). Fires playbackEnded once.
    if (m_playing && !m_endedAtTrim && m_playEndUs < m_endUs && captureUs >= m_playEndUs) {
        m_endedAtTrim = true;
        for (Stream &s : m_streams) s.player->pause();
        setPlaying(false);
        emit playbackEnded();   // trimmed end — same signal the host acts on
        m_timer->stop();
        return;
    }

    // Keep slave cameras on the master's capture clock (R4); cheap no-op for one.
    // A looping impact clip is kept in PHASE instead: its wanted clip time is
    // the playhead's distance from the clip's first frame wrapped at the clip's
    // length, so the impact frame lands as the playhead crosses impact and the
    // loop runs around it the rest of the time. Same drift threshold.
    for (size_t i = 1; i < m_streams.size(); ++i) {
        if (m_streams[i].loop && m_loopHeld)
            continue;   // held still on purpose — re-phased on release
        const qint64 target = mp4MsForStream(int(i), m_streams[i].loop
                                                         ? loopClipUsFor(int(i), captureUs)
                                                         : captureUs);
        if (std::llabs(m_streams[i].player->position() - target) > kSlaveResyncMs)
            m_streams[i].player->setPosition(target);
    }

    if (!m_playing)
        m_timer->stop();   // settled (paused / ended) — idle until the next action
}

qint64 DiskReplaySource::captureUsForStream(int streamIdx, qint64 mp4Ms) const
{
    const Stream &s = m_streams[streamIdx];
    if (s.tUs.empty())
        return m_startUs;
    // The frame ON SCREEN at a position is the one whose interval contains
    // it (floor), and seeks land mid-frame (mp4MsForStream) — rounding both
    // ways put the impact overlay one frame ahead of the picture (2026-09-15).
    long idx = long(std::floor(double(mp4Ms) / 1000.0 * s.playbackFps));
    idx = std::clamp<long>(idx, 0, long(s.tUs.size()) - 1);
    return s.tUs[idx];
}

qint64 DiskReplaySource::mp4MsForStream(int streamIdx, qint64 captureUs) const
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
    return std::lround((double(idx) + 0.5) / s.playbackFps * 1000.0);   // mid-frame: lands ON frame idx
}

void DiskReplaySource::seekPlayersTo(qint64 captureUs)
{
    for (size_t i = 0; i < m_streams.size(); ++i) {
        if (m_streams[i].loop && m_loopHeld)
            continue;   // a held loop keeps its frame through a scrub
        QMediaPlayer *p = m_streams[i].player;
        // A stopped player (clip ran to its end) won't render a seek — pause it
        // first so scrubbing while not playing still updates the frame.
        if (p->playbackState() == QMediaPlayer::StoppedState)
            p->pause();
        p->setPosition(mp4MsForStream(int(i), m_streams[i].loop
                                                  ? loopClipUsFor(int(i), captureUs)
                                                  : captureUs));
    }
}

qint64 DiskReplaySource::loopClipUsFor(int streamIdx, qint64 captureUs) const
{
    // Map a window playhead onto a looping clip: distance from the clip's first
    // frame, wrapped at the clip's length (one frame period past its last
    // frame, so the last frame gets its share of time). captureUs == impact ⇒
    // the clip's own impact frame, by construction.
    const Stream &s = m_streams[streamIdx];
    if (s.tUs.size() < 2)
        return s.tUs.empty() ? m_startUs : s.tUs.front();
    const qint64 period = std::max<qint64>(1, (s.tUs.back() - s.tUs.front()) / qint64(s.tUs.size() - 1));
    const qint64 first  = s.loopStartUs;
    const qint64 last   = std::max(s.loopEndUs, first);
    const qint64 dur    = std::max<qint64>(1, last - first + period);
    // The loop runs at its OWN pace (kImpactLoopSpeed), so its phase is the
    // window playhead scaled to that pace, not the playhead itself.
    const qint64 scaled = qint64(double(captureUs - first) * (kImpactLoopSpeed / std::max(0.01, m_speed)));
    const qint64 local  = ((scaled % dur) + dur) % dur;
    return first + local;
}

void DiskReplaySource::applyPlaybackRates()
{
    // Each MP4 is 30 fps, so its native duration is frameCount/fps. Scale every
    // stream so the whole window replays over the same wall time → m_speed ×
    // capture speed (1.0 = real time), fps-independent, all finishing together.
    // A looping impact clip is NOT stretched to the window (it would smear a
    // 300 ms clip across the whole replay): it runs at the same capture-time
    // speed as everyone else, m_speed × its own capture fps over the container
    // fps. The looping impact clip is the exception to the exception: it runs
    // at kImpactLoopSpeed of real time whatever the transport says, over the
    // trimmed band its impact track marks (see load()).
    const double desiredWallSec = std::max(0.001, double(m_endUs - m_startUs) / 1e6 / m_speed);
    for (Stream &s : m_streams) {
        double rate;
        if (s.loop && s.captureFps > 0.0)
            rate = kImpactLoopSpeed * s.captureFps / s.playbackFps;
        else
            rate = double(s.tUs.size()) / s.playbackFps / desiredWallSec;
        s.player->setPlaybackRate(std::clamp(rate, 0.05, 8.0));
    }
}

void DiskReplaySource::setPlaying(bool p)
{
    if (m_playing == p)
        return;
    m_playing = p;
    if (p && !m_timer->isActive())
        m_timer->start();
    emit playingChanged();
}

void DiskReplaySource::setPositionUs(qint64 us)
{
    us = std::clamp(us, m_startUs, m_endUs);
    if (us == m_positionUs)
        return;
    m_positionUs = us;
    emit positionChanged();
}
