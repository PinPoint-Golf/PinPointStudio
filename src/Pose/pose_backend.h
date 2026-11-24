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

#pragma once

#include <QString>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The ONE ONNX Runtime execution-provider cascade of the pose estimators (ViTPose, MoveNet, MediaPipe;
// platform refactor A7). Three parts, so the choice is testable without an Ort::Session:
//
//   currentEpPlatform()         what this build and host offer: the WITH_COREML / WITH_CUDA / WITH_DIRECTML
//                               compile flags and the NVIDIA driver probe (nvcuda.dll loads on Windows,
//                               /proc/driver/nvidia/version on Linux). Read only when CUDA is compiled in.
//   planExecutionProviders()    PURE: given that platform and an estimator's EpModelOptions, the providers to
//                               try, in order, with their options. CoreML [MLProgram, then the flag call] ->
//                               CUDA [TensorRT appended ahead of it] -> DirectML -> CPU (no step).
//   runExecutionProviders()     walks a plan: each step is tried only while no provider has been chosen; a
//                               throw is that provider failing, logged, and the walk goes on. TensorRT is
//                               additive: its success only makes CUDA's label "TensorRT+CUDA". Then the
//                               estimator's summary line. The append is injected (pose_ep_ort.h appends to
//                               Ort::SessionOptions; the test fakes it), so is the exception type.
//
// The estimators' differences are behaviour, kept as EpModelOptions (never unified: a change here changes the
// pose and so the analysis): MediaPipe's short wording ("CUDA EP active", no "— falling back", "CPU-only", no
// summary line); ViTPose's MLProgram and TensorRT steps, and its TensorRT retry, which logs no summary.
// Every streamed log operand keeps the type it had in the estimators (a QString is quoted by QDebug, a
// const char * is not), so each ppInfo line reads as before. pose_ep_cascade_test proves the plans and
// the lines. Session options (threads, graph optimisation) stay with each estimator: they are not providers.

namespace pinpoint::pose {

// What the build and the host offer.
struct EpPlatform {
    bool coreml       = false;   // WITH_COREML (macOS arm64)
    bool cuda         = false;   // WITH_CUDA (Windows, Linux)
    bool directml     = false;   // WITH_DIRECTML (never defined by CMake today)
    bool nvidiaDriver = false;   // the NVIDIA probe; CUDA is tried only when it passes
};
EpPlatform currentEpPlatform();

enum class EpProvider { CoreMLProgram, CoreML, TensorRT, CUDA, DirectML };

// How an estimator words its EP lines.
//   Long  (MoveNet, ViTPose): "<tag> CUDA execution provider active" / "<tag> CUDA unavailable: <what> — falling back"
//   Short (MediaPipe):        "<tag> CUDA EP active" / "<tag> CUDA unavailable: <what>"
enum class EpWording { Long, Short };
// The line after the walk.
//   UsingLine   "<tag> No GPU EP available — using CPU" / "<tag> Using "<label>" execution provider"
//   CpuOnlyLine "<tag> CPU-only", only when no provider was chosen (MediaPipe)
//   None        nothing (ViTPose's TensorRT retry)
enum class EpSummary { UsingLine, CpuOnlyLine, None };

// One estimator's cascade parameters.
struct EpModelOptions {
    const char *logTag  = "";                    // "[ViTPose]", "[MoveNet]", "[MediaPipe/det]", …
    EpWording   wording = EpWording::Long;
    EpSummary   summary = EpSummary::UsingLine;
    // ViTPose, pose.coreml mlprogram: CoreML MLProgram first, compiled into this cache directory (the
    // estimator makes it and copies the model there first: file work stays with its owner).
    bool    coremlProgram = false;
    QString coremlCacheDir;
    // ViTPose, pose.tensorrt: TensorRT appended ahead of CUDA (fp16, engines cached here; the estimator
    // makes the folder), one optimisation profile of input_0 [1 … maxBatch] x 3 x inputH x inputW.
    bool    tensorrt         = false;
    QString tensorrtCacheDir;
    int     tensorrtMaxBatch = 1;
    int     inputH           = 0;
    int     inputW           = 0;
};
EpModelOptions moveNetEpOptions();                          // [MoveNet], Long, UsingLine
EpModelOptions mediaPipeEpOptions(const char *logTag);      // [MediaPipe/det] / [MediaPipe/lm], Short, CpuOnlyLine
EpModelOptions vitPoseEpOptions();                          // [ViTPose], Long, UsingLine; the caller sets MLProgram / TensorRT

// One provider to try.
struct EpStep {
    EpProvider provider = EpProvider::CoreML;
    QString    label;            // the label it gives when it is appended ("" for TensorRT: see above)
    // CoreMLProgram: the provider options of AppendExecutionProvider("CoreML", …);
    // TensorRT: UpdateTensorRTProviderOptions' keys and values, in that order.
    std::vector<std::pair<std::string, std::string>> options;
    uint32_t   coremlFlags  = 0; // CoreML (NeuralNetwork): OrtSessionOptionsAppendExecutionProvider_CoreML's flags
                                 // (0 = COREML_FLAG_USE_NONE)
    int        cudaDeviceId = 0; // CUDA: OrtCUDAProviderOptions{} with this device_id, every other field its default
};

std::vector<EpStep> planExecutionProviders(const EpPlatform &platform, const EpModelOptions &model);

// The walk's log lines (pose_backend.cpp), one wording per EpModelOptions.
void logEpAppended(const EpStep &step, const EpModelOptions &model);
void logEpUnavailable(const EpStep &step, const EpModelOptions &model, const char *what);
void logEpSummary(const QString &label, const EpModelOptions &model);

// Walks the plan; returns the chosen label ("" = CPU). append(step) appends one provider and throws
// Exception when it cannot.
template <class Exception, class Append>
QString runExecutionProviders(const std::vector<EpStep> &plan, const EpModelOptions &model, Append &&append)
{
    QString label;
    QString tensorrtPrefix;
    for (const EpStep &step : plan) {
        if (!label.isEmpty())
            break;
        try {
            append(step);
            if (step.provider == EpProvider::TensorRT)
                tensorrtPrefix = QStringLiteral("TensorRT+");
            else
                label = (step.provider == EpProvider::CUDA ? tensorrtPrefix : QString()) + step.label;
            logEpAppended(step, model);
        } catch (const Exception &e) {
            logEpUnavailable(step, model, e.what());
        }
    }
    logEpSummary(label, model);
    return label;
}

// The accelerated backend ORT would pick, by presence only, with no estimator options and no
// Ort::Session: the first step of the default plan. "" (CPU only) | "CoreML" | "CUDA" | "DirectML".
// MotionCaptureProbe asks it.
QString bestAvailableAcceleratedBackend();

// Convenience: any non-empty backend string is GPU-accelerated.
inline bool isAccelerated(const QString &b) { return !b.isEmpty(); }

} // namespace pinpoint::pose
