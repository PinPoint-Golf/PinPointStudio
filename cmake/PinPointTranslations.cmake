# The interface's translation catalogues, and the ONE list of them.
#
# A language is offered in Settings → General → Language by src/Gui/settings/GeneralPanel.qml and
# becomes real only when its catalogue is in this list: the app loads
# ":/i18n/PinPointStudio_<tag>.qm" for the tag the setting holds (src/Gui/app/app_translations.h),
# and a tag with no catalogue leaves the interface in English with a warning in the log.
#
# The list is consumed twice, and must stay one list:
#
#   the app   — qt_add_translations(TARGETS PinPointStudio ...) in the root CMakeLists, which
#               compiles each .ts with lrelease and embeds the .qm at :/i18n
#   the tests — app_translations_test (src/Gui/tests) compiles the SAME files into itself, loads
#               each one through the app's loader and checks every catalogue for the things a
#               translation can break without anything failing to build: a placeholder dropped, a
#               phrase two contexts must agree on translated two ways
#
# The file name is the contract: PinPointStudio_<tag>.ts, where <tag> is exactly the tag the
# Language combo stores ("zh_CN"). The source language is British English and has no catalogue.
#
# Paths are relative to the repo root; each consumer prefixes them.
# docs/developer/translations_developer_guide.md says how to add a language and refresh these.

set(PP_TS_FILES
    src/Resources/i18n/PinPointStudio_zh_CN.ts
)

# Sources with translatable strings that the app compiles on only SOME platforms or configurations.
# `update_translations` reads the target's sources, and the target on this machine does not have the
# other platforms' files — so a refresh on Windows would find the AppImage updater's strings gone,
# mark their translations vanished, and the next Linux build would ship them in English. Naming them
# here hands them to lupdate everywhere. A file belongs in this list when it carries tr() and its
# line in the root CMakeLists sits inside an if().
set(PP_TS_EXTRA_SOURCES
    src/Ppcp/ppcp_host_service.cpp          # if(PP_OPENSSL_FOUND AND TARGET ppcp)
    src/Ppcp/ppcp_import_controller.cpp     # if(TARGET ppcp)
    src/Ppcp/ppcp_offer_controller.cpp      # if(TARGET ppcp)
    src/Update/appimage_update.cpp          # Linux
    src/Update/linux_appimage_backend.cpp   # Linux
    src/Video/VideoInputApple.mm            # macOS
    src/Video/VideoInputSpinnaker.cpp       # Windows, with the Spinnaker SDK
)

foreach(_pp_ts IN LISTS PP_TS_FILES)
    if(NOT _pp_ts MATCHES "/PinPointStudio_[A-Za-z]+(_[A-Za-z0-9]+)*\\.ts$")
        message(FATAL_ERROR "PinPointTranslations.cmake: ${_pp_ts} is not named PinPointStudio_<tag>.ts — the app would never load it")
    endif()
endforeach()
