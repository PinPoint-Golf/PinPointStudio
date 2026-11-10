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

// The three factors of a shot (shot_factors.h): strike, direction and flight, each one's values and faults;
// how high the ball flew by the first method the shot can use (landing angle, apex/carry, the driver's apex
// against a speed-matched reference); Sky; the left-hander's gloss; a factor's mix; the flight table's reader,
// over the shipped flight_norms.json too.

#include "../../Coaching/shot_factors.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>

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

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

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
        facts("chunk",         "strike",    "Fat",         L::Ball, order++),
        facts("thin",          "strike",    "Thin",        L::Ball, order++),
        facts("strike_toe",    "strike",    "Toe",         L::Ball, order++),
        facts("smash_deficit", "strike",    "Weak strike", L::Ball, order++),
        facts("slice",         "direction", "Slice",       L::Ball, order++),
        facts("hook",          "direction", "Hook",        L::Ball, order++),
        facts("push",          "direction", "Push",        L::Ball, order++),
        facts("pull",          "direction", "Pull",        L::Ball, order++),
        facts("push_slice",    "direction", "Push-slice",  L::Ball, order++),
        facts("pull_hook",     "direction", "Pull-hook",   L::Ball, order++),
        facts("fade",          "direction", "Fade",        L::Ball, order++),
        facts("draw",          "direction", "Draw",        L::Ball, order++),
        facts("sky",           "flight",    "Sky",         L::Ball, order++),
        facts("carry_deficit", "flight",    "Short",       L::Ball, order++),
    };
    for (const ShotConditionFacts &f : list) p.conditions.insert(f.id, f);
    return p;
}

static const char *const kRowIds[] = { "chunk", "thin", "strike_toe", "smash_deficit", "slice", "hook", "push", "pull",
                                       "push_slice", "pull_hook", "fade", "draw", "sky" };

// A shot with a reading: `fired` fire, `unread` couldn't be assessed, every other row is clean.
static ShotRecord shot(int id, std::initializer_list<const char *> fired, std::initializer_list<const char *> unread = {},
                       const char *club = "7 IRON")
{
    ShotRecord s;
    s.shotId = id;
    s.club   = QString::fromLatin1(club);
    s.carry  = 160.0;
    s.swingLength = QStringLiteral("full");
    const auto in = [](std::initializer_list<const char *> list, const char *rid) {
        return std::any_of(list.begin(), list.end(), [rid](const char *f) { return qstrcmp(f, rid) == 0; });
    };
    for (const char *rid : kRowIds) {
        ConditionRow r;
        r.conditionId = QString::fromLatin1(rid);
        r.state = in(fired, rid) ? ShotState::Fired : in(unread, rid) ? ShotState::NotAssessable : ShotState::Clean;
        r.firedSignalCount = r.state == ShotState::Fired ? 1 : 0;
        s.rows.push_back(r);
    }
    return s;
}

static FlightNorms fixtureNorms()
{
    // The shipped shape (schema 2) with the spec's §4 numbers.
    const char *json = R"({
      "schema": 2,
      "categories": {
        "driver":         { "label": "Driver",           "clubs": ["DRIVER"] },
        "fairway_hybrid": { "label": "Fairway / hybrid", "clubs": ["3 WOOD", "5 WOOD", "3 HYBRID", "4 HYBRID"] },
        "mid_irons":      { "label": "Mid irons",        "clubs": ["5 IRON", "6 IRON", "7 IRON"] },
        "wedges":         { "label": "Wedges",           "clubs": ["GAP WEDGE", "SAND WEDGE", "LOB WEDGE"] }
      },
      "methods": [
        { "id": "landing",    "bands": { "driver": [33, 41], "fairway_hybrid": [37, 45], "mid_irons": [44, 52], "wedges": [49, 56] } },
        { "id": "apex_carry", "bands": { "driver": [0.10, 0.14], "fairway_hybrid": [0.12, 0.16], "mid_irons": [0.16, 0.21], "wedges": [0.19, 0.25] } },
        { "id": "apex", "band": [0.85, 1.15],
          "reference": { "driver": { "points": [ { "ballSpeedMph": 167, "apexYd": 32 }, { "ballSpeedMph": 140, "apexYd": 25 } ] } } }
      ],
      "sources": [ { "id": "s" } ],
      "notes": [ "a note" ]
    })";
    return flightNormsFromJson(QJsonDocument::fromJson(QByteArray(json)).object());
}

static ShotFactors factorsOf(const ShotRecord &s, const ShotOutcomePack &pack, const FlightNorms &norms,
                             const QHash<QString, ClubFlightUsual> &usuals = {}, const QString &intent = QString(),
                             const std::vector<ShotCorrection> &corrections = {})
{
    ShotOutcomeSessionFacts facts;
    facts.flightUsuals = usuals;
    const ShotOutcome o = composeShotOutcome(s, corrections, intent, nullptr, pack, facts);
    return shotFactors(s, o, pack, norms, usuals);
}

int main()
{
    std::printf("shot_factors_test\n");
    const ShotOutcomePack pack = fixturePack();
    const FlightNorms norms = fixtureNorms();

    std::printf("\nstrike:\n");
    {
        const ShotFactors clean = factorsOf(shot(1, {}), pack, norms);
        check(clean.strike.read && clean.strike.values == QStringList{ "solid" } && clean.strike.faults.isEmpty(),
              "assessed and nothing fired: Solid, not a fault");
        const ShotFactors thin = factorsOf(shot(2, { "thin" }), pack, norms);
        check(thin.strike.read && thin.strike.values == QStringList{ "thin" } && thin.strike.faults == QStringList{ "thin" },
              "a thin is the strike's value and its fault");
        const ShotFactors two = factorsOf(shot(3, { "strike_toe", "smash_deficit" }), pack, norms);
        check(two.strike.values == QStringList{ "strike_toe", "smash_deficit" } && two.strike.faults == two.strike.values,
              "a toe and a weak strike: the shot carries both");
        ShotRecord unread = shot(4, {}, { "chunk", "thin", "strike_toe", "smash_deficit" });
        check(!factorsOf(unread, pack, norms).strike.read, "no strike row assessed and no smash: strike not read");
        unread.smash = 1.3;
        const ShotFactors bySmash = factorsOf(unread, pack, norms);
        check(bySmash.strike.read && bySmash.strike.values == QStringList{ "solid" }, "a smash read is a strike assessed");

        // A sticker placed off: every strike reads low, the flight normal (§3.7). The ledger overrules the
        // measured thin the flight doesn't bear out (checkStrikesAgainstFlight), so it is no label and the shot,
        // its strike read all the same, is Solid.
        std::vector<ShotRecord> session;
        for (int i = 0; i < 6; ++i) {
            ShotRecord s = shot(10 + i, i == 0 ? std::initializer_list<const char *>{ "thin" } : std::initializer_list<const char *>{});
            s.launchAngle = 16.0;
            session.push_back(s);
        }
        checkStrikesAgainstFlight(session, norms);
        const QHash<QString, ClubFlightUsual> usuals = sessionFlightUsuals(session);
        const ShotFactors sticker = factorsOf(session[0], pack, norms, usuals);
        check(sticker.strike.read && sticker.strike.values == QStringList{ "solid" } && sticker.strike.faults.isEmpty(),
              "a sticker-offset low strike on a normal flight: Solid");
        std::vector<ShotRecord> realThin = session;
        realThin[0].launchAngle = 6.0;
        checkStrikesAgainstFlight(realThin, norms);
        check(factorsOf(realThin[0], pack, norms, sessionFlightUsuals(realThin)).strike.faults == QStringList{ "thin" },
              "the same strike launched low: Thin");
    }

    std::printf("\ndirection:\n");
    {
        const ShotFactors straight = factorsOf(shot(1, {}), pack, norms);
        check(straight.direction.read && straight.direction.values == QStringList{ "straight" } && straight.direction.faults.isEmpty(),
              "assessed and nothing fired: Straight");
        const ShotFactors pull = factorsOf(shot(2, { "pull" }), pack, norms);
        check(pull.direction.values == QStringList{ "pull" } && pull.direction.faults == QStringList{ "pull" }, "a pull is a fault");
        const ShotFactors fade = factorsOf(shot(3, { "fade" }), pack, norms);
        check(fade.direction.read && fade.direction.values == QStringList{ "fade" } && fade.direction.faults.isEmpty(),
              "a fade is a value, never a fault");
        const ShotFactors draw = factorsOf(shot(4, { "draw" }), pack, norms);
        check(draw.direction.values == QStringList{ "draw" } && draw.direction.faults.isEmpty(), "so is a draw");
        const ShotFactors intended = factorsOf(shot(5, { "fade" }), pack, norms, {}, QStringLiteral("fade"));
        check(intended.direction.read && intended.direction.values == QStringList{ "fade" } && intended.direction.faults.isEmpty(),
              "an intended fade: still the Fade value, the shot judged");
        const ShotFactors pullFade = factorsOf(shot(6, { "pull", "fade" }), pack, norms);
        check(pullFade.direction.values == QStringList{ "pull", "fade" } && pullFade.direction.faults == QStringList{ "pull" },
              "a pull fade: both values, the pull the fault");
        const ShotFactors pushSlice = factorsOf(shot(7, { "push", "slice", "push_slice" }), pack, norms);
        check(pushSlice.direction.values.contains(QStringLiteral("push_slice")) && pushSlice.direction.values.contains(QStringLiteral("slice"))
                  && pushSlice.direction.values.contains(QStringLiteral("push")) && pushSlice.direction.faults.size() == 3,
              "a push slice brings its halves: Push slice, Slice and Push");
        check(factorsOf(shot(8, { "slice", "fade" }), pack, norms).direction.values == QStringList{ "slice" },
              "a slice is never a fade too");
        ShotRecord noLm = shot(9, { "pull" });
        noLm.carry = kNaN;
        const ShotFactors unread = factorsOf(noLm, pack, norms);
        check(!unread.direction.read && unread.strike.read, "no reading: direction not read, the strike still is");
        check(!factorsOf(shot(10, {}, { "slice", "hook", "push", "pull", "push_slice", "pull_hook", "fade", "draw" }), pack, norms).direction.read,
              "no direction row assessed: not read");
    }

    std::printf("\na left-hander:\n");
    {
        ShotRecord slice = shot(1, { "slice" });
        slice.handedness = QStringLiteral("Left");
        const ShotFactors s = factorsOf(slice, pack, norms);
        check(s.direction.values == QStringList{ "hook" } && s.direction.faults == QStringList{ "hook" },
              "a left-hander's slice row is the hook they see");
        ShotRecord fade = shot(2, { "fade" });
        fade.handedness = QStringLiteral("Left");
        check(factorsOf(fade, pack, norms).direction.values == QStringList{ "draw" }, "their fade row is a draw");
        ShotRecord ps = shot(3, { "push_slice" });
        ps.handedness = QStringLiteral("Left");
        const ShotFactors c = factorsOf(ps, pack, norms);
        check(c.direction.values.contains(QStringLiteral("pull_hook")) && c.direction.values.contains(QStringLiteral("hook"))
                  && c.direction.values.contains(QStringLiteral("pull")) && !c.direction.values.contains(QStringLiteral("slice")),
              "their push slice row is a pull hook, with its hook and pull");
    }

    std::printf("\nflight, landing angle first:\n");
    {
        const auto landing = [&](double deg, const char *club = "7 IRON") {
            ShotRecord s = shot(1, {}, {}, club);
            s.descentAngle = deg;
            return factorsOf(s, pack, norms).flight;
        };
        const ShotFactor mid = landing(47.0);
        check(mid.read && mid.values == QStringList{ "mid" } && mid.faults.isEmpty() && mid.method == QStringLiteral("landing")
                  && mid.rawValue == 47.0,
              "a 7 iron landing at 47°: Mid, by the landing angle, the reading kept");
        check(landing(43.9).values == QStringList{ "low" } && landing(44.0).values == QStringList{ "mid" }
                  && landing(52.0).values == QStringList{ "mid" } && landing(52.1).values == QStringList{ "high" },
              "Low under L, High over H, the edges themselves Mid");
        check(landing(38.0, "DRIVER").values == QStringList{ "mid" } && landing(38.0).values == QStringList{ "low" },
              "the same angle is the driver's Mid and the 7 iron's Low: bands by club category");
        check(landing(50.0).faults.isEmpty() && landing(55.0).faults.isEmpty(), "Low, Mid and High are never faults");

        ShotRecord both = shot(2, {});
        both.descentAngle = 47.0;
        both.peakHeight = 150.0;          // 50 yd over 160: an apex/carry of 0.31, High
        const ShotFactor order = factorsOf(both, pack, norms).flight;
        check(order.method == QStringLiteral("landing") && order.values == QStringList{ "mid" },
              "with a landing angle and an apex, the landing angle decides");
    }

    std::printf("\nflight, apex over carry:\n");
    {
        ShotRecord s = shot(1, {});
        s.peakHeight = 90.0;              // feet: 30 yd over a 160 yd carry, 0.1875
        const ShotFactor f = factorsOf(s, pack, norms).flight;
        check(f.read && f.method == QStringLiteral("apex_carry") && f.values == QStringList{ "mid" } && std::fabs(f.rawValue - 0.1875) < 1e-9,
              "no landing angle: apex/carry, the apex in feet turned to yards against the carry in yards (0.1875, Mid)");
        s.peakHeight = 60.0;              // 20 yd: 0.125
        check(factorsOf(s, pack, norms).flight.values == QStringList{ "low" }, "20 yd over 160: Low");
        s.peakHeight = 120.0;             // 40 yd: 0.25
        check(factorsOf(s, pack, norms).flight.values == QStringList{ "high" }, "40 yd over 160: High");

        ShotRecord part = shot(2, {});
        part.peakHeight = 90.0;
        part.swingLength = QStringLiteral("partial");
        check(!factorsOf(part, pack, norms).flight.read, "a part swing skips apex/carry, and with a carry the absolute apex is not used");
        part.descentAngle = 47.0;
        const ShotFactor partLanding = factorsOf(part, pack, norms).flight;
        check(partLanding.read && partLanding.method == QStringLiteral("landing"), "a part swing's landing angle still reads");
    }

    std::printf("\nflight, modelled where the monitor gave no landing angle or apex:\n");
    {
        // A tour-like 7 iron: 120 mph, 16.3°, 7097 rpm (the spec's §3 row).
        ShotRecord s = shot(1, {});
        s.ballSpeed = 120.0;
        s.launchAngle = 16.3;
        s.spinRate = 7097.0;
        LmLaunch in;
        in.ballSpeedMph = 120.0; in.launchAngleDeg = 16.3; in.spinRpm = 7097.0;
        const LmFlightIntegration model = lmIntegrateFlight(in);
        const ShotFactor f = factorsOf(s, pack, norms).flight;
        check(model.has && f.read && f.method == QStringLiteral("landing (modelled)") && std::fabs(f.rawValue - model.descentDeg) < 1e-9
                  && f.values == QStringList{ flightBandValue(model.descentDeg, norms.landing.value(QStringLiteral("mid_irons"))) },
              "no landing angle or apex: the flight model's landing angle (lmIntegrateFlight), marked modelled");
        std::printf("      modelled landing angle: %.1f°\n", model.descentDeg);

        ShotRecord measured = s;
        measured.descentAngle = 40.0;
        const ShotFactor m = factorsOf(measured, pack, norms).flight;
        check(m.method == QStringLiteral("landing") && m.rawValue == 40.0 && m.values == QStringList{ "low" },
              "a measured landing angle wins over the modelled one");
        ShotRecord apex = s;
        apex.peakHeight = 90.0;
        check(factorsOf(apex, pack, norms).flight.method == QStringLiteral("apex_carry"),
              "so does a measured apex over the carry: measured always before modelled");

        ShotRecord noSpin = s;
        noSpin.spinRate = kNaN;
        check(!factorsOf(noSpin, pack, norms).flight.read, "no spin: no model, and the flight is not read");
        ShotRecord noLaunch = s;
        noLaunch.launchAngle = kNaN;
        check(!factorsOf(noLaunch, pack, norms).flight.read, "no launch: no model either");
        check(!std::isfinite(modelledDescentAngle(noSpin)) && std::isfinite(modelledDescentAngle(s)), "modelledDescentAngle says the same");
    }

    std::printf("\nflight, the driver's apex against a speed-matched reference:\n");
    {
        check(std::fabs(flightReferenceApexYd(norms, QStringLiteral("driver"), 140.0) - 25.0) < 1e-9
                  && std::fabs(flightReferenceApexYd(norms, QStringLiteral("driver"), 167.0) - 32.0) < 1e-9
                  && std::fabs(flightReferenceApexYd(norms, QStringLiteral("driver"), 153.5) - 28.5) < 1e-9,
              "the reference: 25 yd at 140 mph, 32 at 167, linear between");
        check(std::fabs(flightReferenceApexYd(norms, QStringLiteral("driver"), 113.0) - 18.0) < 1e-9,
              "below 140 mph the same line carries on (113 mph: 18 yd)");
        check(!std::isfinite(flightReferenceApexYd(norms, QStringLiteral("mid_irons"), 120.0)), "no reference for an iron");

        ShotRecord d = shot(1, {}, {}, "DRIVER");
        d.carry = kNaN;
        d.total = 240.0;                  // still a reading
        d.ballSpeed = 153.5;
        d.peakHeight = 84.0;              // 28 yd over the 28.5 reference: 0.98
        const ShotFactor f = factorsOf(d, pack, norms).flight;
        check(f.read && f.method == QStringLiteral("apex") && f.values == QStringList{ "mid" } && std::fabs(f.rawValue - 28.0 / 28.5) < 1e-9,
              "no landing angle and no carry: the driver's apex against the reference, Mid");
        d.peakHeight = 60.0;              // 20 yd: 0.70
        check(factorsOf(d, pack, norms).flight.values == QStringList{ "low" }, "20 yd at 153.5 mph: Low");
        d.peakHeight = 105.0;             // 35 yd: 1.23
        check(factorsOf(d, pack, norms).flight.values == QStringList{ "high" }, "35 yd: High");
        d.ballSpeed = kNaN;
        check(!factorsOf(d, pack, norms).flight.read, "no ball speed: no reference, not read");

        ShotRecord iron = shot(2, {});
        iron.carry = kNaN;
        iron.total = 170.0;
        iron.ballSpeed = 120.0;
        iron.peakHeight = 90.0;
        check(!factorsOf(iron, pack, norms).flight.read, "an iron's apex alone is never classified: the driver only");
    }

    std::printf("\nflight, what isn't read:\n");
    {
        ShotRecord putt = shot(1, {}, {}, "PUTTER");
        putt.descentAngle = 10.0;
        check(flightClubCategory(norms, QStringLiteral("PUTTER")).isEmpty() && !factorsOf(putt, pack, norms).flight.read,
              "a club in no category (a putter): not read");
        check(!factorsOf(shot(2, {}), pack, norms).flight.read, "no landing angle and no apex: not read");
        ShotRecord noLm = shot(3, {});
        noLm.carry = kNaN;
        noLm.descentAngle = 47.0;
        check(factorsOf(noLm, pack, norms).flight.read, "a landing angle is a reading in itself");
        check(!factorsOf(shot(4, {}), pack, FlightNorms{}).flight.read, "no table: nothing is read from the flight");
        check(!factorsOf(shot(4, {}), pack, norms).flight.read && factorsOf(shot(4, {}), pack, norms).judged(),
              "a shot with its flight unread is still judged on its strike and direction");
    }

    std::printf("\nsky:\n");
    {
        QHash<QString, ClubFlightUsual> usuals;
        ClubFlightUsual driverUsual;
        driverUsual.attack = -1.0;
        driverUsual.carry  = 230.0;
        usuals.insert(QStringLiteral("DRIVER"), driverUsual);
        usuals.insert(QStringLiteral("3 HYBRID"), ClubFlightUsual{ kNaN, kNaN, -3.0, 190.0 });
        usuals.insert(QStringLiteral("7 IRON"), ClubFlightUsual{ kNaN, kNaN, -4.0, 160.0 });

        const auto skyShot = [&](const char *club, double descent, double attack, double carry) {
            ShotRecord s = shot(1, {}, {}, club);
            s.descentAngle = descent;
            s.attackAngle  = attack;
            s.carry        = carry;
            return factorsOf(s, pack, norms, usuals).flight;
        };
        const ShotFactor sky = skyShot("DRIVER", 44.0, -3.0, 170.0);
        check(sky.read && sky.values == QStringList{ "sky" } && sky.faults == QStringList{ "sky" } && sky.method == QStringLiteral("landing"),
              "a driver landing High, 2° steeper than usual, at 74% of the usual carry: Sky, a fault, replacing High");
        check(skyShot("3 HYBRID", 46.0, -5.5, 150.0).values == QStringList{ "sky" }, "a hybrid can be skied too");
        check(skyShot("7 IRON", 53.0, -9.0, 100.0).values == QStringList{ "high" }, "an iron never is: High");
        check(skyShot("DRIVER", 44.0, -2.5, 170.0).values == QStringList{ "high" }, "only 1.5° steeper: High");
        check(skyShot("DRIVER", 44.0, -3.0, 200.0).values == QStringList{ "high" }, "carry at 87% of the usual: High");
        check(skyShot("DRIVER", 39.0, -6.0, 150.0).values == QStringList{ "mid" }, "not High: never Sky");
        check(skyShot("DRIVER", 44.0, kNaN, 170.0).values == QStringList{ "high" }, "no attack angle: High");

        // The measured route (a high strike, low point ahead, a steep attack) no longer names it.
        ShotRecord measured = shot(2, { "sky" }, {}, "DRIVER");
        measured.descentAngle = 38.0;
        const ShotFactors m = factorsOf(measured, pack, norms, usuals);
        check(m.flight.values == QStringList{ "mid" } && m.strike.values == QStringList{ "solid" },
              "a fired sky row is no label: Mid, Solid");
        // The golfer's own word stands.
        const ShotFactors corrected = factorsOf(shot(3, {}, {}, "DRIVER"), pack, norms, usuals, QString(),
                                                { ShotCorrection{ QStringLiteral("flight"), QString(), QStringLiteral("sky") } });
        check(corrected.flight.values == QStringList{ "sky" } && corrected.flight.faults == QStringList{ "sky" }
                  && corrected.flight.method == QStringLiteral("golfer"),
              "a golfer's correction to Sky stands");
    }

    std::printf("\nthe ledger, checked against the flight (checkStrikesAgainstFlight):\n");
    {
        const auto has = [](const ShotOutcome &o, const char *id) {
            return std::any_of(o.labels.begin(), o.labels.end(), [id](const ShotOutcomeLabel &l) { return l.id == QLatin1String(id); });
        };
        const auto row = [](const ShotRecord &s, const char *id) -> const ConditionRow & { return *rowFor(s, QLatin1String(id)); };
        // A 7 iron with a strike height on its thin row: `mm` on the face, launching `launch`.
        const auto iron = [](int id, double mm, double launch, double carry = 158.0) {
            ShotRecord s = shot(id, { "thin" });
            s.launchAngle = launch;
            s.smash       = 1.35;
            s.carry       = carry;
            s.ballSpeed   = 120.0;
            for (ConditionRow &r : s.rows)
                if (r.conditionId == QLatin1String("thin")) {
                    r.drivingMeasureId = QStringLiteral("m_lmStrikeHeight");
                    r.value = mm;
                }
            return s;
        };

        // 4 August 2026: every 7-iron strike read low (about −12 mm) on a normal flight (launch 17–22°).
        std::vector<ShotRecord> aug;
        const double launches[] = { 17.0, 18.5, 19.0, 20.0, 21.0, 22.0, 18.0, 19.5 };
        for (int i = 0; i < 8; ++i) aug.push_back(iron(i + 1, -12.0 - 0.5 * i, launches[i], 150.0 + 2.0 * i));
        const int n = checkStrikesAgainstFlight(aug, norms);
        const bool allOverruled = std::all_of(aug.begin(), aug.end(), [&row](const ShotRecord &s) {
            const ConditionRow &r = row(s, "thin");
            return r.state == ShotState::NotAssessable && r.overruled
                && r.notAssessableReason == QStringLiteral("the strike height disagrees with the ball flight");
        });
        check(n == 8 && allOverruled, "sticker offset: every thin row NotAssessable, overruled, saying why");
        check(row(aug[0], "thin").value == -12.0 && row(aug[7], "thin").value == -15.5, "…its value kept");
        check(!strikeHeightUnread(aug[0]) && !strikeRowUnread(aug[0], "thin"), "…and read: the monitor did give a height");

        QHash<QString, ClubFlightUsual> usuals = sessionFlightUsuals(aug);
        ShotOutcomeSessionFacts facts;
        facts.flightUsuals = usuals;
        int thinLabels = 0, notes = 0, solid = 0;
        for (const ShotRecord &s : aug) {
            const ShotOutcome o = composeShotOutcome(s, {}, QString(), nullptr, pack, facts);
            if (has(o, "thin")) ++thinLabels;
            if (o.strikeFlightNote.startsWith(QStringLiteral("Low on the face (−"))
                && o.strikeFlightNote.endsWith(QStringLiteral(" mm), flight normal"))) ++notes;
            if (shotFactors(s, o, pack, norms, usuals).strike.values == QStringList{ "solid" }) ++solid;
        }
        check(thinLabels == 0 && notes == 8 && solid == 8, "no Thin label, the note on every shot, every strike Solid");
        check(composeShotOutcome(aug[0], {}, QString(), nullptr, pack, facts).strikeFlightNote
                  == QStringLiteral("Low on the face (−12 mm), flight normal"),
              "the note is the overruled row's own reading");

        ClubDistanceSession day;
        day.folder = QStringLiteral("2026-08-04_M_01");
        day.day    = QDate(2026, 8, 4);
        day.shots  = aug;
        const std::vector<ClubSessionPool> pools = poolSessionByClub(day, norms);
        check(pools.size() == 1 && pools[0].shots.size() == 8 && pools[0].mishits.empty(),
              "every shot counts in the usual carry: no mishit");

        // An overruled thin is read, so a low launch can't be read as a thin from the flight in its place.
        ShotRecord bothUnread = aug[0];
        for (ConditionRow &r : bothUnread.rows)
            if (r.conditionId == QLatin1String("chunk")) { r.state = ShotState::NotAssessable; r.notAssessableReason = QStringLiteral("no reading"); }
        bothUnread.launchAngle = 5.0;
        check(!strikeHeightUnread(bothUnread) && strikeFromFlight(bothUnread, usuals).isEmpty(),
              "an overruled thin is never turned into a thin read from the flight");

        // 2 November, shot 5: a real thin launches at 1.7° against a usual of about 11°.
        std::vector<ShotRecord> nov;
        for (int i = 0; i < 6; ++i) {
            ShotRecord s = shot(i + 1, {});
            s.launchAngle = 11.0;
            s.smash = 1.35;
            nov.push_back(s);
        }
        nov.push_back(iron(7, -14.0, 1.7, 120.0));
        checkStrikesAgainstFlight(nov, norms);
        check(row(nov.back(), "thin").state == ShotState::Fired && !row(nov.back(), "thin").overruled, "a real thin stays Fired");
        ClubDistanceSession novDay;
        novDay.shots = nov;
        const std::vector<ClubSessionPool> novPools = poolSessionByClub(novDay, norms);
        check(novPools.size() == 1 && novPools[0].mishits.size() == 1 && novPools[0].mishits[0].kind == QStringLiteral("thin")
                  && novPools[0].shots.size() == 6,
              "…and is a mishit, out of the usual carry");

        // Sky: a measured sky row on an iron is no verdict.
        std::vector<ShotRecord> irons;
        for (int i = 0; i < 5; ++i) irons.push_back(shot(i + 1, i == 0 ? std::initializer_list<const char *>{ "sky" } : std::initializer_list<const char *>{}));
        irons[0].descentAngle = 55.0;
        irons[0].attackAngle  = -9.0;
        irons[0].carry        = 100.0;
        for (size_t i = 1; i < irons.size(); ++i) irons[i].attackAngle = -4.0;
        checkStrikesAgainstFlight(irons, norms);
        check(row(irons[0], "sky").state == ShotState::NotAssessable && row(irons[0], "sky").overruled
                  && row(irons[0], "sky").notAssessableReason == QStringLiteral("Sky is judged from the ball flight"),
              "a measured sky on an iron: overruled, Sky is judged from the ball flight");

        // On a driver the flight rule decides: High, 2° steeper than the usual attack, carry at 74 % of the usual.
        const auto driver = [](int id, bool skyRow, double descent, double attack, double carry) {
            ShotRecord s = shot(id, skyRow ? std::initializer_list<const char *>{ "sky" } : std::initializer_list<const char *>{}, {}, "DRIVER");
            s.descentAngle = descent;
            s.attackAngle  = attack;
            s.carry        = carry;
            return s;
        };
        std::vector<ShotRecord> tee;
        for (int i = 0; i < 5; ++i) tee.push_back(driver(i + 1, false, 37.0, -1.0, 230.0));
        tee.push_back(driver(6, true, 44.0, -3.5, 170.0));     // the flight calls it Sky
        tee.push_back(driver(7, true, 38.0, -1.0, 228.0));     // a sky row on a normal flight
        checkStrikesAgainstFlight(tee, norms);
        check(row(tee[5], "sky").state == ShotState::Fired && !row(tee[5], "sky").overruled, "a driver the flight calls Sky: the row stays Fired");
        check(row(tee[6], "sky").state == ShotState::NotAssessable && row(tee[6], "sky").overruled, "a driver flying normally: overruled");
        check(std::none_of(tee.begin(), tee.begin() + 5, [&row](const ShotRecord &s) { return row(s, "sky").overruled
                                                                                            || row(s, "sky").state != ShotState::Clean; }),
              "no sky row is made where none fired");
        const QHash<QString, ClubFlightUsual> teeUsuals = sessionFlightUsuals(tee);
        check(flightSaysSky(tee[5], teeUsuals, norms)
                  && shotFactors(tee[5], composeShotOutcome(tee[5], {}, QString(), nullptr, pack, ShotOutcomeSessionFacts{}), pack,
                                 norms, teeUsuals).flight.values == QStringList{ "sky" },
              "the row and the flight factor agree: Sky");
        ClubDistanceSession teeDay;
        teeDay.shots = tee;
        const std::vector<ClubSessionPool> teePools = poolSessionByClub(teeDay, norms);
        check(teePools.size() == 1 && teePools[0].mishits.size() == 1 && teePools[0].mishits[0].kind == QStringLiteral("sky")
                  && teePools[0].mishits[0].position == 5,
              "the skied drive leaves the usual carry, the normal one with a sky row counts");
        // A ball-only monitor has no sky row to fire: the flight's Sky still leaves the usual carry.
        ShotRecord ballOnly = tee[5];
        for (ConditionRow &r : ballOnly.rows)
            if (r.conditionId == QLatin1String("sky")) { r.state = ShotState::NotAssessable; r.overruled = false; }
        const ClubFlightUsual driverUsual = teeUsuals.value(QStringLiteral("DRIVER"));
        check(shotMishitKind(ballOnly, {}, &driverUsual, &norms) == QStringLiteral("sky"),
              "the flight's Sky is a mishit with no sky row (driver, woods, hybrids)");
        ShotRecord ironHigh = irons[0];
        const QHash<QString, ClubFlightUsual> ironUsuals = sessionFlightUsuals(irons);
        const ClubFlightUsual ironUsual = ironUsuals.value(QStringLiteral("7 IRON"));
        check(shotMishitKind(ironHigh, {}, &ironUsual, &norms).isEmpty(),
              "an iron flying high and short is no sky mishit");
        check(strikeFromFlight(ballOnly, teeUsuals).isEmpty(), "and a high launch is never a strike read from the flight");

        // A live session converges as shots arrive: three 7 irons are no usual, the fourth makes one.
        std::vector<ShotRecord> live;
        for (int i = 0; i < 3; ++i) {
            live.push_back(iron(i + 1, -12.0, 18.0 + i));
            checkStrikesAgainstFlight(live, norms);
        }
        const auto firedThins = [&row](const std::vector<ShotRecord> &v) {
            return int(std::count_if(v.begin(), v.end(), [&row](const ShotRecord &s) { return row(s, "thin").state == ShotState::Fired; }));
        };
        check(firedThins(live) == 3, "live: three shots of the club, no usual, every thin stands");
        live.push_back(iron(4, -12.0, 19.5));
        checkStrikesAgainstFlight(live, norms);
        check(firedThins(live) == 0, "live: the fourth gives the club a usual, and all four are overruled");
        live.push_back(iron(5, -14.0, 1.7, 110.0));
        checkStrikesAgainstFlight(live, norms);
        check(firedThins(live) == 1 && row(live.back(), "thin").state == ShotState::Fired, "live: a real thin arriving stands");
        const std::vector<ShotRecord> once = live;
        checkStrikesAgainstFlight(live, norms);
        check(toJson(live) == toJson(once), "live: checking again changes nothing");
        live.erase(live.begin() + 3, live.end());
        checkStrikesAgainstFlight(live, norms);
        check(firedThins(live) == 3 && std::none_of(live.begin(), live.end(), [&row](const ShotRecord &s) {
                  return row(s, "thin").overruled || !row(s, "thin").notAssessableReason.isEmpty(); }),
              "live: shots leaving put the rows back as detected");

        // The flag survives diagnostics.json.
        const std::vector<ShotRecord> back = fromJson(toJson(aug));
        check(row(back[0], "thin").overruled && row(back[0], "thin").state == ShotState::NotAssessable
                  && row(back[0], "thin").value == -12.0
                  && row(back[0], "thin").notAssessableReason == QStringLiteral("the strike height disagrees with the ball flight")
                  && !row(back[0], "chunk").overruled,
              "JSON round trip: overruled, NotAssessable, value and reason");
        const QJsonObject firstRow = toJson(aug).value(QStringLiteral("shots")).toArray().at(0).toObject()
                                         .value(QStringLiteral("rows")).toArray().at(0).toObject();
        check(!firstRow.contains(QStringLiteral("overruled")), "an unflagged row writes no key");
    }

    std::printf("\nthe values, in one table:\n");
    {
        check(shotFactorValueLabel(pack, QStringLiteral("solid")) == QStringLiteral("Solid")
                  && shotFactorValueLabel(pack, QStringLiteral("straight")) == QStringLiteral("Straight")
                  && shotFactorValueLabel(pack, QStringLiteral("low")) == QStringLiteral("Low")
                  && shotFactorValueLabel(pack, QStringLiteral("mid")) == QStringLiteral("Mid")
                  && shotFactorValueLabel(pack, QStringLiteral("high")) == QStringLiteral("High"),
              "Solid, Straight, Low, Mid, High in the table's words");
        check(shotFactorValueLabel(pack, QStringLiteral("chunk")) == QStringLiteral("Fat")
                  && shotFactorValueLabel(pack, QStringLiteral("push_slice")) == QStringLiteral("Push-slice"),
              "an outcome in the pack's own short words");
        bool everyFactor = true;
        for (const ShotFactorValueInfo &v : shotFactorValueTable())
            everyFactor = everyFactor && shotFactorNames().contains(v.factor) && !v.adjective.isEmpty();
        check(everyFactor && shotFactorValueTable().size() == 21, "21 values, each with its factor and its adjective");
        check(!shotFactorValueInfo(QStringLiteral("fade"))->fault && shotFactorValueInfo(QStringLiteral("sky"))->fault
                  && !shotFactorValueInfo(QStringLiteral("high"))->fault && shotFactorValueInfo(QStringLiteral("smash_deficit"))->fault,
              "Fade and High are values, Sky and Weak strike faults");
    }

    std::printf("\na factor's mix:\n");
    {
        ShotFactorTally t;
        t.read = 4;
        t.notRead = 1;
        t.add(QStringLiteral("solid"), QStringLiteral("Solid"), false);
        t.add(QStringLiteral("solid"), QStringLiteral("Solid"), false);
        t.add(QStringLiteral("strike_toe"), QStringLiteral("Toe"), true);
        t.add(QStringLiteral("smash_deficit"), QStringLiteral("Weak strike"), true);
        t.add(QStringLiteral("strike_toe"), QStringLiteral("Toe"), true);
        const ShotMixBar bar = composeFactorMixBar(t);
        check(bar.judged == 4 && bar.parts.size() == 3 && bar.parts[0].key == QStringLiteral("solid") && bar.parts[0].kind == QStringLiteral("good")
                  && bar.parts[1].key == QStringLiteral("strike_toe") && bar.parts[1].kind == QStringLiteral("main")
                  && bar.parts[2].kind == QStringLiteral("other"),
              "Solid first, then the faults, the commonest the main one");
        check(bar.line == QStringLiteral("Solid 50% · Toe 50% · Weak strike 25% · 1 not read"),
              "a shot under two values: each part its own share of the shots read, adding past 100");

        ShotFactorTally h;
        h.read = 3;
        h.add(QStringLiteral("high"), QStringLiteral("High"), false);
        h.add(QStringLiteral("low"), QStringLiteral("Low"), false);
        h.add(QStringLiteral("mid"), QStringLiteral("Mid"), false);
        const ShotMixBar heights = composeFactorMixBar(h);
        check(heights.parts.size() == 3 && heights.parts[0].key == QStringLiteral("low") && heights.parts[1].key == QStringLiteral("mid")
                  && heights.parts[2].key == QStringLiteral("high") && heights.parts[0].kind == QStringLiteral("value"),
              "heights in their own order, Low, Mid, High, as plain values");
        check(heights.parts[0].percent + heights.parts[1].percent + heights.parts[2].percent == 100,
              "each shot once: the percentages add up to 100");
    }

    std::printf("\nthe shipped table:\n");
    {
        QString why;
        const FlightNorms shipped = loadFlightNorms(QStringLiteral(PP_FLIGHT_NORMS_PATH), &why);
        check(why.isEmpty() && shipped.schema == 2, "flight_norms.json reads, schema 2");
        check(flightClubCategory(shipped, QStringLiteral("DRIVER")) == QStringLiteral("driver")
                  && flightClubCategory(shipped, QStringLiteral("7 IRON")) == QStringLiteral("mid_irons")
                  && flightClubCategory(shipped, QStringLiteral("PITCHING WEDGE")) == QStringLiteral("short_irons")
                  && flightClubCategory(shipped, QStringLiteral("3 HYBRID")) == QStringLiteral("fairway_hybrid")
                  && flightClubCategory(shipped, QStringLiteral("PUTTER")).isEmpty(),
              "every category's clubs, and none for the putter");
        check(shipped.landing.value(QStringLiteral("driver")).lo == 33.0 && shipped.landing.value(QStringLiteral("driver")).hi == 41.0
                  && shipped.apexCarry.value(QStringLiteral("wedges")).lo == 0.19 && shipped.apexCarry.value(QStringLiteral("wedges")).hi == 0.25
                  && shipped.apexBand.lo == 0.85 && shipped.apexBand.hi == 1.15,
              "the spec's §4 bands");
        check(shipped.apexReference.size() == 1 && shipped.apexReference.value(QStringLiteral("driver")).size() == 2
                  && std::fabs(flightReferenceApexYd(shipped, QStringLiteral("driver"), 140.0) - 25.0) < 1e-9,
              "the driver's reference only");
        check(!shipped.sources.isEmpty() && !shipped.notes.isEmpty(), "its sources and notes");
        check(loadFlightNorms(QStringLiteral("/nonexistent/flight_norms.json"), &why).isEmpty() && !why.isEmpty(),
              "a table that can't be read is empty, and says why");
    }

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
