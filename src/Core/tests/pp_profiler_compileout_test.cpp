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

// Built with PINPOINT_PROFILE_BASELINE=0 PINPOINT_PROFILE=0 (see CMakeLists): every
// macro must expand to ((void)0) — no interning, no records, zero footprint.

#include "pp_profiler.h"

#include <gtest/gtest.h>

#if PINPOINT_PROFILE_BASELINE || PINPOINT_PROFILE
#  error "compile-out test must be built with both profiler tiers OFF"
#endif

namespace {

void useEveryMacro()
{
    PP_PROFILE_SCOPE("X.scope");
    PP_PROFILE_COUNT("X.count");
    PP_PROFILE_MEM_ADD("X.mem", 100);
    PP_PROFILE_MEM_SUB("X.mem", 50);
    PP_PROFILE_MEM_SCOPE("X.ms", 10);
    PP_PROFILE_SCOPE_DEEP("X.deep");
    PP_PROFILE_COUNT_DEEP("X.dcount");
}

} // namespace

TEST(CompileOut, MacrosLeaveNoRecords)
{
    useEveryMacro();
    useEveryMacro();

    const auto snap = pinpoint::profiling::Profiler::instance().snapshot();
    EXPECT_TRUE(snap.scopes.empty());
    EXPECT_TRUE(snap.memory.empty());
}
