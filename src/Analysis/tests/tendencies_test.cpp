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

// tendencies.h — the cross-session reduction (docs/design/shot_outcomes_design.md §7.1): which shots
// count, the earn rule, the usual miss and its "often with", seen-with and its strength, the trend, the
// word bands under a half, each club's outcome counts, and the JSON. Header-only: `shot_outcomes_test` proves the
// composer it calls.

#include "../../Coaching/tendencies.h"
#include "../../Gui/report/report_tendencies.h"

#include <QJsonDocument>

#include <atomic>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static ShotConditionFacts facts(const char *id, const char *family, const char *shortText, const char *abbr,
                                ShotConditionLayer layer, int order, const char *golfer = "", const char *playTo = "")
{
    ShotConditionFacts f;
    f.id          = QString::fromLatin1(id);
    f.family      = QString::fromLatin1(family);
    f.label       = QString::fromLatin1(shortText);
    f.golferShort = QString::fromLatin1(shortText);
    f.golferAbbr  = QString::fromLatin1(abbr);
    f.layer       = layer;
    f.packOrder   = order;
    f.golfer      = QString::fromLatin1(golfer);
    f.playTo      = QString::fromLatin1(playTo);
    return f;
}

static ShotOutcomePack fixturePack()
{
    ShotOutcomePack p;
    using L = ShotConditionLayer;
    int order = 0;
    const std::vector<ShotConditionFacts> list{
        facts("chunk",         "strike",    "Fat",   "FAT", L::Ball, order++, "the club hits the ground first"),
        facts("slice",         "direction", "Slice", "SL",  L::Ball, order++, "the ball slices", "Favour the left side."),
        facts("hook",          "direction", "Hook",  "HK",  L::Ball, order++, "the ball hooks", "Favour the right side."),
        facts("push",          "direction", "Push",  "PU",  L::Ball, order++),
        facts("fade",          "direction", "Fade",  "FD",  L::Ball, order++),
        facts("carry_deficit", "flight",    "Short", "SH",  L::Ball, order++),
        facts("early_extension", "", "Early extension", "", L::Swing, order++, "you stand up through the ball"),
        facts("over_the_top",    "", "Over the top",    "", L::Swing, order++, "your hands come over the top"),
        facts("out_to_in_path",  "", "Out-to-in path",  "", L::Club, order++, "the club comes across the ball"),
        facts("in_to_out_path",  "", "In-to-out path",  "", L::Club, order++, "the club comes from the inside"),
        facts("push_slice",    "direction", "Push slice", "PS", L::Ball, order++),     // last, so no other pack order moves
        facts("pull",          "direction", "Pull",       "PL", L::Ball, order++),
        facts("pull_hook",     "direction", "Pull hook",  "PH", L::Ball, order++),
    };
    for (const ShotConditionFacts &f : list) p.conditions.insert(f.id, f);
    const auto cause = [&p](const char *from, const char *to) { p.causes[QString::fromLatin1(from)] << QString::fromLatin1(to); };
    p.physics[QStringLiteral("chunk")] << QStringLiteral("carry_deficit");
    p.physics[QStringLiteral("slice")] << QStringLiteral("carry_deficit");
    cause("early_extension", "chunk");
    cause("early_extension", "slice");
    cause("over_the_top", "out_to_in_path");
    cause("out_to_in_path", "slice");
    cause("out_to_in_path", "hook");
    return p;
}

static ShotRecord shot(int id, std::initializer_list<const char *> fired, const char *club = "DRIVER")
{
    ShotRecord s;
    s.shotId = id;
    s.club   = QString::fromLatin1(club);
    s.carry  = 200.0;
    s.swingLength = QStringLiteral("full");
    for (const char *rid : { "chunk", "slice", "hook", "push", "carry_deficit", "over_the_top", "early_extension" }) {
        ConditionRow r;
        r.conditionId = QString::fromLatin1(rid);
        const bool hit = std::any_of(fired.begin(), fired.end(), [rid](const char *f) { return qstrcmp(f, rid) == 0; });
        r.state = hit ? ShotState::Fired : ShotState::Clean;
        r.firedSignalCount = hit ? 1 : 0;
        s.rows.push_back(r);
    }
    return s;
}

// `n` shots, every one fired with `fired`.
static TendencySessionInput session(int dayNumber, int n, std::initializer_list<const char *> fired, const char *club = "DRIVER")
{
    TendencySessionInput s;
    s.session.day    = QDate(2026, 3, dayNumber);
    s.session.folder = s.session.day.toString(Qt::ISODate) + QStringLiteral("_M_01");
    for (int i = 0; i < n; ++i)
        s.session.shots.push_back(shot(i + 1, fired, club));
    return s;
}

static QString driverGroup(const QString &club)
{
    const QString c = club.toUpper();
    if (c.contains(QLatin1String("DRIVER"))) return QStringLiteral("driver");
    if (c.contains(QLatin1String("IRON")) || c.contains(QLatin1String("PITCH"))) return tendencyIronGroup(c);
    if (c.contains(QLatin1String("WEDGE")))  return QStringLiteral("wedges");
    return QString();
}

static const TendencyItem *itemFor(const TendencySet &s, const char *key)
{
    for (const TendencyItem &it : s.items) if (it.key == QLatin1String(key)) return &it;
    return nullptr;
}

static const TendencyOnBall *ballFor(const TendencySet &s, const char *id)
{
    for (const TendencyOnBall &b : s.onBall) if (b.conditionId == QLatin1String(id)) return &b;
    return nullptr;
}

int main()
{
    std::printf("tendencies_test\n");
    const ShotOutcomePack pack = fixturePack();

    std::printf("\nwhich shots count, and the earn rule:\n");
    {
        // Two sessions of nine shots: earned at 15, every shot counted.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(set.countedShots == 18 && set.countedSessions == 2, "every shot counts, the first of a session too");
        check(it && !it->building && it->miss == QStringLiteral("Slice") && it->shots == 18 && it->sessions == 2, "earned: the usual miss is the slice");
        check(it && it->share > 0.99 && it->frequency == QStringLiteral("every swing"), "every counted shot, in words");
        check(it && it->seenWithId == QStringLiteral("over_the_top") && it->chain.size() == 3, "seen with the driver, with its chain to the ball");
        check(it && it->strength == QStringLiteral("possible"), "no confirmed chains: possible at most");
    }
    {
        // One session is building however long it is; so is two sessions short of the 15.
        const TendencySet one = reduceTendencies({ session(1, 12, { "slice" }) }, pack, driverGroup);
        const TendencyItem *a = itemFor(one, "DRIVER|full");
        check(a && a->building && a->sessions == 1 && a->seenWith.isEmpty(), "one session: building, no verdict");
        const TendencySet twoShort = reduceTendencies({ session(1, 8, { "slice" }), session(2, 6, { "slice" }) }, pack, driverGroup);
        const TendencyItem *b = itemFor(twoShort, "DRIVER|full");
        check(b && b->building, "two sessions under the 15 shots: building");

        check(a && a->chain.size() == 1 && a->earlyNote.contains(QStringLiteral("no reason has stood out")),
              "building with no reason says so, and still has something to open");
        const TendencySet early = reduceTendencies({ session(1, 12, { "slice", "over_the_top" }) }, pack, driverGroup);
        const TendencyItem *c = itemFor(early, "DRIVER|full");
        check(c && c->building && c->seenWith.isEmpty() && c->seenWithId.isEmpty() && c->strength.isEmpty(),
              "building with a reason: still no verdict and nothing to practise");
        check(c && c->chain.size() == 3 && c->chain[0].words == QStringLiteral("seen with")
                  && c->chain[1].words == QStringLiteral("seen with") && c->chain[2].words.isEmpty(),
              "what has shown up so far, every link only 'seen with'");
        check(c && c->earlyNote == QStringLiteral("Early days: this is what has shown up so far. It becomes a tendency after 2 sessions of 8 or more shots."),
              "and it says it's early and what would earn it");
        const TendencySet back = tendencySetFromJson(tendencySetToJson(early));
        const TendencyItem *d = itemFor(back, "DRIVER|full");
        check(d && d->earlyNote == c->earlyNote && d->chain.size() == 3, "the early note survives the saved file");
    }
    {
        // The shot mix: every counted shot once, good first, then the misses by count.
        ShotMixTally t;
        t.good = 2;
        t.addMiss(QStringLiteral("thin"), QStringLiteral("Thin"));
        t.addMiss(QStringLiteral("slice"), QStringLiteral("Slice"));
        const ShotMixBar half = composeShotMixBar(t);
        check(half.line == QStringLiteral("Good 50% · Slice 25% · Thin 25%"), "the words, good first");
        check(half.judged == 4 && half.parts[0].kind == QStringLiteral("good") && half.parts[1].kind == QStringLiteral("main")
                  && half.parts[2].kind == QStringLiteral("other"), "good, the commonest miss, the rest");

        ShotMixTally thirds;
        thirds.good = 1;
        thirds.addMiss(QStringLiteral("thin"), QStringLiteral("Thin"));
        thirds.addMiss(QStringLiteral("slice"), QStringLiteral("Slice"));
        const ShotMixBar third = composeShotMixBar(thirds);
        check(third.parts[0].percent + third.parts[1].percent + third.parts[2].percent == 100, "the percentages always add up to 100");

        ShotMixTally many;
        many.notRead = 2;
        for (const char *id : { "a", "b", "c", "d", "e" }) many.addMiss(QString::fromLatin1(id), QString::fromLatin1(id));
        const ShotMixBar capped = composeShotMixBar(many);
        check(capped.parts.size() == 4 && capped.parts[3].label == QStringLiteral("Other") && capped.parts[3].count == 2,
              "three misses named, the rest are Other");
        check(capped.line.endsWith(QStringLiteral("2 not read")) && capped.judged == 5, "shots not read are said, not counted");
        check(composeShotMixBar(ShotMixTally()).parts.empty(), "nothing judged, nothing drawn");

        std::vector<TendencySessionInput> mixed{ session(1, 8, { "slice" }), session(2, 8, {}) };
        const TendencySet set = reduceTendencies(mixed, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->mix.judged == 16 && it->mix.line == QStringLiteral("Good 50% · Slice 50%"), "a row counts every shot with the club");
        const TendencySet savedSet = tendencySetFromJson(tendencySetToJson(set));
        const TendencyItem *reloaded = itemFor(savedSet, "DRIVER|full");
        check(reloaded && reloaded->mix.line == it->mix.line && reloaded->mix.parts.size() == 2, "the mix survives the saved file");
    }
    {
        // Enough shots, but a miss on few of them: no usual miss rather than a guess.
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, {}) };
        in[1].session.shots[3] = shot(4, { "slice" });
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && !it->building && it->noUsualMiss && it->miss == QStringLiteral("No usual miss"), "a miss too seldom to earn: no usual miss");
    }
    {
        // Intended shapes leave the tally; an ungrouped club and a shot with no reading do too.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        in[1].intents.insert(4, QStringLiteral("fade"));
        in[1].intents.insert(5, QStringLiteral("fade"));
        in[0].session.shots[5].carry = std::numeric_limits<double>::quiet_NaN();    // no reading
        in[0].session.shots[6].club  = QStringLiteral("PUTTER");
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->shots == 14, "intended shapes, a shot with no reading and an ungrouped club are all left out");
    }
    {
        // A correction reaches the reduction: the shot now reads hook.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        for (int id = 1; id <= 9; ++id) {
            std::vector<ShotCorrection> &list = in[1].session.corrections[id];
            list.push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("hook") });
        }
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->miss == QStringLiteral("Hook"), "a correction replaces the detected label, newest session first");
    }

    std::printf("\neach club's outcomes, Home's outcome filter's choices:\n");
    {
        // Driver: 5 fat slices and 4 slices, then 7 good shots counted (an intended fade and a shot with no
        // reading are not). A 7 iron hook; a putter, with no group, not counted.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, {}) };
        for (int i = 0; i < 5; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice" });
        in[1].intents.insert(1, QStringLiteral("fade"));
        in[1].session.shots[1].carry = std::numeric_limits<double>::quiet_NaN();
        in[1].session.shots.push_back(shot(10, { "hook" }, "7 IRON"));
        in[1].session.shots.push_back(shot(11, { "slice" }, "PUTTER"));
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        check(set.clubOutcomes.size() == 2 && set.clubOutcomes[0].clubId == QStringLiteral("DRIVER")
                  && set.clubOutcomes[1].clubId == QStringLiteral("7 IRON"), "each club with a group, in the bag's order");
        const TendencyClubOutcomes &driver = set.clubOutcomes[0];
        check(driver.judged == 16 && driver.good == 7, "the judged shots and the good ones, the fade and the unread shot left out");
        check(driver.misses.size() == 2 && driver.misses[0].id == QStringLiteral("slice") && driver.misses[0].shots == 9
                  && driver.misses[0].label == QStringLiteral("Slice"), "the slice on 9, most shots first");
        check(driver.misses.size() == 2 && driver.misses[1].id == QStringLiteral("chunk") && driver.misses[1].shots == 5
                  && driver.misses[1].label == QStringLiteral("Fat"), "a fat slice counts under Fat as well");
        check(set.clubOutcomes[1].judged == 1 && set.clubOutcomes[1].good == 0 && set.clubOutcomes[1].misses.size() == 1
                  && set.clubOutcomes[1].misses[0].id == QStringLiteral("hook"), "the 7 iron's hook");
        check(set.countedShots == driver.judged + set.clubOutcomes[1].judged, "the same shots Tendencies counts");
        int missShots = 0;
        for (const TendencyOutcomeCount &m : driver.misses) missShots += m.shots;
        check(missShots == 14 && driver.judged - driver.good == 9, "14 misses counted on 9 bad shots: a fat slice is in two");

        bool ok = false;
        const TendencySet back = tendencySetFromJson(tendencySetToJson(set), &ok);
        bool same = ok && back.clubOutcomes.size() == set.clubOutcomes.size();
        for (size_t c = 0; same && c < set.clubOutcomes.size(); ++c) {
            const TendencyClubOutcomes &a = set.clubOutcomes[c], &b = back.clubOutcomes[c];
            same = a.clubId == b.clubId && a.judged == b.judged && a.good == b.good && a.misses.size() == b.misses.size();
            for (size_t m = 0; same && m < a.misses.size(); ++m)
                same = a.misses[m].id == b.misses[m].id && a.misses[m].label == b.misses[m].label && a.misses[m].shots == b.misses[m].shots;
        }
        check(same, "the counts survive the saved file");
        check(reduceTendencies({}, pack, driverGroup).clubOutcomes.empty(), "no shots, no counts");
    }
    {
        // A push slice (here the golfer's correction) is counted under Push slice, Slice and Push, while its
        // row in Tendencies still has the one primary miss, the push slice.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }) };
        in[0].session.corrections[1].push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("push_slice") });
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const auto countOf = [&set](const char *id) {
            if (set.clubOutcomes.empty()) return -1;
            for (const TendencyOutcomeCount &m : set.clubOutcomes[0].misses) if (m.id == QLatin1String(id)) return m.shots;
            return 0;
        };
        check(set.clubOutcomes.size() == 1 && set.clubOutcomes[0].judged == 9 && set.clubOutcomes[0].good == 0,
              "nine judged shots, none good");
        check(countOf("slice") == 9 && countOf("push_slice") == 1 && countOf("push") == 1,
              "the push slice is under Push slice, Slice and Push");
        check(set.clubOutcomes[0].misses.size() == 3 && set.clubOutcomes[0].misses[0].id == QStringLiteral("slice")
                  && set.clubOutcomes[0].misses[1].id == QStringLiteral("push") && set.clubOutcomes[0].misses[1].label == QStringLiteral("Push")
                  && set.clubOutcomes[0].misses[2].id == QStringLiteral("push_slice"), "most shots first, then by id");
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->mix.judged == 9 && it->mix.parts.size() == 2 && it->mix.parts[0].key == QStringLiteral("slice")
                  && it->mix.parts[0].count == 8 && it->mix.parts[1].key == QStringLiteral("push_slice") && it->mix.parts[1].count == 1,
              "the row's mix still counts each shot once, under its primary miss");

        for (TendencySessionInput &s : in)
            for (ShotRecord &r : s.session.shots) r.handedness = QStringLiteral("Left");
        const TendencySet left = reduceTendencies(in, pack, driverGroup);
        const auto leftCount = [&left](const char *id) {
            if (left.clubOutcomes.empty()) return -1;
            for (const TendencyOutcomeCount &m : left.clubOutcomes[0].misses) if (m.id == QLatin1String(id)) return m.shots;
            return 0;
        };
        check(leftCount("hook") == 9 && leftCount("pull_hook") == 1 && leftCount("pull") == 1 && leftCount("slice") == 0,
              "a left-hander's are their pull hook, hook and pull");
    }

    std::printf("\nthe usual miss, and what it runs with:\n");
    {
        // Fat on every shot, a slice on 9 of the 18 (the second family at the 50% line).
        std::vector<TendencySessionInput> in{ session(1, 9, { "chunk", "early_extension" }), session(2, 9, { "chunk", "early_extension" }) };
        for (int i = 0; i < 9; ++i) {
            in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice", "early_extension" });
        }
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->miss == QStringLiteral("Fat, often with a slice"), "\"Fat, often with a slice\" at half the shots");
        check(it && it->missFamily == QStringLiteral("strike"), "the strike family leads the label");
    }
    {
        // Under half the shots, no suffix.
        std::vector<TendencySessionInput> in{ session(1, 9, { "chunk" }), session(2, 9, { "chunk" }) };
        for (int i = 3; i < 5; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice" });
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->miss == QStringLiteral("Fat"), "a second family on few shots does not reach the label");
    }

    std::printf("\na left-hander's words:\n");
    {
        std::vector<TendencySessionInput> in{ session(1, 9, { "hook", "over_the_top" }), session(2, 9, { "hook", "over_the_top" }) };
        for (TendencySessionInput &s : in)
            for (ShotRecord &r : s.session.shots) r.handedness = QStringLiteral("Left");
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->miss == QStringLiteral("Slice") && it->missId == QStringLiteral("hook") && it->handedness == QStringLiteral("Left"),
              "a left-hander's hook row is a Slice, and the item says whose hand it is");
        check(it && it->chain.size() == 3 && it->chain[0].text == QStringLiteral("The ball slices")
                  && it->chain[1].text == QStringLiteral("The club comes from the inside")
                  && it->chain[2].text == QStringLiteral("Your hands come over the top"),
              "the chain reads in the golfer's words, each step glossed");
        check(set.clubOutcomes.size() == 1 && set.clubOutcomes[0].misses.size() == 1
                  && set.clubOutcomes[0].misses[0].id == QStringLiteral("slice") && set.clubOutcomes[0].misses[0].label == QStringLiteral("Slice"),
              "a left-hander's outcome counts are under the slice they see");
        bool ok = false;
        const TendencySet back = tendencySetFromJson(tendencySetToJson(set), &ok);
        const TendencyItem *b = itemFor(back, "DRIVER|full");
        check(ok && b && b->handedness == QStringLiteral("Left"), "the hand survives the file");
    }

    std::printf("\nthe trend: three judged sessions, or words instead:\n");
    {
        // Two judged sessions: no trend, only whether the miss has been there throughout.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->trend == 0 && it->trendWords == QStringLiteral("→ steady"), "two sessions, present in both: steady");
    }
    {
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, { "slice" }) };
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->trend == 0 && it->trendWords == QStringLiteral("new"), "two sessions, in one only: new");
    }
    {
        // Over the line in the older session and under it in the latest: fading, not new.
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, {}) };
        for (int i = 3; i < 8; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        in[1].session.shots[3] = shot(4, { "slice" });
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && !it->building && !it->noUsualMiss && it->trendWords == QStringLiteral("↓ easing") && it->trend == -1,
              "two sessions, a miss that has left the latest one: easing, not new");
    }
    {
        // Three judged sessions, falling: the last two against the first.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, {}), session(3, 9, {}) };
        for (int i = 3; i < 6; ++i) in[1].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        for (int i = 3; i < 6; ++i) in[2].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->trend == -1 && it->trendWords == QStringLiteral("↓ easing"), "a slice on one in three, down from all: easing");
    }

    std::printf("\nseen-with strength and the line on Home:\n");
    {
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        in[0].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        in[1].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->strength == QStringLiteral("likely") && it->confirmedSessions == 2, "confirmed in two sessions: likely");
        const TendencyOnBall *b = ballFor(set, "over_the_top");
        check(b && b->onTheBall == QStringLiteral("On the ball — your slice with driver."), "\"On the ball\" names the miss and the club");
        check(b && b->text == QStringLiteral("Behind 18 of your last 18 bad driver shots."), "behind: counted on the driver's bad shots");
        check(b && b->tendencyKey == QStringLiteral("DRIVER|full")
                  && b->tendencyText.contains(QStringLiteral("Driver: slice")), "the tendency it feeds");
        check(b && b->produced.size() == 1 && b->produced.front().phrase == QStringLiteral("Slice"), "what it produced");
    }
    {
        // A chain the session could not confirm says nothing on Home (F2: "behind" only when confirmed).
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        const TendencySet set = reduceTendencies(in, pack, driverGroup);
        const TendencyOnBall *b = ballFor(set, "over_the_top");
        check(b && b->onTheBall.isEmpty() && b->text.startsWith(QStringLiteral("Seen on")), "unconfirmed: no line on Home, and no claim of 'behind'");
    }

    std::printf("\nword bands below half:\n");
    check(tendencyFrequencyWords(0.45) == QStringLiteral("fairly often"), "45%: fairly often");
    check(tendencyFrequencyWords(0.3)  == QStringLiteral("sometimes"), "30%: sometimes");
    check(tendencyFrequencyWords(0.1)  == QStringLiteral("rarely"), "10%: rarely");
    check(tendencyFrequencyWords(0.6)  == QStringLiteral("more than half your swings"), "60%: the existing word");
    check(tendencyFrequencyWords(0.95) == QStringLiteral("almost every swing"), "95%: the existing word");

    std::printf("\ncancelled, and the JSON:\n");
    {
        std::atomic<bool> stop{ true };
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }) };
        const TendencySet set = reduceTendencies(in, pack, driverGroup, TendencyOptions{}, &stop);
        check(set.cancelled, "a cancelled reduction says so");
    }
    {
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        in[0].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        in[1].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        TendencySet set = reduceTendencies(in, pack, driverGroup);
        set.clubDistances.append(QJsonObject{ { QStringLiteral("club"), QStringLiteral("DRIVER") } });

        const QJsonObject json = tendencySetToJson(set);
        bool ok = false;
        const TendencySet back = tendencySetFromJson(json, &ok);
        const TendencyItem *it = itemFor(back, "DRIVER|full");
        check(ok && it && it->miss == QStringLiteral("Slice") && it->seenWithId == QStringLiteral("over_the_top")
                  && !it->chain.empty() && it->strength == QStringLiteral("likely"), "items survive the round trip");
        const TendencyOnBall *b = ballFor(back, "over_the_top");
        check(b && !b->onTheBall.isEmpty() && b->produced.size() == 1 && back.clubDistances.size() == 1,
              "the on-ball lines and club distances survive it too");

        QJsonObject other = json;
        other[QStringLiteral("rule")] = kTendencyRuleVersion + 1;
        bool otherOk = true;
        tendencySetFromJson(other, &otherOk);
        check(!otherOk, "another rule version is not read as ours");

        TendencySessionEvidence ev;
        ev.tiers.insert(QStringLiteral("over_the_top"), Tier::Pattern);
        LinkEvidence link;
        link.from = QStringLiteral("over_the_top");
        link.to = QStringLiteral("out_to_in_path");
        link.grade = LinkGrade::Coherent;
        ev.links.push_back(link);
        ev.chainConfirmed.insert(QStringLiteral("over_the_top"));
        const TendencySessionEvidence evBack = tendencyEvidenceFromJson(tendencyEvidenceToJson(ev));
        check(evBack.tiers.value(QStringLiteral("over_the_top")) == Tier::Pattern && evBack.links.size() == 1
                  && evBack.links[0].grade == LinkGrade::Coherent && evBack.chainConfirmed.contains(QStringLiteral("over_the_top")),
              "the session's evidence block round-trips");
    }

    {
        // Irons: long 1-4, mid 5-7, short 8 to the pitching wedge.
        const auto group = [](const char *club) { return tendencyIronGroup(QString::fromLatin1(club)); };
        check(group("1 IRON") == QStringLiteral("long_irons") && group("4 IRON") == QStringLiteral("long_irons"), "1 to 4 are long irons");
        check(group("5 IRON") == QStringLiteral("mid_irons") && group("7 IRON") == QStringLiteral("mid_irons"), "5 to 7 are mid irons");
        check(group("8 IRON") == QStringLiteral("short_irons") && group("9 IRON") == QStringLiteral("short_irons")
                  && group("PITCHING WEDGE") == QStringLiteral("short_irons"), "8 to the pitching wedge are short irons");
        check(group("IRON") == QStringLiteral("mid_irons"), "an iron with no number goes in the middle");
        check(tendencyClubLabel(QStringLiteral("long_irons")) == QStringLiteral("Long irons")
                  && tendencyClubRank(QStringLiteral("long_irons")) < tendencyClubRank(QStringLiteral("mid_irons"))
                  && tendencyClubRank(QStringLiteral("mid_irons")) < tendencyClubRank(QStringLiteral("short_irons"))
                  && tendencyClubRank(QStringLiteral("short_irons")) < tendencyClubRank(QStringLiteral("wedges")),
              "the groups are named, and listed longest club first");
    }
    {
        // Usual distance: the club's own full swing carry.
        const auto row = [](const char *club, const char *text) {
            return QJsonObject{ { QStringLiteral("club"), QString::fromLatin1(club) }, { QStringLiteral("carryText"), QString::fromLatin1(text) } };
        };
        const QJsonArray rows{ row("7 IRON", "147 yd"), row("DRIVER", "188 yd") };
        check(tendencyUsualDistance(QStringLiteral("7 IRON"), QStringLiteral("full"), rows) == QStringLiteral("147 yd"), "a club shows its own usual distance");
        check(tendencyUsualDistance(QStringLiteral("7 IRON"), QStringLiteral("partial"), rows).isEmpty(), "a part swing has no usual distance");
        check(tendencyUsualDistance(QStringLiteral("8 IRON"), QStringLiteral("full"), rows).isEmpty(), "a club with no distances shows none");
    }

    {
        // As of a session: earlier days, and the same day up to and including it.
        const auto counts = [](const char *f, const char *asOf) { return tendencySessionCounts(QString::fromLatin1(f), QString::fromLatin1(asOf)); };
        const char *asOf = "2026-10-05_M_Wrist_02";
        check(counts("2026-10-04_M_Wrist_01", asOf), "an earlier day counts");
        check(counts("2026-10-05_M_Wrist_01", asOf) && counts(asOf, asOf), "the same day counts up to and including the session");
        check(!counts("2026-10-05_M_Wrist_03", asOf), "a later session the same day doesn't");
        check(!counts("2026-10-06_M_Wrist_01", asOf), "a later day doesn't");
        check(!counts("notes", asOf) && counts("notes", "notes"), "an undated folder counts only as itself");
    }
    {
        // The report's block.
        check(pinpoint::report::tendenciesReportBlock(TendencySet{}).isEmpty(), "no rows, no page");
        TendencySet set;
        set.countedShots = 1; set.countedSessions = 2;
        set.clubDistances.append(QJsonObject{ { QStringLiteral("club"), QStringLiteral("7 IRON") }, { QStringLiteral("carryText"), QStringLiteral("147 yd") } });
        TendencyItem earned; earned.key = QStringLiteral("7 IRON|full"); earned.clubId = QStringLiteral("7 IRON"); earned.club = QStringLiteral("7 iron");
        earned.length = QStringLiteral("full"); earned.miss = QStringLiteral("Thin"); earned.building = false;
        earned.seenWith = QStringLiteral("you cast it"); earned.strength = QStringLiteral("likely"); earned.trendWords = QStringLiteral("→ steady");
        TendencyItem early = earned; early.key = QStringLiteral("7 IRON|partial"); early.length = QStringLiteral("partial"); early.building = true; early.sessions = 1;
        early.seenWith.clear();
        set.items = { earned, early };
        const QVariantMap block = pinpoint::report::tendenciesReportBlock(set);
        const QVariantList rows = block.value(QStringLiteral("rows")).toList();
        check(block.value(QStringLiteral("kicker")).toString() == QStringLiteral("1 shot  ·  2 sessions  ·  Tendencies"), "the kicker counts in words");
        check(rows.size() == 2 && rows[0].toMap().value(QStringLiteral("distance")).toString() == QStringLiteral("147 yd")
                  && rows[0].toMap().value(QStringLiteral("seen")).toString() == QStringLiteral("Seen with you cast it  ·  likely  ·  → steady"), "an earned row says what it's seen with");
        check(rows[1].toMap().value(QStringLiteral("club")).toString() == QStringLiteral("7 iron · part swing")
                  && rows[1].toMap().value(QStringLiteral("distance")).toString().isEmpty()
                  && rows[1].toMap().value(QStringLiteral("seen")).toString() == QStringLiteral("Building, 1 session so far"), "a building row states no verdict");
        check(block.value(QStringLiteral("foot")).toString().contains(QStringLiteral("2 sessions of 8 or more shots")), "and the foot says what earns it");
        set.items = { earned };
        check(pinpoint::report::tendenciesReportBlock(set).value(QStringLiteral("foot")).toString().isEmpty(), "no building row, no foot");
    }

    std::printf("\n%s\n", g_fail == 0 ? "OK" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
