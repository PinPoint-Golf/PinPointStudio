# regrade_ledger_new_needs_ini_test (refactor 3.10, review B1; re-worded in refactor 4-c, J-61): `regrade_ledger --engine
# new` must never pool golfers. Each session's athlete comes from its athlete folder's athlete.json (the library, read
# through AthleteRepository), and --athletes-ini (a copy of the app's settings) is only the fallback for a library that
# has not been migrated. A session whose athlete NEITHER names is refused — SKIP, exit non-zero, nothing written —
# because the new engine freezes each session's club baseline from its athlete's earlier sessions and, without a record,
# would count every session of one folder as one golfer. The old engine stays the default (checked with --help).
#
#   cmake -DTOOL=<regrade_ledger> -DWORK=<an empty scratch folder> -P regrade_ledger_needs_ini_check.cmake
#
# The PINPOINT_CORE_* seams come from the test's ENVIRONMENT (the per-session refusal is after the tool's content check).
# Cases 3 and 4 RUN the new engine on a session with one empty swing: they check only that it was not refused (whether
# the empty swing grades is not this test's question); WORK is a scratch folder in the build tree, deleted at the end.
if(NOT TOOL OR NOT WORK)
  message(FATAL_ERROR "TOOL and WORK must be set")
endif()
file(REMOVE_RECURSE "${WORK}")
set(_one "${WORK}/lib/Golfer-One/2026-01-01_Golfer-One_Swing_01")
set(_two "${WORK}/lib/Golfer-Two/2026-01-01_Golfer-Two_Swing_01")
foreach(_s IN ITEMS "${_one}" "${_two}")
  file(MAKE_DIRECTORY "${_s}/swing_0001")
  file(WRITE "${_s}/swing_0001/swing.json" "{}")
endforeach()
file(GLOB_RECURSE _before RELATIVE "${WORK}" "${WORK}/*")

# 1. Neither source: no athlete.json, no --athletes-ini → refused, nothing written.
execute_process(COMMAND "${TOOL}" --engine new "${_one}"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_rc EQUAL 0)
  message(FATAL_ERROR "FAIL: --engine new on a session no source names exited 0:\n${_out}${_err}")
endif()
if(NOT _out MATCHES "SKIP +no athlete.json in Golfer-One names its athlete, and no --athletes-ini was given: refused, so no golfer is pooled")
  message(FATAL_ERROR "FAIL: the refusal does not say why (exit ${_rc}):\n${_out}${_err}")
endif()
file(GLOB_RECURSE _after RELATIVE "${WORK}" "${WORK}/*")
if(NOT _before STREQUAL _after)
  message(FATAL_ERROR "FAIL: the refused run wrote into the session:\n before ${_before}\n after  ${_after}")
endif()
message(STATUS "PASS: a session neither source names is refused (exit ${_rc}) and nothing is written")

# 2. An --athletes-ini that is not a file is refused before anything is read.
execute_process(COMMAND "${TOOL}" --engine new --athletes-ini "${WORK}/no-such.ini" "${_one}"
                RESULT_VARIABLE _rc2 ERROR_VARIABLE _err2)
if(_rc2 EQUAL 0 OR NOT _err2 MATCHES "is not a file")
  message(FATAL_ERROR "FAIL: a missing --athletes-ini file was not refused (exit ${_rc2}):\n${_err2}")
endif()
message(STATUS "PASS: a missing --athletes-ini file is refused (exit ${_rc2})")

# 3. The library: Golfer-One's athlete.json names the athlete → not refused, read from athlete.json.
file(WRITE "${WORK}/lib/Golfer-One/athlete.json"
     "{\"schema\":\"pinpoint.athlete/1\",\"athleteId\":\"6f1c2b9e-3d4a-4c5b-9e8f-0a1b2c3d4e5f\","
     "\"record\":{\"name\":\"Golfer One\",\"sex\":\"female\",\"dob\":\"1990-05-01\"}}")
execute_process(COMMAND "${TOOL}" --engine new --tag check "${_one}"
                RESULT_VARIABLE _rc3 OUTPUT_VARIABLE _out3 ERROR_VARIABLE _err3)
if(_out3 MATCHES "refused, so no golfer is pooled")
  message(FATAL_ERROR "FAIL: a session whose athlete.json names its athlete was refused:\n${_out3}${_err3}")
endif()
if(_out3 MATCHES "2026-01-01_Golfer-One_Swing_01 +(REGRADED|GRADED)" AND NOT _out3 MATCHES "athlete from athlete.json")
  message(FATAL_ERROR "FAIL: the session graded but not as athlete.json's athlete:\n${_out3}${_err3}")
endif()
message(STATUS "PASS: a session whose athlete folder has an athlete.json is not refused (exit ${_rc3})")

# 4. The fallback: no athlete.json for Golfer-Two, an ini that names "Golfer Two" → not refused, read from the ini.
file(WRITE "${WORK}/athletes.ini"
     "[athletes]\n7a2d3c4b-5e6f-4a1b-8c9d-0e1f2a3b4c5d\\name=Golfer Two\n7a2d3c4b-5e6f-4a1b-8c9d-0e1f2a3b4c5d\\sex=male\n")
execute_process(COMMAND "${TOOL}" --engine new --tag check --athletes-ini "${WORK}/athletes.ini" "${_two}"
                RESULT_VARIABLE _rc4 OUTPUT_VARIABLE _out4 ERROR_VARIABLE _err4)
if(_out4 MATCHES "refused, so no golfer is pooled")
  message(FATAL_ERROR "FAIL: a session the ini names was refused:\n${_out4}${_err4}")
endif()
if(_out4 MATCHES "2026-01-01_Golfer-Two_Swing_01 +(REGRADED|GRADED)" AND NOT _out4 MATCHES "athlete from ini")
  message(FATAL_ERROR "FAIL: the session graded but not as the ini's athlete:\n${_out4}${_err4}")
endif()
message(STATUS "PASS: the ini is the fallback for a folder with no athlete.json (exit ${_rc4})")

execute_process(COMMAND "${TOOL}" --help RESULT_VARIABLE _rc5 OUTPUT_VARIABLE _help)
if(NOT _rc5 EQUAL 0 OR NOT _help MATCHES "old \\(the default\\)")
  message(FATAL_ERROR "FAIL: --help does not name the old engine as the default (exit ${_rc5}):\n${_help}")
endif()
message(STATUS "PASS: the usage names the old engine as the default")
file(REMOVE_RECURSE "${WORK}")
