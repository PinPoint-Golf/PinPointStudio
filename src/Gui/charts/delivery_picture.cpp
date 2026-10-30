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

#include "delivery_picture.h"

#include "posture_picture.h"                // the DTL frames, crop, ball and address grip: the Posture picture's reading
#include "../report/report_stills_pure.h"   // which frame: the report stills' own rule
#include "../shot/dtl_overlay_payload.h"    // the DTL club samples, as the replay payload carries them
#include "../../Club/shaft_plane_curve.h"   // shoulderPlaneFromHold — the shoulderPlane metric's own point

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace pinpoint::charts {

namespace st = pinpoint::report::stills;

namespace {

const char *const kTags[kDeliveryPictureFrames] = { "P1", "P4", "P6" };
const int kPosIndex[kDeliveryPictureFrames] = { 0, 3, 5 };
const char *const kWhen[kDeliveryPictureFrames] = { "address", "the top", "P6" };

// ShaftFusionStage's shoulder-plane window: the raw DTL pose before P1 − 100 ms (the ball finder's), joints above 0.3.
constexpr qint64 kHoldEndBeforeAddressUs = 100000;
constexpr float  kShoulderConf = 0.3f;
// The crop takes in the ball with the stills' body margin (a fraction of the crop's height).
constexpr double kBallMargin = 0.06;
// How far past the frame the slot's wash is drawn (in the line's own length from the ball): the frame clips it.
constexpr double kWashReach = 40.0;

bool fin(double v) { return std::isfinite(v); }

// AutoAnnotations.js's address window and the flags restsOnMeasurement refuses.
constexpr qint64 kAddressWindowUs = 60000;
constexpr int kFlagCoasted = 0x04, kFlagPredicted = 0x20, kFlagOffFrame = 0x80, kFlagSynthesized = 0x100;

// The address grip: the MEASURED down-the-line club sample nearest address within 60 ms, through the replay payload's
// one translator. The replay's own rule (nearest, then refuse it if it is not a measurement) leaves no line when a held
// frame happens to sit nearest P1 (Mark 5 Oct swing 7: a HELD frame 0.8 ms after P1, a RAY 5.7 ms before it); the line
// is the shaft at address, so the picture takes the nearest measurement instead. The same sample when the nearest is one.
bool addressClubOf(const QJsonObject &an, qint64 t0, qint64 addrUs, qint64 impactUs, QPointF *grip, QPointF *head)
{
    if (addrUs < 0) return false;
    const QVariantMap det = pinpoint::dtlOverlayDetail(QVariantMap{}, an.value(QStringLiteral("clubDtl")).toObject(), QVariantList{}, impactUs,
                                                       [t0](const QJsonValue &v) { return st::relUs(qint64(v.toDouble()), t0); });
    const QVariantMap club = det.value(QStringLiteral("club")).toMap();
    if (!club.value(QStringLiteral("valid")).toBool()) return false;
    qint64 bestD = std::numeric_limits<qint64>::max();
    bool found = false;
    for (const QVariant &sv : club.value(QStringLiteral("samples")).toList()) {
        const QVariantMap s = sv.toMap();
        const qint64 dt = std::llabs(s.value(QStringLiteral("t_us")).toLongLong() - addrUs);
        if (dt > kAddressWindowUs || dt >= bestD) continue;
        if (s.value(QStringLiteral("flags")).toInt() & (kFlagCoasted | kFlagPredicted | kFlagOffFrame | kFlagSynthesized)) continue;
        const QVariantList g = s.value(QStringLiteral("grip")).toList(), h = s.value(QStringLiteral("head")).toList();
        if (g.size() != 2 || h.size() != 2) continue;
        bestD = dt;
        *grip = QPointF(g[0].toDouble(), g[1].toDouble());
        *head = QPointF(h[0].toDouble(), h[1].toDouble());
        found = true;
    }
    return found;
}

QString sentence(QString s)
{
    s = s.trimmed();
    if (s.isEmpty()) return s;
    s[0] = s[0].toUpper();
    if (!s.endsWith(QLatin1Char('.'))) s += QLatin1Char('.');
    return s;
}

// The line through p and q clipped to the unit square (posture_picture.cpp's extendToSquare, the replay's).
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

// The document's shoulderPlane reading (the phase sample at address), NaN when the swing carries none.
double shoulderMetricOf(const QJsonObject &an)
{
    for (const QJsonValue &mv : an.value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject m = mv.toObject();
        if (m.value(QStringLiteral("key")).toString() != QLatin1String("shoulderPlane")) continue;
        for (const QJsonValue &pv : m.value(QStringLiteral("phaseSamples")).toArray()) {
            const QJsonValue v = pv.toObject().value(QStringLiteral("value"));
            if (v.isDouble() && fin(v.toDouble())) return v.toDouble();
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// The trail shoulder over the address hold, exactly as ShaftFusionStage reads it for shoulderPlane: the raw DTL pose
// frames (not the smoothed track), on the face-on clock (− clubDtl.clockOffsetUs), before P1 − 100 ms, the trail
// shoulder above 0.3, in the club track's pixels, the last 15 by shoulderPlaneFromHold's per-axis median.
void shoulderOf(const QJsonObject &an, qint64 t0, qint64 addrUs, bool leadIsLeft, DeliveryPictureDoc *d)
{
    const QJsonObject cd = an.value(QStringLiteral("clubDtl")).toObject();
    const double fw = cd.value(QStringLiteral("frameWidth")).toDouble(d->W);
    const double fh = cd.value(QStringLiteral("frameHeight")).toDouble(d->H);
    const qint64 offset = qint64(cd.value(QStringLiteral("clockOffsetUs")).toDouble(0));
    const QJsonArray frames = an.value(QStringLiteral("poseDtl")).toObject().value(QStringLiteral("frames")).toArray();
    if (frames.isEmpty() || !(fw > 0) || !(fh > 0)) { d->shoulderWhy = QStringLiteral("the down-the-line pose was not kept on this swing"); return; }
    const int j = leadIsLeft ? 6 : 5;   // planecurve::trailShoulderJoint: the right shoulder for a right-handed golfer
    std::vector<std::pair<qint64, std::pair<double, double>>> seen;   // the confident frames, on the face-on clock
    for (const QJsonValue &fv : frames) {
        const QJsonObject f = fv.toObject();
        const QJsonArray kp = f.value(QStringLiteral("kp")).toArray();
        if (kp.size() < 3 * (j + 1) || !(kp.at(3 * j + 2).toDouble() > double(kShoulderConf))) continue;
        seen.push_back({ st::relUs(qint64(f.value(QStringLiteral("t_us")).toDouble()), t0) - offset,
                         { kp.at(3 * j).toDouble(), kp.at(3 * j + 1).toDouble() } });
    }
    std::stable_sort(seen.begin(), seen.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::pair<double, double>> hold;
    for (const auto &s : seen) {
        if (s.first >= addrUs - kHoldEndBeforeAddressUs) break;
        hold.push_back({ s.second.first * fw, s.second.second * fh });
    }
    namespace pa = pinpoint::analysis;
    // the camera only turns the point into degrees, which the picture does not draw: the point is the camera's own
    const pa::planecurve::ShoulderPlaneReading r = pa::planecurve::shoulderPlaneFromHold(pa::fusion::dtlCamera(0.0, 0.0, 0.0), hold,
                                                                                         d->ball.x() * fw, d->ball.y() * fh);
    d->shoulderFrames = r.n;
    if (!fin(r.sx) || !fin(r.sy)) {
        d->shoulderWhy = QStringLiteral("your trail shoulder was not seen over the address hold");
        return;
    }
    d->hasShoulder = true;
    d->shoulder = QPointF(r.sx / fw, r.sy / fh);
}

// Frame px → the crop's own px.
struct Px {
    double W, H, ox, oy;
    QPointF at(const QPointF &n) const { return QPointF(n.x() * W - ox, n.y() * H - oy); }
};

QVariantList ptsList(const QVector<QPointF> &pts)
{
    QVariantList out;
    for (const QPointF &p : pts) out.append(QVariant(QVariantList{ p.x(), p.y() }));
    return out;
}

QVariantMap markOf(const QString &kind, const QString &label, const QString &dash, const QVector<QPointF> &normPts, const Px &px)
{
    QVector<QPointF> p;
    for (const QPointF &q : normPts) p << px.at(q);
    return { { QStringLiteral("kind"), kind }, { QStringLiteral("role"), QStringLiteral("coach") }, { QStringLiteral("set"), QStringLiteral("plane") },
             { QStringLiteral("label"), label }, { QStringLiteral("dash"), dash }, { QStringLiteral("hueKey"), QString() },
             { QStringLiteral("pts"), ptsList(p) } };
}

QVariantMap defaultSets() { return { { QStringLiteral("plane"), true } }; }

} // namespace

DeliveryPictureDoc deliveryPictureDocFrom(const QJsonObject &root, const QString &swingDir, const QString &stamp)
{
    DeliveryPictureDoc d;
    d.swingDir = swingDir;
    d.stamp = stamp;
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    if (root.isEmpty() || an.isEmpty()) { d.why = QStringLiteral("the swing's document has no analysis"); return d; }
    d.ok = true;
    const PictureCameraDoc cam = pictureCameraDocFrom(root, true);
    d.dtl = cam.present;
    if (!d.dtl) return d;
    const qint64 t0 = qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble());
    const bool leadIsLeft = st::leadIsLeftFor(root.value(QStringLiteral("athlete")).toObject().value(QStringLiteral("handedness")).toString());
    d.W = cam.W;
    d.H = cam.H;
    d.cropPx = cam.cropPx;
    d.hasBall = cam.hasBall;  d.ball = cam.ball;

    // the positions' frames: address, the top and P6, by the stills' rule
    const st::StillStream stream = st::stillStreamOf(root, true);
    for (int k = 0; k < kDeliveryPictureFrames; ++k) {
        const std::optional<qint64> at = st::positionInstantUs(an, QString::fromLatin1(kTags[k]), t0);
        if (!at) continue;
        d.posUs[size_t(k)] = *at;
        const int idx = st::nearestFrame(stream.tUs, *at);
        d.frameIndex[size_t(k)] = idx;
        if (idx >= 0) d.frameUs[size_t(k)] = stream.tUs[idx];
    }

    // the address grip: the measured club sample nearest address
    d.hasClub = addressClubOf(an, t0, d.posUs[0], cam.posUs[2], &d.grip, &d.head);

    // the shoulder plane: the document's metric, through the point it was measured on
    d.shoulderDeg = shoulderMetricOf(an);
    d.shoulderMetric = fin(d.shoulderDeg);
    if (!d.hasBall) d.shoulderWhy = QStringLiteral("the ball was not found down the line");
    else if (!d.shoulderMetric) d.shoulderWhy = QStringLiteral("this swing was analysed before the shoulder plane was measured");
    else if (d.posUs[0] < 0) d.shoulderWhy = QStringLiteral("this swing has no address position");
    else shoulderOf(an, t0, d.posUs[0], leadIsLeft, &d);

    // the crop takes in the ball (every line starts there)
    if (d.hasBall && d.cropPx.isValid() && d.W > 0 && d.H > 0) {
        const double m = kBallMargin * d.cropPx.height();
        const double bx = d.ball.x() * d.W, by = d.ball.y() * d.H;
        const int left = std::max(0, std::min(d.cropPx.left(), int(std::floor(bx - m))));
        const int right = std::min(d.W - 1, std::max(d.cropPx.right(), int(std::ceil(bx + m))));
        const int top = std::max(0, std::min(d.cropPx.top(), int(std::floor(by - m))));
        const int bottom = std::min(d.H - 1, std::max(d.cropPx.bottom(), int(std::ceil(by + m))));
        d.cropPx = QRect(QPoint(left, top), QPoint(right, bottom));
    }
    return d;
}

QVariantMap deliveryPictureUnread(const QString &swingDir, const QString &why)
{
    return { { QStringLiteral("available"), false }, { QStringLiteral("swingDir"), swingDir },
             { QStringLiteral("groups"), QVariantList{} }, { QStringLiteral("legend"), QString() },
             { QStringLiteral("why"), QStringList{ sentence(QStringLiteral("no picture: ") + why) } },
             { QStringLiteral("pending"), false }, { QStringLiteral("defaultSets"), defaultSets() },
             { QStringLiteral("lines"), QVariantMap{} } };
}

QVariantMap deliveryPicture(const DeliveryPictureDoc &doc, const DeliveryPictureOptions &opt)
{
    if (!doc.ok) return deliveryPictureUnread(doc.swingDir, doc.why.isEmpty() ? QStringLiteral("the swing's document could not be read") : doc.why);
    // no down-the-line camera: nothing at all (the section says why the plane is absent, once)
    if (!doc.dtl)
        return { { QStringLiteral("available"), false }, { QStringLiteral("swingDir"), doc.swingDir }, { QStringLiteral("ordinal"), opt.ordinal },
                 { QStringLiteral("groups"), QVariantList{} }, { QStringLiteral("legend"), QString() }, { QStringLiteral("why"), QStringList{} },
                 { QStringLiteral("pending"), false }, { QStringLiteral("defaultSets"), defaultSets() }, { QStringLiteral("lines"), QVariantMap{} } };

    // the lines, normalised in the full frame
    QVector<QPointF> addressLine, shoulderLine, wash;
    QString addressLabel;
    if (doc.hasClub && doc.hasBall) {
        QPointF a, b;
        if (extendToSquare(doc.ball, doc.grip, &a, &b)) { addressLine = { doc.ball, b }; addressLabel = QStringLiteral("address line"); }
    } else if (doc.hasClub) {
        QPointF a, b;
        if (extendToSquare(doc.grip, doc.head, &a, &b)) { addressLine = { a, b }; addressLabel = QStringLiteral("address shaft line"); }
    }
    if (doc.hasBall && doc.hasShoulder) {
        QPointF a, b;
        if (extendToSquare(doc.ball, doc.shoulder, &a, &b)) shoulderLine = { doc.ball, b };
    }
    const bool slot = doc.hasBall && addressLine.size() == 2 && addressLabel == QLatin1String("address line") && shoulderLine.size() == 2;
    if (slot) {
        const QPointF g = doc.grip - doc.ball, s = doc.shoulder - doc.ball;
        wash = { doc.ball, doc.ball + kWashReach * g, doc.ball + kWashReach * s };
    }

    QStringList why;
    if (!doc.hasClub)
        why << QStringLiteral("The club was not measured down the line at address: the address line is not drawn.");
    else if (!doc.hasBall)
        why << QStringLiteral("The ball was not found down the line: the address line is drawn along the shaft, and the shoulder plane "
                              "and the slot are not drawn.");
    if (doc.hasBall && !doc.hasShoulder)
        why << sentence(QStringLiteral("no shoulder plane: ") + (doc.shoulderWhy.isEmpty() ? QStringLiteral("not measured") : doc.shoulderWhy)
                        + (doc.shoulderMetric ? QString() : QStringLiteral(". Re-analyse it to add it")));

    const bool sized = doc.W > 0 && doc.H > 0 && doc.cropPx.isValid() && doc.cropPx.width() >= 8 && doc.cropPx.height() >= 8;
    const Px px{ double(doc.W), double(doc.H), double(doc.cropPx.x()), double(doc.cropPx.y()) };
    bool pending = false;
    QVariantList frames;
    for (int k = 0; k < kDeliveryPictureFrames; ++k) {
        const QString tag = QString::fromLatin1(kTags[k]);
        QVariantMap f{ { QStringLiteral("key"), QStringLiteral("dtl_") + tag }, { QStringLiteral("camera"), QStringLiteral("dtl") },
                       { QStringLiteral("tag"), tag }, { QStringLiteral("pos"), kPosIndex[k] },
                       { QStringLiteral("when"), QString::fromLatin1(kWhen[k]) }, { QStringLiteral("caption"), QString::fromLatin1(kWhen[k]) },
                       { QStringLiteral("note"), QString() },
                       { QStringLiteral("tUs"), qlonglong(doc.posUs[size_t(k)]) }, { QStringLiteral("seekUs"), qlonglong(doc.posUs[size_t(k)]) },
                       { QStringLiteral("frameUs"), qlonglong(doc.frameUs[size_t(k)]) }, { QStringLiteral("frameIndex"), doc.frameIndex[size_t(k)] },
                       { QStringLiteral("frameWidth"), doc.W }, { QStringLiteral("frameHeight"), doc.H },
                       { QStringLiteral("width"), sized ? doc.cropPx.width() : 0 }, { QStringLiteral("height"), sized ? doc.cropPx.height() : 0 },
                       { QStringLiteral("crop"), QVariantMap{ { QStringLiteral("x"), doc.cropPx.x() }, { QStringLiteral("y"), doc.cropPx.y() },
                                                              { QStringLiteral("w"), doc.cropPx.width() }, { QStringLiteral("h"), doc.cropPx.height() } } } };
        QString imageWhy;
        if (!sized) imageWhy = QStringLiteral("The frame's size is not recorded on this swing.");
        else if (doc.posUs[size_t(k)] < 0) imageWhy = QStringLiteral("This swing has no %1 position.").arg(tag);
        else if (doc.frameIndex[size_t(k)] < 0) imageWhy = QStringLiteral("The video has no frames.");
        QVariantList marks;
        if (sized) {
            // the wash under the lines; the address line, then the shoulder plane (dashed, as on the plot)
            if (slot) marks.append(markOf(QStringLiteral("poly"), QStringLiteral("the slot"), QString(), wash, px));
            if (addressLine.size() == 2) marks.append(markOf(QStringLiteral("polyline"), addressLabel, QString(), addressLine, px));
            if (shoulderLine.size() == 2) marks.append(markOf(QStringLiteral("polyline"), QStringLiteral("shoulder plane"), QStringLiteral("dash"), shoulderLine, px));
        }
        f.insert(QStringLiteral("marks"), marks);
        QString outPath, src;
        if (imageWhy.isEmpty() && !opt.stillDir.isEmpty()) {
            const QString key = QStringLiteral("%1|%2|dtl|%3|%4,%5,%6,%7|%8").arg(QDir::cleanPath(doc.swingDir), doc.stamp, tag)
                                    .arg(doc.cropPx.x()).arg(doc.cropPx.y()).arg(doc.cropPx.width()).arg(doc.cropPx.height()).arg(opt.maxPx);
            outPath = QDir(opt.stillDir).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(20))
                                                  + QStringLiteral(".jpg"));
            if (QFileInfo::exists(outPath)) src = QUrl::fromLocalFile(outPath).toString();
            else if (opt.stillErrors.contains(outPath)) imageWhy = sentence(QStringLiteral("the frame could not be read: ") + opt.stillErrors.value(outPath));
            else pending = true;
        }
        f.insert(QStringLiteral("src"), src);
        f.insert(QStringLiteral("imageWhy"), imageWhy);
        f.insert(QStringLiteral("still"), imageWhy.isEmpty() || !outPath.isEmpty()
            ? QVariant(QVariantMap{ { QStringLiteral("swingDir"), doc.swingDir }, { QStringLiteral("camera"), QStringLiteral("dtl") },
                                    { QStringLiteral("phase"), tag }, { QStringLiteral("x"), doc.cropPx.x() }, { QStringLiteral("y"), doc.cropPx.y() },
                                    { QStringLiteral("w"), doc.cropPx.width() }, { QStringLiteral("h"), doc.cropPx.height() },
                                    { QStringLiteral("maxPx"), opt.maxPx }, { QStringLiteral("outPath"), outPath } })
            : QVariant::fromValue(nullptr));
        frames.append(f);
    }

    QStringList parts;
    if (addressLabel == QLatin1String("address line")) parts << QStringLiteral("the solid line runs from the ball up the shaft (your address line)");
    else if (!addressLabel.isEmpty()) parts << QStringLiteral("the solid line runs along the shaft (your address line, without the ball)");
    if (shoulderLine.size() == 2) parts << QStringLiteral("the dashed one from the ball through your trail shoulder (your shoulder plane)");
    QString legend;
    if (!parts.isEmpty()) {
        legend = QStringLiteral("Your lines at address, frozen on every frame: ") + parts.join(QStringLiteral(", ")) + QLatin1Char('.');
        if (slot) legend += QStringLiteral(" The shaded wedge between them is the slot.");
    }
    return { { QStringLiteral("available"), sized }, { QStringLiteral("swingDir"), doc.swingDir }, { QStringLiteral("ordinal"), opt.ordinal },
             { QStringLiteral("legend"), sized ? legend : QString() }, { QStringLiteral("why"), why }, { QStringLiteral("pending"), pending },
             { QStringLiteral("defaultSets"), defaultSets() },
             { QStringLiteral("lines"), QVariantMap{ { QStringLiteral("address"), addressLine.size() == 2 },
                                                     { QStringLiteral("shoulders"), shoulderLine.size() == 2 },
                                                     { QStringLiteral("slot"), slot } } },
             { QStringLiteral("groups"), QVariantList{ QVariantMap{ { QStringLiteral("camera"), QStringLiteral("dtl") },
                                                                    { QStringLiteral("label"), QStringLiteral("down the line") },
                                                                    { QStringLiteral("frames"), frames } } } } };
}

} // namespace pinpoint::charts
