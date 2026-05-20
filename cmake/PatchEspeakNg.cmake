# Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

# Patch espeak-ng 1.52.0 to fix include-path pollution on macOS/Linux.
#
# espeak-include originally exposes include/compat via INTERFACE_INCLUDE_DIRECTORIES.
# That directory shadows system headers (<wctype.h>, <endian.h>) in consumers like
# PinPoint, causing compilation failures.
#
# Strategy:
#   FILE1  src/CMakeLists.txt
#     Strip include/compat from espeak-include's public INTERFACE so it never
#     leaks to consumers.
#
#   FILE2  src/libespeak-ng/CMakeLists.txt
#     1. Keep the espeak-include link as PUBLIC (needed by espeak-ng-bin).
#     2. Re-add include/compat explicitly as PRIVATE to the espeak-ng library
#        itself — espeak-ng needs its own compat shims (endian.h, wctype.h …)
#        at compile time, but those shims must not propagate to consumers.
#
# Pass: -DFILE1=<abs_path_src_CMakeLists> -DFILE2=<abs_path_libespeak-ng_CMakeLists>
#       -DFILE3=<abs_path_top_CMakeLists>

foreach(var FILE1 FILE2 FILE3)
  if(NOT DEFINED "${var}")
    message(FATAL_ERROR "PatchEspeakNg.cmake: ${var} is not set")
  endif()
endforeach()

# --- src/CMakeLists.txt -------------------------------------------------
file(READ "${FILE1}" _c1)
string(REPLACE
    "target_include_directories(espeak-include INTERFACE include include/compat)"
    "target_include_directories(espeak-include INTERFACE include)"
    _c1 "${_c1}")
# On MSVC espeak-ng already adds compat/getopt.c as a source, but the binary
# target has no path to include/compat (we stripped it from the interface
# above), so <getopt.h> cannot be found.  Add it back as PRIVATE here.
string(REPLACE
    "if (MSVC)\n  target_sources(espeak-ng-bin PRIVATE compat/getopt.c)\nendif()"
    "if (MSVC)\n  target_sources(espeak-ng-bin PRIVATE compat/getopt.c)\n  target_include_directories(espeak-ng-bin PRIVATE \${CMAKE_CURRENT_SOURCE_DIR}/include/compat)\nendif()"
    _c1 "${_c1}")
file(WRITE "${FILE1}" "${_c1}")

# --- src/libespeak-ng/CMakeLists.txt ------------------------------------
file(READ "${FILE2}" _c2)

# 1. Restore PUBLIC linkage (a prior workaround had set this to PRIVATE,
#    which broke espeak-ng-bin's access to public headers).
string(REPLACE
    "target_link_libraries(espeak-ng PRIVATE espeak-include)"
    "target_link_libraries(espeak-ng PUBLIC espeak-include)"
    _c2 "${_c2}")

# 2. Add include/compat as PRIVATE so espeak-ng's own sources can still find
#    their compat shims (endian.h, wctype.h, etc.) without exposing them.
string(REPLACE
    "target_include_directories(espeak-ng BEFORE PRIVATE \$<TARGET_PROPERTY:espeak-include,INTERFACE_INCLUDE_DIRECTORIES>)"
    "target_include_directories(espeak-ng BEFORE PRIVATE \$<TARGET_PROPERTY:espeak-include,INTERFACE_INCLUDE_DIRECTORIES> \${CMAKE_CURRENT_SOURCE_DIR}/../include/compat)"
    _c2 "${_c2}")

file(WRITE "${FILE2}" "${_c2}")

# --- top-level CMakeLists.txt -------------------------------------------
# Guard the CPack include so it only fires when espeak-ng is the top-level
# project.  When used as a FetchContent subdirectory, CPack writes
# CPackConfig.cmake to the *parent* build directory and fails on Windows
# with "Permission denied" because the parent CMake run still owns that path.
file(READ "${FILE3}" _c3)
string(REPLACE
    "include(cmake/package.cmake)\ninclude(CPack)"
    "if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)\n  include(cmake/package.cmake)\n  include(CPack)\nendif()"
    _c3 "${_c3}")
file(WRITE "${FILE3}" "${_c3}")
