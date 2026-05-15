/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "thread_policy.h"

#if defined(PINPOINT_PLATFORM_LINUX)
  #include <pthread.h>
  #include <sched.h>
  #include <sys/resource.h>
#elif defined(PINPOINT_PLATFORM_ANDROID)
  #include <sys/resource.h>
#elif defined(PINPOINT_PLATFORM_APPLE)
  #include <pthread.h>
  #include <pthread/qos.h>
#elif defined(PINPOINT_PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace pinpoint {

static thread_local const char* s_last_description = "not called";

const char* ThreadPolicy::lastApplyDescription() noexcept {
    return s_last_description;
}

// ---------------------------------------------------------------------------
// Linux
// ---------------------------------------------------------------------------
#if defined(PINPOINT_PLATFORM_LINUX)

bool ThreadPolicy::apply(ThreadRole role) noexcept {
    if (role == ThreadRole::Capture) {
        struct sched_param param{};
        param.sched_priority = 50;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0) {
            s_last_description = "SCHED_FIFO p=50";
            return true;
        }
        if (setpriority(PRIO_PROCESS, 0, -10) == 0) {
            s_last_description = "nice -10";
            return true;
        }
    } else if (role == ThreadRole::Merger) {
        if (setpriority(PRIO_PROCESS, 0, -5) == 0) {
            s_last_description = "nice -5";
            return true;
        }
    }
    s_last_description = "default (no elevation)";
    return false;
}

bool ThreadPolicy::pinToCore(int core_id) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

// ---------------------------------------------------------------------------
// Android
// ---------------------------------------------------------------------------
#elif defined(PINPOINT_PLATFORM_ANDROID)

bool ThreadPolicy::apply(ThreadRole role) noexcept {
    int prio = (role == ThreadRole::Capture) ? -19
             : (role == ThreadRole::Merger)  ? -8
             : 0;
    if (prio == 0) {
        s_last_description = "default";
        return false;
    }
    bool ok = setpriority(PRIO_PROCESS, 0, prio) == 0;
    s_last_description = ok
        ? ((prio == -19) ? "setpriority(-19)" : "setpriority(-8)")
        : "default (setpriority failed)";
    return ok;
}

bool ThreadPolicy::pinToCore(int /*core_id*/) noexcept {
    return false;
}

// ---------------------------------------------------------------------------
// Apple (macOS / iOS)
// ---------------------------------------------------------------------------
#elif defined(PINPOINT_PLATFORM_APPLE)

bool ThreadPolicy::apply(ThreadRole role) noexcept {
    qos_class_t qos = (role == ThreadRole::Capture)
                    ? QOS_CLASS_USER_INTERACTIVE
                    : (role == ThreadRole::Merger)
                    ? QOS_CLASS_USER_INITIATED
                    : QOS_CLASS_DEFAULT;
    int ret = pthread_set_qos_class_self_np(qos, 0);
    if (ret == 0) {
        s_last_description = (role == ThreadRole::Capture)
            ? "QOS_CLASS_USER_INTERACTIVE"
            : (role == ThreadRole::Merger)
            ? "QOS_CLASS_USER_INITIATED"
            : "QOS_CLASS_DEFAULT";
        return (qos != QOS_CLASS_DEFAULT);
    }
    s_last_description = "default (pthread_set_qos_class_self_np failed)";
    return false;
}

bool ThreadPolicy::pinToCore(int /*core_id*/) noexcept {
    return false;
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
#elif defined(PINPOINT_PLATFORM_WINDOWS)

bool ThreadPolicy::apply(ThreadRole role) noexcept {
    int priority = (role == ThreadRole::Capture)
                 ? THREAD_PRIORITY_TIME_CRITICAL
                 : (role == ThreadRole::Merger)
                 ? THREAD_PRIORITY_ABOVE_NORMAL
                 : THREAD_PRIORITY_NORMAL;
    bool ok = SetThreadPriority(GetCurrentThread(), priority) != 0;
    s_last_description = ok
        ? ((role == ThreadRole::Capture)
           ? "THREAD_PRIORITY_TIME_CRITICAL"
           : (role == ThreadRole::Merger)
           ? "THREAD_PRIORITY_ABOVE_NORMAL"
           : "THREAD_PRIORITY_NORMAL")
        : "default (SetThreadPriority failed)";
    return ok && (priority != THREAD_PRIORITY_NORMAL);
}

bool ThreadPolicy::pinToCore(int core_id) noexcept {
    DWORD_PTR mask = static_cast<DWORD_PTR>(1) << core_id;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
}

// ---------------------------------------------------------------------------
// Fallback (unknown platform)
// ---------------------------------------------------------------------------
#else

bool ThreadPolicy::apply(ThreadRole /*role*/) noexcept {
    s_last_description = "default (platform not supported)";
    return false;
}

bool ThreadPolicy::pinToCore(int /*core_id*/) noexcept {
    return false;
}

#endif

} // namespace pinpoint
