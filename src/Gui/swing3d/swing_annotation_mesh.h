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

// The 3-D swing panel's motion annotations as triangle meshes
// (docs/design/swing_3d_annotations_design.md §4). Pure arithmetic over a SwingAnnotTrack — no
// Quick 3D — so the tests build it without a scene. SwingAnnotationGeometry uploads the result.
//
// What each mode draws mirrors the camera tiles (PpCameraFrame.qml), in 3-D:
//   trace  a tube through the element's anchor from Address to the playhead (the last piece is
//          interpolated to the playhead exactly); an absent frame breaks it; an inferred one is
//          drawn at 40 %.
//   frame  the element's bones at the playhead as thin tubes; the club's frame is the tiles'
//          head trail (the club itself is the panel's club model).
//   fan    the element's bones at past frames over the last 240 ms (≤ 24, alpha 0.08 → 0.55,
//          × 0.7), then the current frame full. Arms fans the lead arm only.
//   plane  the fused downswing plane's quad (element "plane"); the material draws both sides.
// Nothing is drawn outside Address → Finish, or when nothing is playing (t < 0) — except the
// plane, which is a property of the swing, not of an instant.

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QVector3D>

#include <vector>

#include "swing_rig_driver.h"

namespace swing3d {

// The tiles' fan constants (PpCameraFrame.qml: fanWindowMs, kFanMaxFrames, the 0.08 → 0.55 ramp).
constexpr qint64 kFanWindowUs = 240000;
constexpr int    kFanMaxFrames = 24;
constexpr float  kFanAlphaLo = 0.08f, kFanAlphaHi = 0.55f, kFanAlphaScale = 0.7f;
constexpr int    kHeadTrail = 10;          // the club's frame-mode head trail (kTrail)
constexpr float  kInferredAlpha = 0.4f;
constexpr int    kTubeSides = 6;

struct AnnotSpec {
    QString element;          // arms | spine | shoulders | hips | legs | shaft | shaftGrip | plane
    QString mode;             // off | frame | fan | trace
    QString target;           // the tiles' trace-target override ("head", "leadWrist", …) or ""
    bool    leadLeft = true;
    float   radius = 0.007f;  // metres
    QColor  color;            // the element's colour
    QColor  colorB;           // the spine's lower end (its gradient); invalid or transparent ⇒ color
};

// Interleaved vertices: position (3 f32) · normal (3 f32) · colour (4 f32) = 40 bytes; U32 indices.
// The normals make the tubes LIT tubes — a trace reads as a 3-D object, not a flat line.
struct AnnotMesh {
    QByteArray vertices, indices;
    int vertexCount = 0, indexCount = 0;
    QVector3D lo, hi;
    // Test hooks: how many pieces the trace was broken into, and the trace's end point.
    int pieces = 0;
    QVector3D traceEnd;
    int fanFrames = 0;
    float fanAlphaMin = 0, fanAlphaMax = 0;
};
constexpr int kAnnotStride = 40;
constexpr int kAnnotFloats = kAnnotStride / 4;

// The anchor point a trace follows, as the tiles' _traceAnchor() picks it: `target` overrides
// a body element's default; the club's two ends are their own.
int traceAnchor(const QString &element, const QString &target, bool leadLeft);

AnnotMesh buildAnnotationMesh(const SwingAnnotTrack &T, const AnnotSpec &spec, qint64 tUs);

// The club's display stabiliser (annotations design §10): a zero-phase local-quadratic smoother
// with Gaussian weights of width `sigmaUs`, per axis. `weight[i]` = 0 leaves a sample out of every
// fit (an unseen club) — it is still smoothed from its neighbours. Pure, for the tests.
std::vector<QVector3D> smoothPath(const std::vector<qint64> &t, const std::vector<QVector3D> &x,
                                  const std::vector<float> &weight, double sigmaUs);

} // namespace swing3d
