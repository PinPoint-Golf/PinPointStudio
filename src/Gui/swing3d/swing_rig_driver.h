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

// SwingRigDriver — the 3-D swing panel's C++ half (docs/design/swing_3d_viz_design.md §6).
//
// Reads ONE swing's `analysis.skeleton3d` (pinpoint.skeleton3d/1) in C++ — off the GUI thread,
// because the document is tens of megabytes — and answers, for the playhead instant, the rig's
// bone rotations, the root's placement, the club and the ball, in the Qt Quick 3D scene frame.
// QML never sees the track: no QVariantMap crosses the boundary (every QML read of one is a deep
// copy), only a handful of small values per bone.
//
// Scene frame: the golfer's. Origin = the ball at address on the floor (else the mid-heels),
// +X along the stance axis toward the lead heel, +Y up, +Z toward the face-on camera's side of
// the golfer... precisely: world (X face-on right, Y face-on view ray, Z up) is turned about Z
// by −stanceYaw, then mapped (x, y, z) → (x, z, −y), a proper rotation (−90° about X).
//
// Bindings: every per-bone read takes `revision` as an argument — `driver.localRotation(j,
// driver.revision)` — so the binding re-evaluates on each playhead step. (A bare `revision`
// statement in a binding is dropped by the QML compiler.)
//
// Shares NOTHING with the calibration views (BodyVizView / BodyPoseAdapter).

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QQuaternion>
#include <QString>
#include <QVector3D>

#include <array>
#include <memory>
#include <vector>

namespace pinpoint::skeleton3d { struct Skeleton3DTrack; }

// The motion annotations' view of one swing (docs/design/swing_3d_annotations_design.md §4.3):
// the few points the camera tiles' overlays are drawn through, per fitted frame, in the SCENE
// frame — from the same forward kinematics as the drawn figure, so an annotation can never drift
// off it. Plain C++ data for SwingAnnotationGeometry; never handed to QML.
struct SwingAnnotTrack {
    // Joint CENTRES (the fitted surface-marker offsets are not persisted — design §2).
    enum Point : int {
        LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist, LHip, RHip, LKnee, RKnee,
        LAnkle, RAnkle, Head, Grip, ClubButt, ClubHead, PointCount,
        NeckMid = PointCount, PelvisMid          // derived: the shoulder / hip midpoints
    };
    std::vector<qint64> t;                                   // window-relative µs
    std::vector<std::array<QVector3D, PointCount>> p;
    std::vector<std::array<quint8, PointCount>> tier;        // 0 absent … 3 measured (club: shaftTier)
    qint64 addressUs = -1, impactUs = -1, finishUs = -1;
    // The fused downswing plane (club3d.planes.down) as a quad through the clubhead at impact.
    bool planeValid = false;
    std::array<QVector3D, 4> planeQuad {};
    double planeInclDeg = 0;

    QVector3D at(size_t frame, int point) const;             // derived points included
    quint8 tierAt(size_t frame, int point) const;
    QVector3D sample(qint64 tUs, int point) const;           // interpolated (clamped to the track)
    bool inWindow(qint64 tUs) const;                         // Address → Finish, as the tiles draw
};

class SwingRigDriver : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString swingDir   READ swingDir   WRITE setSwingDir   NOTIFY swingDirChanged)
    Q_PROPERTY(qint64  positionUs READ positionUs WRITE setPositionUs NOTIFY revisionChanged)
    Q_PROPERTY(bool    loading    READ loading    NOTIFY loadedChanged)
    Q_PROPERTY(bool    available  READ available  NOTIFY loadedChanged)
    Q_PROPERTY(QString reason     READ reason     NOTIFY loadedChanged)
    Q_PROPERTY(bool    twoViews   READ twoViews   NOTIFY loadedChanged)
    Q_PROPERTY(int     jointCount READ jointCount CONSTANT)
    Q_PROPERTY(double  clubLengthM READ clubLengthM NOTIFY loadedChanged)
    Q_PROPERTY(qint64  startUs    READ startUs    NOTIFY loadedChanged)
    Q_PROPERTY(qint64  endUs      READ endUs      NOTIFY loadedChanged)
    Q_PROPERTY(int     revision   READ revision   NOTIFY revisionChanged)
    // Per frame, updated with revision.
    Q_PROPERTY(QVector3D   rootPosition READ rootPosition NOTIFY revisionChanged)
    Q_PROPERTY(QQuaternion rootRotation READ rootRotation NOTIFY revisionChanged)
    Q_PROPERTY(int         frameTier    READ frameTier    NOTIFY revisionChanged)
    Q_PROPERTY(QString     frameTierText READ frameTierText NOTIFY revisionChanged)
    Q_PROPERTY(QVector3D   shaftButt    READ shaftButt    NOTIFY revisionChanged)
    Q_PROPERTY(QQuaternion shaftRotation READ shaftRotation NOTIFY revisionChanged)
    Q_PROPERTY(int         shaftTier    READ shaftTier    NOTIFY revisionChanged)
    Q_PROPERTY(QVector3D   ballPosition READ ballPosition NOTIFY loadedChanged)
    Q_PROPERTY(bool        ballVisible  READ ballVisible  NOTIFY revisionChanged)
    Q_PROPERTY(QVector3D   centre       READ centre       NOTIFY loadedChanged)   // orbit pivot: mid-hip at address
    // Motion annotations (docs/design/swing_3d_annotations_design.md).
    Q_PROPERTY(bool    inSwingWindow READ inSwingWindow NOTIFY revisionChanged)    // playing, Address → Finish
    Q_PROPERTY(int     positionCount READ positionCount NOTIFY loadedChanged)      // P1–P8 on the face-on track
    Q_PROPERTY(bool    planeAvailable READ planeAvailable NOTIFY loadedChanged)
    Q_PROPERTY(double  planeInclDeg  READ planeInclDeg  NOTIFY loadedChanged)
    Q_PROPERTY(bool    foMirrored    READ foMirrored    NOTIFY loadedChanged)
    Q_PROPERTY(double  clubSmoothingMs READ clubSmoothingMs CONSTANT)   // the club's display stabiliser

public:
    explicit SwingRigDriver(QObject *parent = nullptr);
    ~SwingRigDriver() override;

    QString swingDir() const { return m_swingDir; }
    void    setSwingDir(const QString &dir);
    qint64  positionUs() const { return m_positionUs; }
    void    setPositionUs(qint64 us);
    bool    loading() const { return m_loading; }
    bool    available() const;
    QString reason() const { return m_reason; }
    bool    twoViews() const;
    int     jointCount() const;
    double  clubLengthM() const;
    qint64  startUs() const;
    qint64  endUs() const;
    int     revision() const { return m_revision; }

    QVector3D   rootPosition() const { return m_rootPos; }
    QQuaternion rootRotation() const { return m_local[0]; }
    int         frameTier() const { return m_frameTier; }
    QString     frameTierText() const;
    QVector3D   shaftButt() const { return m_shaftButt; }
    QQuaternion shaftRotation() const { return m_shaftRot; }
    int         shaftTier() const { return m_shaftTier; }
    QVector3D   ballPosition() const { return m_ball; }
    bool        ballVisible() const { return m_ballVisible; }
    QVector3D   centre() const { return m_centre; }

    // The ybot joint index for a name ("LeftForeArm"), −1 if unknown.
    Q_INVOKABLE int jointIndex(const QString &name) const;
    // Parent-local rotation of joint j at the playhead (the root's is in the scene frame).
    Q_INVOKABLE QQuaternion localRotation(int joint, int revision) const;
    // Joint j's offset from its parent, scaled to the golfer (constant per swing).
    Q_INVOKABLE QVector3D offset(int joint, int revision) const;
    // Uniform scale for the mesh that joint j carries (its bone's group scale).
    Q_INVOKABLE double meshScale(int joint, int revision) const;
    // 0 absent, 1 inferred, 2 constrained, 3 measured.
    Q_INVOKABLE int tier(int joint, int revision) const;

    // Synchronous load for tests and probes (the property path is asynchronous).
    Q_INVOKABLE bool loadNow(const QString &dir);

    // ── motion annotations ──
    bool inSwingWindow() const;
    int  positionCount() const;
    bool planeAvailable() const;
    double planeInclDeg() const;
    bool foMirrored() const;
    double clubSmoothingMs() const;
    // P-positions, in time order: the number (1–8), how it was found (1 = MilestoneFit), its
    // window-relative time, and the fitted club's butt and head at that instant.
    Q_INVOKABLE int       positionP(int i) const;
    Q_INVOKABLE int       positionSource(int i) const;
    Q_INVOKABLE qint64    positionTimeUs(int i) const;
    Q_INVOKABLE QVector3D positionHead(int i, int revision) const;
    Q_INVOKABLE QVector3D positionButt(int i, int revision) const;
    // The club at that instant as a rotation of +Y onto butt → head (for a #Cylinder).
    Q_INVOKABLE QQuaternion positionRotation(int i, int revision) const;
    // The fitted cameras (view 0 face-on, 1 DTL) as Qt Quick 3D camera poses in the scene frame,
    // and the vertical field of view from the focal length and the encoded image height.
    Q_INVOKABLE bool        cameraAvailable(int view) const;
    Q_INVOKABLE QVector3D   cameraPosition(int view) const;
    Q_INVOKABLE QQuaternion cameraRotation(int view) const;
    Q_INVOKABLE double      cameraFovDeg(int view) const;
    // Project a scene point through a fitted camera, in pixels of its image (tests; −1 behind).
    Q_INVOKABLE QPointF     projectScene(int view, const QVector3D &p) const;

    // C++ only (SwingAnnotationGeometry): the annotation track, null when unavailable, and the
    // playhead the annotations are drawn at (−1 when nothing is playing).
    const SwingAnnotTrack *annotTrack() const;
    qint64 annotTimeUs() const;

signals:
    void swingDirChanged();
    void loadedChanged();
    void revisionChanged();

private:
    struct Prepared;                                  // the track, pre-baked per frame
    void startLoad();
    void adopt(std::shared_ptr<const Prepared> p, quint64 generation);
    void evaluate();
    static std::shared_ptr<const Prepared> prepare(const QString &dir, QString *reason);

    QString m_swingDir;
    qint64  m_positionUs = -1;     // < 0 = nothing playing: rest at address
    bool    m_loading = false;
    QString m_reason;
    int     m_revision = 0;
    quint64 m_generation = 0;
    std::shared_ptr<const Prepared> m_track;

    std::vector<QQuaternion> m_local;
    std::vector<int>         m_tier;
    QVector3D   m_rootPos;
    int         m_frameTier = 0;
    QVector3D   m_shaftButt;
    QQuaternion m_shaftRot;
    int         m_shaftTier = 0;
    QVector3D   m_ball;
    bool        m_ballVisible = false;
    QVector3D   m_centre;
};
