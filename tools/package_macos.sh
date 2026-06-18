#!/usr/bin/env bash
#
# package_macos.sh — build a relocatable, (optionally signed+notarized) .app inside
# a drag-install .dmg for PinPoint Studio (macOS, x86_64 / Intel for v1; runs under
# Rosetta 2 on Apple Silicon).  Companion to docs/design/macos_update.md and
# docs/implementation/macos_update_impl.md (Stage 1, S1·P0–P1).
#
# ┌─ STATUS ────────────────────────────────────────────────────────────────────┐
# │ The deploy → bundle → relocatable-verify → DMG recipe is exercised on a copy │
# │ of an existing .app (the smoke test).  A from-scratch Release build + clean- │
# │ second-Mac validation are the gates in the impl plan (S1·P0 acceptance).     │
# │ Code signing + notarization live in the (guarded) §5 block, which SKIPS when │
# │ no Developer ID identity is available — so this still emits an UNSIGNED DMG.  │
# └──────────────────────────────────────────────────────────────────────────────┘
#
# What it does:
#   1. Resolve the version from src/Core/version.h (single source of truth).
#   2. Build a Release tree (or reuse one / an already-built .app via --app).
#   3. macdeployqt: bundle Qt frameworks + QML modules + plugins, relinking the
#      app's Homebrew dylibs (OpenCV / FFmpeg / Aravis, all under /usr/local/opt)
#      into Contents/Frameworks with @rpath install names.
#   4. Bundle the deps macdeployqt misses — the @rpath ONNX Runtime dylib (from the
#      build's _deps tree) and, if present, Spinnaker (dlopen'd, so not in NEEDED).
#   5. VERIFY relocatability: no Mach-O in the bundle may still reference an absolute
#      /usr/local or /opt/homebrew path (the clean-host gate).
#   6. (§5, guarded) codesign --options runtime + notarytool + stapler.
#   7. Assemble PinPointStudio-<ver>-x86_64.dmg (the .app + an /Applications symlink).
#
# Usage:
#   tools/package_macos.sh [--no-build] [--app <prebuilt.app>] [--no-sign]
#     --no-build        reuse an existing $BUILD_DIR (skip configure+build)
#     --app <path>      package an already-built .app (COPIED first — never mutated);
#                       implies --no-build. Used by the smoke test / CI artifact reuse.
#     --no-sign         force-skip the codesign/notarize block even if a cert exists
#
# Key environment variables (override per host):
#   CMAKE_PREFIX       Qt6 prefix (default: ~/Qt/6.11.0/macos)
#   JOBS               build parallelism (default 6 — this Mac OOMs above ~6)
#   SIGN_IDENTITY      "Developer ID Application: …" codesign identity (§5; auto-detected if unset)
#   NOTARY_PROFILE     notarytool keychain profile name (§5; required to notarize)
#
set -euo pipefail

# ── repo + output layout ───────────────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# NB: a dedicated release tree — NEVER build/Qt_6_11_0_for_macOS-Debug (that is
# QtCreator's; macdeployqt mutates the .app in place, which would corrupt it).
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/macos-release}"
WORK_DIR="${WORK_DIR:-$REPO_ROOT/build/macos-pkg}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"
CMAKE_PREFIX="${CMAKE_PREFIX:-$HOME/Qt/6.11.0/macos}"
JOBS="${JOBS:-6}"
GH_OWNER="PinPoint-Golf"
GH_REPO="PinPointStudio"

DO_BUILD=1
DO_SIGN=1
PREBUILT_APP=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) DO_BUILD=0 ;;
        --no-sign)  DO_SIGN=0 ;;
        --app)      PREBUILT_APP="${2:-}"; DO_BUILD=0; shift ;;
        *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

log()  { printf '\033[1;36m[macpkg]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[macpkg] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# List every Mach-O we relocate: the main executable + all dylibs/Qt plugins in the
# bundle (plugins are .dylib on macOS). Re-evaluated each pass so libs copied in one
# pass get processed in the next. Qt .framework binaries link only Qt/system, never
# Homebrew, so they need no scanning here.
list_macho() {
    find "$STAGE_APP" -type f -name '*.dylib' 2>/dev/null
    [[ -f "$STAGE_APP/Contents/MacOS/PinPointStudio" ]] && echo "$STAGE_APP/Contents/MacOS/PinPointStudio"
}

# Recursively rewrite every absolute Homebrew dependency in the bundle to @rpath and
# pull the referenced dylib into Frameworks. macdeployqt does this for the top level
# but leaves deep transitive trees half-done (OpenCV → VTK → webp; the Qt sql plugin
# → libmimerapi), so the bundle still points at /usr/local on a clean Mac. Fixpoint:
# re-scan after each pass until a pass copies nothing new.
relocate_closure() {
    local fw="$STAGE_APP/Contents/Frameworks"
    mkdir -p "$fw"
    local pass=0 changed=1
    while [[ "$changed" == 1 ]]; do
        changed=0; pass=$((pass+1))
        local macho dep base copied=0
        while IFS= read -r macho; do
            [[ -f "$macho" ]] || continue
            chmod u+w "$macho" 2>/dev/null || true
            if [[ "$macho" == "$fw/"* ]]; then
                # A lib in Frameworks needs @loader_path so @rpath/<sibling> resolves.
                if ! otool -l "$macho" 2>/dev/null | grep -q 'path @loader_path '; then
                    install_name_tool -add_rpath "@loader_path" "$macho" 2>/dev/null || true
                fi
                # …and its own install id must be @rpath, not the build-machine path
                # macdeployqt left behind (-change can't touch the id; -id can).
                case "$(otool -D "$macho" 2>/dev/null | tail -n +2 | head -1)" in
                    /usr/local/*|/opt/homebrew/*)
                        install_name_tool -id "@rpath/$(basename "$macho")" "$macho" 2>/dev/null || true ;;
                esac
            fi
            while IFS= read -r dep; do
                case "$dep" in
                    /usr/local/*|/opt/homebrew/*)
                        base="$(basename "$dep")"
                        install_name_tool -change "$dep" "@rpath/$base" "$macho" 2>/dev/null || true
                        if [[ ! -f "$fw/$base" && -f "$dep" ]]; then
                            cp -L "$dep" "$fw/$base" && chmod u+w "$fw/$base"
                            install_name_tool -id "@rpath/$base" "$fw/$base" 2>/dev/null || true
                            changed=1; copied=$((copied+1))
                        fi ;;
                esac
            done < <(otool -L "$macho" 2>/dev/null | tail -n +2 | awk '{print $1}')
        done < <(list_macho)
        log "relocate pass $pass — pulled $copied new lib(s) into Frameworks"
        [[ $pass -ge 15 ]] && { log "WARN: relocate did not converge in 15 passes"; break; }
    done
}

MACDEPLOYQT="${MACDEPLOYQT:-$CMAKE_PREFIX/bin/macdeployqt}"

# ── 0. tool preflight ───────────────────────────────────────────────────────────
[[ -x "$MACDEPLOYQT" ]] || die "macdeployqt not found at $MACDEPLOYQT (set CMAKE_PREFIX)"
for t in hdiutil otool install_name_tool; do
    have "$t" || die "required tool '$t' not on PATH"
done

# ── 1. version (single source of truth: src/Core/version.h) ──────────────────────
ver_h="$REPO_ROOT/src/Core/version.h"
maj=$(sed -n 's/^#define PINPOINT_VERSION_MAJOR[[:space:]]*\([0-9]*\).*/\1/p' "$ver_h")
min=$(sed -n 's/^#define PINPOINT_VERSION_MINOR[[:space:]]*\([0-9]*\).*/\1/p' "$ver_h")
pfx=$(sed -n 's/^#define PINPOINT_VERSION_POSTFIX[[:space:]]*"\([^"]*\)".*/\1/p' "$ver_h")
VERSION="v${maj}.${min}${pfx}"
[[ -n "$maj" && -n "$min" ]] || die "could not parse version from $ver_h"
DMG_NAME="PinPointStudio-${VERSION}-x86_64.dmg"
log "version: $VERSION   target: $GH_OWNER/$GH_REPO   dmg: $DMG_NAME"

# ── 2. obtain the .app (build Release, reuse, or copy a prebuilt one) ─────────────
mkdir -p "$WORK_DIR"
STAGE_APP="$WORK_DIR/PinPointStudio.app"
rm -rf "$STAGE_APP"

if [[ -n "$PREBUILT_APP" ]]; then
    [[ -d "$PREBUILT_APP" ]] || die "--app path is not a bundle: $PREBUILT_APP"
    log "copying prebuilt bundle (source is never mutated): $PREBUILT_APP"
    cp -a "$PREBUILT_APP" "$STAGE_APP"
else
    if [[ "$DO_BUILD" == 1 ]]; then
        log "configuring Release in $BUILD_DIR (PINPOINT_INSTALLED=ON)"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
              -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX" \
              -DPINPOINT_INSTALLED=ON
        log "building (-j$JOBS)"
        cmake --build "$BUILD_DIR" --parallel "$JOBS"
    else
        log "skipping build — reusing $BUILD_DIR (--no-build)"
    fi
    SRC_APP="$BUILD_DIR/PinPointStudio.app"
    [[ -d "$SRC_APP" ]] || die "no .app at $SRC_APP (build failed, or pass --app)"
    cp -a "$SRC_APP" "$STAGE_APP"
fi

# ── 3. macdeployqt: Qt frameworks + QML + plugins + Homebrew dylib relocation ─────
# macdeployqt copies the app's externally-linked dylibs (incl. /usr/local/opt
# OpenCV/FFmpeg/Aravis) into Contents/Frameworks and rewrites their install names to
# @rpath, adding an @executable_path/../Frameworks LC_RPATH to the main binary.
log "running macdeployqt"
"$MACDEPLOYQT" "$STAGE_APP" -qmldir="$REPO_ROOT/src" -verbose=1

FW="$STAGE_APP/Contents/Frameworks"
MACOS="$STAGE_APP/Contents/MacOS"
mkdir -p "$FW"

# ── 4. bundle the deps macdeployqt misses ─────────────────────────────────────────
# 4a. ONNX Runtime — linked as @rpath/libonnxruntime.<ver>.dylib (so macdeployqt may
#     not resolve it). Copy from the build's _deps tree into Frameworks; the
#     @executable_path/../Frameworks rpath macdeployqt added then resolves it.
if ! ls "$FW"/libonnxruntime*.dylib >/dev/null 2>&1; then
    ort=""
    for cand in "$BUILD_DIR"/_deps/onnxruntime-*/lib/libonnxruntime.*.dylib \
                "$REPO_ROOT"/build/*/_deps/onnxruntime-*/lib/libonnxruntime.*.dylib; do
        [[ -f "$cand" ]] && { ort="$cand"; break; }
    done
    if [[ -n "$ort" ]]; then
        log "bundling ONNX Runtime: $(basename "$ort")"
        cp -L "$ort" "$FW/"
        base="$(basename "$ort")"
        chmod u+w "$FW/$base"
        install_name_tool -id "@rpath/$base" "$FW/$base" 2>/dev/null || true
    else
        log "WARN: ONNX Runtime dylib not found under _deps — GPU/CPU inference will fail to load"
    fi
fi

# 4b. Spinnaker (proprietary, dlopen'd → not in NEEDED, so macdeployqt skips it).
#     Bundle only if present AND redistribution terms allow; the device enumerator
#     already degrades gracefully when absent.
if [[ -n "${SPINNAKER_DIR:-}" ]]; then
    log "bundling Spinnaker SDK from $SPINNAKER_DIR (confirm redistribution terms!)"
    for s in "$SPINNAKER_DIR"/lib/libSpinnaker*.dylib; do
        [[ -f "$s" ]] && cp -L "$s" "$FW/" || true
    done
else
    log "Spinnaker not bundled (SPINNAKER_DIR unset) — industrial cameras degrade to absent"
fi

# 4c. yt-dlp (Film tab shells out to it). CMake stages yt-dlp_macos next to the build
#     binary; carry it into the bundle if present.
for y in "$BUILD_DIR/yt-dlp_macos" "$MACOS/yt-dlp_macos" "$REPO_ROOT/tools/yt-dlp_macos"; do
    [[ -f "$y" && ! -f "$MACOS/yt-dlp_macos" ]] && { cp "$y" "$MACOS/yt-dlp_macos"; chmod +x "$MACOS/yt-dlp_macos"; log "bundled yt-dlp_macos"; }
done

# 4d. Recursively relocate the Homebrew closure macdeployqt left half-done.
log "relocating Homebrew dependency closure (OpenCV/FFmpeg/Aravis + transitive deps)"
relocate_closure

# ── 5. VERIFY relocatability — the clean-host gate ────────────────────────────────
# No bundled Mach-O may still point at a build-machine-only absolute path. If any do,
# the app will crash with "image not found" on a stock Mac. Fail loudly here.
log "verifying relocatability (no /usr/local or /opt/homebrew references)"
bad=0
while IFS= read -r macho; do
    [[ -f "$macho" ]] || continue
    if otool -L "$macho" 2>/dev/null | tail -n +2 | grep -E '^[[:space:]]+(/usr/local|/opt/homebrew)' >/dev/null; then
        printf '\033[1;31m  UNRELOCATED:\033[0m %s\n' "${macho#"$STAGE_APP"/}"
        otool -L "$macho" 2>/dev/null | tail -n +2 | grep -E '^[[:space:]]+(/usr/local|/opt/homebrew)' | sed 's/^/      /'
        bad=1
    fi
done < <(list_macho)
if [[ "$bad" == 1 ]]; then
    die "bundle still references build-machine paths (see UNRELOCATED above) — would crash on a clean Mac"
fi
log "relocatability OK — bundle is self-contained"

# ── 6. code signing + notarization (cred-guarded) ─────────────────────────────────
# Developer ID codesign (Hardened Runtime) → notarytool → staple. Each stage SKIPS
# with a clear message when its credential is absent, so auto/dev runs still emit an
# unsigned DMG. Real signing needs SIGN_IDENTITY (a "Developer ID Application" cert in
# the keychain) and NOTARY_PROFILE (a `notarytool store-credentials` profile). The DMG
# itself is signed+stapled in §7 after it is built (the notarization unit on macOS is
# the disk image the user downloads). docs/design/macos_update.md §6.
ENTITLEMENTS="$REPO_ROOT/packaging/macos/entitlements.plist"

detect_identity() {   # echo a Developer ID Application identity if one exists, else nothing
    [[ -n "${SIGN_IDENTITY:-}" ]] && { echo "$SIGN_IDENTITY"; return; }
    security find-identity -v -p codesigning 2>/dev/null \
        | sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' | head -1
}

codesign_bundle() {
    local identity; identity="$(detect_identity)"
    if [[ -z "$identity" ]]; then
        log "SKIP codesign — no 'Developer ID Application' identity in the keychain (set up the cert, or this stays UNSIGNED)"
        return 1
    fi
    [[ -f "$ENTITLEMENTS" ]] || die "entitlements not found: $ENTITLEMENTS"
    log "codesigning with: $identity"
    # Sign inside-out: every nested dylib/framework/helper/plugin first, the .app last.
    # --options runtime (Hardened Runtime) + --timestamp are notarization prerequisites.
    while IFS= read -r f; do
        codesign --force --options runtime --timestamp \
                 --sign "$identity" "$f" 2>/dev/null || die "codesign failed: ${f#"$STAGE_APP"/}"
    done < <( { find "$STAGE_APP/Contents/Frameworks" -type f \( -name '*.dylib' -o -name '*.framework' \) 2>/dev/null
               find "$STAGE_APP/Contents/Frameworks" -type d -name '*.framework' 2>/dev/null
               find "$STAGE_APP/Contents/PlugIns" "$STAGE_APP/Contents/Resources" -type f -name '*.dylib' 2>/dev/null
               [[ -f "$STAGE_APP/Contents/MacOS/yt-dlp_macos" ]] && echo "$STAGE_APP/Contents/MacOS/yt-dlp_macos"; } | sort -u )
    # The app last, with entitlements — its seal covers the now-signed contents.
    codesign --force --options runtime --timestamp --entitlements "$ENTITLEMENTS" \
             --sign "$identity" "$STAGE_APP" || die "codesign failed: app bundle"
    codesign --verify --deep --strict --verbose=1 "$STAGE_APP" || die "codesign verification failed"
    log "codesign OK"
    return 0
}

# Notarize + staple a built artifact (the DMG). Returns non-zero (skips) without creds.
notarize_and_staple() {
    local artifact="$1"
    if [[ -z "${NOTARY_PROFILE:-}" ]]; then
        log "SKIP notarize — NOTARY_PROFILE unset (run: xcrun notarytool store-credentials)"
        return 1
    fi
    log "submitting $(basename "$artifact") to notarytool (waits for Apple)…"
    xcrun notarytool submit "$artifact" --keychain-profile "$NOTARY_PROFILE" --wait \
        || die "notarization failed (see: xcrun notarytool log)"
    log "stapling notarization ticket"
    xcrun stapler staple "$artifact" || die "stapler failed"
    xcrun stapler validate "$artifact" || die "stapler validate failed"
    log "notarize + staple OK"
}

SIGNED=0
if [[ "$DO_SIGN" == 1 ]]; then
    codesign_bundle && SIGNED=1
else
    log "signing skipped (--no-sign)"
fi

# ── 7. assemble the drag-install DMG ──────────────────────────────────────────────
mkdir -p "$DIST_DIR"
out="$DIST_DIR/$DMG_NAME"
rm -f "$out"
stage="$WORK_DIR/dmg-stage"
rm -rf "$stage"; mkdir -p "$stage"
cp -a "$STAGE_APP" "$stage/PinPointStudio.app"
ln -s /Applications "$stage/Applications"     # drag-to-install affordance
log "building DMG → $out"
hdiutil create -volname "PinPoint Studio $VERSION" \
    -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$out" >/dev/null
rm -rf "$stage"

# Sign + notarize the DMG itself (the artifact the user downloads). Both skip cleanly
# without creds, leaving a valid unsigned DMG.
if [[ "$SIGNED" == 1 ]]; then
    identity="$(detect_identity)"
    log "codesigning the DMG"
    codesign --force --timestamp --sign "$identity" "$out" || die "DMG codesign failed"
    notarize_and_staple "$out" || log "DMG left un-notarized (no NOTARY_PROFILE)"
fi

log "done."
[[ "$SIGNED" == 1 ]] && log "bundle: SIGNED (Developer ID)" || log "bundle: UNSIGNED (no cert — dev/auto build)"
ls -lh "$out"
cat <<EOF

Next (S1·P1/P2, maintainer gates): codesign + notarize this .app, generate the
EdDSA-signed appcast-mac.xml (packaging/make_appcast_mac.sh), and publish both to a
GitHub Release tagged '${VERSION}' on ${GH_OWNER}/${GH_REPO}
(docs/implementation/macos_release_runbook.md).
EOF
