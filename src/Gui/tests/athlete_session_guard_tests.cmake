# The athlete through its one door (refactor step 2.3; build/refactor/step-2/BRIEF.md §4.5, §6 row 2.3).
#
# athlete_session_guard_test — no athlete switch while a session runs (SWE audit E-01, Stage 0.8g):
# AthleteController's session check, the AppState rule since step 4-a (selectAthlete / deleteAthlete / clearCurrentAthlete refused, a new
# athlete saved but not selected), and a record edit is not a switch (E-03, Stage 0.7): currentAthleteChanged
# fires on a switch only, so the listeners that reload on it need no gate (AthleteSwitchGate is gone).
#
# athlete_repository_test — AthleteStore and the Athlete: one instance per id; athlete.json at creation in a
# folder chosen once (numbered when taken); a rename keeps the folder and its sessions; a settings athlete
# read from settings and never written by a read; remove refused while a session is InProgress, else the
# whole folder in the library's trash; an edit emits recordChanged, not currentAthleteChanged; the Wrist ★
# on the record; the tuning log's append under the lock; the activity from the index.
#
# Both against a scratch INI (QSettings::setPath) and a scratch library — never the real ones. The link
# line is report_studio_test's, for the same reason: athlete_controller.cpp pulls in the norm stack for
# cohortFor().
set(ATHLETE_TEST_SOURCES
    ${GUI}/athlete/athlete_controller.cpp
    ${SRC}/App/app_state.h ${SRC}/App/app_state.cpp   # the switch rule (R-APP-1; step 4-a)
    ${SRC}/DomainModel/athlete.h ${SRC}/DomainModel/athlete.cpp
    ${SRC}/DomainModel/session.h ${SRC}/DomainModel/session.cpp   # the Athlete's sessions (step 3.4a)
    ${SRC}/DomainModel/shot.h ${SRC}/DomainModel/shot.cpp
    ${SRC}/Stores/athlete_store.cpp
    ${SRC}/Stores/session_store.cpp
    ${SRC}/DomainModel/session_identity.cpp
    ${SRC}/Stores/swing_paths.cpp
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
    # The Shots' recordings door: the Wrist reference ★ goes through the Session's Shot (step 5.7b). report_list_test's set.
    ${SRC}/Stores/swing_doc_writer.cpp ${SRC}/Stores/swing_store_repository.cpp
    ${SRC}/Stores/swing_doc_reader.cpp ${SRC}/Stores/swing_doc_analysis.cpp
    ${SRC}/Stores/recording_header.cpp ${SRC}/Analysis/effective_context.cpp
    ${SRC}/Stores/session_archiver.cpp)
pp_add_test(athlete_session_guard_test
    SOURCES athlete_session_guard_test.cpp athlete_test_library.h ${ATHLETE_TEST_SOURCES}
    INCLUDE ${GUI} ${GUI}/athlete ${SRC} ${SRC}/Core ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics ${SRC}/Analysis ${SRC}/LaunchMonitor
    LINK pp_vocabulary pp_platform    Qt6::Test
    AUTOMOC)
pp_add_test(athlete_repository_test
    SOURCES athlete_repository_test.cpp athlete_test_library.h ${ATHLETE_TEST_SOURCES}
    INCLUDE ${GUI} ${GUI}/athlete ${SRC} ${SRC}/Core ${SRC}/Diagnostics ${SRC}/Vocabulary/Metrics ${SRC}/Analysis ${SRC}/LaunchMonitor
    LINK pp_vocabulary pp_platform    Qt6::Test
    AUTOMOC)
