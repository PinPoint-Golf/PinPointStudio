# pp_domain — the Domain model ring (src/DomainModel: the athlete, the session and its shots, the Rig, the rule data,
# the session and athlete diagnostics), as one static library (#424). Defined once for both trees by
# pp_require_ring(domain) (cmake/PinPointRings.cmake). It links only pp_rules (and through it the inner rings) and Qt.
# AUTOMOC is on here, so its seven Q_OBJECT headers are moc'd once, in this library: no user lists them.

pp_require_ring(rules)

set(_d ${PP_RINGS_SRC}/DomainModel)
add_library(pp_domain STATIC
    ${_d}/athlete_diagnostics.cpp
    ${_d}/athlete.cpp
    ${_d}/rig.cpp
    ${_d}/rule_data.cpp
    ${_d}/session_diagnostics.cpp
    ${_d}/session_identity.cpp
    ${_d}/session.cpp
    ${_d}/shot.cpp
    ${_d}/athlete_diagnostics.h   # Q_OBJECT
    ${_d}/athlete.h   # Q_OBJECT
    ${_d}/rig.h   # Q_OBJECT
    ${_d}/rule_data.h   # Q_OBJECT
    ${_d}/session.h   # Q_OBJECT
    ${_d}/session_diagnostics.h   # Q_OBJECT
    ${_d}/shot.h   # Q_OBJECT
)
set_target_properties(pp_domain PROPERTIES AUTOMOC ON)
target_link_libraries(pp_domain PUBLIC pp_rules)

pp_ring_settings(pp_domain)
