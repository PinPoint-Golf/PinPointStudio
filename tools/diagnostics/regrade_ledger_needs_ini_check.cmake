# regrade_ledger_new_needs_ini_test (refactor 3.10, review B1; re-worded in refactor 4-c, J-61, and 5.3): the new engine,
# `regrade_ledger`'s default since 5.3, must never pool golfers. Each session's athlete comes from its athlete folder's
# athlete.json (the library, read through AthleteRepository); else from athletes.json beside the session (a folder of
# several golfers' sessions, the corpus: the entry whose name is in the session folder's name); --athletes-ini (a copy of
# the app's settings) is only the last fallback, for a library that has not been migrated. A session whose athlete NO
# source names is refused — SKIP, exit non-zero, nothing written — because the new engine freezes each session's club
# baseline from its athlete's earlier sessions and, without a record, would count every session of one folder as one
# golfer. Case 7 runs with no --engine (the default is new); --help says so. Case 8 (refactor 5.4b): --engine old is refused. Case 5c (refactor 5.3c): an entry's `aliases`.
#
#   cmake -DTOOL=<regrade_ledger> -DWORK=<an empty scratch folder> -P regrade_ledger_needs_ini_check.cmake
#
# The PINPOINT_CORE_* seams come from the test's ENVIRONMENT (the per-session refusal is after the tool's content check).
# Cases 3, 4 and 5 RUN the new engine on a session with one empty swing: they check only that it was not refused (whether
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
if(NOT _out MATCHES "SKIP +no athlete.json in Golfer-One, no athletes.json beside it and no --athletes-ini names its athlete: refused, so no golfer is pooled")
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

# 5. The golfer file: two golfers' sessions in ONE folder with no athlete.json, and athletes.json beside them → each
#    graded as its own entry (the cohort printed is the entry's), read from athletes.json. Unlike cases 3 and 4, the
#    graded line must be there: the owner opens a session with an empty swing and prints its line.
set(_flat "${WORK}/corpus/swings")
set(_fa "${_flat}/2026-01-02_Golfer-Three_Wrist_01")
set(_fb "${_flat}/2026-01-02_Golfer-Four_Wrist_01")
set(_fc "${_flat}/2026-01-02_Golfer-Five_Wrist_01")
foreach(_s IN ITEMS "${_fa}" "${_fb}" "${_fc}")
  file(MAKE_DIRECTORY "${_s}/swing_0001")
  file(WRITE "${_s}/swing_0001/swing.json" "{}")
endforeach()
# 5c (refactor 5.3c, an alias): Golfer Three's swing records an id athletes.json lists as one of its `aliases`; Golfer
#     Four's records an id the file does not list. Both are graded as their entry's id (the name decides), and each says so.
file(WRITE "${_fa}/swing_0001/swing.json" "{\"athlete\":{\"uuid\":\"52ff45d8-37a6-4474-bdc6-5a4184f78387\"}}")
file(WRITE "${_fb}/swing_0001/swing.json" "{\"athlete\":{\"uuid\":\"9d8c7b6a-5f4e-4d3c-8b2a-1f0e9d8c7b6a\"}}")
file(WRITE "${_flat}/athletes.json"
     "{\"schema\":\"pinpoint.athletes/1\",\"athletes\":["
     "{\"athleteId\":\"0b3c4d5e-6f70-4a81-9b2c-3d4e5f607182\",\"name\":\"Golfer Three\",\"sex\":\"female\",\"dob\":\"1991-02-03\","
     "\"aliases\":[\"52ff45d8-37a6-4474-bdc6-5a4184f78387\"]},"
     "{\"athleteId\":\"1c4d5e6f-7081-4b92-8c3d-4e5f60718293\",\"name\":\"Golfer Four\",\"sex\":\"male\",\"dob\":\"\"}]}")
execute_process(COMMAND "${TOOL}" --engine new --tag check "${_fa}" "${_fb}"
                RESULT_VARIABLE _rc6 OUTPUT_VARIABLE _out6 ERROR_VARIABLE _err6)
if(_out6 MATCHES "refused, so no golfer is pooled")
  message(FATAL_ERROR "FAIL: a session athletes.json names was refused:\n${_out6}${_err6}")
endif()
if(NOT _out6 MATCHES "Golfer-Three_Wrist_01 +(REGRADED|GRADED) +shots[^\n]*cohort female 1991-02-03, athlete from athletes.json")
  message(FATAL_ERROR "FAIL: Golfer Three's session graded but not as athletes.json's Golfer Three:\n${_out6}${_err6}")
endif()
if(NOT _out6 MATCHES "Golfer-Four_Wrist_01 +(REGRADED|GRADED) +shots[^\n]*cohort male -, athlete from athletes.json")
  message(FATAL_ERROR "FAIL: Golfer Four's session graded but not as athletes.json's Golfer Four:\n${_out6}${_err6}")
endif()
message(STATUS "PASS: two golfers in one folder are each read from athletes.json as their own entry (exit ${_rc6})")
if(NOT _out6 MATCHES "its swings record 52ff45d8-37a6-4474-bdc6-5a4184f78387, an alias of Golfer Three in athletes.json: the same golfer")
  message(FATAL_ERROR "FAIL: a swing recording Golfer Three's alias was not said to be the same golfer:\n${_out6}${_err6}")
endif()
if(NOT _out6 MATCHES "its swings record 9d8c7b6a-5f4e-4d3c-8b2a-1f0e9d8c7b6a, which athletes.json does not list for Golfer Four \\(matched by name\\)")
  message(FATAL_ERROR "FAIL: a swing recording an id athletes.json does not list was not said so:\n${_out6}${_err6}")
endif()
message(STATUS "PASS: an alias in athletes.json is the same golfer; an unlisted recorded id is said, not refused")
# 5b. A corpus run freezes no club baseline (refactor 5.3): in a flat folder no golfer has an earlier session, so a
#     baseline frozen there would be empty for good. Neither session gets a club_baseline.json, and each says so.
foreach(_s IN ITEMS "${_fa}" "${_fb}")
  if(EXISTS "${_s}/club_baseline.json")
    message(FATAL_ERROR "FAIL: a corpus run wrote ${_s}/club_baseline.json:\n${_out6}${_err6}")
  endif()
endforeach()
string(REGEX MATCHALL "club baseline not frozen: corpus run" _nf "${_out6}")
list(LENGTH _nf _nfn)
if(NOT _nfn EQUAL 2)
  message(FATAL_ERROR "FAIL: a corpus run did not say, for each session, that no club baseline was frozen (${_nfn}):\n${_out6}${_err6}")
endif()
message(STATUS "PASS: a corpus run (athletes.json) writes no club_baseline.json, and says so per session")

# 6. A session in that folder athletes.json does not name → refused, nothing written; a newer schema names nobody.
file(GLOB_RECURSE _beforeC RELATIVE "${WORK}" "${_fc}/*")
execute_process(COMMAND "${TOOL}" --engine new "${_fc}" RESULT_VARIABLE _rc7 OUTPUT_VARIABLE _out7 ERROR_VARIABLE _err7)
file(GLOB_RECURSE _afterC RELATIVE "${WORK}" "${_fc}/*")
if(_rc7 EQUAL 0 OR NOT _out7 MATCHES "SKIP +no athlete.json in swings, no athletes.json beside it and no --athletes-ini names its athlete" OR NOT _beforeC STREQUAL _afterC)
  message(FATAL_ERROR "FAIL: a session athletes.json does not name was not refused untouched (exit ${_rc7}):\n${_out7}${_err7}")
endif()
file(WRITE "${_flat}/athletes.json" "{\"schema\":\"pinpoint.athletes/2\",\"athletes\":[]}")
execute_process(COMMAND "${TOOL}" --engine new "${_fa}" RESULT_VARIABLE _rc8 OUTPUT_VARIABLE _out8 ERROR_VARIABLE _err8)
if(_rc8 EQUAL 0 OR NOT _out8 MATCHES "refused, so no golfer is pooled \\(athletes.json is pinpoint.athletes/2, a newer build's\\)")
  message(FATAL_ERROR "FAIL: a newer athletes.json was not refused with its reason (exit ${_rc8}):\n${_out8}${_err8}")
endif()
message(STATUS "PASS: a session athletes.json does not name is refused untouched, and a newer schema names nobody")

# 7. The default engine is new: with no --engine, a session no source names is refused (the old engine never refuses).
execute_process(COMMAND "${TOOL}" "${_two}" RESULT_VARIABLE _rc9 OUTPUT_VARIABLE _out9 ERROR_VARIABLE _err9)
if(_rc9 EQUAL 0 OR NOT _out9 MATCHES "refused, so no golfer is pooled")
  message(FATAL_ERROR "FAIL: with no --engine a session no source names was not refused (exit ${_rc9}):\n${_out9}${_err9}")
endif()
execute_process(COMMAND "${TOOL}" --help RESULT_VARIABLE _rc5 OUTPUT_VARIABLE _help)
if(NOT _rc5 EQUAL 0 OR NOT _help MATCHES "--engine new +\\(the default, and the only engine")
  message(FATAL_ERROR "FAIL: --help does not name the new engine as the default and the only one (exit ${_rc5}):\n${_help}")
endif()
message(STATUS "PASS: the default engine is new (a session no source names is refused with no --engine), and --help says so")

# 8. (refactor 5.4b) --engine old is gone: refused with its sentence, nothing read or written.
execute_process(COMMAND "${TOOL}" --engine old "${_two}" RESULT_VARIABLE _rc10 OUTPUT_VARIABLE _out10 ERROR_VARIABLE _err10)
if(NOT _rc10 EQUAL 2 OR NOT _err10 MATCHES "--engine old was removed in refactor 5.4b")
  message(FATAL_ERROR "FAIL: --engine old was not refused with its sentence (exit ${_rc10}):\n${_out10}${_err10}")
endif()
message(STATUS "PASS: --engine old is refused with its sentence (removed in refactor 5.4b)")
file(REMOVE_RECURSE "${WORK}")
