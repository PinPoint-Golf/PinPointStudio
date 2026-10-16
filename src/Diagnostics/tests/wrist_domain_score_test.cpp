// The wrist score over the wrist conditions (wrist_domain_score.h, CONTRACT I2).
//
// Three things are gated here. The ARITHMETIC is the engine's (severity × rule weight × scale,
// clamped, banded at the engine's cut-offs), so the domain score reads the way the number it
// replaces read. The WEIGHT MAP carries the engine's own rule weights — held to the live rule table,
// so a re-weighted engine rule fails this test instead of quietly diverging. And the MEMBERSHIP: every
// condition the shipped pack tags `"domain": "wrist"` has a row in the map, and every row is a wrist
// condition the pack ships.
//
//   ctest -R wrist_domain_score --output-on-failure

#include "../engine/wrist_domain_score.h"
#include "../packs/characteristic_pack.h"
#include "../../Wrist/assessment_rules.h"

#include <QFile>
#include <QSet>

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static WristDomainVerdict v(const char *id, const char *tier, bool fired = false, double conf = 1.0)
{
    WristDomainVerdict out;
    out.conditionId = QString::fromLatin1(id);
    out.label       = QString::fromLatin1(id);
    out.tier        = QString::fromLatin1(tier);
    out.fired       = fired;
    out.confidence  = conf;
    return out;
}

int main()
{
    namespace tun = pinpoint::tuned::rules;

    std::printf("=== nothing read is not assessable ===\n");
    {
        const WristDomainScoreResult r = scoreWristDomain({});
        check(r.score == -1, "no verdicts -> score -1, never 100");
        check(r.band.isEmpty(), "…and no band");
        check(r.contributions.empty(), "…and nothing contributed");
    }

    std::printf("=== clean costs nothing ===\n");
    {
        const WristDomainScoreResult r =
            scoreWristDomain({ v("cupped_at_top", "clean"), v("casting", "clean") });
        check(r.score == 100 && r.base == 100, "all clean -> 100");
        check(r.band == QLatin1String("good"), "…banded good");
        check(r.contributions.empty(), "…with no contributions");
    }

    std::printf("=== the engine's arithmetic ===\n");
    {
        // A pattern weighs as the engine's Fault: 1.0 × 0.8 (open_face_top's weight) × 18 = 14.4.
        const WristDomainScoreResult p = scoreWristDomain({ v("cupped_at_top", "pattern") });
        check(p.score == 86, "cupped_at_top pattern -> 100 - 14.4 = 86");
        check(p.contributions.size() == 1
                  && std::fabs(p.contributions[0].penalty
                               - tun::kSeverityWeightFault * 0.8 * tun::kScoreScale) < 1e-9,
              "…its penalty is severity x rule weight x scale");

        // Watching weighs as the engine's Watch: 0.5 × 1.0 (cast) × 18 = 9.
        const WristDomainScoreResult w = scoreWristDomain({ v("casting", "watching") });
        check(w.score == 91, "casting watching -> 91");

        // A firing on this shot the session has not made a pattern of is a Watch too.
        const WristDomainScoreResult f = scoreWristDomain({ v("scooping", "clean", true) });
        check(f.score == 92, "scooping fired on a clean tier -> watch weight, 100 - 8.1 = 92");

        // Confidence rides along and never moves the central value (engine validation B2).
        const WristDomainScoreResult lo = scoreWristDomain({ v("casting", "watching", false, 0.2) });
        check(lo.score == w.score, "confidence does not move the score");

        // Unknown condition -> the default weight, so a later domain member still scores.
        const WristDomainScoreResult u = scoreWristDomain({ v("some_new_wrist_fault", "pattern") });
        check(u.score == int(std::lround(100 - kWristDomainDefaultWeight * tun::kScoreScale)),
              "a condition missing from the map takes kWristDomainDefaultWeight");
    }

    std::printf("=== contributions, clamping and bands ===\n");
    {
        const WristDomainScoreResult r = scoreWristDomain({
            v("insufficient_set", "watching"),   // 4.5
            v("casting", "pattern"),             // 18
            v("cupped_at_top", "pattern"),       // 14.4
            v("bowed_at_top", "clean") });
        check(r.contributions.size() == 3, "only the penalised verdicts contribute");
        check(r.contributions.size() == 3
                  && r.contributions[0].id == QLatin1String("casting")
                  && r.contributions[1].id == QLatin1String("cupped_at_top")
                  && r.contributions[2].id == QLatin1String("insufficient_set"),
              "contributions are listed largest first");
        check(r.score == 63 && r.band == QLatin1String("watch"), "100 - 36.9 = 63, banded watch");

        std::vector<WristDomainVerdict> all;
        for (const WristDomainWeight &w : wristDomainWeights())
            all.push_back(v(w.conditionId, "pattern"));
        const WristDomainScoreResult z = scoreWristDomain(all);
        check(z.score == 0 && z.band == QLatin1String("fault"), "every condition a pattern clamps at 0");

        check(wristDomainBand(75) == QLatin1String("good") && wristDomainBand(74) == QLatin1String("watch")
                  && wristDomainBand(50) == QLatin1String("watch") && wristDomainBand(49) == QLatin1String("weak")
                  && wristDomainBand(25) == QLatin1String("weak") && wristDomainBand(24) == QLatin1String("fault"),
              "the bands cut at 75 / 50 / 25, as WristDiagnosticsModel::scoreBand does");
    }

    std::printf("=== the weight map IS the engine's rule table ===\n");
    {
        const AssessmentRuleRegistry reg = AssessmentRuleRegistry::makeDefault();
        int drift = 0, missing = 0;
        QSet<QString> seen;
        for (const WristDomainWeight &w : wristDomainWeights()) {
            if (seen.contains(QString::fromLatin1(w.conditionId))) {
                ++drift;
                std::printf("        '%s' has two rows\n", w.conditionId);
            }
            seen.insert(QString::fromLatin1(w.conditionId));
            const RuleDef *def = nullptr;
            for (const auto &rule : reg.rules())
                if (rule->def().id == QLatin1String(w.engineRuleId)) def = &rule->def();
            if (!def) {
                ++missing;
                std::printf("        '%s' cites engine rule '%s', which does not exist\n",
                            w.conditionId, w.engineRuleId);
                continue;
            }
            if (std::fabs(def->weight - w.weight) > 1e-9) {
                ++drift;
                std::printf("        '%s' weighs %.2f, engine rule '%s' weighs %.2f\n",
                            w.conditionId, w.weight, w.engineRuleId, def->weight);
            }
        }
        check(missing == 0, "every row cites an engine rule that exists");
        check(drift == 0, "every row carries that rule's weight, once");

        // Every engine FAULT rule (weight > 0; the strengths weigh 0) has an heir in the domain.
        int orphaned = 0;
        for (const auto &rule : reg.rules()) {
            if (rule->def().weight <= 0.0) continue;
            bool heir = false;
            for (const WristDomainWeight &w : wristDomainWeights())
                if (rule->def().id == QLatin1String(w.engineRuleId)) heir = true;
            if (!heir) {
                ++orphaned;
                std::printf("        engine rule '%s' has no condition in the wrist domain\n",
                            qPrintable(rule->def().id));
            }
        }
        check(orphaned == 0, "every engine fault rule folded into a wrist condition");
    }

    std::printf("=== the map and the shipped pack agree on the domain ===\n");
    {
        QFile f(QStringLiteral(PP_CORE_PACK_PATH));
        check(f.open(QIODevice::ReadOnly), "the shipped pack is readable");
        const PackLoadResult res = loadPack(f.readAll(), QStringLiteral("core.json"));
        check(res.parsed && res.report.errorCount() == 0, "…and loads without errors");

        QSet<QString> tagged, mapped;
        for (const Condition &c : res.pack.conditions)
            if (c.domain == QLatin1String("wrist")) tagged.insert(c.id);
        for (const WristDomainWeight &w : wristDomainWeights())
            mapped.insert(QString::fromLatin1(w.conditionId));

        for (const QString &id : tagged - mapped)
            std::printf("        '%s' is in the wrist domain with no weight row\n", qPrintable(id));
        for (const QString &id : mapped - tagged)
            std::printf("        '%s' has a weight row but is not in the wrist domain\n", qPrintable(id));
        check(tagged.size() == 15, "the pack ships fifteen wrist conditions (CONTRACT I1)");
        check(tagged == mapped, "every wrist condition has exactly one weight row, and nothing else does");
    }

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
