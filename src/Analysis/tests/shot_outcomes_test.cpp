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

// shot_outcomes.h — labels, phrase, story, driver, strength and waterfall, one fixture per rule,
// with the design's §11 session numbers converted to yards.

#include "../../Coaching/shot_outcomes.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static ShotConditionFacts facts(const char *id, const char *family, const char *shortText, const char *abbr,
                                const char *story, ShotConditionLayer layer, int order, const char *golfer = "",
                                const char *playTo = "")
{
    ShotConditionFacts f;
    f.id          = QString::fromLatin1(id);
    f.family      = QString::fromLatin1(family);
    f.label       = QString::fromLatin1(shortText);
    f.golferShort = QString::fromLatin1(shortText);
    f.golferAbbr  = QString::fromLatin1(abbr);
    f.golferStory = QString::fromLatin1(story);
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
        facts("chunk",         "strike",    "Fat",        "FAT", "the club bottomed out behind the ball", L::Ball, order++),
        facts("thin",          "strike",    "Thin",       "TH",  "the club caught it thin",              L::Ball, order++),
        facts("top",           "strike",    "Top",        "TP",  "the club hit the top of the ball",     L::Ball, order++),
        facts("sky",           "strike",    "Sky",        "SK",  "the club went under the ball",         L::Ball, order++),
        facts("shank",         "strike",    "Shank",      "SN",  "it came off the hosel",                L::Ball, order++),
        facts("smash_deficit", "strike",    "Poor strike", "PS", "",                                     L::Ball, order++),
        facts("pull",          "direction", "Pull",       "PL",  "started left",                         L::Ball, order++),
        facts("push",          "direction", "Push",       "PU",  "started right",                        L::Ball, order++),
        facts("slice",         "direction", "Slice",      "SL",  "cut across it",                        L::Ball, order++),
        facts("hook",          "direction", "Hook",       "HK",  "turned over",                          L::Ball, order++),
        facts("pull_hook",     "direction", "Pull-hook",  "PH",  "started left and hooked",              L::Ball, order++),
        facts("push_slice",    "direction", "Push-slice", "PS",  "started right and sliced",             L::Ball, order++),
        facts("fade",          "direction", "Fade",       "FD",  "faded gently",                         L::Ball, order++, "", "Favour the left side."),
        facts("draw",          "direction", "Draw",       "DR",  "drew gently",                          L::Ball, order++, "", "Favour the right side."),
        facts("launch_low",    "flight",    "Low launch", "LO",  "",                                     L::Ball, order++),
        facts("spin_excess",   "flight",    "Too much spin", "SP", "",                                   L::Ball, order++),
        facts("carry_deficit", "flight",    "Short",      "SH",  "",                                     L::Ball, order++),
        facts("early_extension", "", "Early extension", "", "", L::Swing, order++, "you stand up through the ball"),
        facts("over_the_top",    "", "Over the top",    "", "", L::Swing, order++, "your hands come over the top coming down"),
        facts("open_face_to_path",   "", "Open face to path",   "", "", L::Club, order++, "the clubface is open to your swing path at impact"),
        facts("closed_face_to_path", "", "Closed face to path", "", "", L::Club, order++, "the clubface is closed to your swing path at impact"),
        facts("out_to_in_path",      "", "Out-to-in path",      "", "", L::Club, order++, "the club comes across the ball"),
        facts("in_to_out_path",      "", "In-to-out path",      "", "", L::Club, order++, "the club comes from the inside"),
        facts("poor_posture",        "", "Poor posture",        "", "", L::Root, order++, "your posture is poor"),
    };
    for (const ShotConditionFacts &f : list) p.conditions.insert(f.id, f);

    const auto cause = [&p](const char *from, const char *to) { p.causes[QString::fromLatin1(from)] << QString::fromLatin1(to); };
    const auto phys = [&p](const char *from, const char *to) { p.physics[QString::fromLatin1(from)] << QString::fromLatin1(to); };
    const auto excl = [&p](const char *a, const char *b) {
        p.excludes[QString::fromLatin1(a)] << QString::fromLatin1(b);
        p.excludes[QString::fromLatin1(b)] << QString::fromLatin1(a);
    };
    cause("poor_posture", "early_extension");
    cause("early_extension", "chunk");
    cause("early_extension", "out_to_in_path");
    cause("out_to_in_path", "slice");
    cause("open_face_to_path", "slice");
    cause("open_face_to_path", "push_slice");
    cause("over_the_top", "out_to_in_path");
    phys("chunk", "carry_deficit");
    phys("chunk", "smash_deficit");
    phys("smash_deficit", "carry_deficit");
    phys("slice", "carry_deficit");
    phys("thin", "launch_low");
    excl("chunk", "top");
    excl("chunk", "thin");
    excl("thin", "top");
    excl("pull_hook", "push_slice");
    return p;
}

static ConditionRow rowOf(const char *id, ShotState state = ShotState::Fired, int nSignals = 1)
{
    ConditionRow r;
    r.conditionId      = QString::fromLatin1(id);
    r.state            = state;
    r.firedSignalCount = nSignals;
    return r;
}

// §11 shot 10, in yards: carry 126 m, usual 148 ± 6 m.
static ShotRecord lmShot()
{
    ShotRecord s;
    s.shotId = 10;
    s.club   = QStringLiteral("7 IRON");
    s.carry  = 137.8;
    s.total  = 146.0;
    s.offline = 12.0;
    s.ballSpeed = 106.0;
    s.lmClubSpeed = 79.0;
    s.smash = 1.19;
    return s;
}

static ClubBaselineEntry usual7i()
{
    ClubBaselineEntry e;
    e.club = QStringLiteral("7 IRON");
    e.carryMean = 161.9;
    e.carrySd = 6.6;
    e.speedMean = 82.0;
    e.smashMean = 1.33;
    e.n = 40;
    return e;
}

static std::vector<LinkEvidence> grades(const std::vector<std::pair<QString, QString>> &edges, LinkGrade grade)
{
    std::vector<LinkEvidence> links;
    for (const auto &[from, to] : edges) {
        LinkEvidence l;
        l.from = from;
        l.to = to;
        l.grade = grade;
        links.push_back(l);
    }
    return links;
}

static const QString EE = QStringLiteral("early_extension");
static const QString OTI = QStringLiteral("out_to_in_path");
static const QString CHUNK = QStringLiteral("chunk");
static const QString SL = QStringLiteral("slice");

int main()
{
    const ShotOutcomePack pack = fixturePack();
    const ClubBaselineEntry usual = usual7i();

    std::printf("fat slice, short (§11 shot 10):\n");
    {
        ShotRecord s = lmShot();
        ConditionRow slice = rowOf("slice");
        slice.drivingMeasureId = QStringLiteral("m_spinAxis");
        slice.value = 14.2;
        s.rows = { rowOf("chunk"), slice, rowOf("carry_deficit"), rowOf("early_extension"), rowOf("out_to_in_path") };

        ShotOutcomeSessionFacts session;
        session.links = grades({ { EE, CHUNK }, { EE, OTI }, { OTI, SL } }, LinkGrade::MovedTogether);
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &usual, pack, session);

        check(o.ready, "the shot is ready");
        check(o.phrase == QStringLiteral("Fat slice, 24 yd short"), "the phrase");
        check(o.mixKey == QStringLiteral("fat slice"), "the mix key leaves the quantity out");
        check(o.cell == QStringLiteral("FAT SL") && o.cellLines.size() == 2, "the shot-log cell, two lines");
        check(o.driver.id == EE, "the driver is the upstream condition that reaches both misses");
        check(o.driver.explains == 2 && o.driver.of == 3, "it explains the two headline labels of the three");
        check(o.driver.strength == QStringLiteral("likely"), "a swing cause is never clear");
        check(o.story == QStringLiteral("You stand up through the ball: the club bottomed out behind the ball and cut across it."),
              "the story");
        check(!o.clean, "not clean");

        check(o.carry.present && near(o.carry.shortBy, 24.1, 0.01) && near(o.carry.usual, 161.9, 1e-9), "the carry block against the frozen usual");
        check(o.carry.parts.size() == 3 && o.carry.parts[0].id == QStringLiteral("speed")
                  && o.carry.parts[1].name == QStringLiteral("Fat strike") && o.carry.parts[2].id == QStringLiteral("curve"),
              "speed, strike, curve in causal order");
        double sum = 0.0;
        for (const ShotCarryPart &p : o.carry.parts) sum += p.loss;
        check(near(sum, o.carry.shortBy, 1e-6) && near(o.carry.residual, 0.0, 1e-6), "the parts foot to the shortfall");
        check(o.carry.parts[0].loss < o.carry.parts[1].loss && o.carry.parts[2].loss > 0.0, "the strike is the biggest part");
        check(o.carry.line == QStringLiteral("24 yd short of your usual 162: 16 from the fat, 6 slower club speed, 2 curve"),
              "the glance line");
        check(o.carry.provenance == QStringLiteral("against your usual 162 ± 7 yd"), "a modelled distance carries its provenance");

        // A smash shortfall as an extra label is implied by the fat and drops from the words.
        ShotRecord withSmash = s;
        withSmash.rows.push_back(rowOf("smash_deficit"));
        const ShotOutcome o2 = composeShotOutcome(withSmash, {}, QString(), &usual, pack, session);
        check(o2.phrase == QStringLiteral("Fat slice, 24 yd short"), "smash implied by the fat stays out of the phrase");
        check(o2.labels.size() == 4, "but remains a label");

        const auto carryPhysics = std::find_if(o.links.begin(), o.links.end(), [](const ShotOutcomeLink &l) {
            return l.physics && l.fromId == CHUNK && l.toId == QStringLiteral("carry_deficit");
        });
        check(carryPhysics != o.links.end() && carryPhysics->words == QStringLiteral("from the fat"), "outcome to outcome reads from the fat");
        const auto swingLink = std::find_if(o.links.begin(), o.links.end(), [](const ShotOutcomeLink &l) {
            return l.fromId == EE && l.toId == CHUNK;
        });
        check(swingLink != o.links.end() && swingLink->words == QStringLiteral("seen with"), "swing to ball is seen with");
    }

    std::printf("push-slice, a measured ball-from-club link:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("push"), rowOf("slice"), rowOf("push_slice"), rowOf("open_face_to_path") };
        ShotOutcomeSessionFacts graded;
        graded.links = grades({ { QStringLiteral("open_face_to_path"), QStringLiteral("push_slice") } }, LinkGrade::ConditionallyDependent);
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), nullptr, pack, graded);
        check(o.phrase == QStringLiteral("Push-slice"), "the compound names itself once");
        check(o.driver.id == QStringLiteral("open_face_to_path") && o.driver.strength == QStringLiteral("clear"),
              "clear for a ball-from-club link measured on this shot");
        const auto link = std::find_if(o.links.begin(), o.links.end(), [](const ShotOutcomeLink &l) { return !l.physics; });
        check(link != o.links.end() && link->words == QStringLiteral("because"), "and the link says because");

        ShotOutcomeSessionFacts cold;
        cold.links = grades({ { QStringLiteral("open_face_to_path"), QStringLiteral("push_slice") } }, LinkGrade::Coherent);
        check(composeShotOutcome(s, {}, QString(), nullptr, pack, cold).driver.strength == QStringLiteral("possible"),
              "a weak session grade keeps it possible");
        check(composeShotOutcome(s, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()).driver.strength.isEmpty(),
              "an ungraded link says nothing");

        // Corrected to the label: the golfer's word, not a measurement.
        ShotRecord fixedShot = lmShot();
        fixedShot.rows = { rowOf("open_face_to_path"), rowOf("slice", ShotState::Clean) };
        const ShotOutcome corrected = composeShotOutcome(
            fixedShot, { ShotCorrection{ QStringLiteral("direction"), QString(), QStringLiteral("push_slice") } },
            QString(), nullptr, pack, graded);
        check(corrected.driver.id == QStringLiteral("open_face_to_path") && corrected.driver.strength == QStringLiteral("likely"),
              "a corrected label is not measured, so never clear");
    }

    std::printf("clean:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("chunk", ShotState::Clean), rowOf("slice", ShotState::Clean), rowOf("carry_deficit", ShotState::Clean) };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(o.clean && o.phrase == QStringLiteral("Good shot") && o.labels.empty(), "no labels is a good shot");
        check(o.driver.id.isEmpty() && o.noReasonText.isEmpty(), "and looks for no reason");
        check(!o.carry.present, "not short, no waterfall");
    }

    std::printf("intended fade:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("fade"), rowOf("over_the_top"), rowOf("slice", ShotState::Clean) };
        const ShotOutcome o = composeShotOutcome(s, {}, QStringLiteral("fade"), &usual, pack, ShotOutcomeSessionFacts());
        check(o.phrase == QStringLiteral("Fade, as planned"), "the plan is named");
        check(o.clean && o.labels.size() == 1 && o.labels[0].intended && !o.labels[0].miss, "an intended fade is clean");
        check(o.driver.id.isEmpty(), "and has no driver");

        const ShotOutcome unintended = composeShotOutcome(s, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(unintended.phrase == QStringLiteral("Fade") && unintended.clean && !unintended.labels[0].intended,
              "a fade nobody planned is still clean, without the plan");
    }

    std::printf("no driver:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("slice") };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(o.driver.id.isEmpty(), "no driver");
        check(o.story == QStringLiteral("Cut across it."), "the physics sentence alone");
        check(o.noReasonText == QStringLiteral("No reason found"), "and the plain words");
    }

    std::printf("excludes: Thin top is impossible:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("thin", ShotState::Fired, 1), rowOf("top", ShotState::Fired, 3) };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(o.labels.size() == 1 && o.labels[0].id == QStringLiteral("top"), "the more specific reading is kept");
        check(o.phrase == QStringLiteral("Top"), "so the phrase is Top");

        ShotRecord confident = lmShot();
        ConditionRow thin = rowOf("thin", ShotState::Fired, 1);
        thin.confidence = 1.0f;
        ConditionRow top = rowOf("top", ShotState::Fired, 3);
        top.confidence = 0.5f;
        confident.rows = { thin, top };
        check(composeShotOutcome(confident, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()).labels[0].id == QStringLiteral("thin"),
              "confidence decides first");
    }

    std::printf("corrections:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("chunk") };
        const std::vector<ShotCorrection> fix{ ShotCorrection{ QStringLiteral("strike"), QStringLiteral("chunk"), QStringLiteral("thin") } };
        const ShotOutcome o = composeShotOutcome(s, fix, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(o.labels.size() == 1 && o.labels[0].id == QStringLiteral("thin") && o.labels[0].corrected, "the correction replaces the label");
        check(o.phrase == QStringLiteral("Thin"), "and the phrase follows");
        const ShotOutcome none = composeShotOutcome(
            s, { ShotCorrection{ QStringLiteral("strike"), QStringLiteral("chunk"), QString() } }, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(none.labels.empty(), "a correction to none removes it");
    }

    std::printf("left-hander gloss:\n");
    {
        ShotRecord s = lmShot();
        s.handedness = QStringLiteral("Left");
        ConditionRow slice = rowOf("slice");
        s.rows = { slice, rowOf("out_to_in_path") };
        ShotOutcomeSessionFacts session;
        session.links = grades({ { OTI, SL } }, LinkGrade::MovedTogether);
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), nullptr, pack, session);
        check(o.labels.size() == 1 && o.labels[0].id == SL && o.labels[0].displayId == QStringLiteral("hook"), "the row stays, the name mirrors");
        check(o.phrase == QStringLiteral("Hook"), "a left-hander's row reads in their words");
        check(o.labels[0].story == QStringLiteral("turned over"), "the story uses the glossed condition's words");
        check(o.driver.id == OTI && o.driver.displayId == QStringLiteral("in_to_out_path"), "the delivery pair is glossed too");

        ShotRecord lefty = lmShot();
        lefty.handedness = QStringLiteral("Left");
        lefty.rows = { rowOf("fade") };
        check(composeShotOutcome(lefty, {}, QStringLiteral("draw"), nullptr, pack, ShotOutcomeSessionFacts()).phrase
                  == QStringLiteral("Draw, as planned"),
              "intent is the golfer's word, so their draw is the row's fade");

        check(outcomePlayTo(pack, QStringLiteral("draw"), QStringLiteral("Left")) == QStringLiteral("Favour the right side."),
              "playTo mirrors its left and right words");
        check(outcomePlayTo(pack, QStringLiteral("draw"), QStringLiteral("Right")) == QStringLiteral("Favour the right side."),
              "a right-hander reads it as written");
        check(mirrorLeftRightWords(QStringLiteral("Left of the green, right of the bunker")) == QStringLiteral("Right of the green, left of the bunker"),
              "the mirror keeps case");
        check(glossedConditionId(QStringLiteral("slice"), QString()) == SL, "no handedness: no gloss");
    }

    std::printf("a missing LM reading is never clean:\n");
    {
        ShotRecord s;
        s.club = QStringLiteral("7 IRON");
        s.rows = { rowOf("slice"), rowOf("carry_deficit"), rowOf("chunk", ShotState::Clean) };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(o.noReading && o.labels.empty(), "no direction or flight labels without the reading");
        check(!o.clean, "and never clean");
        check(o.phrase == QStringLiteral("No ball reading"), "said plainly");

        ShotRecord fat = s;
        fat.rows = { rowOf("chunk") };
        const ShotOutcome withStrike = composeShotOutcome(fat, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(withStrike.labels.size() == 1 && withStrike.phrase == QStringLiteral("Fat") && !withStrike.clean,
              "a strike seen by the impact camera stands");

        ShotRecord dead = lmShot();
        dead.rows = { rowOf("slice", ShotState::NotAssessable), rowOf("carry_deficit", ShotState::NotAssessable) };
        const ShotOutcome unassessed = composeShotOutcome(dead, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(!unassessed.clean, "a reading the rows could not assess is not clean either");
    }

    std::printf("the waterfall when measures are missing:\n");
    {
        ShotRecord s = lmShot();
        s.lmClubSpeed = std::numeric_limits<double>::quiet_NaN();
        s.rows = { rowOf("carry_deficit") };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(o.carry.present && o.carry.parts.size() == 1 && o.carry.parts[0].id == QStringLiteral("strike"),
              "no club speed, no speed part");
        check(o.carry.residual > 1.0 && o.carry.residualShown, "the residual grows");
        check(o.phrase == QStringLiteral("24 yd short"), "a short shot alone is the quantity");
        check(o.mixKey == QStringLiteral("short"), "and counts as short");

        ShotRecord noUsual = lmShot();
        noUsual.rows = { rowOf("carry_deficit") };
        const ShotOutcome o2 = composeShotOutcome(noUsual, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(!o2.carry.present && o2.phrase == QStringLiteral("Short"), "no history, no waterfall: the word alone");
    }

    std::printf("driver selection:\n");
    {
        // Both reach the slice; the session's tier decides.
        ShotRecord s = lmShot();
        s.rows = { rowOf("slice"), rowOf("open_face_to_path"), rowOf("out_to_in_path") };
        ShotOutcomeSessionFacts session;
        session.tiers.insert(OTI, Tier::Pattern);
        session.tiers.insert(QStringLiteral("open_face_to_path"), Tier::Watching);
        check(composeShotOutcome(s, {}, QString(), nullptr, pack, session).driver.id == OTI, "the higher session tier wins a tie");

        // Reaches the outcome only through physics on an outcome that was not on this shot: not a driver.
        ShotRecord noFat = lmShot();
        noFat.rows = { rowOf("carry_deficit"), rowOf("early_extension") };
        check(composeShotOutcome(noFat, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()).driver.id.isEmpty(),
              "a physics link needs both ends on the shot");
    }

    std::printf("a fade or draw never stands beside a slice or hook:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("slice"), rowOf("fade") };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(o.labels.size() == 1 && o.labels[0].id == SL && o.phrase == QStringLiteral("Slice"), "a slice takes the fade's label");

        ShotRecord compound = lmShot();
        compound.rows = { rowOf("pull_hook"), rowOf("draw") };
        const ShotOutcome c = composeShotOutcome(compound, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(c.labels.size() == 1 && c.labels[0].id == QStringLiteral("pull_hook"), "so does a compound miss");
    }

    std::printf("a left-hander's left and right:\n");
    {
        ShotOutcomePack lefty = pack;
        lefty.conditions[QStringLiteral("push")].golfer = QStringLiteral("you start it right");
        lefty.conditions[QStringLiteral("in_to_out_path")].golfer = QStringLiteral("the club swings out to the right");

        ShotRecord pull = lmShot();
        pull.handedness = QStringLiteral("Left");
        pull.rows = { rowOf("pull") };
        const ShotOutcome o = composeShotOutcome(pull, {}, QString(), nullptr, lefty, ShotOutcomeSessionFacts());
        check(o.labels[0].displayId == QStringLiteral("push") && o.labels[0].golfer == QStringLiteral("you start it left"),
              "the golfer phrase says where the ball went");
        check(o.labels[0].story == QStringLiteral("started left"), "so does the story clause");

        ShotRecord path = lmShot();
        path.handedness = QStringLiteral("Left");
        path.rows = { rowOf("slice"), rowOf("out_to_in_path") };
        const ShotOutcome d = composeShotOutcome(path, {}, QString(), nullptr, lefty, ShotOutcomeSessionFacts());
        check(d.driver.golfer == QStringLiteral("the club swings out to the left"), "and the driver's phrase");

        ShotRecord right = pull;
        right.handedness = QStringLiteral("Right");
        check(composeShotOutcome(right, {}, QString(), nullptr, lefty, ShotOutcomeSessionFacts()).labels[0].story
                  == QStringLiteral("started left"),
              "a right-hander reads the words as authored");
    }

    std::printf("the waterfall fills in causal order:\n");
    {
        ClubBaselineEntry b = usual;
        b.carryMean = 160.0;
        b.speedMean = 80.0;
        b.smashMean = 1.30;

        // Short 20. The formula parts: speed 5, strike 13, then the curve given; launch / spin would be 10.
        const auto shotWithCurve = [](double curveYards) {
            ShotRecord s = lmShot();
            s.carry       = 140.0;
            s.lmClubSpeed = 77.5;
            s.smash       = 1.30 * (1.0 - 13.0 / 155.0);
            ConditionRow slice = rowOf("slice");
            slice.drivingMeasureId = QStringLiteral("m_spinAxis");
            slice.value = std::acos(1.0 - curveYards / 140.0) * 180.0 / M_PI;
            ConditionRow launch = rowOf("launch_low");
            launch.value      = 2.0;
            launch.corridorLo = 2.0 + 10.0 / (160.0 * kCarryLossPerLaunchDegree);
            launch.corridorHi = 30.0;
            s.rows = { slice, launch, rowOf("carry_deficit") };
            return s;
        };
        const auto partOf = [](const ShotOutcome &o, const char *id) -> const ShotCarryPart * {
            for (const ShotCarryPart &p : o.carry.parts)
                if (p.id == QLatin1String(id)) return &p;
            return nullptr;
        };

        const ShotOutcome o = composeShotOutcome(shotWithCurve(4.0), {}, QString(), &b, pack, ShotOutcomeSessionFacts());
        const ShotCarryPart *speed = partOf(o, "speed"), *strike = partOf(o, "strike"), *curve = partOf(o, "curve");
        check(speed && strike && curve && near(speed->loss, 5.0, 1e-6) && near(strike->loss, 13.0, 1e-6),
              "the invented part never shrinks the formula parts");
        check(curve && near(curve->loss, 2.0, 1e-6) && !partOf(o, "launch_spin"), "the last to arrive takes what is left");
        check(o.carry.scaled && near(o.carry.residual, 0.0, 1e-6), "and the block says it was cut");

        const ShotOutcome withRoom = composeShotOutcome(shotWithCurve(1.0), {}, QString(), &b, pack, ShotOutcomeSessionFacts());
        const ShotCarryPart *launchSpin = partOf(withRoom, "launch_spin");
        check(launchSpin && near(launchSpin->loss, 1.0, 1e-6) && launchSpin->modelled, "launch / spin is capped at the remainder and marked");
        check(partOf(withRoom, "curve") && !partOf(withRoom, "curve")->modelled, "a formula part is not marked");

        ShotRecord fits = shotWithCurve(1.0);
        fits.rows = { rowOf("carry_deficit") };
        const ShotOutcome unscaled = composeShotOutcome(fits, {}, QString(), &b, pack, ShotOutcomeSessionFacts());
        check(!unscaled.carry.scaled && near(unscaled.carry.residual, 2.0, 1e-6), "parts that fit are not cut");
    }

    std::printf("a usual has to be earned:\n");
    {
        ClubBaselineEntry fewShots = usual;
        fewShots.n = kClubBaselineMinShots - 1;
        ShotRecord s = lmShot();
        s.rows = { rowOf("carry_deficit") };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &fewShots, pack, ShotOutcomeSessionFacts());
        check(!o.carry.present, "no waterfall under the minimum shots");
        check(o.phrase == QStringLiteral("Short"), "and the phrase keeps the word without a distance");

        fewShots.n = kClubBaselineMinShots;
        check(composeShotOutcome(s, {}, QString(), &fewShots, pack, ShotOutcomeSessionFacts()).carry.present,
              "at the minimum it stands");
    }

    std::printf("clean needs a direction read:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("carry_deficit", ShotState::Clean), rowOf("launch_low", ShotState::Clean),
                   rowOf("slice", ShotState::NotAssessable), rowOf("pull", ShotState::NotAssessable) };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &usual, pack, ShotOutcomeSessionFacts());
        check(!o.clean && o.phrase != QStringLiteral("Good shot"), "carry alone is not a clean shot");

        s.rows.push_back(rowOf("push", ShotState::Clean));
        check(composeShotOutcome(s, {}, QString(), &usual, pack, ShotOutcomeSessionFacts()).clean, "one assessed direction row is enough");
    }

    std::printf("a correction settles its family's excludes:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("top") };
        const ShotOutcome o = composeShotOutcome(
            s, { ShotCorrection{ QStringLiteral("strike"), CHUNK, QStringLiteral("thin") } }, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(o.labels.size() == 1 && o.labels[0].id == QStringLiteral("thin") && o.phrase == QStringLiteral("Thin"),
              "the corrected label removes what it excludes");
    }

    std::printf("the session mix:\n");
    {
        const auto outcome = [&](std::initializer_list<const char *> ids) {
            ShotRecord s = lmShot();
            for (const char *id : ids) s.rows.push_back(rowOf(id));
            if (s.rows.empty()) s.rows = { rowOf("slice", ShotState::Clean) };
            return composeShotOutcome(s, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        };
        std::vector<ShotOutcome> shots{
            outcome({}), outcome({}),
            outcome({ "slice" }), outcome({ "slice" }), outcome({ "slice" }),
            outcome({ "chunk", "slice" }), outcome({ "chunk", "slice" }),
            outcome({ "chunk" }),
        };
        ShotRecord fixedShot = lmShot();
        fixedShot.rows = { rowOf("thin"), rowOf("pull") };
        shots.push_back(composeShotOutcome(fixedShot, { ShotCorrection{ QStringLiteral("strike"), QStringLiteral("chunk"), QStringLiteral("thin") } },
                                           QString(), nullptr, pack, ShotOutcomeSessionFacts()));
        ShotRecord noneShot;
        noneShot.club = QStringLiteral("7 IRON");
        noneShot.rows = { rowOf("slice", ShotState::NotAssessable) };
        shots.push_back(composeShotOutcome(noneShot, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()));

        const ShotMix mix = composeSessionMix(shots);
        check(mix.line == QStringLiteral("2 clean · 3 slice · 2 fat slice · 1 fat · 1 thin pull · 1 no reading"),
              "combinations count as combinations; clean first; a blind shot apart");
        check(mix.corrections == 1, "corrections are counted");
    }

    std::printf("a strike read from the flight:\n");
    {
        const auto wedge = [](int id, double launch, double start, double smash, double attack, double carry) {
            ShotRecord s;
            s.shotId = id;
            s.club = QStringLiteral("GAP WEDGE");
            s.carry = carry;
            s.ballSpeed = 80.0;
            s.lmClubSpeed = 75.0;
            s.smash = smash;
            s.launchAngle = launch;
            s.startDirection = start;
            s.attackAngle = attack;
            s.rows = { rowOf("chunk", ShotState::NotAssessable), rowOf("thin", ShotState::NotAssessable),
                       rowOf("shank", ShotState::NotAssessable), rowOf("slice", ShotState::Clean), rowOf("hook", ShotState::Clean) };
            return s;
        };
        std::vector<ShotRecord> session;
        for (int i = 0; i < 6; ++i) session.push_back(wedge(i + 1, 30.0, -2.0, 1.09, -3.5, 100.0));
        const QHash<QString, ClubFlightUsual> usuals = sessionFlightUsuals(session);
        ShotOutcomeSessionFacts facts;
        facts.flightUsuals = usuals;

        // 5 October, shot 8.
        ShotRecord shank = wedge(8, 1.4, 12.6, 1.07, -6.0, 11.0);
        shank.rows.push_back(rowOf("push"));
        shank.rows.push_back(rowOf("launch_low"));
        shank.rows.push_back(rowOf("push_slice"));
        shank.rows.push_back(rowOf("carry_deficit"));
        check(strikeFromFlight(shank, usuals) == QStringLiteral("shank"), "low and sharp right is a shank");
        const ShotOutcome o = composeShotOutcome(shank, {}, QString(), nullptr, pack, facts);
        const auto label = [&o](const char *id) {
            return std::find_if(o.labels.begin(), o.labels.end(), [id](const ShotOutcomeLabel &l) { return l.id == QLatin1String(id); });
        };
        check(label("shank") != o.labels.end() && label("shank")->inferred, "and it says where it came from");
        check(o.phrase.startsWith(QStringLiteral("Shank")), "it leads the phrase");
        check(!o.phrase.contains(QStringLiteral("slice"), Qt::CaseInsensitive), "the slice is the shank, not a second miss");
        check(label("push_slice")->absorbed && label("launch_low")->absorbed, "its signature is folded in");
        check(o.producedLine == QStringLiteral("From the shank: push-slice, low launch"), "and said once");
        check(!o.clean, "a shank is never clean");

        const ShotOutcome taken = composeShotOutcome(
            shank, { ShotCorrection{ QStringLiteral("strike"), QStringLiteral("shank"), QString() } }, QString(), nullptr, pack, facts);
        check(std::none_of(taken.labels.begin(), taken.labels.end(), [](const ShotOutcomeLabel &l) { return l.id == QLatin1String("shank"); }),
              "Not right? takes it off");
        check(taken.producedLine.isEmpty(), "and the push-slice stands on its own again");

        ShotRecord lefty = shank;
        lefty.handedness = QStringLiteral("Left");
        check(strikeFromFlight(lefty, usuals) == QStringLiteral("thin"), "a left-hander's hosel side is the other way");
        lefty.startDirection = -12.6;
        check(strikeFromFlight(lefty, usuals) == QStringLiteral("shank"), "so left is their shank");

        check(strikeFromFlight(wedge(1, 5.0, -3.9, 1.05, -3.5, 60.0), usuals) == QStringLiteral("thin"), "low launch, smash held: thin");
        check(strikeFromFlight(wedge(1, 5.0, -3.9, 0.85, -3.5, 40.0), usuals) == QStringLiteral("top"), "low launch, smash gone: top");
        check(strikeFromFlight(wedge(1, 31.0, -1.0, 0.98, -5.6, 86.0), usuals) == QStringLiteral("chunk"), "smash down and steeper: fat");
        check(strikeFromFlight(wedge(1, 31.0, -1.0, 0.94, -2.0, 90.0), usuals).isEmpty(), "smash down but shallower: a heel, not fat");
        check(strikeFromFlight(wedge(1, 50.0, -1.0, 1.0, -6.0, 60.0), usuals) == QStringLiteral("sky"), "very high, steeper, smash down: sky");
        check(strikeFromFlight(wedge(1, 30.0, -1.0, 1.09, -3.5, 100.0), usuals).isEmpty(), "a normal shot reads as nothing");

        ShotRecord measured = wedge(1, 1.4, 12.6, 1.07, -6.0, 11.0);
        measured.rows = { rowOf("chunk", ShotState::Clean), rowOf("thin", ShotState::Clean), rowOf("shank", ShotState::Clean) };
        check(strikeFromFlight(measured, usuals).isEmpty(), "a strike that was measured is never second-guessed");

        ShotRecord part = shank;
        part.swingLength = QStringLiteral("partial");
        check(strikeFromFlight(part, usuals).isEmpty(), "a part swing is left alone");

        check(strikeFromFlight(shank, QHash<QString, ClubFlightUsual>()).isEmpty(), "no usual for the club, nothing inferred");
        std::vector<ShotRecord> few(3, wedge(1, 30.0, -2.0, 1.09, -3.5, 100.0));
        check(strikeFromFlight(shank, sessionFlightUsuals(few)).isEmpty(), "three shots aren't a usual");

        ShotRecord ballOnly = shank;
        ballOnly.smash = std::numeric_limits<double>::quiet_NaN();
        ballOnly.attackAngle = std::numeric_limits<double>::quiet_NaN();
        check(strikeFromFlight(ballOnly, usuals) == QStringLiteral("shank"), "ball-only: a shank needs only the flight");
        ballOnly.startDirection = -2.0;
        check(strikeFromFlight(ballOnly, usuals) == QStringLiteral("thin"), "ball-only: no smash to call it a top");
        ShotRecord ballOnlyFat = wedge(1, 31.0, -1.0, 1.0, -5.6, 86.0);
        ballOnlyFat.smash = std::numeric_limits<double>::quiet_NaN();
        check(strikeFromFlight(ballOnlyFat, usuals).isEmpty(), "ball-only: a fat needs the smash");
    }

    std::printf("the words say what was judged:\n");
    {
        ShotRecord s = lmShot();
        s.rows = { rowOf("push", ShotState::Clean), rowOf("slice", ShotState::Clean), rowOf("hook", ShotState::Clean),
                   rowOf("chunk", ShotState::Clean), rowOf("thin", ShotState::Clean) };
        check(composeShotOutcome(s, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()).phrase == QStringLiteral("Good shot"), "all judged: a good shot");

        ShotRecord noStrike = s;
        noStrike.rows = { rowOf("push", ShotState::Clean), rowOf("slice", ShotState::Clean), rowOf("hook", ShotState::Clean),
                          rowOf("chunk", ShotState::NotAssessable), rowOf("thin", ShotState::NotAssessable) };
        const ShotOutcome ballOnly = composeShotOutcome(noStrike, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts());
        check(ballOnly.clean && ballOnly.phrase == QStringLiteral("Good flight · strike not measured"), "no strike reading: says so");

        ShotRecord noCurve = s;
        noCurve.rows = { rowOf("push", ShotState::Clean), rowOf("slice", ShotState::NotAssessable), rowOf("hook", ShotState::NotAssessable),
                         rowOf("chunk", ShotState::Clean), rowOf("thin", ShotState::Clean) };
        check(composeShotOutcome(noCurve, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()).phrase
                  == QStringLiteral("Good flight · curve not measured"), "no curve reading: says so");

        ShotRecord gated = lmShot();
        gated.rows = { rowOf("push", ShotState::NotAssessable), rowOf("slice", ShotState::NotAssessable) };
        check(composeShotOutcome(gated, {}, QString(), nullptr, pack, ShotOutcomeSessionFacts()).phrase == QStringLiteral("Flight not judged"),
              "a reading the rows refused isn't 'no reading'");
    }

    std::printf("a ball-only waterfall:\n");
    {
        ClubBaselineEntry b = usual;
        b.ballSpeedMean = 110.0;
        ShotRecord s = lmShot();
        s.lmClubSpeed = std::numeric_limits<double>::quiet_NaN();
        s.smash = std::numeric_limits<double>::quiet_NaN();
        s.ballSpeed = 100.0;
        s.rows = { rowOf("carry_deficit") };
        const ShotOutcome o = composeShotOutcome(s, {}, QString(), &b, pack, ShotOutcomeSessionFacts());
        check(o.carry.present && !o.carry.parts.empty() && o.carry.parts[0].id == QStringLiteral("ball_speed"),
              "the slower ball is named rather than left in the remainder");
        check(near(o.carry.parts[0].loss, 161.9 * (1.0 - 100.0 / 110.0), 0.01), "by its share of the usual carry");

        ShotRecord withClub = lmShot();
        withClub.rows = { rowOf("carry_deficit") };
        const ShotOutcome full = composeShotOutcome(withClub, {}, QString(), &b, pack, ShotOutcomeSessionFacts());
        check(std::none_of(full.carry.parts.begin(), full.carry.parts.end(), [](const ShotCarryPart &p) { return p.id == QLatin1String("ball_speed"); }),
              "with club data it splits into speed and strike as before");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
