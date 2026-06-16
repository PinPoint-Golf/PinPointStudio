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

#include <cstdint>
#include <string>

// Cross-platform GPU-memory sampling for the resource profiler — the GPU sibling
// of pp_os_metrics.  One small API, exactly one backend per platform, picked to
// match where ONNX Runtime actually uses the GPU:
//
//   Linux   : NVML (NVIDIA) — dlopen("libnvidia-ml.so.1"); ONNX's only GPU EP on
//             Linux is CUDA, so NVIDIA is the only relevant vendor here.
//   Windows : DXGI (any vendor) — IDXGIAdapter3::QueryVideoMemoryInfo; covers both
//             the DirectML and CUDA execution providers in one vendor-neutral query.
//   macOS   : Metal — MTLDevice currentAllocatedSize / recommendedMaxWorkingSetSize;
//             covers CoreML.  Apple Silicon is unified memory (no discrete VRAM).
//
// Everything is cheap (a couple of driver queries) and meant for a low-cadence
// sampler — never a hot path.  On a host with no usable GPU-memory source (e.g. a
// non-NVIDIA Linux box) init() returns false and every field reads 0/empty; the
// library still loads (NVML is dlopen'd, never linked) so there is no hard
// dependency on a GPU being present.
//
// Thread-safety: init()/sample()/processGpuBytes()/reset() are mutex-guarded.
// sample() runs on the profiler's 1 s sampler thread; processGpuBytes() is also
// called from model-load worker threads (pose / TTS) to bracket a session create.

namespace pinpoint::gpumetrics {

struct GpuSample {
    bool        available        = false;  // a GPU-memory source resolved
    bool        unified          = false;  // macOS unified memory (overlaps RSS)
    std::string backend;                   // "NVML" | "DXGI" | "Metal" | ""
    std::string deviceName;                // best-effort adapter/device name
    uint64_t    deviceTotalBytes = 0;      // device VRAM total / budget (0 if unknown)
    uint64_t    deviceUsedBytes  = 0;      // device-wide used (0 if backend can't report it)
    uint64_t    processUsedBytes = 0;      // THIS process's VRAM — the headline number
    uint64_t    peakProcessBytes = 0;      // high-water of processUsedBytes since reset()
};

// Resolve the platform backend.  Idempotent and thread-safe: the first call does
// the work (dlopen NVML / create the DXGI adapter / open the Metal device), later
// calls return the cached availability.  Returns false when no source is usable.
// sample()/processGpuBytes() call this internally, so explicit init() is optional
// (the controller calls it once at startup to log the resolved backend).
bool init();

// Current GPU-memory picture.  Safe (returns an all-zero, available=false sample)
// when no backend resolved.
GpuSample sample();

// This process's VRAM in bytes only (0 when unavailable) — the lightweight query
// used to bracket an Ort::Session creation for per-subsystem attribution.  Also
// folds into the peak watermark so a load-time spike is not missed between ticks.
uint64_t processGpuBytes();

// Zero the peak watermark.  Wired into Profiler::reset() (session start).
void reset();

// Release the dlopen handle / cached adapter (optional; safe to omit at exit).
void shutdown();

} // namespace pinpoint::gpumetrics
