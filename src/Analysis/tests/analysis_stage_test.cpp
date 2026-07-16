// Standalone tests for the capability-gated stage pipeline (src/Analysis/analysis_stage.h):
// authored-order execution, canRun/skipReason gating, the halted short-circuit, product
// gating between stages via the shared AnalysisContext, and the trace's one-entry-per-
// stage contract. Also covers CaptureCapabilities::fromJob + hasRole/hasRoles. Pure
// orchestrator tests — fake stages only, window = nullptr, no SwingWindow/analyzer
// involved. Own main()/check(), no fixture.
//
//   cmake --build build/analyzer-tests --target analysis_stage_test
//   ctest --test-dir build/analyzer-tests -R analysis_stage_test --output-on-failure

#include "../analysis_stage.h"

#include <cstdio>
#include <memory>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// Appends its name to a shared log on run() — proves authored order (canRun defaults true).
class RecordingStage : public AnalysisStage {
public:
    RecordingStage(const QString &n, std::vector<QString> &log) : m_name(n), m_log(log) {}
    QString name() const override { return m_name; }
    void run(AnalysisContext &) override { m_log.push_back(m_name); }
private:
    QString m_name;
    std::vector<QString> &m_log;
};

// canRun() is fixed false — proves the trace records the skip and run() is never invoked.
class NeverRunStage : public AnalysisStage {
public:
    NeverRunStage(const QString &n, const QString &reason, bool &called)
        : m_name(n), m_reason(reason), m_called(called) {}
    QString name() const override { return m_name; }
    bool canRun(const AnalysisContext &) const override { return false; }
    QString skipReason(const AnalysisContext &) const override { return m_reason; }
    void run(AnalysisContext &) override { m_called = true; }
private:
    QString m_name, m_reason;
    bool &m_called;
};

// Sets ctx.halted in run() — the trigger for the short-circuit test.
class HaltStage : public AnalysisStage {
public:
    QString name() const override { return QStringLiteral("Halt"); }
    void run(AnalysisContext &ctx) override { ctx.halted = true; }
};

// canRun() is the AnalysisStage default (always true) — if the halted short-circuit were
// broken, this stage would run and flip `called`.
class WouldRunStage : public AnalysisStage {
public:
    WouldRunStage(const QString &n, bool &called) : m_name(n), m_called(called) {}
    QString name() const override { return m_name; }
    void run(AnalysisContext &) override { m_called = true; }
private:
    QString m_name;
    bool &m_called;
};

// Sets ctx.segImu — the upstream half of a product-gating pair.
class SetSegImuStage : public AnalysisStage {
public:
    QString name() const override { return QStringLiteral("SetSegImu"); }
    void run(AnalysisContext &ctx) override { ctx.segImu = Segmentation{}; }
};

// canRun() reads ctx.segImu.has_value() — the downstream half of the pair.
class NeedsSegImuStage : public AnalysisStage {
public:
    explicit NeedsSegImuStage(std::vector<QString> &log) : m_log(log) {}
    QString name() const override { return QStringLiteral("NeedsSegImu"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.segImu.has_value(); }
    QString skipReason(const AnalysisContext &) const override { return QStringLiteral("no segImu"); }
    void run(AnalysisContext &) override { m_log.push_back(name()); }
private:
    std::vector<QString> &m_log;
};

int main()
{
    std::printf("=== analysis_stage (orchestrator + CaptureCapabilities) ===\n");

    const ShotAnalysisJob job;          // default job: no cameras, no IMUs — unused by
                                         // these orchestrator tests beyond satisfying the
                                         // AnalysisContext reference member.
    const CaptureCapabilities caps;     // empty — pure orchestrator tests, no gating on caps.

    // 1. Authored order preserved — only profile.stages order matters.
    {
        std::vector<QString> log;
        SessionProfile profile;
        profile.stages.push_back(std::make_unique<RecordingStage>(QStringLiteral("A"), log));
        profile.stages.push_back(std::make_unique<RecordingStage>(QStringLiteral("B"), log));
        profile.stages.push_back(std::make_unique<RecordingStage>(QStringLiteral("C"), log));

        AnalysisContext ctx{ caps, job, nullptr };
        runStages(profile, ctx);

        check(log.size() == 3 && log[0] == QStringLiteral("A") && log[1] == QStringLiteral("B")
                  && log[2] == QStringLiteral("C"),
              "authored order: A, B, C ran in declared sequence");
    }

    // 2. canRun == false ⇒ run() never called; trace records ran=false + the stage's
    //    skipReason.
    {
        bool called = false;
        SessionProfile profile;
        profile.stages.push_back(
            std::make_unique<NeverRunStage>(QStringLiteral("Never"), QStringLiteral("no capability"), called));

        AnalysisContext ctx{ caps, job, nullptr };
        runStages(profile, ctx);

        check(!called, "canRun=false: run() not invoked");
        check(ctx.trace.size() == 1 && !ctx.trace[0].ran
                  && ctx.trace[0].skipReason == QStringLiteral("no capability"),
              "canRun=false: trace has ran=false + the stage's skipReason");
    }

    // 3. ctx.halted ⇒ every subsequent stage is skipped with reason "halted", run() never
    //    called, even for a stage whose own canRun() would return true.
    {
        bool called1 = false, called2 = false;
        SessionProfile profile;
        profile.stages.push_back(std::make_unique<HaltStage>());
        profile.stages.push_back(std::make_unique<WouldRunStage>(QStringLiteral("W1"), called1));
        profile.stages.push_back(std::make_unique<WouldRunStage>(QStringLiteral("W2"), called2));

        AnalysisContext ctx{ caps, job, nullptr };
        runStages(profile, ctx);

        check(ctx.halted, "Halt stage set ctx.halted");
        check(ctx.trace.size() == 3, "halted: still one trace entry per stage");
        check(ctx.trace[0].ran, "halted: the Halt stage itself ran (it's the one that set it)");
        check(!called1 && !called2, "halted: downstream run() never invoked");
        check(!ctx.trace[1].ran && ctx.trace[1].skipReason == QStringLiteral("halted")
                  && !ctx.trace[2].ran && ctx.trace[2].skipReason == QStringLiteral("halted"),
              "halted: downstream trace entries are ran=false, skipReason=\"halted\"");
    }

    // 4. Product gating: a downstream stage's canRun() reads a slot an upstream stage
    //    wrote. Same pair, run with and without the upstream stage in the profile.
    {
        std::vector<QString> log;
        SessionProfile withUpstream;
        withUpstream.stages.push_back(std::make_unique<SetSegImuStage>());
        withUpstream.stages.push_back(std::make_unique<NeedsSegImuStage>(log));

        AnalysisContext ctx{ caps, job, nullptr };
        runStages(withUpstream, ctx);

        check(log.size() == 1 && log[0] == QStringLiteral("NeedsSegImu"),
              "product gating: downstream ran after the upstream stage populated segImu");
        check(ctx.trace[1].ran, "product gating: downstream trace entry is ran=true");
    }
    {
        std::vector<QString> log;
        SessionProfile withoutUpstream;
        withoutUpstream.stages.push_back(std::make_unique<NeedsSegImuStage>(log));

        AnalysisContext ctx{ caps, job, nullptr };   // fresh context — segImu unset
        runStages(withoutUpstream, ctx);

        check(log.empty(), "product gating: downstream skipped with no segImu producer upstream");
        check(!ctx.trace[0].ran && ctx.trace[0].skipReason == QStringLiteral("no segImu"),
              "product gating: skip trace carries the downstream stage's own skipReason");
    }

    // 5. Trace shape: one entry per stage, names match, ran entries have elapsedNs >= 0.
    {
        std::vector<QString> log;
        SessionProfile profile;
        profile.stages.push_back(std::make_unique<RecordingStage>(QStringLiteral("X"), log));
        profile.stages.push_back(std::make_unique<RecordingStage>(QStringLiteral("Y"), log));

        AnalysisContext ctx{ caps, job, nullptr };
        runStages(profile, ctx);

        check(ctx.trace.size() == profile.stages.size(), "trace: one entry per stage");
        bool namesMatch = true, elapsedOk = true;
        for (size_t i = 0; i < ctx.trace.size(); ++i) {
            if (ctx.trace[i].name != profile.stages[i]->name()) namesMatch = false;
            if (ctx.trace[i].ran && ctx.trace[i].elapsedNs < 0) elapsedOk = false;
        }
        check(namesMatch, "trace: entry names match their stage's name()");
        check(elapsedOk, "trace: ran entries have elapsedNs >= 0");
    }

    std::printf("--- CaptureCapabilities ---\n");

    // 6a. fromJob on a default job: no cameras, no IMUs.
    {
        const ShotAnalysisJob defJob;
        const CaptureCapabilities c = CaptureCapabilities::fromJob(defJob);
        check(!c.hasCamera(CameraPlacement::FaceOn), "fromJob(default): hasCamera(FaceOn) false");
        check(!c.hasCamera(CameraPlacement::DownTheLine), "fromJob(default): hasCamera(DownTheLine) false");
        check(c.imus.empty(), "fromJob(default): imus empty");
    }

    // 6b. fromJob with one face-on camera source.
    {
        ShotAnalysisJob camJob;
        camJob.faceOnCameraCount = 1;
        camJob.cameraSources.push_back(1);
        const CaptureCapabilities c = CaptureCapabilities::fromJob(camJob);
        check(c.hasCamera(CameraPlacement::FaceOn), "fromJob(faceOnCameraCount=1): hasCamera(FaceOn) true");
        check(!c.hasCamera(CameraPlacement::DownTheLine),
              "fromJob: DownTheLine is never populated (reserved, not yet consumed)");
    }

    // 6c. fromJob threads imuBindings 1:1 into CaptureCapabilities::BoundImu.
    {
        ShotAnalysisJob imuJob;
        ImuSegmentBinding b;
        b.role = SegmentRole::LeadForearm;
        b.calibrated = true;
        b.calibAgeSec = 2.5;
        imuJob.imuBindings.push_back(b);
        const CaptureCapabilities c = CaptureCapabilities::fromJob(imuJob);
        check(c.imus.size() == 1, "fromJob: one BoundImu per imuBindings entry");
        check(c.imus[0].role == SegmentRole::LeadForearm && c.imus[0].calibValid
                  && c.imus[0].calibAgeSec == 2.5,
              "fromJob: BoundImu carries role/calibValid/calibAgeSec through");
    }

    // 6d. hasRole/hasRoles over a hand-built imus vector.
    {
        CaptureCapabilities c;
        c.imus.push_back(CaptureCapabilities::BoundImu{ SegmentRole::Pelvis, true, 1.0 });
        c.imus.push_back(CaptureCapabilities::BoundImu{ SegmentRole::Thorax, false, -1.0 });

        check(c.hasRole(SegmentRole::Pelvis), "hasRole: present role found");
        check(!c.hasRole(SegmentRole::Club), "hasRole: absent role not found");
        check(c.hasRoles({ SegmentRole::Pelvis, SegmentRole::Thorax }), "hasRoles: both present -> true");
        check(!c.hasRoles({ SegmentRole::Pelvis, SegmentRole::Club }), "hasRoles: one absent -> false");
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
