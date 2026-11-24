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

// The ONNX Runtime half of the pose estimators' one execution-provider cascade (pose_backend.h has
// the plan and the walk): appends one planned provider to an Ort::SessionOptions, through the same
// ORT entry point each estimator used, so each provider keeps its defaults. Header-only, included by
// the estimators alone (they already include ORT); the plan and the walk need no ORT.

#include "pose_backend.h"

#include <onnxruntime_cxx_api.h>

#ifdef WITH_COREML
#  include <coreml_provider_factory.h>
#endif

#include <string>
#include <unordered_map>
#include <vector>

namespace pinpoint::pose {

// Appends one step; throws Ort::Exception when the provider cannot be appended (the walk then
// logs it and tries the next). A step for a provider this build has not compiled in throws too
// (currentEpPlatform() never plans one).
inline void appendExecutionProvider(Ort::SessionOptions &opts, const EpStep &step)
{
    switch (step.provider) {
    case EpProvider::CoreMLProgram: {
#ifdef WITH_COREML
        const std::unordered_map<std::string, std::string> o(step.options.begin(), step.options.end());
        opts.AppendExecutionProvider("CoreML", o);
        return;
#else
        break;
#endif
    }
    case EpProvider::CoreML:
#ifdef WITH_COREML
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(opts, step.coremlFlags));
        return;
#else
        break;
#endif
    case EpProvider::TensorRT: {
#ifdef WITH_CUDA
        const OrtApi &api = Ort::GetApi();
        OrtTensorRTProviderOptionsV2 *trt = nullptr;
        try {
            Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&trt));
            std::vector<const char *> keys, vals;
            for (const auto &kv : step.options) {
                keys.push_back(kv.first.c_str());
                vals.push_back(kv.second.c_str());
            }
            Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(trt, keys.data(), vals.data(), keys.size()));
            opts.AppendExecutionProvider_TensorRT_V2(*trt);
        } catch (...) {
            if (trt)
                api.ReleaseTensorRTProviderOptions(trt);
            throw;
        }
        api.ReleaseTensorRTProviderOptions(trt);
        return;
#else
        break;
#endif
    }
    case EpProvider::CUDA: {
#ifdef WITH_CUDA
        OrtCUDAProviderOptions cuda{};
        cuda.device_id = step.cudaDeviceId;
        opts.AppendExecutionProvider_CUDA(cuda);
        return;
#else
        break;
#endif
    }
    case EpProvider::DirectML:
#ifdef WITH_DIRECTML
        opts.AppendExecutionProvider("DML");
        return;
#else
        break;
#endif
    }
    throw Ort::Exception("execution provider not compiled into this build", ORT_NOT_IMPLEMENTED);
}

// The cascade on one SessionOptions: this platform's plan for the model, walked. Returns the label
// ("" = CPU).
inline QString applyExecutionProviders(Ort::SessionOptions &opts, const EpPlatform &platform,
                                       const EpModelOptions &model)
{
    return runExecutionProviders<Ort::Exception>(
        planExecutionProviders(platform, model), model,
        [&opts](const EpStep &step) { appendExecutionProvider(opts, step); });
}

inline QString applyExecutionProviders(Ort::SessionOptions &opts, const EpModelOptions &model)
{
    return applyExecutionProviders(opts, currentEpPlatform(), model);
}

} // namespace pinpoint::pose
