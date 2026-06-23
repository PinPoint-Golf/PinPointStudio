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

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

using namespace pinpoint::markup;

MarkupController::MarkupController(QObject *parent) : QObject(parent) {}

MarkupController::~MarkupController() { m_cap.release(); }

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
        m_cap.release();
        m_dirty = false;
        emit swingsChanged();
        emit currentChanged();
        emit labelsChanged();
        emit dirtyChanged();
        emit frameChanged();
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
    m_cap.release();
    if (m_fo.ok)
        m_cap.open(QDir(dir).filePath(m_fo.videoFile).toStdString());
    m_truth = m_fo.ok ? readTruth(dir, m_fo) : TruthDoc{};
    m_pose  = m_fo.ok ? readPose2d(dir)     : PoseTrack{};
    m_dirty = false;
    m_frameIndex = 0;

    emit currentChanged();
    emit labelsChanged();
    emit dirtyChanged();
    decodeFrame(0);

    if (!m_fo.ok)
        emit message(QStringLiteral("No face-on stream in %1").arg(currentSwingName()));
    else if (!m_cap.isOpened())
        emit message(QStringLiteral("Could not open %1").arg(m_fo.videoFile));
}

void MarkupController::nextSwing() { if (m_currentIndex + 1 < m_swingDirs.size()) openSwing(m_currentIndex + 1); }
void MarkupController::prevSwing() { if (m_currentIndex > 0)                       openSwing(m_currentIndex - 1); }

// ── frame view ───────────────────────────────────────────────────────────────

void MarkupController::decodeFrame(int idx)
{
    if (!m_fo.ok || !m_cap.isOpened() || m_fo.frameCount() <= 0) return;
    idx = std::clamp(idx, 0, m_fo.frameCount() - 1);

    m_cap.set(cv::CAP_PROP_POS_FRAMES, idx);
    cv::Mat bgr;
    if (!m_cap.read(bgr) || bgr.empty()) {
        emit message(QStringLiteral("Decode failed at frame %1").arg(idx));
        return;
    }
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    const QImage img(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888);
    if (m_provider) m_provider->setImage(img.copy());   // detach from the cv::Mat buffer

    m_frameIndex = idx;
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

QVariantList MarkupController::labelledFrames() const
{
    QVariantList out;
    for (auto it = m_truth.shaft.constBegin(); it != m_truth.shaft.constEnd(); ++it)
        out.push_back(it.key());
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
    setDirty(false);
    emit labelsChanged();
    emit frameChanged();
}

void MarkupController::setDirty(bool d)
{
    if (d == m_dirty) return;
    m_dirty = d;
    emit dirtyChanged();
}
