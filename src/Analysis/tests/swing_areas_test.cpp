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

// swing_areas.h — Home's "Your swing" areas, rated by habit counts
// (docs/design/ux_refactor_away_from_4_modes.md §5.2, D1): the four bands and their boundaries, what
// counts (Movement and Delivery; ball flight's Outcomes, only with launch-monitor evidence), the
// trend against the newest substantive session, the orders, the top fault and the work-on words.
// Session records are built by hand so every count is the one the rule sees.

#include "../../Coaching/swing_areas.h"

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

static constexpr qint64 kT0  = 1'700'000'000'000LL;
static constexpr qint64 kDay = 86'400'000LL;

// One condition's showing in one session: fired of assessable swings.
struct Ev { const char *id; int fired, assessable; };

// The session gate (work_ons.h sessionEvidence) on raw counts, as the flat-weighted ledger reads them.
static SessionEvidence stateOf(int fired, int assessable)
{
    if (assessable < 1) return SessionEvidence::NotMeasured;
    if (assessable >= kDiagMinAssessableForPattern && wilsonLowerBound(fired, assessable) >= kDiagPatternWilsonLb)
        return SessionEvidence::Pattern;
    if (assessable >= kDiagMinAssessableForPattern && wilsonUpperBound(fired, assessable) < kWorkOnQuietWilsonUb)
        return SessionEvidence::Quiet;
    return SessionEvidence::Inconclusive;
}

// A session record on day `day`: one evidence row per Ev, and an entry for each `listed` id.
static SessionWorkOns session(const char *id, int day, const std::vector<Ev> &evs, const QStringList &listed = {})
{
    SessionWorkOns s;
    s.sessionId = QString::fromLatin1(id);
    s.startMs   = kT0 + qint64(day) * kDay;
    s.endMs     = s.startMs + 3'600'000LL;
    for (const Ev &e : evs) {
        WorkOnEvidence w;
        w.id         = QString::fromLatin1(e.id);
        w.state      = stateOf(e.fired, e.assessable);
        w.fired      = e.fired;
        w.assessable = e.assessable;
        w.measureId  = w.id + QStringLiteral(".m");
        if (w.state == SessionEvidence::NotMeasured) continue;   // the reduction records only what it assessed
        s.evidence.push_back(w);
        s.shotCount = std::max(s.shotCount, e.assessable);
        if (listed.contains(w.id)) {
            WorkOnEntry en;
            en.id         = w.id;
            en.rank       = int(s.entries.size()) + 1;
            en.fired      = e.fired;
            en.assessable = e.assessable;
            en.measureId  = w.measureId;
            s.entries.push_back(en);
        }
    }
    return s;
}

static void setTypical(SessionWorkOns &s, const char *id, double value, double lo, double hi)
{
    for (WorkOnEvidence &e : s.evidence)
        if (e.id == QLatin1String(id)) {
            e.hasTypical = true;
            e.typicalValue = value;
            e.corridorLo = lo;
            e.corridorHi = hi;
            e.corridorShape = CorridorShape::TwoSided;
        }
}

static AreaConditionInfo ci(const char *group, const char *label, bool counts = true, bool lmOnly = false)
{
    AreaConditionInfo i;
    i.group  = QString::fromLatin1(group);
    i.label  = QString::fromLatin1(label);
    i.golfer = i.label.toLower();
    i.unit   = QStringLiteral("°");
    i.counts = counts;
    i.lmOnly = lmOnly;
    return i;
}

static const AreaSummary *areaOf(const std::vector<AreaSummary> &v, const char *group)
{
    for (const AreaSummary &a : v) if (a.group == QLatin1String(group)) return &a;
    return nullptr;
}

static const AreaItem *itemOf(const AreaSummary *a, const char *id)
{
    if (!a) return nullptr;
    for (const AreaItem &i : a->items) if (i.conditionId == QLatin1String(id)) return &i;
    return nullptr;
}

static QStringList groupsOf(const std::vector<AreaSummary> &v)
{
    QStringList out;
    for (const AreaSummary &a : v) out << a.group;
    return out;
}

static QStringList itemIds(const AreaSummary *a)
{
    QStringList out;
    if (a) for (const AreaItem &i : a->items) out << i.conditionId;
    return out;
}

int main()
{
    std::printf("swing_areas_test\n");
    const QStringList body{ "setup", "posture", "lateral", "sequence", "armsAndClub", "release", "impact", "finish" };

    // ── An empty library ────────────────────────────────────────────────────────
    {
        const QHash<QString, AreaConditionInfo> info{ { "ee", ci("posture", "Early extension") },
                                                      { "slice", ci("ballFlight", "Slice", true, true) } };
        const std::vector<AreaSummary> a = summariseAreas({}, info);
        bool none = a.size() == 8;
        for (const AreaSummary &s : a)
            none = none && !s.measurable && s.measured == 0 && s.items.empty() && !s.hasTrend && s.habitTrend == 0
                && s.topFaultId.isEmpty() && s.workOnsActive == 0;
        check(none, "no sessions: the eight body areas, none measurable, nothing in them");
        check(groupsOf(a) == body, "…in address-to-finish order, and no ball flight");
        check(summariseAreas({}, {}).size() == 8, "no pack either: still the eight rows");
    }

    // ── One session: counts, no trend ───────────────────────────────────────────
    {
        const QHash<QString, AreaConditionInfo> info{ { "ee", ci("posture", "Early extension") },
                                                      { "sway", ci("lateral", "Sway") } };
        const std::vector<AreaSummary> a = summariseAreas({ session("s1", 0, { { "ee", 9, 10 }, { "sway", 0, 40 } }) }, info);
        const AreaSummary *p = areaOf(a, "posture"), *l = areaOf(a, "lateral");
        check(p && p->measurable && p->measured == 1 && p->habits == 1 && !p->hasTrend && p->habitTrend == 0,
              "one session: a habit is counted, and there is no trend to state");
        check(l && l->measurable && l->clean == 1 && l->habits == 0, "0 of 40 is clean");
        check(a.size() == 8 && a[0].group == QStringLiteral("posture") && a[1].group == QStringLiteral("lateral"),
              "the measured areas lead");
    }

    // ── A group with nothing assessable; Excluded conditions ignored ────────────
    {
        const QHash<QString, AreaConditionInfo> info{
            { "ee",     ci("posture", "Early extension") },
            { "scoop",  ci("release", "Scooping") },                     // authored, never assessed
            { "hipcap", ci("setup", "Hip rotation capacity", false) },   // Capacity: Excluded
            { "intent", ci("posture", "Trying to stay down", false) },   // Intent in a measured area
        };
        const std::vector<AreaSummary> a = summariseAreas(
            { session("s1", 0, { { "ee", 0, 40 }, { "hipcap", 10, 10 }, { "intent", 10, 10 } }) }, info);
        const AreaSummary *r = areaOf(a, "release"), *s = areaOf(a, "setup"), *p = areaOf(a, "posture");
        check(r && !r->measurable && r->measured == 0 && r->items.empty() && r->topFaultId.isEmpty(),
              "a group whose conditions were never assessed is not measurable");
        check(s && !s->measurable && s->habits == 0 && s->items.empty(),
              "a group assessed only on Excluded conditions is not measurable either");
        check(p && p->measured == 1 && p->habits == 0 && p->clean == 1 && !itemOf(p, "intent") && p->topFaultId.isEmpty(),
              "an Excluded condition firing on every swing adds no habit and no item to its area");
        check(a.size() == 8 && a[0].group == QStringLiteral("posture") && a[1].group == QStringLiteral("setup")
                  && a.back().group == QStringLiteral("finish"),
              "unmeasurable areas follow, in address-to-finish order");

        // The class rule on its own: Movement and Delivery count; held back never; an Outcome only in ball flight.
        check(areaConditionCounts("posture", WorkOnClass::Movement, false, false)
                  && areaConditionCounts("impact", WorkOnClass::Delivery, false, false),
              "Movement and Delivery count");
        check(!areaConditionCounts("setup", WorkOnClass::Excluded, false, false)
                  && !areaConditionCounts("impact", WorkOnClass::Excluded, true, false),
              "Excluded does not, an Outcome outside ball flight included");
        check(areaConditionCounts("ballFlight", WorkOnClass::Excluded, true, false),
              "ball flight's Outcome conditions count");
        check(!areaConditionCounts("ballFlight", WorkOnClass::Excluded, true, true)
                  && !areaConditionCounts("posture", WorkOnClass::Movement, false, true),
              "a held-back condition never counts, in ball flight or out of it");
    }

    // ── Ball flight: shown only with launch-monitor evidence ────────────────────
    {
        const QHash<QString, AreaConditionInfo> info{
            { "ee",    ci("posture", "Early extension") },
            { "top",   ci("ballFlight", "Topped", true, false) },   // judged from the camera too
            { "slice", ci("ballFlight", "Slice", true, true) },     // launch-monitor keys only
        };
        const std::vector<AreaSummary> camera = summariseAreas({ session("s1", 0, { { "ee", 9, 10 }, { "top", 6, 10 } }) }, info);
        check(!areaOf(camera, "ballFlight") && camera.size() == 8,
              "ball flight is omitted when only a camera-judged outcome was assessed");
        const std::vector<AreaSummary> lm = summariseAreas(
            { session("s1", 0, { { "ee", 9, 10 }, { "top", 6, 10 }, { "slice", 0, 40 } }) }, info);
        const AreaSummary *bf = areaOf(lm, "ballFlight");
        check(bf && bf->measurable && lm.size() == 9 && bf->measured == 2 && bf->habits == 1 && bf->clean == 1
                  && bf->topFaultId == QStringLiteral("top"),
              "with launch-monitor evidence it is present, and counts its Outcome conditions, the camera's included");
    }

    // ── The bands and their boundaries ──────────────────────────────────────────
    {
        const QHash<QString, AreaConditionInfo> info{
            { "half",  ci("posture", "Half") },       // 2 of 4    = 0.5      habit
            { "under", ci("posture", "Under") },      // 49 of 100 = 0.49     occasional
            { "occ",   ci("posture", "Occ") },        // 3 of 20   = 0.15     occasional
            { "rare",  ci("posture", "Rare") },       // 1 of 7    ≈ 0.143, Wilson upper ≈ 0.51: unclear
            { "clean", ci("posture", "Clean") },      // 1 of 40   = 0.025, Wilson upper ≈ 0.13: clean
            { "none",  ci("posture", "None") },       // 0 of 2: upper 0.66 — unclear, not clean
        };
        const std::vector<AreaSummary> a = summariseAreas(
            { session("s1", 0, { { "half", 2, 4 }, { "under", 49, 100 }, { "occ", 3, 20 }, { "rare", 1, 7 },
                                 { "clean", 1, 40 }, { "none", 0, 2 } }) },
            info, [] { AreaOptions o; o.maxItems = 10; return o; }());
        const AreaSummary *p = areaOf(a, "posture");
        const AreaItem *half = itemOf(p, "half"), *under = itemOf(p, "under"), *occ = itemOf(p, "occ"),
                       *rare = itemOf(p, "rare"), *clean = itemOf(p, "clean"), *none = itemOf(p, "none");
        check(half && half->band == AreaBand::Habit && near(half->share, 0.5, 1e-12), "exactly 0.5 is a habit");
        check(under && under->band == AreaBand::Occasional, "0.49 is occasional");
        check(occ && occ->band == AreaBand::Occasional, "exactly 0.15 is occasional");
        check(rare && rare->band == AreaBand::Unclear && wilsonUpperBound(1, 7) >= kAreaCleanUpper,
              "just under 0.15 on seven swings is UNCLEAR, not clean: the upper bound is still high");
        check(clean && clean->band == AreaBand::Clean, "1 of 40 is clean");
        check(none && none->band == AreaBand::Unclear, "0 of 2 is unclear: never seen, on too few to say");
        check(p && p->measured == 6 && p->habits == 1 && p->occasional == 2 && p->clean == 1 && p->unclear == 2,
              "the area's counts are the bands' counts");
        check(half && half->fired == 2 && half->assessable == 4 && areaBandToString(half->band) == QStringLiteral("habit")
                  && areaBandToString(AreaBand::Unclear) == QStringLiteral("unclear"),
              "items quote unweighted swings; the band words are the tokens QML reads");
    }

    // ── The trend: habits against the same counts without the newest substantive session ─
    {
        const QHash<QString, AreaConditionInfo> info{ { "ee", ci("posture", "Early extension") },
                                                      { "sway", ci("posture", "Sway") } };
        std::vector<SessionWorkOns> ss{
            session("s1", 0, { { "ee", 10, 10 }, { "sway", 10, 10 } }),
            session("s2", 1, { { "ee", 10, 10 }, { "sway", 0, 30 } }),     // clears the sway habit
        };
        // sway now: 10 × 0.75 of 7.5 + 30 = 0.2, occasional. Before s2: 10 of 10, a habit.
        std::vector<AreaSummary> a = summariseAreas(ss, info);
        const AreaSummary *p = areaOf(a, "posture");
        check(p && p->hasTrend && p->habits == 1 && p->habitTrend == -1 && p->occasional == 1,
              "a newest session that clears a habit reads −1 habit");
        check(areaOf(a, "lateral") && !areaOf(a, "lateral")->hasTrend, "an unmeasurable area has no trend");

        // A three-ball warm-up after it is not substantive: the trend is still against s2.
        ss.push_back(session("s3", 2, { { "sway", 2, 2 } }));
        check(!ss.back().substantive(), "the warm-up is not substantive");
        a = summariseAreas(ss, info);
        p = areaOf(a, "posture");
        check(p && p->hasTrend && p->habitTrend == -1 && p->habits == 1,
              "…and a warm-up after it changes neither which session the trend drops nor its answer");

        // Two sessions of which only one is substantive: no trend.
        const std::vector<AreaSummary> one = summariseAreas(
            { session("w1", 0, { { "ee", 2, 2 } }), session("w2", 1, { { "ee", 10, 10 } }) }, info);
        check(areaOf(one, "posture") && !areaOf(one, "posture")->hasTrend && areaOf(one, "posture")->habitTrend == 0,
              "one substantive session: no trend");
    }

    // ── Stable ordering: areas, then items ──────────────────────────────────────
    {
        const QHash<QString, AreaConditionInfo> info{
            { "h1", ci("sequence", "H1") }, { "h2", ci("sequence", "H2") }, { "o1", ci("sequence", "O1") },
            { "p1", ci("posture", "P1") },  { "p2", ci("posture", "P2") },
            { "l1", ci("lateral", "L1") },
            { "a1", ci("armsAndClub", "A1") },
            { "i1", ci("impact", "I1") },
            { "f1", ci("finish", "F1") },
            { "r1", ci("release", "R1") },
        };
        // sequence 2 habits + 1 occasional; posture 2 habits; arms & club 1 habit at 0.9; lateral and finish
        // 1 habit at 0.6 each (tied: address to finish decides); impact clean only; setup and release unmeasured.
        const std::vector<SessionWorkOns> ss{
            session("s1", 0, { { "h1", 10, 10 }, { "h2", 8, 10 }, { "o1", 3, 10 }, { "p1", 10, 10 }, { "p2", 9, 10 },
                               { "l1", 6, 10 }, { "a1", 9, 10 }, { "i1", 0, 40 }, { "f1", 6, 10 } }),
        };
        const QStringList expect{ "sequence", "posture", "armsAndClub", "lateral", "finish", "impact", "setup", "release" };
        const std::vector<AreaSummary> a = summariseAreas(ss, info);
        check(groupsOf(a) == expect,
              "areas: habits, then occasional, then the worst share, then address to finish; unmeasured last");
        std::vector<SessionWorkOns> rev{ session("x", 0, {}), ss[0] };
        std::reverse(rev.begin(), rev.end());
        check(groupsOf(summariseAreas(rev, info)) == expect, "the input order of the sessions does not matter");
        check(itemIds(areaOf(a, "sequence")) == (QStringList{ "h1", "h2", "o1" }), "items: by band, then share");

        // Item ties: sessionsListed, then id; habits before occasional before the rest; the cap.
        const QHash<QString, AreaConditionInfo> pi{
            { "top", ci("posture", "Top") }, { "hb", ci("posture", "Hb") }, { "ha", ci("posture", "Ha") },
            { "hc", ci("posture", "Hc") },   { "occ", ci("posture", "Occ") }, { "unc", ci("posture", "Unc") },
            { "cln", ci("posture", "Cln") },
        };
        const std::vector<SessionWorkOns> ps{
            session("p1", 0, { { "top", 10, 10 }, { "hb", 9, 10 }, { "ha", 9, 10 }, { "hc", 9, 10 }, { "occ", 4, 10 },
                               { "unc", 1, 7 }, { "cln", 0, 40 } }, { "hb" }),
        };
        const std::vector<AreaSummary> a5 = summariseAreas(ps, pi);
        const AreaSummary *p5 = areaOf(a5, "posture");
        check(itemIds(p5) == (QStringList{ "top", "hb", "ha", "hc", "occ" }),
              "equal shares: the one listed more often first, then by id; capped at five");
        AreaOptions all;
        all.maxItems = 10;
        const std::vector<AreaSummary> aa = summariseAreas(ps, pi, all);
        const AreaSummary *pa = areaOf(aa, "posture");
        check(itemIds(pa) == (QStringList{ "top", "hb", "ha", "hc", "occ", "unc", "cln" }),
              "the rest after occasional, by share");
        check(p5 && p5->habits == 4 && p5->measured == 7 && p5->topFaultId == QStringLiteral("top"),
              "the cap trims the items, never the counts; the top fault is the highest-share habit");
    }

    // ── The top fault: none when nothing is a habit or occasional ───────────────
    {
        const QHash<QString, AreaConditionInfo> info{ { "i1", ci("impact", "I1") }, { "i2", ci("impact", "I2") },
                                                      { "f1", ci("finish", "F1") } };
        const std::vector<AreaSummary> a = summariseAreas(
            { session("s1", 0, { { "i1", 0, 40 }, { "i2", 1, 7 }, { "f1", 3, 10 } }) }, info);
        const AreaSummary *i = areaOf(a, "impact"), *f = areaOf(a, "finish");
        check(i && i->measurable && i->topFaultId.isEmpty() && i->items.size() == 2,
              "clean and unclear only: no top fault, though both are listed as items");
        check(f && f->topFaultId == QStringLiteral("f1"), "an occasional condition is a top fault");
    }

    // ── The work-on words ───────────────────────────────────────────────────────
    {
        const QHash<QString, AreaConditionInfo> info{
            { "never", ci("posture", "Never") }, { "again", ci("posture", "Again") }, { "fresh", ci("posture", "Fresh") },
            { "old", ci("posture", "Old") },     { "eased", ci("posture", "Eased") }, { "gone", ci("posture", "Gone") },
        };
        AreaOptions all;
        all.maxItems = 10;
        std::vector<SessionWorkOns> ss{
            session("s1", 0, { { "never", 10, 10 }, { "again", 10, 10 }, { "fresh", 10, 10 }, { "old", 10, 10 },
                               { "eased", 10, 10 }, { "gone", 10, 10 } }, { "again", "old", "eased", "gone" }),
            session("s2", 1, { { "never", 10, 10 }, { "again", 10, 10 }, { "fresh", 10, 10 }, { "old", 10, 10 },
                               { "eased", 10, 10 }, { "gone", 0, 10 } }, { "eased" }),
            session("s3", 2, { { "never", 10, 10 }, { "again", 10, 10 }, { "fresh", 10, 10 }, { "old", 10, 10 },
                               { "eased", 0, 10 }, { "gone", 0, 10 } }, { "again", "fresh" }),
            session("s4", 3, { { "fresh", 1, 2 } }),          // a warm-up: not the newest SUBSTANTIVE session
        };
        const std::vector<AreaSummary> aw = summariseAreas(ss, info, all);
        const AreaSummary *p = areaOf(aw, "posture");
        const AreaItem *never = itemOf(p, "never"), *again = itemOf(p, "again"), *fresh = itemOf(p, "fresh"),
                       *old = itemOf(p, "old"), *eased = itemOf(p, "eased"), *gone = itemOf(p, "gone");
        check(never && never->workOn.isEmpty() && never->sessionsListed == 0,
              "a pattern in every session that was never listed is not a work-on");
        check(again && again->workOn == QStringLiteral("active") && again->sessionsListed == 2, "listed before and now: active");
        check(fresh && fresh->workOn == QStringLiteral("new") && fresh->sessionsListed == 1,
              "listed only in the newest substantive session: new (a warm-up after it changes nothing)");
        check(old && old->workOn == QStringLiteral("active") && old->sessionsListed == 1,
              "listed once, long ago, and still a pattern: active, NOT new");
        check(eased && eased->workOn == QStringLiteral("easing"), "quiet in the newest conclusive session: easing");
        check(gone && gone->workOn.isEmpty() && gone->sessionsListed == 1, "cleared: no longer a work-on");
        check(p && p->workOnsActive == 4 && p->workOnsNew == 1,
              "the area counts every work-on still on the list (active, easing, new), and the new one");
    }

    // ── What an item quotes ─────────────────────────────────────────────────────
    {
        QHash<QString, AreaConditionInfo> info{ { "ee", ci("posture", "Early extension") },
                                                { "spine", ci("posture", "Spine") } };
        info["ee"].unitByMeasure.insert(QStringLiteral("ee.m"), QStringLiteral("cm"));
        std::vector<SessionWorkOns> ss{
            session("s1", 0, { { "ee", 9, 10 }, { "spine", 9, 10 } }),
            session("s2", 1, { { "ee", 8, 10 }, { "spine", 9, 10 } }),
        };
        setTypical(ss[0], "ee", 9.0, 0.0, 3.0);
        setTypical(ss[1], "ee", 6.5, 0.0, 3.5);
        setTypical(ss[0], "spine", 12.0, 6.0, 18.0);     // the newest session has none (mixed clubs)
        const std::vector<AreaSummary> aq = summariseAreas(ss, info);
        const AreaSummary *p = areaOf(aq, "posture");
        const AreaItem *ee = itemOf(p, "ee"), *spine = itemOf(p, "spine");
        check(ee && ee->hasTypical && near(ee->typical, 6.5, 1e-12) && near(ee->corridorHi, 3.5, 1e-12)
                  && ee->corridorShape == QStringLiteral("twoSided") && ee->measureId == QStringLiteral("ee.m")
                  && ee->unit == QStringLiteral("cm") && ee->label == QStringLiteral("Early extension")
                  && ee->golfer == QStringLiteral("early extension") && ee->fired == 17 && ee->assessable == 20,
              "the typical reading, corridor and measure are the newest assessing session's; the unit is that measure's");
        check(spine && !spine->hasTypical && spine->corridorShape.isEmpty() && spine->unit == QStringLiteral("°"),
              "no typical in the newest session: none is quoted from an older one; the unit falls back to the condition's");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
