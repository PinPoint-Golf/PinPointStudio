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

// The per-instance variability geometry (DataVariabilityGeometry in src/Gui/variability/
// variability_geometry.h): the §6.1 spread funnel and the §6.2 typicality variants of
// docs/design/variability-mark/spec.md. Expected numbers are worked by hand from the spec's formulas
// (half-width = clamp(ρ/1.25, 0.08, 1) × (H/2 − 2); x = 40 + 14z; …), not read back from the
// provider's constants. The QML singleton's per-instance calls are checked for plain JS objects, and
// polygon() for feeding a real PathPolyline.

#include "variability/variability_geometry.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

using namespace pinpoint::variability;

namespace {

constexpr double kEps = 1e-9;
constexpr double kPi  = 3.14159265358979323846;   // not M_PI: MSVC hides it without _USE_MATH_DEFINES
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool near(double a, double b, double eps = kEps) { return std::abs(a - b) <= eps; }

std::vector<VariabilityFunnelSession> sessionsOf(const std::vector<double> &rhos)
{
    std::vector<VariabilityFunnelSession> s;
    for (const double r : rhos)
        s.push_back({r, 3, false});
    return s;
}

// The band's extent in x.
std::pair<double, double> spanX(const VariabilityFunnelBand &b)
{
    const auto [lo, hi] = std::minmax_element(b.points.begin(), b.points.end(),
                                              [](const QPointF &p, const QPointF &q) { return p.x() < q.x(); });
    return {lo->x(), hi->x()};
}

// The band's half-width at x (its upper edge), read off the polygon: the upper edge is the first half.
double upperYAt(const VariabilityFunnelBand &b, double x)
{
    for (const QPointF &p : b.points)
        if (near(p.x(), x, 1e-6))
            return p.y();   // the first hit is on the upper edge (it is traversed first)
    return kNaN;
}

// The same list three ways: as a JS Array, as the value a C++ QVariantList becomes (directly and wrapped
// in a QVariant), and as an array-like object with a numeric length.
struct ListForms {
    QJSValue array, variantList, wrappedVariant, arrayLike;
};

ListForms listForms(QJSEngine &engine, const QVariantList &items)
{
    ListForms f;
    f.array = engine.newArray(quint32(items.size()));
    f.arrayLike = engine.newObject();
    for (qsizetype i = 0; i < items.size(); ++i) {
        f.array.setProperty(quint32(i), engine.toScriptValue(items[i]));
        f.arrayLike.setProperty(quint32(i), engine.toScriptValue(items[i]));
    }
    f.arrayLike.setProperty(QStringLiteral("length"), int(items.size()));
    f.variantList = engine.toScriptValue(items);
    f.wrappedVariant = engine.toScriptValue(QVariant::fromValue(items));
    return f;
}

QString stringify(QJSEngine &engine, const QJSValue &v)
{
    return engine.globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("stringify"))
        .call({v}).toString();
}

std::vector<double> grid(const std::function<double(int)> &f)
{
    std::vector<double> v(101);
    for (int i = 0; i <= 100; ++i)
        v[std::size_t(i)] = f(i);
    return v;
}

} // namespace

class VariabilityDataGeometryTest : public QObject
{
    Q_OBJECT

private slots:
    void funnelHalfWidthIsMonotonicAndClamped();
    void funnelLayoutAndRoles();
    void funnelBreaksSplitTheBand();
    void funnelSkippedSessionCarriesItsBreak();
    void funnelFewerThanThreeInTheLatestRun();
    void funnelKeepsTheLatestTwelve();
    void funnelMinimumWidth();
    void ringFollowsZAndClampsInTheCell();
    void ringClampsAtTwoPointTwoInTheIcon();
    void zonesAreTheSpecs();
    void ghostJitterIsDeterministicAndBounded();
    void sequentialIdsSpread();
    void ghostDotsPlaceAndCap();
    void corridorBracketMapsLoAndHi();
    void envelopeIsMeanPlusMinusSd();
    void traceScaleHandlesNegativeValues();
    void traceRejectsBrokenCurves();
    void providerAnswersTheInterface();
    void singletonBuildsPlainObjects();
    void polygonFeedsAPathPolyline();
    void funnelAcceptsAnyArrayLike();
    void typicalityAcceptsAnyArrayLike();
    void typicalityTraceAcceptsAnyArrayLike();
    void qmlVarListsFromCppDraw();
};

// ── §6.1 the funnel ────────────────────────────────────────────────────────────────────────────────

void VariabilityDataGeometryTest::funnelHalfWidthIsMonotonicAndClamped()
{
    // H 32: the full half-width is H/2 − 2 = 14 px.
    const std::vector<double> rhos = {0, 0.05, 0.1, 0.3, 0.6, 1.0, 1.25, 1.5, 3.0};
    const auto f = DataVariabilityGeometry::funnel(sessionsOf(rhos), 200, 32);
    QCOMPARE(int(f.sessions.size()), 9);
    const double expected[] = {0.08 * 14, 0.08 * 14, 0.08 * 14, 0.24 * 14, 0.48 * 14, 0.8 * 14, 14, 14, 14};
    for (std::size_t i = 0; i < rhos.size(); ++i)
        QVERIFY2(near(f.sessions[i].halfWidth, expected[i]), qPrintable(QString::number(rhos[i])));
    for (std::size_t i = 1; i < rhos.size(); ++i)
        QVERIFY(f.sessions[i].halfWidth >= f.sessions[i - 1].halfWidth);

    // The band polygon carries the same half-widths about the centre line (y 16).
    QCOMPARE(int(f.bands.size()), 1);
    const auto &b = f.bands[0];
    QCOMPARE(int(b.points.size()), 2 * 9 + 1);   // upper edge, lower edge back, closed
    QCOMPARE(b.points.front(), b.points.back());
    for (std::size_t i = 0; i < 9; ++i) {
        QVERIFY(near(b.points[i].x(), f.sessions[i].x));
        QVERIFY(near(b.points[i].y(), 16 - expected[i]));
        QVERIFY(near(b.points[17 - i].y(), 16 + expected[i]));   // the lower edge, traversed back
    }

    // Movement within a grade still moves the band (§6.1: driven by ρ, not the grade).
    const auto g = DataVariabilityGeometry::funnel(sessionsOf({0.40, 0.45, 0.48}), 120, 32);
    QVERIFY(g.sessions[0].halfWidth < g.sessions[1].halfWidth);
    QVERIFY(g.sessions[1].halfWidth < g.sessions[2].halfWidth);
}

void VariabilityDataGeometryTest::funnelLayoutAndRoles()
{
    // 5 sessions over W 120: x = 6, 33, 60, 87, 114.
    auto in = sessionsOf({0.5, 0.5, 0.5, 0.5, 0.5});
    in.back().grade = 4;
    const auto f = DataVariabilityGeometry::funnel(in, 120, 32);
    QCOMPARE(f.width, 120.0);
    QCOMPARE(f.height, 32.0);
    QCOMPARE(f.currentGrade, 4);
    QVERIFY(f.currentHasReading);
    QVERIFY(!f.trendAfter3);
    QCOMPARE(f.latestRun, 5);
    const double xs[] = {6, 33, 60, 87, 114};
    for (std::size_t i = 0; i < 5; ++i) {
        QVERIFY(near(f.sessions[i].x, xs[i]));
        const auto &m = f.sessions[i].marker;
        QVERIFY(near(m.x, xs[i]) && near(m.y, 16));
        if (i < 4) {
            QVERIFY(!f.sessions[i].current);
            QCOMPARE(m.role, GeometryRole::Text2);
            QCOMPARE(m.r, 1.6);
        } else {
            QVERIFY(f.sessions[i].current);
            QCOMPARE(m.role, GeometryRole::Tone);
            QCOMPARE(m.r, 2.2);
        }
    }
    // Centre line: H/2, dashed, colorBorderStrong, 1 px, the full width.
    QCOMPARE(f.centre.role, GeometryRole::NeutralStrong);
    QVERIFY(f.centre.dashed);
    QCOMPARE(f.centre.width, 1.0);
    QVERIFY(near(f.centre.y1, 16) && near(f.centre.y2, 16));
    QVERIFY(near(f.centre.x1, 0) && near(f.centre.x2, 120));
    // Band: the current tone at 16 %, its edge 1 px at 55 %.
    const auto &b = f.bands[0];
    QCOMPARE(b.fillRole, GeometryRole::Tone);
    QCOMPARE(b.fillAlpha, 0.16);
    QCOMPARE(b.edgeRole, GeometryRole::Tone);
    QCOMPARE(b.edgeAlpha, 0.55);
    QCOMPARE(b.edgeWidth, 1.0);
    QCOMPARE(geometryRoleToken(GeometryRole::Text2), QStringLiteral("text2"));

    // A current session with no reading: not drawn, no current marker, its grade still the tone's.
    auto none = sessionsOf({0.5, 0.5, 0.5, kNaN});
    none.back().grade = 0;
    const auto n = DataVariabilityGeometry::funnel(none, 120, 32);
    QCOMPARE(int(n.sessions.size()), 3);
    QVERIFY(!n.currentHasReading);
    QCOMPARE(n.currentGrade, 0);
    for (const auto &s : n.sessions)
        QVERIFY(!s.current);

    // One session sits where the current one always does, W − 6.
    const auto one = DataVariabilityGeometry::funnel(sessionsOf({0.5}), 120, 32);
    QVERIFY(near(one.sessions[0].x, 114));
}

void VariabilityDataGeometryTest::funnelBreaksSplitTheBand()
{
    // 7 sessions over W 120: step 18, x = 6, 24, 42, 60, 78, 96, 114. A break before the 4th (x 60):
    // midway at 51, so the gap is 49 … 53 and the tick sits at 51.
    auto in = sessionsOf({0.4, 0.5, 0.6, 0.3, 0.3, 0.3, 0.3});
    in[3].breakBefore = true;
    const auto f = DataVariabilityGeometry::funnel(in, 120, 32);
    QVERIFY(!f.trendAfter3);
    QCOMPARE(f.latestRun, 4);
    QCOMPARE(int(f.bands.size()), 2);
    QCOMPARE(f.bands[0].first, 0);
    QCOMPARE(f.bands[0].last, 2);
    QCOMPARE(f.bands[1].first, 3);
    QCOMPARE(f.bands[1].last, 6);
    QCOMPARE(f.sessions[2].run, 0);
    QCOMPARE(f.sessions[3].run, 1);

    const auto [a0, a1] = spanX(f.bands[0]);
    const auto [b0, b1] = spanX(f.bands[1]);
    QVERIFY(near(a0, 6));
    QVERIFY(near(a1, 49));
    QVERIFY(near(b0, 53));
    QVERIFY(near(b1, 114));
    QVERIFY(near(b0 - a1, 4));   // the 4 px gap

    // Each band's stub into the gap holds its own session's half-width, never the other run's.
    QVERIFY(near(upperYAt(f.bands[0], 49), 16 - f.sessions[2].halfWidth));
    QVERIFY(near(upperYAt(f.bands[1], 53), 16 - f.sessions[3].halfWidth));
    QVERIFY(f.bands[1].points.front() == f.bands[1].points.back());

    QCOMPARE(int(f.breaks.size()), 1);
    const auto &br = f.breaks[0];
    QCOMPARE(br.before, 3);
    QVERIFY(near(br.gapX1, 49) && near(br.gapX2, 53));
    QVERIFY(near(br.tick.x1, 51) && near(br.tick.x2, 51));
    QVERIFY(br.tick.dashed);
    QCOMPARE(br.tick.role, GeometryRole::NeutralStrong);
    QCOMPARE(br.tick.width, 1.0);
    // Short: ± half of the 14 px the band can reach, about the centre line.
    QVERIFY(near(br.tick.y1, 16 - 7) && near(br.tick.y2, 16 + 7));

    // Sessions stay evenly spaced across the break.
    for (std::size_t i = 1; i < f.sessions.size(); ++i)
        QVERIFY(near(f.sessions[i].x - f.sessions[i - 1].x, 18));
}

void VariabilityDataGeometryTest::funnelSkippedSessionCarriesItsBreak()
{
    // The break sits on a session with no reading: it still breaks the run between its neighbours.
    auto in = sessionsOf({0.5, 0.5, 0.5, kNaN, 0.5, 0.5, 0.5});
    in[3].breakBefore = true;
    const auto f = DataVariabilityGeometry::funnel(in, 120, 32);
    QCOMPARE(int(f.sessions.size()), 6);
    QCOMPARE(f.sessions[3].input, 4);
    QCOMPARE(f.sessions[3].run, 1);
    QCOMPARE(int(f.bands.size()), 2);
    QCOMPARE(int(f.breaks.size()), 1);
    QCOMPARE(f.latestRun, 3);
}

void VariabilityDataGeometryTest::funnelFewerThanThreeInTheLatestRun()
{
    // Five then a break then two: the latest run has 2, so no band at all, not even the older run's.
    auto in = sessionsOf({0.5, 0.5, 0.5, 0.5, 0.5, 0.4, 0.4});
    in[5].breakBefore = true;
    const auto f = DataVariabilityGeometry::funnel(in, 120, 32);
    QVERIFY(f.trendAfter3);
    QCOMPARE(f.latestRun, 2);
    QVERIFY(f.bands.empty());
    QVERIFY(f.breaks.empty());
    QCOMPARE(int(f.sessions.size()), 7);   // the markers are still described

    // Three in the latest run: bands again.
    in.push_back({0.4, 3, false});
    const auto g = DataVariabilityGeometry::funnel(in, 120, 32);
    QVERIFY(!g.trendAfter3);
    QCOMPARE(g.latestRun, 3);
    QCOMPARE(int(g.bands.size()), 2);

    for (const auto &few : {std::vector<double>{}, std::vector<double>{0.5}, std::vector<double>{0.5, 0.5}}) {
        const auto h = DataVariabilityGeometry::funnel(sessionsOf(few), 120, 32);
        QVERIFY(h.trendAfter3);
        QVERIFY(h.bands.empty());
    }
}

void VariabilityDataGeometryTest::funnelKeepsTheLatestTwelve()
{
    // 14 sessions, a break before input 2: inputs 0 and 1 roll off, so input 2 is the first drawn and
    // its break (to sessions no longer shown) is dropped.
    std::vector<double> rhos;
    for (int i = 0; i < 14; ++i)
        rhos.push_back(0.1 * (i + 1));
    auto in = sessionsOf(rhos);
    in[2].breakBefore = true;
    const auto f = DataVariabilityGeometry::funnel(in, 200, 32);
    QCOMPARE(int(f.sessions.size()), 12);
    QCOMPARE(f.rolledOff, 2);
    QCOMPARE(f.sessions.front().input, 2);
    QCOMPARE(f.sessions.back().input, 13);
    QVERIFY(f.sessions.back().current);
    QVERIFY(near(f.sessions.front().x, 6));
    QVERIFY(near(f.sessions.back().x, 194));
    QCOMPARE(int(f.bands.size()), 1);
    QVERIFY(f.breaks.empty());
    QCOMPARE(f.latestRun, 12);
}

void VariabilityDataGeometryTest::funnelMinimumWidth()
{
    const auto f = DataVariabilityGeometry::funnel(sessionsOf({0.5, 0.5, 0.5}), 50, 32);
    QCOMPARE(f.width, kFunnelMinWidthPx);
    QCOMPARE(f.width, 96.0);
    QCOMPARE(f.requestedWidth, 50.0);
    QVERIFY(near(f.sessions.back().x, 90));
    QVERIFY(near(f.centre.x2, 96));
}

// ── §6.2 Your group ────────────────────────────────────────────────────────────────────────────────

void VariabilityDataGeometryTest::ringFollowsZAndClampsInTheCell()
{
    struct Case { double z, x; bool clamped; int side; };
    const Case cases[] = {
        {0, 40, false, 0},       {1, 54, false, 0},        {-2, 12, false, 0},
        {2.6, 76.4, false, 0},   {-2.6, 3.6, false, 0},    // the edge itself is not past it
        {2.61, 76, true, 1},     {-3, 4, true, -1},        {kInf, 76, true, 1},   {-kInf, 4, true, -1},
    };
    for (const auto &c : cases) {
        const auto t = DataVariabilityGeometry::typicality(MarkForm::Cell, c.z, {}, std::nullopt);
        const QByteArray at = QByteArray::number(c.z);
        QVERIFY2(t.hasSwing && t.ring, at);
        QVERIFY2(near(t.ring->cx, c.x), at);
        QVERIFY2(near(t.ring->cy, 12), at);
        QCOMPARE(t.clamped, c.clamped);
        QCOMPARE(t.side, c.side);
        // The ring: r 3, 1.75 px tone, colorSurface fill; the centre dot r 1.1 in the tone.
        QCOMPARE(t.ring->r, 3.0);
        QCOMPARE(t.ring->width, 1.75);
        QCOMPARE(t.ring->strokeRole, GeometryRole::Tone);
        QCOMPARE(t.ring->fillRole, GeometryRole::Surface);
        QVERIFY2(t.centre && near(t.centre->x, c.x) && near(t.centre->y, 12), at);
        QCOMPARE(t.centre->r, 1.1);
        QCOMPARE(t.centre->role, GeometryRole::Tone);
        QCOMPARE(bool(t.chevron), c.clamped);
        if (!t.chevron)
            continue;
        // The chevron points OUT of the cell and stays inside it, clear of the ring.
        const auto &p = t.chevron->points;
        QCOMPARE(int(p.size()), 3);
        const double tip = p[1].x(), arm = p[0].x();
        QVERIFY2(c.side < 0 ? tip < arm : tip > arm, at);
        QVERIFY(near(p[0].x(), p[2].x()));
        QVERIFY(near(p[1].y(), 12));
        for (const auto &q : p)
            QVERIFY(q.x() >= 0 && q.x() <= 80 && q.y() >= 0 && q.y() <= 24);
        QVERIFY2(std::abs(tip - t.ring->cx) > t.ring->r, at);
        QVERIFY(t.chevron->roundCap);
        QCOMPARE(t.chevron->role, GeometryRole::Tone);
    }
    // The chevrons as defined: ‹ at 11/9 on the left, › at 69/71 on the right, 4 units tall.
    const auto left = DataVariabilityGeometry::typicality(MarkForm::Cell, -5, {}, std::nullopt);
    QCOMPARE(left.chevron->points[0], QPointF(11, 10));
    QCOMPARE(left.chevron->points[1], QPointF(9, 12));
    QCOMPARE(left.chevron->points[2], QPointF(11, 14));
    const auto right = DataVariabilityGeometry::typicality(MarkForm::Cell, 5, {}, std::nullopt);
    QCOMPARE(right.chevron->points[0], QPointF(69, 10));
    QCOMPARE(right.chevron->points[1], QPointF(71, 12));
    QCOMPARE(right.chevron->points[2], QPointF(69, 14));
    // Both clear the outer zone (ellipse 40 ± 28, ± 9 about y 12): every chevron point lies outside it,
    // and clear of the clamped ring's stroke (ring r 3 + half of 1.75 px at k = 1).
    for (const auto *c : {&left, &right}) {
        for (const auto &q : c->chevron->points) {
            const double ex = (q.x() - 40) / 28, ey = (q.y() - 12) / 9;
            QVERIFY(ex * ex + ey * ey > 1);
            QVERIFY(q.x() >= 0 && q.x() <= 80);
        }
        QVERIFY(std::abs(c->chevron->points[1].x() - c->ring->cx) - 0.75 > c->ring->r + 0.875);
    }

    // No z, no swing: the zones only.
    const auto none = DataVariabilityGeometry::typicality(MarkForm::Cell, kNaN, {}, std::nullopt);
    QVERIFY(!none.hasSwing && !none.ring && !none.centre && !none.chevron);
}

void VariabilityDataGeometryTest::ringClampsAtTwoPointTwoInTheIcon()
{
    // x = 12 + clamp(z, −2.2, 2.2) × 5.25, y 12, r 2.6.
    struct Case { double z, x; bool clamped; };
    const Case cases[] = {{0, 12, false}, {1, 17.25, false}, {-1.5, 4.125, false}, {2.2, 23.55, false},
                          {3, 23.55, true}, {-5, 0.45, true}, {-kInf, 0.45, true}};
    for (const auto &c : cases) {
        const auto t = DataVariabilityGeometry::typicality(MarkForm::Icon, c.z, {{"a", 0.5}}, std::nullopt);
        const QByteArray at = QByteArray::number(c.z);
        QVERIFY2(t.ring && near(t.ring->cx, c.x), at);
        QVERIFY(near(t.ring->cy, 12));
        QCOMPARE(t.ring->r, 2.6);
        QCOMPARE(t.clamped, c.clamped);
        // §6.2 gives the Icon no ghosts, centre dot or chevron.
        QVERIFY(t.ghosts.empty());
        QVERIFY(!t.centre);
        QVERIFY(!t.chevron);
    }
}

void VariabilityDataGeometryTest::zonesAreTheSpecs()
{
    const auto c = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {}, std::nullopt);
    QCOMPARE(c.outer.role, GeometryRole::NeutralStrong);
    QVERIFY(!c.outer.filled && c.outer.dashed);
    QCOMPARE(c.outer.width, 1.0);
    QVERIFY(near(c.outer.cx, 40) && near(c.outer.cy, 12) && near(c.outer.rx, 28) && near(c.outer.ry, 9));
    QCOMPARE(c.inner.role, GeometryRole::Text);
    QVERIFY(c.inner.filled && !c.inner.dashed);
    QCOMPARE(c.inner.alpha, 0.07);
    QVERIFY(near(c.inner.rx, 14) && near(c.inner.ry, 5));

    const auto i = DataVariabilityGeometry::typicality(MarkForm::Icon, 0, {}, std::nullopt);
    QVERIFY(!i.outer.filled && i.outer.dashed);
    QVERIFY(near(i.outer.cx, 12) && near(i.outer.rx, 10.5) && near(i.outer.ry, 10.5));
    QVERIFY(i.inner.filled);
    QCOMPARE(i.inner.alpha, 0.07);
    QVERIFY(near(i.inner.rx, 5.25) && near(i.inner.ry, 5.25));
}

void VariabilityDataGeometryTest::ghostJitterIsDeterministicAndBounded()
{
    // Reference vectors: FNV-1a 32-bit (0x811c9dc5, 0xe40c292c, 0xbf9cf968 for these three), then
    // murmur3's fmix32. Worked independently (Python) from the two published algorithms.
    QCOMPARE(ghostJitterHash(QString()), quint32(0xab3e7c0bu));
    QCOMPARE(ghostJitterHash(QStringLiteral("a")), quint32(0x1a80b1b3u));
    QCOMPARE(ghostJitterHash(QStringLiteral("foobar")), quint32(0x0c0da6dcu));

    double lo = 0, hi = 0;
    for (int n = 0; n < 500; ++n) {
        const QString id = QStringLiteral("2026-11-07/swing-%1").arg(n);
        const double j = ghostJitter(id);
        QCOMPARE(ghostJitter(id), j);                 // the same id, the same jitter
        QVERIFY2(j >= -4 && j < 4, qPrintable(id));
        lo = std::min(lo, j);
        hi = std::max(hi, j);
    }
    QVERIFY(lo < -3 && hi > 3);                       // it does spread over the ±4 band

    // The same id in another provider call lands at the same y.
    const std::vector<VariabilityGhostInput> g = {{"s1", 0.2}, {"s2", -0.4}};
    const auto a = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, g, std::nullopt);
    const auto b = DataVariabilityGeometry::typicality(MarkForm::Cell, 1, {{"x", 0}, {"s2", 1.0}}, std::nullopt);
    QCOMPARE(a.ghosts[1].dot.y, b.ghosts[1].dot.y);
    QVERIFY(near(a.ghosts[0].dot.y, 12 + ghostJitter("s1")));
}

void VariabilityDataGeometryTest::sequentialIdsSpread()
{
    // A session's ghosts have ids that differ only at the end. Plain FNV-1a put "swing_0038" …
    // "swing_0049" within 0.4 units of each other (one row); the avalanche must spread them.
    const auto spread = [](const QString &prefix, int from, int to) {
        double lo = 4, hi = -4;
        for (int i = from; i <= to; ++i) {
            const double j = ghostJitter(prefix + QStringLiteral("%1").arg(i, 4, 10, QLatin1Char('0')));
            lo = std::min(lo, j);
            hi = std::max(hi, j);
        }
        return std::pair{lo, hi};
    };
    const auto [lo1, hi1] = spread(QStringLiteral("swing_"), 39, 50);
    QVERIFY2(lo1 <= -2 && hi1 >= 2, qPrintable(QStringLiteral("%1 … %2").arg(lo1).arg(hi1)));
    const auto [lo2, hi2] = spread(QStringLiteral("2026-11-07T10:12:00/swing_"), 40, 49);
    QVERIFY2(lo2 <= -2 && hi2 >= 2, qPrintable(QStringLiteral("%1 … %2").arg(lo2).arg(hi2)));
    const auto [lo3, hi3] = spread(QStringLiteral("2026-11-07/swing_"), 40, 49);
    QVERIFY2(lo3 <= -2 && hi3 >= 2, qPrintable(QStringLiteral("%1 … %2").arg(lo3).arg(hi3)));

    // Ghost dots say explicitly that they have no small variant.
    const auto t = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {{"g", 0.5}}, std::nullopt);
    QCOMPARE(t.ghosts[0].dot.smallMaxSize, 0.0);
    QCOMPARE(t.ghosts[0].dot.rSmall, 1.5);
    const QJsonObject g = toJson(t).value("ghosts").toArray().at(0).toObject();
    QVERIFY(g.contains("smallMaxSize") && g.value("smallMaxSize").toDouble() == 0);
    QCOMPARE(g.value("rSmall").toDouble(), 1.5);
}

void VariabilityDataGeometryTest::ghostDotsPlaceAndCap()
{
    // 15 ghosts, one with no z: the latest 12 with a z are kept, oldest first.
    std::vector<VariabilityGhostInput> g;
    for (int i = 0; i < 15; ++i)
        g.push_back({QStringLiteral("g%1").arg(i), (i - 7) * 0.5});
    g[14].z = kNaN;
    const auto t = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, g, std::nullopt);
    QCOMPARE(int(t.ghosts.size()), 12);
    QCOMPARE(t.ghosts.front().swingId, QStringLiteral("g2"));
    QCOMPARE(t.ghosts.back().swingId, QStringLiteral("g13"));
    for (const auto &gh : t.ghosts) {
        QCOMPARE(gh.dot.role, GeometryRole::Text3);
        QCOMPARE(gh.dot.alpha, 0.70);
        QCOMPARE(gh.dot.r, 1.5);
        QVERIFY(std::abs(gh.dot.y - 12) <= 4);
        if (std::abs(gh.z) <= 2.6) {
            QVERIFY(!gh.clamped);
            QVERIFY(near(gh.dot.x, 40 + gh.z * 14));
        } else {
            QVERIFY(gh.clamped);   // held at the ring's stops, never dropped
            QVERIFY(near(gh.dot.x, gh.z < 0 ? 4 : 76));
        }
    }
    QCOMPARE(t.ghosts.front().z, -2.5);   // g2: (2 − 7) × 0.5
    QVERIFY(near(t.ghosts.front().dot.x, 5));
    QVERIFY(t.ghosts.back().clamped);     // g13: 3.0
}

void VariabilityDataGeometryTest::corridorBracketMapsLoAndHi()
{
    // Cell: x = 40 + 14z. −1 … 1.5 → 26 … 61; the line at y 23.25, 0.75 ticks up, 1.5 px colorGood.
    const auto c = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {}, VariabilityCorridor{-1, 1.5});
    QVERIFY(c.corridor);
    const auto &b = *c.corridor;
    QCOMPARE(b.role, GeometryRole::Good);
    QCOMPARE(b.width, 1.5);
    QVERIFY(near(b.x1, 26) && near(b.x2, 61));
    QVERIFY(!b.loClamped && !b.hiClamped);
    QCOMPARE(int(b.points.size()), 4);
    QCOMPARE(b.points[0], QPointF(26, 22.5));
    QCOMPARE(b.points[1], QPointF(26, 23.25));
    QCOMPARE(b.points[2], QPointF(61, 23.25));
    QCOMPARE(b.points[3], QPointF(61, 22.5));
    for (const auto &p : b.points)
        QVERIFY(p.y() <= 24 && p.y() > 21);   // beneath the outer zone (bottom y 21), inside the cell

    // Swapped ends are the same corridor.
    const auto s = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {}, VariabilityCorridor{1.5, -1});
    QVERIFY(near(s.corridor->x1, 26) && near(s.corridor->x2, 61));

    // An end past the cell clamps to its edge and loses its tick.
    const auto k = DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {}, VariabilityCorridor{-4, 1});
    QVERIFY(k.corridor->loClamped && !k.corridor->hiClamped);
    QVERIFY(near(k.corridor->x1, 0) && near(k.corridor->x2, 54));
    QCOMPARE(int(k.corridor->points.size()), 3);

    // Icon: x = 12 + 5.25z.
    const auto i = DataVariabilityGeometry::typicality(MarkForm::Icon, 0, {}, VariabilityCorridor{-1, 1});
    QVERIFY(near(i.corridor->x1, 6.75) && near(i.corridor->x2, 17.25));

    // Trace: normalised time onto x 2 … 78.
    const auto mean = grid([](int i) { return 10 + std::sin(kPi * i / 100); });
    const auto sd   = grid([](int) { return 1.0; });
    const auto tr = DataVariabilityGeometry::typicalityTrace(mean, sd, mean, VariabilityCorridor{0.25, 0.75});
    QVERIFY(tr.corridor);
    QVERIFY(near(tr.corridor->x1, 21) && near(tr.corridor->x2, 59));
    QVERIFY(tr.corridor->points.front().y() > 21.5);   // beneath the baseline

    // No corridor, or a non-finite end: none.
    QVERIFY(!DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {}, std::nullopt).corridor);
    QVERIFY(!DataVariabilityGeometry::typicality(MarkForm::Cell, 0, {}, VariabilityCorridor{kNaN, 1}).corridor);
}

// ── §6.2 Trace on envelope ─────────────────────────────────────────────────────────────────────────

void VariabilityDataGeometryTest::envelopeIsMeanPlusMinusSd()
{
    const auto mean  = grid([](int i) { return 50 + 30 * std::sin(kPi * i / 100); });
    const auto sd    = grid([](int i) { return 5 + 0.05 * i; });
    const auto swing = grid([&](int i) { return mean[std::size_t(i)] + 0.5 * sd[std::size_t(i)]; });
    const auto t = DataVariabilityGeometry::typicalityTrace(mean, sd, swing, std::nullopt);
    QVERIFY(t.valid);

    double maxPos = 0;
    for (std::size_t i = 0; i < 101; ++i)
        maxPos = std::max(maxPos, mean[i] + sd[i]);
    const double k = 17 / maxPos;   // all positive: the largest value → 17 units above the baseline
    QVERIFY(near(t.valueScale, k));
    QCOMPARE(t.maxNegative, 0.0);
    const auto X = [](std::size_t i) { return 2 + 76.0 * double(i) / 100; };
    const auto Y = [&](double v) { return 21.5 - v * k; };

    QVERIFY(t.envelope);
    const auto &e = t.envelope->points;
    QCOMPARE(int(e.size()), 203);
    QCOMPARE(e.front(), e.back());
    for (std::size_t i = 0; i < 101; ++i) {
        QVERIFY(near(e[i].x(), X(i)) && near(e[i].y(), Y(mean[i] + sd[i]), 1e-9));
        const std::size_t j = 101 + (100 - i);   // the lower edge runs back
        QVERIFY(near(e[j].x(), X(i)) && near(e[j].y(), Y(mean[i] - sd[i]), 1e-9));
    }
    QCOMPARE(t.envelope->role, GeometryRole::Text);
    QCOMPARE(t.envelope->alpha, 0.09);
    QVERIFY(near(std::min_element(e.begin(), e.end(), [](auto &a, auto &b) { return a.y() < b.y(); })->y(), 4.5));

    // Mean: 1 px dashed colorText3. This swing: 1.75 px tone, round cap.
    QCOMPARE(int(t.mean->points.size()), 101);
    QCOMPARE(t.mean->role, GeometryRole::Text3);
    QCOMPARE(t.mean->width, 1.0);
    QVERIFY(t.mean->dashed);
    QCOMPARE(int(t.swing->points.size()), 101);
    QCOMPARE(t.swing->role, GeometryRole::Tone);
    QCOMPARE(t.swing->width, 1.75);
    QVERIFY(t.swing->roundCap && !t.swing->dashed);
    for (std::size_t i = 0; i < 101; ++i) {
        QVERIFY(near(t.mean->points[i].y(), Y(mean[i]), 1e-9));
        QVERIFY(near(t.swing->points[i].y(), Y(swing[i]), 1e-9));
    }
    QVERIFY(near(t.swing->points.front().x(), 2) && near(t.swing->points.back().x(), 78));

    // Baseline: y 21.5 across the traces, colorBorderStrong.
    QVERIFY(near(t.baseline.y1, 21.5) && near(t.baseline.x1, 2) && near(t.baseline.x2, 78));
    QCOMPARE(t.baseline.role, GeometryRole::NeutralStrong);
}

void VariabilityDataGeometryTest::traceScaleHandlesNegativeValues()
{
    // A signed curve: ±22 at the extremes. One scale, zero on the baseline; the negative side has only
    // 2.5 units to the cell's bottom, so k = min(17/22, 2.5/22) = 2.5/22.
    const auto mean = grid([](int i) { return 20 * std::sin(2 * kPi * i / 100); });
    const auto sd   = grid([](int) { return 2.0; });
    const auto t = DataVariabilityGeometry::typicalityTrace(mean, sd, mean, std::nullopt);
    QVERIFY(t.valid);
    QVERIFY(near(t.maxPositive, 22, 1e-6) && near(t.maxNegative, 22, 1e-6));
    QVERIFY(near(t.valueScale, 2.5 / t.maxNegative));
    QVERIFY(near(t.mean->points[0].y(), 21.5));   // value 0 sits on the baseline
    double top = 24, bottom = 0;
    for (const auto &p : t.envelope->points) {
        top = std::min(top, p.y());
        bottom = std::max(bottom, p.y());
    }
    QVERIFY(near(bottom, 24, 1e-9));              // the deepest value reaches the cell's edge, not past it
    QVERIFY(near(top, 21.5 - 2.5, 1e-9));         // the same scale above: honest, not stretched

    // Mostly positive with a small dip: the largest value still maps to 17 above the baseline.
    const auto dip = grid([](int i) { return i == 10 ? -5.0 : 100 * std::sin(kPi * i / 100); });
    const auto zero = grid([](int) { return 0.0; });
    const auto d = DataVariabilityGeometry::typicalityTrace(dip, zero, dip, std::nullopt);
    QVERIFY(near(d.valueScale, 17 / 100.0, 1e-6));
    QVERIFY(near(d.swing->points[10].y(), 21.5 + 5 * 0.17, 1e-6));   // below the baseline, on the same scale

    // Every value 0: flat on the baseline.
    const auto f = DataVariabilityGeometry::typicalityTrace(zero, zero, zero, std::nullopt);
    QVERIFY(f.valid);
    QCOMPARE(f.valueScale, 0.0);
    for (const auto &p : f.swing->points)
        QVERIFY(near(p.y(), 21.5));
}

void VariabilityDataGeometryTest::traceRejectsBrokenCurves()
{
    const auto ok = grid([](int i) { return double(i); });
    const auto sd = grid([](int) { return 1.0; });
    std::vector<double> shortCurve(100, 1.0), nanCurve = ok, negSd = sd;
    nanCurve[40] = kNaN;
    negSd[3] = -0.1;
    for (const auto &bad : {DataVariabilityGeometry::typicalityTrace(shortCurve, sd, ok, std::nullopt),
                            DataVariabilityGeometry::typicalityTrace(ok, sd, nanCurve, std::nullopt),
                            DataVariabilityGeometry::typicalityTrace(ok, negSd, ok, std::nullopt),
                            DataVariabilityGeometry::typicalityTrace({}, {}, {}, std::nullopt)}) {
        QVERIFY(!bad.valid);
        QVERIFY(!bad.envelope && !bad.mean && !bad.swing);
        QVERIFY(near(bad.baseline.y1, 21.5));   // the neutral element alone
    }
}

// ── The provider and the singleton ─────────────────────────────────────────────────────────────────

void VariabilityDataGeometryTest::providerAnswersTheInterface()
{
    const std::unique_ptr<IVariabilityGeometry> scalar =
        std::make_unique<DataVariabilityGeometry>(3.0, std::vector<VariabilityGhostInput>{{"a", 0.1}},
                                                  VariabilityCorridor{-1, 1});
    const auto &cell = scalar->shape(MarkVariant::Typicality, MarkForm::Cell, 4);
    QVERIFY(!cell.isEmpty());
    QVERIFY(cell.typicality && !cell.typicalityTrace);
    QCOMPARE(cell.typicality->form, MarkForm::Cell);
    QVERIFY(cell.typicality->clamped);
    QCOMPARE(int(cell.typicality->ghosts.size()), 1);
    QVERIFY(cell.typicality->corridor);
    // The grade is ignored (§4.1): every grade is the same shape.
    QCOMPARE(&scalar->shape(MarkVariant::Typicality, MarkForm::Cell, 0), &cell);
    const auto &icon = scalar->shape(MarkVariant::Typicality, MarkForm::Icon, 4);
    QCOMPARE(icon.typicality->form, MarkForm::Icon);
    QVERIFY(scalar->shape(MarkVariant::TypicalityTrace, MarkForm::Cell, 0).isEmpty());
    QVERIFY(scalar->shape(MarkVariant::Dispersion, MarkForm::Cell, 3).isEmpty());

    const auto mean = grid([](int i) { return double(i); });
    const auto sd   = grid([](int) { return 1.0; });
    const std::unique_ptr<IVariabilityGeometry> trace = std::make_unique<DataVariabilityGeometry>(mean, sd, mean);
    QVERIFY(trace->shape(MarkVariant::TypicalityTrace, MarkForm::Cell, 2).typicalityTrace->valid);
    QVERIFY(trace->shape(MarkVariant::TypicalityTrace, MarkForm::Icon, 2).isEmpty());   // no Icon form
    QVERIFY(trace->shape(MarkVariant::Typicality, MarkForm::Cell, 2).isEmpty());

    // The shape's JSON carries the data variant under its key; the canonical ones carry null.
    const QJsonObject j = toJson(cell);
    QCOMPARE(j.value("typicality").toObject().value("kind").toString(), QStringLiteral("typicality"));
    QVERIFY(j.value("typicalityTrace").isNull());
    QVERIFY(toJson(makeCanonicalVariabilityGeometry()->shape(MarkVariant::Dispersion, MarkForm::Cell, 3))
                .value("typicality").isNull());
}

void VariabilityDataGeometryTest::singletonBuildsPlainObjects()
{
    QJSEngine engine;
    VariabilityGeometryQml geo(&engine);

    // funnel(): plain arrays and numbers, in px.
    const QJSValue sessions = engine.evaluate(QStringLiteral(
        "[{rho: 0.6, grade: 3}, {rho: 0.5, grade: 3}, {rho: 0.4, grade: 4, breakBefore: true},"
        " {rho: 0.4, grade: 4}, {rho: 0.35, grade: 4}, {grade: 4}]"));
    const QJSValue f = geo.funnel(sessions, 120, 32);
    QVERIFY(f.isObject());
    QCOMPARE(f.property("kind").toString(), QStringLiteral("funnel"));
    QCOMPARE(f.property("currentGrade").toInt(), 4);
    QVERIFY(!f.property("currentHasReading").toBool());   // the last has no rho
    QCOMPARE(f.property("sessions").property("length").toInt(), 5);
    QCOMPARE(f.property("bands").property("length").toInt(), 2);
    QCOMPARE(f.property("breaks").property("length").toInt(), 1);
    QCOMPARE(f.property("minWidth").toNumber(), 96.0);
    const QJSValue dash = f.property("dashPattern");
    QVERIFY(dash.isArray());
    QCOMPARE(dash.property(0).toNumber(), kDashOn);
    QCOMPARE(dash.property(1).toNumber(), kDashOff);
    const QJSValue pts = f.property("bands").property(1).property("points");
    QVERIFY(pts.isArray());
    QVERIFY(pts.property(0).isNumber());
    QCOMPARE(f.property("centre").property("dashed").toBool(), true);
    QCOMPARE(f.property("sessions").property(0).property("marker").property("role").toString(),
             QStringLiteral("text2"));
    QVERIFY(geo.funnel(QJSValue(), 120, 32).property("trendAfter3").toBool());   // no array: no sessions

    // typicality(): a clamped Cell with ghosts and a corridor; ±∞ z is null in the object.
    const QJSValue ghosts = engine.evaluate(QStringLiteral("[{swingId: 'a', z: 0.3}, {swingId: 7, z: -1}]"));
    const QJSValue corridor = engine.evaluate(QStringLiteral("({loZ: -1, hiZ: 1})"));
    const QJSValue t = geo.typicality(1, 3.0, ghosts, corridor);
    QCOMPARE(t.property("kind").toString(), QStringLiteral("typicality"));
    QVERIFY(t.property("clamped").toBool());
    QCOMPARE(t.property("side").toInt(), 1);
    QCOMPARE(t.property("ring").property("cx").toNumber(), 76.0);
    QCOMPARE(t.property("ring").property("fillRole").toString(), QStringLiteral("surface"));
    QCOMPARE(t.property("chevron").property("points").property("length").toInt(), 6);
    QCOMPARE(t.property("ghosts").property("length").toInt(), 2);
    QCOMPARE(t.property("ghosts").property(1).property("swingId").toString(), QStringLiteral("7"));
    QCOMPARE(t.property("corridor").property("role").toString(), QStringLiteral("good"));
    QCOMPARE(t.property("corridor").property("x1").toNumber(), 26.0);
    QCOMPARE(t.property("outer").property("dashed").toBool(), true);
    QVERIFY(geo.typicality(1, 0, QJSValue(QJSValue::NullValue), QJSValue()).property("corridor").isNull());
    const QJSValue inf = geo.typicality(0, kInf, QJSValue(), QJSValue());
    QVERIFY(inf.property("z").isNull());
    QVERIFY(inf.property("clamped").toBool());
    QCOMPARE(inf.property("ring").property("cx").toNumber(), 12 + 2.2 * 5.25);
    QVERIFY(inf.property("chevron").isNull());   // the Icon has none

    // typicalityTrace(): from JS arrays.
    const QJSValue mean = engine.evaluate(QStringLiteral("Array.from({length: 101}, (_, i) => 10 + i / 10)"));
    const QJSValue sd   = engine.evaluate(QStringLiteral("Array.from({length: 101}, () => 1)"));
    const QJSValue tc   = engine.evaluate(QStringLiteral("({loT: 0.25, hiT: 0.75})"));
    const QJSValue tr = geo.typicalityTrace(mean, sd, mean, tc);
    QCOMPARE(tr.property("kind").toString(), QStringLiteral("typicalityTrace"));
    QVERIFY(tr.property("valid").toBool());
    QCOMPARE(tr.property("envelope").property("points").property("length").toInt(), 2 * 203);
    QCOMPARE(tr.property("swing").property("points").property("length").toInt(), 2 * 101);
    QCOMPARE(tr.property("corridor").property("x2").toNumber(), 59.0);
    QVERIFY(!geo.typicalityTrace(sd, sd, engine.evaluate("[1, 2]"), QJSValue()).property("valid").toBool());

    // Per instance: two calls are two objects (no cache).
    QVERIFY(!geo.funnel(sessions, 120, 32).strictlyEquals(geo.funnel(sessions, 120, 32)));

    // polygon(): flat × k.
    const QPolygonF p = geo.polygon({1, 2, 3, 4, 5}, 2);   // an odd tail is ignored
    QCOMPARE(p.size(), qsizetype(2));
    QCOMPARE(p[0], QPointF(2, 4));
    QCOMPARE(p[1], QPointF(6, 8));

    // No engine, no object.
    VariabilityGeometryQml orphan(nullptr);
    QVERIFY(orphan.funnel(sessions, 120, 32).isUndefined());
    QVERIFY(orphan.typicality(1, 0, QJSValue(), QJSValue()).isUndefined());
    QVERIFY(orphan.typicalityTrace(mean, sd, mean, QJSValue()).isUndefined());
}

void VariabilityDataGeometryTest::polygonFeedsAPathPolyline()
{
    qmlRegisterSingletonType<VariabilityGeometryQml>("VariabilityDataGeometryTest", 1, 0, "VariabilityGeometry",
                                                     [](QQmlEngine *q, QJSEngine *j) -> QObject * {
                                                         return VariabilityGeometryQml::create(q, j);
                                                     });
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import VariabilityDataGeometryTest
        Item {
            readonly property real k: 1.5
            readonly property var trace: VariabilityGeometry.typicalityTrace(
                Array.from({length: 101}, (_, i) => Math.sin(Math.PI * i / 100)),
                Array.from({length: 101}, () => 0.1),
                Array.from({length: 101}, (_, i) => Math.sin(Math.PI * i / 100) + 0.05), null)
            readonly property var funnel: VariabilityGeometry.funnel(
                [{rho: 0.5, grade: 4}, {rho: 0.45, grade: 4}, {rho: 0.4, grade: 4}], 120, 32)
            property PathPolyline swing: PathPolyline { path: VariabilityGeometry.polygon(trace.swing.points, k) }
            property PathPolyline band: PathPolyline { path: VariabilityGeometry.polygon(funnel.bands[0].points, 1) }
        }
    )", QUrl());
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    const auto pointsOf = [&](const char *prop) {
        auto *poly = root->property(prop).value<QObject *>();
        const QVariant path = poly ? poly->property("path") : QVariant();
        return path.value<QList<QPointF>>();
    };
    const QList<QPointF> swing = pointsOf("swing");
    QCOMPARE(swing.size(), qsizetype(101));
    QVERIFY(near(swing.front().x(), 2 * 1.5));
    QVERIFY(near(swing.back().x(), 78 * 1.5));
    const QList<QPointF> band = pointsOf("band");
    QCOMPARE(band.size(), qsizetype(2 * 3 + 1));
    QVERIFY(near(band.front().x(), 6));
}

// ── Array-like inputs (a QML `var` holding a C++ list is not a JS Array) ───────────────────────────────

void VariabilityDataGeometryTest::funnelAcceptsAnyArrayLike()
{
    QJSEngine engine;
    VariabilityGeometryQml geo(&engine);
    const QVariantList items = {QVariantMap{{"rho", 0.6}, {"grade", 3}},
                                QVariantMap{{"rho", 0.5}, {"grade", 3}},
                                QVariantMap{{"rho", 0.4}, {"grade", 4}, {"breakBefore", true}},
                                QVariantMap{{"rho", 0.4}, {"grade", 4}},
                                QVariantMap{{"rho", 0.35}, {"grade", 4}}};
    const ListForms f = listForms(engine, items);
    QVERIFY(f.array.isArray());
    const QJSValue ref = geo.funnel(f.array, 120, 32);
    QCOMPARE(ref.property("bands").property("length").toInt(), 2);
    const QString want = stringify(engine, ref);
    for (const QJSValue &v : {f.variantList, f.wrappedVariant, f.arrayLike})
        QCOMPARE(stringify(engine, geo.funnel(v, 120, 32)), want);
    QVERIFY(!f.wrappedVariant.isArray());   // the case isArray() alone missed
}

void VariabilityDataGeometryTest::typicalityAcceptsAnyArrayLike()
{
    QJSEngine engine;
    VariabilityGeometryQml geo(&engine);
    const QVariantList items = {QVariantMap{{"swingId", "a"}, {"z", 0.3}},
                                QVariantMap{{"swingId", "b"}, {"z", -1.2}},
                                QVariantMap{{"swingId", 7}, {"z", 3.1}}};
    const ListForms f = listForms(engine, items);
    const QJSValue corridor = engine.evaluate(QStringLiteral("({loZ: -1, hiZ: 1})"));
    const QJSValue ref = geo.typicality(1, 0.5, f.array, corridor);
    QCOMPARE(ref.property("ghosts").property("length").toInt(), 3);
    const QString want = stringify(engine, ref);
    for (const QJSValue &v : {f.variantList, f.wrappedVariant, f.arrayLike})
        QCOMPARE(stringify(engine, geo.typicality(1, 0.5, v, corridor)), want);
}

void VariabilityDataGeometryTest::typicalityTraceAcceptsAnyArrayLike()
{
    QJSEngine engine;
    VariabilityGeometryQml geo(&engine);
    QVariantList mean, sd, swing;
    for (int i = 0; i <= 100; ++i) {
        mean.append(10 + std::sin(kPi * i / 100));
        sd.append(0.5);
        swing.append(10.2 + std::sin(kPi * i / 100));
    }
    const ListForms m = listForms(engine, mean), s = listForms(engine, sd), w = listForms(engine, swing);
    const QJSValue ref = geo.typicalityTrace(m.array, s.array, w.array, QJSValue());
    QVERIFY(ref.property("valid").toBool());
    const QString want = stringify(engine, ref);
    QCOMPARE(stringify(engine, geo.typicalityTrace(m.variantList, s.variantList, w.variantList, QJSValue())), want);
    QCOMPARE(stringify(engine, geo.typicalityTrace(m.wrappedVariant, s.wrappedVariant, w.wrappedVariant, QJSValue())),
             want);
    QCOMPARE(stringify(engine, geo.typicalityTrace(m.arrayLike, s.arrayLike, w.arrayLike, QJSValue())), want);
    // Mixed forms are fine too; a list of the wrong length is still rejected.
    QCOMPARE(stringify(engine, geo.typicalityTrace(m.wrappedVariant, s.array, w.arrayLike, QJSValue())), want);
    QVariantList shortList = mean;
    shortList.removeLast();
    QVERIFY(!geo.typicalityTrace(engine.toScriptValue(QVariant::fromValue(shortList)), s.array, w.array, QJSValue())
                 .property("valid").toBool());
}

void VariabilityDataGeometryTest::qmlVarListsFromCppDraw()
{
    // The reported failure: a QML `var` property set from C++ to a QVariantList, handed to the provider.
    qmlRegisterSingletonType<VariabilityGeometryQml>("VariabilityDataGeometryVarTest", 1, 0, "VariabilityGeometry",
                                                     [](QQmlEngine *q, QJSEngine *j) -> QObject * {
                                                         return VariabilityGeometryQml::create(q, j);
                                                     });
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import VariabilityDataGeometryVarTest
        QtObject {
            property var sessions: []
            property var ghosts: []
            property var curve: []
            property var sd: []
            readonly property int bands: VariabilityGeometry.funnel(sessions, 120, 32).bands.length
            readonly property int ghostCount: VariabilityGeometry.typicality(1, 0, ghosts, null).ghosts.length
            readonly property bool traceValid: VariabilityGeometry.typicalityTrace(curve, sd, curve, null).valid
        }
    )", QUrl());
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QCOMPARE(root->property("bands").toInt(), 0);

    QVariantList sessions, ghosts, curve, sd;
    for (int i = 0; i < 4; ++i)
        sessions.append(QVariantMap{{"rho", 0.5 - 0.05 * i}, {"grade", 4}});
    for (int i = 0; i < 5; ++i)
        ghosts.append(QVariantMap{{"swingId", QStringLiteral("s%1").arg(i)}, {"z", 0.2 * i}});
    for (int i = 0; i <= 100; ++i) {
        curve.append(std::sin(kPi * i / 100));
        sd.append(0.1);
    }
    root->setProperty("sessions", sessions);
    root->setProperty("ghosts", ghosts);
    root->setProperty("curve", curve);
    root->setProperty("sd", sd);
    QCOMPARE(root->property("bands").toInt(), 1);
    QCOMPARE(root->property("ghostCount").toInt(), 5);
    QVERIFY(root->property("traceValid").toBool());
}

QTEST_MAIN(VariabilityDataGeometryTest)
#include "variability_data_geometry_test.moc"
