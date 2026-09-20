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
// active swing's MP4 (decoded on demand via Qt Multimedia — the same
// QMediaPlayer/QVideoSink path used by replay), and the in-memory shaft/event
// labels. Labels are held NORMALIZED (resolution-agnostic) and persisted as a
// SwingLab-compatible <swingDir>/truth.json via markup_truth (proven
// byte-compatible against tools/swinglab score.py).
//
// TWO PANES, ONE PLAYHEAD, TWO SIDECARS. When the swing has a down-the-line
// camera the panel shows both side by side and they scrub together. There is ONE
// playhead, in window µs. The FACE-ON stream is the master for stepping (a frame
// step is a face-on frame, as it always was); the DTL pane follows to the frame
// whose t_us is NEAREST the playhead — never by index, because the cameras share
// the window clock but sit ~3 ms out of phase and need not even have the same
// frame count.
//
// Marks go to the sidecar of the pane they were made in: face-on → truth.json
// (unchanged, byte for byte), DTL → truth_dtl.json in DTL source pixels, stamped
// with the DTL frame's own t_us. Saving writes each file only when that pane has
// something to write, so marking one can never disturb the other. P-positions and
// capture conditions are instants/properties of the SWING, not of a camera: they
// hang off the shared playhead and live in truth.json only.
//
// Frame display: a seek decodes one still asynchronously per pane; the frame
// handler pushes the QImage to the MarkupImageProvider under that pane's id and
// bumps its token; QML binds `image://markup/face/<frameToken>` and
// `image://markup/dtl/<dtlFrameToken>` so each new frame renders.

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
    // ── the second camera ──────────────────────────────────────────────────
    // `hasDtl` is false when the focused swing has no down-the-line stream — the
    // panel then shows the single face-on pane it always did. `activePane` (0 =
    // face-on, 1 = DTL) is the pane a keyboard mark / clear / ball lands in: the
    // last one clicked, so where the next action goes is never in doubt.
    Q_PROPERTY(bool         hasDtl        READ hasDtl        NOTIFY currentChanged)
    Q_PROPERTY(int          activePane    READ activePane    WRITE setActivePane NOTIFY activePaneChanged)
    Q_PROPERTY(QString      faceAlias     READ faceAlias     NOTIFY currentChanged)
    Q_PROPERTY(QString      dtlAlias      READ dtlAlias      NOTIFY currentChanged)
    // ── frame view ─────────────────────────────────────────────────────────
    Q_PROPERTY(int          frameToken    READ frameToken    NOTIFY frameChanged)
    Q_PROPERTY(int          frameIndex    READ frameIndex    NOTIFY frameChanged)
    Q_PROPERTY(int          frameCount    READ frameCount    NOTIFY currentChanged)
    Q_PROPERTY(double       frameSec      READ frameSec      NOTIFY frameChanged)
    Q_PROPERTY(double       videoAspect   READ videoAspect   NOTIFY currentChanged)
    Q_PROPERTY(int          stride        READ stride        WRITE setStride NOTIFY strideChanged)
    // ── the DTL pane's own frame ───────────────────────────────────────────
    // Its own index / time / aspect, so the pane can show which DTL frame is
    // paired with the face-on one and the panel can size it to its own shape.
    Q_PROPERTY(int          dtlFrameToken READ dtlFrameToken NOTIFY dtlFrameChanged)
    Q_PROPERTY(int          dtlFrameIndex READ dtlFrameIndex NOTIFY dtlFrameChanged)
    Q_PROPERTY(int          dtlFrameCount READ dtlFrameCount NOTIFY currentChanged)
    Q_PROPERTY(double       dtlFrameSec   READ dtlFrameSec   NOTIFY dtlFrameChanged)
    Q_PROPERTY(double       dtlAspect     READ dtlAspect     NOTIFY currentChanged)
    // ── labels ─────────────────────────────────────────────────────────────
    Q_PROPERTY(int          shaftCount    READ shaftCount    NOTIFY labelsChanged)
    Q_PROPERTY(int          eventCount    READ eventCount    NOTIFY labelsChanged)
    Q_PROPERTY(bool         dirty         READ dirty         NOTIFY dirtyChanged)
    Q_PROPERTY(QVariantMap  currentShaft  READ currentShaft  NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap  events        READ events        NOTIFY labelsChanged)
    Q_PROPERTY(QVariantList eventList     READ eventList     NOTIFY labelsChanged)
    Q_PROPERTY(QVariantList shaftList     READ shaftList     NOTIFY labelsChanged)
    Q_PROPERTY(QVariantList labelledFrames READ labelledFrames NOTIFY labelsChanged)
    // ── stationary ball centre (truth.json "ball") ───────────────────────────
    // A single per-swing point (the ball doesn't move) — {has, nx, ny}, normalized.
    // Ground truth for the ball-detector v2 position gate.
    Q_PROPERTY(QVariantMap  ballPoint     READ ballPoint     NOTIFY labelsChanged)
    // ── the DTL pane's labels (truth_dtl.json) ─────────────────────────────
    // Same shapes as the face-on ones, normalized in the DTL frame. Separate
    // because they are a different pixel space in a different file.
    Q_PROPERTY(int          dtlShaftCount READ dtlShaftCount NOTIFY labelsChanged)
    Q_PROPERTY(QVariantMap  dtlShaft      READ dtlShaft      NOTIFY dtlFrameChanged)
    Q_PROPERTY(QVariantMap  dtlBallPoint  READ dtlBallPoint  NOTIFY labelsChanged)
    // ── capture conditions (truth.json "meta", for SwingLab) ─────────────────
    // Free-form strings; "" = unset. Canonical lowercase for the enums, a label
    // for club. Written additively, omitted when unset (see markup_truth). `scope`
    // is consumed by SwingLab validation: full-swing-only checks are skipped when
    // it isn't "full" (so a pitch/chip/putt doesn't fail the full-swing bounds).
    Q_PROPERTY(QString      metaLighting  READ metaLighting  WRITE setMetaLighting NOTIFY metaChanged)
    Q_PROPERTY(QString      metaShaft     READ metaShaft     WRITE setMetaShaft    NOTIFY metaChanged)
    Q_PROPERTY(QString      metaClub      READ metaClub      WRITE setMetaClub     NOTIFY metaChanged)
    // The shared canonical club vocabulary (club_vocabulary.h) — same list the
    // shot-carousel swing-edit popover offers, so both pickers speak one dialect.
    Q_PROPERTY(QStringList  clubOptions   READ clubOptions   CONSTANT)
    Q_PROPERTY(QString      metaScope     READ metaScope     WRITE setMetaScope    NOTIFY metaChanged)
    Q_PROPERTY(QString      metaTempo     READ metaTempo     WRITE setMetaTempo    NOTIFY metaChanged)
    Q_PROPERTY(QString      metaContact   READ metaContact   WRITE setMetaContact  NOTIFY metaChanged)
    Q_PROPERTY(bool         metaClubLeavesFrame READ metaClubLeavesFrame WRITE setMetaClubLeavesFrame NOTIFY metaChanged)
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

    bool         hasDtl()        const { return m_dtl.ok; }
    int          activePane()    const { return m_activePane; }
    void         setActivePane(int p);
    QString      faceAlias()     const { return m_fo.alias; }
    QString      dtlAlias()      const { return m_dtl.alias; }

    int          frameToken()    const { return m_frameToken; }
    int          frameIndex()    const { return m_frameIndex; }
    int          frameCount()    const { return m_fo.frameCount(); }
    double       frameSec()      const;
    double       videoAspect()   const { return m_fo.srcHeight > 0 ? double(m_fo.srcWidth) / m_fo.srcHeight : 1.0; }
    int          stride()        const { return m_stride; }
    void         setStride(int s);

    int          dtlFrameToken() const { return m_dtlFrameToken; }
    int          dtlFrameIndex() const { return m_dtlFrameIndex; }
    int          dtlFrameCount() const { return m_dtl.frameCount(); }
    double       dtlFrameSec()   const;
    double       dtlAspect()     const { return m_dtl.srcHeight > 0 ? double(m_dtl.srcWidth) / m_dtl.srcHeight : 1.0; }

    int          shaftCount()    const { return int(m_truth.shaft.size()); }
    int          eventCount()    const { return int(m_truth.events.size()); }
    // Either pane having unsaved marks lights the Save button — one Save writes
    // whichever sidecars have something to write.
    bool         dirty()         const { return m_dirty || m_dirtyDtl; }
    QVariantMap  currentShaft()  const;
    QVariantMap  events()        const;
    QVariantList eventList()     const;
    QVariantList shaftList()     const;
    QVariantList labelledFrames() const;
    QVariantMap  ballPoint()     const;
    bool         panelVisible()  const { return m_panelRefs > 0; }

    int          dtlShaftCount() const { return int(m_dtlTruth.shaft.size()); }
    QVariantMap  dtlShaft()      const;
    QVariantMap  dtlBallPoint()  const;

    QString      metaLighting()  const { return m_truth.meta.lighting; }
    QString      metaShaft()     const { return m_truth.meta.shaft; }
    QString      metaClub()      const { return m_truth.meta.club; }
    QStringList  clubOptions()   const;
    QString      metaScope()     const { return m_truth.meta.scope; }
    QString      metaTempo()     const { return m_truth.meta.tempo; }
    QString      metaContact()   const { return m_truth.meta.contact; }
    bool         metaClubLeavesFrame() const { return m_truth.meta.clubLeavesFrame; }
    void         setMetaLighting(const QString &v);
    void         setMetaShaft(const QString &v);
    void         setMetaClub(const QString &v);
    void         setMetaScope(const QString &v);
    void         setMetaTempo(const QString &v);
    void         setMetaContact(const QString &v);
    void         setMetaClubLeavesFrame(bool v);

    // The recorded pose is the FACE-ON 2D pose, so it is drawn over the face-on
    // pane only — there is no skeleton to put on the down-the-line frame.
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

    // The same two, in the DTL pane: normalized in the DTL frame, landing on the
    // DTL frame currently paired with the playhead, persisted to truth_dtl.json.
    Q_INVOKABLE void setDtlShaft(double gripNx, double gripNy, double headNx, double headNy);
    Q_INVOKABLE void clearDtlShaft();

    // Stationary ball centre — a single per-swing point (the ball doesn't move),
    // marked with one click. setBall places/moves it; clearBall removes it. The
    // ball is marked once PER CAMERA: it is a different point in each frame.
    Q_INVOKABLE void setBall(double nx, double ny);
    Q_INVOKABLE void clearBall();
    Q_INVOKABLE void setDtlBall(double nx, double ny);
    Q_INVOKABLE void clearDtlBall();

    // Pane-routed forms for the keyboard (u / c / b act on the active pane).
    Q_INVOKABLE void clearShaftIn(int pane);
    Q_INVOKABLE void clearBallIn(int pane);

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
    void activePaneChanged();
    void frameChanged();
    void dtlFrameChanged();
    void labelsChanged();
    void dirtyChanged();
    void strideChanged();
    void skeletonChanged();
    void poseChanged();
    void panelVisibleChanged();
    void metaChanged();
    void message(const QString &text);

private:
    // One camera's decode machinery. Both panes run the identical frame-accurate
    // path — never play, seek to a still, pull it off the sink — so it is written
    // once and instantiated twice rather than copied.
    struct Decoder {
        QMediaPlayer *player = nullptr;      // owned (child); decodes recorded MP4
        QVideoSink   *sink   = nullptr;      // owned (child); receives seeked stills
        int           requestedIdx = -1;     // latest frame asked for (async seek)
        bool          nudged = false;        // exactness guard fired once per seek
        bool          sourceReady = false;   // media reached LoadedMedia
    };

    void rebuildSwingsCache();
    void decodeFrame(int idx);                // the shared playhead → both panes
    void setDirty(bool d);
    void setDirtyDtl(bool d);
    void seedMetaDefaults();                  // common-case capture conditions

    Decoder &decoderFor(bool dtl) { return dtl ? m_dtlDec : m_faceDec; }
    const pinpoint::markup::VideoStreamInfo &streamFor(bool dtl) const { return dtl ? m_dtl : m_fo; }
    // Ask `d` for frame `idx` of `vi`; accept a decoded still into the pane.
    void decodeInto(Decoder &d, const pinpoint::markup::VideoStreamInfo &vi, int idx);
    void acceptFrame(Decoder &d, const pinpoint::markup::VideoStreamInfo &vi,
                     const QVideoFrame &frame, bool dtl);
    // Point a pane's player at its stream and ask for `idx` once the media loads.
    void openStream(Decoder &d, const pinpoint::markup::VideoStreamInfo &vi, int idx);
    // The window-µs instant the playhead is on (the face-on frame's own t_us).
    qint64 playheadUs() const;

    MarkupImageProvider          *m_provider = nullptr;
    QStringList                   m_swingDirs;
    QVariantList                  m_swingsCache;
    int                           m_currentIndex = -1;

    pinpoint::markup::VideoStreamInfo m_fo;       // face-on        → truth.json
    pinpoint::markup::VideoStreamInfo m_dtl;      // down-the-line  → truth_dtl.json
    pinpoint::markup::TruthDoc    m_truth;        // face-on labels
    pinpoint::markup::TruthDoc    m_dtlTruth;     // DTL labels (shaft + ball only)
    pinpoint::markup::PoseTrack   m_pose;
    Decoder                       m_faceDec;
    Decoder                       m_dtlDec;
    int                           m_frameIndex = 0;     // face-on: the playhead itself
    int                           m_frameToken = 0;
    int                           m_dtlFrameIndex = 0;  // DTL: nearest t_us to the playhead
    int                           m_dtlFrameToken = 0;
    int                           m_stride = 10;
    int                           m_activePane = 0;      // 0 = face-on, 1 = DTL
    bool                          m_dirty = false;
    bool                          m_dirtyDtl = false;
    bool                          m_showSkeleton = true;
    int                           m_panelRefs = 0;       // live visible-panel claims
};
