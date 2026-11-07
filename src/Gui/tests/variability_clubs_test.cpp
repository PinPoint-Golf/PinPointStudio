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


// Clubs and the variability grade (src/Gui/charts/variability_clubs.h; Mark's rulings R2a and R2b, 7 Nov):
// the pooled within-club input and the per-club windows, scalar and trace, on synthetic swings.

#include "charts/variability_clubs.h"

#include "../../Metrics/variability/variability_grader.h"

#include <QtTest>

#include <cmath>

using namespace pinpoint::charts;
namespace pv = pinpoint::variability;

namespace {

bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

std::vector<double> bump(double amplitude)
{
    std::vector<double> c(static_cast<size_t>(pv::kTraceGridPoints));
    for (int t = 0; t < pv::kTraceGridPoints; ++t) {
        const double u = (t - 50) / 15.0;
        c[size_t(t)] = amplitude * 100.0 * std::exp(-0.5 * u * u);
    }
    return c;
}

QString s(const char *c) { return QString::fromLatin1(c); }

} // namespace

class VariabilityClubsTest : public QObject {
    Q_OBJECT

private slots:
    void pooledResidualsAboutEachClubsMedian()
    {
        const std::vector<VariabilityClubValue> swings = {
            {s("7i"), 1.0}, {s("DR"), 100.0}, {s("PW"), 50.0}, {s("7i"), 2.0}, {s("9i"), 5.0},
            {s("DR"), 104.0}, {s("7i"), 3.0}, {s("PW"), 51.0}, {s("9i"), kNaN}, {s("DR"), 108.0},
            {s("7i"), 10.0}, {s("9i"), 7.0},
        };
        pv::VariabilityInput base;
        base.corridorHalfWidth = 3.0;
        base.propagatedSigma = 0.5;
        const pv::VariabilityInput in = pooledWithinClubInput(swings, base);
        // 7i median 2.5, DR 104, 9i 6 (its NaN still counts towards the 3, as consistencyResiduals does);
        // PW has 2 swings and is left out. Strike order kept.
        const std::vector<double> expected = {-1.5, -4.0, -0.5, -1.0, 0.0, 0.5, 4.0, 7.5, 1.0};
        QCOMPARE(int(in.values.size()), int(expected.size()));
        for (size_t i = 0; i < expected.size(); ++i) QVERIFY2(near(in.values[i], expected[i]), qPrintable(QString::number(i)));
        QCOMPARE(*in.corridorHalfWidth, 3.0);       // base carried through
        QCOMPARE(*in.propagatedSigma, 0.5);

        const pv::VariabilityResult r = pv::makeVariabilityGrader(pv::Basis::Scalar)->grade(in);
        QVERIFY(r.graded);
        QCOMPARE(r.n, 9);
    }

    void pooledCircularIsCentredBeforeTheMedian()
    {
        const std::vector<VariabilityClubValue> swings = {{s("A"), 179.0}, {s("A"), -179.0}, {s("A"), 178.0}};
        pv::VariabilityInput base;
        base.scaleType = pv::ScaleType::Circular;
        const pv::VariabilityInput in = pooledWithinClubInput(swings, base);
        QCOMPARE(int(in.values.size()), 3);
        // 179 → 181 → 178 about the seam: 2° and 1° apart, never 358°.
        QVERIFY(near(in.values[1] - in.values[0], 2.0, 1e-9));
        QVERIFY(near(in.values[0] - in.values[2], 1.0, 1e-9));
        QVERIFY(near(in.values[0], 0.0, 1e-9));      // 179 is the median
    }

    void pooledTraceResidualsAndOriginalPeaks()
    {
        std::vector<VariabilityClubCurve> swings = {
            {s("7i"), bump(0.9), 240.0}, {s("DR"), bump(3.0), 100.0}, {s("7i"), bump(1.0), 250.0},
            {s("7i"), std::vector<double>(50, 1.0), 300.0},   // off the grid: no reading, still one of the 3
            {s("DR"), bump(3.1), 110.0}, {s("7i"), bump(1.2), 270.0},
        };
        const pv::VariabilityInput in = pooledWithinClubTraceInput(swings);
        // DR has 2 swings: out. The 7i's median curve is bump(1.0) and its median peak 250 (300 ignored).
        QCOMPARE(int(in.curves.size()), 3);
        QCOMPARE(int(in.peakTimesMs.size()), 3);
        QCOMPARE(int(in.peakValues.size()), 3);
        const std::vector<double> amps = {-0.1, 0.0, 0.2};
        const std::vector<double> times = {-10.0, 0.0, 20.0};
        const std::vector<double> peaks = {90.0, 100.0, 120.0};
        for (size_t i = 0; i < 3; ++i) {
            QCOMPARE(int(in.curves[i].size()), pv::kTraceGridPoints);
            QVERIFY(near(in.curves[i][50], amps[i] * 100.0, 1e-9));
            QVERIFY(near(in.peakTimesMs[i], times[i]));
            QVERIFY(near(in.peakValues[i], peaks[i], 1e-9));
        }
    }

    void perClubWindowsOrderedBySwingCount()
    {
        std::vector<VariabilityClubValue> swings;
        for (int i = 0; i < 9; ++i) swings.push_back({s("7i"), i == 4 ? kNaN : double(i)});   // 8 readings
        for (int i = 0; i < 8; ++i) swings.push_back({s("PW"), 20.0 + i});                   // 8, ties 7i
        for (int i = 0; i < 3; ++i) swings.push_back({s("DR"), 200.0 + i});
        swings.push_back({QString(), 1.0});                                                    // blank: its own club
        swings.push_back({s("SW"), kNaN});                                                     // no reading: left out
        swings.push_back({s("SW"), kNaN});

        pv::VariabilityInput base;
        base.scaleType = pv::ScaleType::Ratio;
        base.corridorHalfWidth = 2.0;
        const std::vector<VariabilityClubWindow> w = perClubInputs(swings, base);
        QCOMPARE(int(w.size()), 4);
        QCOMPARE(w[0].club, s("7i"));
        QCOMPARE(w[1].club, s("PW"));
        QCOMPARE(w[2].club, s("DR"));
        QCOMPARE(w[3].club, QString());
        QCOMPARE(w[0].readings, 8);
        QCOMPARE(w[2].readings, 3);
        QCOMPARE(int(w[0].input.values.size()), 9);           // missing one kept, strike order
        QVERIFY(std::isnan(w[0].input.values[4]));
        QCOMPARE(w[0].input.values[8], 8.0);
        QCOMPARE(w[0].input.scaleType, pv::ScaleType::Ratio);
        QCOMPARE(*w[0].input.corridorHalfWidth, 2.0);

        const auto grader = pv::makeVariabilityGrader(pv::Basis::Scalar);
        const pv::VariabilityResult seven = grader->grade(w[0].input);
        QVERIFY(seven.graded);
        QCOMPARE(seven.n, 8);
        QVERIFY(seven.grade >= 1);
        const pv::VariabilityResult driver = grader->grade(w[2].input);   // under N_MIN: still a result
        QVERIFY(driver.graded);
        QCOMPARE(driver.grade, 0);
        QCOMPARE(driver.ungradedReason, pv::UngradedReason::TooFewSwings);
    }

    void perClubTraceWindows()
    {
        std::vector<VariabilityClubCurve> swings;
        for (int i = 0; i < 8; ++i) swings.push_back({s("7i"), bump(1.0 + 0.01 * i), 250.0 + i});
        swings.push_back({s("DR"), bump(3.0), 100.0});
        swings.push_back({s("DR"), std::vector<double>(7, 0.0), 100.0});   // off the grid
        const std::vector<VariabilityClubWindow> w = perClubTraceInputs(swings);
        QCOMPARE(int(w.size()), 2);
        QCOMPARE(w[0].club, s("7i"));
        QCOMPARE(w[0].readings, 8);
        QCOMPARE(w[1].club, s("DR"));
        QCOMPARE(w[1].readings, 1);
        QCOMPARE(int(w[1].input.curves.size()), 2);
        QCOMPARE(int(w[1].input.curves[1].size()), pv::kTraceGridPoints);
        QVERIFY(std::isnan(w[1].input.curves[1][0]));
        QVERIFY(w[1].input.peakValues.empty());

        const auto grader = pv::makeVariabilityGrader(pv::Basis::Trace);
        QVERIFY(grader->grade(w[0].input).graded);
        QCOMPARE(grader->grade(w[0].input).n, 8);
        const pv::VariabilityResult dr = grader->grade(w[1].input);   // not MalformedInput: grade 0
        QVERIFY(dr.graded);
        QCOMPARE(dr.grade, 0);
        QCOMPARE(dr.n, 1);
    }
};

QTEST_GUILESS_MAIN(VariabilityClubsTest)
#include "variability_clubs_test.moc"
