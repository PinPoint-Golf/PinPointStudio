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
#include <vector>

// Cross-platform process- and thread-level resource sampling for the resource
// profiler.  All numbers are cheap to obtain (a clock read or a single syscall)
// and are intended to be pulled on a low-cadence sampler — never on a hot path.
//
//   Process CPU time : POSIX clock_gettime(CLOCK_PROCESS_CPUTIME_ID)
//                      Windows GetProcessTimes (kernel+user)
//   Process RSS      : Linux  /proc/self/statm   (resident pages × page size)
//                      macOS  mach_task_basic_info.resident_size
//                      Windows K32GetProcessMemoryInfo.WorkingSetSize
//   Thread CPU time  : Linux  pthread_getcpuclockid + clock_gettime
//                      macOS  thread_info(THREAD_BASIC_INFO) user+system
//                      Windows GetThreadTimes (kernel+user)
//
// CPU percentages are computed from the delta of CPU time against wall time
// since the previous sample of the same kind, so they reflect the interval
// between calls.  Values can exceed 100 % (work spread across cores) — that is
// deliberate and useful.  State is global; drive each family from one thread.

namespace pinpoint::osmetrics {

struct ProcessSample {
    uint64_t rss_bytes        = 0;    // current resident set
    uint64_t peak_rss_bytes   = 0;    // high-water since the last reset()
    double   cpu_percent      = 0.0;  // process CPU over the interval since the last sampleProcess()
    double   peak_cpu_percent = 0.0;  // high-water since the last reset()
};

struct ThreadSample {
    uint64_t    tid         = 0;
    std::string name;
    double      cpu_percent = 0.0;    // thread CPU over the interval since the last sampleThreads()
};

// Whole-process RSS + CPU%.  First call after construction/reset() reports
// cpu_percent = 0 (no prior baseline to diff against) and seeds the baseline.
ProcessSample sampleProcess();

// CPU time consumed by the *calling* thread so far, in nanoseconds.  Used by the
// deep-tier scope timer to attribute per-scope CPU.  Cheap on POSIX (vDSO); a
// syscall on Windows with ~15 ms granularity, so deep-tier only.
uint64_t threadCpuNowNs();

// Register the *calling* thread so sampleThreads() can report its CPU%.  Call
// once at a worker's run-loop entry with a stable label ("Pose", "Merge", …).
// Captures the platform-native thread handle needed to query it cross-thread.
void registerThread(const std::string &name);

// Drop the calling thread from the table (optional; safe to omit — a thread that
// exits without unregistering simply reports 0% and can be pruned by the caller).
void unregisterThread();

// Per-registered-thread CPU% over the interval since the previous call.  Threads
// appear in registration order.
std::vector<ThreadSample> sampleThreads();

// Zero the high-water marks and re-seed the CPU% baselines (process + threads).
// Call at session start so the standard profile is per-session.
void reset();

} // namespace pinpoint::osmetrics
