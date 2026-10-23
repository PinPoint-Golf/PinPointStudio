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

// THE PURE PARTS OF THE REPORT STILLS (report_stills_pure.h): no decoder, no OpenCV — Qt Core and JSON only, so
// the Charts Posture picture (charts/posture_picture.h) can say which frame and which crop a still takes without
// linking the renderer. Moved here verbatim from report_stills.cpp on 22 Oct 2026 (posture chart stage 5), with
// the document-level helpers the renderer used privately (the pose read, the body band, the crop, the stream
// pick) made public, so the renderer and the picture share ONE rule.

#include "report_stills_pure.h"

#include "../../Core/pp_tuned_constants.h"     // the per-keypoint pose gate
#include "../markup/markup_truth.h"            // faceOnStreamIndex / streamLooksDownTheLine

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::report::stills {

// ── COCO body keypoints (the first 17 of the 133-point WholeBody) ─────────────────────────────────────
namespace {
constexpr int kNose = 0;
constexpr int kLShoulder = 5, kRShoulder = 6, kLElbow = 7, kRElbow = 8, kLWrist = 9, kRWrist = 10;
constexpr int kLHip = 11, kRHip = 12, kLKnee = 13, kRKnee = 14, kLAnkle = 15, kRAnkle = 16;
constexpr int kBodyKeypoints = 17;
} // namespace

bool leadIsLeftFor(const QString &handedness)
{
    // swing_reanalyzer.cpp reads athlete.handedness the same way; "lead = left unless Left-handed" is the
    // convention every body module shares (pose_runner.cpp `leftLeads = (handedness != 2)`).
    return handedness.trimmed().compare(QLatin1String("Left"), Qt::CaseInsensitive) != 0;
}

Segment segmentFor(const QString &line, bool leadIsLeft)
{
    // Lead/trail → the physical COCO side. A right-handed golfer leads with the LEFT side.
    const int shL = leadIsLeft ? kLShoulder : kRShoulder, shT = leadIsLeft ? kRShoulder : kLShoulder;
    const int elL = leadIsLeft ? kLElbow : kRElbow,       elT = leadIsLeft ? kRElbow : kLElbow;
    const int wrL = leadIsLeft ? kLWrist : kRWrist,       wrT = leadIsLeft ? kRWrist : kLWrist;
    const int hpL = leadIsLeft ? kLHip : kRHip,           hpT = leadIsLeft ? kRHip : kLHip;
    const int knL = leadIsLeft ? kLKnee : kRKnee,         knT = leadIsLeft ? kRKnee : kLKnee;
    const int anL = leadIsLeft ? kLAnkle : kRAnkle,       anT = leadIsLeft ? kRAnkle : kLAnkle;

    Segment s;
    const QString n = line.trimmed();
    auto set = [&](QVector<QVector<int>> j, bool lower, const char *words) {
        s.joints = std::move(j); s.lowerBody = lower; s.words = QString::fromLatin1(words);
    };
    if      (n == QLatin1String("trailThigh"))   set({ { hpT }, { knT } }, true, "trail thigh");
    else if (n == QLatin1String("leadThigh"))    set({ { hpL }, { knL } }, true, "lead thigh");
    else if (n == QLatin1String("trailShin"))    set({ { knT }, { anT } }, true, "trail shin");
    else if (n == QLatin1String("leadShin"))     set({ { knL }, { anL } }, true, "lead shin");
    else if (n == QLatin1String("hips"))         set({ { hpT }, { hpL } }, true, "hips");
    else if (n == QLatin1String("stance"))       set({ { anT }, { anL } }, true, "feet");
    else if (n == QLatin1String("spine"))        set({ { kLHip, kRHip }, { kLShoulder, kRShoulder } }, false, "back");
    else if (n == QLatin1String("shoulders"))    set({ { shT }, { shL } }, false, "shoulders");
    else if (n == QLatin1String("leadArm"))      set({ { shL }, { elL }, { wrL } }, false, "lead arm");
    else if (n == QLatin1String("trailArm"))     set({ { shT }, { elT }, { wrT } }, false, "trail arm");
    else if (n == QLatin1String("leadUpperArm")) set({ { shL }, { elL } }, false, "lead upper arm");
    else if (n == QLatin1String("leadForearm"))  set({ { elL }, { wrL } }, false, "lead forearm");
    else if (n == QLatin1String("head"))         set({ { kLShoulder, kRShoulder }, { kNose } }, false, "head");
    else if (n == QLatin1String("shaft"))        { s.club = true; s.words = QStringLiteral("club"); }
    return s;
}

namespace {
constexpr int kShaftMeasured = 0x01, kShaftWedge = 0x08, kShaftImplausible = 0x200;   // swing_analysis.h ShaftSampleFlags
bool point2(const QJsonValue &v, QPointF *out)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2 || !a.at(0).isDouble() || !a.at(1).isDouble()) return false;
    *out = QPointF(a.at(0).toDouble(), a.at(1).toDouble());
    return std::isfinite(out->x()) && std::isfinite(out->y());
}
} // namespace

QVector<ShaftSample> shaftTrack(const QJsonObject &analysis, bool dtl, qint64 t0)
{
    QVector<ShaftSample> out;
    if (!dtl) {
        const QJsonObject club = analysis.value(QStringLiteral("club")).toObject();
        if (!club.value(QStringLiteral("valid")).toBool(false)) return out;
        for (const QJsonValue &v : club.value(QStringLiteral("samples")).toArray()) {
            const QJsonObject o = v.toObject();
            ShaftSample s;
            s.tUs = relUs(qint64(o.value(QStringLiteral("t_us")).toDouble()), t0);
            const int flags = o.value(QStringLiteral("flags")).toInt();
            const bool havePts = point2(o.value(QStringLiteral("grip")), &s.grip) && point2(o.value(QStringLiteral("head")), &s.head);
            // headConf below the club.headConfidence gate (tuned::clubDelivery::kHeadConfMin, 0.30) is a
            // shaft the tracker itself doubts: on Nick 20 Oct _03 swing_0001 at the top (headConf 0.20) it
            // drew ~15° off the club in the picture. Absent (older swings) = not gated, as the gate does.
            const double headConf = o.value(QStringLiteral("headConf")).toDouble(-1.0);
            s.drawable = havePts && (flags & (kShaftMeasured | kShaftWedge)) && !(flags & kShaftImplausible)
                      && (headConf < 0.0 || headConf >= pinpoint::tuned::clubDelivery::kHeadConfMin);
            out.push_back(s);
        }
    } else {
        for (const QJsonValue &v : analysis.value(QStringLiteral("clubDtl")).toObject().value(QStringLiteral("frames")).toArray()) {
            const QJsonObject o = v.toObject();
            ShaftSample s;
            s.tUs = relUs(qint64(o.value(QStringLiteral("t_us")).toDouble()), t0);
            const QString tier = o.value(QStringLiteral("tier")).toString();
            const bool measured = tier == QLatin1String("RAY") || tier == QLatin1String("SEG") || tier == QLatin1String("BAND");
            s.drawable = measured && point2(o.value(QStringLiteral("grip")), &s.grip) && point2(o.value(QStringLiteral("head")), &s.head);
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const ShaftSample &a, const ShaftSample &b) { return a.tUs < b.tUs; });
    return out;
}

QVector<QPointF> shaftAt(const QVector<ShaftSample> &track, qint64 us, qint64 maxUs)
{
    if (track.isEmpty()) return {};
    auto it = std::lower_bound(track.begin(), track.end(), us, [](const ShaftSample &s, qint64 t) { return s.tUs < t; });
    int best = -1;
    qint64 bestD = std::numeric_limits<qint64>::max();
    for (int i : { int(it - track.begin()) - 1, int(it - track.begin()) })
        if (i >= 0 && i < track.size() && std::llabs(track[i].tUs - us) < bestD) { bestD = std::llabs(track[i].tUs - us); best = i; }
    if (best < 0 || bestD > maxUs || !track[best].drawable) return {};
    return { track[best].grip, track[best].head };
}

QVector<QPointF> segmentPoints(const Segment &seg, const QVector<float> &kp, double confMin, QString *weak)
{
    QVector<QPointF> pts;
    for (const QVector<int> &joint : seg.joints) {
        double x = 0, y = 0;
        for (int k : joint) {
            const int o = 3 * k;
            if (k < 0 || k >= kBodyKeypoints || o + 2 >= kp.size()
                || !(double(kp[o + 2]) >= confMin)
                || !std::isfinite(kp[o]) || !std::isfinite(kp[o + 1])) {
                if (weak) *weak = seg.words;
                return {};
            }
            x += kp[o]; y += kp[o + 1];
        }
        pts.push_back(QPointF(x / joint.size(), y / joint.size()));
    }
    return pts;
}

namespace {
double percentile(QVector<double> v, double q)
{
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = q * (v.size() - 1);
    const int lo = int(std::floor(pos)), hi = std::min(int(v.size()) - 1, lo + 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - lo);
}
} // namespace

std::optional<QRectF> bodyExtent(const QVector<QVector<float>> &frames, double confMin)
{
    QVector<double> x0s, y0s, x1s, y1s;
    for (const QVector<float> &kp : frames) {
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        int n = 0;
        for (int k = 0; k < kBodyKeypoints && 3 * k + 2 < kp.size(); ++k) {
            if (!(double(kp[3 * k + 2]) >= confMin)) continue;
            const double x = kp[3 * k], y = kp[3 * k + 1];
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            ++n;
        }
        if (n < 4) continue;   // a frame that saw only a limb or two says nothing about the extent
        x0s << x0; y0s << y0; x1s << x1; y1s << y1;
    }
    if (x0s.size() < 3) return std::nullopt;
    const double x0 = percentile(x0s, 0.03), y0 = percentile(y0s, 0.03);
    const double x1 = percentile(x1s, 0.97), y1 = percentile(y1s, 0.97);
    if (!(x1 > x0) || !(y1 > y0)) return std::nullopt;
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

namespace {
QRectF grown(const QRectF &r, double mx, double my)
{
    return QRectF(r.x() - mx, r.y() - my, r.width() + 2 * mx, r.height() + 2 * my);
}
QRectF clampedToFrame(const QRectF &r) { return r.intersected(QRectF(0, 0, 1, 1)); }
} // namespace

QRectF golferCrop(const std::optional<QRectF> &cropRect, const std::optional<QRectF> &body,
                  double cropMargin, double bodyMargin, QString *note, QString *source)
{
    const bool haveCrop = cropRect && cropRect->width() > 0 && cropRect->height() > 0;
    const bool haveBody = body && body->width() > 0 && body->height() > 0;
    if (!haveCrop && !haveBody) {
        if (note) *note = QStringLiteral("Framed on the middle of the picture: the golfer's position was not found.");
        if (source) *source = QStringLiteral("centre");
        return QRectF(0.2, 0.0, 0.6, 1.0);
    }
    QRectF out;
    if (haveCrop) {
        out = clampedToFrame(grown(*cropRect, cropMargin * cropRect->width(), cropMargin * cropRect->height()));
        if (source) *source = QStringLiteral("pose");
    } else {
        out = clampedToFrame(grown(*body, cropMargin * body->width(), cropMargin * body->height()));
        if (source) *source = QStringLiteral("body");
    }
    if (haveBody) {
        // THE PRIVACY BOUND. Never wider than the golfer's own body through the swing plus bodyMargin of
        // their height — whichever side the seats are on. Vertically the crop keeps its own margin (the
        // club overhead, the feet and the ball), where nobody sits.
        const double m = bodyMargin * body->height();
        const double bx0 = std::max(0.0, body->left() - m), bx1 = std::min(1.0, body->right() + m);
        const double x0 = std::max(out.left(), bx0), x1 = std::min(out.right(), bx1);
        if (x1 > x0) out = QRectF(x0, out.top(), x1 - x0, out.height());
        // Head to feet are always inside, whatever the crop said (a crop from an older pose pass).
        const double y0 = std::max(0.0, std::min(out.top(), body->top() - m));
        const double y1 = std::min(1.0, std::max(out.bottom(), body->bottom() + m));
        out = QRectF(out.left(), y0, out.width(), y1 - y0);
    }
    return out;
}

QRectF fitAspect(const QRectF &crop, double aspect, double frameAspect, double *padX, double *padY)
{
    if (padX) *padX = 0.0;
    if (padY) *padY = 0.0;
    if (!(aspect > 0.0) || !(frameAspect > 0.0) || crop.height() <= 0 || crop.width() <= 0) return crop;
    const double pxW = crop.width() * frameAspect, pxH = crop.height();   // in frame-height units
    const double have = pxW / pxH;
    if (std::abs(have - aspect) < 1e-6) return crop;
    if (have < aspect) {
        // Too narrow: pad the sides. Taking in more of the frame sideways is exactly what the crop forbids.
        if (padX) *padX = aspect * pxH / pxW - 1.0;
        return crop;
    }
    // Too wide: show more above and below, centred, within the frame; pad what the frame cannot give.
    const double wantH = pxW / aspect;
    double y0 = crop.center().y() - wantH / 2, y1 = crop.center().y() + wantH / 2;
    if (y0 < 0) { y1 = std::min(1.0, y1 - y0); y0 = 0; }
    if (y1 > 1) { y0 = std::max(0.0, y0 - (y1 - 1)); y1 = 1; }
    const QRectF out(crop.left(), y0, crop.width(), y1 - y0);
    if (padY && out.height() < wantH) *padY = wantH / out.height() - 1.0;
    return out;
}

std::array<uint8_t, 256> exposureCurve(const std::array<quint32, 256> &hist)
{
    std::array<uint8_t, 256> lut{};
    for (int v = 0; v < 256; ++v) lut[v] = uint8_t(v);
    quint64 total = 0;
    for (quint32 h : hist) total += h;
    if (total == 0) return lut;
    auto pct = [&](double q) {
        const double target = q * double(total);
        quint64 acc = 0;
        for (int v = 0; v < 256; ++v) { acc += hist[v]; if (double(acc) >= target) return v; }
        return 255;
    };
    const int lo = pct(0.01), hi = pct(0.99), med = pct(0.50);

    // Already well lit: leave it alone. "Barely changed" is the requirement, and the cheapest way to
    // honour it is not to touch a frame that has blacks, whites and a mid-tone where they should be.
    if (lo <= 16 && hi >= 235 && med >= 70) return lut;

    // 1. Levels: the 99th percentile to 240, by a gain of at most 3 (beyond that it is noise being lifted).
    //    The black point moves only as far as the stretch needs: it starts to move at a 1.15× stretch and
    //    moves fully by 1.5×, so a hazy but bright frame keeps its blacks and is not crushed.
    constexpr double kTargetHi = 240.0, kMaxGain = 3.0;
    const double rawGain = std::clamp(kTargetHi / std::max(1.0, double(hi - lo)), 1.0, kMaxGain);
    const double loEff = std::min(24.0, lo * std::clamp((rawGain - 1.15) / 0.35, 0.0, 1.0));
    const double gain = std::clamp(kTargetHi / std::max(1.0, double(hi) - loEff), 1.0, kMaxGain);
    auto levels = [&](double v) { return std::max(0.0, (v - loEff) * gain); };

    // 2. Mid-tones: if the median is still darker than 0.36 after the stretch, a gamma that brings it
    //    there, no stronger than 0.6 (a harder lift makes a dark bay look grey and grainy).
    const double m = std::clamp(levels(med) / 255.0, 1e-3, 1.0);
    const double gamma = m >= 0.36 ? 1.0 : std::clamp(std::log(0.36) / std::log(m), 0.6, 1.0);

    // 3. Never blow out: when the gain can push a value past white, a soft shoulder above 225 that
    //    approaches 254 and never reaches white, so a lit sleeve keeps its folds.
    const bool shoulder = gain > 1.0 + 1e-9;
    for (int v = 0; v < 256; ++v) {
        double y = levels(v);
        if (gamma < 1.0) y = 255.0 * std::pow(std::min(y, 255.0 * 4) / 255.0, gamma);
        if (shoulder && y > 225.0) y = 225.0 + 29.0 * (1.0 - std::exp(-(y - 225.0) / 29.0));
        lut[v] = uint8_t(std::clamp(int(std::lround(y)), 0, 255));
    }
    for (int v = 1; v < 256; ++v) lut[v] = std::max(lut[v], lut[v - 1]);   // monotone, by construction AND rounding
    return lut;
}

int phaseEnumFor(const QString &position)
{
    // markup_truth.h's mapping (score.py's): p1→Address(0), p2→ShaftParallelBack(12), p3→MidBackswing(8),
    // p4→Top(2), p5→ArmParallelDown(13), p6→Delivery(9), p7→Impact(5), p8→ShaftParallelThrough(14),
    // p9→FollowThrough(11), p10→Finish(7). Phase is append-only and persisted as these ints.
    static const int kEnum[11] = { -1, 0, 12, 8, 2, 13, 9, 5, 14, 11, 7 };
    const int n = positionNumber(position);
    return n < 1 ? -1 : kEnum[n];
}

int positionNumber(const QString &position)
{
    const QString p = position.trimmed();
    if (p.size() < 2 || (p[0] != QLatin1Char('P') && p[0] != QLatin1Char('p'))) return -1;
    bool ok = false;
    const int n = p.mid(1).toInt(&ok);
    return ok && n >= 1 && n <= 10 ? n : -1;
}

std::optional<qint64> positionInstantUs(const QJsonObject &analysis, const QString &position, qint64 t0)
{
    const int e = phaseEnumFor(position);
    if (e < 0) return std::nullopt;
    for (const QJsonValue &v : analysis.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value(QStringLiteral("phase")).toInt(-1) == e && p.contains(QStringLiteral("t_us")))
            return relUs(qint64(p.value(QStringLiteral("t_us")).toDouble()), t0);
    }
    const int n = positionNumber(position);
    for (const QJsonValue &v : analysis.value(QStringLiteral("club")).toObject()
                                   .value(QStringLiteral("positions")).toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value(QStringLiteral("p")).toInt(-1) == n && p.contains(QStringLiteral("t_us")))
            return relUs(qint64(p.value(QStringLiteral("t_us")).toDouble()), t0);
    }
    return std::nullopt;
}

int nearestFrame(const QVector<qint64> &frameTimesUs, qint64 us)
{
    pinpoint::markup::VideoStreamInfo vi;   // the replay's own rule, not a second copy of it
    vi.frameTimesUs = frameTimesUs;
    return pinpoint::markup::frameIndexForUs(vi, us);
}

QVector<float> poseAt(const QVector<PoseSample> &samples, qint64 us, qint64 maxNearestUs, qint64 maxBracketUs)
{
    if (samples.isEmpty()) return {};
    auto it = std::lower_bound(samples.begin(), samples.end(), us,
                               [](const PoseSample &s, qint64 t) { return s.tUs < t; });
    const int hi = int(it - samples.begin());
    const int lo = hi - 1;
    int nearest = -1;
    qint64 bestD = std::numeric_limits<qint64>::max();
    for (int i : { lo, hi })
        if (i >= 0 && i < samples.size() && std::llabs(samples[i].tUs - us) < bestD) {
            bestD = std::llabs(samples[i].tUs - us); nearest = i;
        }
    if (nearest >= 0 && bestD <= maxNearestUs) return samples[nearest].kp;
    if (lo < 0 || hi >= samples.size()) return {};
    const PoseSample &a = samples[lo], &b = samples[hi];
    if (us - a.tUs > maxBracketUs || b.tUs - us > maxBracketUs || a.kp.size() != b.kp.size() || b.tUs <= a.tUs)
        return {};
    const double f = double(us - a.tUs) / double(b.tUs - a.tUs);
    QVector<float> kp(a.kp.size());
    for (int i = 0; i + 2 < kp.size(); i += 3) {
        kp[i]     = float(a.kp[i]     + (b.kp[i]     - a.kp[i])     * f);
        kp[i + 1] = float(a.kp[i + 1] + (b.kp[i + 1] - a.kp[i + 1]) * f);
        kp[i + 2] = std::min(a.kp[i + 2], b.kp[i + 2]);
    }
    return kp;
}

// ── Document-level helpers (report_stills_pure.h), shared by the renderer and the Charts Posture picture ──
namespace {
// Margins (golferCrop). 12% of the pose crop each side; the body band 6% of the golfer's height each side.
// Measured on Nick 20 Oct _03 swing_0005 face-on (frame 208, brightened ×3): the seated bystander's shoe ends
// at x = 250 px of 1008 (0.248) and the golfer's body band starts at ≈ 0.33, so 6% (≈ 37 px) puts the crop's
// edge at x ≈ 0.29 — about 45 px clear of him. 8% left only 28 px, and the pose crop's own 15% + aspect
// widening + 12% reached x ≈ 0.18, over his knee and shoe. The wrists are in the band, so 6% of height still
// clears a gripped hand at the top.
constexpr double kCropMargin = 0.12;
constexpr double kBodyMargin = 0.06;
// The body band is read from the swing itself, address to finish, with this much either side.
constexpr qint64 kBandPadUs = 200000;
} // namespace

PoseTrackLite readPoseTrack(const QJsonObject &analysis, bool dtl, qint64 t0)
{
    PoseTrackLite out;
    const QJsonObject b = analysis.value(dtl ? QStringLiteral("poseDtl") : QStringLiteral("pose2d")).toObject();
    const QJsonArray arr = b.contains(QStringLiteral("smoothed")) ? b.value(QStringLiteral("smoothed")).toArray()
                                                                  : b.value(QStringLiteral("frames")).toArray();
    if (arr.isEmpty()) return out;
    out.present = true;
    out.samples.reserve(arr.size());
    for (const QJsonValue &fv : arr) {
        const QJsonObject f = fv.toObject();
        PoseSample s;
        s.tUs = relUs(qint64(f.value(QStringLiteral("t_us")).toDouble()), t0);
        const QJsonArray kp = f.value(QStringLiteral("kp")).toArray();
        // Body keypoints only: the first 17 × (x, y, conf). The hands are never read.
        const int n = std::min<int>(kp.size(), 3 * 17);
        s.kp.reserve(n);
        for (int i = 0; i < n; ++i) s.kp.push_back(float(kp.at(i).toDouble()));
        out.samples.push_back(std::move(s));
    }
    std::sort(out.samples.begin(), out.samples.end(), [](const PoseSample &a, const PoseSample &b) { return a.tUs < b.tUs; });
    const QJsonObject cr = b.value(QStringLiteral("cropRect")).toObject();
    if (!cr.isEmpty()) {
        const QRectF r(cr.value(QStringLiteral("x")).toDouble(), cr.value(QStringLiteral("y")).toDouble(),
                       cr.value(QStringLiteral("w")).toDouble(), cr.value(QStringLiteral("h")).toDouble());
        if (r.width() > 0 && r.height() > 0) out.cropRect = r;
    }
    return out;
}

std::optional<QRectF> swingBodyBand(const PoseTrackLite &pose, const QJsonObject &analysis, qint64 t0)
{
    // Address to finish (or the last instant the swing names), padded: the golfer's whole movement, and
    // not the walk onto the mat before it.
    const std::optional<qint64> a = positionInstantUs(analysis, QStringLiteral("P1"), t0);
    std::optional<qint64> z = positionInstantUs(analysis, QStringLiteral("P10"), t0);
    if (!z) for (const char *p : { "P9", "P8", "P7" }) if ((z = positionInstantUs(analysis, QString::fromLatin1(p), t0))) break;
    const qint64 from = a ? *a - kBandPadUs : std::numeric_limits<qint64>::min();
    const qint64 to   = z ? *z + kBandPadUs : std::numeric_limits<qint64>::max();
    QVector<QVector<float>> frames;
    for (const PoseSample &s : pose.samples) if (s.tUs >= from && s.tUs <= to) frames.push_back(s.kp);
    return bodyExtent(frames, pinpoint::tuned::upperBody::kConfMin);
}

QRectF stillGolferCrop(const PoseTrackLite &pose, const std::optional<QRectF> &band, QString *note)
{
    return golferCrop(pose.cropRect, band, kCropMargin, kBodyMargin, note);
}

QRect stillCropPixels(const QRectF &srcN, int W, int H)
{
    return QRect(QPoint(int(std::floor(srcN.left() * W)), int(std::floor(srcN.top() * H))),
                 QPoint(int(std::ceil(srcN.right() * W)) - 1, int(std::ceil(srcN.bottom() * H)) - 1))
        .intersected(QRect(0, 0, W, H));
}

StillStream stillStreamOf(const QJsonObject &root, bool dtl)
{
    StillStream out;
    QVector<QJsonObject> videos;
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject s = v.toObject();
        if (s.value(QStringLiteral("kind")).toString() == QLatin1String("video")) videos.push_back(s);
    }
    if (videos.isEmpty()) return out;
    int faceIdx = pinpoint::markup::faceOnStreamIndex(videos);
    // markup's face-on pick falls back to the FIRST video; a swing filmed down the line alone would then read its DTL
    // video as face-on. A stream that declares itself down the line (perspective 1), or is named so without declaring
    // face-on, is never the face-on pick (posture chart stage 5: the picture's DTL-only swing).
    if (faceIdx >= 0 && faceIdx < videos.size()) {
        const int persp = videos[faceIdx].value(QStringLiteral("setup")).toObject().value(QStringLiteral("perspective")).toInt(-1);
        if (persp == 1 || (persp != 2 && pinpoint::markup::streamLooksDownTheLine(videos[faceIdx]))) faceIdx = -1;
    }
    int idx = -1;
    if (!dtl) {
        idx = faceIdx;
    } else {
        for (int i = 0; i < videos.size() && idx < 0; ++i)
            if (i != faceIdx && videos[i].value(QStringLiteral("setup")).toObject()
                                    .value(QStringLiteral("perspective")).toInt(-1) == 1)
                idx = i;
        for (int i = 0; i < videos.size() && idx < 0; ++i)
            if (i != faceIdx && pinpoint::markup::streamLooksDownTheLine(videos[i])) idx = i;
    }
    if (idx < 0) return out;
    const QJsonObject s = videos.at(idx);
    out.file = s.value(QStringLiteral("file")).toString();
    for (const QJsonValue &t : s.value(QStringLiteral("frames")).toObject().value(QStringLiteral("t_us")).toArray())
        out.tUs.push_back(qint64(t.toDouble()));
    const QJsonObject enc = s.value(QStringLiteral("encoded")).toObject();
    out.width = enc.value(QStringLiteral("width")).toInt(0);
    out.height = enc.value(QStringLiteral("height")).toInt(0);
    out.ok = !out.file.isEmpty() && !out.tUs.isEmpty();
    return out;
}

} // namespace pinpoint::report::stills
