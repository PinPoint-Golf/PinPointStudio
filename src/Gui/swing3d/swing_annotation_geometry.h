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

// SwingAnnotationGeometry — one motion annotation of the 3-D swing panel as a Quick 3D mesh
// (docs/design/swing_3d_annotations_design.md §4). The arithmetic is swing_annotation_mesh.h;
// this only uploads it. The track is read in C++ from the driver — no QVariantMap, no JS array.
//
// QML: Model { geometry: SwingAnnotationGeometry { driver: drv; element: "arms"; mode: "trace";
//                                                  revision: drv.revision; … }
//              materials: PrincipledMaterial { lighting: NoLighting; vertexColorsEnabled: true … } }
// `revision` is the driver's: binding it re-evaluates the mesh on every playhead step.

#include <QColor>
#include <QPointer>
#include <QQmlEngine>
#include <QQuick3DGeometry>
#include <QString>

#include "swing_rig_driver.h"

class SwingAnnotationGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(SwingRigDriver *driver READ driver WRITE setDriver NOTIFY specChanged)
    Q_PROPERTY(QString element READ element WRITE setElement NOTIFY specChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY specChanged)
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY specChanged)
    Q_PROPERTY(bool leadLeft READ leadLeft WRITE setLeadLeft NOTIFY specChanged)
    Q_PROPERTY(double radius READ radius WRITE setRadius NOTIFY specChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY specChanged)
    Q_PROPERTY(QColor colorB READ colorB WRITE setColorB NOTIFY specChanged)
    Q_PROPERTY(int revision READ revision WRITE setRevision NOTIFY specChanged)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY rebuilt)
    Q_PROPERTY(int indexCount READ indexCount NOTIFY rebuilt)

public:
    explicit SwingAnnotationGeometry(QQuick3DObject *parent = nullptr);

    SwingRigDriver *driver() const { return m_driver; }
    void setDriver(SwingRigDriver *d);
    QString element() const { return m_element; }
    void setElement(const QString &v);
    QString mode() const { return m_mode; }
    void setMode(const QString &v);
    QString target() const { return m_target; }
    void setTarget(const QString &v);
    bool leadLeft() const { return m_leadLeft; }
    void setLeadLeft(bool v);
    double radius() const { return m_radius; }
    void setRadius(double v);
    QColor color() const { return m_color; }
    void setColor(const QColor &v);
    QColor colorB() const { return m_colorB; }
    void setColorB(const QColor &v);
    int revision() const { return m_revision; }
    void setRevision(int v);
    int vertexCount() const { return m_vertexCount; }
    int indexCount() const { return m_indexCount; }

signals:
    void specChanged();
    void rebuilt();

private:
    void rebuild();

    QPointer<SwingRigDriver> m_driver;
    QString m_element, m_mode = QStringLiteral("off"), m_target;
    bool m_leadLeft = true;
    double m_radius = 0.007;
    QColor m_color = Qt::white, m_colorB;
    int m_revision = 0;
    int m_vertexCount = 0, m_indexCount = 0;
};
