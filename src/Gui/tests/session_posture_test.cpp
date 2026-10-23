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

// The Posture section's core (session_posture.h; posture_chart_design.md stage 2a) against the SHIPPED
// pack and norms (PINPOINT_CORE_PACK / _NORMS / _CONTEXTS).
//
//   · the §2.1 medians of Mark, Nick and Jess, fed as sessions of five shots, give the recomputed
//     worked match of the plan (twinkling-petting-sparrow.md) to 2 dp: distances, nearest, gap, exits;
//   · Nick leaves at P5 and never at the top; D3 is not named;
//   · a ⚠ cell (Nick's hips at the top) is drawn with its number and reason and never matched;
//   · face-on-only and DTL-only captures match on their own rows and say so;
//   · a real swing (a trimmed copy of Nick 2026-10-20_03 swing_0002) reads, cell for cell, what
//     measure_dump read off the same swing;
//   · fewer than five shots: the pattern is the shot's own, said so; the trail-knee threshold; "-";
//   · the 22 Oct review: the trail knee's band follows its style; a ⚠ cell never rings; the ring's word
//     is the side of the band; a departure is named only by its own condition (loss_of_posture names
//     none); the head line follows the work-on order (roots before symptoms);
//   · round 2: the head line prefers a fired fault, exit or not; a session between the two says every
//     shot's word; a row with no value says why, once, in the trust fold; the report's noun.
//   · stage 4c (decision 15): hip depth at P3–P5 reads the butt line with no ⚠ and counts in d; where the
//     butt line is refused it falls back to the hip joints under the ⚠, and the trust fold says which source.

#include "charts/session_posture.h"
#include "charts/session_series_model.h"
#include "charts/session_series_types.h"
#include "charts/session_wrist.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace pinpoint::charts;

namespace {

constexpr double N = std::numeric_limits<double>::quiet_NaN();
constexpr int P2 = 1, P3 = 2, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7;
const QChar NB(0x00A0);
const QChar MINUS(0x2212);

int r(PostureRow row) { return int(row); }
int dp2(double d) { return int(std::lround(d * 100.0)); }
int dp1(double d) { return int(std::lround(d * 10.0)); }

// §2.1 (design table), P1..P8. Head, hips, bend, lead knee, trail knee; pelvis height separately.
PostureGrid golfer(const QString &who, bool withPelvis = false)
{
    PostureGrid g = postureEmptyGrid();
    const auto put = [&](PostureRow row, std::array<double, 8> v) { g[size_t(r(row))] = v; };
    if (who == QLatin1String("Mark")) {
        put(PostureRow::Head,      { 0, 0.1, 0.0, 0.8, -0.2, 0.8, 2.4, 6.6 });
        put(PostureRow::Hips,      { 0, -0.3, 1.0, 3.0, 4.5, 6.5, 7.6, N });
        put(PostureRow::Bend,      { 0, -3.6, -8.3, -7.2, -4.4, -5.0, -10.2, N });
        put(PostureRow::LeadKnee,  { 0, 3.9, 14.2, 14.2, 13.0, 4.4, -3.0, -15.0 });
        put(PostureRow::TrailKnee, { 0, -9.7, -17.9, -18.3, -6.3, 7.5, 14.8, 17.7 });
        if (withPelvis) put(PostureRow::PelvisHeight, { 0, -9.3, -16.9, -20.4, -13.8, -7.7, -3.9, N });
    } else if (who == QLatin1String("Nick")) {
        put(PostureRow::Head,      { 0, 0.2, 0.5, -0.1, -0.4, -0.1, 0.1, 3.8 });
        put(PostureRow::Hips,      { 0, 2.6, 2.9, 4.1, 4.7, 5.6, 6.5, N });
        put(PostureRow::Bend,      { 0, -3.2, -3.9, -8.5, -7.5, -9.4, -13.5, N });
        put(PostureRow::LeadKnee,  { 0, 1.6, 9.3, 16.6, 3.0, -0.8, -4.8, -11.4 });
        put(PostureRow::TrailKnee, { 0, -17.1, -21.7, -21.0, -12.7, -8.2, -7.5, 2.4 });
        if (withPelvis) put(PostureRow::PelvisHeight, { 0, -4.9, -6.0, -5.3, -2.5, -1.6, 5.2, N });
    } else {
        put(PostureRow::Head,      { 0, -2.6, -5.2, -9.5, -8.6, -7.3, -6.0, 2.1 });
        put(PostureRow::Hips,      { 0, -0.3, -0.4, -1.8, 0.9, 3.3, 0.6, N });
        put(PostureRow::Bend,      { 0, 1.8, 3.0, 2.1, 2.9, -0.3, -2.2, N });
        put(PostureRow::LeadKnee,  { 0, 6.1, 19.4, 36.0, 23.4, 2.5, -9.3, -7.9 });
        put(PostureRow::TrailKnee, { 0, -0.9, -1.1, 1.0, 9.8, 11.4, 10.5, 0.8 });
        if (withPelvis) put(PostureRow::PelvisHeight, { 0, -8.8, -14.8, -18.6, -16.6, -9.1, -4.4, N });
    }
    return g;
}

// What the golfers' sessions actually fire of the departures' conditions (the 20 Oct copies' ledgers):
// Mark and Nick early extension and loss of posture; Jess the head dropping and the lead knee folding.
QStringList firedOf(const QString &who)
{
    if (who == QLatin1String("Jess")) return { QStringLiteral("head_drop_backswing"), QStringLiteral("lead_knee_folds_backswing") };
    return { QStringLiteral("early_extension"), QStringLiteral("loss_of_posture") };
}

// A session of n shots whose medians ARE the grid: offsets −k…+k (×d per row), so the middle shot is it.
// Every shot fired `fired`.
std::vector<PostureShotInput> sessionOf(const PostureGrid &g, int n = 5, bool spread = false, const QStringList &fired = {})
{
    static const double d[kPostureRows] = { 0.1, 0.3, 0.2, 0.5, 0.5, 1.0 };
    std::vector<PostureShotInput> out;
    for (int i = 0; i < n; ++i) {
        PostureGrid x = g;
        const int k = i - n / 2;
        if (spread)
            for (int row = 0; row < kPostureRows; ++row)
                for (int p = 1; p < kPosturePositions; ++p)
                    if (std::isfinite(x[size_t(row)][size_t(p)])) x[size_t(row)][size_t(p)] += k * d[row];
        PostureShotInput s = postureInputFromValues(x, i + 1, fired);
        s.contextId = QStringLiteral("iron_7");
        out.push_back(s);
    }
    return out;
}

struct ExitWant { PostureRow row; int pos; double value; double z; };

void checkExits(const std::vector<PostureExit> &got, const std::vector<ExitWant> &want)
{
    QCOMPARE(int(got.size()), int(want.size()));
    for (size_t i = 0; i < want.size(); ++i) {
        QCOMPARE(int(got[i].row), int(want[i].row));
        QCOMPARE(got[i].pos, want[i].pos);
        QCOMPARE(dp1(got[i].value), dp1(want[i].value));
        QCOMPARE(dp1(got[i].z), dp1(want[i].z));
    }
}

bool hasDeparture(const std::vector<PostureDeparture> &d, const char *id)
{
    return std::any_of(d.begin(), d.end(), [&](const PostureDeparture &x) { return x.id == QLatin1String(id); });
}

} // namespace

class SessionPostureTest : public QObject
{
    Q_OBJECT

    const PostureEnv *m_env = nullptr;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        warmWristBands();
        m_env = &postureSharedEnv();
        QVERIFY(m_env->pack);
        QVERIFY(m_env->norms);
        // the bands resolve at the pattern nodes, from the shipped rows
        const PostureBand &h4 = m_env->level[size_t(r(PostureRow::Head))][P4];
        QVERIFY(h4.has);
        QCOMPARE(h4.mu, 2.0);
        QCOMPARE(h4.sigmaLo, 4.0);
        QCOMPARE(h4.contextId, QStringLiteral("posture_level"));
        QCOMPARE(h4.source, QStringLiteral("literature"));
        QCOMPARE(h4.n, 29);
        const PostureBand &h5sp = m_env->squatPush[size_t(r(PostureRow::Head))][P5];
        QCOMPARE(h5sp.mu, -4.0);
        QCOMPARE(m_env->squatPush[size_t(r(PostureRow::Hips))][P2].contextId, QStringLiteral("any"));   // fell back up the tree
        // pelvis height at P3–P5: the authored seed (decision 16), drawn under the ⚠; P4 at the pattern node, never the
        // fault corridor of m_pelvisLiftTop's general row
        const PostureBand &pl4 = m_env->level[size_t(r(PostureRow::PelvisHeight))][P4];
        QVERIFY(pl4.has);
        QCOMPARE(pl4.mu, -5.0);
        QCOMPARE(pl4.contextId, QStringLiteral("posture_level"));
        QCOMPARE(m_env->squatPush[size_t(r(PostureRow::PelvisHeight))][P4].contextId, QStringLiteral("posture_squat_push"));
        QCOMPARE(m_env->level[size_t(r(PostureRow::PelvisHeight))][P3].mu, -3.0);
        QCOMPARE(m_env->level[size_t(r(PostureRow::PelvisHeight))][P5].mu, -8.0);
        QVERIFY(!m_env->level[size_t(r(PostureRow::Hips))][P8].has);
        QVERIFY(!m_env->absolutes.empty());   // the rigSensitive address measures
    }

    // 1. The recomputed worked match (plan table), exactly, to 2 dp.
    void workedMatch_data()
    {
        QTest::addColumn<QString>("who");
        QTest::addColumn<int>("dL");
        QTest::addColumn<int>("dS");
        QTest::addColumn<int>("nearest");
        QTest::addColumn<int>("gap");
        QTest::addColumn<QString>("fit");
        QTest::newRow("Mark") << "Mark" << 123 << 141 << 0 << 18 << "nearest";
        QTest::newRow("Nick") << "Nick" << 146 << 168 << 0 << 22 << "nearest";
        QTest::newRow("Jess") << "Jess" << 161 << 148 << 1 << 13 << "between";
    }
    void workedMatch()
    {
        QFETCH(QString, who); QFETCH(int, dL); QFETCH(int, dS); QFETCH(int, nearest); QFETCH(int, gap); QFETCH(QString, fit);
        const PostureSessionView s = postureSession(sessionOf(golfer(who), 5, false, firedOf(who)), *m_env);
        QVERIFY(s.available);
        QVERIFY(s.fromSession);
        QCOMPARE(s.match.basis, QStringLiteral("both"));
        QCOMPARE(dp2(s.match.dLevel), dL);
        QCOMPARE(dp2(s.match.dSquatPush), dS);
        QCOMPARE(int(s.match.nearest), nearest);
        QCOMPARE(dp2(s.match.gap), gap);
        QCOMPARE(s.match.fit, fit);
        QCOMPARE(int(s.pattern), nearest);
        if (who == QLatin1String("Mark")) {
            checkExits(s.exits, { { PostureRow::Hips, P6, 6.5, 2.6 }, { PostureRow::Hips, P7, 7.6, 2.6 },
                                  { PostureRow::Bend, P7, -10.2, -2.4 } });
            QCOMPARE(s.exits[0].departure, QStringLiteral("D1"));
            QCOMPARE(s.exits[1].departure, QStringLiteral("D1"));
            QCOMPARE(s.exits[2].departure, QStringLiteral("D2"));   // head 2.4 cm up at impact, above the band
            QVERIFY(s.exits[0].conditionIds.contains(QStringLiteral("early_extension")));
            QVERIFY(s.exits[0].named && s.exits[1].named);   // early_extension: 5 of 5, a pattern
            // D2 is named only by coming_out_of_it, which did not fire: loss_of_posture names no departure
            QCOMPARE(s.exits[2].conditionIds, QStringList{ QStringLiteral("coming_out_of_it") });
            QVERIFY(!s.exits[2].named);
            QVERIFY(!hasDeparture(s.departures, "D2"));
            QCOMPARE(s.exits[0].words, QStringLiteral("P6 hips +6.5") + NB + QStringLiteral("cm: Hips in"));
            QCOMPARE(s.trail.word, QStringLiteral("released"));
            QCOMPARE(s.impact.line, QStringLiteral("hips 7.6") + NB + QStringLiteral("cm in, head 2.4") + NB
                                        + QStringLiteral("cm up, bend ") + MINUS + QStringLiteral("10°: hips in, then up"));
            QVERIFY(s.top.line.startsWith(QStringLiteral("head level, trail leg straightened 18°")));
            QVERIFY(s.top.line.endsWith(QStringLiteral("(hips ⚠)")));
            QVERIFY(s.headline.startsWith(QStringLiteral("Level on 5 of 5 shots; you leave it the same way every time: hips in from P6")));
            // decision 14: the report's noun runs through every session sentence
            const PostureSessionView sw = postureSession(sessionOf(golfer(who), 5, false, firedOf(who)), *m_env, -1, nullptr, nullptr,
                                                         QStringLiteral("swings"));
            QVERIFY2(sw.headline.startsWith(QStringLiteral("Level on 5 of 5 swings; you leave it the same way every time")), qPrintable(sw.headline));
            QVERIFY2(!sw.headline.contains(QStringLiteral("shot")), qPrintable(sw.headline));
        } else if (who == QLatin1String("Nick")) {
            checkExits(s.exits, { { PostureRow::LeadKnee, P5, 3.0, -2.4 }, { PostureRow::Hips, P6, 5.6, 2.2 },
                                  { PostureRow::Hips, P7, 6.5, 2.2 }, { PostureRow::Bend, P7, -13.5, -3.5 } });
            QVERIFY(s.exits[0].departure.isEmpty());
            QVERIFY(!s.exits[0].named);
            QVERIFY(s.exits[0].words.endsWith(QStringLiteral("outside the good-player band at P5, not far enough to be a fault")));
            QVERIFY(s.exits[3].named);
            QCOMPARE(s.exits[3].departure, QStringLiteral("D1"));   // a level head: the bend went with the hips
            QVERIFY(!hasDeparture(s.departures, "D2"));
            QCOMPARE(s.trail.word, QStringLiteral("released"));
            QCOMPARE(s.impact.line, QStringLiteral("hips 6.5") + NB + QStringLiteral("cm in under a level head; that is where the 14° of bend went"));
        } else {
            checkExits(s.exits, { { PostureRow::Head, P3, -5.2, -2.1 }, { PostureRow::Head, P4, -9.5, -2.9 },
                                  { PostureRow::LeadKnee, P4, 36.0, 3.3 } });
            QCOMPARE(s.exits[0].departure, QStringLiteral("D4"));
            QCOMPARE(s.exits[2].departure, QStringLiteral("D7"));
            QVERIFY(s.exits[0].named && s.exits[2].named);
            // decision 12: a session between the two says every shot's word, zero terms left out; the tail stays
            QVERIFY2(s.headline.startsWith(QStringLiteral("Between Level and Squat and push: between on 5 of 5 shots; ")), qPrintable(s.headline));
            QVERIFY(hasDeparture(s.departures, "D4") && hasDeparture(s.departures, "D7"));
            QVERIFY(!hasDeparture(s.departures, "D5"));   // still below address at impact
            QCOMPARE(s.match.words, QStringLiteral("between Level and Squat and push (Squat and push nearer)"));
            QCOMPARE(s.trail.word, QStringLiteral("kept"));
            QVERIFY(s.top.line.contains(QStringLiteral("down 9.5") + NB + QStringLiteral("cm, most of it the lead knee folding 36°")));
        }
    }

    // The same with §2.1's pelvis-height row added: the distances move, no nearest pattern does. The plan's
    // table was computed from head, hips, trunk bend and lead knee only (design Appendix B).
    void workedMatchWithPelvisHeight()
    {
        const struct { const char *who; int dL, dS, near; } want[] = { { "Mark", 130, 144, 0 }, { "Nick", 137, 157, 0 }, { "Jess", 163, 152, 1 } };
        for (const auto &w : want) {
            const PostureSessionView s = postureSession(sessionOf(golfer(QString::fromLatin1(w.who), true)), *m_env);
            QCOMPARE(dp2(s.match.dLevel), w.dL);
            QCOMPARE(dp2(s.match.dSquatPush), w.dS);
            QCOMPARE(int(s.match.nearest), w.near);
        }
    }

    // 2. Nick holds his posture going back: no exit before P5, no D3.
    void nickNothingGoingBack()
    {
        const PostureShotInput in = postureInputFromValues(golfer(QStringLiteral("Nick")), 1, firedOf(QStringLiteral("Nick")));
        const PostureShotView v = postureShot(in, *m_env, 0);
        for (const PostureExit &e : v.exits) QVERIFY(e.pos >= P5);
        QVERIFY(!hasDeparture(v.departures, "D3"));
        QVERIFY(hasDeparture(v.departures, "D1"));
    }

    // 3. A ⚠ cell: drawn with its number and its reason, never in d.
    void warnedCellDrawnNotMatched()
    {
        PostureGrid g = golfer(QStringLiteral("Nick"));
        const PostureShotView v = postureShot(postureInputFromValues(g), *m_env, -1);
        const PostureCellView &c = v.rows[size_t(r(PostureRow::Hips))].cells[P4];
        QVERIFY(c.warned);
        QCOMPARE(c.warnReason, QStringLiteral("the hip points turn with the pelvis"));
        QCOMPARE(c.text, QStringLiteral("+4.1"));
        QVERIFY(c.band.has);
        QVERIFY(!c.counted);
        QVERIFY(!c.exit);
        QVERIFY(v.rows[size_t(r(PostureRow::Bend))].cells[P3].warned);
        QCOMPARE(v.rows[size_t(r(PostureRow::Bend))].cells[P3].warnReason,
                 QStringLiteral("the shoulders turning out of the down-the-line picture move the joint line"));
        g[size_t(r(PostureRow::Hips))][P4] = 40.0;   // nothing it says can move the match
        const PostureShotView w = postureShot(postureInputFromValues(g), *m_env, -1);
        QCOMPARE(w.match.dLevel, v.match.dLevel);
        QCOMPARE(w.match.dSquatPush, v.match.dSquatPush);
        QVERIFY(v.trust.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("the hip points turn with the pelvis")));
    }

    // 4. Face-on only and DTL only: each matches on its own rows and says so.
    void singleCamera()
    {
        const PostureGrid mark = golfer(QStringLiteral("Mark"), true);
        PostureGrid face = postureEmptyGrid(), dtl = postureEmptyGrid();
        face[size_t(r(PostureRow::Head))] = mark[size_t(r(PostureRow::Head))];
        face[size_t(r(PostureRow::PelvisHeight))] = mark[size_t(r(PostureRow::PelvisHeight))];
        for (PostureRow row : { PostureRow::Hips, PostureRow::Bend, PostureRow::LeadKnee, PostureRow::TrailKnee })
            dtl[size_t(r(row))] = mark[size_t(r(row))];
        const PostureMatch f = postureShot(postureInputFromValues(face), *m_env).match;
        QVERIFY(f.ok);
        QCOMPARE(f.basis, QStringLiteral("faceOn"));
        QCOMPARE(f.rows, (QStringList{ QStringLiteral("head"), QStringLiteral("pelvisHeight") }));
        QCOMPARE(f.basisWords, QStringLiteral("Matched on head height and pelvis height alone: this capture has no down-the-line camera."));
        QCOMPARE(f.cells, 9);   // head P2–P7, pelvis height P2, P6, P7
        const PostureMatch d = postureShot(postureInputFromValues(dtl), *m_env).match;
        QVERIFY(d.ok);
        QCOMPARE(d.basis, QStringLiteral("dtl"));
        QCOMPARE(d.rows, (QStringList{ QStringLiteral("hips"), QStringLiteral("bend"), QStringLiteral("leadKnee") }));
        QCOMPARE(d.basisWords, QStringLiteral("Matched on hip depth, trunk bend and lead knee alone: this capture has no face-on camera."));
        QCOMPARE(d.cells, 12);  // hips and bend P2, P6, P7; lead knee P2–P7
    }

    // 5. A real swing reads what measure_dump read (Nick 2026-10-20_03 swing_0002, the 22 Oct dump).
    void realShotEqualsMeasureDump()
    {
        QFile f(qEnvironmentVariable("PP_POSTURE_FIXTURE"));
        QVERIFY2(f.open(QIODevice::ReadOnly), "PP_POSTURE_FIXTURE");
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QString why;
        const auto shot = SessionSeriesModel::extractShot(
            root, QStringLiteral("/fixture/2026-10-20_Nick-Henn_Wrist_03/swing_0002"), &why);
        QVERIFY2(shot, qPrintable(why));
        const PostureShotInput in = postureInputFromShot(*shot, *m_env);
        const std::array<std::array<double, 8>, kPostureRows> dump{ {
            { 0, 4.708243665, 0.5042486268, 0.2023360832, 0.3401684205, 0.8621784996, 1.386226854, 4.06683178 },
            // P3–P5: the butt line (hipDepthButt, re-analysed 22 Oct, stage 4c); the joints there read
            // 3.337892827, 3.819543065, 4.532498496 and are the fallback (decision 15)
            { 0, 2.529788519, -1.6300501225309196, -1.0867000816872698, 1.0867000816872845, 5.554071522, 6.506112236, N },
            { 0, -4.615697959, -4.980289605, -8.924045303, -8.293608046, -10.4091602, -14.52898354, N },
            { 0, 1.6384925, 7.64989923, 13.19568302, 6.02569646, -3.700748788, -7.891477723, -8.892344467 },
            { 0, -22.423326, -23.04924119, -23.78430577, -18.08387901, -9.607251556, -8.075800729, 2.368062154 },
            { 0, -4.526813088, -7.483146011, -5.481384069, -1.720850907, -1.026098295, 5.409050565, N } } };
        for (int row = 0; row < kPostureRows; ++row) {
            QVERIFY(in.present[size_t(row)]);
            for (int p = 0; p < kPosturePositions; ++p) {
                const double got = in.value[size_t(row)][size_t(p)], want = dump[size_t(row)][size_t(p)];
                if (std::isnan(want)) {
                    QVERIFY2(std::isnan(got), qPrintable(QStringLiteral("row %1 P%2 should be absent").arg(row).arg(p + 1)));
                } else {
                    QVERIFY2(std::fabs(got - want) < 1e-6,
                             qPrintable(QStringLiteral("row %1 P%2: %3 vs dump %4").arg(row).arg(p + 1).arg(got, 0, 'g', 12).arg(want, 0, 'g', 12)));
                }
            }
        }
        QCOMPARE(in.contextId, QStringLiteral("iron_7"));
        for (int p = P3; p <= P5; ++p) QCOMPARE(int(in.hipSource[size_t(p)]), int(PostureHipSource::ButtLine));
        const PostureShotView v = postureShot(in, *m_env);
        // hip depth at the top: −1.1 cm on the butt line, no ⚠, in the match
        const PostureCellView &h4 = v.rows[size_t(r(PostureRow::Hips))].cells[P4];
        QCOMPARE(h4.text, QString(MINUS) + QStringLiteral("1.1"));
        QVERIFY(!h4.warned && h4.warnReason.isEmpty());
        QVERIFY(h4.counted);
        QCOMPARE(h4.band.measureId, QStringLiteral("m_hipDepthButt_p4"));
        QCOMPARE(v.trust.hipSource, QStringLiteral("Hip depth at P3–P5: the butt line, the back of the hips seen down the line, which the turn does not drag."));
        QVERIFY(v.trust.lines.contains(v.trust.hipSource));
        QVERIFY(!v.trust.warnings.join(QLatin1Char('\n')).contains(postureHipJointsReason()));
        // "-" on the DTL rows masked after impact (and pelvis height, out of its domain)
        QCOMPARE(v.rows[size_t(r(PostureRow::Hips))].cells[P8].text, QStringLiteral("-"));
        QCOMPARE(v.rows[size_t(r(PostureRow::Bend))].cells[P8].text, QStringLiteral("-"));
        QCOMPARE(v.rows[size_t(r(PostureRow::PelvisHeight))].cells[P8].text, QStringLiteral("-"));
        QCOMPARE(v.rows[size_t(r(PostureRow::LeadKnee))].cells[P8].text, QStringLiteral("−9"));
        // the fault lines are the grading conditions' corridors at the shot's context
        const PostureFaultCell &h7 = v.rows[size_t(r(PostureRow::Hips))].cells[P7].fault;
        QVERIFY(h7.hi.has);
        QCOMPARE(h7.hi.conditionId, QStringLiteral("early_extension"));
        QCOMPARE(h7.hi.edge, 5.0);
        const PostureFaultCell &l4 = v.rows[size_t(r(PostureRow::LeadKnee))].cells[P4].fault;
        QCOMPARE(l4.hi.conditionId, QStringLiteral("lead_knee_folds_backswing"));
        QCOMPARE(l4.hi.edge, 27.0);
        QCOMPARE(v.rows[size_t(r(PostureRow::TrailKnee))].cells[P4].fault.lo.conditionId, QStringLiteral("trail_knee_straighten"));
        // the rig-sensitive address readings (§7.4.2, 22 Oct): every one shown — its number, its good-player band at the
        // shot's grading context, the ⚠ and its few words — and the rule said once in the trust fold
        QCOMPARE(v.absolutes.size(), m_env->absolutes.size());
        for (const PostureAbsoluteView &a : v.absolutes) {
            QCOMPARE(a.trust, postureRigRule());
            QVERIFY2(std::isfinite(a.value) && a.text != QLatin1String("-"), qPrintable(a.measureId));
            QVERIFY2(a.band.has, qPrintable(a.measureId));
            QVERIFY(a.warned);
            QCOMPARE(a.warnShort, postureRigWarnShort());
            QCOMPARE(a.history.n, 0);                 // no history handed in: no box, and the words why
            QVERIFY(!a.historyWhy.isEmpty());
        }
        QCOMPARE(v.trust.lines.count(postureRigRule()), 1);
        for (const QString &l : v.addressLines) QVERIFY(v.trust.lines.contains(l));
        QVERIFY(v.trust.bandSources.startsWith(QStringLiteral("Bands from 20 tour professionals (Kim 2021), 29 skilled men (Evans 2008), ")));
        QVERIFY(v.trust.bandSources.contains(QStringLiteral("a vendor's published figures")));
        QVERIFY(!v.trust.trunkDatum.isEmpty());
    }

    // 5b. Decision 15 on the real swing: the butt line at the top is read without a ⚠ and counted in d; refused
    // there, the cell falls back to the hip joints WITH the ⚠; the trust fold says which source, per session.
    void buttLineReadOrFallback()
    {
        QFile f(qEnvironmentVariable("PP_POSTURE_FIXTURE"));
        QVERIFY2(f.open(QIODevice::ReadOnly), "PP_POSTURE_FIXTURE");
        QString why;
        const auto shot = SessionSeriesModel::extractShot(
            QJsonDocument::fromJson(f.readAll()).object(), QStringLiteral("/fixture/2026-10-20_Nick-Henn_Wrist_03/swing_0002"), &why);
        QVERIFY2(shot, qPrintable(why));
        QVERIFY(shot->metric(QStringLiteral("hipDepthButt")));
        const size_t H = size_t(r(PostureRow::Hips));

        // (1) the butt line at P4: no ⚠, counted
        const PostureShotInput in = postureInputFromShot(*shot, *m_env);
        QCOMPARE(int(in.hipSource[P4]), int(PostureHipSource::ButtLine));
        const PostureShotView v = postureShot(in, *m_env, 0);
        QVERIFY(!v.rows[H].cells[P4].warned);
        QVERIFY(v.rows[H].cells[P4].counted);
        QVERIFY(v.match.ok);

        // (2) the mask refused the top: the P4 sample gone, the joints' +3.8 cm under the ⚠, out of d
        ShotSeries refused = *shot;
        auto &ps = refused.metrics[QStringLiteral("hipDepthButt")].phaseSamples;
        ps.erase(std::remove_if(ps.begin(), ps.end(), [](const PhaseSample &x) { return x.phase == posturePhaseIds()[P4]; }), ps.end());
        const PostureShotInput fb = postureInputFromShot(refused, *m_env);
        QCOMPARE(int(fb.hipSource[P3]), int(PostureHipSource::ButtLine));
        QCOMPARE(int(fb.hipSource[P4]), int(PostureHipSource::Joints));
        QCOMPARE(int(fb.hipSource[P5]), int(PostureHipSource::ButtLine));
        QVERIFY(std::fabs(fb.value[H][P4] - 3.819543065) < 1e-6);
        const PostureShotView w = postureShot(fb, *m_env, 0);
        const PostureCellView &c = w.rows[H].cells[P4];
        QVERIFY(c.warned);
        QCOMPARE(c.warnReason, postureHipJointsReason());
        QVERIFY(!c.counted && !c.outside && !c.exit);
        QCOMPARE(w.match.cells, v.match.cells - 1);
        QVERIFY(!w.top.hipsIn);
        QVERIFY(w.top.hipsWarned);
        const QString mixed = QStringLiteral("Hip depth at P3–P5: the butt line at P3 and P5; the hip joints (⚠) at P4 — the hip points turn with the pelvis.");
        QCOMPARE(w.trust.hipSource, mixed);
        QVERIFY(w.trust.warnings.contains(mixed));

        // (3) the session: four shots on the butt line, one with no butt line at all (no ruler)
        ShotSeries none = *shot;
        none.metrics.remove(QStringLiteral("hipDepthButt"));
        const PostureShotInput nb = postureInputFromShot(none, *m_env);
        for (int p = P3; p <= P5; ++p) QCOMPARE(int(nb.hipSource[size_t(p)]), int(PostureHipSource::Joints));
        QVERIFY(nb.present[H]);
        std::vector<PostureShotInput> five;
        for (int i = 1; i <= 4; ++i) { PostureShotInput x = in; x.ordinal = i; five.push_back(x); }
        PostureShotInput fifth = nb;
        fifth.ordinal = 5;
        five.push_back(fifth);
        const PostureSessionView s = postureSession(five, *m_env, 0);
        const QString line = QStringLiteral("Hip depth at P3–P5: the butt line on 4 of 5 shots; the hip joints (⚠) on 1 — the hip points turn with the pelvis.");
        QCOMPARE(s.trust.hipSource, line);
        QVERIFY(s.trust.warnings.contains(line));
        const PostureCellView &sc = s.rows[H].view.cells[P4];
        QVERIFY(!sc.warned && sc.counted);
        QCOMPARE(s.rows[H].boxes[P4].n, 5);   // the box: each shot's own source
        QCOMPARE(dp1(s.rows[H].boxes[P4].med), -11);
        QVERIFY(s.match.rows.contains(QStringLiteral("hips")));
        // every shot on the joints: the session's cell under the ⚠, said so
        const PostureSessionView j = postureSession(std::vector<PostureShotInput>(5, nb), *m_env, 0);
        QVERIFY(j.rows[H].view.cells[P4].warned);
        QCOMPARE(j.rows[H].view.cells[P4].warnReason, postureHipJointsReason());
        QCOMPARE(j.trust.hipSource, QStringLiteral("Hip depth at P3–P5: the hip joints (⚠) on every shot, no butt line — the hip points turn with the pelvis."));
        QVERIFY(j.top.hipsWarned && !j.top.hipsIn);
    }

    // 6. Fewer than five shots; the trail-knee threshold; W-D6.
    void fewShotsTrailAndWd6()
    {
        const PostureGrid mark = golfer(QStringLiteral("Mark"));
        const PostureSessionView few = postureSession(sessionOf(mark, 3), *m_env);
        QVERIFY(few.available);
        QVERIFY(!few.fromSession);
        QCOMPARE(few.patternSource, QStringLiteral("shot"));
        QVERIFY(few.headline.contains(QStringLiteral("each shot is matched on its own")));
        const PostureShotView v = postureShot(postureInputFromValues(mark), *m_env, -1, &few);
        QCOMPARE(v.patternSource, QStringLiteral("shot"));
        QVERIFY(v.patternSourceWords.contains(QStringLiteral("fewer than five shots")));
        QCOMPARE(v.patternLine, QStringLiteral("Level (nearest, from this shot: fewer than five shots in the session) · trail knee released"));
        const PostureSessionView five = postureSession(sessionOf(mark, 5, true, firedOf(QStringLiteral("Mark"))), *m_env);
        QVERIFY(five.fromSession);
        const PostureShotView w = postureShot(postureInputFromValues(mark), *m_env, -1, &five);
        QCOMPARE(w.patternSource, QStringLiteral("session"));
        QCOMPARE(w.patternLine, QStringLiteral("Level (nearest, of your session) · trail knee released"));
        const PostureShotView chosen = postureShot(postureInputFromValues(mark), *m_env, 1, &five);
        QCOMPARE(int(chosen.pattern), 1);
        QCOMPARE(chosen.patternSource, QStringLiteral("chosen"));
        // the session's boxes, departures and repeatability
        QCOMPARE(five.rows[size_t(r(PostureRow::Hips))].boxes[P7].n, 5);
        QCOMPARE(dp1(five.rows[size_t(r(PostureRow::Hips))].boxes[P7].med), 76);
        QVERIFY(!five.departureCounts.empty());
        QCOMPARE(five.departureCounts.front().id, QStringLiteral("D1"));
        QCOMPARE(five.departureCounts.front().shots, 5);
        QVERIFY(five.tightest.has && five.loosest.has);
        QCOMPARE(int(five.tightest.row), r(PostureRow::Head));
        QCOMPARE(five.tightest.pos, P4);
        QCOMPARE(int(five.loosest.row), r(PostureRow::Hips));
        QCOMPARE(five.loosest.pos, P2);

        // the trail knee: kept while it loses less than 8° by the top
        PostureGrid g = mark;
        g[size_t(r(PostureRow::TrailKnee))][P4] = -7.9;
        QCOMPARE(postureTrailStyle(g).word, QStringLiteral("kept"));
        g[size_t(r(PostureRow::TrailKnee))][P4] = -8.1;
        QCOMPARE(postureTrailStyle(g).word, QStringLiteral("released"));
        QCOMPARE(postureTrailStyle(mark).words, QStringLiteral("trail leg straightened 18°"));
        QCOMPARE(postureTrailStyle(golfer(QStringLiteral("Jess"))).words, QStringLiteral("trail knee kept its flex"));

        // W-D6: outside the band on a side a condition grades, and the condition did not fire
        PostureGrid k = golfer(QStringLiteral("Nick"));
        k[size_t(r(PostureRow::LeadKnee))][P4] = 25.0;   // band 13 ± 7; lead_knee_folds_backswing fires past 27
        PostureShotInput in = postureInputFromValues(k);
        in.contextId = QStringLiteral("iron_7");
        in.graded = true;
        in.clean << QStringLiteral("lead_knee_folds_backswing");
        const PostureShotView x = postureShot(in, *m_env, 0);
        const PostureCellView &c = x.rows[size_t(r(PostureRow::LeadKnee))].cells[P4];
        QVERIFY(c.outside);
        QCOMPARE(c.ring, QStringLiteral("more bend"));   // the side of the band, no number
        QVERIFY(!c.beyondFault);
        QCOMPARE(c.note, QStringLiteral("outside the good-player band at P4, not far enough to be a fault"));
        // "-" for P8 on the DTL rows masked after impact, in a fixture too
        QCOMPARE(x.rows[size_t(r(PostureRow::Hips))].cells[P8].text, QStringLiteral("-"));
        // trail_knee_straighten is a finding when it fires (Q3), though the trail knee never matches
        in.fired << QStringLiteral("trail_knee_straighten");
        QVERIFY(postureShot(in, *m_env, 0).firedConditions.contains(QStringLiteral("trail_knee_straighten")));
    }

    // 7. The chart names nothing the conditions did not find (§8): the same numbers with NO condition
    // fired leave every exit in the W-D6 words, no departure, no count, and a W-D6 head.
    void namesOnlyWhatFired()
    {
        for (const QString who : { QStringLiteral("Mark"), QStringLiteral("Nick"), QStringLiteral("Jess") }) {
            const PostureShotView v = postureShot(postureInputFromValues(golfer(who)), *m_env, -1);
            QVERIFY(!v.exits.empty());
            for (const PostureExit &e : v.exits) {
                QVERIFY(!e.named);
                QVERIFY2(e.words.endsWith(postureWd6Words(e.pos)), qPrintable(e.words));
                if (!e.name.isEmpty()) QVERIFY2(!e.words.contains(e.name), qPrintable(e.words));
            }
            QVERIFY(v.departures.empty());
            QVERIFY2(v.headline.contains(QStringLiteral("not far enough to be a fault")), qPrintable(v.headline));

            const PostureSessionView s = postureSession(sessionOf(golfer(who)), *m_env);
            for (const PostureExit &e : s.exits) QVERIFY(!e.named);
            QVERIFY(s.departures.empty());
            QVERIFY(s.departureCounts.empty());
            QVERIFY(s.fromSession && !s.exits.empty());
            QVERIFY2(s.headline.endsWith(QStringLiteral(", not far enough to be a fault.")), qPrintable(s.headline));
        }
        // the exact words, once
        QCOMPARE(postureWd6Words(P4), QStringLiteral("outside the good-player band at P4, not far enough to be a fault"));
        // Mark's numbers, early extension only: D1 named, D2 (coming_out_of_it / loss_of_posture) not
        const PostureShotView m = postureShot(postureInputFromValues(golfer(QStringLiteral("Mark")), 1, { QStringLiteral("early_extension") }),
                                              *m_env, 0);
        QVERIFY(hasDeparture(m.departures, "D1"));
        QVERIFY(!hasDeparture(m.departures, "D2"));
        QVERIFY(m.headline.startsWith(QStringLiteral("Your hips moved")));
        // the session's tiers decide the medians' names: watching is not a pattern
        const QHash<QString, QString> watching{ { QStringLiteral("early_extension"), QStringLiteral("watching") },
                                                { QStringLiteral("loss_of_posture"), QStringLiteral("watching") } };
        const PostureSessionView t = postureSession(sessionOf(golfer(QStringLiteral("Mark")), 5, false, firedOf(QStringLiteral("Mark"))),
                                                    *m_env, -1, &watching);
        for (const PostureExit &e : t.exits) QVERIFY(!e.named);
        QVERIFY(!t.departureCounts.empty());   // fired on each shot: the shots still count
    }

    // 8. The 22 Oct review, one rule each.
    void reviewRules()
    {
        // (1) the trail knee's band follows its style: released (Nick, −21° by the top) reads the
        // posture_trail_released node at P2–P4; kept (Jess) the pattern's own; P5 is the pattern's either way
        const size_t T = size_t(r(PostureRow::TrailKnee));
        for (int p = P2; p <= P4; ++p) {
            const PostureBand &rb = m_env->trailReleased[T][size_t(p)];
            QVERIFY(rb.has);
            QCOMPARE(rb.contextId, postureTrailReleasedContext());
            QCOMPARE(rb.mu, -15.0);
            QCOMPARE(rb.sigmaLo, 8.0);
        }
        const PostureShotView nick = postureShot(postureInputFromValues(golfer(QStringLiteral("Nick")), 1, firedOf(QStringLiteral("Nick"))),
                                                 *m_env, 0);
        QCOMPARE(nick.trail.word, QStringLiteral("released"));
        for (int p = P2; p <= P4; ++p) {
            const PostureCellView &c = nick.rows[T].cells[size_t(p)];
            QCOMPARE(c.band.contextId, postureTrailReleasedContext());
            QVERIFY2(!c.outside, qPrintable(c.text));   // −17…−22 sit in −15 ± 8
        }
        QCOMPARE(nick.rows[T].cells[P5].band.contextId, m_env->level[T][P5].contextId);
        const PostureShotView jess = postureShot(postureInputFromValues(golfer(QStringLiteral("Jess"))), *m_env, 0);
        QCOMPARE(jess.trail.word, QStringLiteral("kept"));
        QCOMPARE(jess.rows[T].cells[P4].band.contextId, m_env->level[T][P4].contextId);
        QVERIFY(jess.rows[T].cells[P4].band.contextId != postureTrailReleasedContext());
        const PostureSessionView ns = postureSession(sessionOf(golfer(QStringLiteral("Nick")), 5, false, firedOf(QStringLiteral("Nick"))), *m_env);
        QCOMPARE(ns.rows[T].view.cells[P3].band.contextId, postureTrailReleasedContext());   // the session's style

        // (2) a ⚠ cell never rings: Nick's hips at the top, +4.1 cm against the Level band
        PostureGrid big = golfer(QStringLiteral("Nick"));
        big[size_t(r(PostureRow::Hips))][P4] = 12.0;   // far outside — still no ring under the ⚠
        const PostureShotView w = postureShot(postureInputFromValues(big, 1, firedOf(QStringLiteral("Nick"))), *m_env, 0);
        const PostureCellView &hw = w.rows[size_t(r(PostureRow::Hips))].cells[P4];
        QVERIFY(hw.warned && std::fabs(hw.z) > 1.0);
        QVERIFY(!hw.outside && hw.ring.isEmpty() && hw.note.isEmpty() && !hw.exit);
        QCOMPARE(hw.text, QStringLiteral("+12.0"));
        for (const PostureExit &e : w.exits) QVERIFY(!(e.row == PostureRow::Hips && e.pos == P4));

        // (3) the ring's word is the side of the band: Nick's lead knee at P5, +3° against +20 ± 7° → straighter;
        // his hips at impact, above the band → in
        const PostureCellView &lk = nick.rows[size_t(r(PostureRow::LeadKnee))].cells[P5];
        QVERIFY(lk.outside && lk.value > 0);
        QCOMPARE(lk.ring, QStringLiteral("straighter"));
        QCOMPARE(nick.rows[size_t(r(PostureRow::Hips))].cells[P7].ring, QStringLiteral("in"));

        // (5) loss_of_posture alone names nothing; coming_out_of_it names D2
        const PostureShotView lop = postureShot(postureInputFromValues(golfer(QStringLiteral("Mark")), 1, { QStringLiteral("loss_of_posture") }),
                                                *m_env, 0);
        QVERIFY(lop.departures.empty());
        QVERIFY(lop.firedConditions.contains(QStringLiteral("loss_of_posture")));   // still a finding
        const QStringList both{ QStringLiteral("early_extension"), QStringLiteral("coming_out_of_it") };
        const PostureShotView m2 = postureShot(postureInputFromValues(golfer(QStringLiteral("Mark")), 1, both), *m_env, 0);
        QVERIFY(hasDeparture(m2.departures, "D1") && hasDeparture(m2.departures, "D2"));

        // (4) the head line follows the work-on order: roots before symptoms, then |z|
        QVERIFY(m2.headline.startsWith(QStringLiteral("Your hips moved")));   // no order: the largest named exit
        const QStringList d2First{ QStringLiteral("coming_out_of_it"), QStringLiteral("early_extension") };
        const PostureShotView m3 = postureShot(postureInputFromValues(golfer(QStringLiteral("Mark")), 1, both), *m_env, 0, nullptr, &d2First);
        // D2 first: read on its own row, the head (decision 11), not the trunk bend that went with it
        QVERIFY2(m3.headline.startsWith(QStringLiteral("Your head rose 2.4") + NB + QStringLiteral("cm by impact; good players stay between")),
                 qPrintable(m3.headline));
        const QStringList d1First{ QStringLiteral("early_extension"), QStringLiteral("coming_out_of_it") };
        const PostureShotView m4 = postureShot(postureInputFromValues(golfer(QStringLiteral("Mark")), 1, both), *m_env, 0, nullptr, &d1First);
        QVERIFY2(m4.headline.startsWith(QStringLiteral("Your hips moved")), qPrintable(m4.headline));
        QCOMPARE(postureWorkOnRank(&d1First, QStringLiteral("coming_out_of_it")), 1);
        QCOMPARE(postureWorkOnRank(&d1First, QStringLiteral("loss_of_posture")), std::numeric_limits<int>::max());
        QCOMPARE(postureWorkOnRank(nullptr, QStringLiteral("early_extension")), std::numeric_limits<int>::max());
    }

    // 9. The 22 Oct review, round 2.
    void reviewRoundTwo()
    {
        // (1) the head line prefers a FIRED fault, read on its row with the band, exit or not: Mark's 7 Oct shot 2,
        // hips +5.3 cm at impact — outside the band but not |z| > 2 — while early extension fired
        PostureGrid g = golfer(QStringLiteral("Mark"));
        g[size_t(r(PostureRow::Hips))][P5] = 2.5;
        g[size_t(r(PostureRow::Hips))][P6] = 4.7;
        g[size_t(r(PostureRow::Hips))][P7] = 5.3;
        const QStringList fired{ QStringLiteral("loss_of_posture"), QStringLiteral("early_extension") };
        const PostureShotView m = postureShot(postureInputFromValues(g, 1, fired), *m_env, 0, nullptr, &fired);
        const PostureCellView &h7 = m.rows[size_t(r(PostureRow::Hips))].cells[P7];
        QVERIFY2(h7.outside && !h7.exit, qPrintable(QString::number(h7.z)));
        QVERIFY(std::none_of(m.exits.begin(), m.exits.end(), [](const PostureExit &e) { return e.row == PostureRow::Hips && e.pos == P7; }));
        const QString want = QStringLiteral("Your hips moved 5.3") + NB + QStringLiteral("cm toward the ball by impact; good players stay between %1 and %2.")
                                 .arg(postureFigure(PostureRow::Hips, h7.band.lo), postureFigure(PostureRow::Hips, h7.band.hi) + NB + QStringLiteral("cm"));
        QCOMPARE(m.headline, want);
        // the condition's own position, where it lies in the departure's window
        PostureShotInput at6 = postureInputFromValues(g, 1, fired);
        at6.conditionPos.insert(QStringLiteral("early_extension"), P6);
        QVERIFY2(postureShot(at6, *m_env, 0, nullptr, &fired).headline.startsWith(QStringLiteral("Your hips moved 4.7") + NB + QStringLiteral("cm toward the ball by P6")),
                 qPrintable(postureShot(at6, *m_env, 0, nullptr, &fired).headline));
        // no departure condition fired: the W-D6 words, as before
        QVERIFY(postureShot(postureInputFromValues(g, 1, { QStringLiteral("loss_of_posture") }), *m_env, 0).headline.contains(QStringLiteral("not far enough to be a fault")));

        // (3) a row with no value anywhere says why, once, in the trust fold (Jess, 20 Oct shot 2: no ruler)
        PostureGrid j = golfer(QStringLiteral("Jess"), true);
        j[size_t(r(PostureRow::Hips))].fill(N);
        const auto noRuler = [&](int ordinal) {
            PostureShotInput in = postureInputFromValues(j, ordinal, firedOf(QStringLiteral("Jess")));
            in.gateWords[size_t(r(PostureRow::Hips))].fill(QStringLiteral("no ruler"));
            in.gateWhy[size_t(r(PostureRow::Hips))] = QStringLiteral("No ball to measure with in either view.");
            in.contextId = QStringLiteral("iron_8");
            return in;
        };
        const PostureShotView js = postureShot(noRuler(2), *m_env, 1);
        QCOMPARE(js.trust.absent, QStringList{ QStringLiteral("Hip depth: not measured on this shot — no ruler: no ball to measure with in either view.") });
        QCOMPARE(js.trust.lines.count(js.trust.absent.front()), 1);
        for (const QString &l : js.trust.lines) QVERIFY2(!l.startsWith(QStringLiteral("Hip depth: no ruler")), qPrintable(l));
        QVERIFY(postureShot(postureInputFromValues(golfer(QStringLiteral("Jess"), true)), *m_env, 1).trust.absent.isEmpty());
        // the session: on any swing, and on k of M
        std::vector<PostureShotInput> five;
        for (int i = 1; i <= 5; ++i) five.push_back(noRuler(i));
        const PostureSessionView all = postureSession(five, *m_env, -1, nullptr, nullptr, QStringLiteral("swings"));
        QCOMPARE(all.trust.absent, QStringList{ QStringLiteral("Hip depth: not measured on any swing — no ruler: no ball to measure with in either view.") });
        five[0] = postureInputFromValues(golfer(QStringLiteral("Jess"), true), 1, firedOf(QStringLiteral("Jess")));
        const PostureSessionView some = postureSession(five, *m_env);
        QCOMPARE(some.trust.absent, QStringList{ QStringLiteral("Hip depth: not measured on 4 of 5 shots — no ruler: no ball to measure with in either view.") });
    }
};

QTEST_GUILESS_MAIN(SessionPostureTest)
#include "session_posture_test.moc"
