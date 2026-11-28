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

// coaching_plan.h — Home's stage-organised Coaching tab: shared habit pool with swing_areas.h,
// static Setup checks separated from the one dynamic swing thought ("Your focus"), Wilson-bound
// ordering within each stage, authored upstream/downstream cues without causal overclaiming, and
// clean empty/setup-only states. Header-only, no pack.

#include "../../Diagnostics/derived/coaching_plan.h"

#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static constexpr qint64 kT0  = 1'700'000'000'000LL;
static constexpr qint64 kDay = 86'400'000LL;

struct Ev { const char *id; int fired, assessable; };

static SessionEvidence stateOf(int fired, int assessable)
{
    if (assessable < 1) return SessionEvidence::NotMeasured;
    if (assessable >= kDiagMinAssessableForPattern && wilsonLowerBound(fired, assessable) >= kDiagPatternWilsonLb)
        return SessionEvidence::Pattern;
    if (assessable >= kDiagMinAssessableForPattern && wilsonUpperBound(fired, assessable) < kPriorityQuietWilsonUb)
        return SessionEvidence::Quiet;
    return SessionEvidence::Inconclusive;
}

static SessionPriorities session(const char *id, int day, const std::vector<Ev> &evs)
{
    SessionPriorities s;
    s.sessionId = QString::fromLatin1(id);
    s.startMs   = kT0 + qint64(day) * kDay;
    s.endMs     = s.startMs + 3'600'000LL;
    for (const Ev &e : evs) {
        PriorityEvidence w;
        w.id         = QString::fromLatin1(e.id);
        w.state      = stateOf(e.fired, e.assessable);
        w.fired      = e.fired;
        w.assessable = e.assessable;
        if (w.state == SessionEvidence::NotMeasured) continue;
        s.evidence.push_back(w);
        s.shotCount = std::max(s.shotCount, e.assessable);
    }
    return s;
}

static CoachingConditionInfo ci(const char *group, int when, const char *golfer,
                                const char *well = "", const char *why = "",
                                const char *drillId = "", const char *drillLabel = "",
                                const char *drillInst = "", bool counts = true)
{
    CoachingConditionInfo i;
    i.group            = QString::fromLatin1(group);
    i.when             = when;
    i.stage            = coachingStageOf(when, i.group);
    i.counts           = counts;
    i.golfer           = QString::fromLatin1(golfer);
    i.golferWell       = QString::fromLatin1(well);
    i.golferWhy        = QString::fromLatin1(why);
    i.drillId          = QString::fromLatin1(drillId);
    i.drillLabel       = QString::fromLatin1(drillLabel);
    i.drillInstruction = QString::fromLatin1(drillInst);
    return i;
}

static const CoachingStageGroup *stageOf(const CoachingPlan &p, CoachingStage s)
{
    for (const CoachingStageGroup &g : p.stages)
        if (g.stage == s) return &g;
    return nullptr;
}

static QStringList idsOf(const std::vector<CoachingHabitItem> &items)
{
    QStringList out;
    for (const CoachingHabitItem &i : items) out << i.conditionId;
    return out;
}

int main()
{
    std::printf("coaching_plan_test\n");

    // ── Empty library ───────────────────────────────────────────────────────────
    {
        const CoachingPlan p = buildCoachingPlan({}, {});
        check(!p.ready && p.setupChecks.empty() && !p.focus.present && p.stages.empty(),
              "no sessions: plan is not ready and holds nothing");
    }

    // ── Stage mapping from P1..P10 ──────────────────────────────────────────────
    {
        check(coachingStageOf(1, "setup") == CoachingStage::Setup
                  && coachingStageOf(0, "setup") == CoachingStage::Setup,
              "P1 and setup group map to Setup");
        check(coachingStageOf(2, "planePath") == CoachingStage::BackswingTop
                  && coachingStageOf(3, "arms") == CoachingStage::BackswingTop
                  && coachingStageOf(4, "lateral") == CoachingStage::BackswingTop,
              "P2..P4 map to Backswing & top");
        check(coachingStageOf(5, "rotation") == CoachingStage::DownswingImpact
                  && coachingStageOf(6, "wrists") == CoachingStage::DownswingImpact
                  && coachingStageOf(7, "posture") == CoachingStage::DownswingImpact,
              "P5..P7 map to Downswing & impact");
        check(coachingStageOf(8, "lateral") == CoachingStage::Finish
                  && coachingStageOf(10, "rotation") == CoachingStage::Finish,
              "P8..P10 map to Finish");
    }

    // ── Setup checks are separated and NEVER hijack Your focus ──────────────────
    {
        const QHash<QString, CoachingConditionInfo> info{
            { "ball_back",     ci("setup", 1, "the ball is too far back", "the ball sits opposite your lead heel",
                                  "Ball position shifts your low point.", "d_ball", "Ball position stick", "Lay a stick.") },
            { "stance_narrow", ci("setup", 1, "your stance is too narrow", "your feet are shoulder-width apart") },
            { "sway",          ci("lateral", 4, "your hips sway away going back", "your hips turn in place going back",
                                  "Sway moves the bottom of your swing.", "d_hip", "Trail hip load and hold", "Turn into your trail hip.") },
            { "trail_knee",    ci("lateral", 4, "your trail leg straightens going back", "your trail knee holds its flex",
                                  "Straightening lets your hips slide.", "d_hip", "Trail hip load and hold", "Turn into your trail hip.") },
            { "ee",            ci("posture", 7, "your hips move toward the ball", "your hips stay back through impact") },
            { "arms_first",    ci("rotation", 7, "your arms outrun your chest", "your chest leads your arms down") },
            { "one_block",     ci("rotation", 7, "your chest doesn't speed up past your hips") }, // 1/1 -> Unclear, excluded
            { "occasional",    ci("posture", 7, "your head drops", "") },                         // 3/10 -> Occasional, excluded
            { "finish",        ci("lateral", 10, "you fall off balance at the finish") },
        };
        const PriorityCauses causes{
            { QStringLiteral("sway"), QStringLiteral("ee") },
            { QStringLiteral("ball_back"), QStringLiteral("arms_first") },
        };
        const std::vector<SessionPriorities> ss{
            session("s1", 0, { { "ball_back", 10, 10 }, { "stance_narrow", 8, 10 }, { "sway", 9, 10 },
                               { "trail_knee", 10, 10 }, { "ee", 12, 12 }, { "arms_first", 25, 25 },
                               { "one_block", 1, 1 }, { "occasional", 3, 10 }, { "finish", 8, 10 } }),
        };

        const CoachingPlan p = buildCoachingPlan(ss, info, causes);
        check(p.ready, "plan is ready");
        check(idsOf(p.setupChecks) == (QStringList{ "ball_back", "stance_narrow" }),
              "Setup habits go into setupChecks, ordered by Wilson lower bound");
        check(p.focus.present && p.focus.title == QStringLiteral("Trail hip load and hold")
                  && p.focus.stageKey == QStringLiteral("backswingTop")
                  && p.focus.stageLabel == QStringLiteral("Backswing & top")
                  && p.focus.focusConditionId == QStringLiteral("trail_knee"),
              "Your focus picks the earliest DYNAMIC drill group — never hijacked by Setup");
        check(idsOf(p.focus.rightNow) == (QStringList{ "trail_knee", "sway" }) && p.focus.aimFor.size() == 2,
              "Your focus groups both habits sharing the drill, ordered by Wilson lower bound");
        check(p.focus.reasonShort.contains(QStringLiteral("covers 2 habits"))
                  && p.focus.reasonShort.contains(QStringLiteral("often eases your hips move toward the ball")),
              "focus reason names the habit count and the authored downstream habit in golfer words");
        check(!p.focus.reasonShort.contains(QStringLiteral("because"), Qt::CaseInsensitive)
                  && !p.focus.reasonShort.contains(QStringLiteral("cause"), Qt::CaseInsensitive),
              "focus reason never uses causal-claim words");

        const CoachingStageGroup *bs = stageOf(p, CoachingStage::BackswingTop);
        const CoachingStageGroup *ds = stageOf(p, CoachingStage::DownswingImpact);
        const CoachingStageGroup *fn = stageOf(p, CoachingStage::Finish);
        check(p.stages.size() == 3 && bs && ds && fn,
              "all three dynamic stages are present when Finish has a habit");
        check(idsOf(bs->items) == (QStringList{ "trail_knee", "sway" })
                  && bs->items[0].isFocus && bs->items[1].isFocus,
              "Backswing & top keeps the focus items in place and marks them isFocus");
        check(idsOf(ds->items) == (QStringList{ "arms_first", "ee" }),
              "Downswing & impact ranks by Wilson lower bound (25/25 > 12/12) and excludes 1/1 and 3/10");
        check(ds->items[0].upstreamNote == QStringLiteral("May follow from the ball is too far back")
                  && ds->items[1].upstreamNote == QStringLiteral("May follow from your hips sway away going back"),
              "downstream habits carry gentle upstream notes from earlier active habits (Setup or Backswing)");
        check(idsOf(fn->items) == (QStringList{ "finish" }), "Finish holds its habit");
    }

    // ── Wilson lower bound beats raw share (3/3 vs 152/153) ─────────────────────
    {
        const QHash<QString, CoachingConditionInfo> info{
            { "small3", ci("rotation", 7, "small three of three") },
            { "solid",  ci("rotation", 7, "established habit") },
        };
        const CoachingPlan p = buildCoachingPlan(
            { session("s1", 0, { { "small3", 3, 3 }, { "solid", 152, 153 } }) }, info);
        const CoachingStageGroup *ds = stageOf(p, CoachingStage::DownswingImpact);
        const CoachingStageGroup *fn = stageOf(p, CoachingStage::Finish);
        check(ds && idsOf(ds->items) == (QStringList{ "solid", "small3" })
                  && p.focus.focusConditionId == QStringLiteral("solid"),
              "152/153 beats 3/3 in stage order and focus selection");
        check(p.stages.size() == 3 && fn && fn->items.empty(),
              "all three dynamic stages are always present, with Finish empty when clean");
    }

    // ── Setup-only golfer: no invented dynamic focus, all stages still shown ────
    {
        const QHash<QString, CoachingConditionInfo> info{
            { "ball_back", ci("setup", 1, "the ball is too far back") },
        };
        const CoachingPlan p = buildCoachingPlan(
            { session("s1", 0, { { "ball_back", 10, 10 } }) }, info);
        check(p.ready && p.setupChecks.size() == 1 && !p.focus.present && p.stages.size() == 3
                  && p.stages[0].items.empty() && p.stages[1].items.empty() && p.stages[2].items.empty(),
              "only Setup habits: setupChecks shown, no invented focus, all three dynamic stages shown clean");
    }

    // ── Session pips and trend across sessions ──────────────────────────────────
    {
        const QHash<QString, CoachingConditionInfo> info{
            { "grow", ci("posture", 7, "your hips rise through impact") },
            { "ease", ci("posture", 7, "your chest lifts through impact") },
        };
        const std::vector<SessionPriorities> ss{
            session("s1", 0, { { "grow", 3, 10 }, { "ease", 10, 10 } }),
            session("s2", 1, { { "grow", 4, 10 }, { "ease", 10, 10 } }),
            session("s3", 2, { { "grow", 9, 10 }, { "ease", 6, 10 } }),
            session("s4", 3, { { "grow", 9, 10 }, { "ease", 6, 10 } }),
        };
        const CoachingPlan p = buildCoachingPlan(ss, info);
        const CoachingStageGroup *ds = stageOf(p, CoachingStage::DownswingImpact);
        const CoachingHabitItem *grow = nullptr, *ease = nullptr;
        if (ds) for (const CoachingHabitItem &it : ds->items) {
            if (it.conditionId == QLatin1String("grow")) grow = &it;
            if (it.conditionId == QLatin1String("ease")) ease = &it;
        }
        check(grow && grow->trend == 1 && grow->sessions == (std::vector<bool>{ false, false, true, true }),
              "growing habit carries trend +1 and per-session pips");
        check(ease && ease->trend == -1 && ease->sessions == (std::vector<bool>{ true, true, true, true }),
              "easing habit carries trend -1 and per-session pips");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
