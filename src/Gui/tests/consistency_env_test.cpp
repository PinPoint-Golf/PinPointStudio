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

// The Consistency env on the real manifest, conditions and norms: the 28 readings placed by hand
// (consistency_design.md Appendix B), the walk over the conditions against the tally's own area column,
// its tie-break on a pack made here, and the norm sigma rule (CONTRACT "Norm sigma of a reading").

#include "charts/consistency_env.h"

#include "Coaching/swing_areas_pack.h"
#include "Diagnostics/packs/pack_provider.h"
#include "Metrics/metric_catalogue.h"

#include <QFile>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QtTest>

#include <cmath>
#include <memory>

namespace pa = pinpoint::analysis;
using pinpoint::charts::ConsistencyEnv;
using pinpoint::charts::consistencyEnvFromPacks;

namespace {

// Appendix B, as written there and in the tally's HAND.
const QHash<QString, QString> &handPlacements()
{
    static const QHash<QString, QString> v{
        { "clubAngularSpeed", "sequence" },      { "clubheadSpeed", "sequence" },
        { "handSpeed", "sequence" },             { "leadArmAngularSpeed", "sequence" },
        { "thoraxAngularSpeed", "sequence" },    { "xFactor", "sequence" },
        { "tempoBackswing", "sequence" },
        { "thoraxRotationSigned", "armsAndClub" }, { "elbowAlignment", "armsAndClub" },
        { "handPlaneCurve", "swingPlane" },
        { "shoulderPlaneAngle", "armsAndClub" }, { "handPlaneSteepening", "swingPlane" },
        { "swingPlane", "swingPlane" },          { "swingPlaneAngle", "swingPlane" },
        { "headTilt", "posture" },               { "spineSideBend", "posture" },
        { "trunkRise", "posture" },              { "hipDepthButt", "posture" },
        { "headPitch", "posture" },              { "spineBackLine", "posture" },
        { "ballBodyDistance", "setup" },         { "stanceWidthMm", "setup" },
        { "toeLineAngle", "setup" },             { "leadFootFlare", "setup" },
        { "trailFootFlare", "setup" },
        { "attackAngle", "impact" },             { "lowPointFromAttack", "impact" },
        { "plumbBobDistance", "lateral" },
        { "trailWristFlexExt", "release" },
    };
    return v;
}

// Deliberately unplaced and not read (Appendix B).
const QSet<QString> &unplacedKeys()
{
    static const QSet<QString> v{ "compoundMiss", "shoulderPlane", "swingPlaneIotaBack", "swingPlaneIotaDown" };
    return v;
}

// The table's areas, in the contract's order.
const QStringList &tableAreas()
{
    static const QStringList v{ "setup", "posture", "armsAndClub", "swingPlane", "sequence", "lateral", "release", "impact", "finish" };
    return v;
}

// metric -> area, from the tally's own output. Reads the first four columns: a later one is a unit, free text.
QHash<QString, QString> tallyAreas()
{
    QHash<QString, QString> out;
    QFile f(QString::fromUtf8(CONSISTENCY_READINGS_CSV));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    f.readLine();                                       // session,metric,column,area,...
    while (!f.atEnd()) {
        const QStringList cells = QString::fromUtf8(f.readLine()).trimmed().split(QLatin1Char(','));
        if (cells.size() > 3) out.insert(cells.at(1), cells.at(3));
    }
    return out;
}

pa::Measure measureOf(const QString &id, const QString &metricKey, const QStringList &preferKeys = {})
{
    pa::Measure m;
    m.id = id;
    m.metricKey = metricKey;
    m.preferKeys = preferKeys;
    return m;
}

pa::Signal signalOf(const QString &id, const QStringList &measures)
{
    pa::Signal s;
    s.id = id;
    s.measures = measures;
    return s;
}

pa::Condition conditionOf(const QString &id, pa::ConditionGroup group, const QStringList &detectedBy)
{
    pa::Condition c;
    c.id = id;
    c.group = group;
    c.detectedBy = detectedBy;
    return c;
}

constexpr int kP1 = 0, kP4 = 2, kP7 = 5;                // Phase ids

} // namespace

class ConsistencyEnvTest : public QObject {
    Q_OBJECT

    ConsistencyEnv m_env;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);       // the user's own packs stay out of it
        m_env = consistencyEnvFromPacks();
        QVERIFY(m_env.areaOf);
        QVERIFY(m_env.normSigma);
    }

    void theManifestCarriesAppendixBAndNothingElse()
    {
        pa::MetricCatalogue catalogue;
        pa::installMetricManifest(catalogue);

        QCOMPARE(int(handPlacements().size()), 29);
        for (auto it = handPlacements().constBegin(); it != handPlacements().constEnd(); ++it) {
            const pa::MetricDescriptor *d = catalogue.descriptor(it.key());
            QVERIFY2(d, qPrintable(it.key()));
            QCOMPARE(d->consistencyArea, it.value());
            QCOMPARE(m_env.areaOf(it.key()), it.value());
        }

        int carried = 0;
        for (const pa::MetricDescriptor *d : catalogue.all()) {
            if (d->consistencyArea.isEmpty()) continue;
            ++carried;
            QVERIFY2(tableAreas().contains(d->consistencyArea), qPrintable(d->key));
            QVERIFY2(pa::areaGroupOrder().contains(d->consistencyArea), qPrintable(d->key));
        }
        QCOMPARE(carried, 29);
    }

    // Appendix B says no condition detects on these. The hand placement wins here and the walk wins in the
    // tally, so the two only agree while that stays true.
    void noConditionDetectsOnAHandPlacedReading()
    {
        const std::unique_ptr<pa::ICharacteristicPackProvider> provider = pa::makeCharacteristicPackProvider();
        QVERIFY(provider);
        const QHash<QString, QString> walked = pa::metricAreasForPack(provider->pack());
        for (auto it = handPlacements().constBegin(); it != handPlacements().constEnd(); ++it)
            QVERIFY2(!walked.contains(it.key()), qPrintable(it.key()));
    }

    void everyReadingOfTheTallySitsInItsArea()
    {
        const QHash<QString, QString> tally = tallyAreas();
        QVERIFY2(!tally.isEmpty(), CONSISTENCY_READINGS_CSV);

        QStringList wrong;
        for (auto it = tally.constBegin(); it != tally.constEnd(); ++it) {
            const QString ours = m_env.areaOf(it.key());
            // The tally is a record of 6 Nov: Swing plane became its own area on 8 Nov (swing_plane_area.md).
            static const QSet<QString> movedSince{ "swingPlaneCurve", "handPlaneCurve", "handPlaneSteepening", "swingPlane",
                                                   "swingPlaneAngle", "handPathLoop", "transitionPlaneDelta" };
            const QString want = movedSince.contains(it.key()) ? QStringLiteral("swingPlane") : it.value();
            if (ours != want)
                wrong << QStringLiteral("%1: tally \"%2\", env \"%3\"").arg(it.key(), want, ours);
            if (ours.isEmpty() && !unplacedKeys().contains(it.key()))
                wrong << QStringLiteral("%1: unplaced and not on the named list").arg(it.key());
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1String("; "))));
    }

    void theNamedFourAreUnplaced()
    {
        for (const QString &key : unplacedKeys()) {
            QVERIFY2(m_env.areaOf(key).isEmpty(), qPrintable(key));
            QVERIFY2(!handPlacements().contains(key), qPrintable(key));
        }
        QVERIFY(m_env.areaOf(QStringLiteral("noSuchKey")).isEmpty());
    }

    // The ties core.json holds today, each settled by the group met first.
    void aTieInTheShippedPackGoesToTheGroupMetFirst()
    {
        QCOMPARE(m_env.areaOf(QStringLiteral("spineForwardBend")), QStringLiteral("setup"));
        QCOMPARE(m_env.areaOf(QStringLiteral("leadKneeFlexion")), QStringLiteral("lateral"));
        QCOMPARE(m_env.areaOf(QStringLiteral("shaftDirection")), QStringLiteral("setup"));
        QCOMPARE(m_env.areaOf(QStringLiteral("pelvisRotation")), QStringLiteral("sequence"));
    }

    void theWalkCountsOwnKeysAndKeepsFirstMetOrder()
    {
        pa::CharacteristicPack pack;
        pack.measures.push_back(measureOf("mMost", "mostUsed"));
        pack.measures.push_back(measureOf("mTie", "tied", { "hm.tied" }));      // a rung, so "hm.tied" gets no area
        pack.measures.push_back(measureOf("mFlight", "flightOnly"));
        pack.measures.push_back(measureOf("mNoKey", QString()));
        pack.signalDefs.push_back(signalOf("sMost", { "mMost" }));
        pack.signalDefs.push_back(signalOf("sTie", { "mTie" }));
        pack.signalDefs.push_back(signalOf("sBoth", { "mMost", "mTie" }));
        pack.signalDefs.push_back(signalOf("sFlight", { "mFlight", "mMost" }));
        pack.signalDefs.push_back(signalOf("sNoKey", { "mNoKey" }));

        using G = pa::ConditionGroup;
        // mostUsed: posture first, then impact twice. Ball flight's readings must not count toward it.
        pack.conditions.push_back(conditionOf("c1", G::Posture, { "sMost" }));
        pack.conditions.push_back(conditionOf("c2", G::Impact, { "sMost" }));
        pack.conditions.push_back(conditionOf("c3", G::Impact, { "sMost", "sBoth" }));  // reads mMost once
        pack.conditions.push_back(conditionOf("c4", G::BallFlight, { "sFlight" }));
        pack.conditions.push_back(conditionOf("c5", G::BallFlight, { "sFlight" }));
        pack.conditions.push_back(conditionOf("c6", G::BallFlight, { "sFlight" }));
        // tied: impact (c3) and finish once each, and setup, first in every other order there is, once.
        pack.conditions.push_back(conditionOf("c7", G::Finish, { "sTie" }));
        pack.conditions.push_back(conditionOf("c8", G::Setup, { "sTie" }));
        pack.conditions.push_back(conditionOf("c9", G::Setup, { "sNoKey" }));

        const QHash<QString, QString> areas = pa::metricAreasForPack(pack);
        QCOMPARE(areas.value("mostUsed"), QStringLiteral("impact"));
        QCOMPARE(areas.value("tied"), QStringLiteral("impact"));
        QVERIFY(!areas.contains("hm.tied"));
        QVERIFY(!areas.contains("flightOnly"));
        QCOMPARE(int(areas.size()), 2);
    }

    void aKeyWithNoNormHasNoSigma()
    {
        QVERIFY(std::isnan(m_env.normSigma(QStringLiteral("headTilt"), kP7, QStringLiteral("7 IRON"))));
        QVERIFY(std::isnan(m_env.normSigma(QStringLiteral("noSuchKey"), kP7, QStringLiteral("DRIVER"))));
    }

    void theClubChoosesTheNorm()
    {
        const auto at = [&](const char *key, const char *club) {
            return m_env.normSigma(QString::fromLatin1(key), kP7, QString::fromLatin1(club));
        };
        QCOMPARE(at("clubheadSpeed", "DRIVER"), 4.65);
        QCOMPARE(at("clubheadSpeed", "7 IRON"), 3.75);          // the family's row
        QCOMPARE(at("clubheadSpeed", "PITCHING WEDGE"), 3.4);
        QCOMPARE(at("clubheadSpeed", ""), 18.0);                // no club: the full swing
        QCOMPARE(at("lm.spinRate", "DRIVER"), 600.0);
        QCOMPARE(at("lm.spinRate", "7 IRON"), 1500.0);
        QCOMPARE(at("lm.spinRate", "PUTTER"), 2000.0);          // `any`, not a full swing's
    }

    void bothSidesOfTheNormCount()
    {
        QCOMPARE(m_env.normSigma(QStringLiteral("trunkRise"), kP7, QStringLiteral("7 IRON")), 4.5);     // 6 and 3
    }

    // m_leadWristAtTop is the preferred measure at P4 and carries no norm; the change from address beside it does.
    void aMeasureWithoutANormIsPassedOver()
    {
        QCOMPARE(m_env.normSigma(QStringLiteral("leadWristFlexExt"), kP4, QStringLiteral("7 IRON")), 11.0);
    }

    // The ladder, as measuresForMetricAtPhase joins it: this key is a rung of m_lowPointAhead, not its own key.
    void aRungOfTheLadderHasTheMeasuresNorm()
    {
        QCOMPARE(m_env.normSigma(QStringLiteral("lowPointFromAttack"), kP7, QStringLiteral("7 IRON")), 1.6);
    }

    void noMeasureAtThePhaseTakesTheMedianOfTheKey()
    {
        QCOMPARE(m_env.normSigma(QStringLiteral("thoraxRotation"), kP1, QStringLiteral("7 IRON")), 16.0);  // 12 and 20
        QCOMPARE(m_env.normSigma(QStringLiteral("clubheadSpeed"), kP1, QStringLiteral("DRIVER")), 4.65);
    }

    // Each has a posture_* row that differs from the row a club resolves.
    void postureContextsAreNeverRead()
    {
        QCOMPARE(m_env.normSigma(QStringLiteral("headLift"), kP4, QStringLiteral("7 IRON")), 3.0);          // not 4
        QCOMPARE(m_env.normSigma(QStringLiteral("spineForwardBend"), kP7, QStringLiteral("7 IRON")), 3.75); // not 3
    }

    void aSecondAskingAnswersTheSame()
    {
        const QString key = QStringLiteral("clubheadSpeed");
        QCOMPARE(m_env.normSigma(key, kP7, QStringLiteral("DRIVER")), m_env.normSigma(key, kP7, QStringLiteral("DRIVER")));
        QCOMPARE(consistencyEnvFromPacks().areaOf(key), m_env.areaOf(key));
    }
};

QTEST_GUILESS_MAIN(ConsistencyEnvTest)
#include "consistency_env_test.moc"
