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

#include "swing_annotation_geometry.h"

#include "swing_annotation_mesh.h"

SwingAnnotationGeometry::SwingAnnotationGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
}

#define PP_SETTER(Name, member, Type)                          \
    void SwingAnnotationGeometry::Name(Type v)                 \
    {                                                          \
        if (member == v) return;                               \
        member = v;                                            \
        emit specChanged();                                    \
        rebuild();                                             \
    }
PP_SETTER(setElement, m_element, const QString &)
PP_SETTER(setMode, m_mode, const QString &)
PP_SETTER(setTarget, m_target, const QString &)
PP_SETTER(setLeadLeft, m_leadLeft, bool)
PP_SETTER(setRadius, m_radius, double)
PP_SETTER(setColor, m_color, const QColor &)
PP_SETTER(setColorB, m_colorB, const QColor &)
#undef PP_SETTER

void SwingAnnotationGeometry::setDriver(SwingRigDriver *d)
{
    if (m_driver == d) return;
    if (m_driver) disconnect(m_driver, nullptr, this, nullptr);
    m_driver = d;
    // A new swing loads asynchronously: rebuild when it lands, not only on the next playhead step.
    if (m_driver) connect(m_driver, &SwingRigDriver::loadedChanged, this, &SwingAnnotationGeometry::rebuild);
    emit specChanged();
    rebuild();
}

void SwingAnnotationGeometry::setRevision(int v)
{
    if (m_revision == v) return;
    m_revision = v;
    emit specChanged();
    rebuild();
}

void SwingAnnotationGeometry::rebuild()
{
    swing3d::AnnotMesh m;
    const SwingAnnotTrack *T = m_driver ? m_driver->annotTrack() : nullptr;
    if (T) {
        swing3d::AnnotSpec s;
        s.element = m_element;
        s.mode = m_mode;
        s.target = m_target;
        s.leadLeft = m_leadLeft;
        s.radius = float(m_radius);
        s.color = m_color;
        s.colorB = m_colorB;
        m = swing3d::buildAnnotationMesh(*T, s, m_driver->annotTimeUs());
    }
    if (m.vertexCount == 0 && m_vertexCount == 0) return;     // empty stays empty: no upload
    clear();
    setStride(swing3d::kAnnotStride);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 24, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    setVertexData(m.vertices);
    setIndexData(m.indices);
    setBounds(m.lo, m.hi);
    update();
    m_vertexCount = m.vertexCount;
    m_indexCount = m.indexCount;
    emit rebuilt();
}
