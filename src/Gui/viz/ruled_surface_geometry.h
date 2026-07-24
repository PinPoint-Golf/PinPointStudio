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

// RuledSurfaceGeometry — first custom QQuick3DGeometry in the app (T7, WP5a
// overlay). Builds a ruled strip surface from a flat list of {butt, head}
// pose pairs (the swing-reference model's sampled shaft poses, world/ball
// frame, metres) — the "swept plane" visualisation showing the whole
// backswing->downswing->follow-through shaft sweep at once, distinct from the
// single playhead-driven ghost shaft (a plain #Cylinder, drawn separately in
// SwingRefOverlay.qml).
//
// `poses` shape (documented here since it's a QVariantList, not a typed
// struct — the design choice per the T7 brief's "flat list of floats or a
// pose struct; pick and document"): each entry is a QVariantMap
//   { "butt": vector3d, "head": vector3d, "restart": bool }
// consecutive entries (i-1, i) with entry[i].restart == false form one quad
// (butt[i-1], head[i-1], butt[i], head[i]) -> two triangles. `restart: true`
// starts a NEW strip at that point instead (used at the first sample of each
// of the reference model's three segments — Backswing/Downswing/
// FollowThrough — even though the poses ARE continuous across P4/P7 joins,
// so a future per-segment colour/shading pass has a clean seam to key off).
// SwingRefOverlayModel::surfacePoses is the producer of this shape.
//
// Per-quad flat normals (cross product of the quad's own edges) — a
// low-poly reference overlay has no need for per-vertex smoothing across
// quads, and flat shading makes the "swept plane" reading more legible.
// Non-indexed triangle list (6 vertices/quad) — simplicity over the modest
// vertex-count saving an index buffer would give (typically a few hundred
// quads for 3 segments x samplesPerSegment).
class RuledSurfaceGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RuledSurfaceGeometry)

    Q_PROPERTY(QVariantList poses READ poses WRITE setPoses NOTIFY posesChanged)

public:
    explicit RuledSurfaceGeometry(QQuick3DObject *parent = nullptr);

    QVariantList poses() const { return m_poses; }
    void setPoses(const QVariantList &poses);

signals:
    void posesChanged();

private:
    void rebuild();

    QVariantList m_poses;
};
