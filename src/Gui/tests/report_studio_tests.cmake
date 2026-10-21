# Session report — studio tests (owned by the agent building the studio part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.

# StudioProfile (src/Core/studio_profile) and the golfer's four report fields on AthleteController
# (design §7): defaults, the INI round trip, the logo / coach-photo copies, the stamp moving with each
# printed field, the monogram, the accent's contrast test; reportDetails, isJuniorOn on the session
# date, the email shape check, the approval date, deleteAthlete. Against a scratch INI
# (QSettings::setPath) and a scratch studio folder (PINPOINT_STUDIO_DIR) — never the real ones.
#
# athlete_controller.cpp pulls in the norm stack for cohortFor(); the link line below is the one
# src/Update/tests/CMakeLists.txt (update_controller_policy_test) already proves, for the same reason.
pp_add_test(report_studio_test
    SOURCES report_studio_test.cpp
            ${SRC}/Core/studio_profile.cpp
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
