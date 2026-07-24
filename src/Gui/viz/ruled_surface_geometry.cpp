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

#include "ruled_surface_geometry.h"

#include <QVariantMap>
#include <QVector3D>

#include <vector>

RuledSurfaceGeometry::RuledSurfaceGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void RuledSurfaceGeometry::setPoses(const QVariantList &poses)
{
    if (m_poses == poses)
        return;
    m_poses = poses;
    emit posesChanged();
    rebuild();
    update();
}

void RuledSurfaceGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    struct P { QVector3D butt, head; bool restart; };
    std::vector<P> pts;
    pts.reserve(std::size_t(m_poses.size()));
    for (const QVariant &v : m_poses) {
        const QVariantMap m = v.toMap();
        P p;
        p.butt    = m.value(QStringLiteral("butt")).value<QVector3D>();
        p.head    = m.value(QStringLiteral("head")).value<QVector3D>();
        p.restart = m.value(QStringLiteral("restart")).toBool();
        pts.push_back(p);
    }

    constexpr int kFloatsPerVertex = 6;   // position(3) + normal(3)
    QByteArray vertexData;
    vertexData.reserve(int(pts.size()) * 6 * kFloatsPerVertex * int(sizeof(float)));

    QVector3D boundsMin(0, 0, 0), boundsMax(0, 0, 0);
    bool haveBounds = false;
    auto growBounds = [&](const QVector3D &p) {
        if (!haveBounds) { boundsMin = boundsMax = p; haveBounds = true; return; }
        boundsMin.setX(std::min(boundsMin.x(), p.x())); boundsMax.setX(std::max(boundsMax.x(), p.x()));
        boundsMin.setY(std::min(boundsMin.y(), p.y())); boundsMax.setY(std::max(boundsMax.y(), p.y()));
        boundsMin.setZ(std::min(boundsMin.z(), p.z())); boundsMax.setZ(std::max(boundsMax.z(), p.z()));
    };

    auto pushVert = [&](const QVector3D &pos, const QVector3D &n) {
        const float buf[kFloatsPerVertex] = { pos.x(), pos.y(), pos.z(), n.x(), n.y(), n.z() };
        vertexData.append(reinterpret_cast<const char *>(buf), sizeof(buf));
        growBounds(pos);
    };

    int quadCount = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].restart)
            continue;   // start a new strip here — no quad connects to the previous point

        const QVector3D &A = pts[i - 1].butt, &B = pts[i - 1].head;
        const QVector3D &C = pts[i].butt,     &D = pts[i].head;

        QVector3D n1 = QVector3D::crossProduct(C - A, B - A);
        if (!n1.isNull()) n1.normalize();
        QVector3D n2 = QVector3D::crossProduct(D - B, C - B);
        if (!n2.isNull()) n2.normalize();
        if (n1.isNull()) n1 = n2.isNull() ? QVector3D(0, 0, 1) : n2;
        if (n2.isNull()) n2 = n1;

        // Two triangles covering quad A,B,D,C (CCW front face, matching Qt
        // Quick3D's own custom-geometry example convention).
        pushVert(A, n1); pushVert(C, n1); pushVert(B, n1);
        pushVert(B, n2); pushVert(C, n2); pushVert(D, n2);
        ++quadCount;
    }

    if (quadCount == 0) {
        // Degenerate zero-area triangle so an empty/one-point poses list
        // never hands the renderer a zero-vertex buffer.
        pushVert(QVector3D(0, 0, 0), QVector3D(0, 0, 1));
        pushVert(QVector3D(0, 0, 0), QVector3D(0, 0, 1));
        pushVert(QVector3D(0, 0, 0), QVector3D(0, 0, 1));
    }

    setVertexData(vertexData);
    setStride(kFloatsPerVertex * int(sizeof(float)));
    setBounds(boundsMin, boundsMax);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float),
                QQuick3DGeometry::Attribute::F32Type);
}
