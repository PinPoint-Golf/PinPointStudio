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

#include "polyline_geometry.h"

#include <QVector3D>

#include <algorithm>

PolylineGeometry::PolylineGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void PolylineGeometry::setPoints(const QVariantList &points)
{
    if (m_points == points)
        return;
    m_points = points;
    emit pointsChanged();
    rebuild();
    update();
}

void PolylineGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::LineStrip);

    constexpr int kFloatsPerVertex = 3;   // position only, no normal
    QByteArray vertexData;
    vertexData.reserve(int(m_points.size()) * kFloatsPerVertex * int(sizeof(float)));

    QVector3D boundsMin(0, 0, 0), boundsMax(0, 0, 0);
    bool haveBounds = false;
    auto pushVert = [&](const QVector3D &p) {
        const float buf[kFloatsPerVertex] = { p.x(), p.y(), p.z() };
        vertexData.append(reinterpret_cast<const char *>(buf), sizeof(buf));
        if (!haveBounds) { boundsMin = boundsMax = p; haveBounds = true; return; }
        boundsMin.setX(std::min(boundsMin.x(), p.x())); boundsMax.setX(std::max(boundsMax.x(), p.x()));
        boundsMin.setY(std::min(boundsMin.y(), p.y())); boundsMax.setY(std::max(boundsMax.y(), p.y()));
        boundsMin.setZ(std::min(boundsMin.z(), p.z())); boundsMax.setZ(std::max(boundsMax.z(), p.z()));
    };

    int vertCount = 0;
    for (const QVariant &v : m_points) {
        pushVert(v.value<QVector3D>());
        ++vertCount;
    }

    if (vertCount < 2) {
        // Degenerate zero-length strip so an empty/one-point list never hands
        // the renderer a sub-minimal vertex buffer for a LineStrip (needs
        // >=2 vertices).
        pushVert(QVector3D(0, 0, 0));
        pushVert(QVector3D(0, 0, 0));
    }

    setVertexData(vertexData);
    setStride(kFloatsPerVertex * int(sizeof(float)));
    setBounds(boundsMin, boundsMax);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                QQuick3DGeometry::Attribute::F32Type);
}
