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

#include "markup_controller.h"
#include "markup_image_provider.h"

#include <QDir>
#include <QImage>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cmath>

using namespace pinpoint::markup;

MarkupController::MarkupController(QObject *parent) : QObject(parent)
{
    // One reused player/sink for the whole session — recreating per swing would
    // join decode threads on the GUI thread (see DiskReplaySource). We never
    // play; we only seek to stills and pull each seeked frame off the sink.
    m_player = new QMediaPlayer(this);
    m_sink   = new QVideoSink(this);
    m_player->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged, this, &MarkupController::onFrame);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus st) {
                if (st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia) {
                    if (!m_sourceReady) {
                        m_sourceReady = true;
                        decodeFrame(m_requestedIdx >= 0 ? m_requestedIdx : 0);
                    }
                } else if (st == QMediaPlayer::InvalidMedia) {
                    emit message(QStringLiteral("Could not open %1").arg(m_fo.videoFile));
                }
            });
}

MarkupController::~MarkupController() = default;

// ── queue ────────────────────────────────────────────────────────────────────

void MarkupController::loadSwings(const QVariantList &swingDirs)
{
    m_swingDirs.clear();
    for (const QVariant &v : swingDirs) {
        const QString d = v.toString();
        if (!d.isEmpty()) m_swingDirs << d;
    }
    rebuildSwingsCache();
    m_currentIndex = -1;
    emit swingsChanged();
    emit currentChanged();
    if (!m_swingDirs.isEmpty()) openSwing(0);
}

void MarkupController::loadSwing(const QString &swingDir)
{
    if (swingDir.isEmpty()) {                      // clear
        if (m_swingDirs.isEmpty() && m_currentIndex < 0) return;
        m_swingDirs.clear();
        m_swingsCache.clear();
        m_currentIndex = -1;
        m_fo = {};
        m_truth = {};
        m_pose = {};
        m_player->setSource(QUrl());
        m_sourceReady = false;
        m_requestedIdx = -1;
        m_dirty = false;
        emit swingsChanged();
        emit currentChanged();
        emit labelsChanged();
        emit dirtyChanged();
        emit frameChanged();
        emit metaChanged();
        return;
    }
    if (hasSwing() && currentSwingDir() == swingDir) return;   // already focused — keep edits
    m_swingDirs = QStringList{ swingDir };
    rebuildSwingsCache();
    emit swingsChanged();
    openSwing(0);
}

void MarkupController::rebuildSwingsCache()
{
    m_swingsCache.clear();
    for (const QString &d : m_swingDirs) {
        const TruthSummary s = summarize(d);
        QVariantMap m;
        m.insert(QStringLiteral("swingDir"),   d);
        m.insert(QStringLiteral("name"),       QDir(d).dirName());
        m.insert(QStringLiteral("shaftCount"), s.shaftCount);
        m.insert(QStringLiteral("eventCount"), s.eventCount);
        m.insert(QStringLiteral("labelled"),   s.exists && (s.shaftCount > 0 || s.eventCount > 0));
        m_swingsCache.push_back(m);
    }
}

int MarkupController::labelledCount() const
{
    int n = 0;
    for (const QVariant &v : m_swingsCache)
        if (v.toMap().value(QStringLiteral("labelled")).toBool()) ++n;
    return n;
}

QString MarkupController::currentSwingDir() const
{
    return hasSwing() ? m_swingDirs.at(m_currentIndex) : QString();
}

QString MarkupController::currentSwingName() const
{
    return hasSwing() ? QDir(m_swingDirs.at(m_currentIndex)).dirName() : QString();
}

void MarkupController::openSwing(int queueIndex)
{
    if (queueIndex < 0 || queueIndex >= m_swingDirs.size()) return;
    m_currentIndex = queueIndex;
    const QString dir = m_swingDirs.at(queueIndex);

    m_fo = readFaceOn(dir);
    m_truth = m_fo.ok ? readTruth(dir, m_fo) : TruthDoc{};
    m_pose  = m_fo.ok ? readPose2d(dir)     : PoseTrack{};
    m_dirty = false;
    m_frameIndex = 0;

    // Seed the common-case defaults so the panel reads coherently and validation
    // always has a scope to gate on: club from the system default (clubs aren't
    // persisted in swing.json yet — see SwingDocReader), and scope/tempo/contact
    // to the dominant corpus case (full swing, normal tempo, ball struck) — the
    // labeller overrides the exceptions. lighting/shaft have no dominant default
    // so they stay unset until picked. Seeding initial state does not mark the
    // swing dirty — it is written on the next save like any other label.
    seedMetaDefaults();

    // Load the MP4 into the reused player. setSource is async; the first decode
    // happens once mediaStatus reaches LoadedMedia (handled in the ctor lambda).
    m_sourceReady = false;
    m_requestedIdx = 0;
    if (m_fo.ok) {
        m_player->setSource(QUrl::fromLocalFile(QDir(dir).filePath(m_fo.videoFile)));
        m_player->pause();
    } else {
        m_player->setSource(QUrl());
    }

    emit currentChanged();
    emit labelsChanged();
    emit dirtyChanged();
    emit metaChanged();

    if (!m_fo.ok)
        emit message(QStringLiteral("No face-on stream in %1").arg(currentSwingName()));
}

void MarkupController::nextSwing() { if (m_currentIndex + 1 < m_swingDirs.size()) openSwing(m_currentIndex + 1); }
void MarkupController::prevSwing() { if (m_currentIndex > 0)                       openSwing(m_currentIndex - 1); }

// ── frame view ───────────────────────────────────────────────────────────────

void MarkupController::decodeFrame(int idx)
{
    if (!m_fo.ok || m_fo.frameCount() <= 0) return;
    idx = std::clamp(idx, 0, m_fo.frameCount() - 1);
    m_requestedIdx = idx;
    m_nudged = false;
    if (!m_sourceReady) return;   // the LoadedMedia handler will seek once ready

    // Seek by milliseconds (QMediaPlayer's domain). The MP4 has fixed-rate
    // sequential PTS, so target the mid-point of the frame's interval — rounding
    // then never lands on a neighbouring frame boundary. onFrame() bumps the
    // token when the decoded still arrives.
    const qint64 ms = qint64(std::llround((idx + 0.5) * 1000.0 / m_fo.playbackFps));
    m_player->setPosition(ms);
}

void MarkupController::onFrame(const QVideoFrame &frame)
{
    if (!m_fo.ok || m_requestedIdx < 0 || !frame.isValid()) return;

    // Exactness guard: startTime() is the frame PTS (µs). If the decoder landed
    // on the wrong frame, nudge once toward the requested index before accepting.
    const qint64 ptsUs = frame.startTime();
    if (ptsUs >= 0 && !m_nudged) {
        const int landed = int(std::llround(double(ptsUs) * m_fo.playbackFps / 1e6));
        if (landed != m_requestedIdx) {
            m_nudged = true;
            // Landed early (L<R) → aim later in R's interval (0.75); landed late
            // → aim earlier (0.25). Stays well clear of the frame boundaries.
            const double frac = (landed < m_requestedIdx) ? 0.75 : 0.25;
            const qint64 ms = qint64(std::llround(
                (m_requestedIdx + frac) * 1000.0 / m_fo.playbackFps));
            m_player->setPosition(std::max<qint64>(0, ms));
            return;   // wait for the corrected frame (accepted regardless, m_nudged set)
        }
    }

    const QImage img = frame.toImage();
    if (img.isNull()) return;
    if (m_provider) m_provider->setImage(img);

    m_frameIndex = m_requestedIdx;
    ++m_frameToken;
    emit frameChanged();
    emit poseChanged();   // currentPose tracks the active frame
}

double MarkupController::frameSec() const
{
    if (m_fo.frameTimesUs.isEmpty() || m_frameIndex < 0 || m_frameIndex >= m_fo.frameCount()) return 0.0;
    return double(m_fo.frameTimesUs[m_frameIndex] - m_fo.frameTimesUs.first()) / 1e6;
}

void MarkupController::setStride(int s)
{
    s = std::clamp(s, 1, 120);
    if (s == m_stride) return;
    m_stride = s;
    emit strideChanged();
}

void MarkupController::stepFrame(int delta)   { setFrameIndex(m_frameIndex + delta); }

void MarkupController::setFrameIndex(int idx)
{
    if (m_fo.frameCount() <= 0) return;
    idx = std::clamp(idx, 0, m_fo.frameCount() - 1);
    if (idx == m_frameIndex && m_frameToken > 0) return;   // no-op (already shown)
    decodeFrame(idx);
}

void MarkupController::seekFraction(double frac)
{
    if (m_fo.frameCount() <= 0) return;
    frac = std::clamp(frac, 0.0, 1.0);
    setFrameIndex(int(std::lround(frac * (m_fo.frameCount() - 1))));
}

void MarkupController::nextLabelled()
{
    for (auto it = m_truth.shaft.constBegin(); it != m_truth.shaft.constEnd(); ++it)
        if (it.key() > m_frameIndex) { setFrameIndex(it.key()); return; }
}

void MarkupController::prevLabelled()
{
    int target = -1;
    for (auto it = m_truth.shaft.constBegin(); it != m_truth.shaft.constEnd(); ++it) {
        if (it.key() < m_frameIndex) target = it.key();
        else break;
    }
    if (target >= 0) setFrameIndex(target);
}

// ── labels ───────────────────────────────────────────────────────────────────

QVariantMap MarkupController::currentShaft() const
{
    QVariantMap m;
    const auto it = m_truth.shaft.constFind(m_frameIndex);
    if (it == m_truth.shaft.constEnd()) { m.insert(QStringLiteral("has"), false); return m; }
    m.insert(QStringLiteral("has"),    true);
    m.insert(QStringLiteral("gripNx"), it->gripNx);
    m.insert(QStringLiteral("gripNy"), it->gripNy);
    m.insert(QStringLiteral("headNx"), it->headNx);
    m.insert(QStringLiteral("headNy"), it->headNy);
    return m;
}

QVariantMap MarkupController::currentPose() const
{
    QVariantMap m;
    if (!m_showSkeleton || !m_pose.ok || m_frameIndex < 0 || m_frameIndex >= m_fo.frameCount()) {
        m.insert(QStringLiteral("has"), false);
        return m;
    }
    const qint64 target = m_fo.frameTimesUs[m_frameIndex];   // window-relative
    const int pi = poseIndexForUs(m_pose, target);
    // >40 ms gap → pose doesn't cover this video frame (swing-span bounded); hide.
    if (pi < 0 || std::llabs(m_pose.frames[pi].relUs - target) > 40000) {
        m.insert(QStringLiteral("has"), false);
        return m;
    }
    const auto &pf = m_pose.frames[pi];
    m.insert(QStringLiteral("has"), true);
    QVariantList kp;
    kp.reserve(pf.kp.size());
    for (float v : pf.kp) kp.push_back(v);
    m.insert(QStringLiteral("kp"), kp);
    if (pf.hasLead)  m.insert(QStringLiteral("lead"),  QVariantList{ pf.leadX,  pf.leadY  });
    if (pf.hasTrail) m.insert(QStringLiteral("trail"), QVariantList{ pf.trailX, pf.trailY });
    m.insert(QStringLiteral("handConf"), pf.handConf);
    return m;
}

void MarkupController::setShowSkeleton(bool on)
{
    if (on == m_showSkeleton) return;
    m_showSkeleton = on;
    emit skeletonChanged();
    emit poseChanged();
}

QVariantMap MarkupController::events() const
{
    QVariantMap m;
    const qint64 t0 = m_fo.frameTimesUs.isEmpty() ? 0 : m_fo.frameTimesUs.first();
    for (auto it = m_truth.events.constBegin(); it != m_truth.events.constEnd(); ++it) {
        const int idx = it.value();
        QVariantMap e;
        e.insert(QStringLiteral("frame"), idx);
        e.insert(QStringLiteral("sec"),
                 (idx >= 0 && idx < m_fo.frameCount()) ? double(m_fo.frameTimesUs[idx] - t0) / 1e6 : 0.0);
        e.insert(QStringLiteral("hasClub"), m_truth.shaft.contains(idx));   // P complete = time + club
        m.insert(it.key(), e);
    }
    return m;
}

// Render-ready list of the marked positions, each carrying its WINDOW-RELATIVE
// t_us (the raw face-on frame timestamp — the same domain ShotReplayController's
// startUs/endUs and the analysis phases live in). The Transit timeline binds this
// directly so it can place a comparison diamond per markup position with the very
// formula it uses for the discovered stations.
QVariantList MarkupController::eventList() const
{
    QVariantList out;
    const qint64 t0 = m_fo.frameTimesUs.isEmpty() ? 0 : m_fo.frameTimesUs.first();
    for (auto it = m_truth.events.constBegin(); it != m_truth.events.constEnd(); ++it) {
        const int idx = it.value();
        if (idx < 0 || idx >= m_fo.frameCount()) continue;
        QVariantMap e;
        e.insert(QStringLiteral("name"),    it.key());
        e.insert(QStringLiteral("frame"),   idx);
        e.insert(QStringLiteral("tUs"),     qlonglong(m_fo.frameTimesUs[idx]));
        e.insert(QStringLiteral("sec"),     double(m_fo.frameTimesUs[idx] - t0) / 1e6);
        e.insert(QStringLiteral("hasClub"), m_truth.shaft.contains(idx));
        out.push_back(e);
    }
    return out;
}

QVariantList MarkupController::labelledFrames() const
{
    QVariantList out;
    for (auto it = m_truth.shaft.constBegin(); it != m_truth.shaft.constEnd(); ++it)
        out.push_back(it.key());
    return out;
}

// Render-ready list of every frame carrying a shaft (club) label, each with its
// WINDOW-RELATIVE t_us — the same domain as eventList() and the discovered
// stations. The Transit timeline draws a thin tick per entry so the frames where
// the club has actually been laid read at a glance, complementing the per-position
// diamonds (which only mark the named P-positions). m_truth.shaft is a QMap keyed
// by frame index, so this is already in ascending frame (hence time) order.
QVariantList MarkupController::shaftList() const
{
    QVariantList out;
    for (auto it = m_truth.shaft.constBegin(); it != m_truth.shaft.constEnd(); ++it) {
        const int idx = it.key();
        if (idx < 0 || idx >= m_fo.frameCount()) continue;
        QVariantMap e;
        e.insert(QStringLiteral("frame"), idx);
        e.insert(QStringLiteral("tUs"),   qlonglong(m_fo.frameTimesUs[idx]));
        out.push_back(e);
    }
    return out;
}

void MarkupController::setShaft(double gripNx, double gripNy, double headNx, double headNy)
{
    if (!hasSwing()) return;
    ShaftLabel L;
    L.gripNx = std::clamp(gripNx, 0.0, 1.0); L.gripNy = std::clamp(gripNy, 0.0, 1.0);
    L.headNx = std::clamp(headNx, 0.0, 1.0); L.headNy = std::clamp(headNy, 0.0, 1.0);
    m_truth.shaft.insert(m_frameIndex, L);
    setDirty(true);
    emit labelsChanged();
    emit frameChanged();   // currentShaft tracks the active frame
}

void MarkupController::clearShaft()
{
    if (m_truth.shaft.remove(m_frameIndex) > 0) {
        setDirty(true);
        emit labelsChanged();
        emit frameChanged();
    }
}

void MarkupController::setEvent(const QString &name)
{
    if (!hasSwing() || !eventNames().contains(name)) return;
    m_truth.events.insert(name, m_frameIndex);
    setDirty(true);
    emit labelsChanged();
}

void MarkupController::clearEvent(const QString &name)
{
    if (m_truth.events.remove(name) > 0) {
        setDirty(true);
        emit labelsChanged();
    }
}

// ── capture conditions ───────────────────────────────────────────────────────

void MarkupController::setMetaLighting(const QString &v)
{
    const QString t = v.trimmed();
    if (!hasSwing() || t == m_truth.meta.lighting) return;
    m_truth.meta.lighting = t;
    setDirty(true);
    emit metaChanged();
}

void MarkupController::setMetaShaft(const QString &v)
{
    const QString t = v.trimmed();
    if (!hasSwing() || t == m_truth.meta.shaft) return;
    m_truth.meta.shaft = t;
    setDirty(true);
    emit metaChanged();
}

void MarkupController::setMetaClub(const QString &v)
{
    const QString t = v.trimmed();
    if (!hasSwing() || t == m_truth.meta.club) return;
    m_truth.meta.club = t;
    setDirty(true);
    emit metaChanged();
}

void MarkupController::setMetaScope(const QString &v)
{
    const QString t = v.trimmed();
    if (!hasSwing() || t == m_truth.meta.scope) return;
    m_truth.meta.scope = t;
    setDirty(true);
    emit metaChanged();
}

void MarkupController::setMetaTempo(const QString &v)
{
    const QString t = v.trimmed();
    if (!hasSwing() || t == m_truth.meta.tempo) return;
    m_truth.meta.tempo = t;
    setDirty(true);
    emit metaChanged();
}

void MarkupController::setMetaContact(const QString &v)
{
    const QString t = v.trimmed();
    if (!hasSwing() || t == m_truth.meta.contact) return;
    m_truth.meta.contact = t;
    setDirty(true);
    emit metaChanged();
}

void MarkupController::setMetaClubLeavesFrame(bool v)
{
    if (!hasSwing() || v == m_truth.meta.clubLeavesFrame) return;
    m_truth.meta.clubLeavesFrame = v;
    setDirty(true);
    emit metaChanged();
}

bool MarkupController::save()
{
    if (!hasSwing() || !m_fo.ok) { emit message(QStringLiteral("Nothing to save")); return false; }
    QString err;
    const bool ok = writeTruth(currentSwingDir(), m_truth, m_fo, &err);
    if (ok) {
        setDirty(false);
        rebuildSwingsCache();
        emit swingsChanged();
        emit message(QStringLiteral("Saved %1 shaft / %2 events").arg(shaftCount()).arg(eventCount()));
    } else {
        emit message(QStringLiteral("Save failed: %1").arg(err));
    }
    return ok;
}

void MarkupController::revert()
{
    if (!hasSwing()) return;
    m_truth = m_fo.ok ? readTruth(currentSwingDir(), m_fo) : TruthDoc{};
    seedMetaDefaults();
    setDirty(false);
    emit labelsChanged();
    emit frameChanged();
    emit metaChanged();
}

// Fill the common-case capture conditions when truth.json doesn't carry them, so
// the panel starts coherent and SwingLab always has a scope to gate on. Does not
// touch fields that are already set (an edited swing keeps its choices).
void MarkupController::seedMetaDefaults()
{
    if (m_truth.meta.club.isEmpty())    m_truth.meta.club    = QStringLiteral("DRIVER");
    if (m_truth.meta.scope.isEmpty())   m_truth.meta.scope   = QStringLiteral("full");
    if (m_truth.meta.tempo.isEmpty())   m_truth.meta.tempo   = QStringLiteral("normal");
    if (m_truth.meta.contact.isEmpty()) m_truth.meta.contact = QStringLiteral("ball");
}

void MarkupController::retainPanel()
{
    if (++m_panelRefs == 1) emit panelVisibleChanged();
}

void MarkupController::releasePanel()
{
    if (m_panelRefs > 0 && --m_panelRefs == 0) emit panelVisibleChanged();
}

void MarkupController::setDirty(bool d)
{
    if (d == m_dirty) return;
    m_dirty = d;
    emit dirtyChanged();
}
