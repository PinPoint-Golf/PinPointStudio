// Standalone tests for the analysis → profiler bridge (src/Analysis/analysis_profiling.cpp):
// recordAnalysisRun() feeds each ran stage's wall time into a "Analysis.Stage.<name>"
// profiler scope (aggregate calls/total/min/max) plus a whole-run "Analysis.analyze"
// scope, and appends one full per-stage record to AnalysisProfileLog. Skipped stages
// are recorded in the run breakdown but NOT in the aggregate scopes; a halted run is
// ok=false. Own main()/check(), no fixture — builds an AnalysisContext with a
// hand-authored ctx.trace (no real analyzer, window = nullptr).
//
//   cmake --build build/analyzer-tests --target analysis_profiling_test
//   ctest --test-dir build/analyzer-tests -R analysis_profiling_test --output-on-failure

#include "../analysis_profiling.h"
#include "../analysis_stage.h"

#include "AnalysisProfileLog.h"
#include "pp_profiler.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

using namespace pinpoint::analysis;
using pinpoint::profiling::Profiler;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// Find a scope by name in a profiler snapshot (nullopt if absent).
static std::optional<Profiler::ScopeStat> findScope(const Profiler::Snapshot &snap,
                                                    const std::string &name)
{
    for (const auto &s : snap.scopes)
        if (s.name == name) return s;
    return std::nullopt;
}

// Append a stage trace entry (ran, with wall) to a context.
static void pushRan(AnalysisContext &ctx, const QString &name, qint64 ns)
{
    StageTraceEntry e;
    e.name = name;
    e.ran = true;
    e.elapsedNs = ns;
    ctx.trace.push_back(std::move(e));
}
static void pushSkip(AnalysisContext &ctx, const QString &name, const QString &reason)
{
    StageTraceEntry e;
    e.name = name;
    e.skipReason = reason;   // ran defaults false, elapsedNs 0
    ctx.trace.push_back(std::move(e));
}

int main()
{
    std::printf("=== analysis_profiling (bridge → profiler scopes + run log) ===\n");

    ShotAnalysisJob job;          // sessionType default -1; set per-run below
    job.sessionType = 1;          // Wrist
    const CaptureCapabilities caps;
    AnalysisProfileLog *log = AnalysisProfileLog::instance();

    // 1. A single Wrist run: two ran stages + one skipped stage.
    int seq = -1;
    {
        AnalysisContext ctx{ caps, job, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        ctx.detail->score.overall = 42;
        ctx.detail->pose2d.frames.resize(3);   // frames == 3
        ctx.wall.start();
        pushRan(ctx, QStringLiteral("Pose"), 5'000'000);    // 5 ms
        pushRan(ctx, QStringLiteral("Shaft"), 2'000'000);   // 2 ms
        pushSkip(ctx, QStringLiteral("Ball"), QStringLiteral("no pose"));

        recordAnalysisRun(QStringLiteral("Wrist"), ctx);

        const Profiler::Snapshot snap = Profiler::instance().snapshot();
        const auto pose  = findScope(snap, "Analysis.Stage.Pose");
        const auto shaft = findScope(snap, "Analysis.Stage.Shaft");
        const auto ball  = findScope(snap, "Analysis.Stage.Ball");
        const auto total = findScope(snap, "Analysis.analyze");

        check(pose && pose->calls == 1 && pose->wall_ns_total == 5'000'000
                  && pose->wall_ns_max == 5'000'000,
              "aggregate: Analysis.Stage.Pose n=1 total=max=5ms");
        check(shaft && shaft->calls == 1 && shaft->wall_ns_total == 2'000'000,
              "aggregate: Analysis.Stage.Shaft n=1 total=2ms");
        check(!ball.has_value(), "aggregate: skipped stage (Ball) not recorded as a scope");
        check(total && total->calls == 1, "aggregate: Analysis.analyze whole-run scope recorded");

        QList<AnalysisProfileLog::AnalysisRun> runs = log->fetchSince(seq);
        check(runs.size() == 1, "runlog: exactly one run recorded");
        if (!runs.isEmpty()) {
            const AnalysisProfileLog::AnalysisRun &r = runs.first();
            check(r.profile == QStringLiteral("Wrist") && r.ok && r.sessionType == 1,
                  "runlog: profile=Wrist ok=true sessionType=1");
            check(r.frames == 3 && r.score == 42.0, "runlog: frames=3 score=42 carried from detail");
            check(r.totalMs >= 0.0, "runlog: totalMs measured (>= 0)");
            check(r.stages.size() == 3, "runlog: one stage entry per trace entry (incl skipped)");
            const bool poseRow  = r.stages.size() > 0 && r.stages[0].name == QStringLiteral("Pose")
                                  && r.stages[0].ran && r.stages[0].ms == 5.0;
            const bool ballRow  = r.stages.size() > 2 && r.stages[2].name == QStringLiteral("Ball")
                                  && !r.stages[2].ran && r.stages[2].skipReason == QStringLiteral("no pose")
                                  && r.stages[2].ms == 0.0;
            check(poseRow, "runlog: Pose row ran=true ms=5.0");
            check(ballRow, "runlog: Ball row ran=false ms=0 skipReason carried");
        }
    }

    // 2. A second run accumulates into the aggregate scope (min/max/total), and a new
    //    run appears in the log.
    {
        AnalysisContext ctx{ caps, job, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        ctx.wall.start();
        pushRan(ctx, QStringLiteral("Pose"), 3'000'000);   // 3 ms

        recordAnalysisRun(QStringLiteral("Wrist"), ctx);

        const Profiler::Snapshot snap = Profiler::instance().snapshot();
        const auto pose = findScope(snap, "Analysis.Stage.Pose");
        check(pose && pose->calls == 2 && pose->wall_ns_total == 8'000'000
                  && pose->wall_ns_max == 5'000'000 && pose->wall_ns_min == 3'000'000,
              "aggregate: Pose accumulates n=2 total=8ms max=5ms min=3ms");

        QList<AnalysisProfileLog::AnalysisRun> runs = log->fetchSince(seq);
        check(runs.size() == 1, "runlog: only the new (2nd) run returned by fetchSince cursor");
    }

    // 3. A halted run: ok=false, and the halted-skipped stages still appear in the run
    //    breakdown but not in the aggregate scopes.
    {
        AnalysisContext ctx{ caps, job, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        ctx.halted = true;
        ctx.wall.start();
        pushRan(ctx, QStringLiteral("ImuResample"), 1'000'000);
        pushSkip(ctx, QStringLiteral("Pose"), QStringLiteral("halted"));

        recordAnalysisRun(QStringLiteral("Wrist"), ctx);

        QList<AnalysisProfileLog::AnalysisRun> runs = log->fetchSince(seq);
        check(runs.size() == 1 && !runs.first().ok, "runlog: halted run recorded with ok=false");
        check(runs.first().stages.size() == 2, "runlog: halted run keeps its full stage breakdown");
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
