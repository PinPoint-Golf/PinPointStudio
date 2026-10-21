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

// report_stills_test — the session report's stills (src/Gui/report/report_stills.h): which body keypoints a
// segment reads for each handedness, the crop maths (the pose crop, the privacy bound of the golfer's own
// body, the aspect padding), the exposure curve (monotone, never blown out, the identity on a well-lit
// crop), the position instant and the frame and pose sample matched to it — all on synthetic data. Then
// one real swing READ-ONLY from the share (Nick Henn 20 Oct _03 swing_0005), which SKIPS when the share is
// absent: the frame the replay shows at P4, the line on it, the crop clear of the people seated in the bay,
// the keyframe seek decoding the same pixels as a decode from frame 0, and renderAll's ordering.
//
// REVIEW MODE (not run by ctest): `report_stills_test --review <outdir>` renders the review set — Nick 20 Oct
// _03, Jess 20 Oct _03 and Mark 7 Oct _01, two swings each, P1/P4/P7 on both cameras with the trailThigh,
// spine and leadArm lines — through renderAll, and writes <outdir>/manifest.tsv for the contact sheet
// (build/report-build/stills-review/contact_sheet.py). Reads the share only; writes only to <outdir>.

#include "report/report_stills.h"

#include "../../Analysis/mp4_frame_reader.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <cstdio>

using namespace pinpoint::report::stills;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// A flat 17-keypoint list with every point at (x, y) = (0.1 k, 0.05 k) and the given confidence.
static QVector<float> bodyKp(float conf = 0.9f)
{
    QVector<float> kp;
    for (int k = 0; k < 17; ++k) kp << 0.1f * (k % 10) << 0.05f * k << conf;
    return kp;
}

static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }

static void testSegments()
{
    std::printf("segments\n");
    check(leadIsLeftFor(QStringLiteral("Right")) && !leadIsLeftFor(QStringLiteral("Left"))
              && !leadIsLeftFor(QStringLiteral("left")) && leadIsLeftFor(QString()),
          "handedness: right or unknown → left side leads; Left → right side leads");

    const Segment rhThigh = segmentFor(QStringLiteral("trailThigh"), true);
    const Segment lhThigh = segmentFor(QStringLiteral("trailThigh"), false);
    check(rhThigh.joints == QVector<QVector<int>>{ { 12 }, { 14 } }, "trailThigh, right-handed: right hip → right knee (12 → 14)");
    check(lhThigh.joints == QVector<QVector<int>>{ { 11 }, { 13 } }, "trailThigh, left-handed: left hip → left knee (11 → 13)");
    check(rhThigh.lowerBody && rhThigh.words == QLatin1String("trail thigh"), "trailThigh is lower body, 'trail thigh'");
    check(segmentFor(QStringLiteral("leadArm"), true).joints == QVector<QVector<int>>{ { 5 }, { 7 }, { 9 } },
          "leadArm, right-handed: left shoulder → elbow → wrist (5, 7, 9)");
    check(segmentFor(QStringLiteral("leadArm"), false).joints == QVector<QVector<int>>{ { 6 }, { 8 }, { 10 } },
          "leadArm, left-handed: right shoulder → elbow → wrist (6, 8, 10)");
    check(segmentFor(QStringLiteral("leadShin"), true).joints == QVector<QVector<int>>{ { 13 }, { 15 } }, "leadShin RH: 13 → 15");
    check(segmentFor(QStringLiteral("spine"), false).joints == QVector<QVector<int>>{ { 11, 12 }, { 5, 6 } },
          "spine: mid-hip → mid-shoulder, either hand");
    check(segmentFor(QStringLiteral("head"), true).joints == QVector<QVector<int>>{ { 5, 6 }, { 0 } }, "head: mid-shoulder → nose");
    check(segmentFor(QStringLiteral("stance"), true).joints == QVector<QVector<int>>{ { 16 }, { 15 } }, "stance: ankle → ankle");
    // Every vocabulary name resolves; no segment ever reads a hand keypoint (91–132) or a face/foot point.
    bool allBody = true, allKnown = true;
    for (const char *n : { "trailThigh", "leadThigh", "trailShin", "leadShin", "spine", "shoulders", "hips", "leadArm",
                           "leadUpperArm", "leadForearm", "trailArm", "head", "stance" })
        for (bool lead : { true, false }) {
            const Segment s = segmentFor(QString::fromLatin1(n), lead);
            allKnown = allKnown && !s.joints.isEmpty() && !s.words.isEmpty();
            for (const auto &j : s.joints) for (int k : j) allBody = allBody && k >= 0 && k < 17;
        }
    check(allKnown, "all 13 vocabulary names resolve, with words");
    check(allBody, "only body keypoints 0–16 are ever read");
    check(!segmentFor(QStringLiteral("elbowChicken"), true).known(), "an unknown name resolves to nothing");
    const Segment shaft = segmentFor(QStringLiteral("shaft"), true);
    check(shaft.known() && shaft.club && shaft.joints.isEmpty() && shaft.words == QLatin1String("club"),
          "shaft is the club line (no body keypoints), 'club'");

    QVector<float> kp = bodyKp();
    QString weak;
    const QVector<QPointF> spine = segmentPoints(segmentFor(QStringLiteral("spine"), true), kp, 0.30, &weak);
    check(spine.size() == 2 && near(spine[0].x(), (0.1 + 0.2) / 2, 1e-6) && near(spine[0].y(), (0.55 + 0.60) / 2, 1e-6)
              && near(spine[1].x(), (0.5 + 0.6) / 2, 1e-6),
          "spine points are the joint means");
    kp[3 * 14 + 2] = 0.29f;   // the right knee just under the gate
    weak.clear();
    check(segmentPoints(rhThigh, kp, 0.30, &weak).isEmpty() && weak == QLatin1String("trail thigh"),
          "a joint under the 0.30 gate draws nothing and names the part");
    check(!segmentPoints(lhThigh, kp, 0.30).isEmpty(), "the other side's thigh is unaffected");
    check(segmentPoints(rhThigh, QVector<float>(9, 1.0f), 0.30).isEmpty(), "a short keypoint list draws nothing");
}

static void testCrop()
{
    std::printf("crop\n");
    const QRectF cr(0.25, 0.12, 0.55, 0.73), body(0.31, 0.18, 0.40, 0.60);
    QString note, src;
    const QRectF only = golferCrop(cr, std::nullopt, 0.12, 0.08, &note, &src);
    check(near(only.left(), 0.25 - 0.066) && near(only.top(), 0.12 - 0.0876) && near(only.right(), 0.80 + 0.066)
              && note.isEmpty() && src == QLatin1String("pose"),
          "pose crop + 12% each side");
    const QRectF bounded = golferCrop(cr, body, 0.12, 0.08, &note, &src);
    check(near(bounded.left(), 0.31 - 0.048) && near(bounded.right(), 0.71 + 0.048),
          "horizontally bounded by the body + 8% of its height, both sides (the privacy bound)");
    check(near(bounded.top(), only.top()) && near(bounded.bottom(), only.bottom()), "vertically the pose crop's own margin");
    const QRectF tall = golferCrop(QRectF(0.3, 0.3, 0.4, 0.3), body, 0.12, 0.08);
    check(tall.top() <= 0.18 - 0.048 + 1e-9 && tall.bottom() >= 0.78 + 0.048 - 1e-9, "head to feet always inside");
    const QRectF bodyOnly = golferCrop(std::nullopt, body, 0.12, 0.08, &note, &src);
    check(src == QLatin1String("body") && near(bodyOnly.top(), 0.18 - 0.072) && near(bodyOnly.left(), 0.31 - 0.048),
          "no pose crop: the body + margin");
    const QRectF edge = golferCrop(QRectF(0.0, 0.05, 0.9, 0.9), std::nullopt, 0.12, 0.08);
    check(edge.left() >= 0 && edge.top() >= 0 && edge.right() <= 1 + 1e-12 && edge.bottom() <= 1 + 1e-12, "clamped to the frame");
    note.clear();
    const QRectF centre = golferCrop(std::nullopt, std::nullopt, 0.12, 0.08, &note, &src);
    check(near(centre.left(), 0.2) && near(centre.width(), 0.6) && src == QLatin1String("centre") && !note.isEmpty(),
          "neither: a centred crop, and a note");

    // bodyExtent: one wild frame does not widen it; too few frames say nothing.
    QVector<QVector<float>> frames;
    for (int i = 0; i < 40; ++i) {
        QVector<float> kp;
        for (int k = 0; k < 17; ++k) kp << float(0.40 + 0.01 * (k % 5)) << float(0.2 + 0.03 * k) << 0.9f;
        frames << kp;
    }
    QVector<float> wild = frames[0];
    wild[0] = 0.02f;   // one frame's nose on the bystander
    frames[7] = wild;
    const auto ext = bodyExtent(frames, 0.30);
    check(ext && near(ext->left(), 0.40, 1e-6) && near(ext->right(), 0.44, 1e-6) && near(ext->top(), 0.2, 1e-6),
          "body extent: robust to one stray keypoint");
    check(!bodyExtent(frames.mid(0, 2), 0.30), "body extent: fewer than 3 frames → none");

    // fitAspect
    double px = -1, py = -1;
    const QRectF narrow(0.4, 0.1, 0.2, 0.8);   // on a square frame: 0.25 wide/high
    const QRectF n2 = fitAspect(narrow, 2.0 / 3.0, 1.0, &px, &py);
    check(n2 == narrow && near(px, (2.0 / 3.0) / 0.25 - 1.0) && near(py, 0), "too narrow: padded at the sides, never widened");
    const QRectF wide(0.2, 0.4, 0.6, 0.2);
    const QRectF w2 = fitAspect(wide, 1.0, 1.0, &px, &py);
    check(near(w2.left(), 0.2) && near(w2.width(), 0.6) && near(w2.height(), 0.6) && near(w2.center().y(), 0.5) && near(px, 0) && near(py, 0),
          "too wide: grown above and below within the frame");
    const QRectF w3 = fitAspect(QRectF(0.0, 0.0, 1.0, 0.5), 0.5, 1.0, &px, &py);
    check(near(w3.height(), 1.0) && near(py, 1.0), "too wide for the frame: the rest is padded");
    check(fitAspect(narrow, 0.0, 1.0, &px, &py) == narrow && px == 0 && py == 0, "aspect 0: unchanged");
}

static bool monotoneAndBounded(const std::array<uint8_t, 256> &c)
{
    if (c[0] != 0) return false;
    for (int v = 1; v < 256; ++v) if (c[v] < c[v - 1]) return false;
    return true;
}

static void testExposure()
{
    std::printf("exposure\n");
    // Well lit: blacks, whites and a mid-tone.
    std::array<quint32, 256> lit{};
    for (int v = 0; v < 256; ++v) lit[v] = 100;
    const auto id = exposureCurve(lit);
    bool identity = true;
    for (int v = 0; v < 256; ++v) identity = identity && id[v] == v;
    check(identity, "well-lit crop: the identity");

    // Bright but with no deep blacks (p1 ≈ 30, p99 ≈ 250): barely changed.
    std::array<quint32, 256> hazy{};
    for (int v = 30; v < 252; ++v) hazy[v] = 100;
    const auto hz = exposureCurve(hazy);
    int maxDev = 0;
    for (int v = 0; v < 256; ++v) maxDev = std::max(maxDev, std::abs(int(hz[v]) - v));
    check(monotoneAndBounded(hz) && maxDev <= 2, "bright, low-contrast crop: within 2 levels of the identity");

    // Dark bay (Jess's face-on): everything in 0..70, median ~15.
    std::array<quint32, 256> dark{};
    for (int v = 0; v < 70; ++v) dark[v] = v < 30 ? 400 : 40;
    const auto dk = exposureCurve(dark);
    check(monotoneAndBounded(dk), "dark crop: monotone, black stays black");
    check(dk[15] > 30 && dk[60] > 150, "dark crop: mid-tones and the brightest tenth lifted");
    check(dk[69] < 255 && dk[255] < 255, "dark crop: nothing reaches white (the shoulder)");

    // A dark crop with a few bright highlights (a lit LED strip): the highlights stay below white.
    std::array<quint32, 256> spots = dark;
    for (int v = 240; v < 256; ++v) spots[v] = 2;
    const auto sp = exposureCurve(spots);
    check(monotoneAndBounded(sp) && sp[255] <= 255 && sp[200] < 255, "highlights kept: monotone, not clipped flat");

    // Gain capped: a nearly black crop is lifted at most 3× before the gamma.
    std::array<quint32, 256> black{};
    for (int v = 0; v < 12; ++v) black[v] = 100;
    const auto bk = exposureCurve(black);
    check(monotoneAndBounded(bk) && bk[11] < 120, "near-black crop: the lift is capped (noise is not stretched to grey)");
    std::array<quint32, 256> empty{};
    const auto em = exposureCurve(empty);
    check(em[128] == 128, "empty histogram: the identity");
}

static void testTiming()
{
    std::printf("timing\n");
    check(phaseEnumFor(QStringLiteral("P1")) == 0 && phaseEnumFor(QStringLiteral("P2")) == 12 && phaseEnumFor(QStringLiteral("P3")) == 8
              && phaseEnumFor(QStringLiteral("P4")) == 2 && phaseEnumFor(QStringLiteral("P5")) == 13 && phaseEnumFor(QStringLiteral("P6")) == 9
              && phaseEnumFor(QStringLiteral("P7")) == 5 && phaseEnumFor(QStringLiteral("P8")) == 14 && phaseEnumFor(QStringLiteral("P9")) == 11
              && phaseEnumFor(QStringLiteral("P10")) == 7,
          "P1..P10 → the analyzer's Phase enum (markup_truth / score.py mapping)");
    check(phaseEnumFor(QStringLiteral("P11")) == -1 && phaseEnumFor(QStringLiteral("top")) == -1 && positionNumber(QStringLiteral("p7")) == 7,
          "a non-position is refused; lower case accepted");

    const qint64 t0 = 552733944038;
    QJsonObject an{
        { QStringLiteral("phases"), QJsonArray{ QJsonObject{ { QStringLiteral("phase"), 0 }, { QStringLiteral("t_us"), double(t0 + 1395257) } },
                                                QJsonObject{ { QStringLiteral("phase"), 2 }, { QStringLiteral("t_us"), 2212475.0 } } } },
        { QStringLiteral("club"), QJsonObject{ { QStringLiteral("positions"),
            QJsonArray{ QJsonObject{ { QStringLiteral("p"), 4 }, { QStringLiteral("t_us"), 9.0 } },
                        QJsonObject{ { QStringLiteral("p"), 8 }, { QStringLiteral("t_us"), 3532739.0 } } } } } } };
    check(positionInstantUs(an, QStringLiteral("P1"), t0) == 1395257, "an absolute instant is re-timed by t0");
    check(positionInstantUs(an, QStringLiteral("P4"), t0) == 2212475, "the phase list wins over the ladder");
    check(positionInstantUs(an, QStringLiteral("P8"), t0) == 3532739, "a position missing from the phases comes from the ladder");
    check(!positionInstantUs(an, QStringLiteral("P9"), t0), "a position the swing does not have: none");

    const QVector<qint64> ft{ 0, 6700, 13400, 20100 };
    check(nearestFrame(ft, 6700) == 1 && nearestFrame(ft, 9000) == 1 && nearestFrame(ft, 10100) == 2,
          "nearest frame by timestamp");
    check(nearestFrame(ft, 10050) == 1, "a tie goes to the earlier frame (the replay's rule)");
    check(nearestFrame(ft, -50) == 0 && nearestFrame(ft, 99999) == 3 && nearestFrame({}, 5) == -1, "clamped at the ends; none for no frames");

    QVector<PoseSample> ps;
    for (qint64 t : { 0LL, 6700LL, 13400LL, 93400LL, 113400LL }) {
        PoseSample s; s.tUs = t; s.kp = { float(t) / 1e6f, 0.5f, t == 13400 ? 0.4f : 0.9f };
        ps << s;
    }
    check(poseAt(ps, 6700, 20000, 60000).value(0) == ps[1].kp[0], "pose: the exact sample");
    check(poseAt(ps, 10000, 20000, 60000).value(0) == ps[1].kp[0] || poseAt(ps, 10000, 20000, 60000).value(0) == ps[2].kp[0],
          "pose: the nearest within 20 ms");
    check(poseAt(ps, 103400, 5000, 60000).size() == 3 && near(poseAt(ps, 103400, 5000, 60000)[0], 0.1034, 1e-4)
              && near(poseAt(ps, 103400, 5000, 60000)[2], 0.9, 1e-6),
          "pose: a bracket within 60 ms either side is blended");
    check(poseAt(ps, 53400, 20000, 30000).isEmpty(), "pose: nothing close enough → none");
    check(poseAt({}, 0, 20000, 60000).isEmpty(), "pose: no samples → none");
}

static void testShaft()
{
    std::printf("shaft\n");
    const qint64 t0 = 1000000000;
    auto fo = [](double t, int flags, bool pts = true) {
        QJsonObject o{ { QStringLiteral("t_us"), t }, { QStringLiteral("flags"), flags } };
        if (pts) { o[QStringLiteral("grip")] = QJsonArray{ 0.5, 0.5 }; o[QStringLiteral("head")] = QJsonArray{ 0.4, 0.9 }; }
        return o;
    };
    QJsonObject an{ { QStringLiteral("club"), QJsonObject{ { QStringLiteral("valid"), true }, { QStringLiteral("samples"),
        QJsonArray{ fo(t0 + 0, 0x01), fo(t0 + 6700, 0x11), fo(t0 + 13400, 0x18), fo(t0 + 20100, 0x04 | 0x10),
                    fo(t0 + 26800, 0x01 | 0x200), fo(t0 + 33500, 0x01, false), fo(t0 + 40200, 0x11), fo(t0 + 46900, 0x01) } } } } };
    {   // headConf: 0.20 is under the 0.30 club.headConfidence gate; 0.30 is at it.
        QJsonObject club = an[QStringLiteral("club")].toObject();
        QJsonArray ss = club[QStringLiteral("samples")].toArray();
        QJsonObject a = ss[6].toObject(); a[QStringLiteral("headConf")] = 0.20; ss[6] = a;
        QJsonObject b = ss[7].toObject(); b[QStringLiteral("headConf")] = 0.30; ss[7] = b;
        club[QStringLiteral("samples")] = ss; an[QStringLiteral("club")] = club;
    }
    const QVector<ShaftSample> tr = shaftTrack(an, false, t0);
    check(tr.size() == 8 && tr[0].tUs == 0 && tr[1].tUs == 6700, "face-on: club.samples, re-timed by t0");
    check(!tr[6].drawable && tr[7].drawable, "headConf under the 0.30 gate is not drawn; at it, it is");
    check(tr[0].drawable && tr[1].drawable && tr[2].drawable, "measured, measured with a projected head, and a blur wedge are drawn");
    check(!tr[3].drawable && !tr[4].drawable && !tr[5].drawable, "coasted, implausible and pointless samples are not");
    const QVector<QPointF> at = shaftAt(tr, 6000, 20000);
    check(at.size() == 2 && at[0] == QPointF(0.5, 0.5) && at[1] == QPointF(0.4, 0.9), "shaftAt: the nearest sample, grip then head");
    check(shaftAt(tr, 20000, 20000).isEmpty(), "shaftAt: a coasted nearest sample is not filled from a measured neighbour");
    check(shaftAt(tr, 100000, 20000).isEmpty(), "shaftAt: nothing within 20 ms → none");
    QJsonObject invalid = an;
    QJsonObject club = an[QStringLiteral("club")].toObject(); club[QStringLiteral("valid")] = false; invalid[QStringLiteral("club")] = club;
    check(shaftTrack(invalid, false, t0).isEmpty(), "face-on: club.valid false → no track (the overlay's gate)");

    auto dt = [](double t, const char *tier, bool head = true) {
        QJsonObject o{ { QStringLiteral("t_us"), t }, { QStringLiteral("tier"), QString::fromLatin1(tier) },
                       { QStringLiteral("grip"), QJsonArray{ 0.46, 0.25 } } };
        o[QStringLiteral("head")] = head ? QJsonValue(QJsonArray{ 0.7, 0.8 }) : QJsonValue(QJsonValue::Null);
        return o;
    };
    QJsonObject ad{ { QStringLiteral("clubDtl"), QJsonObject{ { QStringLiteral("frames"),
        QJsonArray{ dt(10, "RAY"), dt(20, "SEG"), dt(30, "BAND"), dt(40, "HELD"), dt(50, "END_ON", false), dt(60, "UNSEEN", false), dt(70, "RAY", false) } } } } };
    const QVector<ShaftSample> td = shaftTrack(ad, true, t0);
    check(td.size() == 7 && td[0].drawable && td[1].drawable && td[2].drawable, "DTL: RAY, SEG and BAND are drawn");
    check(!td[3].drawable && !td[4].drawable && !td[5].drawable && !td[6].drawable, "DTL: HELD, END_ON, UNSEEN and a headless frame are not");
    check(shaftTrack(QJsonObject{}, true, t0).isEmpty(), "no clubDtl → no track");
}

static double warnShare(const QImage &img)
{
    // The share of pixels near the warn colour (#BA562D) — the line is on the still.
    int hits = 0;
    const QImage im = img.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < im.height(); y += 2)
        for (int x = 0; x < im.width(); x += 2) {
            const QRgb c = im.pixel(x, y);
            if (std::abs(qRed(c) - 0xBA) < 30 && std::abs(qGreen(c) - 0x56) < 30 && std::abs(qBlue(c) - 0x2D) < 30) ++hits;
        }
    return double(hits) / double((im.width() / 2) * (im.height() / 2));
}

static void testRealSwing(QCoreApplication &app)
{
    std::printf("real swing (read-only)\n");
    const QString swing = QStringLiteral("/mnt/swingdata/Nick-Henn/2026-10-20_Nick-Henn_Wrist_03/swing_0005");
    if (!QFileInfo::exists(swing + QStringLiteral("/swing.ppsw"))) {
        std::printf("  [SKIP] %s not reachable (the share is not mounted)\n", qPrintable(swing));
        return;
    }
    QTemporaryDir tmp;
    StillRequest rq;
    rq.swingDir = swing; rq.camera = QStringLiteral("faceOn"); rq.phase = QStringLiteral("P4");
    rq.line = QStringLiteral("trailThigh"); rq.addressLine = true; rq.outPath = tmp.filePath(QStringLiteral("p4.jpg"));
    QElapsedTimer t; t.start();
    const StillResult r = ReportStills::renderOne(rq);
    std::printf("  (face-on P4 in %lld ms; note \"%s\"; error \"%s\")\n", t.elapsed(), qPrintable(r.note), qPrintable(r.error));
    check(r.ok && r.path == rq.outPath && QFileInfo::exists(rq.outPath), "face-on P4 still written");
    // P4 of swing_0005 is t = 2 212 475 µs, which is face-on frame 330 exactly (frames.t_us[330]).
    check(r.frameIndex == 330, "the frame is the one at the swing's own P4 instant (frame 330)");
    check(r.lineDrawn && r.addressDrawn && r.note.isEmpty(), "trail thigh and its address line drawn, no note");
    // The seated bystander in this bay reaches x ≈ 0.25 of the face-on frame; the golfer's body starts ≈ 0.31.
    check(r.crop.left() > 0.27 && r.crop.right() < 0.80, "the crop stays clear of the man seated in the bay (his shoe ends at x = 0.248)");
    const QImage img(rq.outPath);
    check(!img.isNull() && std::max(img.width(), img.height()) == 1400, "long edge 1400 px");
    check(warnShare(img) > 0.0001, "the warn line is on the picture");

    // The club line on the same swing: face-on P4 is a measured shaft (headConf 0.45); P1 is a shaft the
    // tracker doubts (headConf 0.20, under the 0.30 gate) — no line, and the note says so.
    {
        StillRequest c = rq;
        c.phase = QStringLiteral("P4"); c.line = QStringLiteral("shaft"); c.addressLine = false;
        c.outPath = tmp.filePath(QStringLiteral("p4_shaft.jpg"));
        const StillResult rc = ReportStills::renderOne(c);
        check(rc.ok && rc.lineDrawn && rc.note.isEmpty(), "face-on P4: the club shaft drawn");
        c.phase = QStringLiteral("P1"); c.outPath = tmp.filePath(QStringLiteral("p1_shaft.jpg"));
        const StillResult r1 = ReportStills::renderOne(c);
        check(r1.ok && !r1.lineDrawn && r1.note.startsWith(QLatin1String("No club line:")),
              "face-on P1: a doubted shaft is not drawn, and the still says so");
    }

    // A portrait slot: padded, the same long edge, the requested aspect.
    rq.aspect = 2.0 / 3.0; rq.outPath = tmp.filePath(QStringLiteral("p4_23.jpg"));
    const StillResult r23 = ReportStills::renderOne(rq);
    const QImage i23(rq.outPath);
    check(r23.ok && std::abs(double(i23.width()) / i23.height() - 2.0 / 3.0) < 0.01, "aspect 2:3 honoured");

    // The keyframe seek decodes the same pixels as a decode forward from frame 0 (the independent path).
    {
        using pinpoint::analysis::Mp4FrameDecoder;
        Mp4FrameDecoder::Config seekCfg; seekCfg.ffmpeg = true; seekCfg.seek = true;
        Mp4FrameDecoder a((swing + QStringLiteral("/Face-On.mp4")).toStdString(), seekCfg);
        Mp4FrameDecoder b((swing + QStringLiteral("/Face-On.mp4")).toStdString(), {});
        cv::Mat ma, mb, mb2;
        const bool okA = a.read(371, ma);
        const cv::Mat ca = ma.clone();
        const bool okB = b.read(371, mb);
        const bool same = okA && okB && ca.size() == mb.size() && cv::norm(ca, mb, cv::NORM_INF) == 0;
        b.read(372, mb2);
        const bool differs = !mb2.empty() && cv::norm(ca, mb2, cv::NORM_L1) > 0;
        check(same, "seek and rewind decode frame 371 identically");
        check(differs, "and the next frame is a different picture");
    }

    // DTL, a line that is not there to see, and errors that say why.
    StillRequest d = rq;
    d.camera = QStringLiteral("dtl"); d.phase = QStringLiteral("P7"); d.line = QStringLiteral("spine"); d.addressLine = false;
    d.aspect = 0; d.outPath = tmp.filePath(QStringLiteral("dtl_p7.jpg"));
    StillRequest bad = d;
    bad.phase = QStringLiteral("P9"); bad.outPath = tmp.filePath(QStringLiteral("p9.jpg"));
    StillRequest none = d;
    none.swingDir = tmp.filePath(QStringLiteral("no_such_swing")); none.outPath = tmp.filePath(QStringLiteral("none.jpg"));

    // renderAll: results in request order, on the object's thread.
    ReportStills stills;
    QVector<StillResult> got;
    bool done = false;
    QObject::connect(&stills, &ReportStills::finished, &app, [&](const QVector<StillResult> &res) {
        got = res; done = true; app.quit();
    });
    stills.renderAll({ d, none, bad });
    if (!done) app.exec();
    check(got.size() == 3, "renderAll: one result per request");
    if (got.size() == 3) {
        check(got[0].ok && got[0].lineDrawn && got[0].frameIndex == 371, "renderAll[0]: DTL P7 with the spine, frame 371");
        check(!got[1].ok && got[1].error.contains(QLatin1String("swing document")), "renderAll[1]: a missing swing says so");
        check(!got[2].ok && got[2].error.contains(QLatin1String("P9")), "renderAll[2]: a position the swing lacks says so");
    }
}

// ── Review mode ─────────────────────────────────────────────────────────────────────────────────────────
static int review(QCoreApplication &app, const QString &outDir, const QStringList &extraSwings, const QString &extraLine)
{
    struct Sess { const char *tag; const char *dir; };
    const Sess sessions[] = {
        { "nick", "/mnt/swingdata/Nick-Henn/2026-10-20_Nick-Henn_Wrist_03" },
        { "jess", "/mnt/swingdata/Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03" },
        { "mark", "/mnt/swingdata/Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01" },
    };
    QDir().mkpath(outDir);
    QVector<StillRequest> reqs;
    QStringList captions;
    for (const Sess &s : sessions) {
        if (!extraSwings.isEmpty()) break;   // `--swing` renders only the swings it names
        QDir sd(QString::fromLatin1(s.dir));
        QStringList swings = sd.entryList({ QStringLiteral("swing_*") }, QDir::Dirs, QDir::Name);
        int used = 0;
        for (const QString &sw : swings) {
            if (used == 2) break;
            const QString dir = sd.filePath(sw);
            if (!QFileInfo::exists(dir + QStringLiteral("/swing.ppsw")) && !QFileInfo::exists(dir + QStringLiteral("/swing.json"))) continue;
            ++used;
            // Swing 1: P1 lead arm, P4 trail thigh with the address line (a "change" picture), P7 spine,
            // natural box. Swing 2 rotates the lines and asks for a 2:3 portrait slot.
            const char *lines1[3] = { "leadArm", "trailThigh", "spine" };
            const char *lines2[3] = { "spine", "leadArm", "trailThigh" };
            const char *pos[3] = { "P1", "P4", "P7" };
            for (const char *cam : { "faceOn", "dtl" })
                for (int k = 0; k < 3; ++k) {
                    StillRequest r;
                    r.swingDir = dir; r.camera = QString::fromLatin1(cam); r.phase = QString::fromLatin1(pos[k]);
                    r.line = QString::fromLatin1(used == 1 ? lines1[k] : lines2[k]);
                    r.addressLine = r.line == QLatin1String("trailThigh") && k > 0;
                    r.aspect = used == 1 ? 0.0 : 2.0 / 3.0;
                    r.outPath = QDir(outDir).filePath(QStringLiteral("%1_%2_%3_%4_%5.jpg")
                                                          .arg(QString::fromLatin1(s.tag), sw.mid(6), r.camera, r.phase, r.line));
                    reqs << r;
                    captions << QStringLiteral("%1 %2").arg(QString::fromLatin1(s.tag), sw);
                }
        }
    }
    // `--swing <dir>` (repeatable): also these swings, P1/P4/P7 on both cameras with the trail thigh and
    // a 2:3 slot — for checking one bay's bystanders on the swing that showed them.
    for (const QString &dir : extraSwings)
        for (const char *cam : { "faceOn", "dtl" })
            for (const char *ph : { extraLine == QLatin1String("shaft") ? "P2" : "P1", "P4", "P7" }) {
                StillRequest r;
                r.swingDir = dir; r.camera = QString::fromLatin1(cam); r.phase = QString::fromLatin1(ph);
                r.line = extraLine; r.aspect = 2.0 / 3.0;
                const QString tag = QDir(dir).dirName() + QLatin1Char('_') + QFileInfo(QFileInfo(dir).absolutePath()).fileName().section(QLatin1Char('_'), 1, 1);
                r.outPath = QDir(outDir).filePath(QStringLiteral("extra_%1_%2_%3_%4.jpg").arg(tag, r.camera, r.phase, r.line));
                reqs << r;
                captions << QStringLiteral("extra %1").arg(tag);
            }
    ReportStills stills;
    QVector<StillResult> got;
    QElapsedTimer t; t.start();
    QObject::connect(&stills, &ReportStills::finished, &app, [&](const QVector<StillResult> &res) { got = res; app.quit(); });
    stills.renderAll(reqs);
    app.exec();
    std::printf("review: %d stills in %lld ms\n", int(got.size()), t.elapsed());
    QFile mf(QDir(outDir).filePath(QStringLiteral("manifest.tsv")));
    if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    QTextStream ts(&mf);
    ts << "file\tswing\tcamera\tphase\tline\taddress\taspect\tok\tframe\tcrop\tnote\terror\n";
    int bad = 0;
    for (int i = 0; i < reqs.size() && i < got.size(); ++i) {
        const StillRequest &r = reqs[i]; const StillResult &g = got[i];
        bad += g.ok ? 0 : 1;
        ts << QFileInfo(r.outPath).fileName() << '\t' << captions[i] << '\t' << r.camera << '\t' << r.phase << '\t' << r.line << '\t'
           << (r.addressLine ? 1 : 0) << '\t' << r.aspect << '\t' << (g.ok ? 1 : 0) << '\t' << g.frameIndex << '\t'
           << QStringLiteral("%1,%2,%3,%4").arg(g.crop.left(), 0, 'f', 3).arg(g.crop.top(), 0, 'f', 3)
                  .arg(g.crop.right(), 0, 'f', 3).arg(g.crop.bottom(), 0, 'f', 3)
           << '\t' << g.note << '\t' << g.error << '\n';
    }
    std::printf("review: %d failed; manifest %s\n", bad, qPrintable(mf.fileName()));
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int rv = int(args.indexOf(QStringLiteral("--review")));
    if (rv >= 0) {
        QStringList extra;
        for (int i = 0; i + 1 < args.size(); ++i)
            if (args[i] == QLatin1String("--swing")) extra << args[i + 1];
        const int li = int(args.indexOf(QStringLiteral("--line")));
        // `--line <name>` for the `--swing` set (default trailThigh); `--line shaft` draws P2 in place of P1.
        return review(app, args.value(rv + 1, QStringLiteral("stills-review")), extra,
                      li >= 0 ? args.value(li + 1, QStringLiteral("trailThigh")) : QStringLiteral("trailThigh"));
    }

    testSegments();
    testCrop();
    testExposure();
    testTiming();
    testShaft();
    testRealSwing(app);

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
