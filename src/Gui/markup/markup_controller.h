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

// MarkupController (QML context property `markupController`) — backs the Markup
// Lab screen: a queue of recorded swings to label, an exact-frame view of the
// active swing's face-on MP4 (decoded on demand via Qt Multimedia — the same
// QMediaPlayer/QVideoSink path used by replay), and the in-memory shaft/event
// labels. Labels are held NORMALIZED (resolution-agnostic) and persisted as a
// SwingLab-compatible <swingDir>/truth.json via markup_truth (proven
// byte-compatible against tools/swinglab score.py).
//
// Frame display: a seek decodes one still asynchronously; onFrame() pushes the
// QImage to a MarkupImageProvider and bumps `frameToken`; QML binds
// `image://markup/<frameToken>` so the new frame renders.

#include "markup_truth.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class MarkupImageProvider;
class QMediaPlayer;
class QVideoSink;
class QVideoFrame;

class MarkupController : public QObject
{
    Q_OBJECT
    // ── queue ──────────────────────────────────────────────────────────────
    Q_PROPERTY(QVariantList swings        READ swings        NOTIFY swingsChanged)
    Q_PROPERTY(int          swingCount    READ swingCount    NOTIFY swingsChanged)
    Q_PROPERTY(int          labelledCount READ labelledCount NOTIFY swingsChanged)
    Q_PROPERTY(int          currentIndex  READ currentIndex  NOTIFY currentChanged)
    Q_PROPERTY(QString      currentSwingDir  READ currentSwingDir  NOTIFY currentChanged)
    Q_PROPERTY(QString      currentSwingName READ currentSwingName NOTIFY currentChanged)
    Q_PROPERTY(bool         hasSwing      READ hasSwing      NOTIFY currentChanged)
    // ── frame view ─────────────────────────────────────────────────────────
    Q_PROPERTY(int          frameToken    READ frameToken    NOTIFY frameChanged)
    Q_PROPERTY(int          frameIndex    READ frameIndex    NOTIFY frameChanged)
    Q_PROPERTY(int          frameCount    READ frameCount    NOTIFY currentChanged)
    Q_PROPERTY(double       frameSec      READ frameSec      NOTIFY frameChanged)
    Q_PROPERTY(double       videoAspect   READ videoAspect   NOTIFY currentChanged)
    Q_PROPERTY(int          stride        READ stride        WRITE setStride NOTIFY strideChanged)
    // ── labels ─────────────────────────────────────────────────────────────
    Q_PROPERTY(int          shaftCount    READ shaftCount    NOTIFY labelsChanged)
    Q_PROPERTY(int          eventCount    READ eventCount    NOTIFY labelsChanged)
    Q_PROPERTY(bool         dirty         READ dirty         NOTIFY dirtyChanged)
    Q_PROPERTY(QVariantMap  currentShaft  READ currentShaft  NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap  events        READ events        NOTIFY labelsChanged)
    Q_PROPERTY(QVariantList eventList     READ eventList     NOTIFY labelsChanged)
    Q_PROPERTY(QVariantList labelledFrames READ labelledFrames NOTIFY labelsChanged)
    // ── panel presence ───────────────────────────────────────────────────────
    // True while the (active screen's) Markup panel is on-screen — the Transit
    // timeline only paints its markup-diamond overlay while this holds, so toggling
    // the panel out of the View hides the diamonds. Maintained by retain/release
    // claims so the StackLayout's off-screen duplicate panel can't interfere.
    Q_PROPERTY(bool         panelVisible  READ panelVisible  NOTIFY panelVisibleChanged)
    // ── recorded pose overlay (display only) ─────────────────────────────────
    Q_PROPERTY(bool         poseAvailable READ poseAvailable NOTIFY currentChanged)
    Q_PROPERTY(bool         showSkeleton  READ showSkeleton  WRITE setShowSkeleton NOTIFY skeletonChanged)
    Q_PROPERTY(QVariantMap  currentPose   READ currentPose   NOTIFY poseChanged)

public:
    explicit MarkupController(QObject *parent = nullptr);
    ~MarkupController() override;

    // Non-owning; the QML engine owns the provider (addImageProvider). Set once
    // at startup before any swing is opened.
    void setImageProvider(MarkupImageProvider *p) { m_provider = p; }

    QVariantList swings()        const { return m_swingsCache; }
    int          swingCount()    const { return int(m_swingDirs.size()); }
    int          labelledCount() const;
    int          currentIndex()  const { return m_currentIndex; }
    QString      currentSwingDir()  const;
    QString      currentSwingName() const;
    bool         hasSwing()      const { return m_currentIndex >= 0 && m_currentIndex < m_swingDirs.size(); }

    int          frameToken()    const { return m_frameToken; }
    int          frameIndex()    const { return m_frameIndex; }
    int          frameCount()    const { return m_fo.frameCount(); }
    double       frameSec()      const;
    double       videoAspect()   const { return m_fo.srcHeight > 0 ? double(m_fo.srcWidth) / m_fo.srcHeight : 1.0; }
    int          stride()        const { return m_stride; }
    void         setStride(int s);

    int          shaftCount()    const { return int(m_truth.shaft.size()); }
    int          eventCount()    const { return int(m_truth.events.size()); }
    bool         dirty()         const { return m_dirty; }
    QVariantMap  currentShaft()  const;
    QVariantMap  events()        const;
    QVariantList eventList()     const;
    QVariantList labelledFrames() const;
    bool         panelVisible()  const { return m_panelRefs > 0; }

    bool         poseAvailable() const { return m_pose.ok; }
    bool         showSkeleton()  const { return m_showSkeleton; }
    void         setShowSkeleton(bool on);
    QVariantMap  currentPose()   const;

    // Queue control
    Q_INVOKABLE void loadSwings(const QVariantList &swingDirs);
    // Panel context: focus a single swing (or clear with ""). No-op when the dir
    // is already loaded, so in-progress edits survive a re-bind.
    Q_INVOKABLE void loadSwing(const QString &swingDir);
    Q_INVOKABLE void openSwing(int queueIndex);
    Q_INVOKABLE void nextSwing();
    Q_INVOKABLE void prevSwing();

    // Frame navigation
    Q_INVOKABLE void stepFrame(int delta);
    Q_INVOKABLE void setFrameIndex(int idx);
    Q_INVOKABLE void seekFraction(double frac);
    Q_INVOKABLE void nextLabelled();
    Q_INVOKABLE void prevLabelled();

    // Labelling (normalized [0..1] points in the displayed video space)
    Q_INVOKABLE void setShaft(double gripNx, double gripNy, double headNx, double headNy);
    Q_INVOKABLE void clearShaft();
    Q_INVOKABLE void setEvent(const QString &name);
    Q_INVOKABLE void clearEvent(const QString &name);

    Q_INVOKABLE bool save();
    Q_INVOKABLE void revert();

    // Panel presence claims — the visible Markup panel retains on show / releases on
    // hide+destroy (see panelVisible). Balanced, refcounted, so transient overlaps
    // between screens never flip the state spuriously.
    Q_INVOKABLE void retainPanel();
    Q_INVOKABLE void releasePanel();

signals:
    void swingsChanged();
    void currentChanged();
    void frameChanged();
    void labelsChanged();
    void dirtyChanged();
    void strideChanged();
    void skeletonChanged();
    void poseChanged();
    void panelVisibleChanged();
    void message(const QString &text);

private:
    void rebuildSwingsCache();
    void decodeFrame(int idx);
    void onFrame(const QVideoFrame &frame);   // QVideoSink::videoFrameChanged
    void setDirty(bool d);

    MarkupImageProvider          *m_provider = nullptr;
    QStringList                   m_swingDirs;
    QVariantList                  m_swingsCache;
    int                           m_currentIndex = -1;

    pinpoint::markup::FaceOnInfo  m_fo;
    pinpoint::markup::TruthDoc    m_truth;
    pinpoint::markup::PoseTrack   m_pose;
    QMediaPlayer                 *m_player = nullptr;   // owned (child); decodes recorded MP4
    QVideoSink                   *m_sink = nullptr;     // owned (child); receives seeked stills
    int                           m_requestedIdx = -1;  // latest frame asked for (async seek)
    bool                          m_nudged = false;     // exactness guard fired once per seek
    bool                          m_sourceReady = false;// media reached LoadedMedia
    int                           m_frameIndex = 0;
    int                           m_frameToken = 0;
    int                           m_stride = 10;
    bool                          m_dirty = false;
    bool                          m_showSkeleton = true;
    int                           m_panelRefs = 0;       // live visible-panel claims
};
