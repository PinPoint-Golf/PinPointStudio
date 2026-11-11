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

// tendencies.h — the cross-session reduction (docs/design/shot_outcomes_design.md §7.1), three factors to a row
// (strike, direction, flight, shot_factors.h): which shots count, the earn rule per factor, each factor's usual
// (its commonest fault when it earns, else its commonest value), seen-with and its strength, the trend, the word
// bands under a half, each club's factor tuples, the on-ball lines, and the JSON. Header-only:
// `shot_outcomes_test` proves the composer and `shot_factors_test` the factors it reads.

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
        facts("thin",          "strike",    "Thin",       "TH", L::Ball, order++),
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

static const ShotOutcomePack &fixturePackOnce()
{
    static const ShotOutcomePack k = fixturePack();
    return k;
}

static ShotRecord shot(int id, std::initializer_list<const char *> fired, const char *club = "DRIVER")
{
    ShotRecord s;
    s.shotId = id;
    s.club   = QString::fromLatin1(club);
    s.carry  = 200.0;
    s.swingLength = QStringLiteral("full");
    for (const char *rid : { "chunk", "thin", "slice", "hook", "push", "pull", "carry_deficit", "over_the_top", "early_extension" }) {
        ConditionRow r;
        r.conditionId = QString::fromLatin1(rid);
        const bool hit = std::any_of(fired.begin(), fired.end(), [rid](const char *f) { return qstrcmp(f, rid) == 0; });
        r.state = hit ? ShotState::Fired : ShotState::Clean;
        r.firedSignalCount = hit ? 1 : 0;
        s.rows.push_back(r);
    }
    // Rule 11: a measured fat is a miss only when the flight agrees (measuredStrikeFlightAgrees, applied to the
    // ledger rows by checkStrikesAgainstFlight), and every shot here carries the same 200. These fixtures are about
    // counting the misses, not about that rule, so a fat's reading is its total and not its carry: with no carry or
    // smash to judge it by, it would stand as measured were the rows checked.
    if (std::any_of(fired.begin(), fired.end(), [](const char *f) { return qstrcmp(f, "chunk") == 0; })) {
        s.carry = std::numeric_limits<double>::quiet_NaN();
        s.total = 210.0;
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

// No reading at all: the monitor's strike height went with it, so the strike rows couldn't be read either.
static ShotRecord noReading(ShotRecord s)
{
    s.carry = std::numeric_limits<double>::quiet_NaN();
    s.total = std::numeric_limits<double>::quiet_NaN();
    for (ConditionRow &r : s.rows)
        if (r.conditionId == QLatin1String("chunk") || r.conditionId == QLatin1String("thin")) r.state = ShotState::NotAssessable;
    return s;
}

static TendencySet reduce(const std::vector<TendencySessionInput> &in, const FlightNorms &norms = FlightNorms{})
{
    return reduceTendencies(in, fixturePackOnce(), driverGroup, norms);
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

// Shots with a club whose `factor` shows `id`, over its tuples.
static int valueShots(const TendencySet &s, const char *club, const char *factor, const char *id)
{
    int n = 0;
    for (const TendencyClubOutcomes &c : s.clubOutcomes) {
        if (c.clubId != QLatin1String(club)) continue;
        for (const TendencyFactorTuple &t : c.tuples) {
            const QStringList &v = qstrcmp(factor, "strike") == 0 ? t.strike : qstrcmp(factor, "direction") == 0 ? t.direction : t.flight;
            if (v.contains(QLatin1String(id))) n += t.shots;
        }
    }
    return n;
}

static bool usual(const TendencyFactor &f, const char *value, const char *label, bool fault, bool building = false)
{
    return f.value == QLatin1String(value) && f.label == QLatin1String(label) && f.fault == fault && f.building == building;
}

int main()
{
    std::printf("tendencies_test\n");
    const ShotOutcomePack &pack = fixturePackOnce();

    std::printf("\nwhich shots count, and the earn rule:\n");
    {
        // Two sessions of nine shots: earned at 15, every shot counted.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(set.countedShots == 18 && set.countedSessions == 2, "every shot counts, the first of a session too");
        check(it && it->shots == 18 && it->sessions == 2, "the row's shots and sessions");
        check(it && usual(it->direction, "slice", "Slice", true) && it->direction.shots == 18 && it->direction.sessions == 2,
              "earned: the direction's usual is its fault, the slice");
        check(it && it->direction.share > 0.99 && it->direction.frequency == QStringLiteral("every swing"), "every counted shot, in words");
        check(it && it->direction.seenWithId == QStringLiteral("over_the_top") && it->direction.chain.size() == 3
                  && it->direction.workOn == QStringLiteral("over_the_top"),
              "seen with the driver, with its chain to the ball, and that is its work-on");
        check(it && it->direction.strength == QStringLiteral("possible"), "no confirmed chains: possible at most");
        check(it && usual(it->strike, "solid", "Solid", false) && it->strike.share > 0.99 && it->strike.seenWith.isEmpty()
                  && it->strike.chain.empty(),
              "the strike, with no fault, says its commonest value plainly: Solid, nothing seen with it");
        check(it && it->flight.label == QStringLiteral("-") && it->flight.shots == 0 && !it->flight.building && it->flight.value.isEmpty(),
              "no flight table: the flight isn't read, said as -");
    }
    {
        // One session is building however long it is; so is two sessions short of the 15.
        const TendencySet one = reduce({ session(1, 12, { "slice" }) });
        const TendencyItem *a = itemFor(one, "DRIVER|full");
        check(a && usual(a->direction, "slice", "Slice", true, true) && a->direction.sessions == 1 && a->direction.seenWith.isEmpty(),
              "one session: building, what has shown up so far, no verdict");
        check(a && usual(a->strike, "solid", "Solid", false, true) && a->strike.earlyNote.isEmpty(),
              "a building factor with no fault says its commonest value, and no early note");
        const TendencySet twoShort = reduce({ session(1, 8, { "slice" }), session(2, 6, { "slice" }) });
        const TendencyItem *b = itemFor(twoShort, "DRIVER|full");
        check(b && b->direction.building, "two sessions under the 15 shots: building");

        check(a && a->direction.chain.size() == 1 && a->direction.earlyNote.contains(QStringLiteral("no reason has stood out")),
              "building with no reason says so, and still has something to open");
        const TendencySet early = reduce({ session(1, 12, { "slice", "over_the_top" }) });
        const TendencyItem *c = itemFor(early, "DRIVER|full");
        check(c && c->direction.building && c->direction.seenWith.isEmpty() && c->direction.seenWithId.isEmpty()
                  && c->direction.strength.isEmpty() && c->direction.workOn.isEmpty(),
              "building with a reason: still no verdict and nothing to practise");
        check(c && c->direction.chain.size() == 3 && c->direction.chain[0].words == QStringLiteral("seen with")
                  && c->direction.chain[1].words == QStringLiteral("seen with") && c->direction.chain[2].words.isEmpty(),
              "what has shown up so far, every link only 'seen with'");
        check(c && c->direction.earlyNote == QStringLiteral("Early days: this is what has shown up so far. It becomes a tendency after 2 sessions of 8 or more shots."),
              "and it says it's early and what would earn it");
        const TendencySet back = tendencySetFromJson(tendencySetToJson(early));
        const TendencyItem *d = itemFor(back, "DRIVER|full");
        check(d && d->direction.earlyNote == c->direction.earlyNote && d->direction.chain.size() == 3, "the early note survives the saved file");
    }
    {
        // The old one-miss bar is still the shot_outcomes.h helper's: good first, then the misses by count.
        ShotMixTally t;
        t.good = 2;
        t.addMiss(QStringLiteral("thin"), QStringLiteral("Thin"));
        t.addMiss(QStringLiteral("slice"), QStringLiteral("Slice"));
        const ShotMixBar half = composeShotMixBar(t);
        check(half.line == QStringLiteral("Good 50% · Slice 25% · Thin 25%"), "the words, good first");
        check(half.judged == 4 && half.parts[0].kind == QStringLiteral("good") && half.parts[1].kind == QStringLiteral("main")
                  && half.parts[2].kind == QStringLiteral("other"), "good, the commonest miss, the rest");

        // Each factor's bar: every shot it was read on, once, under its commonest value with the key.
        std::vector<TendencySessionInput> mixed{ session(1, 8, { "slice" }), session(2, 8, {}) };
        const TendencySet set = reduce(mixed);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->direction.mix.judged == 16 && it->direction.mix.line == QStringLiteral("Straight 50% · Slice 50%")
                  && it->direction.mix.parts[0].kind == QStringLiteral("good") && it->direction.mix.parts[1].kind == QStringLiteral("main"),
              "the direction's bar: Straight first, then the slice");
        check(it && it->strike.mix.line == QStringLiteral("Solid 100%"), "the strike's: Solid");
        check(it && it->flight.mix.judged == 0 && it->flight.mix.line == QStringLiteral("16 not read"), "the flight's: none read, said");
        const TendencySet savedSet = tendencySetFromJson(tendencySetToJson(set));
        const TendencyItem *reloaded = itemFor(savedSet, "DRIVER|full");
        check(reloaded && reloaded->direction.mix.line == it->direction.mix.line && reloaded->direction.mix.parts.size() == 2,
              "the mix survives the saved file");
    }
    {
        // Enough shots, but a fault on few of them: the commonest value, plainly, not a guess.
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, {}) };
        in[1].session.shots[3] = shot(4, { "slice" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->direction, "straight", "Straight", false) && it->direction.seenWith.isEmpty(),
              "a fault too seldom to earn: Straight, never in a fault's words");
    }
    {
        // An intended shape now counts, its Fade a direction value; an ungrouped club and a shot with no reading
        // are left out. The older session has ten shots so that eight still count and it stays a judged session
        // (the earn rule's 2 sessions of 8 or more). shot() carries no fade row, so the two fades get one.
        std::vector<TendencySessionInput> in{ session(1, 10, { "slice" }), session(2, 9, { "slice" }) };
        const auto fadeShot = [](int id) {
            ShotRecord s = shot(id, {});
            ConditionRow fade;
            fade.conditionId = QStringLiteral("fade");
            fade.state = ShotState::Fired;
            fade.firedSignalCount = 1;
            s.rows.push_back(fade);
            return s;
        };
        in[1].session.shots[3] = fadeShot(4);
        in[1].session.shots[4] = fadeShot(5);
        in[1].intents.insert(4, QStringLiteral("fade"));
        in[1].intents.insert(5, QStringLiteral("fade"));
        in[0].session.shots[5] = noReading(in[0].session.shots[5]);
        in[0].session.shots[6].club  = QStringLiteral("PUTTER");
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->shots == 17 && valueShots(set, "DRIVER", "direction", "fade") == 2,
              "the two intended fades count, as Fade; the shot with no reading and the putter don't");
        check(it && usual(it->direction, "slice", "Slice", true), "and the slice is still the usual");
    }
    {
        // A correction reaches the reduction: the shot now reads hook.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        for (int id = 1; id <= 9; ++id) {
            std::vector<ShotCorrection> &list = in[1].session.corrections[id];
            list.push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("hook") });
        }
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->direction.label == QStringLiteral("Hook"), "a correction replaces the detected label, newest session first");
    }
    {
        // A flight table: every driver shot landing at 44° is High, a value, said plainly.
        const char *json = R"({ "schema": 2, "categories": { "driver": { "label": "Driver", "clubs": ["DRIVER"] } },
                                "methods": [ { "id": "landing", "bands": { "driver": [33, 41] } } ] })";
        const FlightNorms norms = flightNormsFromJson(QJsonDocument::fromJson(QByteArray(json)).object());
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, {}) };
        for (TendencySessionInput &s : in)
            for (ShotRecord &r : s.session.shots) r.descentAngle = 44.0;
        const TendencySet set = reduce(in, norms);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->flight, "high", "High", false) && it->flight.shots == 18 && it->flight.mix.line == QStringLiteral("High 100%"),
              "the flight's usual: High, a value, from the table");
        check(valueShots(set, "DRIVER", "flight", "high") == 18, "and the tuples carry it");
    }
    {
        // Rule 13, faults weighted, values counted: Mid on all 11 of the older session's shots, Low on all 9 of
        // the newer's. Recency-weighted Low leads (9 / 17.25 = 0.52 against Mid's 0.48), but Mid is on more
        // shots, 11 of 20, so the usual is Mid, as the bar beside it says, and its share is that plain count.
        const char *json = R"({ "schema": 2, "categories": { "driver": { "label": "Driver", "clubs": ["DRIVER"] } },
                                "methods": [ { "id": "landing", "bands": { "driver": [33, 41] } } ] })";
        const FlightNorms norms = flightNormsFromJson(QJsonDocument::fromJson(QByteArray(json)).object());
        std::vector<TendencySessionInput> in{ session(1, 11, {}), session(2, 9, {}) };
        for (ShotRecord &r : in[0].session.shots) r.descentAngle = 37.0;
        for (ShotRecord &r : in[1].session.shots) r.descentAngle = 30.0;
        const TendencySet set = reduce(in, norms);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(valueShots(set, "DRIVER", "flight", "mid") == 11 && valueShots(set, "DRIVER", "flight", "low") == 9,
              "rule 13: the fixture, Mid 11 older and Low 9 newer");
        check(it && usual(it->flight, "mid", "Mid", false) && std::abs(it->flight.share - 0.55) < 1e-9
                  && it->flight.frequency == QStringLiteral("more than half your swings"),
              "rule 13: a value is the most shots by plain count, Mid 11 of 20, over a recency-weighted Low");
        int barMid = 0, barLow = 0;
        if (it) for (const ShotMixPart &p : it->flight.mix.parts) {
            if (p.key == QStringLiteral("mid")) barMid = p.count;
            if (p.key == QStringLiteral("low")) barLow = p.count;
        }
        check(barMid == 11 && barLow == 9, "rule 13: and the bar agrees, Mid 11 · Low 9");

        // An exact tie by count goes to the table's order: Low before Mid.
        std::vector<TendencySessionInput> tie{ session(1, 9, {}), session(2, 9, {}) };
        for (ShotRecord &r : tie[0].session.shots) r.descentAngle = 37.0;
        for (ShotRecord &r : tie[1].session.shots) r.descentAngle = 30.0;
        const TendencyItem *t = itemFor(reduce(tie, norms), "DRIVER|full");
        check(t && usual(t->flight, "low", "Low", false) && std::abs(t->flight.share - 0.5) < 1e-9,
              "rule 13: a tie by count goes to the table's order, Low then Mid");
    }
    {
        // Rule 13, the building floor: one session of 12 (building), a slice on 2 of them (17 %). Under a
        // quarter, a stray, not what has shown up so far: the direction shows Straight, building, no fault.
        std::vector<TendencySessionInput> in{ session(1, 12, {}) };
        for (int i = 0; i < 2; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "slice", "over_the_top" });
        in[0].session.shots[2] = shot(3, { "chunk" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->direction, "straight", "Straight", false, true) && it->direction.chain.empty()
                  && it->direction.earlyNote.isEmpty() && it->direction.rowId.isEmpty()
                  && std::abs(it->direction.share - 10.0 / 12.0) < 1e-9,
              "rule 13: a building fault under 25 % (slice 2 of 12): Straight, still building, fault=false, no chain");
        check(it && usual(it->strike, "solid", "Solid", false, true),
              "rule 13: one fat in 12 is no building Fat either: Solid");
        check(it && it->direction.mix.parts.size() == 2 && it->direction.mix.parts[1].key == QStringLiteral("slice"),
              "rule 13: the slice still shows in the bar");
    }
    {
        // Rule 13, at the floor: a slice on 3 of 12 (exactly 25 %) shows, a building fault with its chain.
        std::vector<TendencySessionInput> in{ session(1, 12, {}) };
        for (int i = 0; i < 3; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "slice", "over_the_top" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->direction, "slice", "Slice", true, true) && std::abs(it->direction.share - 0.25) < 1e-9,
              "rule 13: a building fault at 25 % (slice 3 of 12) shows: fault=true, building=true");
        check(it && it->direction.chain.size() == 3 && it->direction.chain[0].words == QStringLiteral("seen with")
                  && it->direction.earlyNote.startsWith(QStringLiteral("Early days: this is what has shown up so far.")),
              "rule 13: with its Seen so far chain and early note, as before");
    }

    std::printf("\neach club's factor tuples, Home's filter's counts:\n");
    {
        // Driver: 5 fat slices and 4 slices, then 8 straight shots counted (an intended fade that flew straight
        // among them; a shot with no reading is not). A 7 iron hook; a putter, with no group, not counted.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, {}) };
        for (int i = 0; i < 5; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice" });
        in[1].intents.insert(1, QStringLiteral("fade"));
        in[1].session.shots[1] = noReading(in[1].session.shots[1]);
        in[1].session.shots.push_back(shot(10, { "hook" }, "7 IRON"));
        in[1].session.shots.push_back(shot(11, { "slice" }, "PUTTER"));
        const TendencySet set = reduce(in);
        check(set.clubOutcomes.size() == 2 && set.clubOutcomes[0].clubId == QStringLiteral("DRIVER")
                  && set.clubOutcomes[1].clubId == QStringLiteral("7 IRON"), "each club with a group, in the bag's order");
        const TendencyClubOutcomes &driver = set.clubOutcomes[0];
        check(driver.judged == 17, "the judged shots: the intended shot in, the unread shot out");
        check(driver.tuples.size() == 3
                  && driver.tuples[0].strike == QStringList{ "solid" } && driver.tuples[0].direction == QStringList{ "straight" }
                  && driver.tuples[0].flight.isEmpty() && driver.tuples[0].shots == 8
                  && driver.tuples[1].strike == QStringList{ "chunk" } && driver.tuples[1].direction == QStringList{ "slice" }
                  && driver.tuples[1].shots == 5
                  && driver.tuples[2].strike == QStringList{ "solid" } && driver.tuples[2].direction == QStringList{ "slice" }
                  && driver.tuples[2].shots == 4,
              "each distinct combination once, most shots first: solid straight 8, fat slice 5, solid slice 4; flight unread");
        check(valueShots(set, "DRIVER", "direction", "slice") == 9 && valueShots(set, "DRIVER", "strike", "chunk") == 5,
              "so the Slice choice holds 9 and Fat 5: a fat slice is in both");
        check(set.clubOutcomes[1].judged == 1 && set.clubOutcomes[1].tuples.size() == 1
                  && set.clubOutcomes[1].tuples[0].direction == QStringList{ "hook" }, "the 7 iron's hook");
        check(set.countedShots == driver.judged + set.clubOutcomes[1].judged, "the same shots Tendencies counts");

        bool ok = false;
        const TendencySet back = tendencySetFromJson(tendencySetToJson(set), &ok);
        bool same = ok && back.clubOutcomes.size() == set.clubOutcomes.size();
        for (size_t c = 0; same && c < set.clubOutcomes.size(); ++c) {
            const TendencyClubOutcomes &a = set.clubOutcomes[c], &b = back.clubOutcomes[c];
            same = a.clubId == b.clubId && a.judged == b.judged && a.tuples.size() == b.tuples.size();
            for (size_t m = 0; same && m < a.tuples.size(); ++m)
                same = a.tuples[m].strike == b.tuples[m].strike && a.tuples[m].direction == b.tuples[m].direction
                    && a.tuples[m].flight == b.tuples[m].flight && a.tuples[m].shots == b.tuples[m].shots;
        }
        check(same, "the tuples survive the saved file");
        check(reduce({}).clubOutcomes.empty(), "no shots, no counts");
    }
    {
        // Rule 15: how a row's factors came together (TendencyTogether). Two usual faults, Fat and Slice, that
        // were mostly different shots: 2 fat slices, 3 fats that flew straight, 6 slices struck solid, 5 clean.
        // The row alone reads "Fat · Slice"; the open row says they came together on 2 of 16 and where each went.
        std::vector<TendencySessionInput> in{ session(1, 8, { "slice" }), session(2, 8, {}) };
        for (int i = 0; i < 2; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice" });
        for (int i = 0; i < 3; ++i) in[1].session.shots[size_t(i)] = shot(i + 1, { "chunk" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->strike.fault && it->strike.value == QStringLiteral("chunk") && it->direction.fault
                  && it->direction.value == QStringLiteral("slice"), "the row's usuals: Fat and Slice, both faults");
        const QString pair = QStringLiteral("Fat and slice came together on 2 of 16 shots. Your fat shots went Straight 3 · Slice 2. "
                                            "Your slice shots were struck Solid 6 · Fat 2.");
        check(it && it->together.pairs == QStringList{ pair }, "the pair: how often together, and where each one's shots went, both ways");
        if (it && it->together.pairs != QStringList{ pair }) fprintf(stderr, "  pairs: %s\n", qPrintable(it->together.pairs.join(" | ")));
        const QStringList combos{ QStringLiteral("Solid · Slice — 6 of 16 shots"), QStringLiteral("Solid · Straight — 5 of 16 shots"),
                                  QStringLiteral("Fat · Straight — 3 of 16 shots") };
        check(it && it->together.combos == combos && it->together.moreCombos == QStringLiteral("and 1 other combination, 2 shots"),
              "the commonest combinations over the factors read (flight never is here), most shots first, the rest counted");
        if (it && it->together.combos != combos)
            fprintf(stderr, "  combos: %s / %s\n", qPrintable(it->together.combos.join(" | ")), qPrintable(it->together.moreCombos));

        bool ok = false;
        const TendencySet back = tendencySetFromJson(tendencySetToJson(set), &ok);
        const TendencyItem *b = itemFor(back, "DRIVER|full");
        check(ok && b && b->together.pairs == it->together.pairs && b->together.combos == it->together.combos
                  && b->together.moreCombos == it->together.moreCombos, "the Together lines survive the saved file");

        // Rule 17: the row's shots as one value per factor (what its bars count them under), for the flow (Sankey).
        int flowShots = 0, fatSlice = 0;
        for (const TendencyFlowCombo &c : it->flow) {
            flowShots += c.shots;
            if (c.strike == QStringLiteral("chunk") && c.direction == QStringLiteral("slice") && c.flight.isEmpty()) fatSlice = c.shots;
        }
        check(flowShots == 16 && fatSlice == 2 && it->flow.size() == 4, "the flow: 16 shots in 4 combinations, 2 fat slices, flight unread");
        bool sameFlow = b && b->flow.size() == it->flow.size();
        for (size_t i = 0; sameFlow && i < it->flow.size(); ++i)
            sameFlow = b->flow[i].strike == it->flow[i].strike && b->flow[i].direction == it->flow[i].direction
                    && b->flow[i].flight == it->flow[i].flight && b->flow[i].shots == it->flow[i].shots;
        check(sameFlow, "…and it survives the saved file");

        // One usual fault: no pair to speak of, the combinations still said.
        const TendencySet one = reduce({ session(1, 8, { "slice" }), session(2, 8, {}) });
        const TendencyItem *o = itemFor(one, "DRIVER|full");
        check(o && o->together.pairs.isEmpty() && o->together.combos.size() == 2, "one fault: no pair line, two combinations");

        // Never together: said as that.
        std::vector<TendencySessionInput> apart{ session(1, 8, { "slice" }), session(2, 8, { "chunk" }) };
        const TendencySet apartSet = reduce(apart);
        const TendencyItem *ap = itemFor(apartSet, "DRIVER|full");
        check(ap && ap->together.pairs.size() == 1
                  && ap->together.pairs.first().startsWith(QStringLiteral("Fat and slice never came together in 16 shots.")),
              "two faults on different shots: never together");
    }
    {
        // Rule 11 (rule 14: decided on the ledger): a measured fat counts only when the flight agrees. Three fats
        // among nine drivers: carrying the usual 200 they are no fault at all; carrying 160 (under 0.85 of it)
        // they are Fat. The reduction reads the rows as diagnostics.json holds them, checked against the flight
        // when the ledger was built (checkStrikesAgainstFlight), so the fixture is checked the same way.
        const auto fatsCarrying = [](double carry) {
            std::vector<TendencySessionInput> in{ session(1, 9, {}) };
            for (int i = 0; i < 3; ++i) {
                ShotRecord s = shot(i + 1, { "chunk" });
                s.carry = carry;
                in[0].session.shots[size_t(i)] = s;
            }
            checkStrikesAgainstFlight(in[0].session.shots, FlightNorms{});
            return reduce(in);
        };
        const TendencySet flewNormally = fatsCarrying(200.0);
        check(flewNormally.clubOutcomes.size() == 1 && flewNormally.clubOutcomes[0].judged == 9
                  && valueShots(flewNormally, "DRIVER", "strike", "solid") == 9 && valueShots(flewNormally, "DRIVER", "strike", "chunk") == 0,
              "rule 11: a fat the flight doesn't bear out is no fault, and the shot is Solid");
        const TendencySet cameUpShort = fatsCarrying(160.0);
        check(valueShots(cameUpShort, "DRIVER", "strike", "solid") == 6 && valueShots(cameUpShort, "DRIVER", "strike", "chunk") == 3,
              "rule 11: a fat that came up short is Fat");
    }
    {
        // A push slice (here the golfer's correction) is under Push slice, Slice and Push. In the direction's bar
        // it is counted once, under the one of those three commonest with the club: the slice.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }) };
        in[0].session.corrections[1].push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("push_slice") });
        const TendencySet set = reduce(in);
        check(set.clubOutcomes.size() == 1 && set.clubOutcomes[0].judged == 9, "nine judged shots");
        check(valueShots(set, "DRIVER", "direction", "slice") == 9 && valueShots(set, "DRIVER", "direction", "push_slice") == 1
                  && valueShots(set, "DRIVER", "direction", "push") == 1,
              "the push slice is under Push slice, Slice and Push");
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->direction.mix.judged == 9 && it->direction.mix.parts.size() == 1
                  && it->direction.mix.parts[0].key == QStringLiteral("slice") && it->direction.mix.parts[0].count == 9,
              "the bar counts each shot once, the push slice under the slice, the club's commonest");

        for (TendencySessionInput &s : in)
            for (ShotRecord &r : s.session.shots) r.handedness = QStringLiteral("Left");
        const TendencySet left = reduce(in);
        check(valueShots(left, "DRIVER", "direction", "hook") == 9 && valueShots(left, "DRIVER", "direction", "pull_hook") == 1
                  && valueShots(left, "DRIVER", "direction", "pull") == 1 && valueShots(left, "DRIVER", "direction", "slice") == 0,
              "a left-hander's are their pull hook, hook and pull");
    }

    std::printf("\nthree factors, each its own usual:\n");
    {
        // Fat on every shot, a slice on 9 of the 18: the strike says Fat and the direction Slice. Nothing is
        // "often with" anything: they are two factors.
        std::vector<TendencySessionInput> in{ session(1, 9, { "chunk", "early_extension" }), session(2, 9, { "chunk", "early_extension" }) };
        for (int i = 0; i < 9; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice", "early_extension" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->strike, "chunk", "Fat", true) && it->strike.share > 0.99, "the strike's usual: Fat, on every shot");
        check(it && usual(it->direction, "slice", "Slice", true), "the direction's: Slice, at its own share (0.43, the older session's)");
        check(it && it->strike.seenWithId == QStringLiteral("early_extension") && it->direction.seenWithId == QStringLiteral("early_extension"),
              "each fault with what it is seen with");
    }
    {
        // Under a quarter of the shots: the direction is Straight, the strike still Fat.
        std::vector<TendencySessionInput> in{ session(1, 9, { "chunk" }), session(2, 9, { "chunk" }) };
        for (int i = 3; i < 5; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->strike, "chunk", "Fat", true) && usual(it->direction, "straight", "Straight", false),
              "a slice on 2 of 18 doesn't earn: Straight, beside the Fat");
    }
    {
        // Lead value, directly: the biggest weight, then the most shots, then the id; a combined miss over its
        // own halves on an exact tie.
        const QHash<QString, double> even{ { QStringLiteral("thin"), 0.3 }, { QStringLiteral("slice"), 0.3 } };
        check(tendencyLeadMiss({ QStringLiteral("thin"), QStringLiteral("slice") },
                               { { QStringLiteral("thin"), 0.2 }, { QStringLiteral("slice"), 0.3 } },
                               { { QStringLiteral("thin"), 9 }, { QStringLiteral("slice"), 3 } }) == QStringLiteral("slice"),
              "lead: the biggest share wins over more shots");
        check(tendencyLeadMiss({ QStringLiteral("slice"), QStringLiteral("thin") }, even,
                               { { QStringLiteral("thin"), 4 }, { QStringLiteral("slice"), 3 } }) == QStringLiteral("thin"),
              "lead: an even share goes to the one on more shots");
        check(tendencyLeadMiss({ QStringLiteral("thin"), QStringLiteral("slice") }, even,
                               { { QStringLiteral("thin"), 3 }, { QStringLiteral("slice"), 3 } }) == QStringLiteral("slice"),
              "lead: an exact tie goes by id");
        const QHash<QString, double> halves{ { QStringLiteral("push"), 0.5 }, { QStringLiteral("push_slice"), 0.5 }, { QStringLiteral("slice"), 0.5 } };
        const QHash<QString, int> halvesN{ { QStringLiteral("push"), 3 }, { QStringLiteral("push_slice"), 3 }, { QStringLiteral("slice"), 3 } };
        check(tendencyLeadMiss({ QStringLiteral("slice"), QStringLiteral("push"), QStringLiteral("push_slice") }, halves, halvesN)
                  == QStringLiteral("push_slice"),
              "lead: the same shots as a push slice and its halves are named whole");
        check(tendencyLeadMiss({}, {}, {}).isEmpty(), "lead: none, none");
        check(tendencySameMiss(QStringLiteral("slice"), QStringLiteral("push_slice")) && tendencySameMiss(QStringLiteral("pull_hook"), QStringLiteral("pull"))
                  && !tendencySameMiss(QStringLiteral("slice"), QStringLiteral("push")) && !tendencySameMiss(QStringLiteral("slice"), QStringLiteral("pull_hook")),
              "the same miss seen another way: a combined miss and its own halves only");
    }
    {
        // A 7 iron's thin pulls: the pull on all 18 shots, the thin on the 9 newest. Both earn, each in its factor.
        std::vector<TendencySessionInput> in{ session(1, 9, { "pull" }, "7 IRON"), session(2, 9, { "thin", "pull" }, "7 IRON") };
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "7 IRON|full");
        check(it && usual(it->direction, "pull", "Pull", true) && it->direction.share > 0.99, "thin pulls: the direction's usual is Pull");
        check(it && usual(it->strike, "thin", "Thin", true) && it->strike.share > 0.5 && it->strike.share < 0.6,
              "and the strike's Thin, on the newer session's 9 (0.57 weighted)");
        check(it && it->direction.mix.judged == 18 && it->direction.mix.parts.size() == 1 && it->direction.mix.parts[0].key == QStringLiteral("pull"),
              "the direction's bar is all pulls");
        check(it && it->strike.mix.parts.size() == 2 && it->strike.mix.parts[0].key == QStringLiteral("solid")
                  && it->strike.mix.parts[1].key == QStringLiteral("thin") && it->strike.mix.parts[1].count == 9,
              "the strike's: Solid 9, Thin 9");
    }
    {
        // Mark's driver: a fat on 6 shots of the older session and a slice on 2 of them and 4 of the newer. Each
        // factor earns on its own: strike-first or most-common-wins no longer has to choose between them.
        std::vector<TendencySessionInput> in{ session(1, 8, {}), session(2, 8, {}) };
        for (int i = 0; i < 2; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk", "slice" });
        for (int i = 2; i < 6; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "chunk" });
        for (int i = 0; i < 4; ++i) in[1].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->strike, "chunk", "Fat", true) && usual(it->direction, "slice", "Slice", true),
              "slice 6, fat 6: Fat and Slice, both");
        check(it && it->direction.mix.judged == 16 && it->direction.mix.parts.size() == 2 && it->direction.mix.parts[0].key == QStringLiteral("straight")
                  && it->direction.mix.parts[0].count == 10 && it->direction.mix.parts[1].key == QStringLiteral("slice")
                  && it->direction.mix.parts[1].count == 6,
              "the direction's bar: Straight 10, Slice 6, the fat slices among the slices");
        check(it && it->strike.mix.parts.size() == 2 && it->strike.mix.parts[1].key == QStringLiteral("chunk") && it->strike.mix.parts[1].count == 6,
              "the strike's: Solid 10, Fat 6, the fat slices among the fats");

        // An exact tie, every session the same: each factor has its own, Thin and Slice.
        std::vector<TendencySessionInput> tie{ session(1, 8, {}), session(2, 8, {}) };
        for (TendencySessionInput &s : tie) {
            for (int i = 0; i < 3; ++i) s.session.shots[size_t(i)] = shot(i + 1, { "thin" });
            for (int i = 3; i < 6; ++i) s.session.shots[size_t(i)] = shot(i + 1, { "slice" });
        }
        const TendencySet tied = reduce(tie);
        const TendencyItem *t = itemFor(tied, "DRIVER|full");
        check(t && usual(t->strike, "thin", "Thin", true) && usual(t->direction, "slice", "Slice", true),
              "thin 6, slice 6: no tie to break, Thin and Slice");
        int barShots = 0;
        if (t) for (const ShotMixPart &p : t->strike.mix.parts) barShots += p.count;
        check(t && barShots == t->shots && t->strike.mix.judged == 16, "a factor's bar adds up to the shots");
    }
    {
        // A push slice is a slice: the four push slices count toward the slice on all 18 shots.
        const auto toPushSlice = [](TendencySessionInput &s, int shotId) {
            s.session.corrections[shotId].push_back(ShotCorrection{ QStringLiteral("direction"), QStringLiteral("slice"), QStringLiteral("push_slice") });
        };
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        for (int id = 1; id <= 4; ++id) toPushSlice(in[1], id);
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->direction, "slice", "Slice", true) && it->direction.share > 0.99,
              "a push slice counts toward the slice: every swing, not 14 of 18");
        check(it && it->direction.mix.parts.size() == 1 && it->direction.mix.parts[0].key == QStringLiteral("slice")
                  && it->direction.mix.parts[0].count == 18,
              "and is filed under the slice in the bar");

        // Every shot a push slice: the push slice, its halves on the same shots, names them whole.
        std::vector<TendencySessionInput> all{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        for (TendencySessionInput &s : all)
            for (int id = 1; id <= 9; ++id) toPushSlice(s, id);
        const TendencySet allSet = reduce(all);
        const TendencyItem *a = itemFor(allSet, "DRIVER|full");
        check(a && usual(a->direction, "push_slice", "Push slice", true) && a->direction.rowId == QStringLiteral("push_slice"),
              "a usual push slice, named whole");
        check(a && a->direction.mix.parts.size() == 1 && a->direction.mix.parts[0].key == QStringLiteral("push_slice")
                  && a->direction.mix.parts[0].count == 18,
              "and the bar files them whole");

        // A left-hander's push slice row is their pull hook, its halves their hook and pull.
        for (TendencySessionInput &s : all)
            for (ShotRecord &r : s.session.shots) r.handedness = QStringLiteral("Left");
        const TendencySet leftSet = reduce(all);
        const TendencyItem *l = itemFor(leftSet, "DRIVER|full");
        check(l && usual(l->direction, "pull_hook", "Pull hook", true) && l->direction.rowId == QStringLiteral("push_slice")
                  && l->direction.mix.parts.size() == 1 && l->direction.mix.parts[0].label == QStringLiteral("Pull hook"),
              "a left-hander's push slices read as the pull hook they see, the row behind it kept");
    }
    {
        // A left-hander's thin pushes: the push row is their pull.
        std::vector<TendencySessionInput> in{ session(1, 9, { "push" }), session(2, 9, { "thin", "push" }) };
        for (TendencySessionInput &s : in)
            for (ShotRecord &r : s.session.shots) r.handedness = QStringLiteral("Left");
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && usual(it->direction, "pull", "Pull", true) && it->direction.rowId == QStringLiteral("push")
                  && usual(it->strike, "thin", "Thin", true),
              "a left-hander's direction is glossed (their pull), the strike is not");
    }

    std::printf("\na left-hander's words:\n");
    {
        std::vector<TendencySessionInput> in{ session(1, 9, { "hook", "over_the_top" }), session(2, 9, { "hook", "over_the_top" }) };
        for (TendencySessionInput &s : in)
            for (ShotRecord &r : s.session.shots) r.handedness = QStringLiteral("Left");
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->direction.label == QStringLiteral("Slice") && it->direction.rowId == QStringLiteral("hook")
                  && it->handedness == QStringLiteral("Left"),
              "a left-hander's hook row is a Slice, and the item says whose hand it is");
        check(it && it->direction.chain.size() == 3 && it->direction.chain[0].text == QStringLiteral("The ball slices")
                  && it->direction.chain[1].text == QStringLiteral("The club comes from the inside")
                  && it->direction.chain[2].text == QStringLiteral("Your hands come over the top"),
              "the chain reads in the golfer's words, each step glossed");
        check(it && it->direction.playTo == QStringLiteral("Favour the right side."),
              "play to it: the glossed miss's line, mirrored for their hand");
        check(valueShots(set, "DRIVER", "direction", "slice") == 18, "a left-hander's tuples are under the slice they see");
        bool ok = false;
        const TendencySet back = tendencySetFromJson(tendencySetToJson(set), &ok);
        const TendencyItem *b = itemFor(back, "DRIVER|full");
        check(ok && b && b->handedness == QStringLiteral("Left") && b->direction.rowId == QStringLiteral("hook"), "the hand survives the file");
    }

    std::printf("\nthe trend: three judged sessions, or words instead:\n");
    {
        // Two judged sessions: no trend, only whether the fault has been there throughout.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, { "slice" }) };
        const TendencyItem *it = itemFor(reduce(in), "DRIVER|full");
        check(it && it->direction.trend == 0 && it->direction.trendWords == QStringLiteral("→ steady"), "two sessions, present in both: steady");
        check(it && it->strike.trendWords.isEmpty(), "a factor whose usual is no fault has no trend");
    }
    {
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, { "slice" }) };
        const TendencyItem *it = itemFor(reduce(in), "DRIVER|full");
        check(it && it->direction.trend == 0 && it->direction.trendWords == QStringLiteral("new"), "two sessions, in one only: new");
    }
    {
        // Over the line in the older session and under it in the latest: fading, not new.
        std::vector<TendencySessionInput> in{ session(1, 9, {}), session(2, 9, {}) };
        for (int i = 3; i < 8; ++i) in[0].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        in[1].session.shots[3] = shot(4, { "slice" });
        const TendencyItem *it = itemFor(reduce(in), "DRIVER|full");
        check(it && !it->direction.building && it->direction.fault && it->direction.trendWords == QStringLiteral("↓ easing")
                  && it->direction.trend == -1,
              "two sessions, a fault that has left the latest one: easing, not new");
    }
    {
        // Three judged sessions, falling: the last two against the first.
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice" }), session(2, 9, {}), session(3, 9, {}) };
        for (int i = 3; i < 6; ++i) in[1].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        for (int i = 3; i < 6; ++i) in[2].session.shots[size_t(i)] = shot(i + 1, { "slice" });
        const TendencyItem *it = itemFor(reduce(in), "DRIVER|full");
        check(it && it->direction.trend == -1 && it->direction.trendWords == QStringLiteral("↓ easing"), "a slice on one in three, down from all: easing");
    }

    std::printf("\nseen-with strength and the line on Home:\n");
    {
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        in[0].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        in[1].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        const TendencySet set = reduce(in);
        const TendencyItem *it = itemFor(set, "DRIVER|full");
        check(it && it->direction.strength == QStringLiteral("likely") && it->direction.confirmedSessions == 2, "confirmed in two sessions: likely");
        const TendencyOnBall *b = ballFor(set, "over_the_top");
        check(b && b->onTheBall == QStringLiteral("On the ball — your slice with driver."), "\"On the ball\" names the fault and the club");
        check(b && b->factor == QStringLiteral("direction") && b->value == QStringLiteral("slice"), "keyed on the club and the factor's fault");
        check(b && b->text == QStringLiteral("Behind 18 of your last 18 bad driver shots."), "behind: counted on the driver's bad shots");
        check(b && b->tendencyKey == QStringLiteral("DRIVER|full")
                  && b->tendencyText.contains(QStringLiteral("Driver: slice")), "the tendency it feeds");
        check(b && b->produced.size() == 1 && b->produced.front().phrase == QStringLiteral("Slice"), "what it produced");
    }
    {
        // A chain the session could not confirm says nothing on Home (F2: "behind" only when confirmed).
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        const TendencySet set = reduce(in);
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
        const TendencySet set = reduceTendencies(in, pack, driverGroup, FlightNorms{}, TendencyOptions{}, &stop);
        check(set.cancelled, "a cancelled reduction says so");
    }
    {
        std::vector<TendencySessionInput> in{ session(1, 9, { "slice", "over_the_top" }), session(2, 9, { "slice", "over_the_top" }) };
        in[0].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        in[1].evidence.chainConfirmed.insert(QStringLiteral("over_the_top"));
        TendencySet set = reduce(in);
        set.clubDistances.append(QJsonObject{ { QStringLiteral("club"), QStringLiteral("DRIVER") } });

        const QJsonObject json = tendencySetToJson(set);
        bool ok = false;
        const TendencySet back = tendencySetFromJson(json, &ok);
        const TendencyItem *it = itemFor(back, "DRIVER|full");
        check(ok && it && it->direction.label == QStringLiteral("Slice") && it->direction.seenWithId == QStringLiteral("over_the_top")
                  && !it->direction.chain.empty() && it->direction.strength == QStringLiteral("likely")
                  && it->direction.factor == QStringLiteral("direction") && it->strike.label == QStringLiteral("Solid")
                  && it->flight.label == QStringLiteral("-"),
              "items survive the round trip, all three factors");
        const TendencyOnBall *b = ballFor(back, "over_the_top");
        check(b && !b->onTheBall.isEmpty() && b->produced.size() == 1 && b->factor == QStringLiteral("direction") && back.clubDistances.size() == 1,
              "the on-ball lines and club distances survive it too");

        QJsonObject other = json;
        other[QStringLiteral("rule")] = kTendencyRuleVersion + 1;
        bool otherOk = true;
        tendencySetFromJson(other, &otherOk);
        check(!otherOk, "another rule version is not read as ours");
        QJsonObject thirteen = json;
        thirteen[QStringLiteral("rule")] = 13;
        bool thirteenOk = true;
        tendencySetFromJson(thirteen, &thirteenOk);
        check(kTendencyRuleVersion == 17 && !thirteenOk, "rule 17: a file of rule 13 is not read");
        QJsonObject fourteen = json;
        fourteen[QStringLiteral("rule")] = 14;
        bool fourteenOk = true;
        tendencySetFromJson(fourteen, &fourteenOk);
        check(!fourteenOk, "rule 15: a file of rule 14 (no Together lines) is not read");
        QJsonObject fifteen = json;
        fifteen[QStringLiteral("rule")] = 15;
        bool fifteenOk = true;
        tendencySetFromJson(fifteen, &fifteenOk);
        check(!fifteenOk, "rule 16: a file of rule 15 (Low not yet a watch in its bars) is not read");
        QJsonObject sixteen = json;
        sixteen[QStringLiteral("rule")] = 16;
        bool sixteenOk = true;
        tendencySetFromJson(sixteen, &sixteenOk);
        check(!sixteenOk, "rule 17: a file of rule 16 (no flow combinations) is not read");

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
        earned.length = QStringLiteral("full");
        earned.strike.label = QStringLiteral("Thin"); earned.strike.fault = true; earned.strike.building = false; earned.strike.shots = 20;
        earned.strike.seenWith = QStringLiteral("you cast it"); earned.strike.strength = QStringLiteral("likely");
        earned.strike.trendWords = QStringLiteral("→ steady");
        earned.direction.label = QStringLiteral("Straight"); earned.direction.building = false; earned.direction.shots = 20;
        earned.flight.label = QStringLiteral("-"); earned.flight.building = false;
        TendencyItem early = earned; early.key = QStringLiteral("7 IRON|partial"); early.length = QStringLiteral("partial");
        early.strike.building = true; early.strike.sessions = 1; early.strike.seenWith.clear();
        set.items = { earned, early };
        const QVariantMap block = pinpoint::report::tendenciesReportBlock(set);
        const QVariantList rows = block.value(QStringLiteral("rows")).toList();
        const auto strikeOf = [&rows](int i) { return rows.value(i).toMap().value(QStringLiteral("strike")).toMap(); };
        check(block.value(QStringLiteral("kicker")).toString() == QStringLiteral("1 shot  ·  2 sessions  ·  Tendencies"), "the kicker counts in words");
        check(rows.size() == 2 && rows[0].toMap().value(QStringLiteral("distance")).toString() == QStringLiteral("147 yd")
                  && strikeOf(0).value(QStringLiteral("label")).toString() == QStringLiteral("Thin")
                  && strikeOf(0).value(QStringLiteral("seen")).toString() == QStringLiteral("Seen with you cast it  ·  likely  ·  → steady"),
              "an earned fault says what it's seen with");
        check(rows[0].toMap().value(QStringLiteral("direction")).toMap().value(QStringLiteral("label")).toString() == QStringLiteral("Straight")
                  && rows[0].toMap().value(QStringLiteral("direction")).toMap().value(QStringLiteral("seen")).toString().isEmpty(),
              "a factor's plain value says nothing more");
        check(rows[1].toMap().value(QStringLiteral("club")).toString() == QStringLiteral("7 iron · part swing")
                  && rows[1].toMap().value(QStringLiteral("distance")).toString().isEmpty()
                  && strikeOf(1).value(QStringLiteral("seen")).toString() == QStringLiteral("Building, 1 session so far"), "a building factor states no verdict");
        check(block.value(QStringLiteral("foot")).toString().contains(QStringLiteral("2 sessions of 8 or more shots")), "and the foot says what earns it");
        set.items = { earned };
        check(pinpoint::report::tendenciesReportBlock(set).value(QStringLiteral("foot")).toString().isEmpty(), "no building factor, no foot");

        // The flow on the report: absent without a pack to name its nodes; with one, the map Home's flow reads, laid out in
        // the report's own units (layoutHeight) for the page to scale.
        check(!block.value(QStringLiteral("flow")).isValid() || block.value(QStringLiteral("flow")).isNull(), "no pack, no flow");
        earned.flow = { TendencyFlowCombo{ QStringLiteral("solid"), QStringLiteral("straight"), QStringLiteral("mid"), 8 },
                        TendencyFlowCombo{ QStringLiteral("thin"), QStringLiteral("slice"), QStringLiteral("low"), 4 } };
        set.items = { earned };
        const QVariantMap flow = pinpoint::report::tendenciesReportBlock(set, &fixturePackOnce()).value(QStringLiteral("flow")).toMap();
        check(flow.value(QStringLiteral("total")).toInt() == 12 && flow.value(QStringLiteral("enough")).toBool()
                  && flow.value(QStringLiteral("layoutHeight")).toDouble() == pinpoint::report::kReportFlowLayoutHeight
                  && flow.value(QStringLiteral("nodes")).toList().size() == 6 && flow.value(QStringLiteral("idealPct")).toInt() == 67,
              "with a pack, the report carries the flow: 12 shots, 67% ideal, six nodes, in its own units");
    }

    std::printf("\n%s\n", g_fail == 0 ? "OK" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
