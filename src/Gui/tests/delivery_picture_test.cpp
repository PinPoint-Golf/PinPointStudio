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

// The Club delivery picture strip (charts/delivery_picture.h; club_delivery_chart_design.md stage 6), the twin of the
// Posture picture's tests in charts_posture_presenter_test.cpp, on data/delivery/picture_doc.json — a trimmed copy of
// Mark 5 Oct swing 7 (the steep one of §2.4), analysed with the shoulder plane (fusion stage 9):
//
//   · the document's reading: the down-the-line frames at address, the top and P6 (in that order), the ball, the
//     address grip, and the trail shoulder the shoulderPlane metric was measured through — the view-plane angle of the
//     drawn ball → shoulder line IS the document's shoulderPlane (the camera is the uncalibrated default);
//   · the lines in the frame's own pixels: the address line starts at the ball's pixel, passes through the address
//     grip's and ends on the frame's edge; the shoulder plane likewise through the trail shoulder, dashed; the slot's
//     wash starts at the ball; all three frames carry the SAME marks (frozen at address); the crop takes in the ball;
//   · what is drawn without the ball (the shaft line alone, said), without the shoulder plane (said, "re-analyse"),
//     without a down-the-line camera (nothing at all) and without a document (said);
//   · the stills: an outPath per frame, pending until made, `src` once the file is there;
//   · the presenter: thisShot.picture on a synthetic shot pinned to the fixture, its frames' notes from the plane story,
//     the still maker filling `src` (synchronous).

#include "charts/charts_delivery_presenter.h"
#include "charts/delivery_picture.h"
#include "charts/posture_picture.h"
#include "charts/session_series_types.h"
#include "Club/shaft_plane_curve.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <cmath>

using namespace pinpoint::charts;
namespace pa = pinpoint::analysis;

namespace {

const QString kDir = QStringLiteral("/fixture/delivery/2026-10-05_Mark-Liversedge_Wrist_01/swing_0007");

QJsonObject fixture()
{
    QFile f(qEnvironmentVariable("PP_DELIVERY_PICTURE"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QVariantList framesOf(const QVariantMap &pic)
{
    const QVariantList g = pic.value(QStringLiteral("groups")).toList();
    return g.isEmpty() ? QVariantList{} : g.first().toMap().value(QStringLiteral("frames")).toList();
}

QVariantMap markOf(const QVariantMap &frame, const QString &label)
{
    for (const QVariant &m : frame.value(QStringLiteral("marks")).toList())
        if (m.toMap().value(QStringLiteral("label")).toString() == label) return m.toMap();
    return {};
}

QPointF ptOf(const QVariantMap &mark, int i)
{
    const QVariantList p = mark.value(QStringLiteral("pts")).toList().value(i).toList();
    return QPointF(p.value(0).toDouble(), p.value(1).toDouble());
}

// The frame's own px of a normalised point.
QPointF cropPx(const DeliveryPictureDoc &d, const QPointF &n) { return QPointF(n.x() * d.W - d.cropPx.x(), n.y() * d.H - d.cropPx.y()); }

// Distance of q from the line through a and b, px.
double offLine(const QPointF &a, const QPointF &b, const QPointF &q)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    return std::fabs(dx * (q.y() - a.y()) - dy * (q.x() - a.x())) / std::hypot(dx, dy);
}

// On the full frame's edge, in the crop's px.
bool onFrameEdge(const DeliveryPictureDoc &d, const QPointF &p)
{
    const double x = p.x() + d.cropPx.x(), y = p.y() + d.cropPx.y();
    return std::fabs(x) < 0.5 || std::fabs(y) < 0.5 || std::fabs(x - d.W) < 0.5 || std::fabs(y - d.H) < 0.5;
}

// A synthetic shot whose plane curve is flat at each position (the presenter test's form, one shot).
std::shared_ptr<ShotSeries> makeShot()
{
    constexpr qint64 kImpactUs = 2'000'000;
    constexpr int kPh[7] = { 0, 12, 8, 2, 13, 9, 5 };
    constexpr double kMs[7] = { -1200, -1000, -850, -600, -250, -120, 0 };
    const double val[7] = { 59, 54, 72, 68, 67, 65, 61 };   // §2.4's swing 7
    auto s = std::make_shared<ShotSeries>();
    s->ordinal = 7; s->shotId = 7; s->swingDir = kDir; s->club = QStringLiteral("GAP WEDGE"); s->impactUs = kImpactUs;
    for (int p = 0; p < 7; ++p) s->phases.push_back({ kPh[p], kImpactUs + qint64(kMs[p] * 1000), 1.0 });
    MetricSeries c;
    c.key = QStringLiteral("swingPlaneCurve"); c.unit = QStringLiteral("°"); c.sigma = 0.8;
    for (int k = 0; k <= 248; ++k) {
        const double m = -1200.0 + 5.0 * k;
        int p = 0;
        while (p < 6 && m > kMs[p + 1]) ++p;
        double v = val[6];
        if (p < 6) {
            const double a = kMs[p] + 25, b = kMs[p + 1] - 25;
            v = m <= a ? val[p] : m >= b ? val[p + 1] : val[p] + (val[p + 1] - val[p]) * (m - a) / (b - a);
        }
        c.tUs.push_back(kImpactUs + qint64(m * 1000)); c.value.push_back(v);
    }
    s->metrics.insert(c.key, c); s->metricOrder << c.key;
    MetricSeries sp;
    sp.key = QStringLiteral("shoulderPlane"); sp.unit = QStringLiteral("°");
    PhaseSample ps; ps.phase = 0; ps.value = 68.7; ps.tUs = s->phases.front().tUs;
    sp.phaseSamples.push_back(ps);
    s->metrics.insert(sp.key, sp); s->metricOrder << sp.key;
    return s;
}

} // namespace

class DeliveryPictureTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY2(!fixture().isEmpty(), "PP_DELIVERY_PICTURE");
    }

    // The document's reading: the frames, the ball, the grip, the shoulder the metric was read through.
    void document()
    {
        const DeliveryPictureDoc d = deliveryPictureDocFrom(fixture(), kDir);
        QVERIFY2(d.ok, qPrintable(d.why));
        QVERIFY(d.dtl);
        QCOMPARE(d.W, 576);
        QCOMPARE(d.H, 988);
        for (int k = 0; k < kDeliveryPictureFrames; ++k) {
            QVERIFY(d.posUs[size_t(k)] > 0);
            QVERIFY(d.frameIndex[size_t(k)] >= 0);
            if (k > 0) QVERIFY(d.posUs[size_t(k)] > d.posUs[size_t(k - 1)]);   // address, the top, P6
        }
        QVERIFY(d.hasBall && d.hasClub);
        // the grip is the measured sample 5.7 ms before address (the replay's nearest is a HELD frame 0.8 ms after it,
        // which its own rule refuses: the Posture picture reads no club here)
        QVERIFY(!pictureCameraDocFrom(fixture(), true).hasClub);
        QVERIFY(qAbs(d.grip.x() - 0.42881267802440887) < 1e-12);
        QVERIFY(qAbs(d.ball.x() * 576 - 423.22662889518415) < 1e-6);
        QVERIFY(d.shoulderMetric);
        QVERIFY(qAbs(d.shoulderDeg - 68.67619842753514) < 1e-9);
        QVERIFY2(d.hasShoulder, qPrintable(d.shoulderWhy));
        QVERIFY(d.shoulderFrames >= 3);
        // the trail (right) shoulder sits above and behind the ball, the grip between them
        QVERIFY(d.shoulder.y() < d.grip.y() && d.grip.y() < d.ball.y());
        // the line drawn through it IS the metric: its view-plane angle on the fusion's (uncalibrated) camera
        const double fw = 576, fh = 988;
        const double deg = pa::planecurve::shoulderPlaneInclDeg(pa::fusion::dtlCamera(0.0, 0.0, 0.0), d.shoulder.x() * fw, d.shoulder.y() * fh,
                                                                d.ball.x() * fw, d.ball.y() * fh);
        QVERIFY2(qAbs(deg - d.shoulderDeg) < 0.05, qPrintable(QStringLiteral("drawn %1°, the metric %2°").arg(deg).arg(d.shoulderDeg)));
        // the crop takes in the ball
        QVERIFY(d.cropPx.isValid() && QRect(0, 0, d.W, d.H).contains(d.cropPx));
        QVERIFY(d.cropPx.contains(QPoint(int(d.ball.x() * d.W), int(d.ball.y() * d.H))));
    }

    // The lines in the frame's own pixels, frozen on all three frames.
    void lines()
    {
        const DeliveryPictureDoc d = deliveryPictureDocFrom(fixture(), kDir);
        const QVariantMap pic = deliveryPicture(d, DeliveryPictureOptions{});
        QVERIFY(pic.value(QStringLiteral("available")).toBool());
        QVERIFY(pic.value(QStringLiteral("why")).toStringList().isEmpty());
        QCOMPARE(pic.value(QStringLiteral("defaultSets")).toMap().value(QStringLiteral("plane")).toBool(), true);
        const QVariantMap lines = pic.value(QStringLiteral("lines")).toMap();
        QVERIFY(lines.value(QStringLiteral("address")).toBool() && lines.value(QStringLiteral("shoulders")).toBool() && lines.value(QStringLiteral("slot")).toBool());
        QVERIFY(pic.value(QStringLiteral("legend")).toString().contains(QStringLiteral("slot")));
        const QVariantList frames = framesOf(pic);
        QCOMPARE(frames.size(), 3);
        const QStringList tags{ QStringLiteral("P1"), QStringLiteral("P4"), QStringLiteral("P6") };
        const QPointF ball = cropPx(d, d.ball), grip = cropPx(d, d.grip), shoulder = cropPx(d, d.shoulder);
        for (int k = 0; k < 3; ++k) {
            const QVariantMap f = frames[k].toMap();
            QCOMPARE(f.value(QStringLiteral("tag")).toString(), tags[k]);
            QCOMPARE(f.value(QStringLiteral("seekUs")).toLongLong(), d.posUs[size_t(k)]);
            QCOMPARE(f.value(QStringLiteral("width")).toInt(), d.cropPx.width());
            // the same marks on every frame, in the same order: the wash under the lines
            QCOMPARE(f.value(QStringLiteral("marks")), frames[0].toMap().value(QStringLiteral("marks")));
            const QVariantList marks = f.value(QStringLiteral("marks")).toList();
            QCOMPARE(marks.size(), 3);
            QCOMPARE(marks[0].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("poly"));
            for (const QVariant &m : marks) {
                QCOMPARE(m.toMap().value(QStringLiteral("role")).toString(), QStringLiteral("coach"));
                QCOMPARE(m.toMap().value(QStringLiteral("set")).toString(), QStringLiteral("plane"));
            }
            const QVariantMap a = markOf(f, QStringLiteral("address line")), s = markOf(f, QStringLiteral("shoulder plane"));
            const QVariantMap w = markOf(f, QStringLiteral("the slot"));
            QVERIFY(!a.isEmpty() && !s.isEmpty() && !w.isEmpty());
            QCOMPARE(a.value(QStringLiteral("dash")).toString(), QString());
            QCOMPARE(s.value(QStringLiteral("dash")).toString(), QStringLiteral("dash"));
            // from the ball's pixel, through the grip's / the shoulder's, to the frame's edge
            QVERIFY(QLineF(ptOf(a, 0), ball).length() < 1e-6);
            QVERIFY(QLineF(ptOf(s, 0), ball).length() < 1e-6);
            QVERIFY2(offLine(ptOf(a, 0), ptOf(a, 1), grip) < 0.01, "the address line misses the grip");
            QVERIFY2(offLine(ptOf(s, 0), ptOf(s, 1), shoulder) < 0.01, "the shoulder plane misses the shoulder");
            QVERIFY(onFrameEdge(d, ptOf(a, 1)) && onFrameEdge(d, ptOf(s, 1)));
            // beyond the grip, not back through the ball
            QVERIFY(QLineF(ptOf(a, 1), ball).length() > QLineF(grip, ball).length());
            QVERIFY(QLineF(ptOf(s, 1), ball).length() > QLineF(shoulder, ball).length());
            // the wash: the ball, then out along the address line and the shoulder plane
            QVERIFY(QLineF(ptOf(w, 0), ball).length() < 1e-6);
            QVERIFY(offLine(ball, grip, ptOf(w, 1)) < 0.05);
            QVERIFY(offLine(ball, shoulder, ptOf(w, 2)) < 0.05);
        }
    }

    // No ball: the shaft line at address alone, edge to edge, said; no shoulder plane and no slot.
    void noBall()
    {
        QJsonObject root = fixture();
        QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
        QJsonObject cd = an.value(QStringLiteral("clubDtl")).toObject();
        cd.insert(QStringLiteral("summary"), QJsonObject{ { QStringLiteral("ball"), QJsonObject{ { QStringLiteral("found"), false } } } });
        an.insert(QStringLiteral("clubDtl"), cd);
        root.insert(QStringLiteral("analysis"), an);
        const DeliveryPictureDoc d = deliveryPictureDocFrom(root, kDir);
        QVERIFY(!d.hasBall && d.hasClub && !d.hasShoulder);
        const QVariantMap pic = deliveryPicture(d, DeliveryPictureOptions{});
        QVERIFY(pic.value(QStringLiteral("available")).toBool());
        const QVariantMap f = framesOf(pic).value(1).toMap();
        QCOMPARE(f.value(QStringLiteral("marks")).toList().size(), 1);
        const QVariantMap a = markOf(f, QStringLiteral("address shaft line"));
        QVERIFY(!a.isEmpty());
        QVERIFY(onFrameEdge(d, ptOf(a, 0)) && onFrameEdge(d, ptOf(a, 1)));
        QVERIFY(offLine(ptOf(a, 0), ptOf(a, 1), cropPx(d, d.grip)) < 0.01);
        QVERIFY(offLine(ptOf(a, 0), ptOf(a, 1), cropPx(d, d.head)) < 0.01);
        QVERIFY(!pic.value(QStringLiteral("lines")).toMap().value(QStringLiteral("slot")).toBool());
        QVERIFY(pic.value(QStringLiteral("why")).toStringList().join(QLatin1Char(' ')).contains(QStringLiteral("ball was not found")));
    }

    // No shoulderPlane in the document (an analysis before fusion stage 9): the address line alone, said.
    void noShoulderPlane()
    {
        QJsonObject root = fixture();
        QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
        an.insert(QStringLiteral("metrics"), QJsonArray{});
        root.insert(QStringLiteral("analysis"), an);
        const DeliveryPictureDoc d = deliveryPictureDocFrom(root, kDir);
        QVERIFY(d.hasBall && !d.shoulderMetric && !d.hasShoulder);
        const QVariantMap pic = deliveryPicture(d, DeliveryPictureOptions{});
        const QVariantMap f = framesOf(pic).value(0).toMap();
        QCOMPARE(f.value(QStringLiteral("marks")).toList().size(), 1);
        QVERIFY(!markOf(f, QStringLiteral("address line")).isEmpty());
        const QString why = pic.value(QStringLiteral("why")).toStringList().join(QLatin1Char(' '));
        QVERIFY2(why.contains(QStringLiteral("before the shoulder plane was measured")) && why.contains(QStringLiteral("Re-analyse")), qPrintable(why));
        QVERIFY(!pic.value(QStringLiteral("legend")).toString().contains(QStringLiteral("slot")));
    }

    // No down-the-line camera: nothing at all. No document: said.
    void noDtlAndUnread()
    {
        QJsonObject root = fixture();
        root.insert(QStringLiteral("streams"), QJsonArray{});
        const QVariantMap pic = deliveryPicture(deliveryPictureDocFrom(root, kDir), DeliveryPictureOptions{});
        QVERIFY(!pic.value(QStringLiteral("available")).toBool());
        QVERIFY(pic.value(QStringLiteral("groups")).toList().isEmpty());
        QVERIFY(pic.value(QStringLiteral("why")).toStringList().isEmpty());
        const QVariantMap none = deliveryPicture(deliveryPictureDocFrom(QJsonObject{}, QStringLiteral("/x")), DeliveryPictureOptions{});
        QVERIFY(!none.value(QStringLiteral("available")).toBool());
        QVERIFY(!none.value(QStringLiteral("why")).toStringList().isEmpty());
    }

    // The stills' cache: an outPath per frame, pending until made, `src` once the file is there.
    void stills()
    {
        QTemporaryDir tmp;
        DeliveryPictureOptions po;
        po.stillDir = tmp.path();
        const DeliveryPictureDoc d = deliveryPictureDocFrom(fixture(), kDir);
        const QVariantMap pic = deliveryPicture(d, po);
        QVERIFY(pic.value(QStringLiteral("pending")).toBool());
        const QVariantMap f = framesOf(pic).value(2).toMap();
        const QVariantMap still = f.value(QStringLiteral("still")).toMap();
        QCOMPARE(still.value(QStringLiteral("camera")).toString(), QStringLiteral("dtl"));
        QCOMPARE(still.value(QStringLiteral("phase")).toString(), QStringLiteral("P6"));
        QCOMPARE(still.value(QStringLiteral("w")).toInt(), d.cropPx.width());
        const QString out = still.value(QStringLiteral("outPath")).toString();
        QVERIFY(out.startsWith(tmp.path()));
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QVERIFY(img.save(out, "JPG"));
        const QVariantMap again = framesOf(deliveryPicture(d, po)).value(2).toMap();
        QCOMPARE(QUrl(again.value(QStringLiteral("src")).toString()).toLocalFile(), out);
    }

    // The presenter: thisShot.picture, the notes from the plane story, the still maker filling `src`.
    void presenter()
    {
        QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("delivery-picture"))).removeRecursively();
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = QFileInfo(kDir).path();
        snap->shots.push_back(makeShot());
        int made = 0;
        ChartsDeliveryPresenter::setStillMaker([&made](const QVariantList &stills) {
            QHash<QString, QString> errs;
            for (const QVariant &v : stills) {
                const QVariantMap s = v.toMap();
                QImage img(qMax(8, s.value(QStringLiteral("w")).toInt() / 4), qMax(8, s.value(QStringLiteral("h")).toInt() / 4), QImage::Format_RGB32);
                img.fill(Qt::gray);
                QDir().mkpath(QFileInfo(s.value(QStringLiteral("outPath")).toString()).absolutePath());
                if (img.save(s.value(QStringLiteral("outPath")).toString(), "JPG")) ++made;
                else errs.insert(s.value(QStringLiteral("outPath")).toString(), QStringLiteral("could not write"));
            }
            return errs;
        });
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        p.setPickedSwingDir(kDir);
        p.setPictureDocumentForTest(kDir, qEnvironmentVariable("PP_DELIVERY_PICTURE"));
        const QVariantMap t = p.thisShot();
        ChartsDeliveryPresenter::setStillMaker({});
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        const QVariantMap pic = t.value(QStringLiteral("picture")).toMap();
        QVERIFY(pic.value(QStringLiteral("available")).toBool());
        QCOMPARE(made, 3);
        QVERIFY(!pic.value(QStringLiteral("pending")).toBool());
        const QVariantList frames = framesOf(pic);
        QCOMPARE(frames.size(), 3);
        for (const QVariant &fv : frames)
            QVERIFY(QFileInfo::exists(QUrl(fv.toMap().value(QStringLiteral("src")).toString()).toLocalFile()));
        // the notes are the plane story's: the two lines at address; the plane and where it sat at the top and P6
        QCOMPARE(frames[0].toMap().value(QStringLiteral("note")).toString(), QStringLiteral("address line 59°  ·  shoulder plane 69°"));
        QCOMPARE(frames[1].toMap().value(QStringLiteral("note")).toString(), QStringLiteral("plane 68°  ·  inside the slot"));
        QCOMPARE(frames[2].toMap().value(QStringLiteral("note")).toString(), QStringLiteral("plane 65°  ·  inside the slot"));

        // a shot whose document is not there: no frames, said
        p.setPictureDocumentForTest(kDir, QString());
        const QVariantMap gone = p.thisShot().value(QStringLiteral("picture")).toMap();
        QVERIFY(!gone.value(QStringLiteral("available")).toBool());
        QVERIFY(gone.value(QStringLiteral("why")).toStringList().value(0).contains(QStringLiteral("not on disk")));
    }
};

QTEST_GUILESS_MAIN(DeliveryPictureTest)
#include "delivery_picture_test.moc"
