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

#include "swing_rig_driver.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

#include "swing_annotation_mesh.h"
#include "../../Analysis/skeleton3d/skeleton3d_json.h"
#include "../../Analysis/skeleton3d/skeleton3d_rig.h"
#include "../../Export/swing_store.h"

namespace sk = pinpoint::skeleton3d;

namespace {

constexpr int kPhaseAddress = 0;  // pinpoint::analysis::Phase::Address
constexpr int kPhaseImpact = 5;   // pinpoint::analysis::Phase::Impact
constexpr int kPhaseFinish = 7;   // pinpoint::analysis::Phase::Finish

// The fused downswing plane's quad (annotations design §5b): from the clubhead at impact, this far
// up the shaft and this far either side of it, in metres.
constexpr float kPlaneUpM = 1.8f;
constexpr float kPlaneSideM = 1.2f;

// The club's DISPLAY stabiliser (annotations design §10): the fitted clubhead path smoothed over
// this Gaussian width (zero-phase, local quadratic), the club re-pointed from the fitted hands to it.
// 20 ms removes a third of the head's frame-to-frame jitter at no cost against either camera's
// tracker (24-swing grade); 30 ms and wider start to pull it off the down-the-line video.
constexpr double kClubSmoothUs = 20000.0;

QQuaternion toQt(const sk::Q &q) { return QQuaternion(float(q.w), float(q.x), float(q.y), float(q.z)); }

// World (the fit's) → the scene: about Z by −stanceYaw, then (x, y, z) → (x, z, −y).
struct SceneFrame {
    sk::Q rot;
    sk::V3 origin;
    QVector3D point(const sk::V3 &p) const
    {
        const sk::V3 v = rot.rotate(p - origin);
        return { float(v.x), float(v.y), float(v.z) };
    }
    QVector3D dir(const sk::V3 &d) const
    {
        const sk::V3 v = rot.rotate(d);
        return { float(v.x), float(v.y), float(v.z) };
    }
};

// One fitted camera, in the scene frame (camFrame() in skeleton3d_fit.cpp, mapped through the
// scene transform): the Qt camera pose for the match presets, and the pinhole to project with.
struct SceneCamera {
    bool ok = false;
    QVector3D pos, right, down, fwd;
    double f = 0, W = 0, H = 0;
};

// A camera from the persisted `skeleton3d.camera` block — the fit's own model, mirrored here so
// the GUI does not link the solver. World frame: X face-on image-right, Y face-on view ray, Z up.
SceneCamera sceneCamera(const QJsonObject &c, int view, double W, double H, const SceneFrame &S)
{
    SceneCamera out;
    const double f = c.value(view == 0 ? QStringLiteral("fF") : QStringLiteral("fD")).toDouble();
    if (!(f > 0) || !(W > 0) || !(H > 0)) return out;
    sk::V3 pos, fwd, up, right;
    if (view == 0) {
        const double p = c.value(QStringLiteral("pFDeg")).toDouble() * sk::kDeg;
        pos = {};
        fwd = { 0, std::cos(p), -std::sin(p) };
        up = { 0, std::sin(p), std::cos(p) };
        right = { 1, 0, 0 };
    } else {
        const QJsonArray cd = c.value(QStringLiteral("cD")).toArray();
        if (cd.size() != 3) return out;
        const double psi = c.value(QStringLiteral("psiDDeg")).toDouble() * sk::kDeg;
        const double p = c.value(QStringLiteral("pDDeg")).toDouble() * sk::kDeg;
        const sk::V3 f0 { std::cos(psi), std::sin(psi), 0 };
        const sk::V3 Z { 0, 0, 1 };
        pos = { cd[0].toDouble(), cd[1].toDouble(), cd[2].toDouble() };
        fwd = f0 * std::cos(p) - Z * std::sin(p);
        up = f0 * std::sin(p) + Z * std::cos(p);
        right = f0.cross(Z);
    }
    sk::V3 down = -up;
    const double r = c.value(view == 0 ? QStringLiteral("rFDeg") : QStringLiteral("rDDeg")).toDouble() * sk::kDeg;
    if (r != 0.0) {
        const sk::V3 R0 = right, D0 = down;
        right = R0 * std::cos(r) + D0 * std::sin(r);
        down = D0 * std::cos(r) - R0 * std::sin(r);
    }
    out.ok = true;
    out.pos = S.point(pos);
    out.right = S.dir(right);
    out.down = S.dir(down);
    out.fwd = S.dir(fwd);
    out.f = f; out.W = W; out.H = H;
    return out;
}

// Encoded frame sizes by view, from the document's streams (the same lookup the corpus grader
// makes: an alias naming the face-on or the down-the-line camera). Face-on falls back to the
// club track's frame size.
void streamHeights(const QJsonObject &root, const QJsonObject &an, double fo[2], double dtl[2])
{
    fo[0] = fo[1] = dtl[0] = dtl[1] = 0;
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject el = v.toObject();
        const QString alias = el.value(QStringLiteral("alias")).toString().toLower();
        const QJsonObject enc = el.value(QStringLiteral("encoded")).toObject();
        const double w = enc.value(QStringLiteral("width")).toDouble(), h = enc.value(QStringLiteral("height")).toDouble();
        if (!(w > 0) || !(h > 0)) continue;
        if (alias.contains(QLatin1String("face")) && !(fo[0] > 0)) { fo[0] = w; fo[1] = h; }
        else if ((alias.contains(QLatin1String("dtl")) || alias.contains(QLatin1String("down"))) && !(dtl[0] > 0)) { dtl[0] = w; dtl[1] = h; }
    }
    if (!(fo[0] > 0)) {
        const QJsonObject club = an.value(QStringLiteral("club")).toObject();
        fo[0] = club.value(QStringLiteral("frameWidth")).toDouble();
        fo[1] = club.value(QStringLiteral("frameHeight")).toDouble();
    }
}

} // namespace

// ── SwingAnnotTrack ──────────────────────────────────────────────────────────
QVector3D SwingAnnotTrack::at(size_t frame, int point) const
{
    const auto &q = p[frame];
    if (point == NeckMid) return (q[LShoulder] + q[RShoulder]) * 0.5f;
    if (point == PelvisMid) return (q[LHip] + q[RHip]) * 0.5f;
    return q[size_t(point)];
}

quint8 SwingAnnotTrack::tierAt(size_t frame, int point) const
{
    const auto &q = tier[frame];
    if (point == NeckMid) return std::min(q[LShoulder], q[RShoulder]);
    if (point == PelvisMid) return std::min(q[LHip], q[RHip]);
    return q[size_t(point)];
}

QVector3D SwingAnnotTrack::sample(qint64 tUs, int point) const
{
    if (t.empty()) return {};
    const qint64 c = std::clamp(tUs, t.front(), t.back());
    auto hi = std::upper_bound(t.begin(), t.end(), c);
    const size_t b = hi == t.end() ? t.size() - 1 : size_t(hi - t.begin());
    const size_t a = b > 0 ? b - 1 : 0;
    if (a == b || t[b] <= t[a]) return at(b, point);
    const float w = std::clamp(float(double(c - t[a]) / double(t[b] - t[a])), 0.f, 1.f);
    return at(a, point) * (1.f - w) + at(b, point) * w;
}

bool SwingAnnotTrack::inWindow(qint64 tUs) const
{
    if (addressUs < 0 || finishUs < 0) return true;     // the tiles' rule: no window ⇒ always
    return tUs >= addressUs && tUs <= finishUs;
}

struct SwingRigDriver::Prepared {
    bool dtl = false;
    double clubLengthM = 0.95;
    std::vector<qint64> t;
    std::vector<std::array<QQuaternion, sk::Rig::N>> local;   // root: scene frame
    std::vector<QVector3D> rootPos;
    std::vector<std::array<uint8_t, sk::Rig::N>> tier;
    std::vector<QVector3D> shaftButt;
    std::vector<QQuaternion> shaftRot;
    std::vector<int> shaftTier;
    std::array<QVector3D, sk::Rig::N> offset {};
    std::array<double, sk::Rig::N> meshScale {};
    QVector3D ball;
    bool ballValid = false;
    qint64 impactUs = -1;
    qint64 addressUs = -1;     // where the figure rests when nothing is playing
    QVector3D centre;
    // Motion annotations.
    SwingAnnotTrack annot;
    struct Position { int p = 0; int source = 0; qint64 t = 0; };
    std::vector<Position> positions;
    std::array<SceneCamera, 2> cams {};
    bool foMirrored = false;
};

SwingRigDriver::SwingRigDriver(QObject *parent)
    : QObject(parent), m_local(size_t(sk::Rig::N)), m_tier(size_t(sk::Rig::N), 0)
{
}

SwingRigDriver::~SwingRigDriver() = default;

void SwingRigDriver::setSwingDir(const QString &dir)
{
    if (dir == m_swingDir) return;
    m_swingDir = dir;
    emit swingDirChanged();
    startLoad();
}

void SwingRigDriver::setPositionUs(qint64 us)
{
    if (us == m_positionUs) return;
    m_positionUs = us;
    evaluate();
}

bool SwingRigDriver::available() const { return m_track && !m_track->t.empty(); }
bool SwingRigDriver::twoViews() const { return m_track && m_track->dtl; }
int SwingRigDriver::jointCount() const { return sk::Rig::N; }
double SwingRigDriver::clubLengthM() const { return m_track ? m_track->clubLengthM : 0.95; }
qint64 SwingRigDriver::startUs() const { return available() ? m_track->t.front() : 0; }
qint64 SwingRigDriver::endUs() const { return available() ? m_track->t.back() : 0; }

QString SwingRigDriver::frameTierText() const
{
    if (!available()) return m_reason;
    switch (m_frameTier) {
    case sk::TierMeasured:    return m_track->dtl ? tr("seen by both cameras") : tr("measured");
    case sk::TierConstrained: return tr("constrained by the anatomy");
    case sk::TierInferred:    return tr("inferred");
    default:                  return tr("partly unseen");
    }
}

int SwingRigDriver::jointIndex(const QString &name) const
{
    for (int j = 0; j < sk::Rig::N; ++j)
        if (name == QLatin1String(sk::ybot::kJoints[j].name)) return j;
    return -1;
}

QQuaternion SwingRigDriver::localRotation(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N) return {};
    if (!available()) {
        const auto &q = sk::ybot::kJoints[joint].restQ;
        return QQuaternion(float(q[0]), float(q[1]), float(q[2]), float(q[3]));
    }
    return m_local[size_t(joint)];
}

QVector3D SwingRigDriver::offset(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N) return {};
    if (!available()) {
        const auto &t = sk::ybot::kJoints[joint].restT;
        return { float(t[0]), float(t[1]), float(t[2]) };
    }
    return m_track->offset[size_t(joint)];
}

double SwingRigDriver::meshScale(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N || !available()) return 1.0;
    return m_track->meshScale[size_t(joint)];
}

int SwingRigDriver::tier(int joint, int) const
{
    if (joint < 0 || joint >= sk::Rig::N || !available()) return 0;
    return m_tier[size_t(joint)];
}

bool SwingRigDriver::loadNow(const QString &dir)
{
    m_swingDir = dir;
    ++m_generation;
    QString why;
    auto p = prepare(dir, &why);
    m_reason = why;
    adopt(p, m_generation);
    return available();
}

void SwingRigDriver::startLoad()
{
    const quint64 gen = ++m_generation;
    m_track.reset();
    m_loading = !m_swingDir.isEmpty();
    m_reason = m_swingDir.isEmpty() ? tr("no swing selected") : tr("loading…");
    emit loadedChanged();
    ++m_revision;
    emit revisionChanged();
    if (m_swingDir.isEmpty()) return;
    // The document is tens of megabytes: parse it off the GUI thread and adopt the result there.
    QPointer<SwingRigDriver> self(this);
    const QString dir = m_swingDir;
    QThreadPool::globalInstance()->start([self, dir, gen]() {
        QString why;
        auto p = prepare(dir, &why);
        QMetaObject::invokeMethod(self, [self, p, gen, why]() {
            if (!self) return;
            self->m_reason = why;
            self->adopt(p, gen);
        }, Qt::QueuedConnection);
    });
}

void SwingRigDriver::adopt(std::shared_ptr<const Prepared> p, quint64 generation)
{
    if (generation != m_generation) return;    // a newer swing was asked for meanwhile
    m_track = std::move(p);
    m_loading = false;
    if (m_track) {
        m_ball = m_track->ball;
        m_centre = m_track->centre;
    }
    emit loadedChanged();
    evaluate();
}

std::shared_ptr<const SwingRigDriver::Prepared> SwingRigDriver::prepare(const QString &dir, QString *reason)
{
    QString err;
    const QJsonObject root = pinpoint::SwingStore::load(dir, &err);
    if (root.isEmpty()) {
        *reason = err.isEmpty() ? QObject::tr("no swing document") : err;
        return {};
    }
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    const qint64 t0 = qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
    const sk::Skeleton3DTrack trk = sk::skeleton3dFromJson(an.value(QStringLiteral("skeleton3d")).toObject(), t0);
    if (!trk.valid) {
        *reason = trk.reason.isEmpty() ? QObject::tr("no 3-D skeleton on this shot") : trk.reason;
        return {};
    }
    auto P = std::make_shared<Prepared>();
    P->dtl = trk.dtl;
    P->clubLengthM = trk.clubLengthM > 0.5 ? trk.clubLengthM : 0.95;

    const sk::Rig &R = sk::rig();
    SceneFrame S;
    S.rot = (sk::Q::axisAngle({ 1, 0, 0 }, -sk::kPi / 2.0)
             * sk::Q::axisAngle({ 0, 0, 1 }, -trk.stanceYawDeg * sk::kDeg)).normalized();
    S.origin = trk.displayOrigin;

    for (int j = 0; j < sk::Rig::N; ++j) {
        const int g = sk::jointGroup(j);
        const double s = g >= 0 ? trk.lengths[size_t(g)] : trk.lengths[sk::GSpine];
        const sk::V3 o = R.restT[j] * s;
        P->offset[size_t(j)] = { float(o.x), float(o.y), float(o.z) };
        P->meshScale[size_t(j)] = s;
    }

    // Impact, window-relative, for the ball.
    for (const QJsonValue &pv : an.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject po = pv.toObject();
        const int phase = po.value(QStringLiteral("phase")).toInt();
        if (phase != kPhaseImpact && phase != kPhaseAddress && phase != kPhaseFinish) continue;
        const qint64 raw = qint64(po.value(QStringLiteral("t_us")).toDouble());
        const qint64 rel = raw >= t0 ? raw - t0 : raw;
        if (phase == kPhaseImpact) P->impactUs = rel;
        else if (phase == kPhaseAddress) P->addressUs = rel;
        else P->annot.finishUs = rel;
    }
    P->annot.addressUs = P->addressUs;
    P->annot.impactUs = P->impactUs;
    // P1–P8 from the face-on club track (already window-relative; the ≥ t0 rule is idempotent).
    for (const QJsonValue &pv : an.value(QStringLiteral("club")).toObject().value(QStringLiteral("positions")).toArray()) {
        const QJsonObject po = pv.toObject();
        const qint64 raw = qint64(po.value(QStringLiteral("t_us")).toDouble(-1));
        if (raw < 0) continue;
        P->positions.push_back({ po.value(QStringLiteral("p")).toInt(), po.value(QStringLiteral("source")).toInt(),
                                 raw >= t0 ? raw - t0 : raw });
    }
    std::sort(P->positions.begin(), P->positions.end(),
              [](const Prepared::Position &a, const Prepared::Position &b) { return a.t < b.t; });
    if (trk.ballValid) {
        P->ball = S.point(trk.ball);
        P->ballValid = true;
    }

    const std::array<double, sk::GroupCount> &scale = trk.lengths;
    using AP = SwingAnnotTrack;
    using namespace sk::ybot;
    static constexpr std::array<int, 13> kAnnotJoint {
        LeftArm, RightArm, LeftForeArm, RightForeArm, LeftHand, RightHand, LeftUpLeg, RightUpLeg,
        LeftLeg, RightLeg, LeftFoot, RightFoot, Head };
    sk::Pose pose;
    for (const sk::Skeleton3DFrame &f : trk.frames) {
        if (int(f.dof.size()) != R.dofCount()) continue;
        sk::forwardKinematics(R, f.dof.data(), scale.data(), pose);
        std::array<QQuaternion, sk::Rig::N> loc;
        for (int j = 0; j < sk::Rig::N; ++j) loc[size_t(j)] = toQt(pose.local[j]);
        loc[0] = toQt((S.rot * pose.local[0]).normalized());
        P->t.push_back(f.t_us);
        P->local.push_back(loc);
        P->rootPos.push_back(S.point(pose.pos[0]));
        P->tier.push_back(f.tier);
        const sk::V3 butt = f.grip - f.shaftDir * 0.04;
        P->shaftButt.push_back(S.point(butt));
        P->shaftRot.push_back(QQuaternion::rotationTo(QVector3D(0, 1, 0), S.dir(f.shaftDir).normalized()));
        P->shaftTier.push_back(f.shaftTier);
        std::array<QVector3D, AP::PointCount> ap;
        std::array<quint8, AP::PointCount> at;
        for (size_t k = 0; k < kAnnotJoint.size(); ++k) {
            ap[k] = S.point(pose.pos[kAnnotJoint[k]]);
            at[k] = f.tier[size_t(kAnnotJoint[k])];
        }
        const sk::V3 head = butt + f.shaftDir * P->clubLengthM;     // the tip of the drawn club
        ap[AP::Grip] = S.point(f.grip);
        ap[AP::ClubButt] = S.point(butt);
        ap[AP::ClubHead] = S.point(head);
        at[AP::Grip] = at[AP::ClubButt] = at[AP::ClubHead] = quint8(std::clamp(f.shaftTier, 0, 3));
        P->annot.t.push_back(f.t_us);
        P->annot.p.push_back(ap);
        P->annot.tier.push_back(at);
        if (P->t.size() == 1)
            P->centre = S.point((pose.pos[sk::ybot::LeftUpLeg] + pose.pos[sk::ybot::RightUpLeg]) * 0.5);
    }
    if (P->t.size() < 2) {
        *reason = QObject::tr("the 3-D skeleton has fewer than two frames");
        return {};
    }

    // Stabilise the club for display: smooth the clubhead path, keep the butt in the fitted hands,
    // and point the drawn club at the smoothed head — the club model, its trace and the P-positions
    // stay one object. The length is the fitted one: stabilising must not hide that it is short.
    {
        const size_t n = P->annot.t.size();
        std::vector<QVector3D> heads(n);
        std::vector<float> w(n);
        for (size_t i = 0; i < n; ++i) {
            heads[i] = P->annot.p[i][AP::ClubHead];
            w[i] = P->annot.tier[i][AP::ClubHead] > 0 ? 1.f : 0.f;
        }
        const std::vector<QVector3D> sm = swing3d::smoothPath(P->annot.t, heads, w, kClubSmoothUs);
        for (size_t i = 0; i < n; ++i) {
            const QVector3D butt = P->annot.p[i][AP::ClubButt];
            const QVector3D d = sm[i] - butt;
            if (d.lengthSquared() < 1e-6f) continue;
            const QVector3D dir = d.normalized();
            P->annot.p[i][AP::ClubHead] = butt + dir * float(P->clubLengthM);
            P->shaftRot[i] = QQuaternion::rotationTo(QVector3D(0, 1, 0), dir);
        }
    }

    // The fitted cameras, for the match presets (annotations design §5a).
    const QJsonObject sk3 = an.value(QStringLiteral("skeleton3d")).toObject();
    const QJsonObject camJ = sk3.value(QStringLiteral("camera")).toObject();
    P->foMirrored = sk3.value(QStringLiteral("foMirrored")).toBool();
    double foWH[2], dtlWH[2];
    streamHeights(root, an, foWH, dtlWH);
    P->cams[0] = sceneCamera(camJ, 0, foWH[0], foWH[1], S);
    if (trk.dtl) P->cams[1] = sceneCamera(camJ, 1, dtlWH[0], dtlWH[1], S);

    // The fused downswing plane (annotations design §5b). club3d's frame is the face-on CAMERA's
    // (X image-right, Y view ray, Z up — the camera assumed level); the fit pitched and rolled that
    // camera, so the normal is carried through the fitted face-on axes into the world first.
    const QJsonObject down = an.value(QStringLiteral("club3d")).toObject().value(QStringLiteral("planes"))
                                 .toObject().value(QStringLiteral("down")).toObject();
    const QJsonArray nj = down.value(QStringLiteral("normal")).toArray();
    if (down.value(QStringLiteral("offered")).toBool() && nj.size() == 3 && P->impactUs >= 0) {
        const double pF = camJ.value(QStringLiteral("pFDeg")).toDouble() * sk::kDeg;
        const double rF = camJ.value(QStringLiteral("rFDeg")).toDouble() * sk::kDeg;
        sk::V3 right { 1, 0, 0 }, fwd { 0, std::cos(pF), -std::sin(pF) }, up { 0, std::sin(pF), std::cos(pF) };
        if (rF != 0.0) {
            const sk::V3 R0 = right, D0 = -up;
            right = R0 * std::cos(rF) + D0 * std::sin(rF);
            up = -(D0 * std::cos(rF) - R0 * std::sin(rF));
        }
        const sk::V3 nw = right * nj[0].toDouble() + fwd * nj[1].toDouble() + up * nj[2].toDouble();
        const QVector3D n = S.dir(nw).normalized();
        const QVector3D H = P->annot.sample(P->impactUs, AP::ClubHead);
        const QVector3D B = P->annot.sample(P->impactUs, AP::ClubButt);
        const QVector3D s = (B - H).normalized();
        const QVector3D e1 = (s - n * QVector3D::dotProduct(n, s)).normalized();
        const QVector3D e2 = QVector3D::crossProduct(n, e1).normalized();
        if (n.lengthSquared() > 0.5f && e1.lengthSquared() > 0.5f) {
            P->annot.planeValid = true;
            P->annot.planeInclDeg = down.value(QStringLiteral("inclDeg")).toDouble();
            P->annot.planeQuad = { H - e2 * kPlaneSideM, H + e2 * kPlaneSideM,
                                   H + e1 * kPlaneUpM + e2 * kPlaneSideM, H + e1 * kPlaneUpM - e2 * kPlaneSideM };
        }
    }
    reason->clear();
    return P;
}

void SwingRigDriver::evaluate()
{
    if (available()) {
        const Prepared &P = *m_track;
        // A negative position is "nothing is playing": rest at address, not on the fit's first
        // frame (the lead-in before address is the least-held part of any fit).
        const qint64 want = m_positionUs < 0 ? (P.addressUs >= 0 ? P.addressUs : P.t.front()) : m_positionUs;
        const qint64 t = std::clamp(want, P.t.front(), P.t.back());
        auto hi = std::upper_bound(P.t.begin(), P.t.end(), t);
        size_t b = hi == P.t.end() ? P.t.size() - 1 : size_t(hi - P.t.begin());
        size_t a = b > 0 ? b - 1 : 0;
        float w = 0.f;
        if (b != a && P.t[b] > P.t[a]) w = float(double(t - P.t[a]) / double(P.t[b] - P.t[a]));
        w = std::clamp(w, 0.f, 1.f);
        const size_t near = w < 0.5f ? a : b;
        for (int j = 0; j < sk::Rig::N; ++j) {
            m_local[size_t(j)] = QQuaternion::slerp(P.local[a][size_t(j)], P.local[b][size_t(j)], w);
            m_tier[size_t(j)] = P.tier[near][size_t(j)];
        }
        m_rootPos = P.rootPos[a] * (1.f - w) + P.rootPos[b] * w;
        m_shaftButt = P.shaftButt[a] * (1.f - w) + P.shaftButt[b] * w;
        m_shaftRot = QQuaternion::slerp(P.shaftRot[a], P.shaftRot[b], w);
        m_shaftTier = P.shaftTier[near];
        m_ballVisible = P.ballValid && (P.impactUs < 0 || t <= P.impactUs + 5000);
        // The frame's tier: the weakest of the major joints (an absent one makes it "partly unseen").
        using namespace sk::ybot;
        int ft = sk::TierMeasured;
        for (int j : { Hips, Spine2, Head, LeftArm, RightArm, LeftForeArm, RightForeArm, LeftHand, RightHand,
                       LeftLeg, RightLeg, LeftFoot, RightFoot })
            ft = std::min(ft, m_tier[size_t(j)]);
        m_frameTier = ft;
    }
    ++m_revision;
    emit revisionChanged();
}

// ── motion annotations ───────────────────────────────────────────────────────
double SwingRigDriver::clubSmoothingMs() const { return kClubSmoothUs / 1000.0; }

const SwingAnnotTrack *SwingRigDriver::annotTrack() const { return available() ? &m_track->annot : nullptr; }

qint64 SwingRigDriver::annotTimeUs() const
{
    if (!available() || m_positionUs < 0) return -1;
    return std::clamp(m_positionUs, m_track->annot.t.front(), m_track->annot.t.back());
}

bool SwingRigDriver::inSwingWindow() const
{
    return available() && m_positionUs >= 0 && m_track->annot.inWindow(m_positionUs);
}

int SwingRigDriver::positionCount() const { return available() ? int(m_track->positions.size()) : 0; }
bool SwingRigDriver::planeAvailable() const { return available() && m_track->annot.planeValid; }
double SwingRigDriver::planeInclDeg() const { return planeAvailable() ? m_track->annot.planeInclDeg : 0.0; }
bool SwingRigDriver::foMirrored() const { return available() && m_track->foMirrored; }

int SwingRigDriver::positionP(int i) const
{
    return i >= 0 && i < positionCount() ? m_track->positions[size_t(i)].p : 0;
}

int SwingRigDriver::positionSource(int i) const
{
    return i >= 0 && i < positionCount() ? m_track->positions[size_t(i)].source : 0;
}

qint64 SwingRigDriver::positionTimeUs(int i) const
{
    return i >= 0 && i < positionCount() ? m_track->positions[size_t(i)].t : -1;
}

QVector3D SwingRigDriver::positionHead(int i, int) const
{
    if (i < 0 || i >= positionCount()) return {};
    return m_track->annot.sample(m_track->positions[size_t(i)].t, SwingAnnotTrack::ClubHead);
}

QVector3D SwingRigDriver::positionButt(int i, int) const
{
    if (i < 0 || i >= positionCount()) return {};
    return m_track->annot.sample(m_track->positions[size_t(i)].t, SwingAnnotTrack::ClubButt);
}

QQuaternion SwingRigDriver::positionRotation(int i, int rev) const
{
    const QVector3D d = positionHead(i, rev) - positionButt(i, rev);
    if (d.lengthSquared() < 1e-12f) return {};
    return QQuaternion::rotationTo(QVector3D(0, 1, 0), d.normalized());
}

bool SwingRigDriver::cameraAvailable(int view) const
{
    return available() && (view == 0 || view == 1) && m_track->cams[size_t(view)].ok;
}

QVector3D SwingRigDriver::cameraPosition(int view) const
{
    return cameraAvailable(view) ? m_track->cams[size_t(view)].pos : QVector3D();
}

QQuaternion SwingRigDriver::cameraRotation(int view) const
{
    if (!cameraAvailable(view)) return {};
    const SceneCamera &c = m_track->cams[size_t(view)];
    // A Qt Quick 3D camera looks down its local −Z with +Y up: x = image right, y = image up.
    return QQuaternion::fromAxes(c.right, -c.down, -c.fwd).normalized();
}

double SwingRigDriver::cameraFovDeg(int view) const
{
    if (!cameraAvailable(view)) return 38.0;
    const SceneCamera &c = m_track->cams[size_t(view)];
    return 2.0 * std::atan(0.5 * c.H / c.f) / sk::kDeg;
}

QPointF SwingRigDriver::projectScene(int view, const QVector3D &p) const
{
    if (!cameraAvailable(view)) return { -1, -1 };
    const SceneCamera &c = m_track->cams[size_t(view)];
    const QVector3D r = p - c.pos;
    const double z = QVector3D::dotProduct(r, c.fwd);
    if (z <= 1e-6) return { -1, -1 };
    return { 0.5 * c.W + c.f * QVector3D::dotProduct(r, c.right) / z,
             0.5 * c.H + c.f * QVector3D::dotProduct(r, c.down) / z };
}
