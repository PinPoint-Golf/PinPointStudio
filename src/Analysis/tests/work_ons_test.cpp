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

// work_ons.h — the per-session selection and the per-golfer aggregate, one block per rule in
// the header (docs/design/work_ons_design.md): a work-on is a session pattern and nothing less,
// only movements are listed, absence is not improvement, and the aggregate is a pure function
// of the session records.

#include "../work_ons.h"

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

// 1 fired, 0 clean, -1 not assessable — diagnostic_ledger_test's encoding.
struct Plan {
    QString id;
    std::vector<int> states;
    double z = 3.0;             // every firing's distance out
    double lo = -2.0, hi = 2.0; // the corridor every row is graded against
};

static Plan plan(const char *id, std::vector<int> states, double z = 3.0)
{
    return Plan{ QString::fromLatin1(id), std::move(states), z };
}

static std::vector<ShotRecord> sessionOf(const std::vector<Plan> &plans, qint64 t0 = 1'700'000'000'000LL)
{
    size_t n = 0;
    for (const Plan &p : plans) n = std::max(n, p.states.size());
    std::vector<ShotRecord> shots;
    for (size_t i = 0; i < n; ++i) {
        ShotRecord s;
        s.shotId = int(i) + 1;
        s.club = QStringLiteral("7i");
        s.timestampMs = t0 + qint64(i) * 45'000LL;
        for (const Plan &p : plans) {
            ConditionRow r;
            r.conditionId      = p.id;
            r.drivingMeasureId = p.id + QStringLiteral(".measure");
            r.corridorLo = p.lo;
            r.corridorHi = p.hi;
            r.corridorShape = CorridorShape::TwoSided;
            const int v = i < p.states.size() ? p.states[i] : -1;
            if (v == 1)      { r.state = ShotState::Fired; r.direction = 1; r.z = p.z; r.value = p.z * 2.0; }
            else if (v == 0) { r.state = ShotState::Clean; r.z = 0.5; r.value = 1.0; }
            else             { r.state = ShotState::NotAssessable; r.notAssessableReason = QStringLiteral("ball not tracked"); }
            s.rows.push_back(std::move(r));
        }
        shots.push_back(std::move(s));
    }
    return shots;
}

static LedgerOptions flatOpts()
{
    LedgerOptions o;
    o.warmUpWeight = 1.0;
    o.softTier = false;
    return o;
}

static std::vector<int> rep(int v, int n) { return std::vector<int>(size_t(n), v); }

static SessionWorkOns reduce(const std::vector<Plan> &plans, const QHash<QString, WorkOnClass> &cls,
                             const WorkOnCauses &causes = {}, const char *sessionId = "s",
                             qint64 t0 = 1'700'000'000'000LL)
{
    const std::vector<ShotRecord> shots = sessionOf(plans, t0);
    SessionWorkOns s = reduceSessionWorkOns(shots, conditionLedgers(shots, flatOpts()), cls, causes);
    s.sessionId = QString::fromLatin1(sessionId);
    return s;
}

static QStringList idsOf(const SessionWorkOns &s)
{
    QStringList out;
    for (const WorkOnEntry &e : s.entries) out << e.id;
    return out;
}

static const WorkOnSummary *find(const std::vector<WorkOnSummary> &v, const char *id)
{
    for (const WorkOnSummary &w : v) if (w.id == QLatin1String(id)) return &w;
    return nullptr;
}

int main()
{
    std::printf("work_ons_test\n");
    const WorkOnClass M = WorkOnClass::Movement, D = WorkOnClass::Delivery, X = WorkOnClass::Excluded;

    // ── The quiet gate ──────────────────────────────────────────────────────────
    {
        check(near(wilsonUpperBound(0, 0), 1.0, 1e-12), "no assessable shots is no evidence of quiet");
        check(wilsonUpperBound(0, 3) > 0.5,  "0 of 3 cannot say gone");
        check(wilsonUpperBound(0, 4) < 0.5,  "0 of 4 can");
        check(wilsonUpperBound(1, 8) < 0.5,  "1 of 8 is quiet");
        check(wilsonUpperBound(1, 7) > 0.5,  "1 of 7 is not yet");
        check(wilsonUpperBound(10, 10) <= 1.0 && wilsonUpperBound(10, 10) > 0.99, "always fired tops out at one");

        const auto ls = conditionLedgers(sessionOf({ plan("pat", rep(1, 8)), plan("quiet", rep(0, 8)),
                                                     plan("few", rep(0, 3)), plan("mixed", {1,0,0,1,0,0,0,-1}),
                                                     plan("unseen", rep(-1, 8)) }), flatOpts());
        check(sessionEvidence(*ledgerFor(ls, "pat"))    == SessionEvidence::Pattern,      "a pattern is Pattern");
        check(sessionEvidence(*ledgerFor(ls, "quiet"))  == SessionEvidence::Quiet,        "0 of 8 is Quiet");
        check(sessionEvidence(*ledgerFor(ls, "few"))    == SessionEvidence::Inconclusive, "0 of 3 is Inconclusive, not Quiet");
        check(sessionEvidence(*ledgerFor(ls, "mixed"))  == SessionEvidence::Inconclusive, "2 of 7 reaches neither gate");
        check(sessionEvidence(*ledgerFor(ls, "unseen")) == SessionEvidence::NotMeasured,  "never assessed is NotMeasured");
    }

    // ── Rule 1: a work-on is a session pattern and nothing less ─────────────────
    {
        const QHash<QString, WorkOnClass> cls{ { "a", M }, { "b", M }, { "c", M } };
        const SessionWorkOns three = reduce({ plan("a", rep(1, 3)), plan("b", rep(1, 2)) }, cls);
        // Flat weights: 3 of 3 has a Wilson lower bound of 0.44 and IS a pattern; 2 of 2 is under the floor.
        check(idsOf(three) == QStringList{ "a" }, "3 of 3 lists, 2 of 2 does not reach the assessable floor");

        const SessionWorkOns watching = reduce({ plan("a", {1,0,0,0,0,0,0,0,0,0}), plan("b", rep(0, 10)) }, cls);
        check(watching.entries.empty(), "one firing in ten is Watching and never a work-on");
        check(watching.evidence.size() == 2, "…but the session still records what it saw of both");
        check(reduce({}, cls).entries.empty(), "an empty session has none");
    }

    // ── Rule 2: only movements, the cap, the order ──────────────────────────────
    {
        const QHash<QString, WorkOnClass> cls{ { "m1", M }, { "m2", M }, { "m3", M }, { "m4", M }, { "m5", M },
                                               { "m6", M }, { "out", X }, { "del", D } };
        const SessionWorkOns s = reduce({ plan("out", rep(1, 10), 9.0), plan("del", rep(1, 10), 8.0),
                                          plan("m1", rep(1, 10), 2.5), plan("m2", rep(1, 10), 6.0),
                                          plan("m3", rep(1, 10), 4.0), plan("m4", rep(1, 10), 3.0),
                                          plan("m5", rep(1, 10), 5.0), plan("m6", rep(1, 10), 7.0) }, cls);
        check(idsOf(s) == (QStringList{ "m6", "m2", "m5", "m3", "m4" }),
              "five movements, furthest out first; the outcome and the delivery number are not listed");
        check(s.entries.front().rank == 1 && s.entries.back().rank == 5, "ranks run 1..5");
        check(s.evidenceFor("out") && s.evidenceFor("out")->state == SessionEvidence::Pattern,
              "the excluded outcome is still in the evidence");

        const SessionWorkOns fill = reduce({ plan("m1", rep(1, 10)), plan("del", rep(1, 10), 8.0),
                                             plan("out", rep(1, 10), 9.0) }, cls);
        check(idsOf(fill) == (QStringList{ "m1", "del" }), "a delivery number fills a list short of three, behind the movement");

        const SessionWorkOns unknown = reduce({ plan("not_in_pack", rep(1, 10)) }, cls);
        check(unknown.entries.empty(), "a condition the pack does not class is excluded");
    }

    // ── Roots before symptoms; resolving last ───────────────────────────────────
    {
        const QHash<QString, WorkOnClass> cls{ { "cause", M }, { "symptom", M }, { "fixed", M }, { "absent", M } };
        const WorkOnCauses causes{ { "cause", "symptom" }, { "absent", "cause" } };
        std::vector<int> fixed = rep(1, 8);     // 8 of 13 is a pattern; five clean shots since is resolving
        for (int i = 0; i < 5; ++i) fixed.push_back(0);
        // The cause and its symptom fire on the SAME ten of thirteen shots: the session supports
        // the link (Fisher), so the symptom goes behind its cause.
        std::vector<int> together = rep(1, 10);
        for (int i = 0; i < 3; ++i) together.push_back(0);
        const SessionWorkOns s = reduce({ plan("symptom", together, 6.0), plan("cause", together, 2.5),
                                          plan("fixed", fixed, 9.0) }, cls, causes);
        check(idsOf(s) == (QStringList{ "cause", "symptom", "fixed" }),
              "the cause leads its louder symptom; a fault fixed inside the session goes last");
        check(!s.entry("symptom")->root && s.entry("symptom")->causedBy == QStringList{ "cause" },
              "the symptom names the session pattern authored as causing it");

        // Both on EVERY shot: nothing to compare, so the authored link is named but does not
        // demote — the louder fault leads on size. (Early extension behind over the top, 8 Oct.)
        const SessionWorkOns u = reduce({ plan("symptom", rep(1, 12), 6.0), plan("cause", rep(1, 12), 2.5) },
                                        cls, causes);
        check(idsOf(u) == (QStringList{ "symptom", "cause" }),
              "an untestable link does not put the louder fault behind its authored cause");
        check(u.entry("symptom")->root && u.entry("symptom")->causedBy.isEmpty()
                  && u.entry("symptom")->mayFollow == QStringList{ "cause" },
              "…it is named as a link these swings cannot show");
        check(s.entry("cause")->root, "a cause whose own parent is not a pattern here is a root");
        check(s.entry("fixed")->resolving, "resolving is carried");
    }

    // ── The typical reading ─────────────────────────────────────────────────────
    {
        const QHash<QString, WorkOnClass> cls{ { "a", M } };
        const SessionWorkOns s = reduce({ plan("a", rep(1, 5), 3.0) }, cls);
        check(s.entry("a")->hasTypical && near(s.entry("a")->typicalValue, 6.0, 1e-9)
              && near(s.entry("a")->corridorHi, 2.0, 1e-9), "one corridor: the median reading is stated");

        std::vector<ShotRecord> shots = sessionOf({ plan("a", rep(1, 5), 3.0) });
        shots[2].rows[0].corridorHi = 4.0;      // another club's corridor
        const SessionWorkOns mixed = reduceSessionWorkOns(shots, conditionLedgers(shots, flatOpts()), cls, {});
        check(mixed.entries.size() == 1 && !mixed.entries[0].hasTypical,
              "two corridors: no typical reading is invented across them");
    }

    // ── Round trip ──────────────────────────────────────────────────────────────
    {
        const QHash<QString, WorkOnClass> cls{ { "cause", M }, { "symptom", M } };
        SessionWorkOns s = reduce({ plan("symptom", rep(1, 8), 6.0), plan("cause", rep(1, 8)),
                                    plan("quiet", rep(0, 8)) }, cls, { { "cause", "symptom" } }, "2026-09-15_x_01");
        s.entries[0].name = QStringLiteral("The leader");
        bool ok = false;
        const SessionWorkOns back = sessionWorkOnsFromJson(toJson(s), &ok);
        check(ok && back.sessionId == s.sessionId && back.startMs == s.startMs && back.shotCount == 8
              && back.clubs == s.clubs, "session meta round-trips");
        // Both fire on every shot, so the link is named (mayFollow) and the symptom stays a root.
        check(idsOf(back) == idsOf(s) && back.entries[0].name == QStringLiteral("The leader")
              && back.entry("symptom")->mayFollow == QStringList{ "cause" } && back.entry("symptom")->root
              && back.entries[0].hasTypical && near(back.entries[0].typicalValue, s.entries[0].typicalValue, 1e-9)
              && back.entries[0].corridorShape == CorridorShape::TwoSided, "entries round-trip");
        check(back.evidence.size() == 3 && back.evidenceFor("quiet")->state == SessionEvidence::Quiet,
              "evidence round-trips");

        QJsonObject stale = toJson(s);
        stale[QStringLiteral("ruleVersion")] = kWorkOnRuleVersion + 1;
        sessionWorkOnsFromJson(stale, &ok);
        check(!ok, "a file written under another rule is refused, not aggregated");
    }

    // ── Rule 3 and the aggregate ────────────────────────────────────────────────
    {
        const QHash<QString, WorkOnClass> cls{ { "keeps", M }, { "new", M }, { "old", M }, { "eased", M },
                                               { "gone", M }, { "dtl", M } };
        const qint64 day = 86'400'000LL, t0 = 1'700'000'000'000LL;
        // Oldest first. `dtl` is only measurable in session 1; `eased` is quiet in the last one;
        // `gone` is quiet in the last two; s3 is a three-ball warm-up that can say nothing.
        std::vector<SessionWorkOns> all = {
            reduce({ plan("keeps", rep(1, 10)), plan("old", rep(1, 10)), plan("eased", rep(1, 10)),
                     plan("gone", rep(1, 10)), plan("dtl", rep(1, 10)), plan("new", rep(-1, 10)) }, cls, {}, "s1", t0),
            reduce({ plan("keeps", rep(1, 10)), plan("eased", rep(1, 10)), plan("gone", rep(0, 10)),
                     plan("old", rep(-1, 10)), plan("dtl", rep(-1, 10)) }, cls, {}, "s2", t0 + day),
            reduce({ plan("keeps", {1, 1, -1}), plan("eased", rep(0, 3)), plan("gone", rep(1, 2)) }, cls, {}, "s3", t0 + 2 * day),
            reduce({ plan("keeps", rep(1, 10)), plan("new", rep(1, 10), 5.0), plan("eased", rep(0, 10)),
                     plan("gone", rep(0, 10)), plan("dtl", rep(-1, 10)) }, cls, {}, "s4", t0 + 3 * day),
        };
        check(!all[2].substantive() && all[2].entries.empty(), "the three-ball session is not substantive");

        // Order in must not matter.
        std::swap(all[0], all[3]);
        const std::vector<WorkOnSummary> agg = aggregateWorkOns(all);

        const WorkOnSummary *keeps = find(agg, "keeps"), *fresh = find(agg, "new"), *old = find(agg, "old"),
                            *eased = find(agg, "eased"), *gone = find(agg, "gone"), *dtl = find(agg, "dtl");
        check(keeps && fresh && old && eased && gone && dtl, "every fault that ever made a list is summarised");
        if (keeps && fresh && old && eased && gone && dtl) {
            check(agg.front().id == QStringLiteral("keeps"), "the fault that keeps coming back leads");
            check(keeps->status == WorkOnStatus::Active && keeps->sessionsPattern == 3
                  && keeps->sessionsConclusive == 3 && keeps->history.size() == 4,
                  "3 of 3 conclusive sessions; all four sessions have a point");
            check(keeps->history[2].state == SessionEvidence::Inconclusive && keeps->history[0].sessionId == QStringLiteral("s1"),
                  "history is oldest first and the warm-up's point is Inconclusive");
            // s4 is age 0, s2 age 1, s1 age 2 — the warm-up does not age anything.
            // Swings that showed it, each session's fading by 0.75 per substantive session since:
            // 10 at age 2, 10 at age 1, the warm-up's 2 at age 1, 10 at age 0.
            check(near(keeps->weightedFired, 10 * 0.5625 + 10 * 0.75 + 2 * 0.75 + 10, 1e-9)
                  && near(keeps->weightedAssessable, keeps->weightedFired, 1e-9)
                  && keeps->swingsFired == 32 && keeps->swingsAssessable == 32,
                  "the order counts SWINGS: 32 of 32, the warm-up's two included and ageing nothing");
            check(near(keeps->score, wilsonLowerBound(24.625, 24.625), 1e-9), "…and is the Wilson lower bound on the faded counts");
            // The same 10 of 10: at age 0 it is 10 of 10, at age 2 it is 5.6 of 5.6 — the rate
            // is unchanged and the bound is lower. Unmeasured swings are in neither count.
            check(near(fresh->score, wilsonLowerBound(10, 10), 1e-9) && near(old->score, wilsonLowerBound(5.625, 5.625), 1e-9)
                  && fresh->score > old->score && near(dtl->score, old->score, 1e-9),
                  "age shrinks the evidence, not the rate; sessions that could not look do not count against it");
            check(old->score > 0.5, "a fault seen on every swing it could be judged on still ranks high three sessions on");
            check(eased->swingsFired == 20 && eased->swingsAssessable == 33,
                  "swings on which it could be judged are counted whether it fired or not");
            check(old->status == WorkOnStatus::Active && dtl->status == WorkOnStatus::Active,
                  "a fault no later session could measure stays ACTIVE — absence is not improvement");
            check(dtl->latestConclusiveIndex == 0 && dtl->history[3].state == SessionEvidence::NotMeasured,
                  "…and says when it was last measured");
            check(eased->status == WorkOnStatus::Easing, "quiet once after a pattern is easing");
            check(gone->status == WorkOnStatus::Cleared, "quiet twice running is cleared — the 2-of-2 in between cleared nothing and reset nothing");
            check(agg.back().id == QStringLiteral("gone"), "cleared sorts last");
            check(keeps->hasLatest && keeps->latestSessionId == QStringLiteral("s4"), "the latest listing is the newest session's");
        }

        // A relapse re-activates.
        all.push_back(reduce({ plan("gone", rep(1, 10)), plan("keeps", rep(1, 10)) }, cls, {}, "s5", t0 + 4 * day));
        const std::vector<WorkOnSummary> again = aggregateWorkOns(all);
        check(find(again, "gone") && find(again, "gone")->status == WorkOnStatus::Active, "a cleared fault that returns is active again");

        // Pattern but crowded off a session's list still counts, at half.
        const QHash<QString, WorkOnClass> six{ { "a", M }, { "b", M }, { "c", M }, { "d", M }, { "e", M }, { "f", M } };
        const std::vector<SessionWorkOns> crowd = {
            reduce({ plan("f", rep(1, 10), 9.0) }, six, {}, "c1", t0),
            reduce({ plan("a", rep(1, 10), 8.0), plan("b", rep(1, 10), 7.0), plan("c", rep(1, 10), 6.0),
                     plan("d", rep(1, 10), 5.0), plan("e", rep(1, 10), 4.0), plan("f", rep(1, 10), 3.0) }, six, {}, "c2", t0 + day),
        };
        const std::vector<WorkOnSummary> c = aggregateWorkOns(crowd);
        check(find(c, "f") && near(find(c, "f")->score, wilsonLowerBound(17.5, 17.5), 1e-9) && find(c, "f")->sessionsListed == 1
              && find(c, "f")->sessionsPattern == 2, "swings in a session whose five it missed still count");

        // Twenty swings in one session outrank three in each of two.
        const QHash<QString, WorkOnClass> two{ { "big", M }, { "small", M } };
        const std::vector<WorkOnSummary> sz = aggregateWorkOns({
            reduce({ plan("small", rep(1, 3)) }, two, {}, "z1", t0),
            reduce({ plan("small", rep(1, 3)), plan("big", rep(1, 20)) }, two, {}, "z2", t0 + day) });
        check(sz.size() == 2 && sz.front().id == QStringLiteral("big"),
              "a fault on twenty swings of one session leads one on three swings of each of two");

        // A fault on half its judged swings ranks under one on all of far fewer.
        const QHash<QString, WorkOnClass> hv{ { "half", M }, { "all", M } };
        const std::vector<WorkOnSummary> h = aggregateWorkOns({
            reduce({ plan("half", {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0}), plan("all", {1,1,1,1,1,1}) }, hv, {}, "h1", t0) });
        check(h.size() == 2 && h.front().id == QStringLiteral("all") && h.back().swingsFired == 12,
              "six of six outranks twelve of twenty: the rate where it could be judged, not the raw count");

        check(aggregateWorkOns({}).empty(), "no sessions, no work-ons");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
