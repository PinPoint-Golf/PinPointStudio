# ── The offscreen QML UI suite lives HERE, not in tests/, and that is forced ──
#
# This CMakeLists exists for one target: qml_ui_test. It is pulled in by
# src/Gui/tests/CMakeLists.txt via add_subdirectory and is INERT for the app build, which
# never adds this directory.
#
# ⚠ WHY IT CANNOT SIT IN src/Gui/tests WITH THE OTHER GUI SUITES. qt_add_qml_module derives
# each file's qmlcachegen output directory from the path RELATIVE TO CMAKE_CURRENT_SOURCE_DIR
# (Qt6QmlMacros.cmake:3841, `file(RELATIVE_PATH file_relative ${CMAKE_CURRENT_SOURCE_DIR}
# ${file_absolute})`) — with no property to override it. Declared from src/Gui/tests, every
# one of the 157 files is reached by going UP, so the cache directory becomes
# ".rcc/qmlcache/qml_ui_test_../theme". macOS resolves that and builds; ninja on Windows
# fails outright with
#     mkdir(Gui/.rcc/qmlcache/qml_ui_test_../theme): No such file or directory
# Declared from src/Gui — an ancestor of all 157 — the relative paths are clean and both
# platforms agree. That is the whole reason for this file's location.
#
# The RESOURCE ALIASES stay repo-relative regardless (src/Gui/theme/Theme.qml), so the
# qmldir this module generates matches the app's entry for entry.

# ── The offscreen QML UI suite ────────────────────────────────────────────────
#
# 11 tst_*.qml files that load real components, render them offscreen and press them. This is the
# ONLY coverage of the QML↔C++ seam and of the layouts themselves.
#
# ⚠ IT DECLARES THE APP'S QML MODULE RATHER THAN BORROWING IT. Previously the module was attached to
# the PinPointStudio executable, so the test could only import it via a staged copy with its
# `prefer` line stripped (cmake/StripQmlPrefer.cmake, now deleted) and could only run from an app
# build. That is why it was in no release gate on any platform, and why a 19-hour-stale app build
# on the studio PC was silently running current tests against yesterday's UI — QUICK_TEST_SOURCE_DIR
# reads the tst_*.qml live from the source tree while the module came from a build artifact.
#
# Declaring the module here compiles the same .qml into THIS binary's resources, so `prefer` points
# at resources it actually has, the staging step is unnecessary, and nothing depends on the app.
#
# ⚠ AND IT NOW RUNS AOT-COMPILED rather than interpreted. The staged-copy arrangement loaded the
# .qml from disk, which meant qmlcachegen's output was never what the suite exercised. It now runs
# the way the app runs. That is the fidelity gain; it does NOT make qml_reactivity_test redundant,
# which reads source to catch the dead statements the compiler drops (and the interpreter keeps).
if(NOT TARGET Qt6::QuickTest)
    find_package(Qt6 QUIET COMPONENTS Quick QuickTest Multimedia GuiPrivate)
endif()

if(NOT Qt6QuickTest_FOUND)
    message(STATUS "Gui tests: Qt6::QuickTest not found — offscreen QML UI suite skipped")
else()
    # Not called by tests/cmake/PinPointTests.cmake (which finds only Core + Gui and uses plain
    # add_executable); the QML macros need it.
    #
    # ⚠ REQUIRES 6.10 MATCHES THE ROOT CMakeLists EXACTLY (line 147), and that is the point rather
    # than housekeeping: the argument selects which Qt QML policies are NEW, and QTP0004 among them
    # governs how a module spanning many directories is assembled. Two modules built from one set of
    # lists under two different policy sets are two different modules, which would quietly undo the
    # reason this suite declares the app's module rather than an approximation of it.
    qt_standard_project_setup(REQUIRES 6.10)

    # Absolute and normalised, so nothing downstream carries literal ".." segments — see the
    # note at the top of this file for what those cost on Windows.
    get_filename_component(PP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

    include(${PP_ROOT}/cmake/PinPointQmlModule.cmake)
    include(${PP_ROOT}/cmake/PinPointFonts.cmake)

    # pp_add_test cannot express this target: it hardcodes add_executable + Qt6::Core/Gui and has no
    # hook for a test ENVIRONMENT. So this one is spelled out.
    qt_add_executable(qml_ui_test
        tests/qml/qml_ui_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/app_settings.cpp
        # Beyond the module's own SOURCES: the link closure those types reach into. Every one of
        # these is already compiled by another suite in this umbrella, so nothing new is dragged in.
        ${PP_ROOT}/src/LaunchMonitor/launch_monitor_base.cpp
        ${PP_ROOT}/src/LaunchMonitor/launch_monitor_factory.cpp
        ${PP_ROOT}/src/LaunchMonitor/gcquad_monitor.cpp
        ${PP_ROOT}/src/LaunchMonitor/gcquad_csv_parser.cpp
        # ⚠ THE FACTORY REACHES EVERY CONNECTOR, so a second Kind lands here too:
        # launch_monitor_factory.cpp constructs one of each, which is what makes it
        # the file that pulls the whole set into any target that compiles it.
        ${PP_ROOT}/src/LaunchMonitor/gspro_monitor.cpp
        ${PP_ROOT}/src/LaunchMonitor/gspro_reading.cpp
        ${PP_ROOT}/src/LaunchMonitor/premier_monitor.cpp
        ${PP_ROOT}/src/LaunchMonitor/premier_session_parser.cpp
        ${PP_ROOT}/src/Stores/swing_doc_writer.cpp ${PP_ROOT}/src/Stores/swing_store_repository.cpp
        ${PP_ROOT}/src/Stores/swing_doc_reader.cpp
        ${PP_ROOT}/src/Stores/swing_doc_analysis.cpp
        ${PP_ROOT}/src/Stores/recording_header.cpp
        ${PP_ROOT}/src/Stores/swing_paths.cpp
        # the golfer's input on each shot's review: the model's own session store (step 2.5)
        ${PP_ROOT}/src/Stores/session_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/shot/shot_list_model.cpp
        # the chest's gap bridge (session_bridge.cpp bridges older swings with it)
        # the variability grader: the façade (variability/variability_facade.cpp) needs only variability_result.cpp
        # (its basis strings); the other three are here for the presenters that will grade. variability_grader_test
        # compiles the same four
        # the charts' one idiom (charts_work.cpp: ChartsLane on the one WorkPool) and the ★ label's store (refactor 5.5a)
        ${PP_ROOT}/src/Stores/series_store.cpp)

    # ⚠ RESOURCE ALIASES ARE MANDATORY HERE, unlike in the app. The app declares the module from the
    # repo root, so each .qml is already a relative path and Qt derives the alias from it. These
    # files sit outside THIS directory, so without an explicit alias Qt refuses them outright
    # ("specified with an absolute path and is used in a Qt resource"). Setting the alias to the
    # repo-relative path is what makes the generated qmldir identical to the app's.
    set(_pp_qml_abs "")
    foreach(_f IN LISTS PP_QML_FILES)
        set(_abs "${PP_ROOT}/${_f}")
        set_source_files_properties("${_abs}" PROPERTIES QT_RESOURCE_ALIAS "${_f}")
        list(APPEND _pp_qml_abs "${_abs}")
    endforeach()
    foreach(_s IN LISTS PP_QML_SINGLETONS)
        set_source_files_properties("${PP_ROOT}/${_s}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endforeach()
    list(TRANSFORM PP_QML_SOURCES PREPEND "${PP_ROOT}/")
    list(TRANSFORM PP_CHARTS_RULES_SOURCES PREPEND "${PP_ROOT}/")

    pp_qml_cachegen_keep_unchanged(qml_ui_test)   # see cmake/PinPointQmlModule.cmake
    qt_add_qml_module(qml_ui_test
        URI PinPointStudio
        VERSION 1.0
        QML_FILES ${_pp_qml_abs}
        SOURCES   ${PP_QML_SOURCES})
    target_sources(qml_ui_test PRIVATE ${PP_CHARTS_RULES_SOURCES})   # not QML types (cmake/PinPointQmlModule.cmake)

    # The faces the app ships. Without these every containment assertion measures whatever the HOST
    # falls back to — see loadBundledFonts() in tests/qml/qml_ui_test.cpp for the two different wrong
    # answers that produced.
    list(TRANSFORM PP_FONT_FILES PREPEND "${PP_ROOT}/")
    qt_add_resources(qml_ui_test "test_fonts"
        PREFIX "/fonts"
        BASE "${PP_ROOT}/src/Resources/fonts"
        FILES ${PP_FONT_FILES})

    # The launch monitor art (qrc:/lm/…), the same vendored list the app compiles — the root
    # CMakeLists' app_lm_art. Without it every club head in the panel tests is a blank Image.
    include(${PP_ROOT}/src/Gui/resources/lm/files.cmake)
    list(TRANSFORM PP_LM_ART_FILES PREPEND "${PP_ROOT}/src/Gui/resources/lm/" OUTPUT_VARIABLE _pp_lm_art_test)
    qt_add_resources(qml_ui_test "test_lm_art"
        PREFIX "/lm"
        BASE "${PP_ROOT}/src/Gui/resources/lm"
        FILES ${_pp_lm_art_test})

    set_target_properties(qml_ui_test PROPERTIES AUTOMOC ON MACOSX_BUNDLE OFF)
    target_include_directories(qml_ui_test PRIVATE
        ${SRC} ${PP_ROOT}/src/Core ${PP_ROOT}/src/Buffer ${PP_ROOT}/src/Vocabulary/Metrics ${PP_ROOT}/src/Analysis ${PP_ROOT}/src/Diagnostics
        ${PP_ROOT}/src/Stores ${PP_ROOT}/src/LaunchMonitor ${PP_ROOT}/src/Audio ${PP_ROOT}/src/Video
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/app ${CMAKE_CURRENT_SOURCE_DIR}/review ${CMAKE_CURRENT_SOURCE_DIR}/viz ${CMAKE_CURRENT_SOURCE_DIR}/swing3d ${CMAKE_CURRENT_SOURCE_DIR}/shot ${CMAKE_CURRENT_SOURCE_DIR}/launchmonitor
        ${CMAKE_CURRENT_SOURCE_DIR}/diagnosticmodel ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics ${CMAKE_CURRENT_SOURCE_DIR}/print ${CMAKE_CURRENT_SOURCE_DIR}/charts ${CMAKE_CURRENT_SOURCE_DIR}/trust ${CMAKE_CURRENT_SOURCE_DIR}/shell ${CMAKE_CURRENT_SOURCE_DIR}/variability)
    # PP_DIAG_RESOURCES_DIR: the shipped diagnostics content, which the binary falls back to for any
    # PINPOINT_CORE_* variable its caller did not set (issue #140; see the qml_ui ENVIRONMENT below).
    set(_pp_diag "${PP_ROOT}/src/Resources/diagnostics")
    target_compile_definitions(qml_ui_test PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/qml"
        PP_DIAG_RESOURCES_DIR="${_pp_diag}")
    # NOT linked against QuickShapes, deliberately — PpLmGraphicsBody's FLIGHT card draws with
    # Shape/ShapePath and that plugin resolves off the QML import path at run time exactly as it
    # does for the app, which also links only Qt6::Quick.
    # ⚠ libgspro comes with the connector set above: GsProMonitor is a host for the
    # protocol library, so anything compiling the factory links it. pp_require_gspro()
    # resolves it the way the app does (sibling ../libgspro, an app build's _deps copy,
    # else a fetch), so this suite still configures on a machine that has never built
    # the app. Qt6::Network is the QTcpServer the connector owns.
    pp_require_gspro()
    pp_require_swingstore()   # swing_doc_*.cpp: every document read goes through the store
    # Quick3D: SwingHullGeometry (swing3d/) is a QQuick3DGeometry, compiled here with the module's
    # other C++ types.
    find_package(Qt6 REQUIRED COMPONENTS Network Quick3D)
    target_link_libraries(qml_ui_test PRIVATE pp_domain pp_rules pp_vocabulary pp_platform
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickTest Qt6::Multimedia Qt6::GuiPrivate
        Qt6::Network Qt6::Quick3D gspro pinpoint_swingstore)

    # Offscreen is set on the TEST, not asked of whoever runs it, so headless CI and a developer's
    # desk do the same thing and neither steals focus.
    #
    # ⚠ ONE CTEST TEST PER tst_*.qml, ALL LABELLED qml_ui (platform refactor B0.2b, #251). Until then
    # one test, `qml_ui`, ran every file in one process: on the studio PC's Debug build that took
    # longer than any cap, and a file that stopped hid every file after it. Each file now runs the
    # same binary with `-input <that file>`, as `qml_ui.tst_<name>`, with its own TIMEOUT, so one
    # slow or stuck file is reported alone and the rest still run, in parallel. The files are
    # independent already: every file gets its own engine and its own settings store (#171).
    #   ctest -L qml_ui -j 3          every QML file (what `ctest -R '^qml_ui$'` ran)
    #   ctest -LE qml_ui -j 3         everything else (what `ctest -E '^qml_ui$'` ran)
    #   ctest -R '^qml_ui\.tst_session_report$'   one file
    # The old name `qml_ui` no longer exists; `-R '^qml_ui$'` finds no test (ctest exits 0 on that,
    # so a script still using it runs nothing). The label is the name, kept for at least a release.
    #
    # Not RUN_SERIAL any more, so the label runs at ctest's -j. The old serial rule was about
    # starvation: under `ctest -j` beside the compute-bound suites a loaded machine starved the
    # render loop and a layout assertion became an intermittent crash. Running the label as its
    # own pass (as tools/studio/studio-build.cmd and the testing guide do) keeps the QML files
    # beside each other rather than beside the compute suites.
    #
    # TIMEOUT: about twice the file's time on the slower platform, the studio PC's Debug build
    # (3-30x the Mac's), and never under 120 s. Measured 24 Nov 2026, the studio at -j16 / the Mac at
    # -j3: SessionReport 294 s / 64 s, ChartsPanel 259 s / 51 s, ChartPresets 78 s / 20 s, LmGraphics
    # 54 s / 5 s, and the five SessionDiagnostics files (#254: split from one file that took 1381 s /
    # 432 s, 400 s of the Mac's in waitForRendering's 5 s timeout): promises 231 s / 9 s, chrome 121 s
    # / 5 s, detail 66 s / 4 s, states 42 s / 3 s, review 35 s / 3 s. Every other file takes at most
    # 24 s on the studio and 6 s on the Mac.
    # A file not in the table gets the default. A new file is picked up at the next configure
    # (CONFIGURE_DEPENDS); give it a row here if it runs past a minute on the Mac.
    file(GLOB _pp_qml_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_*.qml")
    # Each file's `// REQ: REQ-REV-5` lines are its test's requirement labels, appended after qml_ui below
    # (tools/method/make_rtm.py reads the same lines for the on-demand traceability report). The files are configure
    # dependencies, so a changed line is picked up at the next build without a full reconfigure.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_pp_qml_tests})
    set(_pp_qml_timeout_default 120)
    set(_pp_qml_timeouts
        tst_session_diagnostics_promises 480
        tst_session_diagnostics_chrome   240
        tst_session_diagnostics_detail   150
        tst_session_report       600
        tst_charts_panel         600
        tst_chart_presets        300
        tst_lm_graphics          300)
    # ⚠ THE SHIPPED DIAGNOSTICS CONTENT, THROUGH THE PINPOINT_CORE_* SEAM (2026-10-23). The app embeds
    # src/Resources/diagnostics/*.json as :/diagnostics (root CMakeLists); this binary does not, so
    # without these the presenters run on an EMPTY pack and no norms. The Posture section's "At
    # address" block is the pack's `rigSensitive` measures, so under ctest it had no rows and hid
    # while a direct run with the variables set passed. The same files the app ships, read live.
    # Since issue #140 the binary ALSO defaults each unset variable to the same directory
    # (PP_DIAG_RESOURCES_DIR above, QmlUiTestSetup's constructor), so a direct run of qml_ui_test.exe
    # gets the shipped content too; these stay explicit, and an explicit value wins over the default.
    set(_pp_qml_env "QT_QPA_PLATFORM=offscreen;PINPOINT_CORE_PACK=${_pp_diag}/core.json;PINPOINT_CORE_NORMS=${_pp_diag}/norms.json;PINPOINT_CORE_FLIGHT_NORMS=${_pp_diag}/flight_norms.json;PINPOINT_CORE_CONTEXTS=${_pp_diag}/contexts.json;PINPOINT_CORE_SCREENS=${_pp_diag}/screens.json;PINPOINT_CORE_DRILLS=${_pp_diag}/drills.json;PINPOINT_CORE_GATE_WORDS=${_pp_diag}/gate_words.json;PINPOINT_CORE_REFERENCES=${_pp_diag}/references.json")
    foreach(_q IN LISTS _pp_qml_tests)
        get_filename_component(_n "${_q}" NAME_WE)
        set(_t ${_pp_qml_timeout_default})
        list(FIND _pp_qml_timeouts ${_n} _i)
        if(_i GREATER -1)
            math(EXPR _i "${_i} + 1")
            list(GET _pp_qml_timeouts ${_i} _t)
        endif()
        add_test(NAME qml_ui.${_n} COMMAND qml_ui_test -input "${_q}")
        set_tests_properties(qml_ui.${_n} PROPERTIES
            ENVIRONMENT "${_pp_qml_env}"
            LABELS qml_ui
            TIMEOUT ${_t})
        file(STRINGS "${_q}" _pp_req_lines REGEX "^[ \t]*//[ \t]*REQ:")
        foreach(_pp_req_line IN LISTS _pp_req_lines)
            string(REGEX MATCHALL "REQ-[A-Z]+-[0-9]+" _pp_req_ids "${_pp_req_line}")
            if(_pp_req_ids)
                set_property(TEST qml_ui.${_n} APPEND PROPERTY LABELS ${_pp_req_ids})
            endif()
        endforeach()
    endforeach()

    # ⚠ OPT-IN, OUT OF THE FULL SUITE AND THE RELEASE GATE (2026-10-17). It passes on an idle
    # machine but crashes in Qt's render path on a loaded one — Windows at the beta5 gate, Linux
    # (GL 2.0 context under offscreen + RHI, then a render-thread race in afterFrameEnd) the same
    # week. Kept for development of the setup screens:
    #   cmake -S tests -B build/tests -DPP_SESSION_SETUP_UI_TEST=ON
    option(PP_SESSION_SETUP_UI_TEST "Build and register the session-setup UI suite (opt-in)" OFF)
    if(PP_SESSION_SETUP_UI_TEST)
        # ── The session-setup UI suite (H1, docs/design/session_wizard_refactor_design.md §7.1) ──
        #
        # Loads session setup (ScreenSessionSetup), ImuCalibrationFlow and PpImuPanel, unmodified,
        # over fake context objects (src/Gui/tests/setup/fakes/). A separate binary from qml_ui_test
        # because the fakes are injected under the context-property names the setup QML reads and the app-only
        # enum carrier SessionController is supplied by a test stand-in — neither may leak into the
        # other suites. (The camera perspective is the registered Perspective type, in every module.) It declares the app's module the same way
        # qml_ui_test does (same lists, same aliases, same policy set), plus the stand-ins.
        #
        # ⚠ THE SOURCE LIST BELOW DUPLICATES qml_ui_test's LINK CLOSURE. Both compile the module's
        # C++ (PP_QML_SOURCES) and so need the same closure behind it; add to both together.
        qt_add_executable(session_setup_ui_test
            tests/setup/session_setup_ui_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app/app_settings.cpp
            ${PP_ROOT}/src/LaunchMonitor/launch_monitor_base.cpp
            ${PP_ROOT}/src/LaunchMonitor/launch_monitor_factory.cpp
            ${PP_ROOT}/src/LaunchMonitor/gcquad_monitor.cpp
            ${PP_ROOT}/src/LaunchMonitor/gcquad_csv_parser.cpp
            ${PP_ROOT}/src/LaunchMonitor/gspro_monitor.cpp
            ${PP_ROOT}/src/LaunchMonitor/gspro_reading.cpp
            ${PP_ROOT}/src/LaunchMonitor/premier_monitor.cpp
            ${PP_ROOT}/src/LaunchMonitor/premier_session_parser.cpp
            ${PP_ROOT}/src/Stores/swing_doc_writer.cpp ${PP_ROOT}/src/Stores/swing_store_repository.cpp
            ${PP_ROOT}/src/Stores/swing_doc_reader.cpp
            ${PP_ROOT}/src/Stores/swing_doc_analysis.cpp
            ${PP_ROOT}/src/Stores/recording_header.cpp
            ${PP_ROOT}/src/Stores/swing_paths.cpp
            ${PP_ROOT}/src/Stores/session_store.cpp   # step 2.5
            ${CMAKE_CURRENT_SOURCE_DIR}/shot/shot_list_model.cpp
            ${PP_ROOT}/src/Stores/series_store.cpp   # refactor 5.5a
            )

        # _pp_qml_abs and the singleton/alias source properties were set above for qml_ui_test and
        # apply to these same files; PP_QML_SOURCES is already PP_ROOT-prefixed.
        #
        # ⚠ NO AUDIO IN THIS BINARY. The real TingPlayer is filtered out and a silent stand-in takes
        # its QML name (tests/setup/silent_ting_player.h says why). The check below fails the
        # configure if the filter ever stops matching, so the sound cannot come back unnoticed.
        set(_pp_setup_qml_sources ${PP_QML_SOURCES})
        list(FILTER _pp_setup_qml_sources EXCLUDE REGEX "ting_player\\.(h|cpp)$")
        list(LENGTH PP_QML_SOURCES _pp_all_n)
        list(LENGTH _pp_setup_qml_sources _pp_setup_n)
        math(EXPR _pp_dropped "${_pp_all_n} - ${_pp_setup_n}")
        if(NOT _pp_dropped EQUAL 2)
            message(FATAL_ERROR "session_setup_ui_test: expected to drop ting_player.h/.cpp from "
                                "the module sources, dropped ${_pp_dropped}. The suite must stay silent.")
        endif()

        # ⚠ OUTPUT_DIRECTORY IS EXPLICIT because two targets in one directory declare URI
        # PinPointStudio; left to the default they would write the same qmldir/qmltypes.
        pp_qml_cachegen_keep_unchanged(session_setup_ui_test)   # see cmake/PinPointQmlModule.cmake
        qt_add_qml_module(session_setup_ui_test
            URI PinPointStudio
            VERSION 1.0
            OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/session_setup_qml/PinPointStudio
            QML_FILES ${_pp_qml_abs}
            SOURCES   ${_pp_setup_qml_sources}
                      ${CMAKE_CURRENT_SOURCE_DIR}/tests/setup/silent_ting_player.h
                      ${CMAKE_CURRENT_SOURCE_DIR}/tests/setup/setup_enum_standins.h)
        target_sources(session_setup_ui_test PRIVATE ${PP_CHARTS_RULES_SOURCES})   # PP_ROOT-prefixed above, as PP_QML_SOURCES

        qt_add_resources(session_setup_ui_test "setup_test_fonts"
            PREFIX "/fonts"
            BASE "${PP_ROOT}/src/Resources/fonts"
            FILES ${PP_FONT_FILES})

        # The avatar meshes BodyVizView (Calibrate) and ArmVizView (Check) load from
        # qrc:/assets/body — the same list as the app's "app_body" resource (root CMakeLists).
        # Without them no RuntimeLoader succeeds and BodyVizView.fullyLoaded, the start gate of
        # both calibration routines, never turns true.
        qt_add_resources(session_setup_ui_test "setup_test_body"
            PREFIX "/assets/body"
            BASE "${PP_ROOT}/src/Resources/body"
            FILES
                ${PP_ROOT}/src/Resources/body/ybot.glb
                ${PP_ROOT}/src/Resources/body/arm_LeftArm.glb
                ${PP_ROOT}/src/Resources/body/arm_LeftForeArm.glb
                ${PP_ROOT}/src/Resources/body/arm_LeftHand.glb
                ${PP_ROOT}/src/Resources/body/arm_RightArm.glb
                ${PP_ROOT}/src/Resources/body/arm_RightForeArm.glb
                ${PP_ROOT}/src/Resources/body/arm_RightHand.glb
                ${PP_ROOT}/src/Resources/body/body_Hips.glb
                ${PP_ROOT}/src/Resources/body/body_Spine.glb
                ${PP_ROOT}/src/Resources/body/body_Spine1.glb
                ${PP_ROOT}/src/Resources/body/body_Spine2.glb
                ${PP_ROOT}/src/Resources/body/body_Head.glb
                ${PP_ROOT}/src/Resources/body/body_LeftShoulder.glb
                ${PP_ROOT}/src/Resources/body/body_RightShoulder.glb
                ${PP_ROOT}/src/Resources/body/body_LeftUpLeg.glb
                ${PP_ROOT}/src/Resources/body/body_LeftLeg.glb
                ${PP_ROOT}/src/Resources/body/body_LeftFoot.glb
                ${PP_ROOT}/src/Resources/body/body_RightUpLeg.glb
                ${PP_ROOT}/src/Resources/body/body_RightLeg.glb
                ${PP_ROOT}/src/Resources/body/body_RightFoot.glb)

        set_target_properties(session_setup_ui_test PROPERTIES AUTOMOC ON MACOSX_BUNDLE OFF)
        target_include_directories(session_setup_ui_test PRIVATE
            ${SRC} ${PP_ROOT}/src/Core ${PP_ROOT}/src/Buffer ${PP_ROOT}/src/Vocabulary/Metrics ${PP_ROOT}/src/Analysis ${PP_ROOT}/src/Diagnostics
            ${PP_ROOT}/src/Stores ${PP_ROOT}/src/LaunchMonitor ${PP_ROOT}/src/Audio ${PP_ROOT}/src/Video
            ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/app ${CMAKE_CURRENT_SOURCE_DIR}/review ${CMAKE_CURRENT_SOURCE_DIR}/viz ${CMAKE_CURRENT_SOURCE_DIR}/swing3d ${CMAKE_CURRENT_SOURCE_DIR}/shot ${CMAKE_CURRENT_SOURCE_DIR}/launchmonitor
            ${CMAKE_CURRENT_SOURCE_DIR}/diagnosticmodel ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics ${CMAKE_CURRENT_SOURCE_DIR}/print ${CMAKE_CURRENT_SOURCE_DIR}/charts ${CMAKE_CURRENT_SOURCE_DIR}/trust ${CMAKE_CURRENT_SOURCE_DIR}/shell ${CMAKE_CURRENT_SOURCE_DIR}/variability
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/setup)
        # Its own QUICK_TEST_SOURCE_DIR: the tst_*.qml and fakes/ are read live from the source
        # tree, so editing them needs no rebuild.
        target_compile_definitions(session_setup_ui_test PRIVATE
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/setup")
        target_link_libraries(session_setup_ui_test PRIVATE pp_domain pp_rules pp_vocabulary pp_platform
            Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickTest Qt6::Multimedia Qt6::GuiPrivate
            Qt6::Network Qt6::Quick3D gspro pinpoint_swingstore)

        add_test(NAME session_setup_ui COMMAND session_setup_ui_test)
        # Offscreen and serial for the same reasons as qml_ui above; this suite renders two View3Ds.
        #
        # ⚠ QT_QUICK_BACKEND=rhi IS WHAT MAKES THE 3-D GUIDE REAL OFFSCREEN. Left to itself the
        # offscreen platform loads Qt Quick's SOFTWARE adaptation (QSG_INFO: "Loading backend
        # software"), under which Quick 3D is not functional: every View3D warns "The Qt Quick
        # scene is using a rendering method that is not based on QRhi … The View3D item is not
        # going to display anything", once per View3D (the wizard has two). Forcing the RHI
        # backend creates a real QRhi (Metal on macOS) for the offscreen window, the warnings go,
        # and the guide's FrameAnimation is ticked by the same render loop the app uses.
        # The shaders the app's QML loads, at the paths the app serves them from: the camera frame's
        # gain shader (without it every test that gives the wizard a camera instance trips three
        # ShaderEffect warnings), the topographic background (PpTopoBackground) and the GPU Bayer
        # demosaic (BayerVideoItem). Without the last two a real D3D11 window — the one this suite
        # runs in on Windows — warns "Empty shader passed to graphics pipeline" on every page that
        # draws them: 107 unexpected warnings across 29 cases, 2026-10-08.
        find_package(Qt6 QUIET COMPONENTS ShaderTools)
        if(Qt6ShaderTools_FOUND)
            qt6_add_shaders(session_setup_ui_test "setup_test_viewgain_shaders"
                BATCHABLE PRECOMPILE OPTIMIZED
                PREFIX "/shaders"
                BASE "${PP_ROOT}"
                FILES
                    "${PP_ROOT}/src/Shaders/viewgain.vert"
                    "${PP_ROOT}/src/Shaders/viewgain.frag"
                    "${PP_ROOT}/src/Shaders/topo.vert"
                    "${PP_ROOT}/src/Shaders/topo.frag"
                    "${PP_ROOT}/src/Shaders/bayer_demosaic.vert"
                    "${PP_ROOT}/src/Shaders/bayer_demosaic.frag")
        endif()

        # QSG_USE_SIMPLE_ANIMATION_DRIVER: under the offscreen platform QML Timers otherwise run
        # about a third fast (a 2000 ms Timer fires near 1500 ms), and this suite asserts pacing.
        #
        # ⚠ ON WINDOWS IT RUNS IN A REAL WINDOW. Offscreen + RHI has no working Quick 3D there
        # (2026-10-08): D3D11 cannot create a swapchain for an offscreen window ("Access is denied",
        # then a "Failed to present / Failed to end frame" warning every frame — 1000+ per case,
        # failing them), and OpenGL aborts (0xC0000409). The windows platform renders the guide for
        # real; the window shows on screen while the suite runs, so leave it unoccluded (an occluded
        # window starves the render loop and its Timers — see Main.qml's probe hook note).
        if(WIN32)
            set(_ssui_qpa windows)
        else()
            set(_ssui_qpa offscreen)
        endif()
        set_tests_properties(session_setup_ui PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${_ssui_qpa};QT_QUICK_BACKEND=rhi;QSG_USE_SIMPLE_ANIMATION_DRIVER=1"
            TIMEOUT 3600
            RUN_SERIAL TRUE)
        # The trunk file alone (its golden traces, platform refactor C-9a), as qml_ui's per-file tests
        # run theirs: `ctest -L qml_ui -R setup_trunk`. The same binary, file and environment as above;
        # the whole suite still runs it too. Opt-in with the suite.
        add_test(NAME session_setup_ui.tst_setup_trunk COMMAND session_setup_ui_test
                 -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/setup/tst_setup_trunk.qml")
        set_tests_properties(session_setup_ui.tst_setup_trunk PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${_ssui_qpa};QT_QUICK_BACKEND=rhi;QSG_USE_SIMPLE_ANIMATION_DRIVER=1"
            LABELS qml_ui
            TIMEOUT 900
            RUN_SERIAL TRUE)
        # The Witmotion arm routine's file alone (its golden traces, platform refactor C-9b), the same way:
        # `ctest -L qml_ui -R setup_calib_witmotion`. Its wizard cases run at the real pace, hence the longer
        # timeout. Opt-in with the suite.
        add_test(NAME session_setup_ui.tst_setup_calib_witmotion COMMAND session_setup_ui_test
                 -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/setup/tst_setup_calib_witmotion.qml")
        set_tests_properties(session_setup_ui.tst_setup_calib_witmotion PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${_ssui_qpa};QT_QUICK_BACKEND=rhi;QSG_USE_SIMPLE_ANIMATION_DRIVER=1"
            LABELS qml_ui
            TIMEOUT 1800
            RUN_SERIAL TRUE)
        # The HackMotion arm routine's file alone (its golden traces, platform refactor C-9c), the same way:
        # `ctest -L qml_ui -R setup_calib_hackmotion`. Its wizard cases run at the real pace. Opt-in with the suite.
        add_test(NAME session_setup_ui.tst_setup_calib_hackmotion COMMAND session_setup_ui_test
                 -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/setup/tst_setup_calib_hackmotion.qml")
        set_tests_properties(session_setup_ui.tst_setup_calib_hackmotion PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${_ssui_qpa};QT_QUICK_BACKEND=rhi;QSG_USE_SIMPLE_ANIMATION_DRIVER=1"
            LABELS qml_ui
            TIMEOUT 1800
            RUN_SERIAL TRUE)
    endif()
endif()
