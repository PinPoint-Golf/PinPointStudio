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

// The pose estimators' one execution-provider cascade (pose_backend.h; platform refactor A7):
// cascade parity, exact. No ONNX Runtime: the append is a fake that records each provider call
// and throws on the calls a scenario says fail.
//
// The reference is the three cascades as they were before A7 (pose_estimator_movenet.cpp,
// pose_estimator_mediapipe.cpp's applyEpCascade, pose_estimator_vitpose.cpp's appendProviders and
// its "Using" line, at 7bacad21), transcribed below with their ORT calls replaced by the same
// recorder and their log lines spelled out as QDebug printed them (a QString quoted, a literal
// not). For every estimator x platform {CoreML, CUDA, CUDA compiled with no driver, DirectML,
// CPU only} x failures {none, the first provider, every provider, and the second only}, the new
// cascade must make the same provider calls with the same options, in the same order, give the
// same label, and log the same lines at the same severity. The real ppInfo/ppWarn path is used
// (pp_log_stream.cpp into PpMessageLog), so the quoting is the real one.

#include "../pose_backend.h"
#include "../../Core/PpMessageLog.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace pinpoint::pose;

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const QString &label)
{
    if (ok) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::printf("  [FAIL] %s\n", qPrintable(label));
}

// ── the fake ORT: records each call, throws on the scenario's failing call numbers ─────────────────
struct FakeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Recorder {
    std::set<int> failing;     // 1-based call numbers that throw; {-1} = every call
    QStringList   calls;
    void call(const QString &what)
    {
        calls << what;
        const int n = int(calls.size());
        if (failing.count(-1) || failing.count(n))
            throw FakeError("boom");
    }
};

static QString kv(const std::vector<std::pair<std::string, std::string>> &o)
{
    QStringList parts;
    for (const auto &p : o)
        parts << QString::fromStdString(p.first) + QLatin1Char('=') + QString::fromStdString(p.second);
    return parts.join(QLatin1Char(','));
}

// The new cascade's step, described the way the reference describes its call.
static QString describe(const EpStep &s)
{
    switch (s.provider) {
    case EpProvider::CoreMLProgram: return QStringLiteral("CoreML(map) ") + kv(s.options);
    case EpProvider::CoreML:        return QStringLiteral("CoreML(flags) %1").arg(s.coremlFlags);
    case EpProvider::TensorRT:      return QStringLiteral("TensorRT_V2 ") + kv(s.options);
    case EpProvider::CUDA:          return QStringLiteral("CUDA(struct) device_id=%1").arg(s.cudaDeviceId);
    case EpProvider::DirectML:      return QStringLiteral("DML");
    }
    return QStringLiteral("?");
}

// ── the reference: the three cascades before A7 ─────────────────────────────────────────────────────
struct Line {
    QString severity, message;
    bool operator==(const Line &o) const { return severity == o.severity && message == o.message; }
};
struct Outcome {
    QStringList       calls;
    QString           label;
    std::vector<Line> lines;
};

static const QString kDash = QString::fromUtf8("—");   // the em dash the lines carry

static void info(Outcome &o, const QString &m) { o.lines.push_back({ QStringLiteral("INFO"), m }); }
static void warn(Outcome &o, const QString &m) { o.lines.push_back({ QStringLiteral("WARN"), m }); }

// MoveNet::load()'s cascade and its Using line (pose_estimator_movenet.cpp:127-184 before A7).
static Outcome oldMoveNet(const EpPlatform &p, Recorder &r)
{
    Outcome o;
    QString &epLabel = o.label;
    if (p.coreml && epLabel.isEmpty()) {
        try { r.call(QStringLiteral("CoreML(flags) 0")); epLabel = QStringLiteral("CoreML");
              info(o, QStringLiteral("[MoveNet] CoreML execution provider active")); }
        catch (const FakeError &e) { info(o, QStringLiteral("[MoveNet] CoreML unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
    }
    if (p.cuda && epLabel.isEmpty()) {
        const bool hasNv = p.nvidiaDriver;
        if (hasNv) {
            try { r.call(QStringLiteral("CUDA(struct) device_id=0")); epLabel = QStringLiteral("CUDA");
                  info(o, QStringLiteral("[MoveNet] CUDA execution provider active")); }
            catch (const FakeError &e) { info(o, QStringLiteral("[MoveNet] CUDA unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
        }
    }
    if (p.directml && epLabel.isEmpty()) {
        try { r.call(QStringLiteral("DML")); epLabel = QStringLiteral("DirectML");
              info(o, QStringLiteral("[MoveNet] DirectML execution provider active")); }
        catch (const FakeError &e) { info(o, QStringLiteral("[MoveNet] DirectML unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
    }
    if (epLabel.isEmpty())
        info(o, QStringLiteral("[MoveNet] No GPU EP available %1 using CPU").arg(kDash));
    else
        info(o, QStringLiteral("[MoveNet] Using \"%1\" execution provider").arg(epLabel));
    o.calls = r.calls;
    return o;
}

// MediaPipe's applyEpCascade(opts, logPrefix) (pose_estimator_mediapipe.cpp:143-198 before A7).
static Outcome oldMediaPipe(const EpPlatform &p, Recorder &r, const QString &logPrefix)
{
    Outcome o;
    QString &label = o.label;
    if (p.coreml && label.isEmpty()) {
        try { r.call(QStringLiteral("CoreML(flags) 0")); label = QStringLiteral("CoreML");
              info(o, logPrefix + QStringLiteral(" CoreML EP active")); }
        catch (const FakeError &e) { info(o, logPrefix + QStringLiteral(" CoreML unavailable: %1").arg(QString::fromUtf8(e.what()))); }
    }
    if (p.cuda && label.isEmpty()) {
        const bool hasNv = p.nvidiaDriver;
        if (hasNv) {
            try { r.call(QStringLiteral("CUDA(struct) device_id=0")); label = QStringLiteral("CUDA");
                  info(o, logPrefix + QStringLiteral(" CUDA EP active")); }
            catch (const FakeError &e) { info(o, logPrefix + QStringLiteral(" CUDA unavailable: %1").arg(QString::fromUtf8(e.what()))); }
        }
    }
    if (p.directml && label.isEmpty()) {
        try { r.call(QStringLiteral("DML")); label = QStringLiteral("DirectML");
              info(o, logPrefix + QStringLiteral(" DirectML EP active")); }
        catch (const FakeError &e) { info(o, logPrefix + QStringLiteral(" DirectML unavailable: %1").arg(QString::fromUtf8(e.what()))); }
    }
    if (label.isEmpty())
        info(o, logPrefix + QStringLiteral(" CPU-only"));
    o.calls = r.calls;
    return o;
}

struct VitOpts {
    bool    coremlProgram = false;
    QString coremlCacheDir;
    bool    tensorrt = false;
    QString trtCacheDir;
    int     maxBatch = 1;
};

// ViTPose's appendProviders(opts, withTrt) and, on the first pass, load()'s Using line
// (pose_estimator_vitpose.cpp:262-416 before A7; the retry at :441 has no Using line).
static Outcome oldVitPose(const EpPlatform &p, Recorder &r, const VitOpts &lo, bool withTrt, bool usingLine)
{
    Outcome o;
    QString &epLabel = o.label;
    if (p.coreml) {
        if (epLabel.isEmpty() && lo.coremlProgram) {
            try { r.call(QStringLiteral("CoreML(map) ModelFormat=MLProgram,MLComputeUnits=ALL,RequireStaticInputShapes=0,"
                                        "EnableOnSubgraphs=0,ModelCacheDirectory=") + lo.coremlCacheDir);
                  epLabel = QStringLiteral("CoreML-MLProgram");
                  info(o, QStringLiteral("[ViTPose] CoreML execution provider active (MLProgram, all compute units, cache \"%1\" )")
                              .arg(lo.coremlCacheDir)); }
            catch (const FakeError &e) { info(o, QStringLiteral("[ViTPose] CoreML MLProgram unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
        }
        if (epLabel.isEmpty()) {
            try { r.call(QStringLiteral("CoreML(flags) 0")); epLabel = QStringLiteral("CoreML");
                  info(o, QStringLiteral("[ViTPose] CoreML execution provider active")); }
            catch (const FakeError &e) { info(o, QStringLiteral("[ViTPose] CoreML unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
        }
    }
    if (p.cuda && epLabel.isEmpty()) {
        const bool hasNv = p.nvidiaDriver;
        QString trtLabel;
        if (hasNv && withTrt) {
            const QString shp    = QStringLiteral("input_0:1x3x256x192");
            const QString shpMax = QStringLiteral("input_0:%1x3x256x192").arg(std::max(1, lo.maxBatch));
            try { r.call(QStringLiteral("TensorRT_V2 device_id=0,trt_fp16_enable=1,trt_engine_cache_enable=1,"
                                        "trt_engine_cache_path=%1,trt_profile_min_shapes=%2,"
                                        "trt_profile_opt_shapes=%3,trt_profile_max_shapes=%3")
                             .arg(lo.trtCacheDir, shp, shpMax));
                  trtLabel = QStringLiteral("TensorRT+");
                  info(o, QStringLiteral("[ViTPose] TensorRT execution provider appended (fp16, engine cache \"%1\" )").arg(lo.trtCacheDir)); }
            catch (const FakeError &e) { warn(o, QStringLiteral("[ViTPose] TensorRT unavailable: %1 %2 CUDA only").arg(QString::fromUtf8(e.what()), kDash)); }
        }
        if (hasNv) {
            try { r.call(QStringLiteral("CUDA(struct) device_id=0")); epLabel = trtLabel + QStringLiteral("CUDA");
                  info(o, QStringLiteral("[ViTPose] CUDA execution provider active")); }
            catch (const FakeError &e) { info(o, QStringLiteral("[ViTPose] CUDA unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
        }
    }
    if (p.directml && epLabel.isEmpty()) {
        try { r.call(QStringLiteral("DML")); epLabel = QStringLiteral("DirectML");
              info(o, QStringLiteral("[ViTPose] DirectML execution provider active")); }
        catch (const FakeError &e) { info(o, QStringLiteral("[ViTPose] DirectML unavailable: %1 %2 falling back").arg(QString::fromUtf8(e.what()), kDash)); }
    }
    if (usingLine) {
        if (epLabel.isEmpty())
            info(o, QStringLiteral("[ViTPose] No GPU EP available %1 using CPU").arg(kDash));
        else
            info(o, QStringLiteral("[ViTPose] Using \"%1\" execution provider").arg(epLabel));
    }
    o.calls = r.calls;
    return o;
}

// ── the new cascade, through the real log ──────────────────────────────────────────────────────────
static Outcome newCascade(const EpPlatform &p, Recorder &r, const EpModelOptions &m)
{
    int seq = PpMessageLog::instance()->currentSeq();
    Outcome o;
    o.label = runExecutionProviders<FakeError>(planExecutionProviders(p, m), m,
                                               [&r](const EpStep &s) { r.call(describe(s)); });
    o.calls = r.calls;
    for (const PpMessageLog::Entry &e : PpMessageLog::instance()->fetchSince(seq))
        o.lines.push_back({ e.severity, e.message });
    return o;
}

static QString show(const Outcome &o)
{
    QStringList l;
    l << QStringLiteral("label '%1'").arg(o.label);
    for (const QString &c : o.calls) l << QStringLiteral("call  ") + c;
    for (const Line &x : o.lines) l << x.severity + QStringLiteral("  ") + x.message;
    return l.join(QStringLiteral("\n          "));
}

static void same(const Outcome &want, const Outcome &got, const QString &what)
{
    const bool ok = want.label == got.label && want.calls == got.calls && want.lines == got.lines;
    check(ok, what);
    if (!ok)
        std::printf("        before A7:\n          %s\n        after:\n          %s\n", qPrintable(show(want)), qPrintable(show(got)));
}

int main()
{
    std::printf("=== pose_ep_cascade_test (one EP cascade, parity with the three before A7) ===\n");

    struct Plat { const char *name; EpPlatform p; };
    const std::vector<Plat> platforms = {
        { "CoreML",              { true,  false, false, false } },
        { "CUDA",                { false, true,  false, true  } },
        { "CUDA, no driver",     { false, true,  false, false } },
        { "DirectML",            { false, false, true,  false } },
        { "CPU only",            { false, false, false, false } },
        // Never built (CoreML is Apple-only, CUDA never Apple), but the order must hold anyway.
        { "every EP",            { true,  true,  true,  true  } },
    };
    struct Fail { const char *name; std::set<int> calls; };
    const std::vector<Fail> failures = {
        { "none fail",          {} },
        { "the first fails",    { 1 } },
        { "the second fails",   { 2 } },
        { "every one fails",    { -1 } },
    };

    const QString mlDir  = QStringLiteral("/cache/coreml/vitpose-b-wholebody-360242862-b8");
    const QString trtDir = QStringLiteral("/cache/tensorrt");

    for (const Plat &pl : platforms) {
        for (const Fail &f : failures) {
            const QString at = QStringLiteral("%1, %2").arg(QLatin1String(pl.name), QLatin1String(f.name));

            { Recorder a{ f.calls }, b{ f.calls };
              same(oldMoveNet(pl.p, a), newCascade(pl.p, b, moveNetEpOptions()), QStringLiteral("MoveNet: ") + at); }

            for (const char *tag : { "[MediaPipe/det]", "[MediaPipe/lm]" }) {
                Recorder a{ f.calls }, b{ f.calls };
                same(oldMediaPipe(pl.p, a, QLatin1String(tag)), newCascade(pl.p, b, mediaPipeEpOptions(tag)),
                     QStringLiteral("MediaPipe %1: ").arg(QLatin1String(tag)) + at);
            }

            // ViTPose: today's defaults per platform (nn or MLProgram, no TensorRT), TensorRT on, and the retry.
            for (int v = 0; v < 4; ++v) {
                VitOpts lo;
                lo.coremlProgram  = (v == 1 || v == 3);
                lo.coremlCacheDir = mlDir;
                lo.tensorrt       = (v >= 2);
                lo.trtCacheDir    = trtDir;
                lo.maxBatch       = 8;
                const bool retry  = (v == 3);
                EpModelOptions m = vitPoseEpOptions();
                // The estimator sets these only where they apply (pose_estimator_vitpose.cpp).
                if (pl.p.coreml && lo.coremlProgram) { m.coremlProgram = true; m.coremlCacheDir = mlDir; }
                if (pl.p.cuda && pl.p.nvidiaDriver && lo.tensorrt) {
                    m.tensorrt = true; m.tensorrtCacheDir = trtDir; m.tensorrtMaxBatch = 8; m.inputH = 256; m.inputW = 192;
                }
                if (retry) { m.tensorrt = false; m.summary = EpSummary::None; }
                Recorder a{ f.calls }, b{ f.calls };
                static const char *names[] = { "nn", "MLProgram", "TensorRT", "TensorRT retry, MLProgram" };
                same(oldVitPose(pl.p, a, lo, /*withTrt*/ lo.tensorrt && !retry, /*usingLine*/ !retry),
                     newCascade(pl.p, b, m), QStringLiteral("ViTPose %1: ").arg(QLatin1String(names[v])) + at);
            }
        }
    }

    // The plans themselves, spelled out (the inventory, NOTES-A7 §1).
    {
        EpModelOptions m = vitPoseEpOptions();
        m.coremlProgram = true; m.coremlCacheDir = mlDir;
        const auto plan = planExecutionProviders({ true, false, false, false }, m);
        check(plan.size() == 2 && plan[0].provider == EpProvider::CoreMLProgram && plan[0].label == QLatin1String("CoreML-MLProgram")
                  && plan[0].options.size() == 5 && plan[0].options[4].second == mlDir.toStdString()
                  && plan[1].provider == EpProvider::CoreML && plan[1].coremlFlags == 0 && plan[1].label == QLatin1String("CoreML"),
              QStringLiteral("Mac ViTPose MLProgram plan: CoreML MLProgram (5 options), then CoreML flags 0"));
        EpModelOptions t = vitPoseEpOptions();
        t.tensorrt = true; t.tensorrtCacheDir = trtDir; t.tensorrtMaxBatch = 0; t.inputH = 256; t.inputW = 192;
        const auto tp = planExecutionProviders({ false, true, false, true }, t);
        check(tp.size() == 2 && tp[0].provider == EpProvider::TensorRT && tp[0].options.size() == 7
                  && tp[0].options[5].second == "input_0:1x3x256x192" && tp[0].options[6].second == "input_0:1x3x256x192"
                  && tp[1].provider == EpProvider::CUDA && tp[1].cudaDeviceId == 0,
              QStringLiteral("TensorRT's max batch is at least 1; CUDA follows with device 0"));
        check(planExecutionProviders({ false, true, false, false }, t).empty(),
              QStringLiteral("CUDA compiled in with no NVIDIA driver plans nothing (CPU), TensorRT included"));
        check(planExecutionProviders({ false, false, false, false }, moveNetEpOptions()).empty(),
              QStringLiteral("CPU only plans nothing"));
    }

    // bestAvailableAcceleratedBackend's rule before A7: CoreML where compiled in, else CUDA when the
    // driver is there, else DirectML where compiled in, else "" — the first step of the default plan.
    for (const Plat &pl : platforms) {
        const QString want = pl.p.coreml ? QStringLiteral("CoreML")
                           : (pl.p.cuda && pl.p.nvidiaDriver) ? QStringLiteral("CUDA")
                           : pl.p.directml ? QStringLiteral("DirectML") : QString();
        const auto plan = planExecutionProviders(pl.p, EpModelOptions{});
        check((plan.empty() ? QString() : plan.front().label) == want,
              QStringLiteral("best available backend on %1 is '%2'").arg(QLatin1String(pl.name), want));
    }
    {
        const auto plan = planExecutionProviders(currentEpPlatform(), EpModelOptions{});
        check(bestAvailableAcceleratedBackend() == (plan.empty() ? QString() : plan.front().label),
              QStringLiteral("bestAvailableAcceleratedBackend() is this build's default plan's first label"));
    }

    // The Mac's real line, as the pre-A7 swinglab_run printed it on 24 Nov 2026 (NOTES-A7 §3a).
    {
        EpModelOptions m = vitPoseEpOptions();
        m.coremlProgram = true;
        m.coremlCacheDir = QStringLiteral("/Users/x/Library/Application Support/PinPointStudio/cache/coreml/vitpose-b-wholebody-360242862-b8");
        Recorder r;
        const Outcome o = newCascade({ true, false, false, false }, r, m);
        check(o.lines.size() == 2
                  && o.lines[0].message == QStringLiteral("[ViTPose] CoreML execution provider active (MLProgram, all compute units, cache "
                                                          "\"/Users/x/Library/Application Support/PinPointStudio/cache/coreml/vitpose-b-wholebody-360242862-b8\" )")
                  && o.lines[1].message == QStringLiteral("[ViTPose] Using \"CoreML-MLProgram\" execution provider"),
              QStringLiteral("the Mac's two ViTPose lines read as they did"));
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    std::printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
