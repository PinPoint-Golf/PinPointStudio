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

// The variability façade (src/Gui/variability/variability_facade.h): what QML is handed is what the
// grader found, field for field, with the token spellings the vocabulary singletons key on, a NaN ρ
// that QML can test rather than print, and the §8.10 constants read from variability_constants.h.
// The results are built by hand: grading is variability_grader_test's job, mapping is this one's.

#include "variability/variability_facade.h"

#include "../../Metrics/variability/variability_constants.h"
#include "../../Metrics/variability/variability_result.h"

#include <QMetaProperty>
#include <QtTest>

#include <cmath>
#include <limits>

using namespace pinpoint::variability;

namespace {

// A scalar consistency grade with its §7.5 row: "σ 2.1° · 0.42 of corridor · n 24".
VariabilityResult scalarResult()
{
    VariabilityResult r;
    r.basis       = Basis::Scalar;
    r.graded      = true;
    r.grade       = 4;
    r.n           = 24;
    r.sd          = 2.1;
    r.sdObserved  = 2.1;
    r.scale       = 5.0;
    r.scaleSource = ScaleSource::Corridor;
    r.rho         = 0.42;
    return r;
}

} // namespace

class VariabilityFacadeTest : public QObject {
    Q_OBJECT

private slots:
    void fromResultMapsEveryField();
    void measurementLimitedIsPassedThroughNotApplied();
    void basisTextUsesTheCallersUnitAndDecimals();
    void basisTokens();
    void domainTokens();
    void ungradedReasonTokens();
    void defaultIsUngraded();
    void nanRho();
    void equalityTreatsNanRhoAsEqual();
    void constantsAreTheHeaders();
    void facadeHasNoInvokables();
};

void VariabilityFacadeTest::fromResultMapsEveryField()
{
    const VariabilityResult r = scalarResult();
    const auto v = VariabilityResultView::fromResult(r, QStringLiteral("°"), 1);

    QCOMPARE(v.grade(), 4);
    QCOMPARE(v.graded(), true);
    QCOMPARE(v.basis(), QStringLiteral("scalar"));
    QCOMPARE(v.ungradedReason(), QString());
    QCOMPARE(v.measurementLimited(), false);
    QCOMPARE(v.n(), 24);
    QCOMPARE(v.rho(), 0.42);
    QVERIFY(v.hasRho());
    QCOMPARE(v.basisText(), QStringLiteral("σ 2.1° · 0.42 of corridor · n 24"));
    QCOMPARE(v.basisText(), r.basisString(QStringLiteral("°"), 1));
    QCOMPARE(v.domain(), QStringLiteral("consistency"));

    // The grade-0 mark: graded, grade 0, a reason, and no basis string (§7.5 has no row for it).
    VariabilityResult few;
    few.basis          = Basis::Trace;
    few.graded         = true;
    few.grade          = 0;
    few.n              = 5;
    few.ungradedReason = UngradedReason::TooFewSwings;
    const auto f = VariabilityResultView::fromResult(few);
    QCOMPARE(f.grade(), 0);
    QCOMPARE(f.graded(), true);
    QCOMPARE(f.basis(), QStringLiteral("trace"));
    QCOMPARE(f.ungradedReason(), QStringLiteral("tooFewSwings"));
    QCOMPARE(f.n(), 5);
    QVERIFY(f.basisText().isEmpty());
    QVERIFY(!f.hasRho());

    // The Needle: measurement domain, no §7.5 row.
    VariabilityResult needle;
    needle.basis           = Basis::Measurement;
    needle.graded          = true;
    needle.grade           = 2;
    needle.n               = 30;
    needle.scale           = 1.0;
    needle.scaleSource     = ScaleSource::MeasurementTolerance;
    needle.uncertainty     = 0.6;
    needle.uncertaintyKind = UncertaintyKind::ValidationRmse;
    needle.rhoM            = 0.6;
    const auto m = VariabilityResultView::fromResult(needle, QStringLiteral("cm"), 1);
    QCOMPARE(m.grade(), 2);
    QCOMPARE(m.basis(), QStringLiteral("measurement"));
    QCOMPARE(m.domain(), QStringLiteral("measurement"));
    QVERIFY(m.basisText().isEmpty());
    QVERIFY(!m.hasRho());   // ρm is not ρ

    // The properties QML reads are the ones above, by name, through the meta-object.
    const QMetaObject &mo = VariabilityResultView::staticMetaObject;
    const char *names[] = {"grade", "graded", "basis", "ungradedReason", "measurementLimited",
                           "n", "rho", "hasRho", "basisText", "domain"};
    for (const char *name : names) {
        const int i = mo.indexOfProperty(name);
        QVERIFY2(i >= 0, name);
        QVERIFY2(!mo.property(i).isWritable(), name);
    }
    QCOMPARE(mo.property(mo.indexOfProperty("basisText")).readOnGadget(&v).toString(), v.basisText());
    QCOMPARE(mo.property(mo.indexOfProperty("grade")).readOnGadget(&v).toInt(), 4);
}

void VariabilityFacadeTest::measurementLimitedIsPassedThroughNotApplied()
{
    VariabilityResult r = scalarResult();
    r.measurementLimited = true;
    const auto v = VariabilityResultView::fromResult(r, QStringLiteral("°"), 1);

    // §8.6: the computed grade stays; the golfer layer's "Checking" is the surface's choice.
    QCOMPARE(v.grade(), 4);
    QCOMPARE(v.graded(), true);
    QCOMPARE(v.measurementLimited(), true);
    QCOMPARE(v.basisText(), QStringLiteral("σ 2.1° · 0.42 of corridor · n 24 · measurement-limited"));
}

void VariabilityFacadeTest::basisTextUsesTheCallersUnitAndDecimals()
{
    VariabilityResult r = scalarResult();
    r.sd = 4.8;
    QCOMPARE(VariabilityResultView::fromResult(r, QStringLiteral("cm"), 1).basisText(),
             QStringLiteral("σ 4.8 cm · 0.42 of corridor · n 24"));
    QCOMPARE(VariabilityResultView::fromResult(r, QStringLiteral("cm"), 2).basisText(),
             QStringLiteral("σ 4.80 cm · 0.42 of corridor · n 24"));
    QCOMPARE(VariabilityResultView::fromResult(r).basisText(), r.basisString());
}

void VariabilityFacadeTest::basisTokens()
{
    QCOMPARE(VariabilityResultView::basisToken(Basis::Scalar),      QStringLiteral("scalar"));
    QCOMPARE(VariabilityResultView::basisToken(Basis::Trace),       QStringLiteral("trace"));
    QCOMPARE(VariabilityResultView::basisToken(Basis::Ordinal),     QStringLiteral("ordinal"));
    QCOMPARE(VariabilityResultView::basisToken(Basis::Measurement), QStringLiteral("measurement"));
}

void VariabilityFacadeTest::domainTokens()
{
    QCOMPARE(VariabilityResultView::domainToken(Basis::Scalar),      QStringLiteral("consistency"));
    QCOMPARE(VariabilityResultView::domainToken(Basis::Trace),       QStringLiteral("consistency"));
    QCOMPARE(VariabilityResultView::domainToken(Basis::Ordinal),     QStringLiteral("consistency"));
    QCOMPARE(VariabilityResultView::domainToken(Basis::Measurement), QStringLiteral("measurement"));
}

void VariabilityFacadeTest::ungradedReasonTokens()
{
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::None),                QString());
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::TooFewSwings),        QStringLiteral("tooFewSwings"));
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::NoScale),             QStringLiteral("noScale"));
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::NonPositiveForRatio), QStringLiteral("nonPositiveForRatio"));
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::NoUncertainty),       QStringLiteral("noUncertainty"));
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::ScaleTypeMismatch),   QStringLiteral("scaleTypeMismatch"));
    QCOMPARE(VariabilityResultView::ungradedReasonToken(UngradedReason::MalformedInput),      QStringLiteral("malformedInput"));

    // And through fromResult: an ungraded result carries its reason and no mark.
    VariabilityResult r;
    r.basis          = Basis::Scalar;
    r.graded         = false;
    r.n              = 12;
    r.ungradedReason = UngradedReason::NoScale;
    const auto v = VariabilityResultView::fromResult(r);
    QCOMPARE(v.graded(), false);
    QCOMPARE(v.grade(), 0);
    QCOMPARE(v.ungradedReason(), QStringLiteral("noScale"));
    QVERIFY(v.basisText().isEmpty());
}

void VariabilityFacadeTest::defaultIsUngraded()
{
    const VariabilityResultView v;
    QCOMPARE(v.graded(), false);
    QCOMPARE(v.grade(), 0);
    QCOMPARE(v.basis(), QStringLiteral("scalar"));
    QCOMPARE(v.domain(), QStringLiteral("consistency"));
    QCOMPARE(v.ungradedReason(), QString());
    QCOMPARE(v.measurementLimited(), false);
    QCOMPARE(v.n(), 0);
    QVERIFY(std::isnan(v.rho()));
    QVERIFY(!v.hasRho());
    QVERIFY(v.basisText().isEmpty());
}

void VariabilityFacadeTest::nanRho()
{
    // Ungraded: ρ was never computed.
    VariabilityResult none;
    const auto a = VariabilityResultView::fromResult(none);
    QVERIFY(std::isnan(a.rho()));
    QVERIFY(!a.hasRho());

    // Ordinal: graded on s′, not ρ.
    VariabilityResult ord;
    ord.basis    = Basis::Ordinal;
    ord.graded   = true;
    ord.grade    = 3;
    ord.n        = 24;
    ord.matching = 21;
    ord.k        = 4;
    ord.s        = 21.0 / 24.0;
    ord.sPrime   = 0.83;
    const auto b = VariabilityResultView::fromResult(ord);
    QVERIFY(std::isnan(b.rho()));
    QVERIFY(!b.hasRho());
    QCOMPARE(b.basisText(), QStringLiteral("Same order in 21 of 24 swings"));

    // An infinity is not a ρ either: it reaches QML as NaN, never as "inf".
    VariabilityResult inf = scalarResult();
    inf.rho = std::numeric_limits<double>::infinity();
    const auto c = VariabilityResultView::fromResult(inf);
    QVERIFY(std::isnan(c.rho()));
    QVERIFY(!c.hasRho());

    // Through the meta-object, as QML reads it.
    const QMetaObject &mo = VariabilityResultView::staticMetaObject;
    const QVariant rho = mo.property(mo.indexOfProperty("rho")).readOnGadget(&b);
    QVERIFY(std::isnan(rho.toDouble()));
}

void VariabilityFacadeTest::equalityTreatsNanRhoAsEqual()
{
    // Two NaN ρ are one "no ρ": an unchanged ordinal, Needle or ungraded view equals its copy.
    VariabilityResult ord;
    ord.basis    = Basis::Ordinal;
    ord.graded   = true;
    ord.grade    = 3;
    ord.n        = 24;
    ord.matching = 21;
    ord.sPrime   = 0.83;
    const auto a = VariabilityResultView::fromResult(ord);
    const auto b = VariabilityResultView::fromResult(ord);
    QVERIFY(std::isnan(a.rho()));
    QVERIFY(a == b);
    QVERIFY(VariabilityResultView() == VariabilityResultView());

    VariabilityResult needle;
    needle.basis  = Basis::Measurement;
    needle.graded = true;
    needle.grade  = 2;
    needle.rhoM   = 0.6;
    QVERIFY(VariabilityResultView::fromResult(needle) == VariabilityResultView::fromResult(needle));

    // A finite ρ against a NaN one, or a different ρ, is a change.
    const auto s1 = VariabilityResultView::fromResult(scalarResult(), QStringLiteral("°"), 1);
    VariabilityResult other = scalarResult();
    other.rho = 0.43;
    QVERIFY(s1 == VariabilityResultView::fromResult(scalarResult(), QStringLiteral("°"), 1));
    QVERIFY(!(s1 == VariabilityResultView::fromResult(other, QStringLiteral("°"), 1)));
    VariabilityResult noRho = scalarResult();
    noRho.rho = VariabilityResult::kNaN;
    QVERIFY(!(s1 == VariabilityResultView::fromResult(noRho, QStringLiteral("°"), 1)));

    // Any other field is a change too.
    VariabilityResult limited = scalarResult();
    limited.measurementLimited = true;
    QVERIFY(!(s1 == VariabilityResultView::fromResult(limited, QStringLiteral("°"), 1)));
}

void VariabilityFacadeTest::constantsAreTheHeaders()
{
    const VariabilityFacade f;
    QCOMPARE(f.nMin(), kNMin);
    QCOMPARE(f.nWindow(), kNWindow);
    QCOMPARE(f.trendMinSessions(), kTrendMinSessions);

    QCOMPARE(f.property("nMin").toInt(), kNMin);
    QCOMPARE(f.property("nWindow").toInt(), kNWindow);
    QCOMPARE(f.property("trendMinSessions").toInt(), kTrendMinSessions);

    const QMetaObject *mo = f.metaObject();
    for (const char *name : {"nMin", "nWindow", "trendMinSessions"}) {
        const QMetaProperty p = mo->property(mo->indexOfProperty(name));
        QVERIFY2(p.isValid(), name);
        QVERIFY2(p.isConstant(), name);
        QVERIFY2(!p.isWritable(), name);
    }
}

void VariabilityFacadeTest::facadeHasNoInvokables()
{
    // QML must never grade: the singleton exposes constants and nothing callable.
    const QMetaObject &mo = VariabilityFacade::staticMetaObject;
    QCOMPARE(mo.methodCount() - mo.methodOffset(), 0);
    const QMetaObject &vo = VariabilityResultView::staticMetaObject;
    QCOMPARE(vo.methodCount() - vo.methodOffset(), 0);
}

QTEST_GUILESS_MAIN(VariabilityFacadeTest)
#include "variability_facade_test.moc"
