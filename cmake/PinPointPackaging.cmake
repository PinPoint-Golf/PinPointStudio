# ─────────────────────────────────────────────────────────────────────────────
# Windows installer packaging — CPack + Inno Setup generator
#
# Produces a per-user, no-admin installer (PinPointStudioSetup-<ver>.exe) with a
# Start Menu entry, optional desktop shortcut, and an uninstaller. The install
# tree is populated by the install() rules in the top-level CMakeLists.txt:
#   • component "core" — app, Qt (via windeployqt), ONNX Runtime, OpenCV, FFmpeg,
#     models, espeak-ng data, yt-dlp. (The Spinnaker SDK is delay-loaded and
#     discovered at runtime — never bundled; see the Spinnaker block in CMakeLists.txt.)
#   • component "cuda" — NVIDIA toolkit + cuDNN runtime (GPU acceleration).
#
# Build the installer (Release) — see packaging/build_installer.ps1:
#   cmake --build <build> --config Release --target PinPointStudio
#   cpack --config <build>/CPackConfig.cmake -G INNOSETUP
#
# Requires Inno Setup 6 (ISCC.exe) on PATH or via CPACK_INNOSETUP_EXECUTABLE.
# ─────────────────────────────────────────────────────────────────────────────

# Packaging is Windows-only here; on other platforms this is a no-op.
if(NOT WIN32)
    return()
endif()

set(CPACK_GENERATOR "INNOSETUP")

set(CPACK_PACKAGE_NAME              "PinPoint Studio")
set(CPACK_PACKAGE_VENDOR            "Mark Liversedge")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PinPoint Studio — golf swing capture & analysis")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PinPointStudio")
set(CPACK_PACKAGE_FILE_NAME         "PinPointStudioSetup-${PROJECT_VERSION}")

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

# Start Menu / desktop shortcuts. "<exe-name>;<label>" — the exe name has no .exe.
set(CPACK_PACKAGE_EXECUTABLES "PinPointStudio;PinPoint Studio")
set(CPACK_CREATE_DESKTOP_LINKS "PinPointStudio")

# ── Components ────────────────────────────────────────────────────────────────
set(CPACK_COMPONENTS_ALL core cuda)
set(CPACK_COMPONENT_CORE_DISPLAY_NAME "PinPoint Studio (required)")
set(CPACK_COMPONENT_CORE_DESCRIPTION
    "The application and all CPU/cloud functionality.")
set(CPACK_COMPONENT_CORE_REQUIRED TRUE)
set(CPACK_COMPONENT_CUDA_DISPLAY_NAME "NVIDIA GPU acceleration")
set(CPACK_COMPONENT_CUDA_DESCRIPTION
    "NVIDIA CUDA + cuDNN runtime (large) for local GPU acceleration of speech and "
    "pose models. Safe to skip on machines without an NVIDIA GPU — the app falls "
    "back to CPU/cloud. Requires an NVIDIA driver at runtime regardless.")

# ── Inno Setup generator specifics ───────────────────────────────────────────
set(CPACK_INNOSETUP_ARCHITECTURE "x64")

# Per-user install: no admin / no UAC, so a future in-app auto-update can replace
# files in place. With PrivilegesRequired=lowest, Inno's default install root
# {autopf} resolves to %LOCALAPPDATA%\Programs — so we do NOT set
# CPACK_INNOSETUP_INSTALL_ROOT (a literal "{localappdata}\Programs" round-trips
# through CPackConfig.cmake as an invalid "\P" escape → CMP0010 warning).
set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")

# Stable application identity — drives upgrade/uninstall detection and the future
# auto-update appcast. NEVER change this GUID. (Leading "{{" is Inno's escape for
# a literal "{" in the [Setup] AppId directive.)
set(CPACK_INNOSETUP_SETUP_AppId "{{B7E5B0A1-3C2D-4F8E-9A6B-1F0C5D4E3A2B}")

# Single-instance/updater mutex — must match the name created in src/Gui/main.cpp.
# Lets the installer detect a running instance and close it for in-place updates.
set(CPACK_INNOSETUP_SETUP_AppMutex "PinPointStudio.SingleInstance.Mutex")

set(CPACK_INNOSETUP_PROGRAM_MENU_FOLDER "PinPoint Studio")
set(CPACK_INNOSETUP_CREATE_UNINSTALL_LINK ON)
set(CPACK_INNOSETUP_RUN_EXECUTABLES "PinPointStudio")

if(EXISTS "${CMAKE_SOURCE_DIR}/src/Resources/icons/pinpointstudio.ico")
    set(CPACK_INNOSETUP_ICON_FILE "${CMAKE_SOURCE_DIR}/src/Resources/icons/pinpointstudio.ico")
endif()

include(CPack)
