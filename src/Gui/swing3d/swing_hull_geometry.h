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

// SwingHullGeometry — the 3-D swing figure as ONE skinned mesh (tools/generate_swing3d_hull.py).
//
// Loads hull.glb (POSITION, NORMAL, JOINTS_0 u16×4 as ybot joint indices, WEIGHTS_0 f32×4) into a
// QQuick3DGeometry, and supplies the Skin's inverse bind poses computed from the SAME rig the fit
// solves (skeleton3d_rig.h at θ = 0, rig frame, unit scale). The generator wrote its own bind
// joint positions into the file; bindMismatchM is the worst difference, so a rig change that the
// hull was not regenerated for is caught instead of drawn as a mangled body.
//
// QML: Model { geometry: SwingHullGeometry { id: g; source: ":/assets/swing3d/hull.glb" }
//              skin: Skin { joints: [ …the 27 bone nodes in ybot order… ]; inverseBindPoses: g.inverseBindPoses } }

#include <QList>
#include <QMatrix4x4>
#include <QQmlEngine>
#include <QQuick3DGeometry>
#include <QString>

class SwingHullGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY loaded)
    Q_PROPERTY(QString error READ error NOTIFY loaded)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY loaded)
    Q_PROPERTY(double bindMismatchM READ bindMismatchM NOTIFY loaded)
    Q_PROPERTY(QList<QMatrix4x4> inverseBindPoses READ inverseBindPoses NOTIFY loaded)

public:
    explicit SwingHullGeometry(QQuick3DObject *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &s);
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    int vertexCount() const { return m_vertexCount; }
    double bindMismatchM() const { return m_bindMismatch; }
    QList<QMatrix4x4> inverseBindPoses() const { return m_invBind; }

signals:
    void sourceChanged();
    void loaded();

private:
    void load();

    QString m_source;
    bool m_ready = false;
    QString m_error;
    int m_vertexCount = 0;
    double m_bindMismatch = -1.0;
    QList<QMatrix4x4> m_invBind;
};
