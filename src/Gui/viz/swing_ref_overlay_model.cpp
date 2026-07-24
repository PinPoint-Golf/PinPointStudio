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

#include "swing_ref_overlay_model.h"
#include "swing_ref_overlay_math.h"

#include "../../Models/swing_reference.h"

#include <QVariant>
#include <QVariantMap>

#include <array>
#include <optional>
#include <vector>

using namespace pinpoint::swingref;
using namespace pinpoint::swingref::overlaymath;

SwingRefOverlayModel::SwingRefOverlayModel(QObject *parent) : QObject(parent) {}
SwingRefOverlayModel::~SwingRefOverlayModel() = default;

void SwingRefOverlayModel::setReference(const QVariantMap &r)
{
    if (m_reference == r)
        return;
    m_reference = r;
    emit referenceChanged();
    rebuildFromReference();
}

void SwingRefOverlayModel::setPositions(const QVariantList &p)
{
    if (m_positions == p)
        return;
    m_positions = p;
    emit positionsChanged();
    recomputeGhost();
    emit outputsChanged();
}

void SwingRefOverlayModel::setPlayheadUs(qint64 us)
{
    if (m_playheadUs == us)
        return;
    m_playheadUs = us;
    emit playheadUsChanged();
    recomputeGhost();
    emit outputsChanged();
}

void SwingRefOverlayModel::setContentX(qreal v) { if (m_contentX == v) return; m_contentX = v; emit contentRectChanged(); }
void SwingRefOverlayModel::setContentY(qreal v) { if (m_contentY == v) return; m_contentY = v; emit contentRectChanged(); }
void SwingRefOverlayModel::setContentW(qreal v) { if (m_contentW == v) return; m_contentW = v; emit contentRectChanged(); }
void SwingRefOverlayModel::setContentH(qreal v) { if (m_contentH == v) return; m_contentH = v; emit contentRectChanged(); }

void SwingRefOverlayModel::setResidualWarnPx(double v)
{
    if (m_residualWarnPx == v)
        return;
    m_residualWarnPx = v;
    emit residualWarnPxChanged();
    if (!m_reference.isEmpty()) {
        const QVariantMap proj = m_reference.value(QStringLiteral("projection")).toMap();
        const double residualPx = proj.value(QStringLiteral("residualPx")).toDouble();
        m_residualWarning = residualPx > m_residualWarnPx;
        emit outputsChanged();
    }
}

void SwingRefOverlayModel::rebuildFromReference()
{
    m_model.reset();
    m_valid          = false;
    m_projMatrix     = QMatrix4x4();
    m_viewMatrix     = QMatrix4x4();
    m_cameraPosition = QVector3D();
    m_cameraRotation = QQuaternion();
    m_surfacePoses   = QVariantList();
    m_pShaftPoses    = QVariantList();
    m_buttTrace      = QVariantList();
    m_headTrace      = QVariantList();
    m_residualWarning = false;
    m_modelClubLenM  = 0.0;

    // Presence of "anthro" is the validity signal — swing_doc.cpp/
    // shot_processor.cpp only ever insert the "reference" block when
    // a.reference.valid was already true (see swing_analysis.h), so there is
    // no separate "valid" key in the persisted map.
    if (!m_reference.contains(QStringLiteral("anthro"))) {
        recomputeGhost();
        emit outputsChanged();
        return;
    }

    const QVariantMap anthroMap = m_reference.value(QStringLiteral("anthro")).toMap();
    const QVariantMap clubMap   = m_reference.value(QStringLiteral("club")).toMap();
    const QVariantMap projMap   = m_reference.value(QStringLiteral("projection")).toMap();

    GolferAnthro anthro;
    anthro.hub = QVector3D(float(anthroMap.value(QStringLiteral("hubX")).toDouble()),
                          float(anthroMap.value(QStringLiteral("hubY")).toDouble()),
                          float(anthroMap.value(QStringLiteral("hubZ")).toDouble()));
    anthro.armLength   = anthroMap.value(QStringLiteral("armLengthM")).toDouble();
    anthro.rightHanded = anthroMap.value(QStringLiteral("rightHanded")).toBool();

    ClubSpec club;
    club.length           = clubMap.value(QStringLiteral("lengthM")).toDouble();
    club.lieDeg           = clubMap.value(QStringLiteral("lieDeg")).toDouble();
    club.forwardLeanP7Deg = clubMap.value(QStringLiteral("forwardLeanP7Deg")).toDouble();
    // NOTE: ballOffsetX is persisted under `anthro` (SwingRefAnthro), not
    // `club` (SwingRefClub has no such field) — matches wrist_analyzer.cpp's
    // own `club.ballOffsetX = anthroOpt->ballOffsetX` assignment when the
    // stage first builds the model.
    club.ballOffsetX = anthroMap.value(QStringLiteral("ballOffsetX")).toDouble();

    if (club.length <= 0.0 || anthro.armLength <= 0.0) {
        recomputeGhost();
        emit outputsChanged();
        return;   // degrade: not enough to rebuild a usable model
    }

    // RefConfig left at pure defaults — see the class header's DESIGN NOTE.
    m_model = makeSwingReferenceModel(anthro, club, RefConfig{});
    m_modelClubLenM = club.length;

    const double w  = projMap.value(QStringLiteral("width")).toDouble();
    const double h  = projMap.value(QStringLiteral("height")).toDouble();
    const QString projKind = projMap.value(QStringLiteral("kind")).toString();

    if (projKind == QStringLiteral("Ortho")) {
        // Phase A "2D-first" orthographic anchor — the PRIMARY projection
        // path (see wrist_analyzer.cpp SwingRefStage). No rvec/tvec/fx/fy
        // were persisted (none were solved); the camera is built directly
        // from sPxPerM/originX/originY/xSign — see swing_ref_overlay_math.h's
        // "Orthographic camera <-> Qt Quick3D CustomCamera" header comment
        // for the derivation this mirrors exactly.
        const double s       = projMap.value(QStringLiteral("sPxPerM")).toDouble();
        const double originX = projMap.value(QStringLiteral("originX")).toDouble();
        const double originY = projMap.value(QStringLiteral("originY")).toDouble();
        const double xSign   = projMap.value(QStringLiteral("xSign")).toDouble();

        m_projMatrix = buildOrthoProjectionMatrix(s, originX, originY, w, h);
        const CameraPose pose = orthoExtrinsicsToQt(xSign);
        m_viewMatrix     = pose.viewMatrix;
        m_cameraPosition = pose.position;
        m_cameraRotation = pose.rotation;
    } else {
        // PnP path ("PoseFit" | "Calibrated") — fx/fy/cx/cy + rvec/tvec.
        const double fx = projMap.value(QStringLiteral("fx")).toDouble();
        const double fy = projMap.value(QStringLiteral("fy")).toDouble();
        const double cx = projMap.value(QStringLiteral("cx")).toDouble();
        const double cy = projMap.value(QStringLiteral("cy")).toDouble();
        m_projMatrix = buildProjectionMatrix(fx, fy, cx, cy, w, h);

        const QVariantList rvecList = projMap.value(QStringLiteral("rvec")).toList();
        const QVariantList tvecList = projMap.value(QStringLiteral("tvec")).toList();
        std::array<double, 3> rvec{ 0, 0, 0 }, tvec{ 0, 0, 0 };
        for (int i = 0; i < 3 && i < rvecList.size(); ++i) rvec[std::size_t(i)] = rvecList.at(i).toDouble();
        for (int i = 0; i < 3 && i < tvecList.size(); ++i) tvec[std::size_t(i)] = tvecList.at(i).toDouble();

        const CameraPose pose = openCvExtrinsicsToQt(rvec, tvec);
        m_viewMatrix     = pose.viewMatrix;
        m_cameraPosition = pose.position;
        m_cameraRotation = pose.rotation;
    }

    const double residualPx = projMap.value(QStringLiteral("residualPx")).toDouble();
    m_residualWarning = residualPx > m_residualWarnPx;

    // Surface poses — the full swept sample (all 3 segments), restart
    // flagged at the first sample of each segment. sample() always emits
    // ordered, complete per-segment runs (Backswing, Downswing,
    // FollowThrough — see GeometricReferenceModel::sample() in
    // swing_reference.cpp), so a segment-enum change IS a segment boundary.
    std::optional<Segment> prevSeg;
    const std::vector<ShaftPose> sampled = m_model->sample();
    m_surfacePoses.reserve(int(sampled.size()));
    m_buttTrace.reserve(int(sampled.size()));
    m_headTrace.reserve(int(sampled.size()));
    for (const ShaftPose &sp : sampled) {
        const bool restart = !prevSeg.has_value() || *prevSeg != sp.segment;
        prevSeg = sp.segment;
        const QVector3D head = sp.clubhead(m_modelClubLenM);
        m_surfacePoses.append(QVariantMap{
            { QStringLiteral("butt"),    QVariant::fromValue(sp.butt) },
            { QStringLiteral("head"),    QVariant::fromValue(head) },
            { QStringLiteral("restart"), restart } });
        // Dense butt/head trajectory (P1->P8) — same sample, no extra pass.
        // No restart handling needed: the three segments are continuous at
        // the P4/P7 joins (see ruled_surface_geometry.h's own comment), so
        // the whole sweep is one unbroken strip.
        m_buttTrace.append(QVariant::fromValue(sp.butt));
        m_headTrace.append(QVariant::fromValue(head));
    }

    // Static P1-P8 strobe poses — the overlay's replacement for the single
    // animated ghost (SwingRefOverlay.qml). s-values come from
    // globalSForP() (P1=0.00 .. P8=3.00, see swing_ref_overlay_math.h), NOT
    // redefined here. midpoint/rotation/lengthM are precomputed the same way
    // recomputeGhost() does below for the single ghost pose, so the QML
    // Repeater3D delegate stays a plain #Cylinder bind.
    m_pShaftPoses.reserve(8);
    for (int p = 1; p <= 8; ++p) {
        const SegmentPhase ph = segmentForGlobalS(globalSForP(p));
        const Segment segEnum = ph.segment == 0 ? Segment::Backswing
                               : ph.segment == 1 ? Segment::Downswing
                                                  : Segment::FollowThrough;
        const ShaftPose pose = m_model->evaluate(segEnum, ph.localS);
        const QVector3D butt  = pose.butt;
        const QVector3D head  = pose.clubhead(m_modelClubLenM);
        const QVector3D delta = head - butt;
        const float lengthM = delta.length();
        const QQuaternion rot = lengthM > 1e-6f
            ? QQuaternion::rotationTo(QVector3D(0, 1, 0), delta.normalized())
            : QQuaternion();
        m_pShaftPoses.append(QVariantMap{
            { QStringLiteral("p"),        p },
            { QStringLiteral("butt"),     QVariant::fromValue(butt) },
            { QStringLiteral("head"),     QVariant::fromValue(head) },
            { QStringLiteral("midpoint"), QVariant::fromValue(butt + 0.5f * delta) },
            { QStringLiteral("rotation"), QVariant::fromValue(rot) },
            { QStringLiteral("lengthM"),  lengthM } });
    }

    m_valid = true;
    recomputeGhost();
    emit outputsChanged();
}

void SwingRefOverlayModel::recomputeGhost()
{
    m_ghostValid      = false;
    m_ghostButt       = QVector3D();
    m_ghostHead       = QVector3D();
    m_ghostMidpoint   = QVector3D();
    m_ghostRotation   = QQuaternion();
    m_ghostLengthM    = 0.0f;
    m_ghostPolyline2D = QVariantList();

    if (!m_model)
        return;

    // Merge (P, t_us) anchors: `positions` (analysisDetail.club.positions —
    // primary, guaranteed non-empty) first, `reference.callouts` (secondary/
    // fallback) second — playheadToGlobalS keeps the FIRST occurrence of a
    // given P, so `positions` wins ties.
    std::vector<PhaseAnchor> anchors;
    anchors.reserve(std::size_t(m_positions.size() + 8));
    for (const QVariant &v : m_positions) {
        const QVariantMap m = v.toMap();
        anchors.push_back({ m.value(QStringLiteral("p")).toInt(),
                          m.value(QStringLiteral("t_us")).toLongLong() });
    }
    const QVariantList callouts = m_reference.value(QStringLiteral("callouts")).toList();
    for (const QVariant &v : callouts) {
        const QVariantMap m = v.toMap();
        anchors.push_back({ m.value(QStringLiteral("p")).toInt(),
                          m.value(QStringLiteral("t_us")).toLongLong() });
    }

    const std::optional<double> s = playheadToGlobalS(anchors, m_playheadUs);
    if (!s)
        return;   // < 2 resolved P anchors — no reliable phase, freeze/hide (see class header note)

    const SegmentPhase sp = segmentForGlobalS(*s);
    const Segment segEnum = sp.segment == 0 ? Segment::Backswing
                           : sp.segment == 1 ? Segment::Downswing
                                              : Segment::FollowThrough;
    const ShaftPose pose = m_model->evaluate(segEnum, sp.localS);
    m_ghostButt  = pose.butt;
    m_ghostHead  = pose.clubhead(m_modelClubLenM);
    m_ghostValid = true;

    // Cylinder-friendly convenience outputs: Quick3D's built-in "#Cylinder"
    // primitive has its long axis along LOCAL +Y (same convention as
    // BodyVizView's bone chain) — rotationTo() finds the shortest-arc
    // rotation from rest +Y to the actual butt->head direction.
    const QVector3D delta = m_ghostHead - m_ghostButt;
    m_ghostLengthM  = delta.length();
    m_ghostMidpoint = m_ghostButt + 0.5f * delta;
    m_ghostRotation = m_ghostLengthM > 1e-6f
        ? QQuaternion::rotationTo(QVector3D(0, 1, 0), delta.normalized())
        : QQuaternion();

    // 2D fallback polyline for the current segment (Canvas colour-mapped
    // pass, T8) — matched by segment index only; single-camera product mode
    // today, so the block's `camera` field is not filtered on here. A future
    // multi-camera (face-on + DTL) pass would add a `cameraId` input
    // property and match on both fields.
    const QVariantList projected = m_reference.value(QStringLiteral("projected")).toList();
    for (const QVariant &pv : projected) {
        const QVariantMap pm = pv.toMap();
        if (pm.value(QStringLiteral("segment")).toInt() == sp.segment) {
            m_ghostPolyline2D = pm.value(QStringLiteral("points")).toList();
            break;
        }
    }
}
