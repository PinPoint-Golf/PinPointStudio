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

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QVariantList>
#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>

#include <memory>

namespace pinpoint::swingref { class SwingReferenceModel; }

// SwingRefOverlayModel — QML_ELEMENT data layer for the WP5a swing-reference
// overlay (T7). Converts `analysisDetail.reference` (SwingReferenceBlock,
// see swing_analysis.h) + the current replay playhead into everything
// SwingRefOverlay.qml needs to draw: a CustomCamera pose/projection matching
// the block's resolved camera extrinsics/intrinsics, the ghost shaft pose at
// the current phase, the swept-surface pose list (RuledSurfaceGeometry
// input), and the 2D fallback polyline for a Canvas colour-mapped pass.
//
// DESIGN NOTE — re-instantiating the model from block params (per the T7
// brief): the persisted SwingReferenceBlock carries the RECOVERED anthro/club
// PARAMETERS (hub, armLength, rightHanded, ballOffsetX, clubName/lengthM/
// lieDeg/forwardLeanP7Deg), not 3D poses — the stage only persists the
// PROJECTED 2D polylines (`projected[]`, normalized image coords, no 3D). To
// get 3D butt/head points for the ghost shaft + ruled surface, this model
// links src/Models/swing_reference.cpp (the model core — Qt-only, no
// AnalysisContext/OpenCV deps, safe to pull into Gui) and calls
// makeSwingReferenceModel() with those recovered parameters, then samples it
// itself. RefConfig is left at pure defaults (tuned::swingref::) — any
// athlete-specific tuning overrides applied at analysis time are NOT
// persisted in the block, so the overlay's re-instantiation is a documented
// simplification (byte-identical to the analysis-time model only when no
// swingref.* overrides were in play — true for the current dark-default
// rollout). Revisit if/when per-athlete RefConfig overrides are persisted.
//
// PLAYHEAD -> PHASE: see src/Gui/viz/swing_ref_overlay_math.h for the full
// derivation. Summary: `positions` (bind analysisDetail.club.positions —
// GUARANTEED non-empty whenever reference.valid, since the stage's canRun
// gate requires !shaft.positions.empty()) supplies the primary P-index/t_us
// anchors; `reference.callouts` supplies extra/fallback anchors (P4/P6/P7,
// each independently optional, but already degrades via wrist_analyzer.cpp's
// own positionTime() fallback to a segmentation-event timestamp when
// `positions` lacks that P). Both are merged and fed to playheadToGlobalS().
// Fewer than 2 resolved anchors (e.g. a swing where neither source has P1/P4/
// P7/P8) => ghostValid stays false and the ghost/RuledSurface simply are not
// positioned meaningfully — T8/future work may add a `phases` (segmentation
// PhaseEvent) fallback for P1/P8 specifically if this proves common in
// practice.
//
// CAMERA MATRICES: QQuick3D's CustomCamera has no settable raw view matrix —
// only Node `position`/`rotation` (world pose) plus its own `projection`
// (QMatrix4x4). So THIS model exposes `cameraPosition`/`cameraRotation` (bind
// directly to CustomCamera.position/.rotation in QML) alongside the raw
// `cameraViewMatrix` (kept for API completeness / testing / future use, not
// itself bindable to any Quick3D property) and `cameraProjectionMatrix` (bind
// to CustomCamera.projection).
//
// CONTENT RECT: `contentX/Y/W/H` are accepted as plain passthrough inputs
// (matching the WP5a spec) but do NOT feed `cameraProjectionMatrix` — that
// matrix is built directly from `reference.projection`'s own fx/fy/cx/cy/
// width/height (the ORIGINAL capture frame's intrinsics), because NDC is
// resolution-independent: Qt Quick3D stretches its -1..1 clip range across
// whatever the View3D's CURRENT on-screen size is. The constraint this
// creates for T8: SwingRefOverlay's View3D must be aspect-locked to
// reference.projection.width:height (matching PpCameraFrame's existing
// aspect-locked video geometry) for the 3D overlay to align with the video
// pixel-for-pixel. contentX/Y/W/H remain available for future callout-chip
// placement (T8) and as a host-positioning contract (SwingRefOverlay is
// expected to be sized/positioned to the content rect, mirroring the video
// frame item's own aspect-locked geometry — see SwingRefOverlay.qml).
class SwingRefOverlayModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── Inputs ───────────────────────────────────────────────────────────────
    Q_PROPERTY(QVariantMap  reference      READ reference      WRITE setReference      NOTIFY referenceChanged)
    Q_PROPERTY(QVariantList positions      READ positions      WRITE setPositions      NOTIFY positionsChanged)
    Q_PROPERTY(qint64       playheadUs     READ playheadUs     WRITE setPlayheadUs     NOTIFY playheadUsChanged)
    Q_PROPERTY(qreal        contentX       READ contentX       WRITE setContentX       NOTIFY contentRectChanged)
    Q_PROPERTY(qreal        contentY       READ contentY       WRITE setContentY       NOTIFY contentRectChanged)
    Q_PROPERTY(qreal        contentW       READ contentW       WRITE setContentW       NOTIFY contentRectChanged)
    Q_PROPERTY(qreal        contentH       READ contentH       WRITE setContentH       NOTIFY contentRectChanged)
    Q_PROPERTY(double       residualWarnPx READ residualWarnPx WRITE setResidualWarnPx NOTIFY residualWarnPxChanged)

    // ── Outputs — all recomputed together; one NOTIFY, same grouping pattern
    // as BodyPoseAdapter::rotationsChanged() ─────────────────────────────────
    Q_PROPERTY(bool         valid                  READ valid                  NOTIFY outputsChanged)
    Q_PROPERTY(QMatrix4x4   cameraProjectionMatrix  READ cameraProjectionMatrix NOTIFY outputsChanged)
    Q_PROPERTY(QMatrix4x4   cameraViewMatrix        READ cameraViewMatrix       NOTIFY outputsChanged)
    Q_PROPERTY(QVector3D    cameraPosition          READ cameraPosition         NOTIFY outputsChanged)
    Q_PROPERTY(QQuaternion  cameraRotation          READ cameraRotation         NOTIFY outputsChanged)
    Q_PROPERTY(bool         ghostValid              READ ghostValid             NOTIFY outputsChanged)
    Q_PROPERTY(QVector3D    ghostButt               READ ghostButt              NOTIFY outputsChanged)
    Q_PROPERTY(QVector3D    ghostHead               READ ghostHead              NOTIFY outputsChanged)
    // Convenience for a QML `#Cylinder` Model: midpoint (position), rotation
    // aligning the primitive's rest +Y axis with butt->head, and the butt-
    // head distance (metres — divide by 100 for the Y-scale, since Quick3D's
    // built-in primitives use the legacy 100-unit convention; see
    // SwingRefOverlay.qml).
    Q_PROPERTY(QVector3D    ghostMidpoint           READ ghostMidpoint          NOTIFY outputsChanged)
    Q_PROPERTY(QQuaternion  ghostRotation           READ ghostRotation          NOTIFY outputsChanged)
    Q_PROPERTY(float        ghostLengthM            READ ghostLengthM          NOTIFY outputsChanged)
    Q_PROPERTY(QVariantList ghostPolyline2D         READ ghostPolyline2D        NOTIFY outputsChanged)
    Q_PROPERTY(QVariantList surfacePoses            READ surfacePoses           NOTIFY outputsChanged)
    // Static P1-P8 strobe poses (independent of playhead) — the overlay's
    // replacement for the single animated ghost (see SwingRefOverlay.qml).
    // Each entry is a QVariantMap:
    //   { "p": int, "butt": vector3d, "head": vector3d,
    //     "midpoint": vector3d, "rotation": quaternion, "lengthM": float }
    // midpoint/rotation/lengthM are precomputed the same way recomputeGhost()
    // does for the single ghost pose (rotationTo(+Y, direction) has no QML
    // equivalent — see ImuCalibrationFlow.qml's hand-rolled slerp for why this
    // stays in C++), so the QML Repeater3D delegate is a plain #Cylinder bind.
    // s-values reuse swing_ref_overlay_math.h::globalSForP (P1=0 .. P8=3.00) —
    // NOT redefined here.
    Q_PROPERTY(QVariantList pShaftPoses             READ pShaftPoses            NOTIFY outputsChanged)
    // Dense butt/clubhead trajectory curves across the WHOLE modelled swing
    // (P1->P8) — plain QVariantList<vector3d>, already in temporal order.
    // Sourced from the SAME sample() call/loop that builds surfacePoses (no
    // extra reconstruction pass); consumed by SwingRefOverlay.qml's two
    // PolylineGeometry Model nodes (thin white trace either side of the P1-P8
    // strobe shafts).
    Q_PROPERTY(QVariantList buttTrace               READ buttTrace              NOTIFY outputsChanged)
    Q_PROPERTY(QVariantList headTrace               READ headTrace              NOTIFY outputsChanged)
    Q_PROPERTY(bool         residualWarning         READ residualWarning        NOTIFY outputsChanged)

public:
    explicit SwingRefOverlayModel(QObject *parent = nullptr);
    ~SwingRefOverlayModel() override;

    QVariantMap reference() const { return m_reference; }
    void setReference(const QVariantMap &r);
    QVariantList positions() const { return m_positions; }
    void setPositions(const QVariantList &p);
    qint64 playheadUs() const { return m_playheadUs; }
    void setPlayheadUs(qint64 us);
    qreal contentX() const { return m_contentX; }
    void setContentX(qreal v);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal v);
    qreal contentW() const { return m_contentW; }
    void setContentW(qreal v);
    qreal contentH() const { return m_contentH; }
    void setContentH(qreal v);
    double residualWarnPx() const { return m_residualWarnPx; }
    void setResidualWarnPx(double v);

    bool         valid() const                 { return m_valid; }
    QMatrix4x4   cameraProjectionMatrix() const { return m_projMatrix; }
    QMatrix4x4   cameraViewMatrix() const       { return m_viewMatrix; }
    QVector3D    cameraPosition() const         { return m_cameraPosition; }
    QQuaternion  cameraRotation() const         { return m_cameraRotation; }
    bool         ghostValid() const             { return m_ghostValid; }
    QVector3D    ghostButt() const              { return m_ghostButt; }
    QVector3D    ghostHead() const              { return m_ghostHead; }
    QVector3D    ghostMidpoint() const          { return m_ghostMidpoint; }
    QQuaternion  ghostRotation() const          { return m_ghostRotation; }
    float        ghostLengthM() const           { return m_ghostLengthM; }
    QVariantList ghostPolyline2D() const        { return m_ghostPolyline2D; }
    QVariantList surfacePoses() const           { return m_surfacePoses; }
    QVariantList pShaftPoses() const            { return m_pShaftPoses; }
    QVariantList buttTrace() const              { return m_buttTrace; }
    QVariantList headTrace() const              { return m_headTrace; }
    bool         residualWarning() const        { return m_residualWarning; }

signals:
    void referenceChanged();
    void positionsChanged();
    void playheadUsChanged();
    void contentRectChanged();
    void residualWarnPxChanged();
    void outputsChanged();

private:
    // Rebuilds the model instance + everything that depends only on
    // `reference` (camera matrices, surfacePoses) — triggered by
    // referenceChanged. Calls recomputeGhost() at the end for the current
    // playhead.
    void rebuildFromReference();
    // Cheap per-tick recompute (ghost pose + 2D polyline selection) —
    // triggered by playheadUs/positions changes. Deliberately does NOT
    // rebuild the model or surfacePoses (that would redo ~600-sample
    // reconstruction on every scrub tick).
    void recomputeGhost();

    QVariantMap  m_reference;
    QVariantList m_positions;
    qint64       m_playheadUs = 0;
    qreal        m_contentX = 0, m_contentY = 0, m_contentW = 0, m_contentH = 0;
    double       m_residualWarnPx = 8.0;   // matches tuned::swingref::kResidualWarnPx

    bool         m_valid = false;
    QMatrix4x4   m_projMatrix;
    QMatrix4x4   m_viewMatrix;
    QVector3D    m_cameraPosition;
    QQuaternion  m_cameraRotation;
    bool         m_ghostValid = false;
    QVector3D    m_ghostButt, m_ghostHead;
    QVector3D    m_ghostMidpoint;
    QQuaternion  m_ghostRotation;
    float        m_ghostLengthM = 0.0f;
    QVariantList m_ghostPolyline2D;
    QVariantList m_surfacePoses;
    QVariantList m_pShaftPoses;
    QVariantList m_buttTrace;
    QVariantList m_headTrace;
    bool         m_residualWarning = false;

    std::unique_ptr<pinpoint::swingref::SwingReferenceModel> m_model;
    double m_modelClubLenM = 0.0;
};
