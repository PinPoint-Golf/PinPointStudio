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
            ${SRC}/Stores/studio_profile_store.cpp   # its repository: the keys and the folder (SET-20, FILE-APP-06)
            ${GUI}/athlete/athlete_controller.cpp
            # The athlete's record through its one door (refactor step 2.3): the Athlete, AthleteStore and
            # the session repository its index reads through.
            ${SRC}/DomainModel/athlete.h ${SRC}/DomainModel/athlete.cpp
            ${SRC}/DomainModel/session.h ${SRC}/DomainModel/session.cpp   # the Athlete's sessions (step 3.4a)
            ${SRC}/DomainModel/shot.h ${SRC}/DomainModel/shot.cpp
            ${SRC}/Stores/athlete_store.cpp
            ${SRC}/Stores/session_store.cpp
            ${SRC}/DomainModel/session_identity.cpp
            ${SRC}/Vocabulary/Identity/identity.h
            ${SRC}/Stores/swing_paths.cpp
            ${SRC}/Core/folder_lock.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
            ${SRC}/Diagnostics/model/norm.cpp
            ${SRC}/Diagnostics/content/norm_pack.cpp
            ${SRC}/Diagnostics/model/context_tree.cpp
            ${SRC}/Diagnostics/model/pack_io.cpp
            ${SRC}/Diagnostics/content/characteristic_pack.cpp
            ${SRC}/Diagnostics/model/characteristic.cpp
            ${SRC}/Diagnostics/content/reference_pack.cpp
            ${SRC}/Diagnostics/model/measure_facets.cpp
            ${SRC}/Diagnostics/model/anatomy_vocabulary.cpp
            # The one session list: the controller's activity is its rows' (refactor step 5.7b).
            ${GUI}/session/session_list_presenter.h ${GUI}/session/session_list_presenter.cpp
            ${GUI}/session/session_list_follow.cpp ${SRC}/Stores/session_list_reads.cpp
    INCLUDE ${GUI} ${GUI}/athlete ${SRC} ${SRC}/Core ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics ${SRC}/Analysis
    LINK    Qt6::Test
    AUTOMOC)
