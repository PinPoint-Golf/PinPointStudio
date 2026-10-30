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

#include "posture_picture.h"

#include "../../Body/address_marks.h"       // buttShiftCm — the butt line's travel, pelvisThrust's sign
#include "../../Body/head_track.h"          // trackHead — the inter-ear ruler headLift is measured with
#include "../../Diagnostics/measures/measure_sample.h"   // buildPhaseGrid — the chart's own sampler, for the head ticks
#include "../report/report_stills_pure.h"   // which frame, which crop: the report stills' own rule
#include "../shot/dtl_overlay_payload.h"    // the DTL ball and club samples, as the replay payload carries them

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>
#include <QVariantList>

#include <algorithm>

namespace pinpoint::charts {

namespace st = pinpoint::report::stills;

namespace {

// AutoAnnotations.js's constants, verbatim.
constexpr double kConfMin = 0.3;
constexpr qint64 kAddressWindowUs = 60000;
constexpr int kFlagCoasted = 0x04, kFlagPredicted = 0x20, kFlagOffFrame = 0x80, kFlagSynthesized = 0x100;
constexpr double kShoulderPlaneMargin = 0.25, kShoulderPlaneUp = 0.5, kShoulderPlaneDown = 0.7;
constexpr int kImpactPhase = 5;   // Phase::Impact

const char *const kTags[kPicturePositions] = { "P1", "P4", "P7" };
const int kPosIndex[kPicturePositions] = { 0, 3, 6 };
const char *const kWhen[kPicturePositions] = { "address", "the top", "impact" };

bool fin(double v) { return std::isfinite(v); }
double numOr(const QJsonValue &v, double d = kPicNaN) { return v.isDouble() ? v.toDouble() : d; }

// A figure as the posture section prints it: one decimal, U+2212 for minus.
QString fig1(double v)
{
    QString s = QString::number(std::fabs(v), 'f', 1);
    return v < -0.05 ? QStringLiteral("−") + s : s;
}

// ── the coach lines (AutoAnnotations.js), normalised as the JS has them ─────────────────────────────
struct Mark {
    QString kind, set, label, dash;
    double  cx = kPicNaN, cy = kPicNaN, r = kPicNaN, x = kPicNaN;
    QVector<QPointF> pts;
};

bool kpt(const QVector<float> &kp, int j, QPointF *out)
{
    if (kp.size() < (j + 1) * 3) return false;
    if (!(kp[j * 3 + 2] > kConfMin)) return false;
    *out = QPointF(kp[j * 3], kp[j * 3 + 1]);
    return true;
}
double dist(const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

// jointNeck(frame): halfway from the shoulder midpoint (5 and 6, both confident) to the ear point (the mean of
// whichever of 3 / 4 are confident, at least one) — the point AddressMarksStage starts its back-of-the-neck walk
// from (measureBackLine's shX/earX, neckFrac 0.5).
bool jointNeck(const QVector<float> &kp, QPointF *out)
{
    QPointF l, r;
    if (!kpt(kp, 5, &l) || !kpt(kp, 6, &r)) return false;
    double ex = 0, ey = 0;
    int ne = 0;
    for (int j : { 3, 4 }) { QPointF e; if (kpt(kp, j, &e)) { ex += e.x(); ey += e.y(); ++ne; } }
    if (ne == 0) return false;
    ex /= ne; ey /= ne;
    const double sx = 0.5 * (l.x() + r.x()), sy = 0.5 * (l.y() + r.y());
    *out = QPointF(sx + 0.5 * (ex - sx), sy + 0.5 * (ey - sy));
    return true;
}

// headCircle(frame): the mean of the visible head points; radius from the ears, else the eyes, else 0.04, never
// below 0.025 — a fraction of the frame HEIGHT.
bool headCircle(const QVector<float> &kp, Mark *m)
{
    if (kp.isEmpty()) return false;
    double cx = 0, cy = 0;
    int n = 0;
    for (int j = 0; j <= 4; ++j) { QPointF p; if (kpt(kp, j, &p)) { cx += p.x(); cy += p.y(); ++n; } }
    if (n == 0) return false;
    cx /= n; cy /= n;
    QPointF lEar, rEar, lEye, rEye;
    const bool ears = kpt(kp, 3, &lEar) && kpt(kp, 4, &rEar);
    const bool eyes = kpt(kp, 1, &lEye) && kpt(kp, 2, &rEye);
    double r = ears ? 0.75 * dist(lEar, rEar) : eyes ? 1.1 * dist(lEye, rEye) : 0.04;
    r = std::max(r, 0.025);
    m->kind = QStringLiteral("circle"); m->cx = cx; m->cy = cy; m->r = r; m->label = QStringLiteral("head"); m->set = QStringLiteral("headHips");
    return true;
}

// extendToSquare(p, q): the line through p and q clipped to the unit square; false when they coincide.
bool extendToSquare(const QPointF &p, const QPointF &q, QPointF *a, QPointF *b)
{
    const double dx = q.x() - p.x(), dy = q.y() - p.y();
    if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9) return false;
    double tMin = -std::numeric_limits<double>::infinity(), tMax = std::numeric_limits<double>::infinity();
    const auto clip = [&](double d, double lo, double hi) {
        if (std::fabs(d) < 1e-12) return;
        double t0 = lo / d, t1 = hi / d;
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0); tMax = std::min(tMax, t1);
    };
    clip(dx, 0 - p.x(), 1 - p.x());
    clip(dy, 0 - p.y(), 1 - p.y());
    if (!(tMin < tMax)) return false;
    *a = QPointF(p.x() + tMin * dx, p.y() + tMin * dy);
    *b = QPointF(p.x() + tMax * dx, p.y() + tMax * dy);
    return true;
}

// The face-on head circle the picture draws: the mask's head outline (stage version 4), else the face points'.
bool pictureHeadCircle(const PictureCameraDoc &cam, Mark *m)
{
    if (cam.marksFound && cam.headFound && fin(cam.headCx) && fin(cam.headCy) && cam.headR > 0) {
        m->kind = QStringLiteral("circle"); m->cx = cam.headCx; m->cy = cam.headCy; m->r = cam.headR;
        m->label = QStringLiteral("head"); m->set = QStringLiteral("headHips");
        return true;
    }
    return headCircle(cam.addressKp, m);
}

QVector<Mark> coachMarks(const PictureCameraDoc &cam, bool leadIsLeft)
{
    QVector<Mark> out;
    const QVector<float> &kp = cam.addressKp;
    if (!cam.dtl) {
        Mark c;
        if (pictureHeadCircle(cam, &c)) out << c;
    }
    // hipLines: the person mask's edges (never the joints instead)
    if (cam.marksFound) {
        if (cam.dtl) {
            if (fin(cam.buttX)) { Mark m; m.kind = QStringLiteral("vline"); m.x = cam.buttX; m.label = QStringLiteral("butt"); m.set = QStringLiteral("headHips"); out << m; }
        } else {
            for (double x : { cam.leftX, cam.rightX })
                if (fin(x)) { Mark m; m.kind = QStringLiteral("vline"); m.x = x; m.label = QStringLiteral("hip edge"); m.set = QStringLiteral("headHips"); out << m; }
        }
    }
    if (!cam.dtl) return out;
    QPointF trail;
    const bool hasTrail = kpt(kp, leadIsLeft ? 6 : 5, &trail);
    // planeLine: grip → ball (else the sample's own head), to the square's edges; dashed
    if (cam.hasClub) {
        QPointF a, b;
        if (extendToSquare(cam.grip, cam.hasBall ? cam.ball : cam.head, &a, &b)) {
            Mark m; m.kind = QStringLiteral("polyline"); m.pts = { a, b }; m.dash = QStringLiteral("dash"); m.set = QStringLiteral("plane");
            m.label = cam.hasBall ? QStringLiteral("address shaft plane (ball → hands)") : QStringLiteral("address shaft plane (shaft)");
            out << m;
        }
    }
    // hoganLine: ball → the trail shoulder, on to the edge; long dashes
    if (cam.hasBall && hasTrail) {
        QPointF a, b;
        if (extendToSquare(cam.ball, trail, &a, &b)) {
            Mark m; m.kind = QStringLiteral("polyline"); m.pts = { cam.ball, b }; m.dash = QStringLiteral("long"); m.set = QStringLiteral("plane");
            m.label = QStringLiteral("Hogan plane (ball → trail shoulder)");
            out << m;
        }
    }
    // spineLine and shoulderPlane: the trail hip joint (the trail leg's first point, so the two join) → the joint
    // neck, both off the address pose; either not confident ⇒ neither line (Mark, 23 Oct 2026)
    QPointF hip, neck;
    const bool axis = kpt(kp, leadIsLeft ? 12 : 11, &hip) && jointNeck(kp, &neck);
    const double vx = axis ? neck.x() - hip.x() : 0, vy = axis ? neck.y() - hip.y() : 0;
    if (axis && !(std::fabs(vx) < 1e-9 && std::fabs(vy) < 1e-9)) {
        Mark m; m.kind = QStringLiteral("polyline"); m.label = QStringLiteral("spine"); m.set = QStringLiteral("posture");
        m.pts = { hip, neck };
        out << m;
        const double aspect = cam.H > 0 ? double(cam.W) / double(cam.H) : kPicNaN;
        if (hasTrail && aspect > 0 && fin(aspect)) {
            double nx = vy, ny = -vx * aspect;
            if (ny > 0) { nx = -nx; ny = -ny; }
            const QPointF o(trail.x() + kShoulderPlaneMargin * nx / aspect, trail.y() + kShoulderPlaneMargin * ny);
            Mark s; s.kind = QStringLiteral("polyline"); s.label = QStringLiteral("shoulder plane"); s.dash = QStringLiteral("dot"); s.set = QStringLiteral("posture");
            s.pts = { QPointF(o.x() - kShoulderPlaneDown * vx, o.y() - kShoulderPlaneDown * vy), QPointF(o.x() + kShoulderPlaneUp * vx, o.y() + kShoulderPlaneUp * vy) };
            out << s;
        }
    }
    // trailLeg: hip → knee → ankle, all three confident
    {
        const int j[3] = { leadIsLeft ? 12 : 11, leadIsLeft ? 14 : 13, leadIsLeft ? 16 : 15 };
        QPointF h, k, a;
        if (kpt(kp, j[0], &h) && kpt(kp, j[1], &k) && kpt(kp, j[2], &a)) {
            Mark m; m.kind = QStringLiteral("polyline"); m.label = QStringLiteral("trail leg"); m.set = QStringLiteral("posture"); m.pts = { h, k, a };
            out << m;
        }
    }
    return out;
}

// ── into the frame's own pixels (crop-local) ─────────────────────────────────────────────────────────
struct Px {
    double W, H, ox, oy;
    double x(double nx) const { return nx * W - ox; }
    double y(double ny) const { return ny * H - oy; }
};
QVariantList ptsList(const QVector<QPointF> &pts)
{
    QVariantList out;
    for (const QPointF &p : pts) out.append(QVariant(QVariantList{ p.x(), p.y() }));
    return out;
}
QVariantMap markMap(const Mark &m, const Px &px, const QString &role)
{
    QVariantMap o{ { QStringLiteral("kind"), m.kind }, { QStringLiteral("role"), role }, { QStringLiteral("set"), m.set },
                   { QStringLiteral("label"), m.label }, { QStringLiteral("dash"), m.dash }, { QStringLiteral("hueKey"), QString() } };
    if (m.kind == QLatin1String("circle")) {
        o.insert(QStringLiteral("cx"), px.x(m.cx)); o.insert(QStringLiteral("cy"), px.y(m.cy)); o.insert(QStringLiteral("r"), m.r * px.H);
    } else if (m.kind == QLatin1String("vline")) {
        o.insert(QStringLiteral("x"), px.x(m.x));
    } else {
        QVector<QPointF> p;
        for (const QPointF &q : m.pts) p << QPointF(px.x(q.x()), px.y(q.y()));
        o.insert(QStringLiteral("pts"), ptsList(p));
    }
    return o;
}
QVariantMap rectMark(double x, double y, double w, double h, const QString &label, const QString &hueKey)
{
    return { { QStringLiteral("kind"), QStringLiteral("rect") }, { QStringLiteral("role"), QStringLiteral("band") },
             { QStringLiteral("set"), QString() }, { QStringLiteral("label"), label }, { QStringLiteral("dash"), QString() },
             { QStringLiteral("hueKey"), hueKey }, { QStringLiteral("x"), x }, { QStringLiteral("y"), y },
             { QStringLiteral("w"), w }, { QStringLiteral("h"), h } };
}

// A horizontal tick in the frame's own pixels (the head ticks: already crop-local).
QVariantMap tickMark(double x0, double x1, double y, const QString &label, const QString &role, const QString &hueKey)
{
    return { { QStringLiteral("kind"), QStringLiteral("polyline") }, { QStringLiteral("role"), role }, { QStringLiteral("set"), QString() },
             { QStringLiteral("label"), label }, { QStringLiteral("dash"), QString() }, { QStringLiteral("hueKey"), hueKey },
             { QStringLiteral("pts"), ptsList({ QPointF(x0, y), QPointF(x1, y) }) } };
}

QString sentence(QString s)
{
    s = s.trimmed();
    if (s.isEmpty()) return s;
    s[0] = s[0].toUpper();
    if (!s.endsWith(QLatin1Char('.'))) s += QLatin1Char('.');
    return s;
}

// The view's keys of an addressMarks object.
void readView(const QJsonObject &v, PictureCameraDoc *c)
{
    c->marksFound = v.value(QStringLiteral("found")).toBool(false);
    if (!c->marksFound) return;
    c->leftX = numOr(v.value(QStringLiteral("leftX")));
    c->rightX = numOr(v.value(QStringLiteral("rightX")));
    c->buttX = numOr(v.value(QStringLiteral("buttX")));
    c->rowY = numOr(v.value(QStringLiteral("rowY")));
    c->headCx = numOr(v.value(QStringLiteral("headCx")));
    c->headCy = numOr(v.value(QStringLiteral("headCy")));
    c->headR = numOr(v.value(QStringLiteral("headR")));
    c->headFound = v.value(QStringLiteral("headFound")).toBool(false) && fin(c->headCx) && fin(c->headCy) && c->headR > 0;
    c->headRefused = v.value(QStringLiteral("headRefused")).toString();
}

// The face-on pose as the analysis' PoseTrack2D (frames and the smoothed companion), re-timed window-relative:
// trackHead's own input, so the ruler is the one headLift was measured with.
pinpoint::analysis::PoseTrack2D poseTrackOf(const QJsonObject &block, qint64 t0)
{
    using pinpoint::analysis::PoseFrame2D;
    pinpoint::analysis::PoseTrack2D out;
    const auto read = [&](const QJsonArray &arr, std::vector<PoseFrame2D> &into) {
        into.reserve(size_t(arr.size()));
        for (const QJsonValue &fv : arr) {
            const QJsonObject f = fv.toObject();
            PoseFrame2D p;
            p.t_us = st::relUs(qint64(f.value(QStringLiteral("t_us")).toDouble()), t0);
            const QJsonArray kp = f.value(QStringLiteral("kp")).toArray();
            const int n = std::min<int>(int(kp.size()) / 3, pinpoint::analysis::kWholeBodyJoints);
            for (int j = 0; j < n; ++j) {
                p.kp[size_t(j)] = QPointF(kp.at(3 * j).toDouble(), kp.at(3 * j + 1).toDouble());
                p.conf[size_t(j)] = float(kp.at(3 * j + 2).toDouble());
            }
            into.push_back(p);
        }
        std::sort(into.begin(), into.end(), [](const PoseFrame2D &a, const PoseFrame2D &b) { return a.t_us < b.t_us; });
    };
    read(block.value(QStringLiteral("frames")).toArray(), out.frames);
    read(block.value(QStringLiteral("smoothed")).toArray(), out.smoothed);
    return out;
}

QVector<float> nearestPose(const st::PoseTrackLite &pose, qint64 t, qint64 windowUs)
{
    const QVector<float> *best = nullptr;
    qint64 bestD = std::numeric_limits<qint64>::max();
    for (const st::PoseSample &s : pose.samples) {
        const qint64 d = std::llabs(s.tUs - t);
        if (d <= windowUs && d < bestD) { bestD = d; best = &s.kp; }
    }
    return best ? *best : QVector<float>{};
}

// The stills' crop bounds the golfer by his JOINTS (bodyExtent), and the butt and the hip edges are the person
// mask's outline, behind and outside them: on Nick's DTL frames the crop's edge fell ON the butt line. The picture
// widens the crop, sideways only, to the golfer's own marks plus kMarkMargin of the crop's height (his body, never the
// room: the margin is the stills' body margin) and the room the hip strip needs. Clamped to the frame.
constexpr double kMarkMargin = 0.06;
void widenToMarks(PictureCameraDoc *c)
{
    if (!c->cropPx.isValid() || c->W <= 0) return;
    QVector<double> xs;
    for (double x : { c->leftX, c->rightX, c->buttX, c->impactButtX }) if (fin(x)) xs << x * c->W;
    if (xs.isEmpty()) return;
    const double m = kMarkMargin * c->cropPx.height();
    const auto [lo, hi] = std::minmax_element(xs.cbegin(), xs.cend());
    const int left = std::max(0, std::min(c->cropPx.left(), int(std::floor(*lo - m))));
    const int right = std::min(c->W - 1, std::max(c->cropPx.right(), int(std::ceil(*hi + m))));
    c->cropPx = QRect(QPoint(left, c->cropPx.top()), QPoint(right, c->cropPx.bottom()));
}

void readCamera(const QJsonObject &root, const QJsonObject &an, qint64 t0, bool dtl, PictureCameraDoc *c)
{
    c->dtl = dtl;
    const st::StillStream stream = st::stillStreamOf(root, dtl);
    c->present = stream.ok;
    if (!c->present) return;
    c->W = stream.width;
    c->H = stream.height;
    if (dtl && (c->W <= 0 || c->H <= 0)) {
        const QJsonObject cd = an.value(QStringLiteral("clubDtl")).toObject();
        c->W = cd.value(QStringLiteral("frameWidth")).toInt();
        c->H = cd.value(QStringLiteral("frameHeight")).toInt();
    }
    const st::PoseTrackLite pose = st::readPoseTrack(an, dtl, t0);
    if (c->W > 0 && c->H > 0)
        c->cropPx = st::stillCropPixels(st::stillGolferCrop(pose, st::swingBodyBand(pose, an, t0)), c->W, c->H);
    for (int k = 0; k < kPicturePositions; ++k) {
        const std::optional<qint64> at = st::positionInstantUs(an, QString::fromLatin1(kTags[k]), t0);
        if (!at) continue;
        c->posUs[size_t(k)] = *at;
        const int idx = st::nearestFrame(stream.tUs, *at);
        c->frameIndex[size_t(k)] = idx;
        if (idx >= 0) c->frameUs[size_t(k)] = stream.tUs[idx];
    }
    if (c->posUs[0] >= 0) c->addressKp = nearestPose(pose, c->posUs[0], kAddressWindowUs);

    const QJsonObject am = an.value(QStringLiteral("addressMarks")).toObject();
    readView(am.value(dtl ? QStringLiteral("dtl") : QStringLiteral("faceOn")).toObject(), c);
    if (!dtl) { widenToMarks(c); return; }

    // the DTL ball and the club nearest address, through the replay payload's ONE translator
    const qint64 impactUs = c->posUs[2];
    const QVariantMap det = pinpoint::dtlOverlayDetail(QVariantMap{}, an.value(QStringLiteral("clubDtl")).toObject(), QVariantList{}, impactUs,
                                                       [t0](const QJsonValue &v) { return st::relUs(qint64(v.toDouble()), t0); });
    const QVariantList balls = det.value(QStringLiteral("ball")).toMap().value(QStringLiteral("samples")).toList();
    if (!balls.isEmpty()) {
        const QVariantMap b = balls.first().toMap();
        const double bx = b.value(QStringLiteral("x")).toDouble(), by = b.value(QStringLiteral("y")).toDouble();
        if (b.value(QStringLiteral("found")).toBool() && fin(bx) && fin(by)) { c->hasBall = true; c->ball = QPointF(bx, by); }
    }
    const QVariantMap club = det.value(QStringLiteral("club")).toMap();
    if (club.value(QStringLiteral("valid")).toBool() && c->posUs[0] >= 0) {
        // nearest(samples, address, 60 ms), then restsOnMeasurement
        const QVariantList ss = club.value(QStringLiteral("samples")).toList();
        qint64 bestD = std::numeric_limits<qint64>::max();
        QVariantMap best;
        for (const QVariant &sv : ss) {
            const QVariantMap s = sv.toMap();
            const qint64 d = std::llabs(s.value(QStringLiteral("t_us")).toLongLong() - c->posUs[0]);
            if (d <= kAddressWindowUs && d < bestD) { bestD = d; best = s; }
        }
        const int flags = best.value(QStringLiteral("flags")).toInt();
        const QVariantList g = best.value(QStringLiteral("grip")).toList(), h = best.value(QStringLiteral("head")).toList();
        if (!best.isEmpty() && (flags & (kFlagCoasted | kFlagPredicted | kFlagOffFrame | kFlagSynthesized)) == 0 && g.size() == 2 && h.size() == 2) {
            c->hasClub = true;
            c->grip = QPointF(g[0].toDouble(), g[1].toDouble());
            c->head = QPointF(h[0].toDouble(), h[1].toDouble());
        }
    }
    // the butt line at impact (schema /2's dtlPhases)
    for (const QJsonValue &pv : am.value(QStringLiteral("dtlPhases")).toArray()) {
        const QJsonObject p = pv.toObject();
        if (p.value(QStringLiteral("phase")).toInt(-1) != kImpactPhase) continue;
        c->impactButtFound = p.value(QStringLiteral("found")).toBool(false) && fin(numOr(p.value(QStringLiteral("buttX"))));
        c->impactButtX = numOr(p.value(QStringLiteral("buttX")));
        c->impactButtRefused = p.value(QStringLiteral("refused")).toString();
    }
    widenToMarks(c);
}

// THE HEAD TICKS: headLift and headSway as HeadTrackStage builds them (buildHeadSeries, on this document's pose and
// ruler), read at P1 and P7 by the chart's sampler (buildPhaseGrid over the document's ladder) and put back in frame
// px about the address reference: y = addrCentre.y − lift · pxPerCm, x = addrCentre.x + sway · pxPerCm. So the drawn
// (address tick − impact tick) / pxPerCm IS headLift P7 − P1, the chart's head-height cell at P7.
void headTicksOf(const pinpoint::analysis::HeadTrackResult &head, const QJsonObject &an, qint64 t0, double pxPerCm,
                 const pinpoint::analysis::HeadTrackConfig &cfg, PictureDoc *d)
{
    namespace pa = pinpoint::analysis;
    // the ladder, window-relative, for the series' own phase readings and for the sampler
    QJsonArray phases;
    std::vector<pa::PhaseEvent> events;
    for (const QJsonValue &pv : an.value(QStringLiteral("phases")).toArray()) {
        QJsonObject p = pv.toObject();
        if (!p.contains(QStringLiteral("t_us"))) continue;
        const qint64 t = st::relUs(qint64(p.value(QStringLiteral("t_us")).toDouble()), t0);
        p.insert(QStringLiteral("t_us"), t);
        phases.append(p);
        pa::PhaseEvent e;
        e.phase = pa::Phase(p.value(QStringLiteral("phase")).toInt());
        e.t_us = t;
        events.push_back(e);
    }
    // the series as the stage writes them: the curve AND its labelled readings at address, the top and impact (the
    // sampler falls back to a labelled reading where the curve has no sample within its ±15 ms window — the pose is
    // posed every 4th frame at address)
    const std::vector<pa::MetricSeries> series = pa::buildHeadSeries(head, events, head.addrScalePx / cfg.earWidthMm);
    QJsonArray metrics;
    for (const pa::MetricSeries &m : series) {
        if (m.key != QLatin1String("headLift") && m.key != QLatin1String("headSway")) continue;
        QJsonArray t, v, ps;
        for (size_t i = 0; i < m.t_us.size() && i < m.value.size(); ++i) { t.append(qint64(m.t_us[i])); v.append(m.value[i]); }
        for (const pa::PhaseSample &q : m.phaseSamples)
            ps.append(QJsonObject{ { QStringLiteral("phase"), int(q.phase) }, { QStringLiteral("t_us"), qint64(q.t_us) }, { QStringLiteral("value"), q.value } });
        metrics.append(QJsonObject{ { QStringLiteral("key"), m.key }, { QStringLiteral("unit"), m.unit },
                                    { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v }, { QStringLiteral("phaseSamples"), ps } });
    }
    const pa::SwingPhaseGrid grid = pa::buildPhaseGrid(QJsonObject{ { QStringLiteral("phases"), phases }, { QStringLiteral("metrics"), metrics } },
                                                       pa::PhaseGridConfig{});
    const pa::MetricPhaseGrid *lift = grid.metric(QStringLiteral("headLift"));
    const pa::MetricPhaseGrid *sway = grid.metric(QStringLiteral("headSway"));
    const pa::PhaseGridValue *la = lift ? lift->at(pa::Phase::Address) : nullptr;
    const pa::PhaseGridValue *li = lift ? lift->at(pa::Phase::Impact) : nullptr;
    const pa::PhaseGridValue *sa = sway ? sway->at(pa::Phase::Address) : nullptr;
    const pa::PhaseGridValue *si = sway ? sway->at(pa::Phase::Impact) : nullptr;
    if (!la || !li || !fin(la->value) || !fin(li->value)) {
        d->headTicksWhy = QStringLiteral("the head was not tracked at address and impact");
        return;
    }
    const QPointF c = head.addrCenterPx;
    const auto at = [&](const pa::PhaseGridValue *l, const pa::PhaseGridValue *s) {
        return QPointF(c.x() + (s && fin(s->value) ? s->value : 0.0) * pxPerCm, c.y() - l->value * pxPerCm);
    };
    d->headTicks = true;
    d->headLiftAddrCm = la->value;
    d->headLiftImpactCm = li->value;
    d->headAddrPx = at(la, sa);
    d->headImpactPx = at(li, si);
}

} // namespace

QString postureHeadRiseWords(double cm)
{
    if (!fin(cm)) return QString();
    if (std::fabs(cm) < 0.05) return QStringLiteral("level with address");
    return QStringLiteral("%1 cm %2").arg(QString::number(std::fabs(cm), 'f', 1), cm > 0 ? QStringLiteral("higher") : QStringLiteral("lower"));
}

QString postureButtTravelWords(double cm)
{
    if (!fin(cm)) return QString();
    if (std::fabs(cm) < 0.05) return QStringLiteral("level with address");
    return QStringLiteral("%1 cm %2").arg(QString::number(std::fabs(cm), 'f', 1),
                                          cm > 0 ? QStringLiteral("toward the ball") : QStringLiteral("back from the ball"));
}

PictureDoc postureDocFrom(const QJsonObject &root, const QString &swingDir, const QString &stamp)
{
    PictureDoc d;
    d.swingDir = swingDir;
    d.stamp = stamp;
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    if (root.isEmpty() || an.isEmpty()) { d.why = QStringLiteral("the swing's document has no analysis"); return d; }
    d.ok = true;
    const qint64 t0 = qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
    d.leadIsLeft = st::leadIsLeftFor(root.value(QStringLiteral("athlete")).toObject().value(QStringLiteral("handedness")).toString());
    const QJsonObject am = an.value(QStringLiteral("addressMarks")).toObject();
    d.marksVersion = am.isEmpty() ? 0 : std::max(1, am.value(QStringLiteral("stageVersion")).toInt(1));

    readCamera(root, an, t0, true, &d.dtl);
    readCamera(root, an, t0, false, &d.faceOn);

    // the DTL ruler pelvisThrust was measured in (analysis.dtlRuler)
    if (an.contains(QStringLiteral("dtlRuler"))) {
        const QJsonObject r = an.value(QStringLiteral("dtlRuler")).toObject();
        const double c = numOr(r.value(QStringLiteral("cmPerPx")));
        d.dtlToward = r.value(QStringLiteral("toward")).toInt(0);
        if (c > 0 && fin(c) && (d.dtlToward == 1 || d.dtlToward == -1)) d.dtlCmPerPx = c;
        else {
            const QString why = r.value(QStringLiteral("refused")).toString().trimmed().isEmpty()
                                    ? r.value(QStringLiteral("reason")).toString().trimmed() : r.value(QStringLiteral("refused")).toString().trimmed();
            d.dtlRulerWhy = c > 0 ? QStringLiteral("the ball's side was not decided")
                                  : why.isEmpty() ? QStringLiteral("no ball to measure with in either view") : why;
        }
    } else {
        d.dtlRulerWhy = QStringLiteral("this swing was analysed before the down-the-line ruler was kept");
    }

    // the face-on inter-ear ruler (HeadTrackStage: addrScalePx / head.earWidthMm, mm → cm)
    if (d.faceOn.present && d.faceOn.W > 0 && d.faceOn.H > 0) {
        const pinpoint::analysis::HeadTrackConfig cfg;
        const pinpoint::analysis::PoseTrack2D pose = poseTrackOf(an.value(QStringLiteral("pose2d")).toObject(), t0);
        const pinpoint::analysis::HeadTrackResult head = pinpoint::analysis::trackHead(pose, d.faceOn.W, d.faceOn.H, d.faceOn.posUs[0], cfg);
        if (head.valid && head.addrScalePx > 0.0 && cfg.earWidthMm > 0.0) {
            d.headPxPerCm = 10.0 * head.addrScalePx / cfg.earWidthMm;
            headTicksOf(head, an, t0, d.headPxPerCm, cfg, &d);
        } else {
            d.headRulerWhy = QStringLiteral("the ears were not seen at address, so the head has no ruler");
        }
    } else {
        d.headRulerWhy = QStringLiteral("no face-on frame size");
    }
    return d;
}

PictureCameraDoc pictureCameraDocFrom(const QJsonObject &root, bool dtl)
{
    PictureCameraDoc c;
    c.dtl = dtl;
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    if (root.isEmpty() || an.isEmpty()) return c;
    const qint64 t0 = qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
    readCamera(root, an, t0, dtl, &c);
    return c;
}

QVariantMap posturePictureUnread(const QString &swingDir, const QString &why)
{
    return { { QStringLiteral("available"), false }, { QStringLiteral("swingDir"), swingDir },
             { QStringLiteral("groups"), QVariantList{} }, { QStringLiteral("legend"), QString() },
             { QStringLiteral("why"), QStringList{ sentence(QStringLiteral("no picture: ") + why) } },
             { QStringLiteral("pending"), false }, { QStringLiteral("typical"), QString() },
             { QStringLiteral("defaultSets"), QVariantMap{ { QStringLiteral("posture"), true }, { QStringLiteral("plane"), false },
                                                           { QStringLiteral("headHips"), true } } } };
}

QVariantMap posturePicture(const PictureDoc &doc, const PictureBands &bands, const PictureOptions &opt)
{
    if (!doc.ok) return posturePictureUnread(doc.swingDir, doc.why.isEmpty() ? QStringLiteral("the swing's document could not be read") : doc.why);
    QStringList why;
    if (doc.marksVersion == 0)
        why << QStringLiteral("This swing was analysed before the address marks were read: the butt line and the hip edges "
                              "are not drawn. Re-analyse it to add them.");
    else if (doc.marksVersion < 3 && doc.dtl.present)
        why << QStringLiteral("This swing was analysed before the butt line was read at each position: your butt line at impact is not "
                              "drawn. Re-analyse it to add it.");
    if (!doc.dtl.present) why << QStringLiteral("No down-the-line camera on this swing: the face-on frames only.");
    if (!doc.faceOn.present) why << QStringLiteral("No face-on camera on this swing: the down-the-line frames only.");
    if (doc.faceOn.present && doc.faceOn.marksFound && !doc.faceOn.headFound) {
        if (doc.marksVersion < 4)
            why << QStringLiteral("This swing was analysed before the head's outline was read: the head circle is drawn from the face points, "
                                  "which sit low when you look down at the ball. Re-analyse it to fit the circle to your head.");
        else
            why << QStringLiteral("The person mask did not find the top of your head at address (%1): the head circle is drawn from the face "
                                  "points instead.").arg(doc.faceOn.headRefused.isEmpty() ? QStringLiteral("no reason recorded") : doc.faceOn.headRefused);
    }
    for (const PictureCameraDoc *c : { &doc.dtl, &doc.faceOn })
        if (c->present && doc.marksVersion > 0 && !c->marksFound)
            why << QStringLiteral("The person mask found no body at address %1: its %2 not drawn%3.")
                       .arg(c->dtl ? QStringLiteral("down the line") : QStringLiteral("face-on"),
                            c->dtl ? QStringLiteral("butt line is") : QStringLiteral("hip edges are"),
                            c->dtl ? QString() : QStringLiteral(", and the head circle is drawn from the face points"));

    bool anyBand = false, anyInk = false, anyHeadInk = false, anyFrame = false, pending = false;
    QVariantList groups;
    for (const PictureCameraDoc *cp : { &doc.dtl, &doc.faceOn }) {
        const PictureCameraDoc &c = *cp;
        if (!c.present) continue;
        const QString camId = c.dtl ? QStringLiteral("dtl") : QStringLiteral("faceOn");
        const bool sized = c.W > 0 && c.H > 0 && c.cropPx.isValid() && c.cropPx.width() >= 8 && c.cropPx.height() >= 8;
        const Px px{ double(c.W), double(c.H), double(c.cropPx.x()), double(c.cropPx.y()) };
        const QVector<Mark> coach = coachMarks(c, doc.leadIsLeft);
        QVariantList frames;
        for (int k = 0; k < kPicturePositions; ++k) {
            const QString tag = QString::fromLatin1(kTags[k]);
            QVariantMap f{ { QStringLiteral("key"), camId + QLatin1Char('_') + tag }, { QStringLiteral("camera"), camId },
                           { QStringLiteral("tag"), tag }, { QStringLiteral("pos"), kPosIndex[k] },
                           { QStringLiteral("when"), QString::fromLatin1(kWhen[k]) },
                           { QStringLiteral("tUs"), qlonglong(c.posUs[size_t(k)]) }, { QStringLiteral("seekUs"), qlonglong(c.posUs[size_t(k)]) },
                           { QStringLiteral("frameUs"), qlonglong(c.frameUs[size_t(k)]) }, { QStringLiteral("frameIndex"), c.frameIndex[size_t(k)] },
                           { QStringLiteral("frameWidth"), c.W }, { QStringLiteral("frameHeight"), c.H },
                           { QStringLiteral("width"), sized ? c.cropPx.width() : 0 }, { QStringLiteral("height"), sized ? c.cropPx.height() : 0 },
                           { QStringLiteral("crop"), QVariantMap{ { QStringLiteral("x"), c.cropPx.x() }, { QStringLiteral("y"), c.cropPx.y() },
                                                                  { QStringLiteral("w"), c.cropPx.width() }, { QStringLiteral("h"), c.cropPx.height() } } } };
            QString caption = QString::fromLatin1(kWhen[k]);
            QStringList notes;
            QVariantList marks;
            QString imageWhy;
            if (!sized) imageWhy = QStringLiteral("The frame's size is not recorded on this swing.");
            else if (c.posUs[size_t(k)] < 0) imageWhy = QStringLiteral("This swing has no %1 position.").arg(tag);
            else if (c.frameIndex[size_t(k)] < 0) imageWhy = QStringLiteral("The video has no frames.");
            if (sized) {
                for (const Mark &m : coach) marks.append(markMap(m, px, QStringLiteral("coach")));
                if (k == 2 && c.dtl) {
                    // the strip in front of the address butt line, and the golfer's own butt line at impact
                    const bool butt = c.marksFound && fin(c.buttX);
                    if (butt && bands.hipHas && fin(doc.dtlCmPerPx) && fin(bands.hipLo) && fin(bands.hipHi)) {
                        const double xa = c.buttX * c.W + doc.dtlToward * bands.hipLo / doc.dtlCmPerPx - px.ox;
                        const double xb = c.buttX * c.W + doc.dtlToward * bands.hipHi / doc.dtlCmPerPx - px.ox;
                        marks.prepend(rectMark(std::min(xa, xb), 0.0, std::fabs(xb - xa), double(c.cropPx.height()),
                                               QStringLiteral("good players' hips at impact"), bands.hipHueKey));
                        caption = QStringLiteral("impact: good players' hips sit in the shaded strip");
                        anyBand = true;
                    } else if (butt && bands.hipHas) {
                        notes << QStringLiteral("No strip: no ruler — %1").arg(doc.dtlRulerWhy.isEmpty() ? QStringLiteral("not measured") : doc.dtlRulerWhy);
                    }
                    if (c.impactButtFound) {
                        Mark m; m.kind = QStringLiteral("vline"); m.x = c.impactButtX; m.label = QStringLiteral("your butt line at impact");
                        marks.append(markMap(m, px, QStringLiteral("ink")));
                        anyInk = true;
                        double cm = kPicNaN;
                        if (butt && fin(doc.dtlCmPerPx)) {
                            pinpoint::analysis::addressmarks::ViewMarks at, addr;
                            at.buttX = c.impactButtX;
                            addr.buttX = c.buttX;
                            cm = pinpoint::analysis::addressmarks::buttShiftCm(at, addr, c.W, double(doc.dtlToward), doc.dtlCmPerPx);
                        }
                        notes << (fin(cm) ? QStringLiteral("The white line: your butt line at impact, %1").arg(postureButtTravelWords(cm))
                                          : QStringLiteral("The white line: your butt line at impact"));
                    } else if (doc.marksVersion >= 3 && !c.impactButtRefused.isEmpty()) {
                        notes << QStringLiteral("Your butt line at impact was not read: %1").arg(c.impactButtRefused);
                    }
                } else if (k == 2 && !c.dtl) {
                    // the band hung from the tick at the tracked head centre at address; the head at impact in ink
                    Mark head;
                    const bool circle = pictureHeadCircle(c, &head);
                    const double rPx = circle ? head.r * c.H : (fin(doc.headPxPerCm) ? 7.5 * doc.headPxPerCm : 0.0);
                    if (doc.headTicks && fin(doc.headPxPerCm) && rPx > 0) {
                        const double ax = doc.headAddrPx.x() - px.ox, ay = doc.headAddrPx.y() - px.oy;
                        const double ix = doc.headImpactPx.x() - px.ox, iy = doc.headImpactPx.y() - px.oy;
                        const double half = 1.5 * rPx;
                        if (bands.headHas && fin(bands.headLo) && fin(bands.headHi)) {
                            const double ya = ay - bands.headHi * doc.headPxPerCm, yb = ay - bands.headLo * doc.headPxPerCm;   // + = up
                            marks.prepend(rectMark(ax - half, std::min(ya, yb), 2 * half, std::fabs(yb - ya),
                                                   QStringLiteral("good players' heads at impact"), bands.headHueKey));
                            caption = QStringLiteral("impact: good players' heads sit in the shaded band");
                            anyBand = true;
                        }
                        marks.append(tickMark(ax - half, ax + half, ay, QStringLiteral("your head at address"), QStringLiteral("band"), bands.headHueKey));
                        marks.append(tickMark(ix - rPx, ix + rPx, iy, QStringLiteral("your head at impact"), QStringLiteral("ink"), QString()));
                        anyHeadInk = true;
                        notes << QStringLiteral("Your head at impact (the white tick): %1")
                                     .arg(postureHeadRiseWords(doc.headLiftImpactCm - doc.headLiftAddrCm));
                    } else if (bands.headHas && !fin(doc.headPxPerCm)) {
                        notes << QStringLiteral("No band: %1").arg(doc.headRulerWhy);
                    } else if (bands.headHas) {
                        notes << QStringLiteral("No band: %1").arg(doc.headTicksWhy.isEmpty() ? QStringLiteral("the head was not tracked") : doc.headTicksWhy);
                    }
                }
            }
            f.insert(QStringLiteral("caption"), caption);
            QStringList ns;
            for (const QString &n : std::as_const(notes)) ns << sentence(n);
            f.insert(QStringLiteral("note"), ns.join(QLatin1Char(' ')));
            f.insert(QStringLiteral("marks"), marks);
            // the still: what to make, and where it is
            QString outPath, src;
            if (imageWhy.isEmpty() && !opt.stillDir.isEmpty()) {
                const QString key = QStringLiteral("%1|%2|%3|%4|%5,%6,%7,%8|%9").arg(QDir::cleanPath(doc.swingDir), doc.stamp, camId, tag)
                                        .arg(c.cropPx.x()).arg(c.cropPx.y()).arg(c.cropPx.width()).arg(c.cropPx.height()).arg(opt.maxPx);
                outPath = QDir(opt.stillDir).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(20))
                                                      + QStringLiteral(".jpg"));
                if (QFileInfo::exists(outPath)) src = QUrl::fromLocalFile(outPath).toString();
                else if (opt.stillErrors.contains(outPath)) imageWhy = sentence(QStringLiteral("the frame could not be read: ") + opt.stillErrors.value(outPath));
                else pending = true;
            }
            f.insert(QStringLiteral("src"), src);
            f.insert(QStringLiteral("imageWhy"), imageWhy);
            f.insert(QStringLiteral("still"), imageWhy.isEmpty() || !outPath.isEmpty()
                ? QVariant(QVariantMap{ { QStringLiteral("swingDir"), doc.swingDir }, { QStringLiteral("camera"), camId }, { QStringLiteral("phase"), tag },
                                        { QStringLiteral("x"), c.cropPx.x() }, { QStringLiteral("y"), c.cropPx.y() },
                                        { QStringLiteral("w"), c.cropPx.width() }, { QStringLiteral("h"), c.cropPx.height() },
                                        { QStringLiteral("maxPx"), opt.maxPx }, { QStringLiteral("outPath"), outPath } })
                : QVariant::fromValue(nullptr));
            anyFrame = anyFrame || sized;
            frames.append(f);
        }
        groups.append(QVariantMap{ { QStringLiteral("camera"), camId },
                                   { QStringLiteral("label"), c.dtl ? QStringLiteral("down the line") : QStringLiteral("face-on") },
                                   { QStringLiteral("frames"), frames } });
    }
    QString legend = QStringLiteral("Thin lines: your lines at address, frozen on every frame.");
    if (anyBand) legend += QStringLiteral(" Shaded: where good players are at impact (the %1 band).")
                               .arg(bands.patternName.isEmpty() ? QStringLiteral("pattern's") : bands.patternName);
    if (anyInk) legend += QStringLiteral(" The white line: your butt line at impact.");
    if (anyHeadInk) legend += QStringLiteral(" The white tick: your head at impact, against the tick at your head at address.");
    return { { QStringLiteral("available"), anyFrame }, { QStringLiteral("swingDir"), doc.swingDir }, { QStringLiteral("ordinal"), opt.ordinal },
             { QStringLiteral("typical"), QString() }, { QStringLiteral("legend"), anyFrame ? legend : QString() },
             { QStringLiteral("why"), why }, { QStringLiteral("pending"), pending }, { QStringLiteral("groups"), groups },
             { QStringLiteral("defaultSets"), QVariantMap{ { QStringLiteral("posture"), true }, { QStringLiteral("plane"), false },
                                                           { QStringLiteral("headHips"), true } } } };
}

} // namespace pinpoint::charts
