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

// The outcome scope (outcome_scope.h): its keys, which shots a choice holds (the Tendencies rule, shared),
// the counts that set a fault on the chosen shots against the other shots, and the words for them.

#include "../../Coaching/outcome_scope.h"

#include <cstdio>
#include <limits>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static ShotConditionFacts facts(const char *id, const char *family, const char *shortText, ShotConditionLayer layer, int order)
{
    ShotConditionFacts f;
    f.id          = QString::fromLatin1(id);
    f.family      = QString::fromLatin1(family);
    f.label       = QString::fromLatin1(shortText);
    f.golferShort = QString::fromLatin1(shortText);
    f.golfer      = QString::fromLatin1(shortText).toLower();
    f.layer       = layer;
    f.packOrder   = order;
    return f;
}

static ShotOutcomePack fixturePack()
{
    ShotOutcomePack p;
    using L = ShotConditionLayer;
    int order = 0;
    const std::vector<ShotConditionFacts> list{
        facts("chunk",           "strike",    "Fat",             L::Ball,  order++),
        facts("slice",           "direction", "Slice",           L::Ball,  order++),
        facts("hook",            "direction", "Hook",            L::Ball,  order++),
        facts("push",            "direction", "Push",            L::Ball,  order++),
        facts("push_slice",      "direction", "Push-slice",      L::Ball,  order++),
        facts("pull",            "direction", "Pull",            L::Ball,  order++),
        facts("pull_hook",       "direction", "Pull-hook",       L::Ball,  order++),
        facts("carry_deficit",   "flight",    "Short",           L::Ball,  order++),
        facts("over_the_top",    "",          "Over the top",    L::Swing, order++),
        facts("early_extension", "",          "Early extension", L::Swing, order++),
    };
    for (const ShotConditionFacts &f : list) p.conditions.insert(f.id, f);
    p.physics[QStringLiteral("chunk")] << QStringLiteral("carry_deficit");
    p.causes[QStringLiteral("over_the_top")] << QStringLiteral("slice");
    p.causes[QStringLiteral("early_extension")] << QStringLiteral("chunk");
    return p;
}

// A driver shot with a reading: `fired` fire, `unread` couldn't be assessed, every other row is clean.
static ShotRecord shot(int id, std::initializer_list<const char *> fired, std::initializer_list<const char *> unread = {})
{
    ShotRecord s;
    s.shotId = id;
    s.club   = QStringLiteral("DRIVER");
    s.carry  = 200.0;
    s.swingLength = QStringLiteral("full");
    const auto in = [](std::initializer_list<const char *> list, const char *rid) {
        return std::any_of(list.begin(), list.end(), [rid](const char *f) { return qstrcmp(f, rid) == 0; });
    };
    for (const char *rid : { "chunk", "slice", "hook", "push", "push_slice", "carry_deficit", "over_the_top", "early_extension" }) {
        ConditionRow r;
        r.conditionId = QString::fromLatin1(rid);
        r.state = in(fired, rid) ? ShotState::Fired : in(unread, rid) ? ShotState::NotAssessable : ShotState::Clean;
        r.firedSignalCount = r.state == ShotState::Fired ? 1 : 0;
        s.rows.push_back(r);
    }
    return s;
}

static TendencySessionInput sessionOf(std::vector<ShotRecord> shots)
{
    TendencySessionInput s;
    s.session.day    = QDate(2026, 11, 3);
    s.session.folder = s.session.day.toString(Qt::ISODate) + QStringLiteral("_M_01");
    s.session.shots  = std::move(shots);
    return s;
}

static ShotJudgement judgeOne(const ShotRecord &r, const ShotOutcomePack &pack, const QString &intent = QString())
{
    TendencySessionInput s = sessionOf({ r });
    if (!intent.isEmpty()) s.intents.insert(r.shotId, intent);
    return judgedShotOutcomes(s, pack).value(r.shotId);
}

static std::vector<int> idsOf(const std::vector<ShotRecord> &shots)
{
    std::vector<int> out;
    for (const ShotRecord &s : shots) out.push_back(s.shotId);
    return out;
}

int main()
{
    std::printf("outcome_scope_test\n");
    const ShotOutcomePack pack = fixturePack();

    std::printf("\nthe keys:\n");
    {
        check(outcomeScopeAllKey() == QStringLiteral("all") && outcomeScopeGoodKey() == QStringLiteral("good")
                  && outcomeScopeMissKey(QStringLiteral("slice")) == QStringLiteral("miss:slice"), "all, good, miss:<id>");
        check(OutcomeScope().isAll() && OutcomeScope().key == QStringLiteral("all"), "a scope is All until it's told otherwise");
        const OutcomeScope good = outcomeScopeFromKey(QStringLiteral("good"));
        check(!good.isAll() && good.good && good.missId.isEmpty() && good.key == QStringLiteral("good"), "good is the clean shots");
        const OutcomeScope slice = outcomeScopeFromKey(QStringLiteral("miss:slice"));
        check(!slice.isAll() && !slice.good && slice.missId == QStringLiteral("slice") && slice.key == QStringLiteral("miss:slice"),
              "a miss is its id");
        check(outcomeScopeFromKey(QStringLiteral("miss:push_slice")).missId == QStringLiteral("push_slice"), "an id with an underscore");
        bool roundTrips = true;
        for (const char *k : { "all", "good", "miss:slice", "miss:carry_deficit" })
            roundTrips = roundTrips && outcomeScopeFromKey(outcomeScopeFromKey(QString::fromLatin1(k)).key).key == QString::fromLatin1(k);
        check(roundTrips, "every key reads back as itself");
        bool allMalformed = true;
        for (const char *k : { "", "miss:", "miss: slice", "miss:slice!", "Good", "ALL", "bogus", "group:mid_irons", "slice" })
            allMalformed = allMalformed && outcomeScopeFromKey(QString::fromLatin1(k)).isAll()
                           && outcomeScopeFromKey(QString::fromLatin1(k)).key == QStringLiteral("all");
        check(allMalformed, "a key that names nothing reads as All");
    }

    std::printf("\nwhich shots a choice holds:\n");
    {
        const ShotJudgement fatSlice = judgeOne(shot(1, { "chunk", "slice", "carry_deficit" }), pack);
        check(fatSlice.judged && !fatSlice.good && fatSlice.missIds == QStringList{ "chunk", "slice" }
                  && fatSlice.missLabels == QStringList{ "Fat", "Slice" },
              "a fat slice carries both misses, the strike first; the short the fat explains folds in");
        check(outcomeScopeFromKey(QStringLiteral("miss:chunk")).contains(fatSlice)
                  && outcomeScopeFromKey(QStringLiteral("miss:slice")).contains(fatSlice)
                  && !outcomeScopeFromKey(QStringLiteral("miss:carry_deficit")).contains(fatSlice)
                  && !outcomeScopeFromKey(QStringLiteral("good")).contains(fatSlice),
              "so it is under Fat and under Slice, not Short, not Good");

        const ShotJudgement pushSlice = judgeOne(shot(2, { "push", "slice", "push_slice" }), pack);
        check(pushSlice.judged && pushSlice.missIds == QStringList{ "push_slice", "slice", "push" }
                  && pushSlice.missLabels == QStringList{ "Push-slice", "Slice", "Push" },
              "a push slice is under Push slice, Slice and Push, the combined label first");
        check(outcomeScopeFromKey(QStringLiteral("miss:push_slice")).contains(pushSlice)
                  && outcomeScopeFromKey(QStringLiteral("miss:slice")).contains(pushSlice)
                  && outcomeScopeFromKey(QStringLiteral("miss:push")).contains(pushSlice)
                  && !outcomeScopeFromKey(QStringLiteral("miss:hook")).contains(pushSlice),
              "so the Slice choice holds it");
        const ShotJudgement onlyCompound = judgeOne(shot(12, { "push_slice" }), pack);
        check(onlyCompound.missIds == QStringList{ "push_slice", "slice", "push" },
              "the halves come with the combined label even when their own rows didn't fire");

        ShotRecord leftPushSlice = shot(13, { "push", "slice", "push_slice" });
        leftPushSlice.handedness = QStringLiteral("Left");
        const ShotJudgement leftCompound = judgeOne(leftPushSlice, pack);
        check(leftCompound.judged && leftCompound.missIds == QStringList{ "pull_hook", "hook", "pull" }
                  && leftCompound.missLabels == QStringList{ "Pull-hook", "Hook", "Pull" },
              "a left-hander's push slice row is their pull hook, and its halves are their hook and pull");

        const ShotJudgement clean = judgeOne(shot(3, {}), pack);
        check(clean.judged && clean.good && clean.missIds.isEmpty(), "a clean shot is good");
        check(outcomeScopeFromKey(QStringLiteral("good")).contains(clean) && !outcomeScopeFromKey(QStringLiteral("miss:slice")).contains(clean),
              "and only under Good");

        ShotRecord left = shot(4, { "slice" });
        left.handedness = QStringLiteral("Left");
        const ShotJudgement lefty = judgeOne(left, pack);
        check(lefty.judged && lefty.missIds == QStringList{ "hook" } && lefty.missLabels == QStringList{ "Hook" },
              "a left-hander's slice row reads as the hook they see");
        check(outcomeScopeFromKey(QStringLiteral("miss:hook")).contains(lefty) && !outcomeScopeFromKey(QStringLiteral("miss:slice")).contains(lefty),
              "and the key is the glossed id");

        const ShotJudgement faded = judgeOne(shot(5, { "slice" }), pack, QStringLiteral("fade"));
        check(!faded.judged, "a shape the golfer meant to hit is not judged");
        check(judgeOne(shot(6, { "slice" }), pack, QStringLiteral("straight")).judged, "an explicit straight intent is");
        ShotRecord noLm = shot(7, { "slice" });
        noLm.carry = std::numeric_limits<double>::quiet_NaN();
        const ShotJudgement unread = judgeOne(noLm, pack);
        check(!unread.judged, "a shot with no launch monitor reading is not judged");
        const ShotJudgement noShape = judgeOne(shot(8, {}, { "slice", "hook", "push", "push_slice" }), pack);
        check(!noShape.judged, "a shot whose direction couldn't be read is not judged");

        check(OutcomeScope().contains(faded) && OutcomeScope().contains(unread) && OutcomeScope().contains(fatSlice),
              "All holds every shot, judged or not");
        check(!outcomeScopeFromKey(QStringLiteral("good")).contains(unread) && !outcomeScopeFromKey(QStringLiteral("miss:slice")).contains(faded),
              "a choice holds only judged shots");

        TendencySessionInput corrected = sessionOf({ shot(9, { "slice" }) });
        corrected.session.corrections[9].push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("hook") });
        check(judgedShotOutcomes(corrected, pack).value(9).missIds == QStringList{ "hook" }, "the golfer's correction counts");
    }

    std::printf("\na session's shots, and Tendencies agreeing:\n");
    // 1-4 slice with over the top; 5 a slice that couldn't see over the top; 6-9 good, over the top on 6;
    // 10 an intended fade; 11 no reading. Both of the last fire over the top.
    std::vector<ShotRecord> shots;
    for (int id = 1; id <= 4; ++id) shots.push_back(shot(id, { "slice", "over_the_top" }));
    shots.push_back(shot(5, { "slice" }, { "over_the_top" }));
    shots.push_back(shot(6, { "over_the_top" }));
    for (int id = 7; id <= 9; ++id) shots.push_back(shot(id, {}));
    shots.push_back(shot(10, { "slice", "over_the_top" }));
    ShotRecord noReading = shot(11, { "over_the_top" });
    noReading.carry = std::numeric_limits<double>::quiet_NaN();
    shots.push_back(noReading);
    TendencySessionInput session = sessionOf(shots);
    session.intents.insert(10, QStringLiteral("fade"));
    const QHash<int, ShotJudgement> judged = judgedShotOutcomes(session, pack);
    {
        check(judged.size() == 11, "every shot is there, judged or not");
        int judgedCount = 0;
        for (const ShotJudgement &j : judged) if (j.judged) ++judgedCount;
        const TendencySet set = reduceTendencies({ session }, pack, [](const QString &) { return QStringLiteral("driver"); });
        check(judgedCount == 9 && set.countedShots == judgedCount, "the same shots count as in Tendencies");
        check(set.clubOutcomes.size() == 1 && set.clubOutcomes[0].good == 4 && set.clubOutcomes[0].misses.size() == 1
                  && set.clubOutcomes[0].misses[0].id == QStringLiteral("slice") && set.clubOutcomes[0].misses[0].shots == 5,
              "and Tendencies' counts are the choices' sizes");

        check(idsOf(shotsInOutcomeScope(shots, judged, OutcomeScope())).size() == 11, "All sees every shot");
        check(idsOf(shotsInOutcomeScope(shots, judged, outcomeScopeFromKey(QStringLiteral("miss:slice")))) == std::vector<int>{ 1, 2, 3, 4, 5 },
              "Slice sees the slices, in ledger order, not the intended fade");
        check(idsOf(shotsInOutcomeScope(shots, judged, outcomeScopeFromKey(QStringLiteral("good")))) == std::vector<int>{ 6, 7, 8, 9 },
              "Good sees the clean shots, not the one with no reading");
        check(shotsInOutcomeScope(shots, judged, outcomeScopeFromKey(QStringLiteral("miss:hook"))).empty(), "a miss not hit sees nothing");
        check(idsOf(shotsInOutcomeScope(shots, {}, outcomeScopeFromKey(QStringLiteral("miss:slice")))).empty(),
              "a shot with no judgement is not judged");
    }

    std::printf("\nthe chosen shots against the other shots:\n");
    {
        const QSet<QString> ids{ QStringLiteral("over_the_top"), QStringLiteral("early_extension"), QStringLiteral("hook") };
        const QHash<QString, OutcomeContrast> slices = outcomeContrasts(shots, judged, outcomeScopeFromKey(QStringLiteral("miss:slice")), ids);
        const OutcomeContrast ott = slices.value(QStringLiteral("over_the_top"));
        check(ott.a == 4 && ott.n1 == 4, "on the slices: fired on 4 of the 4 that could see it");
        check(ott.b == 1 && ott.n0 == 4, "on the other shots: 1 of 4; the fade and the unread shot in neither");
        const OutcomeContrast ee = slices.value(QStringLiteral("early_extension"));
        check(ee.a == 0 && ee.n1 == 5 && ee.b == 0 && ee.n0 == 4, "a fault never seen still counts the shots that assessed it");
        check(slices.size() == 3 && !slices.contains(QStringLiteral("slice")), "only the conditions asked for");

        const OutcomeContrast goodOtt = outcomeContrasts(shots, judged, outcomeScopeFromKey(QStringLiteral("good")), ids)
                                            .value(QStringLiteral("over_the_top"));
        check(goodOtt.a == 1 && goodOtt.n1 == 4 && goodOtt.b == 4 && goodOtt.n0 == 4, "Good is set against the misses");
        check(outcomeContrasts(shots, judged, OutcomeScope(), ids).isEmpty(), "All has nothing to set against");

        OutcomeContrast sum = ott;
        sum += goodOtt;
        check(sum.a == 5 && sum.n1 == 8 && sum.b == 5 && sum.n0 == 8, "counts add up across sessions");
    }

    std::printf("\nthe words:\n");
    {
        const QString slice = QStringLiteral("Slice");
        check(outcomeContrastLine(OutcomeContrast{ 34, 38, 12, 61 }, slice) == QStringLiteral("On your slice shots 34 of 38 · other shots 12 of 61"),
              "more often on the chosen shots: the counts alone");
        check(outcomeContrastLine(OutcomeContrast{ 7, 20, 4, 20 }, slice) == QStringLiteral("On your slice shots 7 of 20 · other shots 4 of 20"),
              "exactly 15 points more is more often");
        check(outcomeContrastLine(OutcomeContrast{ 9, 10, 55, 61 }, slice)
                  == QStringLiteral("On your slice shots 9 of 10 · other shots 55 of 61 — just as often on your other shots"),
              "within 15 points: just as often");
        check(outcomeContrastLine(OutcomeContrast{ 6, 20, 4, 20 }, slice).endsWith(QStringLiteral(" — just as often on your other shots")),
              "10 points more is still just as often");
        check(outcomeContrastLine(OutcomeContrast{ 2, 10, 40, 60 }, slice)
                  == QStringLiteral("On your slice shots 2 of 10 · other shots 40 of 60 — less often than on your other shots"),
              "15 points or more less: less often");
        check(outcomeContrastLine(OutcomeContrast{ 5, 6, 1, 7 }, slice)
                  == QStringLiteral("On your slice shots 5 of 6 · other shots 1 of 7 — too few other shots to compare"),
              "under 8 other shots: too few to compare");
        check(outcomeContrastLine(OutcomeContrast{ 8, 8, 0, 8 }, slice) == QStringLiteral("On your slice shots 8 of 8 · other shots 0 of 8"),
              "8 other shots are enough");
        check(outcomeContrastLine(OutcomeContrast{ 0, 0, 12, 61 }, slice).isEmpty(), "no chosen shot assessed it: no line");
        check(outcomeContrastLine(OutcomeContrast{ 3, 4, 10, 40 }, QStringLiteral("Good")).startsWith(QStringLiteral("On your good shots 3 of 4")),
              "Good reads as your good shots");
        TendencyOptions strict;
        strict.trendDelta = 0.8;        // 89% against 20% is under 80 points
        check(outcomeContrastLine(OutcomeContrast{ 34, 38, 12, 61 }, slice, strict).endsWith(QStringLiteral("just as often on your other shots")),
              "the rule is Tendencies' own option");
    }

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
