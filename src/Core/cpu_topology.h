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

// physicalCoreCount() — the number of physical CPU cores, a far better proxy for
// sizing an inference thread pool than the historical hardware_concurrency()/2
// (which halves a no-SMT machine, mis-sizes hybrid P/E-core Intels, and — with a
// cap of 8 — starves >16-logical-core boxes). Discovered once and cached in a
// function-local static, so it is cheap to call per model load.
//
// Deliberately Qt-free and dependency-light (std only): src/Core is the lowest
// common layer and this is consumed from both the app and the offline analysis
// tools, and must stay standalone-unit-testable. Header-only so it needs no
// CMake wiring in either the app target or the offline-sources list.

#include <thread>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX          // keep windows.h's min/max macros away from std::clamp
#  endif
#  include <windows.h>
#  include <vector>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#else
#  include <fstream>
#  include <set>
#  include <string>
#endif

namespace pinpoint {

namespace detail {

// The historical proxy: logical cores / 2, floored at 1. Used everywhere the
// platform query is unavailable or returns nothing. (std::max is avoided here so
// windows.h's min/max macros can never interfere.)
inline int hardwareConcurrencyHalf()
{
    const unsigned hw   = std::thread::hardware_concurrency();
    const int      half = static_cast<int>(hw / 2);
    return half > 0 ? half : 1;
}

#if !defined(_WIN32) && !defined(__APPLE__)
// Linux: count distinct hardware-thread-per-core groups. Each online CPU's
// topology/thread_siblings_list names every logical CPU sharing its physical
// core, so SMT siblings share one list and the number of DISTINCT lists is the
// physical-core count. CPUs are enumerated from cpu/present (the canonical online
// set — gaps from offline CPUs are handled). Returns 0 when sysfs topology is
// absent (some containers / exotic kernels) so the caller can fall back.
inline int linuxPhysicalCores()
{
    std::ifstream present("/sys/devices/system/cpu/present");
    if (!present.is_open())
        return 0;
    std::string spec;                     // e.g. "0-11" or "0-3,8-11"
    std::getline(present, spec);

    std::set<std::string> cores;          // distinct sibling-lists ⇒ physical cores
    size_t pos = 0;
    while (pos < spec.size()) {
        // One "lo" or "lo-hi" token from the comma-separated range list.
        const size_t comma = spec.find(',', pos);
        const std::string tok =
            spec.substr(pos, comma == std::string::npos ? comma : comma - pos);
        pos = (comma == std::string::npos) ? spec.size() : comma + 1;
        if (tok.empty())
            continue;
        const size_t dash = tok.find('-');
        long lo = 0, hi = 0;
        try {
            lo = std::stol(tok.substr(0, dash));
            hi = (dash == std::string::npos) ? lo : std::stol(tok.substr(dash + 1));
        } catch (...) {
            continue;                     // malformed token — skip defensively
        }
        for (long cpu = lo; cpu <= hi; ++cpu) {
            std::ifstream sib("/sys/devices/system/cpu/cpu" + std::to_string(cpu)
                              + "/topology/thread_siblings_list");
            if (!sib.is_open())
                continue;
            std::string list;
            std::getline(sib, list);
            if (!list.empty())
                cores.insert(list);
        }
    }
    return static_cast<int>(cores.size());
}
#endif // Linux

} // namespace detail

// Number of physical CPU cores, discovered once and cached. Falls back to the
// legacy hardware_concurrency()/2 proxy whenever the platform query is
// unavailable or yields nothing; never returns < 1.
inline int physicalCoreCount()
{
    static const int kCores = [] {
        int n = 0;
#if defined(_WIN32)
        DWORD len = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
        if (len > 0) {
            std::vector<char> buf(len);
            if (GetLogicalProcessorInformationEx(
                    RelationProcessorCore,
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),
                    &len)) {
                for (DWORD off = 0; off < len; ) {
                    auto *info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        buf.data() + off);
                    if (info->Relationship == RelationProcessorCore)
                        ++n;              // one RelationProcessorCore record per physical core
                    off += info->Size;
                }
            }
        }
#elif defined(__APPLE__)
        int    cores = 0;
        size_t sz    = sizeof(cores);
        if (sysctlbyname("hw.physicalcpu", &cores, &sz, nullptr, 0) == 0)
            n = cores;
#else
        n = detail::linuxPhysicalCores();
#endif
        return n > 0 ? n : detail::hardwareConcurrencyHalf();
    }();
    return kCores;
}

} // namespace pinpoint
