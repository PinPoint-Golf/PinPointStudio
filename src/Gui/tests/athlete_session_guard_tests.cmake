# No athlete switch while a session runs, and a record edit is not a switch (SWE audit E-01 / E-03,
# Stage 0.8g): AthleteController's session check (selectAthlete / deleteAthlete / clearCurrentAthlete
# refused, a new athlete saved but not selected) and AthleteSwitchGate, the gate the live carousel's
# reload (main.cpp) and SessionReviewController::onAthleteChanged run through. Against a scratch INI
# (QSettings::setPath) — never the real one. The link line is report_studio_test's, for the same
# reason: athlete_controller.cpp pulls in the norm stack for cohortFor().
pp_add_test(athlete_session_guard_test
    SOURCES athlete_session_guard_test.cpp
            ${GUI}/athlete/athlete_controller.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
            ${SRC}/Diagnostics/norms/norm.cpp
            ${SRC}/Diagnostics/norms/norm_pack.cpp
            ${SRC}/Diagnostics/norms/context_tree.cpp
            ${SRC}/Diagnostics/engine/pack_io.cpp
            ${SRC}/Diagnostics/packs/characteristic_pack.cpp
            ${SRC}/Diagnostics/engine/characteristic.cpp
            ${SRC}/Diagnostics/packs/reference_pack.cpp
            ${SRC}/Diagnostics/measures/measure_facets.cpp
            ${SRC}/Diagnostics/measures/anatomy_vocabulary.cpp
    INCLUDE ${GUI} ${GUI}/athlete ${SRC} ${SRC}/Core ${SRC}/Diagnostics ${SRC}/Metrics ${SRC}/Analysis
    LINK    Qt6::Test
    AUTOMOC)
