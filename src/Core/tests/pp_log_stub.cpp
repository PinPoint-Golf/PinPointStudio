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

// Minimal PpLogStream provider for the standalone Core test harness. The real
// implementation (pp_debug.cpp) drags in whisper/ggml/FFmpeg; here we just
// satisfy the linker — pp_profiler.cpp's setDeepEnabled() emits one ppInfo()
// line, which we swallow. Mirrors src/Analysis/tests/imu_test_stubs.cpp.

#include "pp_debug.h"

PpLogStream::PpLogStream(QtMsgType t) : m_type(t) { m_dbg.emplace(&m_buf); }
PpLogStream::~PpLogStream() = default;
