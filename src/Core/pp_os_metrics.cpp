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

#include "pp_os_metrics.h"

#include <chrono>
#include <mutex>

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>          // PROCESS_MEMORY_COUNTERS; K32GetProcessMemoryInfo is a
                              // kernel32 export, so no psapi.lib link is required.
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach/task.h>
#  include <mach/thread_act.h>
#  include <pthread.h>
#  include <ctime>
#else  // Linux / other POSIX
#  include <cstdio>
#  include <ctime>
#  include <pthread.h>
#  include <unistd.h>
#endif

namespace pinpoint::osmetrics {
namespace {

// Monotonic wall clock in nanoseconds — the denominator for every CPU%.
uint64_t wallNowNs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ── Process CPU time (ns) and RSS (bytes), per platform ───────────────────────

uint64_t processCpuNowNs()
{
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return 0;
    auto to_ns = [](const FILETIME &ft) -> uint64_t {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return v.QuadPart * 100ull;   // 100-ns units → ns
    };
    return to_ns(k) + to_ns(u);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0)
        return 0;
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

uint64_t processRssBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return uint64_t(pmc.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return uint64_t(info.resident_size);
    return 0;
#else
    // /proc/self/statm: "<size> <resident> <shared> ..." in pages.
    uint64_t rssPages = 0;
    if (FILE *f = fopen("/proc/self/statm", "r")) {
        unsigned long total = 0, resident = 0;
        if (fscanf(f, "%lu %lu", &total, &resident) == 2)
            rssPages = resident;
        fclose(f);
    }
    static const long pageSize = sysconf(_SC_PAGESIZE);
    return rssPages * uint64_t(pageSize > 0 ? pageSize : 4096);
#endif
}

// ── Per-thread CPU time (ns) for an already-registered thread ─────────────────

#if defined(_WIN32)
using ThreadHandle = HANDLE;                 // from OpenThread(THREAD_QUERY_INFORMATION, …)
#elif defined(__APPLE__)
using ThreadHandle = mach_port_t;            // from pthread_mach_thread_np()
#else
using ThreadHandle = pthread_t;              // for pthread_getcpuclockid()
#endif

uint64_t threadCpuNowNs(ThreadHandle h)
{
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u))
        return 0;
    auto to_ns = [](const FILETIME &ft) -> uint64_t {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return v.QuadPart * 100ull;
    };
    return to_ns(k) + to_ns(u);
#elif defined(__APPLE__)
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(h, THREAD_BASIC_INFO,
                    reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    auto tv_ns = [](const time_value_t &t) -> uint64_t {
        return uint64_t(t.seconds) * 1000000000ull + uint64_t(t.microseconds) * 1000ull;
    };
    return tv_ns(info.user_time) + tv_ns(info.system_time);
#else
    clockid_t cid;
    if (pthread_getcpuclockid(h, &cid) != 0)
        return 0;
    struct timespec ts;
    if (clock_gettime(cid, &ts) != 0)
        return 0;
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

// ── Shared sampler state ──────────────────────────────────────────────────────

struct ThreadEntry {
    uint64_t     tid  = 0;
    std::string  name;
    ThreadHandle handle{};
    uint64_t     prevCpuNs = 0;
    bool         alive     = true;
};

std::mutex             g_mutex;
std::vector<ThreadEntry> g_threads;

// Process delta baselines + watermarks.  prevWallNs == 0 means "unseeded".
uint64_t g_procPrevCpuNs   = 0;
uint64_t g_procPrevWallNs  = 0;
uint64_t g_procPeakRss     = 0;
double   g_procPeakCpuPct  = 0.0;

// Thread-table delta baseline (single wall reference shared by all entries).
uint64_t g_threadsPrevWallNs = 0;

uint64_t currentTid()
{
#if defined(_WIN32)
    return uint64_t(GetCurrentThreadId());
#elif defined(__APPLE__)
    uint64_t t = 0;
    pthread_threadid_np(nullptr, &t);
    return t;
#else
    return uint64_t(pthread_self());
#endif
}

ThreadHandle currentThreadHandle()
{
#if defined(_WIN32)
    // Real handle (not the pseudo-handle) so another thread can query it later.
    return OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
#elif defined(__APPLE__)
    return pthread_mach_thread_np(pthread_self());   // no extra ref to release
#else
    return pthread_self();
#endif
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

ProcessSample sampleProcess()
{
    const uint64_t cpuNs  = processCpuNowNs();
    const uint64_t wallNs = wallNowNs();

    ProcessSample s;
    s.rss_bytes = processRssBytes();

    std::lock_guard<std::mutex> lk(g_mutex);

    if (g_procPrevWallNs != 0) {
        const uint64_t dWall = wallNs - g_procPrevWallNs;
        const uint64_t dCpu  = cpuNs  - g_procPrevCpuNs;
        if (dWall > 0)
            s.cpu_percent = 100.0 * double(dCpu) / double(dWall);
    }
    g_procPrevCpuNs  = cpuNs;
    g_procPrevWallNs = wallNs;

    if (s.rss_bytes > g_procPeakRss)    g_procPeakRss   = s.rss_bytes;
    if (s.cpu_percent > g_procPeakCpuPct) g_procPeakCpuPct = s.cpu_percent;
    s.peak_rss_bytes   = g_procPeakRss;
    s.peak_cpu_percent = g_procPeakCpuPct;
    return s;
}

uint64_t threadCpuNowNs()
{
#if defined(_WIN32)
    return threadCpuNowNs(GetCurrentThread());   // pseudo-handle is fine for self
#elif defined(__APPLE__)
    mach_port_t self = pthread_mach_thread_np(pthread_self());
    return threadCpuNowNs(self);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return 0;
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

void registerThread(const std::string &name)
{
    ThreadEntry e;
    e.tid       = currentTid();
    e.name      = name;
    e.handle    = currentThreadHandle();
    e.prevCpuNs = threadCpuNowNs(e.handle);
    e.alive     = true;

    std::lock_guard<std::mutex> lk(g_mutex);
    // Replace a stale entry for the same tid (thread ids are recycled by the OS).
    for (auto &existing : g_threads) {
        if (existing.tid == e.tid) {
#if defined(_WIN32)
            if (existing.handle) CloseHandle(existing.handle);
#endif
            existing = e;
            return;
        }
    }
    g_threads.push_back(e);
}

void unregisterThread()
{
    const uint64_t tid = currentTid();
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto &e : g_threads) {
        if (e.tid == tid) {
            e.alive = false;
#if defined(_WIN32)
            if (e.handle) { CloseHandle(e.handle); e.handle = nullptr; }
#endif
        }
    }
}

std::vector<ThreadSample> sampleThreads()
{
    const uint64_t wallNs = wallNowNs();

    std::lock_guard<std::mutex> lk(g_mutex);

    uint64_t dWall = 0;
    if (g_threadsPrevWallNs != 0 && wallNs > g_threadsPrevWallNs)
        dWall = wallNs - g_threadsPrevWallNs;
    g_threadsPrevWallNs = wallNs;

    std::vector<ThreadSample> out;
    out.reserve(g_threads.size());
    for (auto &e : g_threads) {
        if (!e.alive)
            continue;
        const uint64_t cpuNs = threadCpuNowNs(e.handle);
        ThreadSample ts;
        ts.tid  = e.tid;
        ts.name = e.name;
        if (dWall > 0 && cpuNs >= e.prevCpuNs)
            ts.cpu_percent = 100.0 * double(cpuNs - e.prevCpuNs) / double(dWall);
        e.prevCpuNs = cpuNs;
        out.push_back(std::move(ts));
    }
    return out;
}

void reset()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_procPrevCpuNs    = 0;
    g_procPrevWallNs   = 0;   // unseed → next sample reports 0% and re-baselines
    g_procPeakRss      = 0;
    g_procPeakCpuPct   = 0.0;
    g_threadsPrevWallNs = 0;
    for (auto &e : g_threads) {
        if (!e.alive)
            continue;   // dead thread's handle dangles (see sampleThreads) — querying it segfaults
        e.prevCpuNs = threadCpuNowNs(e.handle);
    }
}

} // namespace pinpoint::osmetrics
