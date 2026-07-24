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

#include <QQuick3DGeometry>
#include <QQmlEngine>
#include <QVariantList>

// PolylineGeometry — thin custom QQuick3DGeometry (companion to
// RuledSurfaceGeometry, same file/registration pattern) rendering a flat list
// of world-space points as a single connected line strip
// (PrimitiveType::LineStrip). Quick3D has no per-segment "line width" control
// — GPU line rasterization is inherently ~1px regardless of any material
// property — which is exactly the "thin trace" look the swing-reference
// overlay wants for its butt/head trajectory curves (SwingRefOverlay.qml):
// a hairline arc that reads clearly without competing with the 8 P-position
// shaft cylinders or the measured-shaft colour-mapped pass.
//
// `points` shape: QVariantList<QVariant::fromValue(QVector3D)> — plain
// pass-through points already in temporal order (SwingRefOverlayModel's
// buttTrace/headTrace, sampled directly from the reference model's own
// sample() call — the same sampling that feeds surfacePoses). No restart/
// segment-break handling: the reference model's three segments are
// continuous at the P4/P7 joins (see ruled_surface_geometry.h's own comment
// on this), so the whole P1->P8 sweep is rendered as ONE unbroken strip.
//
// No normals/lighting — position-only vertex buffer, flat-white unlit
// material is applied in QML (PrincipledMaterial.NoLighting), matching the
// "just show me the trajectory" intent.
class PolylineGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PolylineGeometry)

    Q_PROPERTY(QVariantList points READ points WRITE setPoints NOTIFY pointsChanged)

public:
    explicit PolylineGeometry(QQuick3DObject *parent = nullptr);

    QVariantList points() const { return m_points; }
    void setPoints(const QVariantList &points);

signals:
    void pointsChanged();

private:
    void rebuild();

    QVariantList m_points;
};
