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

// The shot filter (outcome_scope.h): its keys, which shots a choice holds (the Tendencies rule, shared, factor by
// factor), the counts that set a fault on the chosen shots against the other shots, and the words for them.

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
        facts("fade",            "direction", "Fade",            L::Ball,  order++),
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
    for (const char *rid : { "chunk", "slice", "hook", "push", "push_slice", "fade", "carry_deficit", "over_the_top", "early_extension" }) {
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

static ShotFactors judgeOne(const ShotRecord &r, const ShotOutcomePack &pack, const QString &intent = QString())
{
    TendencySessionInput s = sessionOf({ r });
    if (!intent.isEmpty()) s.intents.insert(r.shotId, intent);
    return judgedShotFactors(s, pack, FlightNorms{}).value(r.shotId);
}

// No reading at all: the monitor's strike height went with it, so the strike row couldn't be read either.
static ShotRecord noReadingShot(int id, std::initializer_list<const char *> fired)
{
    ShotRecord s = shot(id, fired, { "chunk" });
    s.carry = std::numeric_limits<double>::quiet_NaN();
    return s;
}

static ShotFilter filter(const char *key) { return shotFilterFromKey(QString::fromLatin1(key)); }

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
        check(shotFilterAllKey() == QStringLiteral("all") && ShotFilter().isAll() && ShotFilter().key() == QStringLiteral("all"),
              "a filter is All until it's told otherwise");
        const ShotFilter pullThin = filter("strike:thin|direction:pull|flight:any");
        check(!pullThin.isAll() && pullThin.strike == QStringLiteral("thin") && pullThin.direction == QStringLiteral("pull")
                  && pullThin.flight == QStringLiteral("any") && pullThin.key() == QStringLiteral("strike:thin|direction:pull|flight:any"),
              "three parts, any of them any");
        check(filter("direction:push_slice").direction == QStringLiteral("push_slice")
                  && filter("direction:push_slice").key() == QStringLiteral("strike:any|direction:push_slice|flight:any"),
              "a part left out is Any, and the key says all three in order");
        check(filter("flight:high|strike:solid").key() == QStringLiteral("strike:solid|direction:any|flight:high"), "in any order on the way in");
        check(filter("strike:any|direction:any|flight:any").isAll() && filter("strike:any|direction:any|flight:any").key() == QStringLiteral("all"),
              "every part Any is All");
        bool roundTrips = true;
        for (const char *k : { "all", "strike:thin|direction:pull|flight:any", "strike:any|direction:any|flight:sky" })
            roundTrips = roundTrips && filter(filter(k).key().toLatin1().constData()).key() == QString::fromLatin1(k);
        check(roundTrips, "every key reads back as itself");
        bool allMalformed = true;
        for (const char *k : { "", "good", "miss:slice", "strike:", "strike: thin", "strike:thin!", "spin:high", "strike:thin|strike:top",
                               "direction:pull|", "ALL", "group:mid_irons" })
            allMalformed = allMalformed && filter(k).isAll() && filter(k).key() == QStringLiteral("all");
        check(allMalformed, "a key that names nothing, or an old one, reads as All");
    }

    std::printf("\nwhich shots a choice holds:\n");
    {
        const ShotFactors fatSlice = judgeOne(shot(1, { "chunk", "slice", "carry_deficit" }), pack);
        check(fatSlice.judged() && fatSlice.strike.values == QStringList{ "chunk" } && fatSlice.direction.values == QStringList{ "slice" }
                  && !fatSlice.flight.read,
              "a fat slice: Fat and Slice; with no table its flight is not read");
        check(filter("strike:chunk").contains(fatSlice) && filter("direction:slice").contains(fatSlice)
                  && filter("strike:chunk|direction:slice").contains(fatSlice) && !filter("strike:solid").contains(fatSlice)
                  && !filter("direction:straight").contains(fatSlice),
              "so it is under Fat, under Slice and under both; not Solid, not Straight");
        check(!filter("flight:mid").contains(fatSlice) && !filter("strike:chunk|flight:mid").contains(fatSlice),
              "a factor not read fails any choice for it, and AND fails with it");

        const ShotFactors pushSlice = judgeOne(shot(2, { "push", "slice", "push_slice" }), pack);
        check(filter("direction:push_slice").contains(pushSlice) && filter("direction:slice").contains(pushSlice)
                  && filter("direction:push").contains(pushSlice) && !filter("direction:hook").contains(pushSlice),
              "a push slice is under Push slice, Slice and Push");
        const ShotFactors onlyCompound = judgeOne(shot(12, { "push_slice" }), pack);
        check(filter("direction:slice").contains(onlyCompound) && filter("direction:push").contains(onlyCompound),
              "the halves come with the combined label even when their own rows didn't fire");

        ShotRecord leftPushSlice = shot(13, { "push", "slice", "push_slice" });
        leftPushSlice.handedness = QStringLiteral("Left");
        const ShotFactors leftCompound = judgeOne(leftPushSlice, pack);
        check(filter("direction:pull_hook").contains(leftCompound) && filter("direction:hook").contains(leftCompound)
                  && filter("direction:pull").contains(leftCompound) && !filter("direction:slice").contains(leftCompound),
              "a left-hander's push slice row is their pull hook, and its halves are their hook and pull");

        const ShotFactors clean = judgeOne(shot(3, {}), pack);
        check(clean.judged() && filter("strike:solid|direction:straight").contains(clean) && !filter("direction:slice").contains(clean),
              "a clean shot is Solid and Straight, and only there");

        ShotRecord left = shot(4, { "slice" });
        left.handedness = QStringLiteral("Left");
        const ShotFactors lefty = judgeOne(left, pack);
        check(filter("direction:hook").contains(lefty) && !filter("direction:slice").contains(lefty), "a left-hander's slice row reads as the hook they see");

        const ShotFactors faded = judgeOne(shot(5, { "fade" }), pack, QStringLiteral("fade"));
        check(faded.judged() && filter("direction:fade").contains(faded) && faded.direction.faults.isEmpty(),
              "a shape the golfer meant to hit is judged, as the Fade it is");
        const ShotFactors unread = judgeOne(noReadingShot(7, { "slice" }), pack);
        check(!unread.judged(), "a shot with no reading and no strike read is not judged");
        const ShotFactors noShape = judgeOne(shot(8, {}, { "slice", "hook", "push", "push_slice", "fade" }), pack);
        check(noShape.judged() && !noShape.direction.read && !filter("direction:straight").contains(noShape)
                  && filter("strike:solid").contains(noShape),
              "a shot whose direction couldn't be read is judged on its strike, and in no direction choice");

        check(ShotFilter().contains(faded) && ShotFilter().contains(unread) && ShotFilter().contains(fatSlice),
              "All holds every shot, judged or not");

        TendencySessionInput corrected = sessionOf({ shot(9, { "slice" }) });
        corrected.session.corrections[9].push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("hook") });
        check(judgedShotFactors(corrected, pack, FlightNorms{}).value(9).direction.values == QStringList{ "hook" }, "the golfer's correction counts");
    }

    std::printf("\nthe words:\n");
    {
        check(filterWords(filter("strike:thin|direction:pull")) == QStringLiteral("pulled, thin"), "pulled, thin: direction first");
        check(filterWords(filter("strike:solid|direction:straight|flight:high")) == QStringLiteral("straight, solid, high"),
              "straight, solid, high");
        check(filterWords(filter("direction:fade")) == QStringLiteral("faded") && filterWords(filter("strike:chunk")) == QStringLiteral("fat")
                  && filterWords(filter("flight:sky")) == QStringLiteral("skied") && filterWords(filter("flight:mid")) == QStringLiteral("mid-height")
                  && filterWords(filter("direction:push_slice")) == QStringLiteral("push-sliced")
                  && filterWords(filter("strike:smash_deficit")) == QStringLiteral("weakly struck"),
              "each value its adjective from the one table");
        check(filterWords(filter("strike:wobble"), { { QStringLiteral("wobble"), QStringLiteral("Wobble") } }) == QStringLiteral("wobble")
                  && filterWords(filter("strike:wobble")) == QStringLiteral("wobble"),
              "a value the table doesn't hold: its label, lower case, else its id");
        check(filterWords(ShotFilter()).isEmpty(), "All has no words");
    }

    std::printf("\na session's shots, and Tendencies agreeing:\n");
    // 1-4 slice with over the top; 5 a slice that couldn't see over the top; 6-9 straight, over the top on 6;
    // 10 an intended fade with over the top; 11 no reading at all, over the top.
    std::vector<ShotRecord> shots;
    for (int id = 1; id <= 4; ++id) shots.push_back(shot(id, { "slice", "over_the_top" }));
    shots.push_back(shot(5, { "slice" }, { "over_the_top" }));
    shots.push_back(shot(6, { "over_the_top" }));
    for (int id = 7; id <= 9; ++id) shots.push_back(shot(id, {}));
    shots.push_back(shot(10, { "fade", "over_the_top" }));
    shots.push_back(noReadingShot(11, { "over_the_top" }));
    TendencySessionInput session = sessionOf(shots);
    session.intents.insert(10, QStringLiteral("fade"));
    const QHash<int, ShotFactors> judged = judgedShotFactors(session, pack, FlightNorms{});
    {
        check(judged.size() == 11, "every shot is there, judged or not");
        int judgedCount = 0;
        for (const ShotFactors &j : judged) if (j.judged()) ++judgedCount;
        const TendencySet set = reduceTendencies({ session }, pack, [](const QString &) { return QStringLiteral("driver"); }, FlightNorms{});
        check(judgedCount == 10 && set.countedShots == judgedCount, "the same shots count as in Tendencies, the intended fade among them");
        const auto tupleShots = [&set](const QStringList &direction) {
            if (set.clubOutcomes.size() != 1) return -1;
            for (const TendencyFactorTuple &t : set.clubOutcomes[0].tuples)
                if (t.direction == direction && t.strike == QStringList{ "solid" } && t.flight.isEmpty()) return t.shots;
            return 0;
        };
        check(set.clubOutcomes.size() == 1 && set.clubOutcomes[0].judged == 10 && set.clubOutcomes[0].tuples.size() == 3
                  && tupleShots({ "slice" }) == 5 && tupleShots({ "straight" }) == 4 && tupleShots({ "fade" }) == 1,
              "and Tendencies' tuples are the choices' sizes: solid slice 5, solid straight 4, solid fade 1");

        check(idsOf(shotsInShotFilter(shots, judged, ShotFilter())).size() == 11, "All sees every shot");
        check(idsOf(shotsInShotFilter(shots, judged, filter("direction:slice"))) == std::vector<int>{ 1, 2, 3, 4, 5 },
              "Slice sees the slices, in ledger order");
        check(idsOf(shotsInShotFilter(shots, judged, filter("strike:solid|direction:straight"))) == std::vector<int>{ 6, 7, 8, 9 },
              "Solid and Straight see the clean shots, not the one with no reading");
        check(idsOf(shotsInShotFilter(shots, judged, filter("direction:fade"))) == std::vector<int>{ 10 }, "Fade sees the intended fade");
        check(shotsInShotFilter(shots, judged, filter("direction:hook")).empty(), "a value not hit sees nothing");
        check(idsOf(shotsInShotFilter(shots, {}, filter("direction:slice"))).empty(), "a shot with no judgement is not judged");
    }

    std::printf("\nthe chosen shots against the other shots:\n");
    {
        const QSet<QString> ids{ QStringLiteral("over_the_top"), QStringLiteral("early_extension"), QStringLiteral("hook") };
        const QHash<QString, OutcomeContrast> slices = outcomeContrasts(shots, judged, filter("direction:slice"), ids);
        const OutcomeContrast ott = slices.value(QStringLiteral("over_the_top"));
        check(ott.a == 4 && ott.n1 == 4, "on the slices: fired on 4 of the 4 that could see it");
        check(ott.b == 2 && ott.n0 == 5, "on the other judged shots: 2 of 5 (the fade among them); the unread shot in neither");
        const OutcomeContrast ee = slices.value(QStringLiteral("early_extension"));
        check(ee.a == 0 && ee.n1 == 5 && ee.b == 0 && ee.n0 == 5, "a fault never seen still counts the shots that assessed it");
        check(slices.size() == 3 && !slices.contains(QStringLiteral("slice")), "only the conditions asked for");

        const OutcomeContrast straightOtt = outcomeContrasts(shots, judged, filter("direction:straight"), ids)
                                                .value(QStringLiteral("over_the_top"));
        check(straightOtt.a == 1 && straightOtt.n1 == 4 && straightOtt.b == 5 && straightOtt.n0 == 5, "Straight is set against the rest");
        check(outcomeContrasts(shots, judged, ShotFilter(), ids).isEmpty(), "All has nothing to set against");

        OutcomeContrast sum = ott;
        sum += straightOtt;
        check(sum.a == 5 && sum.n1 == 8 && sum.b == 7 && sum.n0 == 10, "counts add up across sessions");
    }

    std::printf("\nthe contrast line:\n");
    {
        const QString slice = filterWords(filter("direction:slice"));
        check(outcomeContrastLine(OutcomeContrast{ 34, 38, 12, 61 }, slice) == QStringLiteral("On your sliced shots 34 of 38 · other shots 12 of 61"),
              "more often on the chosen shots: the counts alone");
        check(outcomeContrastLine(OutcomeContrast{ 8, 11, 12, 31 }, filterWords(filter("strike:thin|direction:pull")))
                  == QStringLiteral("On your pulled, thin shots 8 of 11 · other shots 12 of 31"),
              "the words name the choice");
        check(outcomeContrastLine(OutcomeContrast{ 7, 20, 4, 20 }, slice) == QStringLiteral("On your sliced shots 7 of 20 · other shots 4 of 20"),
              "exactly 15 points more is more often");
        check(outcomeContrastLine(OutcomeContrast{ 9, 10, 55, 61 }, slice)
                  == QStringLiteral("On your sliced shots 9 of 10 · other shots 55 of 61 — just as often on your other shots"),
              "within 15 points: just as often");
        check(outcomeContrastLine(OutcomeContrast{ 6, 20, 4, 20 }, slice).endsWith(QStringLiteral(" — just as often on your other shots")),
              "10 points more is still just as often");
        check(outcomeContrastLine(OutcomeContrast{ 2, 10, 40, 60 }, slice)
                  == QStringLiteral("On your sliced shots 2 of 10 · other shots 40 of 60 — less often than on your other shots"),
              "15 points or more less: less often");
        check(outcomeContrastLine(OutcomeContrast{ 5, 6, 1, 7 }, slice)
                  == QStringLiteral("On your sliced shots 5 of 6 · other shots 1 of 7 — too few other shots to compare"),
              "under 8 other shots: too few to compare");
        check(outcomeContrastLine(OutcomeContrast{ 8, 8, 0, 8 }, slice) == QStringLiteral("On your sliced shots 8 of 8 · other shots 0 of 8"),
              "8 other shots are enough");
        check(outcomeContrastLine(OutcomeContrast{ 0, 0, 12, 61 }, slice).isEmpty(), "no chosen shot assessed it: no line");
        check(outcomeContrastLine(OutcomeContrast{ 3, 4, 10, 40 }, filterWords(filter("strike:solid|direction:straight")))
                  .startsWith(QStringLiteral("On your straight, solid shots 3 of 4")),
              "Solid and Straight read as your straight, solid shots");
        TendencyOptions strict;
        strict.trendDelta = 0.8;        // 89% against 20% is under 80 points
        check(outcomeContrastLine(OutcomeContrast{ 34, 38, 12, 61 }, slice, strict).endsWith(QStringLiteral("just as often on your other shots")),
              "the rule is Tendencies' own option");
    }

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
