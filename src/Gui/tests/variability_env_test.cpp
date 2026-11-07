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

// The variability scale provider (docs/design/variability-mark/spec.md §8.2 step 4, §8.5, §8.6) on the real
// manifest and norms: the corridor rung IS consistencyEnvFromPacks().normSigma, a key with no norm and no
// variabilityScale has no scale, and the scale type is the descriptor's. Then the ladder and σnoise on a
// stub env and in-memory descriptors, since the shipped manifest states no variabilityScale yet.

#include "charts/consistency_env.h"
#include "charts/session_warp.h"                // phase ids
#include "charts/variability_env.h"

#include "Metrics/metric_catalogue.h"

#include <QtTest>

#include <cmath>
#include <memory>

namespace pa = pinpoint::analysis;
namespace pv = pinpoint::variability;
using namespace pinpoint::charts;

namespace {

const pa::MetricCatalogue &manifest()
{
    static const struct Holder {
        Holder() { pa::installMetricManifest(catalogue); }
        pa::MetricCatalogue catalogue;
    } holder;
    return holder.catalogue;
}

} // namespace

class VariabilityEnvTest : public QObject {
    Q_OBJECT

    std::unique_ptr<IVariabilityScaleProvider> m_provider;
    ConsistencyEnv m_env;

private slots:
    void initTestCase()
    {
        // The GUI thread's first call, as the contract asks: the provider takes the env here.
        m_provider = makeVariabilityScaleProvider();
        m_env = consistencyEnvFromPacks();
        QVERIFY(m_provider);
        QVERIFY(m_env.normSigma);
    }

    void theCorridorIsTheConsistencyNormSigma_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<int>("phase");
        QTest::addColumn<QString>("club");
        QTest::addColumn<double>("expected");     // the norms' own (σLo + σHi)/2, as consistency_env_test pins
        QTest::newRow("clubheadSpeed driver")  << "clubheadSpeed"    << phase::Impact << "DRIVER"         << 4.65;
        QTest::newRow("clubheadSpeed 7 iron")  << "clubheadSpeed"    << phase::Impact << "7 IRON"         << 3.75;
        QTest::newRow("clubheadSpeed wedge")   << "clubheadSpeed"    << phase::Impact << "PITCHING WEDGE" << 3.4;
        QTest::newRow("trunkRise both sides")  << "trunkRise"        << phase::Impact << "7 IRON"         << 4.5;
        QTest::newRow("leadWristFlexExt top")  << "leadWristFlexExt" << phase::Top    << "7 IRON"         << 11.0;
        QTest::newRow("thoraxRotation median") << "thoraxRotation"   << phase::Address << "7 IRON"        << 16.0;
    }

    void theCorridorIsTheConsistencyNormSigma()
    {
        QFETCH(QString, key);
        QFETCH(int, phase);
        QFETCH(QString, club);
        QFETCH(double, expected);
        const double normSigma = m_env.normSigma(key, phase, club);
        QCOMPARE(normSigma, expected);
        const VariabilityScaleAnswer a = m_provider->scaleFor(key, phase, club);
        QCOMPARE(a.source, pv::ScaleSource::Corridor);
        QVERIFY(a.found());
        QCOMPARE(a.scale, normSigma);
        QCOMPARE(m_provider->corridorHalfWidth(key, phase, club), normSigma);
    }

    void aKeyWithNoNormHasNoScale()
    {
        for (const char *key : { "headTilt", "noSuchKey" }) {
            const VariabilityScaleAnswer a = m_provider->scaleFor(QString::fromLatin1(key), phase::Impact,
                                                                  QStringLiteral("7 IRON"));
            QCOMPARE(a.source, pv::ScaleSource::None);
            QVERIFY(!a.found());
            QVERIFY(std::isnan(a.scale));
            QVERIFY(std::isnan(m_provider->corridorHalfWidth(QString::fromLatin1(key), phase::Impact,
                                                             QStringLiteral("7 IRON"))));
        }
    }

    void theScaleTypeIsTheDescriptors()
    {
        int seen = 0, notInterval = 0;
        for (const pa::MetricDescriptor *d : manifest().all()) {
            QCOMPARE(m_provider->scaleType(d->key), d->scaleType);
            QCOMPARE(m_provider->variabilityScale(d->key), d->variabilityScale);
            QCOMPARE(m_provider->measurementTolerance(d->key), d->measurementTolerance);
            ++seen;
            notInterval += d->scaleType != pv::ScaleType::Interval ? 1 : 0;
        }
        QVERIFY(seen > 100);
        QVERIFY(notInterval > 0);       // the check above would pass on an all-Interval answer otherwise
        QCOMPARE(m_provider->scaleType(QStringLiteral("clubheadSpeed")), pv::ScaleType::Ratio);
        QCOMPARE(m_provider->scaleType(QStringLiteral("impactShaftLean")), pv::ScaleType::Circular);
        QCOMPARE(m_provider->scaleType(QStringLiteral("kinematicSequence")), pv::ScaleType::Ordinal);
        QCOMPARE(m_provider->scaleType(QStringLiteral("headTilt")), pv::ScaleType::Interval);
        QCOMPARE(m_provider->scaleType(QStringLiteral("noSuchKey")), pv::ScaleType::Interval);   // the default
    }

    // ── the ladder and σnoise on a stub ─────────────────────────────────────────────────────────

    void theCorridorWinsThenVariabilityScaleThenNone()
    {
        pa::MetricDescriptor withScale;
        withScale.key = QStringLiteral("a");
        withScale.scaleType = pv::ScaleType::Ratio;
        withScale.variabilityScale = 2.5;
        withScale.measurementTolerance = 0.4;
        pa::MetricDescriptor bare;
        bare.key = QStringLiteral("b");

        ConsistencyEnv env;
        env.normSigma = [](const QString &key, int, const QString &club) {
            if (key == QLatin1String("a") && club == QLatin1String("DRIVER")) return 3.0;
            if (key == QLatin1String("a") && club == QLatin1String("WEDGE")) return 0.0;    // not a scale
            return kNaN;
        };
        const auto provider = makeVariabilityScaleProvider(env, [&](const QString &key) -> const pa::MetricDescriptor * {
            if (key == QLatin1String("a")) return &withScale;
            if (key == QLatin1String("b")) return &bare;
            return nullptr;
        });

        VariabilityScaleAnswer a = provider->scaleFor(QStringLiteral("a"), phase::Impact, QStringLiteral("DRIVER"));
        QCOMPARE(a.source, pv::ScaleSource::Corridor);
        QCOMPARE(a.scale, 3.0);
        a = provider->scaleFor(QStringLiteral("a"), phase::Impact, QStringLiteral("7 IRON"));
        QCOMPARE(a.source, pv::ScaleSource::VariabilityScale);
        QCOMPARE(a.scale, 2.5);
        QVERIFY(std::isnan(provider->corridorHalfWidth(QStringLiteral("a"), phase::Impact, QStringLiteral("7 IRON"))));
        a = provider->scaleFor(QStringLiteral("a"), phase::Impact, QStringLiteral("WEDGE"));
        QCOMPARE(a.source, pv::ScaleSource::VariabilityScale);
        a = provider->scaleFor(QStringLiteral("b"), phase::Impact, QStringLiteral("DRIVER"));
        QCOMPARE(a.source, pv::ScaleSource::None);

        QCOMPARE(provider->scaleType(QStringLiteral("a")), pv::ScaleType::Ratio);
        QCOMPARE(provider->measurementTolerance(QStringLiteral("a")), std::optional<double>(0.4));
        QCOMPARE(provider->measurementTolerance(QStringLiteral("b")), std::optional<double>());
    }

    void aVariabilityScaleThatIsNotPositiveIsSkipped()
    {
        // variabilityScale is a rung only when finite and positive; otherwise the ladder falls to None (and the
        // grader then tries its CV rung itself).
        for (double bad : { 0.0, -1.0, kNaN }) {
            pa::MetricDescriptor d;
            d.key = QStringLiteral("a");
            d.variabilityScale = bad;
            const auto provider = makeVariabilityScaleProvider(ConsistencyEnv{}, [&](const QString &key) {
                return key == QLatin1String("a") ? &d : nullptr;
            });
            const VariabilityScaleAnswer a = provider->scaleFor(QStringLiteral("a"), phase::Impact, QStringLiteral("DRIVER"));
            QCOMPARE(a.source, pv::ScaleSource::None);
            QVERIFY(std::isnan(a.scale));
        }
    }

    void theNoiseIsTheReadingsOwnSigma()
    {
        const auto provider = makeVariabilityScaleProvider(ConsistencyEnv{}, {});
        ConsistencyReading r;
        r.ownSigma = 1.25;
        QCOMPARE(provider->noiseSigma(r), 1.25);
        r.ownSigma = kNaN;
        QVERIFY(std::isnan(provider->noiseSigma(r)));
        r.ownSigma = 0.0;
        QVERIFY(std::isnan(provider->noiseSigma(r)));
        // No env, no descriptors: nothing found, nothing invented.
        QCOMPARE(provider->scaleFor(QStringLiteral("x"), phase::Impact, QStringLiteral("DRIVER")).source,
                 pv::ScaleSource::None);
        QCOMPARE(provider->scaleType(QStringLiteral("x")), pv::ScaleType::Interval);
    }
};

QTEST_GUILESS_MAIN(VariabilityEnvTest)
#include "variability_env_test.moc"
