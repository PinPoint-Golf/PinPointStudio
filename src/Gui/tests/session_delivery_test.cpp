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

// The Club delivery section's core (session_delivery.h; club_delivery_chart_design.md stage 2) on rows cut from the
// evidence CSV (data/delivery/club_delivery_rows.json, PP_DELIVERY_ROWS):
//
//   · §2.2's per-golfer slopes, r and scatter to 0.01, as `club_delivery_extract.py fits` recomputes them: Mark 5 Oct
//     (28 monitor shots) and Jess 20 Oct (16) shown, Nick 20 Oct (17) refused, said as a finding;
//   · §2.4's two plane stories (Mark 5 Oct swings 7 and 26) word for word, their slot, published shape and head;
//   · a no-monitor session (Mark 7 Oct): no fit, the §4.3 rule 3 head against the golfer's usual;
//   · a no-DTL shot (Mark 4 Aug driver): the monitor's shot shape and the rule 4 head only;
//   · lie → face on a 7-iron (Nick, the monitor's lie and loft), a driver (the lie barely matters) and the camera's
//     fallback (shaftLie P1 → P7, the club's factory loft);
//   · the monitor first: a shot with lm.attackAngle says the monitor's; one without says the camera's;
//   · the hands' stretches of the curve (decision 5) and the sampler on a synthetic shot.

#include "charts/session_delivery.h"
#include "charts/session_series_types.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <vector>

using namespace pinpoint::charts;

namespace {

constexpr double N = std::numeric_limits<double>::quiet_NaN();
int dp2(double d) { return int(std::lround(d * 100.0)); }

double num(const QJsonValue &v) { return v.isDouble() ? v.toDouble() : N; }

struct Row { QString golfer, session, swing; DeliveryShotInput in; };

std::vector<Row> loadRows()
{
    std::vector<Row> out;
    QFile f(qEnvironmentVariable("PP_DELIVERY_ROWS"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonArray rows = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("rows")).toArray();
    for (const QJsonValue &rv : rows) {
        const QJsonObject r = rv.toObject();
        Row row;
        row.golfer = r.value(QStringLiteral("golfer")).toString();
        row.session = r.value(QStringLiteral("session")).toString();
        row.swing = r.value(QStringLiteral("swing")).toString();
        DeliveryShotInput &in = row.in;
        in.swingDir = row.session + QLatin1Char('/') + row.swing;
        in.shotId = row.swing.mid(6).toInt();
        in.club = r.value(QStringLiteral("club")).toString();
        in.nominalLoft = deliveryNominalLoft(in.club);
        const QJsonArray pl = r.value(QStringLiteral("plane")).toArray(), ha = r.value(QStringLiteral("hands")).toArray();
        for (int p = 0; p < kDeliveryPositions; ++p) { in.plane[size_t(p)] = num(pl.at(p)); in.hands[size_t(p)] = num(ha.at(p)); }
        in.steepening    = num(r.value(QStringLiteral("steepening")));
        in.shoulderPlane = num(r.value(QStringLiteral("shoulderPlane")));
        in.lieP1         = num(r.value(QStringLiteral("lieP1")));
        in.lieP7         = num(r.value(QStringLiteral("lieP7")));
        in.shaftLean     = num(r.value(QStringLiteral("shaftLean")));
        in.lmPath        = num(r.value(QStringLiteral("lm.clubPath")));
        in.lmFace        = num(r.value(QStringLiteral("lm.faceAngle")));
        in.lmFaceToPath  = num(r.value(QStringLiteral("lm.faceToPath")));
        in.lmAttack      = num(r.value(QStringLiteral("lm.attackAngle")));
        in.lmLoft        = num(r.value(QStringLiteral("lm.dynamicLoft")));
        in.lmLie         = num(r.value(QStringLiteral("lm.lieAngle")));
        out.push_back(row);
    }
    return out;
}

// One golfer's shots from sessions starting with `prefix` (and of `club`, when given), numbered in order.
std::vector<DeliveryShotInput> shotsOf(const std::vector<Row> &rows, const QString &golfer, const QString &prefix,
                                       const QString &club = QString())
{
    std::vector<DeliveryShotInput> out;
    for (const Row &r : rows) {
        if (!r.golfer.startsWith(golfer) || !r.session.startsWith(prefix)) continue;
        if (!club.isEmpty() && r.in.club != club) continue;
        out.push_back(r.in);
        out.back().ordinal = int(out.size());
    }
    return out;
}

const DeliveryShotInput *swingOf(const std::vector<DeliveryShotInput> &shots, const QString &swing)
{
    for (const DeliveryShotInput &s : shots) if (s.swingDir.endsWith(swing)) return &s;
    return nullptr;
}

QString linesText(const std::vector<DeliveryImpactLine> &lines, const QString &key)
{
    for (const DeliveryImpactLine &l : lines) if (l.key == key) return l.text;
    return QString();
}
const DeliveryImpactLine *lineOf(const std::vector<DeliveryImpactLine> &lines, const QString &key)
{
    for (const DeliveryImpactLine &l : lines) if (l.key == key) return &l;
    return nullptr;
}

} // namespace

class SessionDeliveryTest : public QObject
{
    Q_OBJECT

    std::vector<Row> m_rows;
    std::vector<DeliveryShotInput> m_mark, m_jess, m_nick, m_mark7, m_driver;

private slots:
    void initTestCase()
    {
        m_rows = loadRows();
        QVERIFY2(m_rows.size() == 138, qPrintable(QStringLiteral("the fixture has %1 rows").arg(m_rows.size())));
        m_mark   = shotsOf(m_rows, QStringLiteral("Mark"), QStringLiteral("2026-10-05"));
        m_jess   = shotsOf(m_rows, QStringLiteral("Jess"), QStringLiteral("2026-10-20"));
        m_nick   = shotsOf(m_rows, QStringLiteral("Nick"), QStringLiteral("2026-10-20"));
        m_mark7  = shotsOf(m_rows, QStringLiteral("Mark"), QStringLiteral("2026-10-07"));
        m_driver = shotsOf(m_rows, QStringLiteral("Mark"), QStringLiteral("2026-08-04"), QStringLiteral("DRIVER"));
        QCOMPARE(int(m_mark.size()), 31);
        QCOMPARE(int(m_jess.size()), 23);
        QCOMPARE(int(m_nick.size()), 26);
        QCOMPARE(int(m_mark7.size()), 50);
        QCOMPARE(int(m_driver.size()), 8);
    }

    // §2.2, recomputed by `club_delivery_extract.py fits` on the committed CSV.
    void fitsReproduceTheEvidence_data()
    {
        QTest::addColumn<QString>("who");
        QTest::addColumn<int>("which");     // 0 path~P6, 1 path~steepening, 2 attack~P6
        QTest::addColumn<int>("n");
        QTest::addColumn<int>("slope");     // × 100
        QTest::addColumn<int>("r");         // × 100
        QTest::addColumn<int>("sd");        // × 100
        QTest::addColumn<QString>("verdict");
        QTest::newRow("Mark path~P6")      << "Mark" << 0 << 28 << -92  << -80 << 143 << "shown";
        QTest::newRow("Mark path~steep")   << "Mark" << 1 << 28 << -102 << -79 << 144 << "shown";
        QTest::newRow("Mark attack~P6")    << "Mark" << 2 << 28 << -51  << -79 << 82  << "shown";
        QTest::newRow("Jess path~P6")      << "Jess" << 0 << 16 << -59  << -63 << 79  << "shown";
        QTest::newRow("Jess path~steep")   << "Jess" << 1 << 16 << 2    << 2   << 102 << "weak";
        QTest::newRow("Jess attack~P6")    << "Jess" << 2 << 16 << -56  << -66 << 70  << "shown";
        QTest::newRow("Nick path~P6")      << "Nick" << 0 << 17 << -33  << -22 << 140 << "weak";
        QTest::newRow("Nick path~steep")   << "Nick" << 1 << 17 << -24  << -21 << 141 << "weak";
        QTest::newRow("Nick attack~P6")    << "Nick" << 2 << 17 << 11   << 18  << 59  << "weak";
    }
    void fitsReproduceTheEvidence()
    {
        QFETCH(QString, who); QFETCH(int, which); QFETCH(int, n); QFETCH(int, slope); QFETCH(int, r); QFETCH(int, sd);
        QFETCH(QString, verdict);
        const auto &shots = who == QLatin1String("Mark") ? m_mark : who == QLatin1String("Jess") ? m_jess : m_nick;
        const DeliveryFits fits = deliveryFits(shots);
        const DeliveryFit &f = which == 0 ? fits.pathP6 : which == 1 ? fits.pathSteep : fits.attackP6;
        QCOMPARE(f.n, n);
        QCOMPARE(dp2(f.slope), slope);
        QCOMPARE(dp2(f.r), r);
        QCOMPARE(dp2(f.sd), sd);
        QCOMPARE(f.verdict, verdict);
        QCOMPARE(f.holds, verdict == QLatin1String("shown"));
        QCOMPARE(int(f.ordinals.size()), n);
    }

    // The three verdicts, as the section says them (§4.4, §4.7).
    void fitVerdictsInWords()
    {
        const DeliveryFits mark = deliveryFits(m_mark), jess = deliveryFits(m_jess), nick = deliveryFits(m_nick);
        // decision 7: the stronger path fit — the plane at P6 for all three (Mark: |r| 0.7996 against 0.7943)
        QCOMPARE(mark.path().x, DeliveryX::PlaneP6);
        QCOMPARE(jess.path().x, DeliveryX::PlaneP6);
        QCOMPARE(nick.path().x, DeliveryX::PlaneP6);
        QCOMPARE(deliveryFitSentence(mark.path()),
                 QStringLiteral("On your 28 shots with the monitor, each degree steeper at P6 moves your path 0.9° left."));
        QCOMPARE(deliveryFitSentence(jess.path()),
                 QStringLiteral("On your 16 shots with the monitor, each degree steeper at P6 moves your path 0.6° left."));
        QCOMPARE(deliveryFitSentence(nick.path()),
                 QStringLiteral("On your 17 shots your path doesn't follow your plane (r −0.22) — the cause is elsewhere."));
        QCOMPARE(deliveryAttackSentence(mark.attackP6), QStringLiteral("Each degree steeper at P6 makes your attack 0.5° steeper."));
        QCOMPARE(deliveryAttackSentence(nick.attackP6), QString());

        QCOMPARE(deliverySession(m_mark).head,
                 QStringLiteral("Your path follows your plane: steeper at P6 by a degree, 0.9° further left (28 shots)."));
        QCOMPARE(deliverySession(m_jess).head,
                 QStringLiteral("Your path follows your plane: steeper at P6 by a degree, 0.6° further left (16 shots)."));
        QCOMPARE(deliverySession(m_nick).head,
                 QStringLiteral("On your 17 shots your path doesn't follow your plane (r −0.22) — the cause is elsewhere."));
        // the report's noun
        QCOMPARE(deliverySession(m_nick, QStringLiteral("swings")).head,
                 QStringLiteral("On your 17 swings your path doesn't follow your plane (r −0.22) — the cause is elsewhere."));

        // too few: the first seven of Mark's monitor shots
        std::vector<DeliveryShotInput> seven;
        for (const DeliveryShotInput &s : m_mark) if (s.hasPath() && fin7(s) && seven.size() < 7) seven.push_back(s);
        const DeliveryFit few = deliveryFits(seven).path();
        QCOMPARE(few.verdict, QStringLiteral("tooFew"));
        QCOMPARE(deliveryFitSentence(few),
                 QStringLiteral("12 shots with the monitor are needed to see how your path follows your plane; this session has 7."));
        // the physical sign: Mark's path turned around refuses a strong fit
        std::vector<DeliveryShotInput> flipped = m_mark;
        for (DeliveryShotInput &s : flipped) s.lmPath = -s.lmPath;
        QCOMPARE(deliveryFits(flipped).pathP6.verdict, QStringLiteral("wrongSign"));
    }

    // §2.4: Mark 5 Oct swings 7 and 26.
    void planeStoriesWordForWord()
    {
        const DeliveryShotInput *s7 = swingOf(m_mark, QStringLiteral("swing_0007"));
        const DeliveryShotInput *s26 = swingOf(m_mark, QStringLiteral("swing_0026"));
        QVERIFY(s7 && s26);
        const DeliveryStory a = deliveryPlaneStory(*s7), b = deliveryPlaneStory(*s26);
        QCOMPARE(a.text, QStringLiteral("set on 59° · 13° steeper at P3 · peaked at the top · delivered at 61°, 2° above where you set it. "
                                        "Hands 12° steeper coming down."));
        QCOMPARE(b.text, QStringLiteral("set on 57° · 3° steeper at P3 · peaked at the top · delivered at 58°, where you set it. "
                                        "Hands came down on the plane they went up."));
        QCOMPARE(a.shapeWord, QStringLiteral("is steep from P3"));
        QCOMPARE(b.shapeWord, QStringLiteral("matches"));
        QCOMPARE(a.shapeLine, QStringLiteral("Most good players go back a little under the plane they deliver on, reach the top a few "
                                             "degrees above it and are back on it by impact; yours is steep from P3."));
        // the slot: swing 7's P3 (72°) is above its shoulder plane (69°); swing 26 stays inside
        QCOMPARE(deliverySlotWords(*s7).text, QStringLiteral("above the shoulder plane at P3 — a steep backswing"));
        QCOMPARE(deliverySlotWords(*s26).text, QStringLiteral("inside the slot"));

        // the head (§4.3 rule 1: the plane at P6 is Mark's stronger fit) and the ball-flight line
        const DeliveryShotView v7 = deliveryShot(*s7, m_mark), v26 = deliveryShot(*s26, m_mark);
        QCOMPARE(v7.head.rule, 1);
        QCOMPARE(v7.head.text, QStringLiteral("At P6 your plane was 65°, in your steepest third, and the path went 4.3° left."));
        QCOMPARE(v26.head.text, QStringLiteral("At P6 your plane was 58°, in your flattest third, and the path went 3.5° right."));
        QCOMPARE(v7.thisLine, QStringLiteral("This one: 65° at P6, your steepest third → path −4.3°."));
        QCOMPARE(v26.thisLine, QStringLiteral("This one: 58° at P6, your flattest third → path +3.5°."));
        QCOMPARE(v7.mode, QStringLiteral("full"));
        // the 5 Oct monitor gave no face, lie or loft: no shot shape, and the trust fold says so
        QVERIFY(!v7.shape.available);
        QVERIFY(v7.trust.lines.join(QLatin1Char('\n')).contains(QStringLiteral("On this session it gave no face, no lie and no loft.")));

        // a shot with no monitor on a session whose fit holds: the fit's prediction, said as one (§4.4)
        DeliveryShotInput bare = *s7;
        bare.lmPath = bare.lmAttack = N;
        const DeliveryShotView vb = deliveryShot(bare, m_mark);
        QCOMPARE(vb.head.rule, 3);
        QCOMPARE(vb.thisLine, QStringLiteral("This one: 65° at P6, your steepest third; on your fit, a plane like this goes with a path about 4° left."));
    }

    // Mark 7 Oct: no monitor on the session — no fit, and the head is the story's largest departure from the usual.
    void noMonitorShot()
    {
        const DeliverySessionView sv = deliverySession(m_mark7);
        QCOMPARE(sv.fits.pathP6.n, 0);
        QCOMPARE(sv.monitorShots, 0);
        QCOMPARE(sv.planeShots, 47);
        QVERIFY(sv.trust.lines.contains(QStringLiteral("No launch monitor reading on this session: the plane is not linked to the ball.")));
        const auto head = [&](const QString &swing) {
            const DeliveryShotInput *s = swingOf(m_mark7, swing);
            return s ? deliveryShot(*s, m_mark7) : DeliveryShotView{};
        };
        const DeliveryShotView v1 = head(QStringLiteral("swing_0001"));
        QCOMPARE(v1.mode, QStringLiteral("noMonitor"));
        QCOMPARE(v1.head.rule, 3);
        QCOMPARE(v1.head.text, QStringLiteral("You went back 8° steeper than your usual at P3."));
        QCOMPARE(head(QStringLiteral("swing_0006")).head.text, QStringLiteral("Your plane was where it usually is."));
        QCOMPARE(head(QStringLiteral("swing_0008")).head.text, QStringLiteral("Your hands steepened 3° less than your usual coming down."));
        QVERIFY(!v1.flightAvailable);
        QVERIFY(v1.story.available);
        // the session's own band and where it varies
        QVERIFY(sv.band.shown);
        QVERIFY(sv.varies.has);
        QVERIFY(sv.varies.words.endsWith(QStringLiteral(").")));
        QVERIFY(sv.head.startsWith(QStringLiteral("Your plane: set on 59°, delivered at 60°, hands 5° steeper coming down (47 shots).")));
    }

    // Mark 4 Aug driver: the monitor only (§4.3 rule 4): the grid and its head, no story, no fit.
    void noDtlShot()
    {
        const DeliveryShotInput *s = swingOf(m_driver, QStringLiteral("swing_0007"));
        QVERIFY(s);
        const DeliveryShotView v = deliveryShot(*s, m_driver);
        QVERIFY(v.available);
        QCOMPARE(v.mode, QStringLiteral("noDtl"));
        QVERIFY(!v.story.available);
        QVERIFY(!v.slot.available);
        QVERIFY(!v.flightAvailable);
        QVERIFY(v.shape.available);
        QCOMPARE(v.shape.start, 1);
        QCOMPARE(v.shape.curve, 1);
        QCOMPARE(v.shape.word, QStringLiteral("Push, fade"));
        QCOMPARE(v.head.rule, 4);
        QCOMPARE(v.head.text, QStringLiteral("Push, fade: face 2.4° right, 3.2° open to the path."));
        QVERIFY(v.trust.lines.join(QLatin1Char('\n')).contains(QStringLiteral("The swing plane was not read on this shot")));
        // the grid's words, and their mirror for a left-handed golfer
        DeliveryShotInput g = *s;
        g.lmFace = -2.0; g.lmFaceToPath = 1.5;
        QCOMPARE(deliveryShotShape(g).word, QStringLiteral("Pull, slight fade"));
        g.leftHanded = true;
        QCOMPARE(deliveryShotShape(g).word, QStringLiteral("Push, slight draw"));
        g.leftHanded = false; g.lmFace = 0.4; g.lmFaceToPath = -0.6;
        QCOMPARE(deliveryShotShape(g).word, QStringLiteral("Straight"));
        g.lmFaceToPath = N; g.lmPath = 2.0;   // face − path = −1.6: a slight draw
        QCOMPARE(deliveryShotShape(g).word, QStringLiteral("Straight start, slight draw"));
        // the session: every shot's shape, the commonest named
        const DeliverySessionView sv = deliverySession(m_driver);
        QCOMPARE(sv.planeShots, 0);
        QCOMPARE(int(sv.shapes.size()), 8);
        QVERIFY(!sv.shapeLine.isEmpty());
        QCOMPARE(sv.head, sv.shapeLine + QStringLiteral("."));
    }

    // §4.5: Δface ≈ Δlie × sin(loft).
    void lieTurnsTheFace()
    {
        // Nick 20 Oct 7-iron swing 1 (session 02): the monitor's lie −3.19 (toe down) and loft 32.16
        DeliveryShotInput nick;
        for (const Row &r : m_rows)
            if (r.golfer.startsWith(QLatin1String("Nick")) && r.session.endsWith(QLatin1String("_02")) && r.swing == QLatin1String("swing_0001")) nick = r.in;
        QVERIFY(std::isfinite(nick.lmLie));
        const std::vector<DeliveryImpactLine> ln = deliveryImpactLines(nick);
        QCOMPARE(linesText(ln, QStringLiteral("lie")), QStringLiteral("Toe 3.2° down at impact → face about 1.7° right on this 7-iron."));
        const DeliveryImpactLine *lie = lineOf(ln, QStringLiteral("lie"));
        QCOMPARE(lie->source, QStringLiteral("monitor"));
        QCOMPARE(lie->loftSource, QStringLiteral("monitor"));
        QCOMPARE(dp2(lie->face), -170);

        // the driver: 2.2° toe up on 12.7° of loft
        const DeliveryShotInput *drv = swingOf(m_driver, QStringLiteral("swing_0007"));
        QCOMPARE(linesText(deliveryImpactLines(*drv), QStringLiteral("lie")),
                 QStringLiteral("Toe 2.2° up at impact → face about 0.5° left: on a driver the lie barely matters."));
        // a driver with no monitor loft: the club's factory loft (10°)
        DeliveryShotInput d2 = *drv;
        d2.lmLoft = N;
        const std::vector<DeliveryImpactLine> ld2 = deliveryImpactLines(d2);
        const DeliveryImpactLine *l2 = lineOf(ld2, QStringLiteral("lie"));
        QCOMPARE(l2->loftSource, QStringLiteral("club"));
        QCOMPARE(l2->loft, 10.0);
        QVERIFY(l2->text.endsWith(QStringLiteral("on a driver the lie barely matters.")));

        // no monitor lie: the camera's shaftLie P1 → P7 on the gap wedge's factory loft (52°)
        const DeliveryShotInput *s7 = swingOf(m_mark, QStringLiteral("swing_0007"));
        const std::vector<DeliveryImpactLine> l7 = deliveryImpactLines(*s7);
        QCOMPARE(linesText(l7, QStringLiteral("lie")),
                 QStringLiteral("Shaft 2° more upright at impact than at address → face about 1.2° left on this gap wedge."));
        QCOMPARE(lineOf(l7, QStringLiteral("lie"))->source, QStringLiteral("camera"));
        // lean, with no monitor loft
        QCOMPARE(linesText(l7, QStringLiteral("lean")), QStringLiteral("Hands 7° ahead."));
        // Jess's monitor carries loft: lean beside it
        const DeliveryShotInput *j = nullptr;
        for (const DeliveryShotInput &s : m_jess) if (std::isfinite(s.shaftLean) && std::isfinite(s.lmLoft)) { j = &s; break; }
        QVERIFY(j);
        QVERIFY(linesText(deliveryImpactLines(*j), QStringLiteral("lean")).contains(QStringLiteral("; the monitor saw ")));
    }

    // The monitor first: lm.attackAngle where it was read; the camera's only where it was not.
    void monitorFirstThenCamera()
    {
        DeliveryShotInput s = *swingOf(m_mark, QStringLiteral("swing_0007"));
        s.ourAttack = -2.0;
        const std::vector<DeliveryImpactLine> la = deliveryImpactLines(s);
        const DeliveryImpactLine *a = lineOf(la, QStringLiteral("attack"));
        QVERIFY(a);
        QCOMPARE(a->source, QStringLiteral("monitor"));
        QCOMPARE(dp2(a->value), -545);
        QCOMPARE(a->text, QStringLiteral("Attack −5.4° (the monitor's)."));
        s.lmAttack = N;
        const std::vector<DeliveryImpactLine> lb = deliveryImpactLines(s);
        const DeliveryImpactLine *b = lineOf(lb, QStringLiteral("attack"));
        QCOMPARE(b->source, QStringLiteral("camera"));
        QCOMPARE(b->value, -2.0);
        QCOMPARE(b->text, QStringLiteral("Attack −2° (the camera's)."));
        // and the low point beside it, the monitor's first
        s.lowPoint = 1.24; s.lmLowPoint = -0.5;
        const std::vector<DeliveryImpactLine> lc = deliveryImpactLines(s);
        const DeliveryImpactLine *c = lineOf(lc, QStringLiteral("attack"));
        QCOMPARE(c->text, QStringLiteral("Attack −2° (the camera's) · low point 0.5 in behind the ball."));
        QCOMPARE(c->source, QStringLiteral("both"));
        // the fits never read the camera: a shot with only our path is not a monitor point
        DeliveryShotInput cam = s;
        cam.lmPath = N; cam.ourPath = -3.0;
        QVERIFY(!std::isfinite(deliveryYOf(cam, DeliveryY::Path)));
    }

    // Decision 5: the hands carry the curve where it sits on the hand plane, after P1, for ≥ 20 ms.
    void handsCarryTheCurve()
    {
        std::vector<double> t, c, h;
        for (int i = 0; i <= 60; ++i) {
            t.push_back(-300.0 + 5.0 * i);
            c.push_back(60.0);
            // on the hand plane over [−250, −200] (11 points) and for one lone point at −100 (too short)
            const double tt = t.back();
            h.push_back((tt >= -250 && tt <= -200) || tt == -100 ? 60.2 : 57.0);
        }
        const std::vector<DeliverySpan> sp = deliveryHandsSpans(t, c, {}, t, h, {}, -280.0);
        QCOMPARE(int(sp.size()), 1);
        QCOMPARE(sp[0].t0Ms, -250.0);
        QCOMPARE(sp[0].t1Ms, -200.0);
        // a bridged stretch of the curve is never the hands'
        std::vector<uint8_t> valid(t.size(), 1);
        for (size_t i = 0; i < t.size(); ++i) if (t[i] >= -230 && t[i] <= -220) valid[i] = 0;
        // … and it splits the run into [−250, −235] and [−215, −200], 15 ms each: both too short
        QCOMPARE(int(deliveryHandsSpans(t, c, valid, t, h, {}, -280.0).size()), 0);
        // nothing before P1 (address: the shaft carries it)
        QCOMPARE(int(deliveryHandsSpans(t, c, {}, t, h, {}, -210.0).size()), 0);
    }

    // deliveryInputFromShot through the sampler: a flat curve about each position reads its value; the monitor's and the
    // shoulder plane's phase samples come through; "-" stays NaN.
    void readsAShot()
    {
        ShotSeries s;
        s.ordinal = 1; s.club = QStringLiteral("7 IRON"); s.impactUs = 2'000'000;
        const int ph[7] = { 0, 12, 8, 2, 13, 9, 5 };
        const double ms[7] = { -1200, -1000, -850, -600, -250, -120, 0 };
        const double val[7] = { 58, 54, 62, 65, 64, 61, 59 };
        for (int p = 0; p < 7; ++p) s.phases.push_back({ ph[p], s.impactUs + qint64(ms[p] * 1000), 1.0 });
        MetricSeries c;
        c.key = QStringLiteral("swingPlaneCurve"); c.unit = QStringLiteral("°");
        for (double m = -1200; m <= 40; m += 5) {
            double v = val[6];
            for (int p = 0; p < 7; ++p) if (std::fabs(m - ms[p]) <= 25) { v = val[p]; break; }
            c.tUs.push_back(s.impactUs + qint64(m * 1000));
            c.value.push_back(v);
        }
        s.metrics.insert(c.key, c);
        const auto instant = [&](const QString &key, int phase, double v) {
            MetricSeries m;
            m.key = key; m.unit = QStringLiteral("°");
            PhaseSample ps; ps.phase = phase; ps.value = v;
            for (const PhaseMark &pm : s.phases) if (pm.phase == phase) ps.tUs = pm.tUs;
            m.phaseSamples.push_back(ps);
            s.metrics.insert(key, m);
        };
        instant(QStringLiteral("shoulderPlane"), 0, 68.0);
        instant(QStringLiteral("lm.clubPath"), 5, -2.5);
        instant(QStringLiteral("handPlaneSteepening"), 2, 4.0);
        const DeliveryShotInput in = deliveryInputFromShot(s);
        for (int p = 0; p < 7; ++p) QCOMPARE(in.plane[size_t(p)], val[p]);
        QCOMPARE(in.posMs[3], -600.0);
        QCOMPARE(in.shoulderPlane, 68.0);
        QCOMPARE(in.lmPath, -2.5);
        QCOMPARE(in.steepening, 4.0);
        QVERIFY(!std::isfinite(in.lmAttack));
        QVERIFY(!std::isfinite(in.hands[0]));
        QCOMPARE(in.nominalLoft, 34.0);
        QCOMPARE(in.curveFromMs, -1200.0);
        QCOMPARE(in.curveToMs, 40.0);
        QVERIFY(in.handsSpans.empty());   // no hand plane: nothing to say the hands carry it
        QCOMPARE(deliveryPlaneStory(in).text,
                 QStringLiteral("set on 58° · 4° steeper at P3 · peaked at the top · delivered at 59°, where you set it. "
                                "Hands 4° steeper coming down."));
    }

private:
    static bool fin7(const DeliveryShotInput &s) { return std::isfinite(s.plane[5]); }
};

QTEST_GUILESS_MAIN(SessionDeliveryTest)
#include "session_delivery_test.moc"
