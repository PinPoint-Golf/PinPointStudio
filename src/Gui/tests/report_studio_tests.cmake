# Session report — studio tests (owned by the agent building the studio part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.

# StudioProfile (src/App/studio_profile) and the golfer's four report fields on AthleteController
# (design §7): defaults, the INI round trip, the logo / coach-photo copies, the stamp moving with each
# printed field, the monogram, the accent's contrast test; reportDetails, isJuniorOn on the session
# date, the email shape check, the approval date, deleteAthlete. Against a scratch INI
# (QSettings::setPath) and a scratch studio folder (PINPOINT_STUDIO_DIR) — never the real ones.
#
# athlete_controller.cpp pulls in the norm stack for cohortFor(); the link line below is the one
# src/Update/tests/CMakeLists.txt (update_controller_policy_test) already proves, for the same reason.
pp_add_test(report_studio_test
    SOURCES report_studio_test.cpp
            ${SRC}/App/studio_profile.cpp
            ${GUI}/athlete/athlete_controller.cpp
            # The athlete's record through its one door (refactor step 2.3): the Athlete, AthleteStore and
            # the session repository its index reads through.
            # The one session list: the controller's activity is its rows' (refactor step 5.7b).
            ${GUI}/session/session_list_presenter.h ${GUI}/session/session_list_presenter.cpp
            ${GUI}/session/session_list_follow.cpp
    INCLUDE ${GUI} ${GUI}/athlete ${SRC} ${SRC}/Core ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics ${SRC}/Analysis
    LINK pp_stores pp_domain pp_rules pp_vocabulary pp_platform    Qt6::Test
    AUTOMOC)
