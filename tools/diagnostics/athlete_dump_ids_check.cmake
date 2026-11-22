# athlete_dump_ids_test (refactor 5.3c; REVIEW-5.3 R2): athlete_dump takes each athlete's id from its sessions' own
# session.json files, read through the session repository, and an empty dump never passes.
#
#   cmake -DTOOL=<athlete_dump> -DSWING=<a live swing folder> -DWORK=<an empty folder> -P athlete_dump_ids_check.cmake
#
# A. A migrated-style library: the session file names an id that is NOT the one made from the folder's name (as since the
#    22 Nov migrations) → the session is counted (1/1), the id read from session.json, exit 0, under either engine.
#    Until 5.3c: 0/0, exit 0.
# B. A folder holding a session that leaves 0 counted (here: a folder name the store does not derive from the athlete's
#    name, so its index is empty) → exit non-zero, and it says so.
# C. Sessions whose files name two athletes → refused with words, before --fresh deletes anything.
# The PINPOINT_CORE_* seams come from the test's ENVIRONMENT. The tool writes only under a path with a `scratch`
# component, so WORK's folders sit under one; WORK is deleted at the end.
if(NOT TOOL OR NOT WORK OR NOT SWING)
  message(FATAL_ERROR "TOOL, SWING and WORK must be set")
endif()
file(REMOVE_RECURSE "${WORK}")
set(_lib "${WORK}/scratch/lib")

# A session folder: one swing (the live fixture) and a session file naming `id`.
function(make_session dir sid id)
  file(MAKE_DIRECTORY "${dir}/swing_0001")
  file(COPY "${SWING}/swing.json" DESTINATION "${dir}/swing_0001")
  file(WRITE "${dir}/session.json"
       "{\"schema\":\"pinpoint.session/1\",\"sessionId\":\"${sid}\",\"athleteId\":\"${id}\","
       "\"startedAt\":\"2026-01-01T10:00:00.000Z\",\"endedAt\":\"2026-01-01T11:00:00.000Z\",\"status\":\"ended\",\"type\":\"wrist\"}")
endfunction()

# A. The id from session.json.
make_session("${_lib}/Golfer-One/2026-01-01_Golfer-One_Wrist_01" "3b0c1d2e-4f5a-4b6c-8d7e-9f0a1b2c3d4e"
             "6f1c2b9e-3d4a-4c5b-9e8f-0a1b2c3d4e5f")
execute_process(COMMAND "${TOOL}" --fresh --out "${WORK}/a.json" "${_lib}/Golfer-One"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _out MATCHES "Golfer-One +SETTLED +sessions 1/1 [^\n]*athlete id from session.json")
  message(FATAL_ERROR "FAIL: a migrated-style session was not counted as its session.json's athlete's (exit ${_rc}):\n${_out}${_err}")
endif()
file(READ "${WORK}/a.json" _dump)
if(NOT _dump MATCHES "\"sessionCount\": *1")
  message(FATAL_ERROR "FAIL: the dump does not count the session:\n${_dump}")
endif()
message(STATUS "PASS: the athlete's id is its session.json's, and the session is counted (exit ${_rc})")
execute_process(COMMAND "${TOOL}" --engine old --fresh --out "${WORK}/a-old.json" "${_lib}/Golfer-One"
                RESULT_VARIABLE _rco OUTPUT_VARIABLE _outo ERROR_VARIABLE _erro)
if(NOT _rco EQUAL 0 OR NOT _outo MATCHES "Golfer-One +SETTLED +sessions 1/1 [^\n]*athlete id from session.json")
  message(FATAL_ERROR "FAIL: under --engine old the session was not counted as its session.json's athlete's (exit ${_rco}):\n${_outo}${_erro}")
endif()
message(STATUS "PASS: --engine old reads the same id and counts the session (exit ${_rco})")

# B. Sessions, 0 counted: a failure, in words.
make_session("${_lib}/Golfer Three/2026-01-01_Golfer-Three_Wrist_01" "5d2e3f4a-4f5a-4b6c-8d7e-9f0a1b2c3d4e"
             "7a2d3c4b-5e6f-4a1b-8c9d-0e1f2a3b4c5d")
execute_process(COMMAND "${TOOL}" --fresh --out "${WORK}/b.json" "${_lib}/Golfer Three"
                RESULT_VARIABLE _rc2 OUTPUT_VARIABLE _out2 ERROR_VARIABLE _err2)
if(_rc2 EQUAL 0 OR NOT _err2 MATCHES "Golfer Three holds 1 session folder\\(s\\) but 0 sessions were counted")
  message(FATAL_ERROR "FAIL: a folder of sessions that dumped none did not fail with words (exit ${_rc2}):\n${_out2}${_err2}")
endif()
message(STATUS "PASS: a folder holding sessions that leaves 0 counted fails, and says so (exit ${_rc2})")

# C. Two athletes' session files in one folder: refused before anything is deleted.
make_session("${_lib}/Golfer-Two/2026-01-01_Golfer-Two_Wrist_01" "6e3f4a5b-4f5a-4b6c-8d7e-9f0a1b2c3d4e"
             "0b3c4d5e-6f70-4a81-9b2c-3d4e5f607182")
make_session("${_lib}/Golfer-Two/2026-01-02_Golfer-Two_Wrist_01" "7f4a5b6c-4f5a-4b6c-8d7e-9f0a1b2c3d4e"
             "1c4d5e6f-7081-4b92-8c3d-4e5f60718293")
file(WRITE "${_lib}/Golfer-Two/2026-01-01_Golfer-Two_Wrist_01/work_ons.json" "{}")
execute_process(COMMAND "${TOOL}" --fresh --out "${WORK}/c.json" "${_lib}/Golfer-Two"
                RESULT_VARIABLE _rc3 OUTPUT_VARIABLE _out3 ERROR_VARIABLE _err3)
if(_rc3 EQUAL 0 OR NOT _err3 MATCHES "Golfer-Two: its sessions' session.json files name 2 athletes")
  message(FATAL_ERROR "FAIL: sessions naming two athletes were not refused with words (exit ${_rc3}):\n${_out3}${_err3}")
endif()
if(NOT EXISTS "${_lib}/Golfer-Two/2026-01-01_Golfer-Two_Wrist_01/work_ons.json")
  message(FATAL_ERROR "FAIL: the refused folder's work_ons.json was deleted (--fresh ran before the refusal)")
endif()
message(STATUS "PASS: sessions naming two athletes are refused before anything is deleted (exit ${_rc3})")
file(REMOVE_RECURSE "${WORK}")
