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

#include "pose_backend.h"
#include "pp_debug.h"

#include <QtGlobal>
#include <QFile>

#include <algorithm>

#if defined(Q_OS_WIN)
#  include <QLibrary>
#endif

namespace pinpoint::pose {

namespace {

// The NVIDIA driver probe, the one copy (the three estimators and this file each had it).
bool nvidiaDriverPresent()
{
#if defined(Q_OS_LINUX)
    // ORT's only GPU EP on Linux is CUDA; the NVIDIA driver publishes this
    // procfs node, so its presence means an NVIDIA GPU + driver are installed.
    return QFile::exists(QStringLiteral("/proc/driver/nvidia/version"));
#elif defined(Q_OS_WIN)
    // nvcuda.dll is installed by the NVIDIA driver; if it loads, a GPU is present
    // (same probe CudaRuntimeController uses).
    QLibrary nvcuda(QStringLiteral("nvcuda.dll"));
    const bool hasNv = nvcuda.load();
    if (hasNv)
        nvcuda.unload();
    return hasNv;
#else
    return false;
#endif
}

std::string tensorrtShape(int batch, int h, int w)
{
    return "input_0:" + std::to_string(batch) + "x3x" + std::to_string(h) + "x" + std::to_string(w);
}

} // namespace

EpPlatform currentEpPlatform()
{
    // The WITH_* defines are set by CMake when the matching GPU ORT binary is downloaded.
    // WITH_COREML is defined only on Apple-Silicon macOS and WITH_CUDA never on Apple, so the
    // probe (read before any provider is tried) runs exactly where the estimators ran it.
    EpPlatform p;
#ifdef WITH_COREML
    p.coreml = true;
#endif
#ifdef WITH_CUDA
    p.cuda = true;
    p.nvidiaDriver = nvidiaDriverPresent();
#endif
#ifdef WITH_DIRECTML
    p.directml = true;
#endif
    return p;
}

EpModelOptions moveNetEpOptions()
{
    EpModelOptions m;
    m.logTag = "[MoveNet]";
    return m;
}

EpModelOptions mediaPipeEpOptions(const char *logTag)
{
    EpModelOptions m;
    m.logTag  = logTag;
    m.wording = EpWording::Short;
    m.summary = EpSummary::CpuOnlyLine;
    return m;
}

EpModelOptions vitPoseEpOptions()
{
    EpModelOptions m;
    m.logTag = "[ViTPose]";
    return m;
}

std::vector<EpStep> planExecutionProviders(const EpPlatform &platform, const EpModelOptions &model)
{
    std::vector<EpStep> plan;
    if (platform.coreml) {
        if (model.coremlProgram) {
            // MLProgram is the format CoreML compiles for the GPU/ANE, fp16-capable (ViTPose, pose.coreml).
            EpStep s;
            s.provider = EpProvider::CoreMLProgram;
            s.label    = QStringLiteral("CoreML-MLProgram");
            s.options  = { { "ModelFormat", "MLProgram" },
                           { "MLComputeUnits", "ALL" },
                           { "RequireStaticInputShapes", "0" },
                           { "EnableOnSubgraphs", "0" },
                           { "ModelCacheDirectory", model.coremlCacheDir.toStdString() } };
            plan.push_back(std::move(s));
        }
        EpStep s;
        s.provider    = EpProvider::CoreML;
        s.label       = QStringLiteral("CoreML");
        s.coremlFlags = 0;   // COREML_FLAG_USE_NONE
        plan.push_back(std::move(s));
    }
    if (platform.cuda && platform.nvidiaDriver) {
        if (model.tensorrt) {
            // pose.tensorrt — UNTESTED (no nvinfer on the studio). Appended AHEAD of CUDA: TensorRT
            // takes the subgraphs it can build, CUDA the rest. fp16 engines, cached; the batch dim gets
            // one optimisation profile [1 … batch] so a partial batch needs no rebuild.
            const std::string shp    = tensorrtShape(1, model.inputH, model.inputW);
            const std::string shpMax = tensorrtShape(std::max(1, model.tensorrtMaxBatch), model.inputH, model.inputW);
            EpStep s;
            s.provider = EpProvider::TensorRT;
            s.options  = { { "device_id", "0" },
                           { "trt_fp16_enable", "1" },
                           { "trt_engine_cache_enable", "1" },
                           { "trt_engine_cache_path", model.tensorrtCacheDir.toStdString() },
                           { "trt_profile_min_shapes", shp },
                           { "trt_profile_opt_shapes", shpMax },
                           { "trt_profile_max_shapes", shpMax } };
            plan.push_back(std::move(s));
        }
        EpStep s;
        s.provider     = EpProvider::CUDA;
        s.label        = QStringLiteral("CUDA");
        s.cudaDeviceId = 0;
        plan.push_back(std::move(s));
    }
    if (platform.directml) {
        // DirectML runs on any Direct3D 12 adapter.
        EpStep s;
        s.provider = EpProvider::DirectML;
        s.label    = QStringLiteral("DirectML");
        plan.push_back(std::move(s));
    }
    return plan;   // empty: CPU only
}

// The log lines, word for word as each estimator wrote them. A QString operand stays a QString
// (QDebug quotes it), a literal stays a const char * (unquoted).
void logEpAppended(const EpStep &step, const EpModelOptions &model)
{
    const char *tag = model.logTag;
    const bool  shortWords = model.wording == EpWording::Short;
    switch (step.provider) {
    case EpProvider::CoreMLProgram:
        ppInfo() << tag << "CoreML execution provider active (MLProgram, all compute units,"
                 << "cache" << model.coremlCacheDir << ")";
        return;
    case EpProvider::TensorRT:
        ppInfo() << tag << "TensorRT execution provider appended (fp16, engine cache"
                 << model.tensorrtCacheDir << ")";
        return;
    case EpProvider::CoreML:
        if (shortWords) ppInfo() << tag << "CoreML EP active";
        else            ppInfo() << tag << "CoreML execution provider active";
        return;
    case EpProvider::CUDA:
        if (shortWords) ppInfo() << tag << "CUDA EP active";
        else            ppInfo() << tag << "CUDA execution provider active";
        return;
    case EpProvider::DirectML:
        if (shortWords) ppInfo() << tag << "DirectML EP active";
        else            ppInfo() << tag << "DirectML execution provider active";
        return;
    }
}

void logEpUnavailable(const EpStep &step, const EpModelOptions &model, const char *what)
{
    const char *tag = model.logTag;
    const char *name = "";
    switch (step.provider) {
    case EpProvider::TensorRT:
        ppWarn() << tag << "TensorRT unavailable:" << what << "— CUDA only";
        return;
    case EpProvider::CoreMLProgram: name = "CoreML MLProgram unavailable:"; break;
    case EpProvider::CoreML:        name = "CoreML unavailable:";           break;
    case EpProvider::CUDA:          name = "CUDA unavailable:";             break;
    case EpProvider::DirectML:      name = "DirectML unavailable:";         break;
    }
    if (model.wording == EpWording::Short) ppInfo() << tag << name << what;
    else                                   ppInfo() << tag << name << what << "— falling back";
}

void logEpSummary(const QString &label, const EpModelOptions &model)
{
    switch (model.summary) {
    case EpSummary::UsingLine:
        if (label.isEmpty()) ppInfo() << model.logTag << "No GPU EP available — using CPU";
        else                 ppInfo() << model.logTag << "Using" << label << "execution provider";
        return;
    case EpSummary::CpuOnlyLine:
        if (label.isEmpty()) ppInfo() << model.logTag << "CPU-only";
        return;
    case EpSummary::None:
        return;
    }
}

QString bestAvailableAcceleratedBackend()
{
    // The estimators' order, by presence only: CoreML is always there where it is compiled in
    // (Apple Silicon), CUDA when the driver probe passes, DirectML when compiled in.
    const std::vector<EpStep> plan = planExecutionProviders(currentEpPlatform(), EpModelOptions{});
    return plan.empty() ? QString() : plan.front().label;
}

} // namespace pinpoint::pose
