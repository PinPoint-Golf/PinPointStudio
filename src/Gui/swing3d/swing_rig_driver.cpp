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

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <map>

#include "swing_annotation_mesh.h"
#include "../../Skeleton3d/skeleton3d_json.h"
#include "../../Skeleton3d/skeleton3d_rig.h"
#include "../../Skeleton3d/skeleton3d_synthesis.h"
#include "../../Export/swing_store.h"

namespace sk = pinpoint::skeleton3d;

namespace {

constexpr int kPhaseAddress = 0;  // pinpoint::analysis::Phase::Address
constexpr int kPhaseImpact = 5;   // pinpoint::analysis::Phase::Impact
constexpr int kPhaseFinish = 7;   // pinpoint::analysis::Phase::Finish
constexpr int kPhaseP8 = 14;      // pinpoint::analysis::Phase::ShaftParallelThrough (P8)

// The 3-D swing ENDS AT P8 (Mark, 28 Sept): past it the fit is at its least held — the DTL has
// usually lost the club and the lead arm takes the wrong face-on depth mirror
// (skeleton3d_shaft_branch_design.md) — and nothing after it is worth the mess. With no P8 on file
// the end is impact + this (P8 sits ~95 ms after impact on the 4 July swings).
constexpr qint64 kEndAfterImpactUs = 100000;

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

// The fused downswing plane's normal in the scene frame, and its inclination (annotations design
// §5b). club3d's frame is the face-on CAMERA's (X image-right, Y view ray, Z up — the camera assumed
// level); the fit pitched and rolled that camera, so the normal is carried through the fitted
// face-on axes into the world first. False when the plane was not offered.
bool downPlaneNormal(const QJsonObject &an, const SceneFrame &S, QVector3D *normal, double *inclDeg)
{
    const QJsonObject down = an.value(QStringLiteral("club3d")).toObject().value(QStringLiteral("planes"))
                                 .toObject().value(QStringLiteral("down")).toObject();
    const QJsonArray nj = down.value(QStringLiteral("normal")).toArray();
    if (!down.value(QStringLiteral("offered")).toBool() || nj.size() != 3) return false;
    const QJsonObject camJ = an.value(QStringLiteral("skeleton3d")).toObject().value(QStringLiteral("camera")).toObject();
    const double pF = camJ.value(QStringLiteral("pFDeg")).toDouble() * sk::kDeg;
    const double rF = camJ.value(QStringLiteral("rFDeg")).toDouble() * sk::kDeg;
    sk::V3 right { 1, 0, 0 }, fwd { 0, std::cos(pF), -std::sin(pF) }, up { 0, std::sin(pF), std::cos(pF) };
    if (rF != 0.0) {
        const sk::V3 R0 = right, D0 = -up;
        right = R0 * std::cos(rF) + D0 * std::sin(rF);
        up = -(D0 * std::cos(rF) - R0 * std::sin(rF));
    }
    const sk::V3 nw = right * nj[0].toDouble() + fwd * nj[1].toDouble() + up * nj[2].toDouble();
    *normal = S.dir(nw).normalized();
    *inclDeg = down.value(QStringLiteral("inclDeg")).toDouble();
    return normal->lengthSquared() > 0.5f;
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
    qint64 p8Us = -1;
    qint64 endUs = -1;         // the display's end: P8 (see kEndAfterImpactUs)
    QVector3D centre;
    // Motion annotations.
    SwingAnnotTrack annot;
    struct Position { int p = 0; int source = 0; qint64 t = 0; };
    std::vector<Position> positions;
    std::array<SceneCamera, 2> cams {};
    bool foMirrored = false;

    void setBones(const std::array<double, sk::GroupCount> &lengths);
    void appendFrame(const sk::Pose &pose, const SceneFrame &S, qint64 t, const std::array<uint8_t, sk::Rig::N> &jointTier,
                     const sk::V3 &grip, const sk::V3 &shaftDir, int shaftTierNow);
    bool finishFrames(QString *reason);
    void setPlane(const QVector3D &normal, double inclDeg);
    static std::shared_ptr<const Prepared> fromSynthesis(const sk::SynthResult &r, QString *reason);
    std::shared_ptr<const Prepared> retimed(const std::array<qint64, 7> &from, const std::array<qint64, 7> &to) const;
};

// What one read of a session produced: every shot's club and P-times (a change of selected shot needs
// nothing else), and the finished synthesis for each club asked about so far.
struct SwingRigDriver::SessionData {
    struct Keys { QString club; std::array<qint64, 7> p {}; };
    struct Entry {
        std::shared_ptr<const Prepared> canon;     // null: refused, the shot is shown instead
        std::array<qint64, 7> canonP {};
        int shots = 0, planeShots = 0;
        bool uncertain = false;
    };
    QString dir, fingerprint;
    QHash<QString, Keys> shots;                    // by swing folder name
    std::map<QString, std::shared_ptr<const Entry>> entries;
};

void SwingRigDriver::Prepared::setBones(const std::array<double, sk::GroupCount> &lengths)
{
    const sk::Rig &R = sk::rig();
    for (int j = 0; j < sk::Rig::N; ++j) {
        const int g = sk::jointGroup(j);
        const double s = g >= 0 ? lengths[size_t(g)] : lengths[sk::GSpine];
        const sk::V3 o = R.restT[j] * s;
        offset[size_t(j)] = { float(o.x), float(o.y), float(o.z) };
        meshScale[size_t(j)] = s;
    }
}

void SwingRigDriver::Prepared::appendFrame(const sk::Pose &pose, const SceneFrame &S, qint64 tUs,
                                           const std::array<uint8_t, sk::Rig::N> &jointTier,
                                           const sk::V3 &grip, const sk::V3 &shaftDir, int shaftTierNow)
{
    using AP = SwingAnnotTrack;
    using namespace sk::ybot;
    static constexpr std::array<int, 13> kAnnotJoint {
        LeftArm, RightArm, LeftForeArm, RightForeArm, LeftHand, RightHand, LeftUpLeg, RightUpLeg,
        LeftLeg, RightLeg, LeftFoot, RightFoot, Head };
    std::array<QQuaternion, sk::Rig::N> loc;
    for (int j = 0; j < sk::Rig::N; ++j) loc[size_t(j)] = toQt(pose.local[j]);
    loc[0] = toQt((S.rot * pose.local[0]).normalized());
    t.push_back(tUs);
    local.push_back(loc);
    rootPos.push_back(S.point(pose.pos[0]));
    tier.push_back(jointTier);
    const sk::V3 butt = grip - shaftDir * 0.04;
    shaftButt.push_back(S.point(butt));
    shaftRot.push_back(QQuaternion::rotationTo(QVector3D(0, 1, 0), S.dir(shaftDir).normalized()));
    shaftTier.push_back(shaftTierNow);
    std::array<QVector3D, AP::PointCount> ap;
    std::array<quint8, AP::PointCount> at;
    for (size_t k = 0; k < kAnnotJoint.size(); ++k) {
        ap[k] = S.point(pose.pos[kAnnotJoint[k]]);
        at[k] = jointTier[size_t(kAnnotJoint[k])];
    }
    const sk::V3 head = butt + shaftDir * clubLengthM;     // the tip of the drawn club
    ap[AP::Grip] = S.point(grip);
    ap[AP::ClubButt] = S.point(butt);
    ap[AP::ClubHead] = S.point(head);
    at[AP::Grip] = at[AP::ClubButt] = at[AP::ClubHead] = quint8(std::clamp(shaftTierNow, 0, 3));
    annot.t.push_back(tUs);
    annot.p.push_back(ap);
    annot.tier.push_back(at);
    if (t.size() == 1)
        centre = S.point((pose.pos[sk::ybot::LeftUpLeg] + pose.pos[sk::ybot::RightUpLeg]) * 0.5);
}

bool SwingRigDriver::Prepared::finishFrames(QString *reason)
{
    using AP = SwingAnnotTrack;
    // Drop the frames past the end, keeping the first one beyond it so the pose AT the end
    // interpolates exactly.
    if (endUs >= 0) {
        size_t keep = t.size();
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i] >= endUs) { keep = i + 1; break; }
        t.resize(keep); local.resize(keep); rootPos.resize(keep); tier.resize(keep);
        shaftButt.resize(keep); shaftRot.resize(keep); shaftTier.resize(keep);
        annot.t.resize(keep); annot.p.resize(keep); annot.tier.resize(keep);
    }
    if (t.size() < 2) {
        *reason = QObject::tr("the 3-D skeleton has fewer than two frames");
        return false;
    }
    if (endUs < 0 || endUs > t.back()) endUs = t.back();
    positions.erase(std::remove_if(positions.begin(), positions.end(),
                                   [&](const Position &q) { return q.t > endUs; }),
                    positions.end());

    // Stabilise the club for display: smooth the clubhead path, keep the butt in the fitted hands,
    // and point the drawn club at the smoothed head — the club model, its trace and the P-positions
    // stay one object. The length is the fitted one: stabilising must not hide that it is short.
    const size_t n = annot.t.size();
    std::vector<QVector3D> heads(n);
    std::vector<float> w(n);
    for (size_t i = 0; i < n; ++i) {
        heads[i] = annot.p[i][AP::ClubHead];
        w[i] = annot.tier[i][AP::ClubHead] > 0 ? 1.f : 0.f;
    }
    const std::vector<QVector3D> sm = swing3d::smoothPath(annot.t, heads, w, kClubSmoothUs);
    for (size_t i = 0; i < n; ++i) {
        const QVector3D butt = annot.p[i][AP::ClubButt];
        const QVector3D d = sm[i] - butt;
        if (d.lengthSquared() < 1e-6f) continue;
        const QVector3D dir = d.normalized();
        annot.p[i][AP::ClubHead] = butt + dir * float(clubLengthM);
        shaftRot[i] = QQuaternion::rotationTo(QVector3D(0, 1, 0), dir);
    }
    return true;
}

// The fused downswing plane as a quad through the clubhead at impact (annotations design §5b).
void SwingRigDriver::Prepared::setPlane(const QVector3D &n, double inclDeg)
{
    using AP = SwingAnnotTrack;
    if (impactUs < 0) return;
    const QVector3D H = annot.sample(impactUs, AP::ClubHead);
    const QVector3D B = annot.sample(impactUs, AP::ClubButt);
    const QVector3D s = (B - H).normalized();
    const QVector3D e1 = (s - n * QVector3D::dotProduct(n, s)).normalized();
    const QVector3D e2 = QVector3D::crossProduct(n, e1).normalized();
    if (n.lengthSquared() > 0.5f && e1.lengthSquared() > 0.5f) {
        annot.planeValid = true;
        annot.planeInclDeg = inclDeg;
        annot.planeQuad = { H - e2 * kPlaneSideM, H + e2 * kPlaneSideM,
                            H + e1 * kPlaneUpM + e2 * kPlaneSideM, H + e1 * kPlaneUpM - e2 * kPlaneSideM };
    }
}

std::shared_ptr<const SwingRigDriver::Prepared> SwingRigDriver::Prepared::fromSynthesis(const sk::SynthResult &r, QString *reason)
{
    auto P = std::make_shared<Prepared>();
    P->dtl = true;
    P->clubLengthM = r.clubLengthM > 0.5 ? r.clubLengthM : 0.95;
    P->setBones(r.lengths);
    P->addressUs = r.pUs[0];
    P->impactUs = r.pUs[6];
    P->p8Us = r.pUs[7];
    P->endUs = r.pUs[7];
    P->annot.addressUs = P->addressUs;
    P->annot.impactUs = P->impactUs;
    P->annot.finishUs = P->endUs;
    for (size_t k = 0; k < r.pUs.size(); ++k) P->positions.push_back({ int(k + 1), r.pSource[k], r.pUs[k] });
    if (r.ballValid) {
        P->ball = { float(r.ball.x), float(r.ball.y), float(r.ball.z) };
        P->ballValid = true;
    }
    const SceneFrame identity {};           // the synthesis is in the scene frame already
    sk::Pose pose;
    for (const sk::SynthFrame &f : r.frames) {
        sk::synthPose(r, f, pose);
        P->appendFrame(pose, identity, f.t_us, f.tier, f.grip, f.shaftDir, f.shaftTier);
    }
    if (!P->finishFrames(reason)) return {};
    if (r.planeValid)
        P->setPlane(QVector3D(float(r.planeNormal.x), float(r.planeNormal.y), float(r.planeNormal.z)).normalized(), r.planeInclDeg);
    return P;
}

// A copy on the selected shot's clock: `from` (canonical P1–P7) → `to` (that shot's), so the figure
// reaches the top when the video does. Past P7 it runs at canonical speed.
std::shared_ptr<const SwingRigDriver::Prepared> SwingRigDriver::Prepared::retimed(const std::array<qint64, 7> &from,
                                                                                  const std::array<qint64, 7> &to) const
{
    auto map = [&](qint64 c) -> qint64 {
        if (c < 0) return c;
        if (c >= from[6]) return to[6] + (c - from[6]);
        size_t k = 0;
        while (k + 1 < 6 && c >= from[k + 1]) ++k;
        const double f = double(c - from[k]) / double(std::max<qint64>(1, from[k + 1] - from[k]));
        return to[k] + qint64(std::llround(f * double(to[k + 1] - to[k])));
    };
    auto P = std::make_shared<Prepared>(*this);
    for (qint64 &v : P->t) v = map(v);
    for (qint64 &v : P->annot.t) v = map(v);
    P->addressUs = map(addressUs);
    P->impactUs = map(impactUs);
    P->p8Us = map(p8Us);
    P->endUs = map(endUs);
    P->annot.addressUs = map(annot.addressUs);
    P->annot.impactUs = map(annot.impactUs);
    P->annot.finishUs = map(annot.finishUs);
    for (Position &q : P->positions) q.t = map(q.t);
    return P;
}

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
qint64 SwingRigDriver::endUs() const { return available() ? m_track->endUs : 0; }

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
    QVariantList gates;
    auto p = prepare(dir, &why, &gates);
    m_reason = why;
    m_gates = gates;
    adopt(p, m_generation);
    return available();
}

void SwingRigDriver::setSessionScope(bool on)
{
    if (on == m_sessionScope) return;
    m_sessionScope = on;
    m_sessionCheck = on;
    emit sessionScopeChanged();
    startLoad();
}

void SwingRigDriver::setPanelShown(bool shown)
{
    if (shown == m_panelShown) return;
    m_panelShown = shown;
    emit panelShownChanged();
    if (shown && m_loadPending) startLoad();
}

void SwingRigDriver::invalidateSession()
{
    m_session.reset();
    if (m_sessionScope) startLoad();
}

void SwingRigDriver::clearSessionFacts()
{
    m_sessionShots = 0;
    m_sessionPlaneShots = 0;
    m_sessionFallback.clear();
    m_clubAfterImpactUncertain = false;
}

void SwingRigDriver::startLoad()
{
    if (m_sessionScope && !m_panelShown) {          // nothing is worked out until the panel is on show
        m_loadPending = true;
        return;
    }
    m_loadPending = false;
    if (m_sessionScope && !m_swingDir.isEmpty()) {
        startSessionLoad();
        return;
    }
    clearSessionFacts();
    startShotLoad();
}

void SwingRigDriver::startShotLoad()
{
    const quint64 gen = ++m_generation;
    m_track.reset();
    m_loading = !m_swingDir.isEmpty();
    m_reason = m_swingDir.isEmpty() ? tr("no swing selected") : tr("loading…");
    m_gates.clear();
    emit loadedChanged();
    ++m_revision;
    emit revisionChanged();
    if (m_swingDir.isEmpty()) return;
    // The document is tens of megabytes: parse it off the GUI thread and adopt the result there.
    QPointer<SwingRigDriver> self(this);
    const QString dir = m_swingDir;
    QThreadPool::globalInstance()->start([self, dir, gen]() {
        QString why;
        QVariantList gates;
        auto p = prepare(dir, &why, &gates);
        QMetaObject::invokeMethod(self, [self, p, gen, why, gates]() {
            if (!self) return;
            if (gen != self->m_generation) return;   // a newer swing was asked for meanwhile
            self->m_reason = why;
            self->m_gates = gates;
            self->adopt(p, gen);
        }, Qt::QueuedConnection);
    });
}

void SwingRigDriver::startSessionLoad()
{
    const QFileInfo fi(m_swingDir);
    const QString sessionDir = fi.absolutePath(), folder = fi.fileName();
    // Another shot of a session already worked out: a lookup and a retime, no file read.
    if (!m_sessionCheck && m_session && m_session->dir == sessionDir) {
        const auto keys = m_session->shots.constFind(folder);
        if (keys != m_session->shots.constEnd() && m_session->entries.count(keys->club)) {
            ++m_generation;
            adoptSession(m_session, false);
            return;
        }
    }
    const quint64 gen = ++m_generation;
    m_track.reset();
    m_loading = true;
    m_reason = tr("loading…");
    m_gates.clear();
    clearSessionFacts();
    emit loadedChanged();
    ++m_revision;
    emit revisionChanged();
    QPointer<SwingRigDriver> self(this);
    std::shared_ptr<const SessionData> previous = m_session;
    QThreadPool::globalInstance()->start([self, sessionDir, folder, previous, gen]() {
        auto data = prepareSession(sessionDir, folder, previous);
        QMetaObject::invokeMethod(self, [self, data, gen]() {
            if (!self || gen != self->m_generation) return;
            self->adoptSession(data, false);
        }, Qt::QueuedConnection);
    });
}

void SwingRigDriver::adoptSession(std::shared_ptr<const SessionData> data, bool synchronous)
{
    m_session = std::move(data);
    m_sessionCheck = false;
    const SessionData::Keys keys = m_session->shots.value(QFileInfo(m_swingDir).fileName());
    const auto it = m_session->entries.find(keys.club);
    const std::shared_ptr<const SessionData::Entry> entry = it == m_session->entries.end() ? nullptr : it->second;
    if (!entry || !entry->canon) {
        clearSessionFacts();
        m_sessionFallback = tr("Session needs %1 shots in 3-D · showing this shot").arg(sk::kSynthMinShots);
        if (synchronous) {
            QString why;
            QVariantList gates;
            auto p = prepare(m_swingDir, &why, &gates);
            m_reason = why;
            m_gates = gates;
            adopt(p, m_generation);
        } else {
            startShotLoad();
        }
        return;
    }
    // The selected shot's clock. Known P-times stand; a missing one takes its place from the
    // canonical durations; a shot with none plays on the canonical clock.
    std::array<qint64, 7> to = entry->canonP;
    std::array<bool, 7> known {};
    int nKnown = 0;
    qint64 last = -1;
    for (size_t k = 0; k < 7; ++k)
        if (keys.p[k] > last) { known[k] = true; last = keys.p[k]; ++nKnown; }
    if (nKnown > 0) {
        for (size_t k = 0; k < 7; ++k) {
            if (known[k]) { to[k] = keys.p[k]; continue; }
            int a = int(k) - 1, b = int(k) + 1;
            while (a >= 0 && !known[size_t(a)]) --a;
            while (b < 7 && !known[size_t(b)]) ++b;
            const auto &c = entry->canonP;
            if (a >= 0 && b < 7) {
                const double f = double(c[k] - c[size_t(a)]) / double(std::max<qint64>(1, c[size_t(b)] - c[size_t(a)]));
                to[k] = keys.p[size_t(a)] + qint64(std::llround(f * double(keys.p[size_t(b)] - keys.p[size_t(a)])));
            } else if (a >= 0) {
                to[k] = keys.p[size_t(a)] + (c[k] - c[size_t(a)]);
            } else {
                to[k] = keys.p[size_t(b)] - (c[size_t(b)] - c[k]);
            }
        }
    }
    m_sessionShots = entry->shots;
    m_sessionPlaneShots = entry->planeShots;
    m_clubAfterImpactUncertain = entry->uncertain;
    m_sessionFallback.clear();
    m_reason.clear();
    m_gates.clear();
    adopt(entry->canon->retimed(entry->canonP, to), m_generation);
}

bool SwingRigDriver::loadSessionNow(const QString &swingDir)
{
    m_swingDir = swingDir;
    m_sessionScope = true;
    ++m_generation;
    const QFileInfo fi(swingDir);
    adoptSession(prepareSession(fi.absolutePath(), fi.fileName(), m_session), true);
    return available();
}

std::shared_ptr<const SwingRigDriver::SessionData> SwingRigDriver::prepareSession(const QString &sessionDir, const QString &shotFolder,
                                                                                  std::shared_ptr<const SessionData> previous)
{
    // The carousel's own enumeration (SwingDocReader::findSwingDirs): swing_* by name.
    const QStringList folders = QDir(sessionDir).entryList({ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QString fingerprint;
    for (const QString &name : folders) {
        const pinpoint::SwingStore::DocInfo d = pinpoint::SwingStore::info(sessionDir + QLatin1Char('/') + name);
        fingerprint += QStringLiteral("%1:%2:%3;").arg(name).arg(d.size).arg(d.mtimeMs);
    }
    const bool sameFiles = previous && previous->dir == sessionDir && previous->fingerprint == fingerprint;
    if (sameFiles) {
        const auto keys = previous->shots.constFind(shotFolder);
        if (keys != previous->shots.constEnd() && previous->entries.count(keys->club)) return previous;
    }

    auto out = std::make_shared<SessionData>();
    out->dir = sessionDir;
    out->fingerprint = fingerprint;
    if (sameFiles) out->entries = previous->entries;

    // Only what the synthesis uses: the pose tracks are most of a document.
    static const QStringList kParts {
        QStringLiteral("/analysis/skeleton3d"), QStringLiteral("/analysis/club/positions"),
        QStringLiteral("/analysis/club3d/planes"), QStringLiteral("/review"),
        QStringLiteral("/athlete"), QStringLiteral("/clock") };
    std::vector<sk::SynthSwing> swings;
    swings.reserve(size_t(folders.size()));
    bool leadIsLeft = true;
    for (const QString &name : folders) {
        const QJsonObject root = pinpoint::SwingStore::loadParts(sessionDir + QLatin1Char('/') + name, kParts);
        const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
        const qint64 t0 = qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
        sk::SynthSwing s;
        s.id = name;
        s.club = root.value(QStringLiteral("review")).toObject().value(QStringLiteral("club")).toString();
        SessionData::Keys keys;
        keys.club = s.club;
        keys.p.fill(-1);
        for (const QJsonValue &pv : an.value(QStringLiteral("club")).toObject().value(QStringLiteral("positions")).toArray()) {
            const QJsonObject po = pv.toObject();
            const int p = po.value(QStringLiteral("p")).toInt();
            const qint64 raw = qint64(po.value(QStringLiteral("t_us")).toDouble(-1));
            if (p < 1 || p > 7 || raw < 0) continue;
            keys.p[size_t(p - 1)] = raw >= t0 ? raw - t0 : raw;
            s.pSource[size_t(p - 1)] = po.value(QStringLiteral("source")).toInt();
        }
        for (size_t k = 0; k < 7; ++k) s.pUs[k] = keys.p[k];
        out->shots.insert(name, keys);
        if (name == shotFolder) {
            const QString hand = root.value(QStringLiteral("athlete")).toObject().value(QStringLiteral("handedness")).toString();
            if (!hand.isEmpty()) leadIsLeft = hand.compare(QLatin1String("left"), Qt::CaseInsensitive) != 0;
        }
        s.track = sk::skeleton3dFromJson(an.value(QStringLiteral("skeleton3d")).toObject(), t0);
        if (s.track.valid) {
            SceneFrame S;
            S.rot = (sk::Q::axisAngle({ 1, 0, 0 }, -sk::kPi / 2.0)
                     * sk::Q::axisAngle({ 0, 0, 1 }, -s.track.stanceYawDeg * sk::kDeg)).normalized();
            S.origin = s.track.displayOrigin;
            QVector3D n;
            double incl = 0;
            if (downPlaneNormal(an, S, &n, &incl)) {
                s.hasPlane = true;
                s.planeNormal = { n.x(), n.y(), n.z() };
                s.planeInclDeg = incl;
            }
            swings.push_back(std::move(s));
        }
    }

    const QString club = out->shots.value(shotFolder).club;
    const sk::SynthResult r = sk::synthesiseSession(swings, club, leadIsLeft);
    auto entry = std::make_shared<SessionData::Entry>();
    if (r.valid) {
        QString why;
        entry->canon = Prepared::fromSynthesis(r, &why);
        for (size_t k = 0; k < 7; ++k) entry->canonP[k] = r.pUs[k];
        entry->shots = r.nShots;
        entry->planeShots = r.nPlaneShots;
        entry->uncertain = r.clubAfterImpactUncertain;
    }
    out->entries[club] = entry;
    return out;
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

std::shared_ptr<const SwingRigDriver::Prepared> SwingRigDriver::prepare(const QString &dir, QString *reason, QVariantList *gates)
{
    QString err;
    const QJsonObject root = pinpoint::SwingStore::load(dir, &err);
    if (root.isEmpty()) {
        *reason = err.isEmpty() ? QObject::tr("no swing document") : err;
        return {};
    }
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    // The records ride out whatever the skeleton's fate: a refused one is exactly what they explain.
    if (gates && an.contains(QStringLiteral("gatesVersion")))
        *gates = an.value(QStringLiteral("gates")).toArray().toVariantList();
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

    P->setBones(trk.lengths);

    // Impact, window-relative, for the ball.
    for (const QJsonValue &pv : an.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject po = pv.toObject();
        const int phase = po.value(QStringLiteral("phase")).toInt();
        if (phase != kPhaseImpact && phase != kPhaseAddress && phase != kPhaseFinish && phase != kPhaseP8) continue;
        const qint64 raw = qint64(po.value(QStringLiteral("t_us")).toDouble());
        const qint64 rel = raw >= t0 ? raw - t0 : raw;
        if (phase == kPhaseImpact) P->impactUs = rel;
        else if (phase == kPhaseAddress) P->addressUs = rel;
        else if (phase == kPhaseP8) P->p8Us = rel;
        else P->annot.finishUs = rel;
    }
    // The display's end: P8, else impact + 100 ms, else the fit's own end (set after the frames).
    P->endUs = P->p8Us >= 0 ? P->p8Us : P->impactUs >= 0 ? P->impactUs + kEndAfterImpactUs : -1;
    if (P->endUs >= 0) P->annot.finishUs = P->endUs;      // the annotations' window ends there too
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
    sk::Pose pose;
    for (const sk::Skeleton3DFrame &f : trk.frames) {
        if (int(f.dof.size()) != R.dofCount()) continue;
        sk::forwardKinematics(R, f.dof.data(), scale.data(), pose);
        P->appendFrame(pose, S, f.t_us, f.tier, f.grip, f.shaftDir, f.shaftTier);
    }
    if (!P->finishFrames(reason)) return {};

    // The fitted cameras, for the match presets (annotations design §5a).
    const QJsonObject sk3 = an.value(QStringLiteral("skeleton3d")).toObject();
    const QJsonObject camJ = sk3.value(QStringLiteral("camera")).toObject();
    P->foMirrored = sk3.value(QStringLiteral("foMirrored")).toBool();
    double foWH[2], dtlWH[2];
    streamHeights(root, an, foWH, dtlWH);
    P->cams[0] = sceneCamera(camJ, 0, foWH[0], foWH[1], S);
    if (trk.dtl) P->cams[1] = sceneCamera(camJ, 1, dtlWH[0], dtlWH[1], S);

    // The fused downswing plane (annotations design §5b).
    QVector3D planeN;
    double planeIncl = 0;
    if (downPlaneNormal(an, S, &planeN, &planeIncl)) P->setPlane(planeN, planeIncl);
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
        const qint64 t = std::clamp(want, P.t.front(), P.endUs);     // past P8 the figure holds
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
    return std::clamp(m_positionUs, m_track->annot.t.front(), m_track->endUs);
}

bool SwingRigDriver::inSwingWindow() const
{
    return available() && m_positionUs >= 0 && m_track->annot.inWindow(m_positionUs);
}

int SwingRigDriver::positionCount() const { return available() ? int(m_track->positions.size()) : 0; }
bool SwingRigDriver::planeAvailable() const { return available() && m_track->annot.planeValid; }
bool SwingRigDriver::faceOnOnly() const { return available() && !(m_track->dtl && m_track->annot.planeValid); }
bool SwingRigDriver::heldAtEnd() const { return available() && m_positionUs > m_track->endUs; }
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
