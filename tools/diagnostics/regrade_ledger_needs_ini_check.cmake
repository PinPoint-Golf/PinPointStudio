# regrade_ledger_new_needs_ini_test (refactor 3.10, review B1): `regrade_ledger --engine new` with no --athletes-ini must
# refuse — exit non-zero, say why, and touch nothing — because the new engine freezes each session's club baseline from
# its athlete's earlier sessions and, without the ini, would count every session of one folder as one golfer.
# The old engine stays the default: no --engine still parses (checked with --help, which reads and writes nothing).
#   cmake -DTOOL=<regrade_ledger> -DWORK=<an empty scratch folder> -P regrade_ledger_needs_ini_check.cmake
if(NOT TOOL OR NOT WORK)
  message(FATAL_ERROR "TOOL and WORK must be set")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/Golfer-One/2026-01-01_Golfer-One_Swing_01/swing_0001")
file(WRITE "${WORK}/Golfer-One/2026-01-01_Golfer-One_Swing_01/swing_0001/swing.json" "{}")
file(GLOB_RECURSE _before RELATIVE "${WORK}" "${WORK}/*")

execute_process(COMMAND "${TOOL}" --engine new "${WORK}/Golfer-One/2026-01-01_Golfer-One_Swing_01"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_rc EQUAL 0)
  message(FATAL_ERROR "FAIL: --engine new without --athletes-ini exited 0:\n${_out}${_err}")
endif()
if(NOT _err MATCHES "--engine new needs --athletes-ini")
  message(FATAL_ERROR "FAIL: the refusal does not say why (exit ${_rc}):\n${_out}${_err}")
endif()
file(GLOB_RECURSE _after RELATIVE "${WORK}" "${WORK}/*")
if(NOT _before STREQUAL _after)
  message(FATAL_ERROR "FAIL: the refused run wrote into the session:\n before ${_before}\n after  ${_after}")
endif()
message(STATUS "PASS: --engine new without --athletes-ini refused (exit ${_rc}) and wrote nothing")

execute_process(COMMAND "${TOOL}" --engine new --athletes-ini "${WORK}/no-such.ini"
                        "${WORK}/Golfer-One/2026-01-01_Golfer-One_Swing_01"
                RESULT_VARIABLE _rc2 ERROR_VARIABLE _err2)
if(_rc2 EQUAL 0 OR NOT _err2 MATCHES "is not a file")
  message(FATAL_ERROR "FAIL: a missing --athletes-ini file was not refused (exit ${_rc2}):\n${_err2}")
endif()
message(STATUS "PASS: a missing --athletes-ini file is refused (exit ${_rc2})")

execute_process(COMMAND "${TOOL}" --help RESULT_VARIABLE _rc3 OUTPUT_VARIABLE _help)
if(NOT _rc3 EQUAL 0 OR NOT _help MATCHES "old \\(the default\\)")
  message(FATAL_ERROR "FAIL: --help does not name the old engine as the default (exit ${_rc3}):\n${_help}")
endif()
message(STATUS "PASS: the usage names the old engine as the default")
file(REMOVE_RECURSE "${WORK}")
