// THE ONE PATH FROM A GOLFER'S HISTORY TO A FINDING (docs/design/shot_outcomes_design.md §3.3).
//
// m_carryShortfall and m_ballSpeedShortfall read the club baseline; every other measure must not.
// That is the containment the design rests on — "per-athlete norms out of scope" holds everywhere
// except these two — so it is asserted across the whole shipped pack, not assumed.

#include "../measures/live_measure_source.h"
#include "../packs/pack_provider.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "ok" : "FAIL", label);
    if (!c) ++g_fail;
}

// Records whether, and for which club, it was read.
class SpyBaseline final : public IClubBaselineProvider {
public:
    explicit SpyBaseline(ClubBaselineEntry e) : m_entry(std::move(e)) {}
    std::optional<ClubBaselineEntry> forClub(const QString &club) const override
    {
        ++reads;
        lastClub = club;
        return m_entry;
    }
    mutable int     reads = 0;
    mutable QString lastClub;

private:
    ClubBaselineEntry m_entry;
};

static MetricPhaseGrid impactMetric(const QString &key, double value)
{
    MetricPhaseGrid g;
    g.key = key;
    PhaseGridValue v;
    v.phase = Phase::Impact;
    v.value = value;
    g.values.push_back(v);
    return g;
}

static SwingPhaseGrid monitorGrid()
{
    SwingPhaseGrid g;
    g.swingDir = QStringLiteral("test");
    g.club     = QStringLiteral("7 IRON");
    g.metrics.push_back(impactMetric(QStringLiteral("lm.carryDistance"), 130.0));
    g.metrics.push_back(impactMetric(QStringLiteral("lm.ballSpeed"), 100.0));
    return g;
}

static ClubBaselineEntry earnedEntry()
{
    ClubBaselineEntry e;
    e.club          = QStringLiteral("7 IRON");
    e.carryMean     = 150.0;
    e.carrySd       = 8.0;
    e.ballSpeedMean = 112.0;
    e.ballSpeedSd   = 4.0;
    e.n             = kClubBaselineMinShots;
    return e;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("club_baseline_provider_test\n");

    const std::unique_ptr<ICharacteristicPackProvider> packProv = makeResourcePackProvider();
    const CharacteristicPack                          &pack     = packProv->pack();

    const QString carryId = QStringLiteral("m_carryShortfall");
    const QString speedId = QStringLiteral("m_ballSpeedShortfall");

    // ── The factory ─────────────────────────────────────────────────────────
    {
        QHash<QString, ClubBaselineEntry> table;
        table.insert(QStringLiteral("7 IRON"), earnedEntry());
        const auto provider = makeClubBaselineProvider(table);
        check(provider->forClub(QStringLiteral("7 IRON")).has_value(), "a club in the table is found");
        check(!provider->forClub(QStringLiteral("9 IRON")).has_value(), "a club not in the table is not");
    }

    // ── z = (mean − actual) / sd, + is short ────────────────────────────────
    {
        LiveMeasureSource src(monitorGrid(), pack);
        src.setClubBaselineProvider(std::make_shared<SpyBaseline>(earnedEntry()));

        const auto carry = src.value(carryId);
        check(carry.has_value() && std::fabs(carry->value - 2.5) < 1e-9, "carry is 2.5 SDs short");
        check(src.missingKind(carryId) == MissingKind::None, "a read has no missing reason");
        const auto speed = src.value(speedId);
        check(speed.has_value() && std::fabs(speed->value - 3.0) < 1e-9, "ball speed is 3 SDs short");
    }

    // ── The baseline is keyed by canonical club id, whatever the shot declared ──
    {
        SwingPhaseGrid g = monitorGrid();
        g.club = QStringLiteral("7-iron");
        const auto spy = std::make_shared<SpyBaseline>(earnedEntry());
        LiveMeasureSource src(std::move(g), pack);
        src.setClubBaselineProvider(spy);
        (void)src.value(carryId);
        check(spy->lastClub == QStringLiteral("7 IRON"), "a declared 7-iron asks for the canonical 7 IRON");
    }

    // ── The shortfall is unreadable until the club has earned it ────────────
    {
        const auto expectNotEarned = [&](const std::shared_ptr<IClubBaselineProvider> &p, const char *label) {
            LiveMeasureSource src(monitorGrid(), pack);
            src.setClubBaselineProvider(p);
            const bool absent = !src.value(carryId).has_value();
            check(absent && src.missingKind(carryId) == MissingKind::ClubBaselineNotEarned, label);
            const LiveMeasureSource::GateCause gc = src.gateCause(carryId);
            check(gc.gate == QStringLiteral("rule.diag.clubBaselineMinShots") && gc.subject.isEmpty() && !gc.clause.isEmpty(),
                  "the row names the gate rule, with no per-swing subject");
        };

        LiveMeasureSource noProvider(monitorGrid(), pack);
        check(!noProvider.value(carryId).has_value()
                  && noProvider.missingKind(carryId) == MissingKind::ClubBaselineNotEarned,
              "no provider: unreadable, not zero");

        ClubBaselineEntry fewShots = earnedEntry();
        fewShots.n = kClubBaselineMinShots - 1;
        expectNotEarned(std::make_shared<SpyBaseline>(fewShots), "too few shots with this club: unreadable");

        ClubBaselineEntry noSpread = earnedEntry();
        noSpread.carrySd = 0.0;
        expectNotEarned(std::make_shared<SpyBaseline>(noSpread), "a zero spread: unreadable, never a division by zero");

        ClubBaselineEntry noMean = earnedEntry();
        noMean.carryMean = std::numeric_limits<double>::quiet_NaN();
        expectNotEarned(std::make_shared<SpyBaseline>(noMean), "an absent mean: unreadable");

        QHash<QString, ClubBaselineEntry> table;
        table.insert(QStringLiteral("9 IRON"), earnedEntry());
        expectNotEarned(makeClubBaselineProvider(table), "a club with no entry: unreadable");
    }

    // ── A shot the monitor did not report says so, whatever the baseline holds ──
    {
        SwingPhaseGrid g;
        g.swingDir = QStringLiteral("test");
        g.club     = QStringLiteral("7 IRON");
        g.metrics.push_back(impactMetric(QStringLiteral("pelvisRotation"), 12.0));
        LiveMeasureSource src(std::move(g), pack);
        src.setClubBaselineProvider(std::make_shared<SpyBaseline>(earnedEntry()));
        check(!src.value(carryId).has_value() && src.missingKind(carryId) == MissingKind::NoLaunchMonitor,
              "no launch monitor on this shot: that is the reason, not the baseline");
    }

    // ── No other measure reads the provider ─────────────────────────────────
    {
        const auto spy = std::make_shared<SpyBaseline>(earnedEntry());
        LiveMeasureSource src(monitorGrid(), pack);
        src.setClubBaselineProvider(spy);

        const QSet<QString> allowed{ carryId, speedId };
        int strays = 0;
        for (const Measure &m : pack.measures) {
            const int before = spy->reads;
            (void)src.value(m.id);
            if (spy->reads != before && !allowed.contains(m.id)) {
                ++strays;
                std::printf("        '%s' read the club baseline\n", qPrintable(m.id));
            }
        }
        check(strays == 0, "no measure other than the two shortfalls reads the club baseline");
        check(spy->reads == 2, "and each of the two reads it once");
        check(spy->lastClub == QStringLiteral("7 IRON"), "asked about the shot's club");

        for (const Measure &m : pack.measures)
            if (m.metricKey.startsWith(QLatin1String("history.")) && !allowed.contains(m.id))
                check(false, "only the two shortfalls live in the history namespace");
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
