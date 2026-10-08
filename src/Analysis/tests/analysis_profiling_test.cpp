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

// Append a ran stage at its place on the timeline (offsets in ms from the analysis start).
static void pushTimed(AnalysisContext &ctx, const QString &name, qint64 startMs, qint64 endMs,
                      int thread, std::vector<int> preds)
{
    StageTraceEntry e;
    e.name = name;
    e.ran = true;
    e.elapsedNs = (endMs - startMs) * 1'000'000;
    e.startNs = startMs * 1'000'000;
    e.endNs = endMs * 1'000'000;
    e.thread = thread;
    e.preds = std::move(preds);
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

    // 4. The timeline reaches the log: offsets, thread and preds per stage.
    {
        AnalysisContext ctx{ caps, job, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        ctx.wall.start();
        pushTimed(ctx, QStringLiteral("Decode"), 0, 100, 0, {});
        pushTimed(ctx, QStringLiteral("Pose"), 100, 400, 0, { 0, 7 });   // 7: out of range, dropped

        recordAnalysisRun(QStringLiteral("Wrist"), ctx);

        QList<AnalysisProfileLog::AnalysisRun> runs = log->fetchSince(seq);
        check(runs.size() == 1, "timeline: run recorded");
        if (!runs.isEmpty()) {
            const AnalysisProfileLog::AnalysisRun &r = runs.first();
            const AnalysisProfileLog::StageTiming &p = r.stages[1];
            check(p.startMs == 100.0 && p.endMs == 400.0 && p.thread == 0,
                  "timeline: start/end offsets and thread carried");
            check(p.preds == QVector<int>{ 0 }, "timeline: preds carried, an out-of-range index dropped");
            check(r.spanMs == 400.0 && r.threads == 1 && r.stages[0].critical && p.critical,
                  "timeline: span, thread count and the critical path filled on record");
        }
    }

    // 5. The critical path on a parallel run (times in ms, T = pool thread):
    //
    //   T0  Decode 0–100 ─┬─ Pose 100–600 ─────────── Shaft 600–900 ── Ladder 900–1000
    //   T1                └─ Ball 100–300   Impact 300–350
    //   T2                   Address 120–200 (a leaf, no dependencies)
    //
    // Ladder depends on Shaft and Impact; Shaft on Pose; Pose and Ball on Decode. The path
    // is Ladder ← Shaft ← Pose ← Decode; Ball, Impact and Address are off it.
    {
        AnalysisProfileLog::AnalysisRun run;
        const auto stage = [](const char *name, double s, double e, int t, QVector<int> preds) {
            AnalysisProfileLog::StageTiming st;
            st.name = QString::fromLatin1(name);
            st.ran = true;
            st.ms = e - s;
            st.startMs = s;
            st.endMs = e;
            st.thread = t;
            st.preds = preds;
            return st;
        };
        run.stages = { stage("Decode", 0, 100, 0, {}),        // 0
                       stage("Pose", 100, 600, 0, { 0 }),     // 1
                       stage("Ball", 100, 300, 1, { 0 }),     // 2
                       stage("Address", 120, 200, 2, {}),     // 3
                       stage("Impact", 300, 350, 1, { 2 }),   // 4
                       stage("Shaft", 600, 900, 0, { 1 }),    // 5
                       stage("Ladder", 900, 1000, 0, { 5, 4 }) }; // 6
        markCriticalPath(run);
        QString onPath;
        for (const auto &s : run.stages) if (s.critical) onPath += s.name + QLatin1Char(' ');
        check(onPath == QStringLiteral("Decode Pose Shaft Ladder "), "path: Ladder ← Shaft ← Pose ← Decode");
        check(run.spanMs == 1000.0 && run.criticalMs == 1000.0 && run.workMs == 1330.0 && run.threads == 3,
              "path: span 1000, on the path 1000, work 1330, 3 threads");

        // Impact now waits for a thread: its dependency (Ball) ended at 300 but it started
        // at 600, when Pose released T0 — it is held, not dependent, and the path follows
        // the release: Ladder ← Impact ← Pose ← Decode (Impact ends after Shaft here).
        run.stages[4] = stage("Impact", 600, 950, 0, { 2 });
        run.stages[5] = stage("Shaft", 600, 900, 1, { 1 });
        run.stages[6] = stage("Ladder", 950, 1000, 0, { 5, 4 });
        markCriticalPath(run);
        onPath.clear();
        for (const auto &s : run.stages) if (s.critical) onPath += s.name + QLatin1Char(' ');
        check(onPath == QStringLiteral("Decode Pose Impact Ladder "),
              "path: a stage held past its dependency follows the stage whose end released it");

        // A sequential run without preds (the loop, or a trace built without the graph):
        // every ran stage is on the path, a skipped one is not.
        AnalysisProfileLog::AnalysisRun seqRun;
        seqRun.stages = { stage("A", 0, 10, 0, {}), stage("B", 10, 30, 0, {}), stage("C", 30, 35, 0, {}) };
        AnalysisProfileLog::StageTiming skipped;
        skipped.name = QStringLiteral("D");
        skipped.startMs = skipped.endMs = 35;
        seqRun.stages.push_back(skipped);
        markCriticalPath(seqRun);
        check(seqRun.stages[0].critical && seqRun.stages[1].critical && seqRun.stages[2].critical
                  && !seqRun.stages[3].critical && seqRun.criticalMs == 35.0 && seqRun.threads == 1,
              "path: a sequential run puts every ran stage on the path");

        AnalysisProfileLog::AnalysisRun none;
        none.stages = { skipped };
        markCriticalPath(none);
        check(none.spanMs == 0.0 && !none.stages[0].critical, "path: nothing ran, no path");
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
