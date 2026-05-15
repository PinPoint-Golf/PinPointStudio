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

#pragma once

#if defined(__linux__) && !defined(__ANDROID__)
  #define PINPOINT_PLATFORM_LINUX   1
#elif defined(__ANDROID__)
  #define PINPOINT_PLATFORM_ANDROID 1
#elif defined(_WIN32)
  #define PINPOINT_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    #define PINPOINT_PLATFORM_IOS   1
  #else
    #define PINPOINT_PLATFORM_MACOS 1
  #endif
#endif

#if defined(PINPOINT_PLATFORM_MACOS) || defined(PINPOINT_PLATFORM_IOS)
  #define PINPOINT_PLATFORM_APPLE   1
#endif

#if defined(_MSC_VER)
  #include <intrin.h>
  #define PINPOINT_CPU_PAUSE() _mm_pause()
#elif defined(__x86_64__) || defined(__i386__)
  #define PINPOINT_CPU_PAUSE() __builtin_ia32_pause()
#elif defined(__arm__) || defined(__aarch64__)
  #define PINPOINT_CPU_PAUSE() __asm__ __volatile__("yield")
#else
  #define PINPOINT_CPU_PAUSE() ((void)0)
#endif
