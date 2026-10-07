// analysis_dag_test — the analysis DAG executor (analysis_dag.h; analysis_dag_design.md step B)
// driven by fake stages, no window, no analysis code:
//   1. the graph: RAW / WAR / WAW / append edges, the implicit `halted` read, the serial group,
//      an undeclared stage as a barrier, every edge forward;
//   2. the run: same trace shape as runStages, every stage starts after its predecessors end,
//      stages overlap where the graph allows;
//   3. detail->series: appenders that finish in REVERSE authored order still splice in authored
//      order; a reader sees exactly its authored prefix; the final series equals runStages';
//   4. a halt skips every later stage, as the loop does;
//   5. two stages holding the same camera never overlap; different cameras do.

#include "../analysis_dag.h"

#include <QJsonArray>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// A stage with a declaration, a sleep, and optional actions.
class FakeStage : public AnalysisStage {
public:
    FakeStage(QString n, StageDecl d, int sleepMs = 0) : m_name(std::move(n)), m_decl(std::move(d)), m_sleep(sleepMs) {}
    QString name() const override { return m_name; }
    StageDecl decl() const override { return m_decl; }
    void run(AnalysisContext &ctx) override
    {
        if (m_sleep > 0) std::this_thread::sleep_for(std::chrono::milliseconds(m_sleep));
        if (appendKey.size()) {
            MetricSeries m;
            m.key = appendKey;
            ctx.seriesOut().push_back(m);
        }
        if (readSeries) {
            seen.clear();
            for (const MetricSeries &m : ctx.detail->series) seen << m.key;
        }
        if (halt) ctx.halted = true;
    }
    QString     appendKey;
    bool        readSeries = false;
    bool        halt = false;
    QStringList seen;
private:
    QString   m_name;
    StageDecl m_decl;
    int       m_sleep;
};

class Undeclared : public AnalysisStage {
public:
    QString name() const override { return QStringLiteral("Undeclared"); }
    void run(AnalysisContext &) override {}
};

static StageDecl decl(std::vector<QString> r, std::vector<QString> w, std::vector<QString> a = {})
{
    StageDecl d;
    d.reads = std::move(r);
    d.writes = std::move(w);
    d.appends = std::move(a);
    return d;
}

static bool hasEdge(const StageGraph &g, int a, int b)
{
    for (int s : g.succs[size_t(a)]) if (s == b) return true;
    return false;
}

int main()
{
    ShotAnalysisJob job;

    std::printf("1. graph\n");
    {
        SessionProfile p;
        p.stages.push_back(std::make_unique<FakeStage>("W",  decl({}, { "seg" })));                 // 0
        p.stages.push_back(std::make_unique<FakeStage>("R1", decl({ "seg" }, { "ball" })));         // 1
        p.stages.push_back(std::make_unique<FakeStage>("R2", decl({ "seg" }, {})));                 // 2
        p.stages.push_back(std::make_unique<FakeStage>("M",  decl({ "seg" }, { "seg" })));          // 3
        p.stages.push_back(std::make_unique<FakeStage>("Leaf", decl({ "detail.impact" }, {})));     // 4
        p.stages.push_back(std::make_unique<FakeStage>("Halt", decl({}, { "halted" })));            // 5
        p.stages.push_back(std::make_unique<FakeStage>("After", decl({}, { "detail.score" })));     // 6
        p.stages.push_back(std::make_unique<Undeclared>());                                         // 7
        p.stages.push_back(std::make_unique<FakeStage>("Tail", decl({}, { "detail.bindings" })));   // 8
        const StageGraph g = buildStageGraph(p);
        check(hasEdge(g, 0, 1) && hasEdge(g, 0, 2), "RAW: writer → both readers");
        check(!hasEdge(g, 1, 2), "two readers of one resource: no edge");
        check(hasEdge(g, 1, 3) && hasEdge(g, 2, 3), "WAR: the readers before a mutator keep their place");
        check(hasEdge(g, 0, 3), "WAW: writer → mutator");
        check(!hasEdge(g, 0, 4) && hasEdge(g, 4, 5), "a leaf has no data edge, only the implicit halted read");
        check(hasEdge(g, 5, 6), "halted: the halting stage → every later stage");
        check(hasEdge(g, 6, 7) && hasEdge(g, 7, 8), "undeclared stage is a barrier both ways");
        bool forward = true;
        for (const StageEdge &e : g.edges) if (e.from >= e.to) forward = false;
        check(forward, "every edge points forward (authored order is a topological order)");
        bool someReduced = false;
        for (const StageEdge &e : g.edges) someReduced = someReduced || e.reduced;
        check(someReduced, "transitive reduction marks implied edges");
        const QJsonObject j = stageGraphToJson(QStringLiteral("T"), g);
        check(j.value("nodes").toArray().size() == 9 && j.value("edges").toArray().size() == int(g.edges.size()),
              "JSON export: one node per stage, every edge");
        bool known = true;
        for (const char *r : { "seg", "ball", "detail.impact", "halted", "detail.score", "detail.bindings" })
            known = known && res::isKnownResource(QString::fromLatin1(r));
        check(known && !res::isKnownResource(QStringLiteral("detail.serise")), "resource table catches a typo");

        // haltSafe (DtlPose under pose.dtlEarly): no implicit halted read, so no edge from the
        // halting stage — its data edges stay.
        SessionProfile q;
        q.stages.push_back(std::make_unique<FakeStage>("Pose", decl({}, { "detail.pose2d" })));            // 0
        q.stages.push_back(std::make_unique<FakeStage>("Halt", decl({ "ctx.series" }, { "halted" })));     // 1
        StageDecl hs = decl({ "detail.pose2d" }, { "detail.poseDtl" });
        hs.haltSafe = true;
        q.stages.push_back(std::make_unique<FakeStage>("DtlEarly", hs));                                   // 2
        q.stages.push_back(std::make_unique<FakeStage>("Plain", decl({ "detail.pose2d" }, {})));           // 3
        const StageGraph gq = buildStageGraph(q);
        check(hasEdge(gq, 0, 2) && !hasEdge(gq, 1, 2), "haltSafe: the data edge stays, the halt edge goes");
        check(hasEdge(gq, 1, 3), "haltSafe is per stage: a plain later stage still waits for the halt");

        SessionProfile s;
        for (const char *nm : { "SegResolve", "X", "EventRefine", "PositionsLadder", "TimelineFusion" })
            s.stages.push_back(std::make_unique<FakeStage>(nm, decl({}, {})));
        const StageGraph gs = buildStageGraph(s);
        check(hasEdge(gs, 0, 2) && hasEdge(gs, 2, 3) && hasEdge(gs, 3, 4) && !hasEdge(gs, 0, 1)
                  && gs.groups.size() == 1 && gs.groups[0].size() == 4,
              "serial group chains its members by name, nothing else");
    }

    std::printf("2+3. run, overlap, series splice\n");
    {
        // B0 overwrites detail.series; A1..A3 only append, sleeping LONGEST first so they finish
        // in reverse; R reads the series (sees A1..A3), A4 appends after R; K is an independent
        // 120 ms leaf that must overlap the appenders.
        auto build = [](std::vector<FakeStage *> &out) {
            SessionProfile p;
            auto add = [&](FakeStage *f) { out.push_back(f); p.stages.emplace_back(f); };
            add(new FakeStage("B0", decl({}, { "detail.series" })));
            FakeStage *a1 = new FakeStage("A1", decl({ "seg" }, {}, { "detail.series" }), 90); a1->appendKey = "a1"; add(a1);
            FakeStage *a2 = new FakeStage("A2", decl({ "seg" }, {}, { "detail.series" }), 50); a2->appendKey = "a2"; add(a2);
            FakeStage *a3 = new FakeStage("A3", decl({ "seg" }, {}, { "detail.series" }), 10); a3->appendKey = "a3"; add(a3);
            FakeStage *k  = new FakeStage("K",  decl({ "detail.impact" }, { "detail.addressMarks" }), 120); add(k);
            FakeStage *r  = new FakeStage("R",  decl({ "detail.series" }, { "detail.score" })); r->readSeries = true; add(r);
            FakeStage *a4 = new FakeStage("A4", decl({ "seg" }, {}, { "detail.series" }), 5); a4->appendKey = "a4"; add(a4);
            return p;
        };
        std::vector<FakeStage *> seqStages, parStages;
        SessionProfile seqP = build(seqStages), parP = build(parStages);

        AnalysisContext seq{ CaptureCapabilities{}, job, nullptr };
        seq.detail = std::make_shared<SwingAnalysis>();
        runStages(seqP, seq);
        AnalysisContext par{ CaptureCapabilities{}, job, nullptr };
        par.detail = std::make_shared<SwingAnalysis>();
        runStagesParallel(parP, par, 4);

        QStringList seqKeys, parKeys;
        for (const MetricSeries &m : seq.detail->series) seqKeys << m.key;
        for (const MetricSeries &m : par.detail->series) parKeys << m.key;
        check(seqKeys == (QStringList{ "a1", "a2", "a3", "a4" }), "loop: series in authored order");
        check(parKeys == seqKeys, "DAG: series spliced in authored order though A3 finished first");
        check(parStages[5]->seen == (QStringList{ "a1", "a2", "a3" }) && parStages[5]->seen == seqStages[5]->seen,
              "the reader saw exactly its authored prefix (not A4)");

        check(par.trace.size() == parP.stages.size(), "trace: one entry per stage");
        bool namesOk = true, allRan = true;
        for (size_t i = 0; i < par.trace.size(); ++i) {
            namesOk = namesOk && par.trace[i].name == parP.stages[i]->name();
            allRan  = allRan && par.trace[i].ran;
        }
        check(namesOk && allRan, "trace: authored order, every stage ran");
        const StageGraph g = buildStageGraph(parP);
        bool depsOk = true;
        for (const StageEdge &e : g.edges)
            if (par.trace[size_t(e.to)].startNs < par.trace[size_t(e.from)].endNs) depsOk = false;
        check(depsOk, "every stage started after each of its predecessors ended");
        const StageTraceEntry &k = par.trace[4], &a1 = par.trace[1];
        check(k.startNs < a1.endNs && a1.startNs < k.endNs && k.thread != a1.thread,
              "the independent leaf overlapped an appender on another thread");
        qint64 parWall = 0, seqWall = 0;
        for (const StageTraceEntry &e : par.trace) parWall = std::max(parWall, e.endNs);
        for (const StageTraceEntry &e : seq.trace) seqWall = std::max(seqWall, e.endNs);
        std::printf("    wall: loop %.0f ms, DAG %.0f ms\n", seqWall / 1e6, parWall / 1e6);
        check(parWall < seqWall, "the DAG run is shorter than the loop");
    }

    std::printf("4. halt\n");
    {
        SessionProfile p;
        p.stages.push_back(std::make_unique<FakeStage>("Early", decl({}, { "detail.impact" }), 20));
        auto *h = new FakeStage("Halt", decl({ "ctx.series" }, { "halted" }));
        h->halt = true;
        p.stages.emplace_back(h);
        p.stages.push_back(std::make_unique<FakeStage>("Later1", decl({}, { "detail.score" })));
        p.stages.push_back(std::make_unique<FakeStage>("Later2", decl({ "seg" }, {})));
        AnalysisContext ctx{ CaptureCapabilities{}, job, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        runStagesParallel(p, ctx, 4);
        check(ctx.trace.size() == 4 && ctx.trace[0].ran && ctx.trace[1].ran, "the stages up to the halt ran");
        check(!ctx.trace[2].ran && ctx.trace[2].skipReason == QStringLiteral("halted")
                  && !ctx.trace[3].ran && ctx.trace[3].skipReason == QStringLiteral("halted"),
              "every later stage skipped as \"halted\"");
    }

    std::printf("5. one reader per camera\n");
    {
        ShotAnalysisJob camJob;
        camJob.cameraSources = { pinpoint::SourceId(1) };
        camJob.dtlSource = pinpoint::SourceId(2);
        SessionProfile p;
        p.stages.push_back(std::make_unique<FakeStage>("Dtl1", decl({ "window.dtl" }, { "detail.shaftDtl" }), 60));
        p.stages.push_back(std::make_unique<FakeStage>("Dtl2", decl({ "window.dtl" }, { "detail.addressMarks" }), 60));
        p.stages.push_back(std::make_unique<FakeStage>("Fo",   decl({ "window.faceOn" }, { "detail.impact" }), 60));
        AnalysisContext ctx{ CaptureCapabilities{}, camJob, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        runStagesParallel(p, ctx, 4);
        const StageTraceEntry &d1 = ctx.trace[0], &d2 = ctx.trace[1], &fo = ctx.trace[2];
        const auto overlap = [](const StageTraceEntry &a, const StageTraceEntry &b) {
            return a.startNs < b.endNs && b.startNs < a.endNs;
        };
        check(!overlap(d1, d2), "the two down-the-line stages never overlapped");
        check(overlap(fo, d1) || overlap(fo, d2), "the face-on stage overlapped a down-the-line one");
    }

    std::printf("6. frame store (decode.frameStore)\n");
    {
        // Store fetches the face-on window into frames.faceOn; A and B read only the store (no
        // camera held) and overlap; Legacy still declares window.faceOn and is ordered after the
        // store by the implicit frames.faceOn read; Early (authored before the store) keeps its
        // place before it.
        ShotAnalysisJob camJob;
        camJob.cameraSources = { pinpoint::SourceId(1) };
        SessionProfile p;
        p.stages.push_back(std::make_unique<FakeStage>("Early",  decl({ "window.faceOn" }, { "detail.impact" }), 10));     // 0
        p.stages.push_back(std::make_unique<FakeStage>("Store",  decl({ "window.faceOn" }, { "frames.faceOn" }), 30));     // 1
        p.stages.push_back(std::make_unique<FakeStage>("A",      decl({ "frames.faceOn" }, { "detail.shaft" }), 80));      // 2
        p.stages.push_back(std::make_unique<FakeStage>("B",      decl({ "frames.faceOn" }, { "detail.addressMarks" }), 80)); // 3
        p.stages.push_back(std::make_unique<FakeStage>("Legacy", decl({ "window.faceOn" }, { "detail.score" }), 10));      // 4
        const StageGraph g = buildStageGraph(p, &camJob);
        check(res::isKnownResource(QStringLiteral("frames.faceOn")) && res::isKnownResource(QStringLiteral("frames.dtl"))
                  && !res::isCameraResource(QStringLiteral("frames.faceOn")),
              "frames.faceOn / frames.dtl are known resources, not camera readers");
        check(hasEdge(g, 1, 2) && hasEdge(g, 1, 3) && !hasEdge(g, 2, 3), "store → both readers, readers unordered");
        check(hasEdge(g, 1, 4), "a window reader after the store reads the store implicitly");
        check(hasEdge(g, 0, 1), "a window reader before the store keeps its place (WAR)");
        AnalysisContext ctx{ CaptureCapabilities{}, camJob, nullptr };
        ctx.detail = std::make_shared<SwingAnalysis>();
        runStagesParallel(p, ctx, 4);
        const auto overlap = [](const StageTraceEntry &a, const StageTraceEntry &b) {
            return a.startNs < b.endNs && b.startNs < a.endNs;
        };
        check(overlap(ctx.trace[2], ctx.trace[3]), "two readers of one store ran at once");
        check(ctx.trace[2].startNs >= ctx.trace[1].endNs && ctx.trace[4].startNs >= ctx.trace[1].endNs,
              "every reader started after the store was built");
    }

    std::printf("analysis_dag_test: %s (%d failure%s)\n", g_fail ? "FAIL" : "PASS", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
