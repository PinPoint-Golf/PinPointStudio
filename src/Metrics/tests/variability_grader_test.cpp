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

// THE VARIABILITY GRADER (src/Metrics/variability/, docs/design/variability-mark/spec.md §8). Every
// case is a synthetic window whose spread is set exactly, so each assertion is a number the spec
// fixes: the band edges on both sides, bias invariance, the circular seam, the n and scale gates,
// the two halves of a trace grade, the chance-corrected ordinal share, noise subtraction, the trend
// and its run rule, live hysteresis, and the §7.5 basis strings.

#include "variability_constants.h"
#include "variability_grader.h"
#include "variability_outcome.h"
#include "variability_result.h"
#include "variability_trend.h"
#include "variability_typicality.h"

#include <QtTest>

#include <cmath>
#include <limits>

using namespace pinpoint::variability;

namespace {

bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

// n values with mean `mean` and sample SD exactly `sd` (to rounding): an evenly spaced ladder,
// standardised.
std::vector<double> withSd(double sd, int n = 24, double mean = 0.0)
{
    std::vector<double> z(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) z[size_t(i)] = i - (n - 1) / 2.0;
    const double base = sampleSd(z);
    for (double &x : z) x = mean + x * sd / base;
    return z;
}

VariabilityResult gradeScalar(const std::vector<double> &values, std::optional<double> corridor,
                              ScaleType type = ScaleType::Interval)
{
    VariabilityInput in;
    in.values = values;
    in.corridorHalfWidth = corridor;
    in.scaleType = type;
    return makeVariabilityGrader(Basis::Scalar)->grade(in);
}

// Evenly spaced values whose type-7 median is `median` and whose robust spread IQR/1.349 is `spread`
// exactly: typicality's scale (R22). Spacing k puts the quartiles (n − 1)/4 steps either side of the median.
std::vector<double> withSpread(double spread, int n = 24, double median = 0.0)
{
    const double k = 2.0 * 1.349 * spread / double(n - 1);
    std::vector<double> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[size_t(i)] = median + (i - (n - 1) / 2.0) * k;
    return v;
}

// A smooth bump on the trace grid, peak 100 at the middle.
std::vector<double> bump(double amplitude = 1.0)
{
    std::vector<double> c(static_cast<size_t>(kTraceGridPoints));
    for (int t = 0; t < kTraceGridPoints; ++t) {
        const double u = (t - 50) / 15.0;
        c[size_t(t)] = amplitude * 100.0 * std::exp(-0.5 * u * u);
    }
    return c;
}

double rms(const std::vector<double> &c)
{
    double s = 0.0;
    for (double x : c) s += x * x;
    return std::sqrt(s / double(c.size()));
}

std::vector<TrendPoint> trendOf(const std::vector<double> &rhos, int n = 24, const QString &context = "7i")
{
    std::vector<TrendPoint> pts;
    for (double r : rhos) pts.push_back({r, n, context, Basis::Scalar, QStringLiteral("corridor")});
    return pts;
}

} // namespace

class VariabilityGraderTest : public QObject {
    Q_OBJECT

private slots:
    // ---- Bands ------------------------------------------------------------------------------
    void bandEdgesBelongToTheTighterGrade()
    {
        const GradeBands &r = kBands.ratio;
        QCOMPARE(bandGrade(0.0, r), 5);
        QCOMPARE(bandGrade(0.25, r), 5);   QCOMPARE(bandGrade(0.2501, r), 4);
        QCOMPARE(bandGrade(0.50, r), 4);   QCOMPARE(bandGrade(0.5001, r), 3);
        QCOMPARE(bandGrade(0.75, r), 3);   QCOMPARE(bandGrade(0.7501, r), 2);
        QCOMPARE(bandGrade(1.00, r), 2);   QCOMPARE(bandGrade(1.0001, r), 1);
        QCOMPARE(bandGrade(std::numeric_limits<double>::infinity(), r), 1);
        QCOMPARE(bandGrade(std::nan(""), r), 0);

        const GradeBands &o = kBands.ordinal;
        QCOMPARE(bandGrade(1.0, o), 5);
        QCOMPARE(bandGrade(0.85, o), 5);   QCOMPARE(bandGrade(0.8499, o), 4);
        QCOMPARE(bandGrade(0.65, o), 4);   QCOMPARE(bandGrade(0.6499, o), 3);
        QCOMPARE(bandGrade(0.45, o), 3);   QCOMPARE(bandGrade(0.4499, o), 2);
        QCOMPARE(bandGrade(0.25, o), 2);   QCOMPARE(bandGrade(0.2499, o), 1);
        QCOMPARE(bandGrade(-0.3, o), 1);
    }

    void knownSdGivesExpectedRhoAndGrade_data()
    {
        QTest::addColumn<double>("rho");
        QTest::addColumn<int>("grade");
        for (int i = 0; i < 4; ++i) {
            const double e = kBands.ratio.edges[size_t(i)];
            QTest::addRow("below %.2f", e) << e - 0.005 << 5 - i;
            QTest::addRow("above %.2f", e) << e + 0.005 << 4 - i;
        }
    }
    void knownSdGivesExpectedRhoAndGrade()
    {
        QFETCH(double, rho);
        QFETCH(int, grade);
        const double corridor = 4.0;
        const VariabilityResult r = gradeScalar(withSd(rho * corridor), corridor);
        QVERIFY(r.graded);
        QCOMPARE(r.basis, Basis::Scalar);
        QCOMPARE(r.scaleSource, ScaleSource::Corridor);
        QCOMPARE(r.n, 24);
        QVERIFY(near(r.sd, rho * corridor, 1e-12));
        QVERIFY(near(r.rho, rho, 1e-12));
        QCOMPARE(r.grade, grade);
        QCOMPARE(r.ungradedReason, UngradedReason::None);
    }

    // ---- Bias -------------------------------------------------------------------------------
    void consistencyIsBiasInvariant()
    {
        const std::vector<double> v = withSd(1.3, 24, 10.0);
        std::vector<double> shifted = v;
        for (double &x : shifted) x += 250.0;
        const VariabilityResult a = gradeScalar(v, 2.0);
        const VariabilityResult b = gradeScalar(shifted, 2.0);
        QVERIFY(near(a.rho, b.rho, 1e-9));
        QCOMPARE(a.grade, b.grade);
        QCOMPARE(a.grade, 3);   // 0.65
    }

    void measurementBiasWorsensRhoM()
    {
        // One set of readings, two questions. Readings = truth + bias + noise: the golfer's own spread
        // (truth), a sensor offset, and ±0.2 sensor noise (population SD exactly 0.2, mean 0). As the
        // bias grows the consistency grade must not move, and the RMSE against truth — bias included,
        // RMSE² = bias² + 0.2² — must worsen ρm.
        const std::vector<double> truth = withSd(1.0, 24, 40.0);
        const auto measurement = makeVariabilityGrader(Basis::Measurement);
        int consistencyGrade = -1;
        double consistencyRho = 0.0, lastRhoM = -1.0;
        const std::vector<std::pair<double, int>> cases{{0.0, 5}, {0.3, 4}, {0.6, 3}, {1.0, 1}};
        for (const auto &[bias, expectGrade] : cases) {
            std::vector<double> readings;
            double sumSq = 0.0;
            for (size_t i = 0; i < truth.size(); ++i) {
                const double reading = truth[i] + bias + (i % 2 ? -0.2 : 0.2);
                readings.push_back(reading);
                sumSq += (reading - truth[i]) * (reading - truth[i]);
            }
            const double rmse = std::sqrt(sumSq / double(truth.size()));
            QVERIFY(near(rmse, std::sqrt(bias * bias + 0.04), 1e-12));

            const VariabilityResult c = gradeScalar(readings, 3.0);
            if (consistencyGrade < 0) { consistencyGrade = c.grade; consistencyRho = c.rho; }
            QCOMPARE(c.grade, consistencyGrade);
            QVERIFY(near(c.rho, consistencyRho, 1e-9));

            VariabilityInput in;
            in.measurementTolerance = 1.0;
            in.validationRmse = rmse;
            const VariabilityResult m = measurement->grade(in);
            QVERIFY(m.graded);
            QCOMPARE(m.uncertaintyKind, UncertaintyKind::ValidationRmse);
            QCOMPARE(m.scaleSource, ScaleSource::MeasurementTolerance);
            QVERIFY(m.rhoM > lastRhoM);
            QCOMPARE(m.grade, expectGrade);
            lastRhoM = m.rhoM;
        }
    }

    void measurementLadders()
    {
        const auto grader = makeVariabilityGrader(Basis::Measurement);
        VariabilityInput in;
        in.stationarySd = 0.1;
        in.liveResidualRmse = 0.3;
        in.corridorHalfWidth = 4.0;   // tolerance 0.25 × 4 = 1.0
        VariabilityResult r = grader->grade(in);
        QCOMPARE(r.uncertaintyKind, UncertaintyKind::LiveResidualRmse);   // live before stationary
        QCOMPARE(r.scaleSource, ScaleSource::CorridorFraction);
        QVERIFY(near(r.scale, kMeasTolFraction * 4.0));
        QVERIFY(near(r.rhoM, 0.3));
        QCOMPARE(r.grade, 4);
        QVERIFY(r.basisString().isEmpty());   // §7.5 has no Needle row

        in.corridorHalfWidth.reset();
        r = grader->grade(in);
        QVERIFY(!r.graded);
        QCOMPARE(r.ungradedReason, UngradedReason::NoScale);

        VariabilityInput none;
        none.measurementTolerance = 1.0;
        QCOMPARE(grader->grade(none).ungradedReason, UngradedReason::NoUncertainty);

        in.measurementTolerance = 1.0;
        in.uncertaintyN = kNMin - 1;
        r = grader->grade(in);
        QVERIFY(r.graded);
        QCOMPARE(r.grade, 0);
        QCOMPARE(r.ungradedReason, UngradedReason::TooFewSwings);
    }

    // ---- Circular ---------------------------------------------------------------------------
    void circularWrapGradesTight()
    {
        std::vector<double> v;
        for (int i = 0; i < 12; ++i) { v.push_back(179.0 - 0.1 * i); v.push_back(-179.0 + 0.1 * i); }
        const VariabilityResult circ = gradeScalar(v, 10.0, ScaleType::Circular);
        QVERIFY(circ.graded);
        QVERIFY2(circ.sd < 2.0, qPrintable(QString::number(circ.sd)));
        QCOMPARE(circ.grade, 5);
        // The same readings taken linearly read the seam as a 358° spread.
        const VariabilityResult lin = gradeScalar(v, 10.0, ScaleType::Interval);
        QCOMPARE(lin.grade, 1);
        // Away from the seam the circular SD agrees with the linear one for a small spread.
        const std::vector<double> small = withSd(2.0, 24, 30.0);
        QVERIFY(near(circularSdDeg(small), sampleSd(small), 0.05));
        QVERIFY(near(circularMeanDeg(v), 180.0, 1e-6) || near(circularMeanDeg(v), -180.0, 1e-6));
    }

    // ---- Thresholds -------------------------------------------------------------------------
    void tooFewSwingsIsGradeZero()
    {
        VariabilityResult r = gradeScalar(withSd(1.0, kNMin - 1), 4.0);
        QVERIFY(r.graded);
        QCOMPARE(r.grade, 0);
        QCOMPARE(r.ungradedReason, UngradedReason::TooFewSwings);
        QVERIFY(std::isnan(r.bandValue()));
        QVERIFY(r.basisString("°").isEmpty());

        r = gradeScalar(withSd(1.0, kNMin), 4.0);
        QVERIFY(r.graded);
        QCOMPARE(r.grade, 5);

        // Missing readings are not swings.
        std::vector<double> v = withSd(1.0, kNMin - 1);
        v.push_back(std::nan(""));
        QCOMPARE(gradeScalar(v, 4.0).n, kNMin - 1);
    }

    void noScaleIsUngraded()
    {
        VariabilityResult r = gradeScalar(withSd(1.0), std::nullopt);
        QVERIFY(!r.graded);
        QCOMPARE(r.ungradedReason, UngradedReason::NoScale);
        QVERIFY(r.basisString().isEmpty());
        // No scale wins over too few: a measure that can never be graded shows no grade-0 promise.
        r = gradeScalar(withSd(1.0, 3), std::nullopt);
        QCOMPARE(r.ungradedReason, UngradedReason::NoScale);
        // A non-positive corridor is not a corridor.
        QCOMPARE(gradeScalar(withSd(1.0), 0.0).ungradedReason, UngradedReason::NoScale);

        VariabilityInput in;
        in.values = withSd(1.0);
        in.variabilityScale = 2.0;
        r = makeVariabilityGrader(Basis::Scalar)->grade(in);
        QCOMPARE(r.scaleSource, ScaleSource::VariabilityScale);
        QVERIFY(near(r.rho, 0.5, 1e-12));

        in.scaleType = ScaleType::Ordinal;
        QCOMPARE(makeVariabilityGrader(Basis::Scalar)->grade(in).ungradedReason, UngradedReason::ScaleTypeMismatch);
    }

    void ratioNeedsPositiveValuesForCv()
    {
        std::vector<double> v = withSd(5.0, 24, 100.0);
        VariabilityResult r = gradeScalar(v, std::nullopt, ScaleType::Ratio);
        QVERIFY(r.graded);
        QCOMPARE(r.scaleSource, ScaleSource::CV);
        QVERIFY(near(r.scale, 100.0, 1e-9));
        QVERIFY(near(r.rho, 0.05, 1e-12));

        v.back() = 0.0;
        r = gradeScalar(v, std::nullopt, ScaleType::Ratio);
        QVERIFY(!r.graded);
        QCOMPARE(r.ungradedReason, UngradedReason::NonPositiveForRatio);
        // With a corridor the CV rung is never reached.
        QVERIFY(gradeScalar(v, 30.0, ScaleType::Ratio).graded);
        // Noise subtraction on the CV route: SD 5 observed, σnoise 3, golfer SD 4 over a mean of 100.
        VariabilityInput cv;
        cv.values = withSd(5.0, 24, 100.0);
        cv.scaleType = ScaleType::Ratio;
        cv.noiseSd = 3.0;
        r = makeVariabilityGrader(Basis::Scalar)->grade(cv);
        QCOMPARE(r.scaleSource, ScaleSource::CV);
        QVERIFY(near(r.sdObserved, 5.0, 1e-12));
        QVERIFY(near(r.sd, 4.0, 1e-12));
        QVERIFY(near(r.rho, 0.04, 1e-12));
        // CV is for Ratio only.
        QCOMPARE(gradeScalar(withSd(5.0, 24, 100.0), std::nullopt, ScaleType::Interval).ungradedReason,
                 UngradedReason::NoScale);
    }

    // ---- Traces -----------------------------------------------------------------------------
    void pureTimingShiftGradesOnTiming()
    {
        VariabilityInput in;
        const std::vector<double> peaks = withSd(30.0, 24, 250.0);
        for (double p : peaks) { in.curves.push_back(bump()); in.peakTimesMs.push_back(p); }
        const VariabilityResult r = makeVariabilityGrader(Basis::Trace)->grade(in);
        QVERIFY(r.graded);
        QVERIFY(near(r.rhoAmp, 0.0, 1e-12));
        QVERIFY(near(r.sdPeakMs, 30.0, 1e-9));
        QCOMPARE(r.timingScaleSource, ScaleSource::TimingDefault);
        QVERIFY(near(r.rhoTiming, 30.0 / kTimingScaleMs, 1e-9));
        QCOMPARE(r.limitedBy, TraceLimiter::Timing);
        QCOMPARE(r.grade, 1);   // 1.5
        QCOMPARE(r.basisString(), QStringLiteral("σ peak 30 ms · n 24"));

        in.timingCorridorHalfWidthMs = 100.0;
        const VariabilityResult wide = makeVariabilityGrader(Basis::Trace)->grade(in);
        QCOMPARE(wide.timingScaleSource, ScaleSource::Corridor);
        QCOMPARE(wide.grade, 4);   // 0.30
    }

    void pureAmplitudeChangeGradesOnAmplitude()
    {
        VariabilityInput in;
        const std::vector<double> amps = withSd(0.06, 24, 1.0);
        for (double a : amps) { in.curves.push_back(bump(a)); in.peakTimesMs.push_back(250.0); }
        const VariabilityResult r = makeVariabilityGrader(Basis::Trace)->grade(in);
        QVERIFY(r.graded);
        QVERIFY(near(r.rhoTiming, 0.0, 1e-12));
        QCOMPARE(r.limitedBy, TraceLimiter::Amplitude);
        // SD(t) = SD(a)·shape(t), so RMS SD(t) = 0.06·RMS(shape); the scale is 0.10 × the mean peak 100.
        QCOMPARE(r.scaleSource, ScaleSource::PeakFraction);
        QVERIFY(near(r.scale, kTraceAmpFraction * 100.0, 1e-9));
        QVERIFY(near(r.sd, 0.06 * rms(bump()), 1e-9));
        QVERIFY(near(r.rhoAmp, r.sd / r.scale, 1e-12));
        QCOMPARE(r.rho, r.rhoAmp);
        QCOMPARE(r.grade, bandGrade(r.rhoAmp, kBands.ratio));

        in.corridorHalfWidth = 2.0 * r.sd;   // ρₐ = 0.5
        const VariabilityResult c = makeVariabilityGrader(Basis::Trace)->grade(in);
        QCOMPARE(c.scaleSource, ScaleSource::Corridor);
        QCOMPARE(c.grade, 4);
        QCOMPARE(c.basisString("°/s", 1),
                 QStringLiteral("σ %1°/s · 0.50 of corridor · n 24").arg(QString::number(c.sd, 'f', 1)));
    }

    void traceWorseHalfWins()
    {
        VariabilityInput in;
        const std::vector<double> amps = withSd(0.06, 24, 1.0);
        const std::vector<double> peaks = withSd(12.0, 24, 250.0);   // ρₜ 0.6
        for (int i = 0; i < 24; ++i) { in.curves.push_back(bump(amps[size_t(i)])); in.peakTimesMs.push_back(peaks[size_t(i)]); }
        in.corridorHalfWidth = 0.06 * rms(bump()) / 0.2;              // ρₐ 0.2
        const VariabilityResult r = makeVariabilityGrader(Basis::Trace)->grade(in);
        QCOMPARE(r.limitedBy, TraceLimiter::Timing);
        QCOMPARE(r.grade, 3);
    }

    void traceGates()
    {
        VariabilityInput in;
        for (int i = 0; i < kNMin - 1; ++i) { in.curves.push_back(bump()); in.peakTimesMs.push_back(200.0 + i); }
        VariabilityResult r = makeVariabilityGrader(Basis::Trace)->grade(in);
        QVERIFY(r.graded);
        QCOMPARE(r.grade, 0);
        QCOMPARE(r.ungradedReason, UngradedReason::TooFewSwings);

        // A flat trace with no corridor has no scale at ANY n: ungraded, timing and all, never grade 0.
        for (int count : {kNMin - 3, kNMin + 4}) {
            VariabilityInput flat;
            for (int i = 0; i < count; ++i) {
                flat.curves.push_back(std::vector<double>(size_t(kTraceGridPoints), 0.0));
                flat.peakTimesMs.push_back(200.0 + 40.0 * i);   // wildly scattered timing
            }
            const VariabilityResult f = makeVariabilityGrader(Basis::Trace)->grade(flat);
            QVERIFY(!f.graded);
            QCOMPARE(f.ungradedReason, UngradedReason::NoScale);
            QVERIFY(std::isnan(f.rhoTiming));
        }

        in.curves.push_back(std::vector<double>(100, 1.0));   // off the grid
        in.peakTimesMs.push_back(200.0);
        r = makeVariabilityGrader(Basis::Trace)->grade(in);
        QVERIFY(!r.graded);
        QCOMPARE(r.ungradedReason, UngradedReason::MalformedInput);
    }

    void circularTraceIgnoresWholeTurns()
    {
        // Two otherwise identical sets of angle curves, one with a single curve reported a turn higher.
        VariabilityInput a;
        a.scaleType = ScaleType::Circular;
        const std::vector<double> amps  = withSd(0.06, 24, 1.0);
        const std::vector<double> peaks = withSd(5.0, 24, 250.0);
        for (int i = 0; i < 24; ++i) { a.curves.push_back(bump(amps[size_t(i)])); a.peakTimesMs.push_back(peaks[size_t(i)]); }
        VariabilityInput b = a;
        for (double &x : b.curves[7]) x += 360.0;

        const auto grader = makeVariabilityGrader(Basis::Trace);
        const VariabilityResult ra = grader->grade(a), rb = grader->grade(b);
        QVERIFY(ra.graded && rb.graded);
        QCOMPARE(ra.grade, rb.grade);
        QVERIFY(near(ra.rhoAmp, rb.rhoAmp, 1e-9));
        QVERIFY(near(ra.scale, rb.scale, 1e-9));
        QCOMPARE(ra.limitedBy, rb.limitedBy);
        // Taken linearly, the shifted curve would be a 360° outlier.
        b.scaleType = ScaleType::Interval;
        QVERIFY(grader->grade(b).rhoAmp > 10.0 * ra.rhoAmp);

        // Typicality: the same swing against either window, and itself shifted a turn, reads the same.
        TraceTypicalityInput ta;
        ta.scaleType = ScaleType::Circular;
        ta.windowCurves = a.curves;
        ta.windowPeakTimesMs = a.peakTimesMs;
        ta.curve = bump(1.1);
        ta.peakTimeMs = 262.0;
        TraceTypicalityInput tb = ta;
        tb.windowCurves = b.curves;
        for (double &x : tb.curve) x -= 360.0;
        const auto ya = traceTypicality(ta), yb = traceTypicality(tb);
        QVERIFY(ya && yb);
        QVERIFY(near(ya->rmsZ, yb->rmsZ, 1e-6));
        QCOMPARE(ya->band, yb->band);
        QCOMPARE(ya->amplitudeSign, yb->amplitudeSign);
        QCOMPARE(ya->amplitudeSign, 1);
        QCOMPARE(ya->basisString(), yb->basisString());
    }

    // ---- Ordinal ----------------------------------------------------------------------------
    void ordinalFiftyFiftyIsChance()
    {
        VariabilityInput in;
        in.k = 2;
        for (int i = 0; i < 12; ++i) { in.categories << QStringLiteral("on") << QStringLiteral("off"); }
        const VariabilityResult r = makeVariabilityGrader(Basis::Ordinal)->grade(in);
        QVERIFY(r.graded);
        QVERIFY(near(r.s, 0.5));
        QVERIFY(near(r.sPrime, 0.0));
        QCOMPARE(r.grade, 1);
        QCOMPARE(r.bands().sense, GradeBands::Sense::HigherIsTighter);
    }

    void repeatedWrongOrderIsGrooved()
    {
        VariabilityInput in;
        in.k = 24;   // 4! orders of four segments
        for (int i = 0; i < 24; ++i) in.categories << QStringLiteral("arm>pelvis>club>chest");
        const VariabilityResult r = makeVariabilityGrader(Basis::Ordinal)->grade(in);
        QVERIFY(near(r.sPrime, 1.0));
        QCOMPARE(r.grade, 5);
        QCOMPARE(r.matching, 24);

        VariabilityInput bad;
        bad.k = 1;
        bad.categories = in.categories;
        QCOMPARE(makeVariabilityGrader(Basis::Ordinal)->grade(bad).ungradedReason, UngradedReason::MalformedInput);
    }

    // ---- Noise subtraction (§8.6) -----------------------------------------------------------
    void noiseSubtraction()
    {
        VariabilityInput in;
        in.values = withSd(1.0);
        in.corridorHalfWidth = 2.0;
        const auto grader = makeVariabilityGrader(Basis::Scalar);

        in.noiseSd = 0.0;
        VariabilityResult r = grader->grade(in);
        QCOMPARE(r.sd, r.sdObserved);   // identity, bit for bit

        in.noiseSd = 0.6;
        r = grader->grade(in);
        QVERIFY(near(r.sd, 0.8, 1e-12));
        QVERIFY(near(r.sdObserved, 1.0, 1e-12));
        QVERIFY(near(r.rho, 0.4, 1e-12));

        in.noiseSd = 5.0;   // more noise than spread: never negative, never NaN
        r = grader->grade(in);
        QCOMPARE(r.sd, 0.0);
        QCOMPARE(r.rho, 0.0);
        QCOMPARE(r.grade, 5);
    }

    void needleSetsMeasurementLimited()
    {
        VariabilityInput in;
        in.values = withSd(2.1);
        in.corridorHalfWidth = 5.0;
        for (int needle : {1, 2}) {
            in.sourceNeedleGrade = needle;
            QVERIFY(makeVariabilityGrader(Basis::Scalar)->grade(in).measurementLimited);
        }
        for (int needle : {0, 3, 5}) {
            in.sourceNeedleGrade = needle;
            QVERIFY(!makeVariabilityGrader(Basis::Scalar)->grade(in).measurementLimited);
        }
        // The grade itself is untouched: the golfer layer swaps in "Checking", not the grader.
        in.sourceNeedleGrade = 2;
        QCOMPARE(makeVariabilityGrader(Basis::Scalar)->grade(in).grade, 4);
    }

    // ---- Trend (§8.8) -----------------------------------------------------------------------
    void trendHoldsWithinTheNoise()
    {
        const TrendResult t = variabilityTrend(trendOf({0.50, 0.48, 0.52, 0.49}));
        QCOMPARE(t.direction, TrendDirection::Holding);
        QCOMPARE(t.sessions, 4);
    }

    void trendClearFallTightens()
    {
        const std::vector<double> rhos{0.80, 0.60, 0.40, 0.25};
        const TrendResult t = variabilityTrend(trendOf(rhos));
        QCOMPARE(t.direction, TrendDirection::Tightening);
        // The propagation, recomputed independently.
        const double tbar = 1.5;
        double sxx = 0.0, var = 0.0, sxy = 0.0, ybar = 0.5125;
        for (int i = 0; i < 4; ++i) {
            const double dt = i - tbar, se = rhos[size_t(i)] / std::sqrt(2.0 * 23);
            sxx += dt * dt; sxy += dt * (rhos[size_t(i)] - ybar); var += dt * dt * se * se;
        }
        QVERIFY(near(t.change, 3.0 * sxy / sxx, 1e-12));
        QVERIFY(near(t.changeSe, 3.0 * std::sqrt(var / (sxx * sxx)), 1e-12));
        QVERIFY(near(t.firstRho, 0.80) && near(t.lastRho, 0.25));

        QCOMPARE(variabilityTrend(trendOf({0.25, 0.40, 0.60, 0.80})).direction, TrendDirection::Loosening);
        // The same fall on two swings a session is within its own noise.
        QCOMPARE(variabilityTrend(trendOf(rhos, 3)).direction, TrendDirection::Holding);
    }

    void trendUsesTheLatestUnbrokenRun()
    {
        // Five falling driver sessions, then three steady 7-iron ones: the driver fall is not this trend.
        std::vector<TrendPoint> pts = trendOf({0.9, 0.8, 0.6, 0.4, 0.3}, 24, "driver");
        for (const TrendPoint &p : trendOf({0.50, 0.49, 0.51}, 24, "7i")) pts.push_back(p);
        TrendResult t = variabilityTrend(pts);
        QCOMPARE(t.sessions, 3);
        QCOMPARE(t.runStart, 5);
        QCOMPARE(t.direction, TrendDirection::Holding);
        QVERIFY(near(t.firstRho, 0.50));

        // Two in the latest run is not a trend, however long the run before it.
        pts.pop_back();
        t = variabilityTrend(pts);
        QCOMPARE(t.sessions, 2);
        QCOMPARE(t.direction, TrendDirection::Insufficient);
        QVERIFY(t.basisString().isEmpty());

        // A change of basis or scale is a break too.
        std::vector<TrendPoint> scale = trendOf({0.9, 0.7, 0.5, 0.3});
        scale[2].scaleKey = QStringLiteral("variabilityScale");
        QCOMPARE(variabilityTrend(scale).sessions, 1);
        std::vector<TrendPoint> basis = trendOf({0.9, 0.7, 0.5, 0.3});
        basis[1].basisKind = Basis::Trace;
        QCOMPARE(variabilityTrend(basis).sessions, 2);

        // A session without a reading is skipped, not a break.
        std::vector<TrendPoint> gap = trendOf({0.8, 0.6, 0.4, 0.25});
        gap.insert(gap.begin() + 2, TrendPoint{std::nan(""), 0, "7i", Basis::Scalar, "corridor"});
        QCOMPARE(variabilityTrend(gap).sessions, 4);
        QCOMPARE(variabilityTrend(gap).direction, TrendDirection::Tightening);
    }

    void trendIsInsufficientBelowMinimum()
    {
        QCOMPARE(variabilityTrend({}).direction, TrendDirection::Insufficient);
        QCOMPARE(variabilityTrend(trendOf({0.8, 0.2})).direction, TrendDirection::Insufficient);
        std::vector<TrendPoint> ord = trendOf({0.2, 0.5, 0.9});
        for (TrendPoint &p : ord) p.basisKind = Basis::Ordinal;
        const TrendResult t = variabilityTrend(ord);
        QCOMPARE(t.direction, TrendDirection::Insufficient);
        QVERIFY(t.ordinalBasis);
    }

    void trendReadsAtMostTheFunnelsSessions()
    {
        // Fourteen sessions: two loose ones that the §6.1 funnel has rolled off, then twelve steady.
        std::vector<double> rhos{2.0, 2.0};
        for (int i = 0; i < 12; ++i) rhos.push_back(i % 2 ? 0.49 : 0.51);
        const TrendResult t = variabilityTrend(trendOf(rhos));
        QCOMPARE(t.sessions, kFunnelMaxSessions);
        QCOMPARE(t.runStart, 2);
        QVERIFY(near(t.firstRho, 0.51));
        QCOMPARE(t.direction, TrendDirection::Holding);
        QCOMPARE(t.basisString(), QStringLiteral("ρ 0.51 → 0.49 over 12 sessions"));
    }

    // ---- Hysteresis (§8.9) ------------------------------------------------------------------
    void hysteresisDoesNotFlicker()
    {
        int g = bandGrade(0.48, kBands.ratio);
        QCOMPARE(g, 4);
        for (double rho : {0.52, 0.49, 0.51, 0.48, 0.52, 0.50, 0.53}) {
            g = hysteresisGrade(rho, g, kBands.ratio);
            QCOMPARE(g, 4);
        }
        // Frozen assessments use the plain bands for the same value.
        QCOMPARE(bandGrade(0.52, kBands.ratio), 3);

        g = hysteresisGrade(0.56, g, kBands.ratio);   // past 0.50 + 0.05
        QCOMPARE(g, 3);
        for (double rho : {0.48, 0.52, 0.46, 0.51}) {
            g = hysteresisGrade(rho, g, kBands.ratio);
            QCOMPARE(g, 3);
        }
        g = hysteresisGrade(0.44, g, kBands.ratio);   // past 0.50 − 0.05
        QCOMPARE(g, 4);

        // A jump across several bands takes the plain grade.
        QCOMPARE(hysteresisGrade(0.9, 5, kBands.ratio), 2);
        // No previous grade imposes nothing.
        QCOMPARE(hysteresisGrade(0.52, 0, kBands.ratio), 3);

        // s′ on the ordinal bands, which run the other way.
        int o = bandGrade(0.67, kBands.ordinal);
        QCOMPARE(o, 4);
        for (double sp : {0.63, 0.66, 0.61, 0.64}) { o = hysteresisGrade(sp, o, kBands.ordinal); QCOMPARE(o, 4); }
        o = hysteresisGrade(0.59, o, kBands.ordinal);
        QCOMPARE(o, 3);
        o = hysteresisGrade(0.69, o, kBands.ordinal);
        QCOMPARE(o, 3);
        o = hysteresisGrade(0.71, o, kBands.ordinal);
        QCOMPARE(o, 4);
    }

    // ---- Typicality (§8.7, amended by R22: the robust z) ---------------------------------------
    void scalarTypicalityBands()
    {
        const std::vector<double> window = withSpread(1.0, 24, 10.0);
        QCOMPARE(scalarTypicality(10.9, window)->band, TypicalityBand::Usual);       // z 0.9
        QCOMPARE(scalarTypicality(8.5, window)->band, TypicalityBand::ALittleOff);   // z −1.5
        QCOMPARE(scalarTypicality(11.9, window)->band, TypicalityBand::ALittleOff);  // z 1.9
        QCOMPARE(typicalityBand(1.0), TypicalityBand::Usual);
        QCOMPARE(typicalityBand(-2.0), TypicalityBand::ALittleOff);
        QCOMPARE(scalarTypicality(12.5, window)->band, TypicalityBand::Unusual);
        QVERIFY(near(scalarTypicality(12.5, window)->z, 2.5, 1e-9));
        QVERIFY(!scalarTypicality(10.0, withSpread(1.0, kNMin - 1, 10.0)));

        std::vector<double> seam;
        for (int i = 0; i < 12; ++i) { seam.push_back(179.0 - 0.1 * i); seam.push_back(-179.0 + 0.1 * i); }
        const auto c = scalarTypicality(-179.5, seam, ScaleType::Circular);
        QVERIFY(c);
        QCOMPARE(c->band, TypicalityBand::Usual);
    }

    // R22: z is (x − median)/(IQR/1.349), so one wild swing in the window moves neither the centre nor the
    // scale much. By hand: {1…7, 100}: median 4.5, Q1 2.75, Q3 6.25 (type 7), spread 3.5/1.349.
    void typicalityIsTheRobustZ()
    {
        const std::vector<double> window{ 1, 2, 3, 4, 5, 6, 7, 100 };
        const auto t = scalarTypicality(8.0, window);
        QVERIFY(t);
        QVERIFY(near(t->usual, 4.5, 1e-12));
        QVERIFY(near(t->sd, 3.5 / 1.349, 1e-12));
        QVERIFY(near(t->z, 1.349, 1e-12));
        QVERIFY(t->median);
        QCOMPARE(t->band, TypicalityBand::ALittleOff);   // the mean (16) and SD (≈ 34) would have said usual
        QCOMPARE(t->basisString(QString(), 1), QStringLiteral("this swing 8.0 · median 4.5"));

        // Half the window on one value: IQR 0 is no spread, as a flat window (the zero-spread rule).
        const std::vector<double> stacked{ 5, 5, 5, 5, 5, 5, 4, 9 };
        QCOMPARE(scalarTypicality(5.0, stacked)->z, 0.0);
        QVERIFY(std::isinf(scalarTypicality(5.5, stacked)->z));
    }

    void zeroSpreadTypicality()
    {
        const std::vector<double> flat(10, 5.0);
        const auto same = scalarTypicality(5.0, flat);
        QVERIFY(same);
        QCOMPARE(same->z, 0.0);
        QCOMPARE(same->band, TypicalityBand::Usual);
        const auto above = scalarTypicality(5.1, flat);
        QVERIFY(std::isinf(above->z) && above->z > 0.0);
        QCOMPARE(above->band, TypicalityBand::Unusual);
        const auto below = scalarTypicality(4.9, flat);
        QVERIFY(std::isinf(below->z) && below->z < 0.0);
        QCOMPARE(below->band, TypicalityBand::Unusual);
    }

    void circularTypicalityAcrossTheSeam()
    {
        // A window centred on 180° with SD 2°, reported in (−180, 180]: half of it reads near −178.
        std::vector<double> window;
        for (double x : withSd(2.0, 24, 180.0)) window.push_back(wrapDeg(x));
        const auto off = scalarTypicality(-177.0, window, ScaleType::Circular);   // 183°: +3° off the mean
        QVERIFY(off);
        QVERIFY(near(off->sd, 2.0, 0.06));   // the circular SD is a population SD: ≈ 2·√(23/24)
        QVERIFY(near(off->z, 3.0 / off->sd, 1e-6));
        QVERIFY(off->z > 1.0 && off->z <= 2.0);
        QCOMPARE(off->band, TypicalityBand::ALittleOff);
        const auto far = scalarTypicality(175.0, window, ScaleType::Circular);    // −5° off the mean
        QVERIFY(near(far->z, -5.0 / far->sd, 1e-6));
        QCOMPARE(far->band, TypicalityBand::Unusual);
        const auto turn = scalarTypicality(-177.0 + 360.0, window, ScaleType::Circular);
        QVERIFY(near(turn->z, off->z, 1e-9));
        // Circular keeps the circular mean (no circular median in the code), so its basis keeps "average".
        QVERIFY(!off->median);
        QVERIFY(off->basisString(QStringLiteral("°"), 1).contains(QStringLiteral(" · average ")));
    }

    // ---- The outcome rule (R4, R4b) ----------------------------------------------------------
    void outcomeRule()
    {
        for (const char *k : { "lm.carryDistance", "lm.totalDistance", "lm.offline", "lm.launchDirection", "lm.ballSpeed",
                               "lm.launchAngle", "lm.spinRate", "lm.spinAxis", "lm.peakHeight", "lm.smashFactor",
                               "ballSpeed", "launchDirection", "compoundMiss" })
            QVERIFY2(isOutcomeMetric(QString::fromLatin1(k)), k);
        for (const char *k : { "lm.clubheadSpeed", "lm.clubPath", "lm.faceAngle", "lm.faceToPath", "lm.attackAngle",
                               "lm.dynamicLoft", "lm.lieAngle", "lm.lowPointAhead", "lm.strikeLocation", "lm.strikeHeight",
                               "clubPath", "lowPointAhead", "headRise" })
            QVERIFY2(!isOutcomeMetric(QString::fromLatin1(k)), k);
    }

    // ---- Basis strings (§7.5) ---------------------------------------------------------------
    void basisStrings()
    {
        // Scalar consistency.
        VariabilityResult r = gradeScalar(withSd(2.1), 5.0);
        QCOMPARE(r.basisString(QStringLiteral("°"), 1), QStringLiteral("σ 2.1° · 0.42 of corridor · n 24"));
        QCOMPARE(r.basisString(QStringLiteral("cm"), 1), QStringLiteral("σ 2.1 cm · 0.42 of corridor · n 24"));
        // A σ always shows two significant figures: the measure's places, or more where those would print fewer.
        QCOMPARE(gradeScalar(withSd(3.2), 10.0).basisString(QStringLiteral("% stance width"), 0),
                 QStringLiteral("σ 3.2% stance width · 0.32 of corridor · n 24"));
        QCOMPARE(gradeScalar(withSd(0.04), 0.1).basisString(QStringLiteral("°"), 1), QStringLiteral("σ 0.040° · 0.40 of corridor · n 24"));
        QCOMPARE(gradeScalar(withSd(45.0), 100.0).basisString(QStringLiteral("°/s"), 0), QStringLiteral("σ 45°/s · 0.45 of corridor · n 24"));
        // A figure that rounds to zero is never "-0".
        QCOMPARE(formatWithUnit(-0.04, QStringLiteral("°"), 1), QStringLiteral("0.0°"));
        QCOMPARE(formatWithUnit(-0.4, QStringLiteral("cm"), 0), QStringLiteral("0 cm"));
        QCOMPARE(formatWithUnit(-0.6, QStringLiteral("cm"), 0), QStringLiteral("-1 cm"));

        // Units that start with °, % or : sit against the number; others follow a space.
        QCOMPARE(formatWithUnit(12.34, QStringLiteral("% stance width"), 1), QStringLiteral("12.3% stance width"));
        QCOMPARE(formatWithUnit(3.14, QStringLiteral(":1"), 1), QStringLiteral("3.1:1"));
        QCOMPARE(formatWithUnit(120.0, QStringLiteral("°/s"), 0), QStringLiteral("120°/s"));
        QCOMPARE(formatWithUnit(4.8, QStringLiteral("cm"), 1), QStringLiteral("4.8 cm"));
        QCOMPARE(formatWithUnit(12.0, QString(), 0), QStringLiteral("12"));

        // Ball flight (§7.5, §9.6).
        const std::vector<double> offline = withSd(6.2, 12, 3.0), carry = withSd(4.8, 12, 150.0);
        QCOMPARE(ballFlightBasisString(sampleSd(offline), sampleSd(carry), 12, QStringLiteral("yd"), 0),
                 QStringLiteral("offline ±6 yd · carry ±5 yd · 12 shots"));
        QCOMPARE(ballFlightBasisString(5.5, 4.26, 9, QStringLiteral("m"), 1),
                 QStringLiteral("offline ±5.5 m · carry ±4.3 m · 9 shots"));

        // Measurement-limited.
        VariabilityInput in;
        in.values = withSd(2.1);
        in.corridorHalfWidth = 5.0;
        in.sourceNeedleGrade = 1;
        QCOMPARE(makeVariabilityGrader(Basis::Scalar)->grade(in).basisString(QStringLiteral("°"), 1),
                 QStringLiteral("σ 2.1° · 0.42 of corridor · n 24 · measurement-limited"));

        // The other scale rungs are named for themselves, never "corridor".
        in.sourceNeedleGrade.reset();
        in.corridorHalfWidth.reset();
        in.variabilityScale = 5.0;
        QCOMPARE(makeVariabilityGrader(Basis::Scalar)->grade(in).basisString(QStringLiteral("°"), 1),
                 QStringLiteral("σ 2.1° · 0.42 of scale · n 24"));
        QCOMPARE(gradeScalar(withSd(4.2, 24, 100.0), std::nullopt, ScaleType::Ratio).basisString(QStringLiteral("km/h"), 1),
                 QStringLiteral("σ 4.2 km/h · CV 0.04 · n 24"));

        // Timing-limited trace.
        VariabilityInput tr;
        for (double p : withSd(18.0, 24, 240.0)) { tr.curves.push_back(bump()); tr.peakTimesMs.push_back(p); }
        QCOMPARE(makeVariabilityGrader(Basis::Trace)->grade(tr).basisString(), QStringLiteral("σ peak 18 ms · n 24"));

        // Ordinal.
        VariabilityInput ord;
        ord.k = 24;
        for (int i = 0; i < 21; ++i) ord.categories << QStringLiteral("pelvis>chest>arm>club");
        ord.categories << QStringLiteral("chest>pelvis>arm>club") << QStringLiteral("pelvis>arm>chest>club")
                       << QStringLiteral("chest>pelvis>arm>club");
        const VariabilityResult o = makeVariabilityGrader(Basis::Ordinal)->grade(ord);
        QCOMPARE(o.basisString(), QStringLiteral("Same order in 21 of 24 swings"));
        QCOMPARE(o.grade, 5);   // s′ (0.875 − 1/24)/(1 − 1/24) = 0.87

        // Trend.
        const TrendResult t = variabilityTrend(trendOf({0.62, 0.60, 0.52, 0.47, 0.41, 0.38}));
        QCOMPARE(t.basisString(), QStringLiteral("ρ 0.62 → 0.38 over 6 sessions"));

        // Typicality, scalar.
        const auto s = scalarTypicality(5.2, withSpread(0.5, 24, 4.8));
        QVERIFY(s);
        QCOMPARE(s->basisString(QStringLiteral("cm"), 1), QStringLiteral("this swing 5.2 cm · median 4.8 cm"));
        // a surface's own figure format (the launch monitor's "3.5° L")
        QCOMPARE(s->basisString([](double v) { return QString::number(v, 'f', 1) + QStringLiteral(" u"); }),
                 QStringLiteral("this swing 5.2 u · median 4.8 u"));

        // Typicality, trace: on time, early, late and higher.
        TraceTypicalityInput ti;
        const std::vector<double> amps  = withSpread(0.05, 24, 1.0);
        const std::vector<double> peaks = withSpread(10.0, 24, 200.0);
        for (int i = 0; i < 24; ++i) {
            ti.windowCurves.push_back(bump(amps[size_t(i)]));
            ti.windowPeakTimesMs.push_back(peaks[size_t(i)]);
        }
        ti.curve = bump(1.0);
        ti.peakTimeMs = 205.0;
        auto tt = traceTypicality(ti);
        QVERIFY(tt);
        QCOMPARE(tt->band, TypicalityBand::Usual);
        QCOMPARE(tt->basisString(), QStringLiteral("peak on time"));

        ti.peakTimeMs = 175.0;
        tt = traceTypicality(ti);
        QVERIFY(near(tt->timingDeviationMs, -25.0, 1e-9));
        QCOMPARE(tt->basisString(), QStringLiteral("peak 25 ms early"));

        ti.peakTimeMs = 245.0;
        ti.curve = bump(1.2);
        tt = traceTypicality(ti);
        QCOMPARE(tt->amplitudeSign, 1);
        QCOMPARE(tt->band, TypicalityBand::Unusual);   // z(t) = 4 everywhere
        QVERIFY(near(tt->rmsZ, 4.0, 1e-6));
        QCOMPARE(tt->basisString(), QStringLiteral("peak 45 ms late, higher"));

        // A deviation that rounds to 0 ms is on time, however tight the window's timing.
        TraceTypicalityInput tight = ti;
        tight.windowPeakTimesMs = withSpread(0.1, 24, 200.0);
        tight.curve = bump(1.0);
        tight.peakTimeMs = 200.3;   // z 3, but 0 ms
        tt = traceTypicality(tight);
        QCOMPARE(tt->timing, PeakTiming::OnTime);
        QCOMPARE(tt->basisString(), QStringLiteral("peak on time"));
        tight.peakTimeMs = 201.0;
        QCOMPARE(traceTypicality(tight)->basisString(), QStringLiteral("peak 1 ms late"));

        ti.windowCurves.resize(kNMin - 1);
        ti.windowPeakTimesMs.resize(kNMin - 1);
        QVERIFY(!traceTypicality(ti));
    }

    // ---- R3: the propagated σ marks, never subtracts ---------------------------------------
    void propagatedSigmaMarksMeasurementLimited()
    {
        VariabilityInput in;
        in.values = withSd(1.0);
        in.corridorHalfWidth = 5.0;
        const auto grader = makeVariabilityGrader(Basis::Scalar);

        const VariabilityResult plain = grader->grade(in);   // absent
        QVERIFY(!plain.measurementLimited);
        QCOMPARE(plain.grade, 5);                              // ρ 0.20

        in.propagatedSigma = 1.5;                              // SD 1.0 inside it
        VariabilityResult r = grader->grade(in);
        QVERIFY(r.measurementLimited);
        QCOMPARE(r.grade, plain.grade);                        // grade unchanged
        QCOMPARE(r.sd, r.sdObserved);                          // never subtracted
        QCOMPARE(r.rho, plain.rho);
        QCOMPARE(r.basisString(QStringLiteral("°"), 1),
                 QStringLiteral("σ 1.0° · 0.20 of corridor · n 24 · measurement-limited"));

        in.propagatedSigma = 0.5;                              // SD above it
        r = grader->grade(in);
        QVERIFY(!r.measurementLimited);
        QCOMPARE(r.basisString(QStringLiteral("°"), 1), QStringLiteral("σ 1.0° · 0.20 of corridor · n 24"));

        for (double unusable : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
            in.propagatedSigma = unusable;
            QVERIFY(!grader->grade(in).measurementLimited);
        }

        // Grade 0: no spread was taken, so nothing to compare.
        VariabilityInput few = in;
        few.values = withSd(1.0, kNMin - 1);
        few.propagatedSigma = 10.0;
        const VariabilityResult f = grader->grade(few);
        QCOMPARE(f.grade, 0);
        QVERIFY(!f.measurementLimited);
    }

    void propagatedSigmaTestsTheObservedSdNotTheNoiseFreeOne()
    {
        // The noiseSd path is unchanged, and the R3 test reads the OBSERVED SD, before it.
        VariabilityInput in;
        in.values = withSd(1.0);
        in.corridorHalfWidth = 2.0;
        in.noiseSd = 0.6;
        in.propagatedSigma = 0.9;   // above the noise-free 0.8, below the observed 1.0
        const VariabilityResult r = makeVariabilityGrader(Basis::Scalar)->grade(in);
        QVERIFY(near(r.sd, 0.8, 1e-12));
        QVERIFY(near(r.sdObserved, 1.0, 1e-12));
        QVERIFY(near(r.rho, 0.4, 1e-12));
        QVERIFY(!r.measurementLimited);
    }

    void propagatedSigmaOnACircularScalarUsesTheCircularSd()
    {
        // 2° of spread across the seam: the linear SD is ~180°, the circular one ~2°.
        VariabilityInput in;
        for (double x : withSd(2.0, 24, 180.0)) in.values.push_back(wrapDeg(x));
        in.scaleType = ScaleType::Circular;
        in.corridorHalfWidth = 10.0;
        in.propagatedSigma = 3.0;
        const VariabilityResult r = makeVariabilityGrader(Basis::Scalar)->grade(in);
        QVERIFY(r.sdObserved < 3.0);
        QVERIFY(r.measurementLimited);
        in.propagatedSigma = 1.0;
        QVERIFY(!makeVariabilityGrader(Basis::Scalar)->grade(in).measurementLimited);
    }

    void propagatedSigmaOnATraceUsesTheRmsObservedSd()
    {
        VariabilityInput in;
        const std::vector<double> amps = withSd(0.06, 24, 1.0);
        for (double a : amps) { in.curves.push_back(bump(a)); in.peakTimesMs.push_back(250.0); }
        const double rmsSd = 0.06 * rms(bump());
        in.corridorHalfWidth = 2.0 * rmsSd;
        const auto grader = makeVariabilityGrader(Basis::Trace);
        const VariabilityResult plain = grader->grade(in);
        QVERIFY(!plain.measurementLimited);

        in.propagatedSigma = rmsSd * 1.01;
        const VariabilityResult r = grader->grade(in);
        QVERIFY(r.measurementLimited);
        QCOMPARE(r.grade, plain.grade);
        QCOMPARE(r.sd, r.sdObserved);
        QVERIFY(r.basisString(QStringLiteral("°/s"), 1).endsWith(QStringLiteral(" · measurement-limited")));

        in.propagatedSigma = rmsSd * 0.99;
        QVERIFY(!grader->grade(in).measurementLimited);

        // Timing sets the grade: an amplitude inside its own σ does not limit it.
        in.propagatedSigma = rmsSd * 1.01;
        const std::vector<double> peaks = withSd(200.0, 24, 250.0);
        for (size_t i = 0; i < in.peakTimesMs.size(); ++i) in.peakTimesMs[i] = peaks[i];
        const VariabilityResult t = grader->grade(in);
        QCOMPARE(t.limitedBy, TraceLimiter::Timing);
        QVERIFY(!t.measurementLimited);
    }

    // ---- R1: a cell takes its loosest third --------------------------------------------------
    void cellGradeTakesTheSecondLoosestOfSix()
    {
        // Six ρ, shuffled: n = 6 → index ⌈4.02⌉ − 1 = 4 of the sorted, the 5th smallest (0.80).
        std::vector<VariabilityResult> rs;
        for (double rho : {0.60, 0.10, 0.90, 0.30, 0.80, 0.20}) {
            VariabilityResult r;
            r.graded = true;
            r.rho = rho;
            r.grade = bandGrade(rho, kBands.ratio);
            rs.push_back(r);
        }
        rs[4].measurementLimited = true;   // the 0.80, which sets the grade
        rs[2].measurementLimited = false;  // the loosest does not
        const CellGrade c = cellGrade(rs);
        QVERIFY(c.graded);
        QCOMPARE(c.n, 6);
        QCOMPARE(c.setting, 4);
        QCOMPARE(c.rho, 0.80);
        QCOMPARE(c.grade, 2);              // 0.75 < 0.80 ≤ 1.00
        QVERIFY(c.measurementLimited);

        rs[4].measurementLimited = false;
        rs[2].measurementLimited = true;
        QVERIFY(!cellGrade(rs).measurementLimited);   // follows the setting reading only

        // n = 1 and n = 3: the only one, and the loosest.
        QCOMPARE(cellGrade({rs[0]}).setting, 0);
        QCOMPARE(cellGrade({rs[1], rs[3], rs[5]}).setting, 1);   // 0.30 of 0.10/0.30/0.20
    }

    void cellGradeIgnoresUngradedAndZeroReadings()
    {
        std::vector<VariabilityResult> rs;
        auto graded = [](double rho) {
            VariabilityResult r;
            r.graded = true; r.rho = rho; r.grade = bandGrade(rho, kBands.ratio);
            return r;
        };
        VariabilityResult ungraded;                        // no scale: graded false
        ungraded.rho = 5.0;
        VariabilityResult zero;                            // too few swings
        zero.graded = true; zero.grade = 0; zero.ungradedReason = UngradedReason::TooFewSwings;
        VariabilityResult ordinal;                         // graded, but no ρ
        ordinal.basis = Basis::Ordinal; ordinal.graded = true; ordinal.grade = 1;
        rs = {ungraded, graded(0.2), zero, ordinal, graded(0.4)};
        const CellGrade c = cellGrade(rs);
        QCOMPARE(c.n, 2);
        QCOMPARE(c.setting, 4);                            // ⌈1.34⌉ − 1 = 1: the 0.4
        QCOMPARE(c.grade, 4);

        // Every graded reading under N_MIN: grade 0.
        const CellGrade z = cellGrade({ungraded, zero, zero});
        QVERIFY(z.graded);
        QCOMPARE(z.grade, 0);
        QCOMPARE(z.n, 0);
        QCOMPARE(z.setting, -1);
        QVERIFY(!z.measurementLimited);

        // None graded: no grade at all, the cell's "-".
        const CellGrade none = cellGrade({ungraded, ungraded});
        QVERIFY(!none.graded);
        QCOMPARE(none.grade, 0);
        QVERIFY(!cellGrade({}).graded);
    }

    // ---- R8: typicality against the session's other same-club swings ------------------------
    void scalarTypicalityWindowIsTheOtherSameClubSwings()
    {
        // 20 seven-iron swings (robust spread 1 about a median of 10), 10 drivers far away, one swing under test.
        std::vector<TypicalitySwing> swings;
        const std::vector<double> sevens = withSpread(1.0, 20, 10.0);
        for (int i = 0; i < 20; ++i) {
            swings.push_back({QStringLiteral("7i-%1").arg(i), QStringLiteral("7i"), sevens[size_t(i)]});
            if (i < 10) swings.push_back({QStringLiteral("dr-%1").arg(i), QStringLiteral("DR"), 100.0 + i});
        }
        swings.push_back({QStringLiteral("me"), QStringLiteral("7i"), 11.5});
        swings.push_back({QStringLiteral("gap"), QStringLiteral("7i"), std::numeric_limits<double>::quiet_NaN()});

        const auto w = scalarTypicalityWindow(swings, QStringLiteral("me"));
        QVERIFY(w);
        // Same club only, this swing out: exactly the 20 sevens.
        QCOMPARE(w->typicality.n, 20);
        QVERIFY(near(w->typicality.usual, 10.0, 1e-9));
        QVERIFY(near(w->typicality.sd, 1.0, 1e-9));
        QVERIFY(near(w->typicality.z, 1.5, 1e-9));
        QCOMPARE(w->typicality.band, TypicalityBand::ALittleOff);
        QCOMPARE(w->typicality.basisString(QStringLiteral("cm"), 1), QStringLiteral("this swing 11.5 cm · median 10.0 cm"));

        // Ghosts: the latest 12 others of the club, oldest first, z against the same window.
        QCOMPARE(int(w->ghosts.size()), kTypicalityMaxGhosts);
        for (int g = 0; g < kTypicalityMaxGhosts; ++g) {
            const int i = 20 - kTypicalityMaxGhosts + g;
            QCOMPARE(w->ghosts[size_t(g)].swingId, QStringLiteral("7i-%1").arg(i));
            QVERIFY(near(w->ghosts[size_t(g)].z, sevens[size_t(i)] - 10.0, 1e-9));
        }
        QVERIFY(!w->corridor);

        // The corridor in the window's z.
        const auto c = scalarTypicalityWindow(swings, QStringLiteral("me"), ScaleType::Interval, 8.5, 11.0);
        QVERIFY(c && c->corridor);
        QVERIFY(near(c->corridor->loZ, -1.5, 1e-9));
        QVERIFY(near(c->corridor->hiZ, 1.0, 1e-9));
        QVERIFY(!scalarTypicalityWindow(swings, QStringLiteral("me"), ScaleType::Interval, 8.5, {})->corridor);

        // A window swing is graded against the others, itself out: n stays 20 (19 others + "me").
        QCOMPARE(scalarTypicalityWindow(swings, QStringLiteral("7i-3"))->typicality.n, 20);

        // No result: unknown swing, its own value missing, fewer than N_MIN others.
        QVERIFY(!scalarTypicalityWindow(swings, QStringLiteral("nobody")));
        QVERIFY(!scalarTypicalityWindow(swings, QStringLiteral("gap")));
        std::vector<TypicalitySwing> few;
        for (int i = 0; i < kNMin; ++i)
            few.push_back({QStringLiteral("s%1").arg(i), QStringLiteral("7i"), double(i)});
        QVERIFY(!scalarTypicalityWindow(few, QStringLiteral("s0")));   // 7 others
        few.push_back({QStringLiteral("s8"), QStringLiteral("7i"), 8.0});
        const auto eight = scalarTypicalityWindow(few, QStringLiteral("s0"));   // 8 others
        QVERIFY(eight);
        QCOMPARE(int(eight->ghosts.size()), kNMin);
        QCOMPARE(eight->ghosts.front().swingId, QStringLiteral("s1"));
        QVERIFY(!scalarTypicalityWindow(few, QStringLiteral("dr-0")));
    }

    void traceTypicalityWindowCarriesTheEnvelope()
    {
        std::vector<TypicalityTraceSwing> swings;
        const std::vector<double> amps = withSpread(0.1, 12, 1.0);
        for (int i = 0; i < 12; ++i)
            swings.push_back({QStringLiteral("7i-%1").arg(i), QStringLiteral("7i"), bump(amps[size_t(i)]), 250.0 + i});
        for (int i = 0; i < 4; ++i)
            swings.push_back({QStringLiteral("dr-%1").arg(i), QStringLiteral("DR"), bump(5.0), 100.0});
        swings.push_back({QStringLiteral("me"), QStringLiteral("7i"), bump(1.0), 255.5});

        const auto w = traceTypicalityWindow(swings, QStringLiteral("me"), ScaleType::Interval, 0.2, 0.6);
        QVERIFY(w);
        QCOMPARE(w->typicality.n, 12);
        QCOMPARE(int(w->mean.size()), kTraceGridPoints);
        QCOMPARE(int(w->sd.size()), kTraceGridPoints);
        QCOMPARE(int(w->swing.size()), kTraceGridPoints);
        QVERIFY(near(w->mean[50], 100.0, 1e-9));        // the per-point median; the drivers' bump(5) is not in it
        QVERIFY(near(w->sd[50], 10.0, 1e-9));           // the per-point IQR/1.349 (R22)
        QVERIFY(w->swing == bump(1.0));
        QCOMPARE(w->typicality.band, TypicalityBand::Usual);
        QVERIFY(w->corridor);
        QCOMPARE(w->corridor->loT, 0.2);
        QCOMPARE(w->corridor->hiT, 0.6);
        QVERIFY(!traceTypicalityWindow(swings, QStringLiteral("me"), ScaleType::Interval, 0.6, 0.2)->corridor);
        QVERIFY(!traceTypicalityWindow(swings, QStringLiteral("me"))->corridor);

        // Fewer than N_MIN others of the club: none (the drivers have 3 others).
        QVERIFY(!traceTypicalityWindow(swings, QStringLiteral("dr-0")));
        QVERIFY(!traceTypicalityWindow(swings, QStringLiteral("nobody")));
    }
};

QTEST_GUILESS_MAIN(VariabilityGraderTest)
#include "variability_grader_test.moc"
