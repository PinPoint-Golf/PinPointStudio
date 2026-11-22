# headless_probe_test (refactor 4-c): THE APP STARTED WITH PINPOINT_HEADLESS_PROBE=1, offscreen, against a scratch home,
# scratch settings and an empty scratch library, with the probe src/Gui/tests/probes/headless_start.qml, which visits
# every screen and every Settings panel and exits. Passes when:
#   - the probe says RESULT PASS and the app exits 0 within the timeout;
#   - the app log says "[HeadlessProbe] not started: <part>" for every device part (Bluetooth, the microphone and
#     speech recognition, speech output, the cameras, the launch monitor, and the phone link where it is built);
#   - no "[HeadlessProbe] REFUSED" line (a part that reached the device layer under the switch), and no QML
#     ReferenceError or TypeError;
#   - on macOS, tccd's log over the run shows no display_prompt (a permission dialog).
#
#   cmake -DAPP=<PinPointStudio binary> -DPROBE=<headless_start.qml> -DWORK=<scratch folder> -P headless_probe_check.cmake
#
# SAFETY FIRST: a binary that does not carry the switch is never launched (its startup would raise the macOS
# Bluetooth, Speech and Microphone prompts on whoever's screen it is): the test FAILS, as it does for a missing binary.
# The only skip is the explicit opt-out APP=OFF (exit 77, ctest's "Not Run").
if(NOT PROBE OR NOT WORK)
  message(FATAL_ERROR "PROBE and WORK must be set")
endif()
# The one skip: an explicit opt-out (-DPP_APP_BINARY=OFF in the tests tree). Exit 77: ctest's "Not Run", never "Passed".
if(APP STREQUAL "OFF")
  message("SKIPPED: headless_probe_test opted out (PP_APP_BINARY=OFF) - the headless start is NOT checked")
  cmake_language(EXIT 77)
endif()
if(NOT APP OR NOT EXISTS "${APP}")
  message(FATAL_ERROR "FAIL: no app binary at '${APP}'. Build the app, or configure the tests tree with "
                      "-DPP_APP_BINARY=<its PinPointStudio binary> (or -DPP_APP_BINARY=OFF to skip, reported as Not Run).")
endif()
file(STRINGS "${APP}" _marker REGEX "\\[HeadlessProbe\\] not started: the pose model" LIMIT_COUNT 1)
if(NOT _marker)
  message(FATAL_ERROR "FAIL: ${APP} does not carry PINPOINT_HEADLESS_PROBE (built before refactor 4-c?). It was NOT "
                      "launched: its startup would raise the Bluetooth, Speech and Microphone prompts. Rebuild the app.")
endif()
file(STRINGS "${APP}" _ppcp REGEX "\\[HeadlessProbe\\] not started: phone link" LIMIT_COUNT 1)

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home/.config/PinPointStudio" "${WORK}/lib" "${WORK}/diagnostics")
file(WRITE "${WORK}/home/.config/PinPointStudio/PinPointStudio.ini"
     "[%General]\nathleteLibraryPath=${WORK}/lib\ncheckForUpdates=false\n")   # AppSettings' "General/..." keys

string(TIMESTAMP _start "%Y-%m-%d %H:%M:%S")
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env
          PINPOINT_HEADLESS_PROBE=1 PINPOINT_LOG_STDERR=1 QT_QPA_PLATFORM=offscreen
          "HOME=${WORK}/home" "CFFIXED_USER_HOME=${WORK}/home" "XDG_CONFIG_HOME=${WORK}/home/.config"
          "PINPOINT_DIAGNOSTICS_DIR=${WORK}/diagnostics"
          "${APP}" --probe-qml "${PROBE}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 200)
set(_log "${_out}\n${_err}")
file(WRITE "${WORK}/app.log" "${_log}")

set(_fail "")
if(NOT _rc EQUAL 0)
  string(APPEND _fail "the app exited ${_rc} (the probe's verdict, a crash or the timeout)\n")
endif()
if(NOT _log MATCHES "PROBE: RESULT PASS")
  string(APPEND _fail "the probe did not say RESULT PASS\n")
endif()
set(_parts "bluetooth" "microphone and speech recognition" "speech output" "cameras" "launch monitor")
if(_ppcp)
  list(APPEND _parts "phone link")
endif()
foreach(_p IN LISTS _parts)
  string(FIND "${_log}" "[HeadlessProbe] not started: ${_p}" _at)
  if(_at LESS 0)
    string(APPEND _fail "no \"[HeadlessProbe] not started: ${_p}\" line\n")
  endif()
endforeach()
string(REGEX MATCHALL "[^\n]*\\[HeadlessProbe\\] REFUSED[^\n]*" _refused "${_log}")
if(_refused)
  string(APPEND _fail "a part reached the device layer under the switch:\n${_refused}\n")
endif()
string(REGEX MATCHALL "[^\n]*(ReferenceError|TypeError)[^\n]*" _qml "${_log}")
if(_qml)
  string(REPLACE ";" "\n" _qml "${_qml}")
  string(APPEND _fail "QML errors:\n${_qml}\n")
endif()

if(APPLE)
  execute_process(COMMAND /usr/bin/log show --info --debug --start "${_start}" --predicate "process == \"tccd\""
                  RESULT_VARIABLE _lrc OUTPUT_VARIABLE _tcc ERROR_VARIABLE _tccerr TIMEOUT 120)
  file(WRITE "${WORK}/tccd.log" "${_tcc}")
  if(NOT _lrc EQUAL 0)
    string(APPEND _fail "tccd's log could not be read (exit ${_lrc}), so no prompt can be ruled out: ${_tccerr}\n")
  else()
    string(REGEX MATCHALL "[^\n]*display_prompt[^\n]*" _prompts "${_tcc}")
    if(_prompts)
      string(REPLACE ";" "\n" _prompts "${_prompts}")
      string(APPEND _fail "tccd raised a permission prompt during the run:\n${_prompts}\n")
    endif()
    string(REGEX MATCHALL "[^\n]*PinPointStudio[^\n]*" _ours "${_tcc}")
    list(LENGTH _ours _n)
    message(STATUS "tccd: ${_n} line(s) name PinPointStudio over the run; no display_prompt is the pass condition")
  endif()
endif()

if(NOT _fail STREQUAL "")
  message(FATAL_ERROR "FAIL:\n${_fail}\nThe app log is ${WORK}/app.log")
endif()
message(STATUS "PASS: the headless start made no device part, raised no prompt and loaded every screen with no QML error")
file(REMOVE_RECURSE "${WORK}")
