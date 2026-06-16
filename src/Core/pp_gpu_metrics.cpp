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

#include "pp_gpu_metrics.h"

#include <mutex>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <dxgi1_4.h>      // IDXGIFactory1, IDXGIAdapter3, DXGI_QUERY_VIDEO_MEMORY_INFO
#elif defined(__APPLE__)
// Metal backend lives in pp_gpu_metrics_mac.mm (ObjC++); declared below.
#else  // Linux / other POSIX
#  include <dlfcn.h>
#  include <unistd.h>       // getpid()
#endif

namespace pinpoint::gpumetrics {
namespace {

std::mutex g_mutex;
bool       g_initAttempted = false;
bool       g_available     = false;
std::string g_backend;
std::string g_deviceName;
bool       g_unified  = false;
uint64_t   g_peakProc = 0;

// ── Platform backends ─────────────────────────────────────────────────────────
// Each backend exposes two operations to the dispatch layer below, all called
// under g_mutex:  backendInit() resolves availability + static device facts, and
// backendQuery() fills the volatile numbers (device used + this-process used).

#if defined(_WIN32)
// ── DXGI (Windows, any vendor) ────────────────────────────────────────────────
IDXGIFactory1 *g_factory = nullptr;
IDXGIAdapter3 *g_adapter = nullptr;
uint64_t       g_deviceTotal = 0;

bool backendInit()
{
    if (CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&g_factory)) != S_OK
        || !g_factory)
        return false;

    // Pick the hardware adapter with the most dedicated VRAM (the discrete GPU).
    IDXGIAdapter1 *best = nullptr;
    SIZE_T         bestMem = 0;
    DXGI_ADAPTER_DESC1 bestDesc{};
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1 *ad = nullptr;
        if (g_factory->EnumAdapters1(i, &ad) != S_OK)
            break;
        DXGI_ADAPTER_DESC1 d{};
        if (ad->GetDesc1(&d) == S_OK
            && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            && d.DedicatedVideoMemory >= bestMem) {
            if (best) best->Release();
            best    = ad;
            bestMem = d.DedicatedVideoMemory;
            bestDesc = d;
            continue;
        }
        ad->Release();
    }
    if (!best)
        return false;

    const bool gotV3 = best->QueryInterface(__uuidof(IDXGIAdapter3),
                                            reinterpret_cast<void **>(&g_adapter)) == S_OK;
    best->Release();
    if (!gotV3 || !g_adapter)
        return false;

    g_deviceTotal = uint64_t(bestDesc.DedicatedVideoMemory);
    char name[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, bestDesc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
    g_deviceName = name;
    g_unified    = false;
    g_backend    = "DXGI";
    return true;
}

void backendQuery(uint64_t &deviceTotal, uint64_t &deviceUsed, uint64_t &procUsed)
{
    deviceTotal = g_deviceTotal;
    deviceUsed  = 0;   // DXGI per-process query has no device-wide-used figure
    procUsed    = 0;
    if (!g_adapter)
        return;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (g_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info) == S_OK)
        procUsed = uint64_t(info.CurrentUsage);   // this process, local (VRAM) segment
}

void backendShutdown()
{
    if (g_adapter) { g_adapter->Release(); g_adapter = nullptr; }
    if (g_factory) { g_factory->Release(); g_factory = nullptr; }
}

#elif defined(__APPLE__)
// ── Metal (macOS) — implemented in pp_gpu_metrics_mac.mm ──────────────────────
} // namespace
namespace detail {
bool     macInit(std::string &name, uint64_t &total, bool &unified);
uint64_t macProcessBytes();
} // namespace detail
namespace {

uint64_t g_deviceTotal = 0;

bool backendInit()
{
    if (!detail::macInit(g_deviceName, g_deviceTotal, g_unified))
        return false;
    g_backend = "Metal";
    return true;
}

void backendQuery(uint64_t &deviceTotal, uint64_t &deviceUsed, uint64_t &procUsed)
{
    deviceTotal = g_deviceTotal;
    deviceUsed  = 0;                          // no device-wide figure from MTLDevice
    procUsed    = detail::macProcessBytes();  // currentAllocatedSize
}

void backendShutdown() {}

#else
// ── NVML (Linux, NVIDIA) — dlopen, no link/header dependency ──────────────────
// Minimal self-declared NVML ABI (stable across driver versions).
extern "C" {
typedef int  ppNvmlReturn;                      // NVML_SUCCESS == 0
typedef void *ppNvmlDevice;                     // opaque handle
struct ppNvmlMemory { unsigned long long total, free, used; };
// v2/v3 process-info layout (forward-compatible superset of the original).
struct ppNvmlProcessInfo {
    unsigned int       pid;
    unsigned long long usedGpuMemory;
    unsigned int       gpuInstanceId;
    unsigned int       computeInstanceId;
};
}

using FnInit          = ppNvmlReturn (*)();
using FnShutdown      = ppNvmlReturn (*)();
using FnGetHandle     = ppNvmlReturn (*)(unsigned int, ppNvmlDevice *);
using FnGetMemory     = ppNvmlReturn (*)(ppNvmlDevice, ppNvmlMemory *);
using FnGetName       = ppNvmlReturn (*)(ppNvmlDevice, char *, unsigned int);
using FnGetProcs      = ppNvmlReturn (*)(ppNvmlDevice, unsigned int *, ppNvmlProcessInfo *);

void        *g_lib       = nullptr;
FnShutdown   g_nvShutdown = nullptr;
FnGetMemory  g_nvMemory   = nullptr;
FnGetProcs   g_nvProcs    = nullptr;
ppNvmlDevice g_device     = nullptr;
unsigned int g_pid        = 0;

template <class Fn>
Fn loadSym(void *lib, const char *name) { return reinterpret_cast<Fn>(dlsym(lib, name)); }

bool backendInit()
{
    g_lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_lib)
        g_lib = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_lib)
        return false;   // no NVIDIA driver present — expected on most dev boxes

    auto nvInit   = loadSym<FnInit>(g_lib, "nvmlInit_v2");
    g_nvShutdown  = loadSym<FnShutdown>(g_lib, "nvmlShutdown");
    auto nvHandle = loadSym<FnGetHandle>(g_lib, "nvmlDeviceGetHandleByIndex_v2");
    g_nvMemory    = loadSym<FnGetMemory>(g_lib, "nvmlDeviceGetMemoryInfo");
    auto nvName   = loadSym<FnGetName>(g_lib, "nvmlDeviceGetName");
    // Prefer the newest per-process API; fall back across driver generations.
    g_nvProcs     = loadSym<FnGetProcs>(g_lib, "nvmlDeviceGetComputeRunningProcesses_v3");
    if (!g_nvProcs) g_nvProcs = loadSym<FnGetProcs>(g_lib, "nvmlDeviceGetComputeRunningProcesses_v2");
    if (!g_nvProcs) g_nvProcs = loadSym<FnGetProcs>(g_lib, "nvmlDeviceGetComputeRunningProcesses");

    if (!nvInit || !nvHandle || !g_nvMemory || nvInit() != 0) {
        dlclose(g_lib); g_lib = nullptr;   // NVML never came up — just drop the handle
        return false;
    }
    if (nvHandle(0, &g_device) != 0 || !g_device) {
        if (g_nvShutdown) g_nvShutdown();  // init succeeded — unwind it before dlclose
        dlclose(g_lib); g_lib = nullptr;
        return false;
    }

    char name[96] = {0};
    if (nvName && nvName(g_device, name, sizeof(name)) == 0)
        g_deviceName = name;
    g_pid     = static_cast<unsigned int>(getpid());
    g_unified = false;
    g_backend = "NVML";
    return true;
}

void backendQuery(uint64_t &deviceTotal, uint64_t &deviceUsed, uint64_t &procUsed)
{
    deviceTotal = deviceUsed = procUsed = 0;
    if (!g_device)
        return;

    ppNvmlMemory mem{};
    if (g_nvMemory && g_nvMemory(g_device, &mem) == 0) {
        deviceTotal = mem.total;
        deviceUsed  = mem.used;
    }
    if (g_nvProcs) {
        // Two-call pattern: first ask for the count, then fetch into a buffer.
        unsigned int count = 0;
        g_nvProcs(g_device, &count, nullptr);          // NVML_ERROR_INSUFFICIENT_SIZE sets count
        if (count > 0) {
            std::vector<ppNvmlProcessInfo> infos(count);
            if (g_nvProcs(g_device, &count, infos.data()) == 0) {
                for (unsigned int i = 0; i < count; ++i)
                    if (infos[i].pid == g_pid)
                        procUsed += infos[i].usedGpuMemory;   // our compute contexts
            }
        }
    }
}

void backendShutdown()
{
    if (!g_lib)
        return;
    if (g_nvShutdown) g_nvShutdown();
    dlclose(g_lib);
    g_lib = nullptr;
    g_device = nullptr;
}
#endif

// ── Shared dispatch (all under g_mutex) ───────────────────────────────────────

// Resolve the backend once.  Caller holds g_mutex.
bool ensureInitLocked()
{
    if (g_initAttempted)
        return g_available;
    g_initAttempted = true;
    g_available     = backendInit();
    return g_available;
}

void bumpPeakLocked(uint64_t proc)
{
    if (proc > g_peakProc)
        g_peakProc = proc;
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

bool init()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    return ensureInitLocked();
}

GpuSample sample()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    GpuSample s;
    if (!ensureInitLocked())
        return s;   // available = false, all zero

    backendQuery(s.deviceTotalBytes, s.deviceUsedBytes, s.processUsedBytes);
    bumpPeakLocked(s.processUsedBytes);

    s.available        = true;
    s.unified          = g_unified;
    s.backend          = g_backend;
    s.deviceName       = g_deviceName;
    s.peakProcessBytes = g_peakProc;
    return s;
}

uint64_t processGpuBytes()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!ensureInitLocked())
        return 0;
    uint64_t total = 0, used = 0, proc = 0;
    backendQuery(total, used, proc);
    bumpPeakLocked(proc);
    return proc;
}

void reset()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_peakProc = 0;
}

void shutdown()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_initAttempted && g_available)
        backendShutdown();
    g_initAttempted = false;
    g_available     = false;
}

} // namespace pinpoint::gpumetrics
