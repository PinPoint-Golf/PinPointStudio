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

// The variability geometry provider (src/Gui/variability/variability_geometry.h): the §4.4 – §4.7 tables
// of docs/design/variability-mark/spec.md as data. The numbers are checked against the spec's tables
// (computed here by hand from the spec, not from the provider's constants); the grade ladder is checked
// for what the marks are FOR — spread never grows with the grade, grade 5 converges, grade 0 is the
// neutral element alone. The QML singleton is checked for its one promise (one JS object per key), and
// for its path call actually feeding a real PathPolyline.

#include "variability/variability_geometry.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace pinpoint::variability;

namespace {

constexpr double kEps = 1e-9;
constexpr double kPi  = 3.14159265358979323846;   // not M_PI: MSVC hides it without _USE_MATH_DEFINES

bool near(double a, double b, double eps = kEps) { return std::abs(a - b) <= eps; }

const MarkVariant kGraded[] = {MarkVariant::Dispersion, MarkVariant::Overlay, MarkVariant::Needle};
const MarkForm    kForms[]  = {MarkForm::Icon, MarkForm::Cell};

const char *name(MarkVariant v, MarkForm f)
{
    static const char *names[3][2] = {{"Dispersion Icon", "Dispersion Cell"},
                                      {"Overlay Icon", "Overlay Cell"},
                                      {"Needle Icon", "Needle Cell"}};
    return names[int(v)][int(f)];
}

const VariabilityShape &shapeOf(MarkVariant v, MarkForm f, int g)
{
    return makeCanonicalVariabilityGeometry()->shape(v, f, g);
}

// The largest distance of a dot from (cx, cy).
double maxDotDistance(const VariabilityShape &s, double cx, double cy)
{
    double d = 0;
    for (const auto &dot : s.dots)
        d = std::max(d, std::hypot(dot.x - cx, dot.y - cy));
    return d;
}

double dotSpanX(const VariabilityShape &s)
{
    const auto [lo, hi] = std::minmax_element(s.dots.begin(), s.dots.end(),
                                              [](const auto &a, const auto &b) { return a.x < b.x; });
    return hi->x - lo->x;
}

double dotSpanY(const VariabilityShape &s)
{
    const auto [lo, hi] = std::minmax_element(s.dots.begin(), s.dots.end(),
                                              [](const auto &a, const auto &b) { return a.y < b.y; });
    return hi->y - lo->y;
}

// How far apart the traces are at their widest, sample by sample (what the eye reads as spread).
double pointwiseTraceSpread(const VariabilityShape &s)
{
    double worst = 0;
    const std::size_t n = s.traces.front().points.size();
    for (std::size_t i = 0; i < n; ++i) {
        double lo = 1e9, hi = -1e9;
        for (const auto &t : s.traces) {
            lo = std::min(lo, t.points[i].y());
            hi = std::max(hi, t.points[i].y());
        }
        worst = std::max(worst, hi - lo);
    }
    return worst;
}

double centreSpread(const VariabilityShape &s)
{
    const auto [lo, hi] = std::minmax_element(s.traces.begin(), s.traces.end(),
                                              [](const auto &a, const auto &b) { return a.centre < b.centre; });
    return hi->centre - lo->centre;
}

double heightSpread(const VariabilityShape &s)
{
    const auto [lo, hi] = std::minmax_element(s.traces.begin(), s.traces.end(),
                                              [](const auto &a, const auto &b) { return a.height < b.height; });
    return hi->height - lo->height;
}

// Shoelace area of a closed polygon.
double area(const std::vector<QPointF> &p)
{
    double a = 0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        a += p[i].x() * p[i + 1].y() - p[i + 1].x() * p[i].y();
    return std::abs(a) / 2;
}

double traceY(double x, double c, double h, double scale, double base) { return base - h * std::exp(-std::pow((x - c) / scale, 2)); }

} // namespace

class VariabilityGeometryTest : public QObject {
    Q_OBJECT

private slots:
    void deterministic();
    void factoryIsShared();
    void dispersionNumbers();
    void overlayNumbers();
    void needleNumbers();
    void sectorIsSampledFinely();
    void gradeFiveConverges();
    void spreadNeverIncreasesWithGrade();
    void gradeZeroIsTheNeutralElementOnly();
    void neutralElementsDoNotMoveWithGrade();
    void clamping();
    void strokesAndSmallSizes();
    void typicalityIsDataOnly();
    void jsonMirrorsTheShape();
    void singletonCachesOneObjectPerKey();
    void tracePathFeedsAPathPolyline();
};

void VariabilityGeometryTest::deterministic()
{
    const CanonicalVariabilityGeometry a, b;
    for (int v = 0; v < kMarkVariantCount; ++v)
        for (int f = 0; f < kMarkFormCount; ++f)
            for (int g = 0; g <= kMarkGradeMax; ++g)
                QCOMPARE(toJson(a.shape(MarkVariant(v), MarkForm(f), g)),
                         toJson(b.shape(MarkVariant(v), MarkForm(f), g)));
}

void VariabilityGeometryTest::factoryIsShared()
{
    const auto a = makeCanonicalVariabilityGeometry();
    const auto b = makeCanonicalVariabilityGeometry();
    QCOMPARE(a.get(), b.get());
    QCOMPARE(&a->shape(MarkVariant::Overlay, MarkForm::Cell, 3), &b->shape(MarkVariant::Overlay, MarkForm::Cell, 3));
    const CanonicalVariabilityGeometry fresh;
    QCOMPARE(toJson(fresh.shape(MarkVariant::Needle, MarkForm::Icon, 2)),
             toJson(a->shape(MarkVariant::Needle, MarkForm::Icon, 2)));
}

void VariabilityGeometryTest::dispersionNumbers()
{
    // §4.4 Icon: ring (12, 12) r 10.5; dots at −30°, 90°, 210° and spread[g] = [7.2, 5.4, 3.6, 1.8, 0].
    const auto &i1 = shapeOf(MarkVariant::Dispersion, MarkForm::Icon, 1);
    QVERIFY(i1.ring);
    QCOMPARE(i1.ring->cx, 12.0);
    QCOMPARE(i1.ring->cy, 12.0);
    QCOMPARE(i1.ring->r, 10.5);
    QCOMPARE(i1.ring->sweepAngle, 360.0);
    QCOMPARE(int(i1.dots.size()), 3);
    QVERIFY(near(i1.dots[0].x, 12 + 7.2 * std::cos(-kPi / 6)));   // 18.2354
    QVERIFY(near(i1.dots[0].y, 12 - 7.2 * 0.5));                   // 8.4
    QVERIFY(near(i1.dots[1].x, 12));
    QVERIFY(near(i1.dots[1].y, 19.2));
    QVERIFY(near(i1.dots[2].x, 12 - 7.2 * std::cos(kPi / 6)));
    QVERIFY(near(i1.dots[2].y, 8.4));
    const auto &i3 = shapeOf(MarkVariant::Dispersion, MarkForm::Icon, 3);
    QVERIFY(near(i3.dots[1].y, 15.6));
    QVERIFY(near(maxDotDistance(i3, 12, 12), 3.6));
    QVERIFY(near(maxDotDistance(shapeOf(MarkVariant::Dispersion, MarkForm::Icon, 4), 12, 12), 1.8));

    // §4.4 Cell: target line x 40, y 2–22; ellipse rx sx+4, ry sy+3.5; dot k at (40 + sx·ox[k], 12 + sy·oy[k]).
    const auto &c2 = shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 2);   // g = 1: sx 21, sy 5.5
    QVERIFY(c2.line);
    QCOMPARE(c2.line->x1, 40.0);
    QCOMPARE(c2.line->y1, 2.0);
    QCOMPARE(c2.line->x2, 40.0);
    QCOMPARE(c2.line->y2, 22.0);
    QVERIFY(c2.ellipse);
    QCOMPARE(c2.ellipse->cx, 40.0);
    QCOMPARE(c2.ellipse->cy, 12.0);
    QVERIFY(near(c2.ellipse->rx, 25));
    QVERIFY(near(c2.ellipse->ry, 9));
    QCOMPARE(int(c2.dots.size()), 7);
    QVERIFY(near(c2.dots[1].x, 40 + 21 * -0.55));   // 28.45
    QVERIFY(near(c2.dots[1].y, 12 + 5.5 * -0.9));   // 7.05
    QVERIFY(near(c2.dots[6].x, 61));
    QVERIFY(near(c2.dots[6].y, 12.55));
    const auto &c1 = shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 1);   // sx 30, sy 7.5
    QVERIFY(near(c1.dots[0].x, 40 - 28.5));
    QVERIFY(near(c1.dots[4].y, 19.5));
    QVERIFY(near(c1.ellipse->rx, 34));
    QVERIFY(near(c1.ellipse->ry, 11));
    const auto &c5 = shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 5);   // sx 1.8, sy 0.6
    QVERIFY(near(c5.ellipse->rx, 5.8));
    QVERIFY(near(c5.ellipse->ry, 4.1));
    QVERIFY(near(c5.dots[3].x, 40.18));
    QVERIFY(near(c5.dots[3].y, 11.88));
}

void VariabilityGeometryTest::overlayNumbers()
{
    // §4.5 Icon: baseline y 21.5, x 1–23; 3 traces at x = 1…23 step 0.5; y = 21.5 − h·exp(−((x − c)/3.8)²).
    const auto &i1 = shapeOf(MarkVariant::Overlay, MarkForm::Icon, 1);   // sp 3.2, hv 0.3
    QVERIFY(i1.line);
    QCOMPARE(i1.line->x1, 1.0);
    QCOMPARE(i1.line->x2, 23.0);
    QCOMPARE(i1.line->y1, 21.5);
    QCOMPARE(i1.line->y2, 21.5);
    QCOMPARE(int(i1.traces.size()), 3);
    for (const auto &t : i1.traces) {
        QCOMPARE(int(t.points.size()), 45);
        QCOMPARE(t.points.front().x(), 1.0);
        QCOMPARE(t.points.back().x(), 23.0);
    }
    QVERIFY(near(i1.traces[0].centre, 8.8));    // 12 + 3.2·−1
    QVERIFY(near(i1.traces[0].height, 10.5));   // 15·(1 + 0.3·−1)
    QVERIFY(near(i1.traces[1].centre, 12.64));
    QVERIFY(near(i1.traces[1].height, 19.5));
    QVERIFY(near(i1.traces[2].centre, 15.2));
    QVERIFY(near(i1.traces[2].height, 15.45));
    QVERIFY(near(i1.traces[0].points[16].x(), 9.0));
    QVERIFY(near(i1.traces[0].points[16].y(), traceY(9.0, 8.8, 10.5, 3.8, 21.5)));   // 11.0290
    QVERIFY(near(i1.traces[0].points[16].y(), 11.02904562460609, 1e-9));
    for (const auto &t : i1.traces)
        for (const auto &p : t.points)
            QVERIFY(near(p.y(), traceY(p.x(), t.centre, t.height, 3.8, 21.5)));

    // §4.5 Cell: baseline x 2–78; 5 traces at x = 2…78 step 1, with the 1.5·exp(−((x − 14)/8)²) bump.
    const auto &c1 = shapeOf(MarkVariant::Overlay, MarkForm::Cell, 1);   // tsp 9, thv 0.3
    QVERIFY(c1.line);
    QCOMPARE(c1.line->x1, 2.0);
    QCOMPARE(c1.line->x2, 78.0);
    QCOMPARE(c1.line->y1, 21.5);
    QCOMPARE(int(c1.traces.size()), 5);
    for (const auto &t : c1.traces) {
        QCOMPARE(int(t.points.size()), 77);
        QCOMPARE(t.points.front().x(), 2.0);
        QCOMPARE(t.points.back().x(), 78.0);
    }
    QVERIFY(near(c1.traces[4].centre, 34.6));   // 40 + 9·−0.6
    QVERIFY(near(c1.traces[4].points[32].x(), 34));
    // R25: §4.5's trace 4 (h 17·(1 + 0.3·1) = 22.1) would peak at y ≈ −0.56, above the cell. It is scaled
    // about the baseline so its highest sample sits at y 0.65 (half the 1.3 px stroke), shape kept.
    {
        const auto &t4 = c1.traces[4];
        double top = 1e9, specRise = 0;
        for (const auto &p : t4.points) {
            top = std::min(top, p.y());
            specRise = std::max(specRise, 21.5 - traceY(p.x(), t4.centre, 22.1, 9, 21.5)
                                              + 1.5 * std::exp(-std::pow((p.x() - 14) / 8, 2)));
        }
        QVERIFY(near(top, 0.65, 1e-9));
        const double k = (21.5 - 0.65) / specRise;
        QVERIFY(k < 1 && k > 0.9);
        QVERIFY(near(t4.height, 22.1 * k, 1e-9));
        for (const auto &p : t4.points)
            QVERIFY(near(p.y(), 21.5 - k * (21.5 - traceY(p.x(), t4.centre, 22.1, 9, 21.5)
                                            + 1.5 * std::exp(-std::pow((p.x() - 14) / 8, 2))), 1e-9));
        QVERIFY(near(t4.points[32].y(), 21.5 - k * (21.5 + 0.5048914075806953), 1e-9));
    }
    // Every Overlay trace, both forms, every grade, stays inside its 24-unit box (R25).
    for (const auto f : kForms)
        for (int g = 0; g <= 5; ++g)
            for (const auto &t : shapeOf(MarkVariant::Overlay, f, g).traces)
                for (const auto &p : t.points)
                    QVERIFY2(p.y() >= 0.65 - 1e-9 && p.y() <= 24, qPrintable(QStringLiteral("grade %1 y %2").arg(g).arg(p.y())));
    // Grades 2 to 5 are untouched: grade 2's tallest trace (h 20.57) peaks at y 0.93.
    QVERIFY(near(shapeOf(MarkVariant::Overlay, MarkForm::Cell, 2).traces[4].height, 17 * (1 + 0.21)));
    const auto &c3 = shapeOf(MarkVariant::Overlay, MarkForm::Cell, 3);   // tsp 3.8, thv 0.13
    QVERIFY(near(c3.traces[1].centre, 40 + 3.8 * 0.55));
    QVERIFY(near(c3.traces[1].height, 17 * (1 + 0.13 * 0.6)));
    for (const auto &t : c3.traces)
        for (const auto &p : t.points)
            QVERIFY(near(p.y(), traceY(p.x(), t.centre, t.height, 9, 21.5) - 1.5 * std::exp(-std::pow((p.x() - 14) / 8, 2))));
}

void VariabilityGeometryTest::needleNumbers()
{
    // §4.6 Icon: gauge centre (12, 20) r 10, the upper half (2, 20) → (22, 20); sectors on (12, 20) r 11.
    const auto &i1 = shapeOf(MarkVariant::Needle, MarkForm::Icon, 1);   // a = 34°
    QVERIFY(i1.arc);
    QCOMPARE(i1.arc->cx, 12.0);
    QCOMPARE(i1.arc->cy, 20.0);
    QCOMPARE(i1.arc->r, 10.0);
    QCOMPARE(i1.arc->startAngle, 180.0);
    QCOMPARE(i1.arc->sweepAngle, 180.0);
    // PathAngleArc convention (clockwise, y down): start (2, 20), through (12, 10), end (22, 20).
    const auto at = [&](double deg) {
        const double t = deg * kPi / 180;
        return QPointF(i1.arc->cx + i1.arc->r * std::cos(t), i1.arc->cy + i1.arc->r * std::sin(t));
    };
    QVERIFY(near(at(180).x(), 2) && near(at(180).y(), 20));
    QVERIFY(near(at(270).x(), 12) && near(at(270).y(), 10));
    QVERIFY(near(at(360).x(), 22) && near(at(360).y(), 20, 1e-9));
    QCOMPARE(int(i1.wedges.size()), 3);
    QVERIFY(near(i1.wedges[0].spread, 34));
    QVERIFY(near(i1.wedges[1].spread, 34 * 0.62));
    QVERIFY(near(i1.wedges[2].spread, 34 * 0.3));
    const auto &w0 = i1.wedges[0].points;
    QVERIFY(near(w0.front().x(), 12) && near(w0.front().y(), 20));
    QVERIFY(near(w0.back().x(), 12) && near(w0.back().y(), 20));
    QVERIFY(near(w0[1].x(), 12 - 11 * std::sin(34 * kPi / 180)));   // 5.8489
    QVERIFY(near(w0[1].y(), 20 - 11 * std::cos(34 * kPi / 180)));   // 10.8806
    QVERIFY(near(w0[w0.size() - 2].x(), 12 + 11 * std::sin(34 * kPi / 180)));
    QVERIFY(near(w0[w0.size() - 2].y(), 10.880586701894542, 1e-9));
    QVERIFY(near(shapeOf(MarkVariant::Needle, MarkForm::Icon, 3).wedges[0].spread, 13));
    QVERIFY(near(shapeOf(MarkVariant::Needle, MarkForm::Icon, 4).wedges[1].spread, 6 * 0.62));
    QVERIFY(i1.needle);
    QCOMPARE(i1.needle->x1, 12.0);
    QCOMPARE(i1.needle->y1, 20.0);
    QCOMPARE(i1.needle->x2, 12.0);
    QCOMPARE(i1.needle->y2, 8.5);
    QVERIFY(i1.pivot);
    QCOMPARE(i1.pivot->x, 12.0);
    QCOMPARE(i1.pivot->y, 20.0);

    // §4.6 Cell: ruler y 5, x 4–76; ticks every 8 from 8 to 72, majors y 2–5, minors y 3.5–5; triangles
    // apex (40, 21), base (40 ± w, 5); needle x 40, y 4–21; pivot (40, 21) r 2.
    const auto &c2 = shapeOf(MarkVariant::Needle, MarkForm::Cell, 2);   // w = 15
    QVERIFY(c2.line);
    QCOMPARE(c2.line->x1, 4.0);
    QCOMPARE(c2.line->x2, 76.0);
    QCOMPARE(c2.line->y1, 5.0);
    QCOMPARE(c2.line->y2, 5.0);
    QCOMPARE(int(c2.ticks.size()), 9);
    for (int i = 0; i < 9; ++i) {
        const auto &t = c2.ticks[std::size_t(i)];
        const double x = 8 + 8 * i;
        QCOMPARE(t.x1, x);
        QCOMPARE(t.x2, x);
        QCOMPARE(t.y2, 5.0);
        const bool major = (i % 2) == 0;   // 8, 24, 40, 56, 72
        QCOMPARE(t.y1, major ? 2.0 : 3.5);
    }
    QCOMPARE(int(c2.wedges.size()), 3);
    const std::vector<QPointF> tri = {{40, 21}, {40 - 9.3, 5}, {40 + 9.3, 5}, {40, 21}};   // 0.62·15
    QCOMPARE(c2.wedges[1].points.size(), tri.size());
    for (std::size_t i = 0; i < tri.size(); ++i)
        QVERIFY(near(c2.wedges[1].points[i].x(), tri[i].x()) && near(c2.wedges[1].points[i].y(), tri[i].y()));
    QVERIFY(near(c2.wedges[0].spread, 15));
    QVERIFY(near(c2.wedges[2].spread, 4.5));
    QVERIFY(near(shapeOf(MarkVariant::Needle, MarkForm::Cell, 1).wedges[0].spread, 24));
    QVERIFY(near(shapeOf(MarkVariant::Needle, MarkForm::Cell, 4).wedges[0].spread, 4));
    QVERIFY(c2.needle);
    QCOMPARE(c2.needle->x1, 40.0);
    QCOMPARE(c2.needle->y1, 4.0);
    QCOMPARE(c2.needle->x2, 40.0);
    QCOMPARE(c2.needle->y2, 21.0);
    QVERIFY(c2.pivot);
    QCOMPARE(c2.pivot->x, 40.0);
    QCOMPARE(c2.pivot->y, 21.0);
    QCOMPARE(c2.pivot->r, 2.0);
}

void VariabilityGeometryTest::sectorIsSampledFinely()
{
    for (int g = 1; g <= 5; ++g) {
        for (const auto &w : shapeOf(MarkVariant::Needle, MarkForm::Icon, g).wedges) {
            const auto &p = w.points;
            QVERIFY(p.size() >= 4);
            // Arc points (all but the pivot at each end): on radius 11, chords ≤ 1 unit, from 270° − half
            // to 270° + half.
            for (std::size_t i = 1; i + 1 < p.size(); ++i)
                QVERIFY(near(std::hypot(p[i].x() - 12, p[i].y() - 20), 11, 1e-9));
            for (std::size_t i = 1; i + 2 < p.size(); ++i)
                QVERIFY(std::hypot(p[i + 1].x() - p[i].x(), p[i + 1].y() - p[i].y()) <= 1.0 + kEps);
            const double first = std::atan2(p[1].y() - 20, p[1].x() - 12) * 180 / kPi + 360;
            const double last  = std::atan2(p[p.size() - 2].y() - 20, p[p.size() - 2].x() - 12) * 180 / kPi + 360;
            QVERIFY2(near(first, 270 - w.spread, 1e-9), qPrintable(QString::number(first)));
            QVERIFY2(near(last, 270 + w.spread, 1e-9), qPrintable(QString::number(last)));
        }
    }
    // a = 34°: arc length 11·68°·π/180 = 13.05 units → 14 chords → pivot + 15 arc points + pivot.
    QCOMPARE(int(shapeOf(MarkVariant::Needle, MarkForm::Icon, 1).wedges[0].points.size()), 17);
}

void VariabilityGeometryTest::gradeFiveConverges()
{
    // Dispersion Icon: the three dots coincide on the centre.
    const auto &di = shapeOf(MarkVariant::Dispersion, MarkForm::Icon, 5);
    QCOMPARE(int(di.dots.size()), 3);
    for (const auto &d : di.dots) {
        QVERIFY(near(d.x, 12));
        QVERIFY(near(d.y, 12));
    }

    // Overlay Icon: sp = hv = 0, so the three traces are the same trace.
    const auto &oi = shapeOf(MarkVariant::Overlay, MarkForm::Icon, 5);
    QCOMPARE(int(oi.traces.size()), 3);
    QCOMPARE(oi.traces[0].points, oi.traces[1].points);
    QCOMPARE(oi.traces[0].points, oi.traces[2].points);
    QCOMPARE(pointwiseTraceSpread(oi), 0.0);

    // Overlay Cell: near-identical, with exactly the spec's residuals: tsp 0.4 over tshift −1…1 = 0.8 of
    // centre, thv 0.015 over th −1…1 = 17·0.03 = 0.51 of height; a small fraction of grade 1's spread.
    const auto &oc = shapeOf(MarkVariant::Overlay, MarkForm::Cell, 5);
    QVERIFY(near(centreSpread(oc), 0.8));
    QVERIFY(near(heightSpread(oc), 0.51));
    const double spread5 = pointwiseTraceSpread(oc), spread1 = pointwiseTraceSpread(shapeOf(MarkVariant::Overlay, MarkForm::Cell, 1));
    QVERIFY2(spread5 < 0.1 * spread1, qPrintable(QStringLiteral("%1 vs %2").arg(spread5).arg(spread1)));

    // Needle: the wedges are zero-width (a = 0, w = 0) and enclose nothing; only the needle remains.
    for (const auto f : kForms) {
        const auto &n = shapeOf(MarkVariant::Needle, f, 5);
        QCOMPARE(int(n.wedges.size()), 3);
        for (const auto &w : n.wedges) {
            QCOMPARE(w.spread, 0.0);
            QVERIFY(near(area(w.points), 0));
        }
        QVERIFY(n.needle);
        QVERIFY(n.pivot);
    }
}

void VariabilityGeometryTest::spreadNeverIncreasesWithGrade()
{
    for (int g = 1; g < 5; ++g) {
        const int h = g + 1;
        const QByteArray at = QByteArray::number(g) + "→" + QByteArray::number(h);

        // Dispersion Icon: dot distance from the centre.
        QVERIFY2(maxDotDistance(shapeOf(MarkVariant::Dispersion, MarkForm::Icon, h), 12, 12)
                     <= maxDotDistance(shapeOf(MarkVariant::Dispersion, MarkForm::Icon, g), 12, 12), at);

        // Dispersion Cell: dot spread in x and y, ellipse rx and ry.
        const auto &a = shapeOf(MarkVariant::Dispersion, MarkForm::Cell, g);
        const auto &b = shapeOf(MarkVariant::Dispersion, MarkForm::Cell, h);
        QVERIFY2(dotSpanX(b) <= dotSpanX(a), at);
        QVERIFY2(dotSpanY(b) <= dotSpanY(a), at);
        QVERIFY2(b.ellipse->rx <= a.ellipse->rx, at);
        QVERIFY2(b.ellipse->ry <= a.ellipse->ry, at);

        // Overlay, both forms: centre spread, height variation, and the pointwise spread of the points.
        for (const auto f : kForms) {
            const auto &p = shapeOf(MarkVariant::Overlay, f, g);
            const auto &q = shapeOf(MarkVariant::Overlay, f, h);
            QVERIFY2(centreSpread(q) <= centreSpread(p), at);
            QVERIFY2(heightSpread(q) <= heightSpread(p), at);
            QVERIFY2(pointwiseTraceSpread(q) <= pointwiseTraceSpread(p), at);
        }

        // Needle, both forms: each wedge's half-angle or half-width, and its area.
        for (const auto f : kForms) {
            const auto &p = shapeOf(MarkVariant::Needle, f, g);
            const auto &q = shapeOf(MarkVariant::Needle, f, h);
            for (std::size_t i = 0; i < 3; ++i) {
                QVERIFY2(q.wedges[i].spread <= p.wedges[i].spread, at);
                QVERIFY2(area(q.wedges[i].points) <= area(p.wedges[i].points) + kEps, at);
            }
        }
    }
    // Within one grade the three wedges nest, widest first (the paint order).
    for (const auto f : kForms) {
        const auto &n = shapeOf(MarkVariant::Needle, f, 2);
        QVERIFY(n.wedges[0].spread > n.wedges[1].spread);
        QVERIFY(n.wedges[1].spread > n.wedges[2].spread);
    }
}

void VariabilityGeometryTest::gradeZeroIsTheNeutralElementOnly()
{
    const auto noTone = [](const VariabilityShape &s) {
        return !s.ellipse && s.wedges.empty() && s.traces.empty() && !s.needle && s.dots.empty() && !s.pivot;
    };
    const auto &di = shapeOf(MarkVariant::Dispersion, MarkForm::Icon, 0);
    QVERIFY(noTone(di));
    QVERIFY(di.ring && !di.arc && !di.line && di.ticks.empty());

    const auto &dc = shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 0);
    QVERIFY(noTone(dc));
    QVERIFY(!dc.ring && !dc.arc && dc.line && dc.ticks.empty());
    QCOMPARE(dc.line->role, GeometryRole::NeutralMid);   // the target line is colorBorderMid

    for (const auto f : kForms) {
        const auto &o = shapeOf(MarkVariant::Overlay, f, 0);
        QVERIFY(noTone(o));
        QVERIFY(!o.ring && !o.arc && o.line && o.ticks.empty());
        QCOMPARE(o.line->role, GeometryRole::NeutralStrong);
    }

    const auto &ni = shapeOf(MarkVariant::Needle, MarkForm::Icon, 0);
    QVERIFY(noTone(ni));
    QVERIFY(!ni.ring && ni.arc && !ni.line && ni.ticks.empty());

    const auto &nc = shapeOf(MarkVariant::Needle, MarkForm::Cell, 0);
    QVERIFY(noTone(nc));
    QVERIFY(!nc.ring && !nc.arc && nc.line && int(nc.ticks.size()) == 9);
    QCOMPARE(nc.line->role, GeometryRole::NeutralMid);   // the ruler
    for (const auto &t : nc.ticks)
        QCOMPARE(t.role, GeometryRole::NeutralStrong);
}

void VariabilityGeometryTest::neutralElementsDoNotMoveWithGrade()
{
    for (const auto v : kGraded) {
        for (const auto f : kForms) {
            const QJsonObject zero = toJson(shapeOf(v, f, 0));
            for (int g = 1; g <= 5; ++g) {
                const QJsonObject j = toJson(shapeOf(v, f, g));
                for (const char *key : {"ring", "arc", "line", "ticks"})
                    QVERIFY2(j.value(QLatin1String(key)) == zero.value(QLatin1String(key)), name(v, f));
            }
            // Neutral roles on the neutral elements, the tone on everything else.
            const auto &s = shapeOf(v, f, 3);
            if (s.ring)  QVERIFY(s.ring->role != GeometryRole::Tone);
            if (s.arc)   QVERIFY(s.arc->role != GeometryRole::Tone);
            if (s.line)  QVERIFY(s.line->role != GeometryRole::Tone);
            for (const auto &t : s.ticks)  QVERIFY(t.role != GeometryRole::Tone);
            if (s.ellipse) QCOMPARE(s.ellipse->role, GeometryRole::Tone);
            for (const auto &w : s.wedges) QCOMPARE(w.role, GeometryRole::Tone);
            for (const auto &t : s.traces) QCOMPARE(t.role, GeometryRole::Tone);
            for (const auto &d : s.dots)   QCOMPARE(d.role, GeometryRole::Tone);
            if (s.needle) QCOMPARE(s.needle->role, GeometryRole::Tone);
            if (s.pivot)  QCOMPARE(s.pivot->role, GeometryRole::Tone);
        }
    }
}

void VariabilityGeometryTest::clamping()
{
    QCOMPARE(clampGrade(-1), 0);
    QCOMPARE(clampGrade(7), 5);
    QCOMPARE(clampGrade(3), 3);
    const auto geo = makeCanonicalVariabilityGeometry();
    for (const auto v : kGraded) {
        for (const auto f : kForms) {
            QCOMPARE(&geo->shape(v, f, -1), &geo->shape(v, f, 0));
            QCOMPARE(&geo->shape(v, f, 7), &geo->shape(v, f, 5));
            QCOMPARE(geo->shape(v, f, -1).grade, 0);
            QCOMPARE(geo->shape(v, f, 7).grade, 5);
        }
    }
    // An unknown variant or form is an empty shape, not a crash or a guess.
    QVERIFY(geo->shape(MarkVariant(9), MarkForm::Icon, 3).isEmpty());
    QVERIFY(geo->shape(MarkVariant::Dispersion, MarkForm(2), 3).isEmpty());
}

void VariabilityGeometryTest::strokesAndSmallSizes()
{
    // Every neutral element: 1 px at every size (§4.4 – §4.6).
    for (const auto v : kGraded) {
        for (const auto f : kForms) {
            const auto &s = shapeOf(v, f, 2);
            std::vector<std::pair<double, double>> widths;
            if (s.ring) widths.push_back({s.ring->width, s.ring->widthSmall});
            if (s.arc)  widths.push_back({s.arc->width, s.arc->widthSmall});
            if (s.line) widths.push_back({s.line->width, s.line->widthSmall});
            for (const auto &t : s.ticks) widths.push_back({t.width, t.widthSmall});
            QVERIFY(!widths.empty());
            for (const auto &[w, ws] : widths) {
                QCOMPARE(w, 1.0);
                QCOMPARE(ws, 1.0);
            }
        }
    }

    // Dispersion: dots 2.6 (3.0 at size ≤ 12) in the Icon, 2.2 in the Cell; tone at 85 %; ellipse 12 %.
    for (const auto &d : shapeOf(MarkVariant::Dispersion, MarkForm::Icon, 3).dots) {
        QCOMPARE(d.r, 2.6);
        QCOMPARE(d.rSmall, 3.0);
        QCOMPARE(d.smallMaxSize, 12.0);
        QCOMPARE(d.alpha, 0.85);
    }
    for (const auto &d : shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 3).dots) {
        QCOMPARE(d.r, 2.2);
        QCOMPARE(d.rSmall, 2.2);
        QCOMPARE(d.alpha, 0.85);
    }
    QCOMPARE(shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 3).ellipse->alpha, 0.12);

    // Overlay: Icon 1.5 px (1.25 at size ≤ 16), tone 70 %; Cell 1.3 px, tone 60 %; round cap and join.
    for (const auto &t : shapeOf(MarkVariant::Overlay, MarkForm::Icon, 3).traces) {
        QCOMPARE(t.width, 1.5);
        QCOMPARE(t.widthSmall, 1.25);
        QCOMPARE(t.smallMaxSize, 16.0);
        QCOMPARE(t.alpha, 0.70);
        QVERIFY(t.roundCap);
    }
    for (const auto &t : shapeOf(MarkVariant::Overlay, MarkForm::Cell, 3).traces) {
        QCOMPARE(t.width, 1.3);
        QCOMPARE(t.widthSmall, 1.3);
        QCOMPARE(t.alpha, 0.60);
        QVERIFY(t.roundCap);
    }

    // Needle Icon: wedges 12 / 16 / 24 % (14 / 18 / 26 % at size ≤ 16); needle 1.5 px (1.25 at ≤ 16),
    // round cap, full tone; pivot r 1.8 (2.2 at ≤ 16).
    const auto &ni = shapeOf(MarkVariant::Needle, MarkForm::Icon, 2);
    const double ia[] = {0.12, 0.16, 0.24}, ias[] = {0.14, 0.18, 0.26};
    for (std::size_t i = 0; i < 3; ++i) {
        QCOMPARE(ni.wedges[i].alpha, ia[i]);
        QCOMPARE(ni.wedges[i].alphaSmall, ias[i]);
        QCOMPARE(ni.wedges[i].smallMaxSize, 16.0);
    }
    QCOMPARE(ni.needle->width, 1.5);
    QCOMPARE(ni.needle->widthSmall, 1.25);
    QCOMPARE(ni.needle->smallMaxSize, 16.0);
    QCOMPARE(ni.needle->alpha, 1.0);
    QVERIFY(ni.needle->roundCap);
    QCOMPARE(ni.pivot->r, 1.8);
    QCOMPARE(ni.pivot->rSmall, 2.2);
    QCOMPARE(ni.pivot->smallMaxSize, 16.0);
    QCOMPARE(ni.pivot->alpha, 1.0);

    // Needle Cell: wedges 12 / 16 / 24 % with no small variant; needle 1.5 px; pivot r 2.
    const auto &nc = shapeOf(MarkVariant::Needle, MarkForm::Cell, 2);
    for (std::size_t i = 0; i < 3; ++i) {
        QCOMPARE(nc.wedges[i].alpha, ia[i]);
        QCOMPARE(nc.wedges[i].alphaSmall, ia[i]);
    }
    QCOMPARE(nc.needle->width, 1.5);
    QCOMPARE(nc.needle->widthSmall, 1.5);
    QCOMPARE(nc.pivot->r, 2.0);
    QCOMPARE(nc.pivot->rSmall, 2.0);
}

void VariabilityGeometryTest::typicalityIsDataOnly()
{
    for (const auto v : {MarkVariant::Typicality, MarkVariant::TypicalityTrace})
        for (const auto f : kForms)
            for (int g = 0; g <= 5; ++g)
                QVERIFY(shapeOf(v, f, g).isEmpty());
}

void VariabilityGeometryTest::jsonMirrorsTheShape()
{
    const auto &s = shapeOf(MarkVariant::Overlay, MarkForm::Cell, 2);
    const QJsonObject j = toJson(s);
    QCOMPARE(j.value("variant").toInt(), 1);
    QCOMPARE(j.value("form").toInt(), 1);
    QCOMPARE(j.value("grade").toInt(), 2);
    QVERIFY(j.value("ring").isNull());
    QVERIFY(j.value("ellipse").isNull());
    QVERIFY(j.value("needle").isNull());
    QVERIFY(j.value("pivot").isNull());
    QCOMPARE(j.value("dots").toArray().size(), 0);
    QCOMPARE(j.value("line").toObject().value("role").toString(), QStringLiteral("neutralStrong"));
    const QJsonArray traces = j.value("traces").toArray();
    QCOMPARE(traces.size(), 5);
    const QJsonObject t2 = traces.at(2).toObject();
    QCOMPARE(t2.value("role").toString(), QStringLiteral("tone"));
    QCOMPARE(t2.value("width").toDouble(), 1.3);
    const QJsonArray pts = t2.value("points").toArray();
    QCOMPARE(pts.size(), 2 * 77);   // flat: x0, y0, x1, y1, …
    QCOMPARE(pts.at(10).toDouble(), s.traces[2].points[5].x());
    QCOMPARE(pts.at(11).toDouble(), s.traces[2].points[5].y());

    const QJsonObject d = toJson(shapeOf(MarkVariant::Dispersion, MarkForm::Cell, 1));
    QCOMPARE(d.value("line").toObject().value("role").toString(), QStringLiteral("neutralMid"));
    QCOMPARE(d.value("dots").toArray().at(0).toObject().value("r").toDouble(), 2.2);
    QCOMPARE(d.value("ellipse").toObject().value("rx").toDouble(), 34.0);
}

void VariabilityGeometryTest::singletonCachesOneObjectPerKey()
{
    QJSEngine engine;
    {
        VariabilityGeometryQml geo(&engine);
        const QJSValue a = geo.shape(1, 1, 3);
        const QJSValue b = geo.shape(1, 1, 3);
        QVERIFY(a.isObject());
        QVERIFY(a.strictlyEquals(b));                                    // the SAME object, not a copy
        QVERIFY(!a.strictlyEquals(geo.shape(1, 1, 4)));
        QVERIFY(geo.shape(0, 0, -1).strictlyEquals(geo.shape(0, 0, 0)));  // clamped before the cache
        QVERIFY(geo.shape(2, 1, 7).strictlyEquals(geo.shape(2, 1, 5)));

        // A plain JS object: arrays are arrays, points are numbers, absent elements are null.
        QVERIFY(a.property("ring").isNull());
        const QJSValue traces = a.property("traces");
        QVERIFY(traces.isArray());
        QCOMPARE(traces.property("length").toInt(), 5);
        const QJSValue pts = traces.property(0).property("points");
        QVERIFY(pts.isArray());
        QCOMPARE(pts.property("length").toInt(), 154);
        const auto &s = shapeOf(MarkVariant::Overlay, MarkForm::Cell, 3);
        QCOMPARE(pts.property(3).toNumber(), s.traces[0].points[1].y());
        QVERIFY(pts.property(3).isNumber());

        // Unknown variants and forms share one empty object, so they cannot grow the cache.
        const QJSValue u = geo.shape(42, 0, 1);
        QVERIFY(u.strictlyEquals(geo.shape(0, 9, 3)));
        QCOMPARE(u.property("variant").toInt(), -1);
        QVERIFY(u.property("line").isNull());
        QCOMPARE(u.property("dots").property("length").toInt(), 0);

        // No engine, no object: undefined, never a copy.
        VariabilityGeometryQml orphan(nullptr);
        QVERIFY(orphan.shape(0, 0, 1).isUndefined());
    }
}

void VariabilityGeometryTest::tracePathFeedsAPathPolyline()
{
    // The cheap route the header documents: tracePath() hands PathPolyline a QPolygonF, scaled by k in
    // C++. Checked against a real PathPolyline, beside the flat-array route the mark can also take.
    qmlRegisterSingletonType<VariabilityGeometryQml>("VariabilityGeometryTest", 1, 0, "VariabilityGeometry",
                                                     [](QQmlEngine *q, QJSEngine *j) -> QObject * {
                                                         return VariabilityGeometryQml::create(q, j);
                                                     });
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import VariabilityGeometryTest
        Item {
            readonly property real k: 2.5
            property PathPolyline viaPath: PathPolyline { path: VariabilityGeometry.tracePath(1, 1, 3, 2, k) }
            property PathPolyline viaFlat: PathPolyline {
                path: {
                    const p = VariabilityGeometry.shape(1, 1, 3).traces[2].points, out = []
                    for (let i = 0; i < p.length; i += 2)
                        out.push(Qt.point(p[i] * k, p[i + 1] * k))
                    return out
                }
            }
            property PathPolyline wedge: PathPolyline { path: VariabilityGeometry.wedgePath(2, 0, 1, 0, k) }
            property bool shared: VariabilityGeometry.shape(1, 1, 3) === VariabilityGeometry.shape(1, 1, 3)
        }
    )", QUrl());
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QVERIFY(root->property("shared").toBool());

    const auto pointsOf = [&](const char *prop) {
        auto *poly = root->property(prop).value<QObject *>();
        const QVariant path = poly ? poly->property("path") : QVariant();
        return path.value<QList<QPointF>>();
    };
    const auto &t = shapeOf(MarkVariant::Overlay, MarkForm::Cell, 3).traces[2].points;
    const QList<QPointF> viaPath = pointsOf("viaPath"), viaFlat = pointsOf("viaFlat");
    QCOMPARE(viaPath.size(), qsizetype(t.size()));
    QCOMPARE(viaFlat.size(), qsizetype(t.size()));
    for (std::size_t i = 0; i < t.size(); ++i) {
        QVERIFY(near(viaPath[qsizetype(i)].x(), t[i].x() * 2.5));
        QVERIFY(near(viaPath[qsizetype(i)].y(), t[i].y() * 2.5));
        QVERIFY(near(viaFlat[qsizetype(i)].x(), viaPath[qsizetype(i)].x()));
        QVERIFY(near(viaFlat[qsizetype(i)].y(), viaPath[qsizetype(i)].y()));
    }
    const auto &w = shapeOf(MarkVariant::Needle, MarkForm::Icon, 1).wedges[0].points;
    const QList<QPointF> wedge = pointsOf("wedge");
    QCOMPARE(wedge.size(), qsizetype(w.size()));
    QVERIFY(near(wedge.front().x(), 12 * 2.5) && near(wedge.front().y(), 20 * 2.5));
}

QTEST_MAIN(VariabilityGeometryTest)
#include "variability_geometry_test.moc"
