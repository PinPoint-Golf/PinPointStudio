# Linux Release Runbook — cut & sign an AppImage release

How to ship a Linux AppImage release that the in-app updater will trust. **The
release signing key never leaves your machine** ([BT-32](../controls/release.md)) — GitHub only ever sees the
public key (committed) and the produced `.sig`. Design: [`linux_update.md`](../../docs/design/linux_update.md).
The rules these steps carry out are the release controls ([`controls/release.md`](../controls/release.md));
a step cites the control's id.

## Facts you need
- **Repo:** `PinPoint-Golf/PinPointStudio` · releases are the updater's feed.
- **Signing key:** ed25519, sign-only, **passphrase-protected**, fingerprint
  **`C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0`** (the public fingerprint the app pins) — the
  private key lives only in your local GPG keyring, backed up offline ([BT-32](../controls/release.md)).
- **Version source of truth:** `src/Core/version.h` → e.g. `v0.1-alpha1`.
  `package_appimage.sh` embeds it in the **AppImage filename**, which is what the
  updater compares. **The git tag is free-form** (GitHub-constrained, e.g.
  `Version0-1-0-alpha1`) — the updater ignores it.
- **A release is live to users only when it is:** published (non-draft) **and**
  non-prerelease **and** carries all three assets: `*.AppImage`, `*.AppImage.zsync`,
  `*.AppImage.sig` ([BT-29](../controls/release.md)). The gate refuses an unsigned release, and drafts/prereleases are
  invisible to the updater — so an in-progress release can't reach users by accident.

## One-time setup
- `gh auth login` (the [GitHub CLI](https://cli.github.com/)).
- Signing key present: `gpg --list-secret-keys C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0`.
- Build tooling installed (see [`../../BUILDING.md`](../../BUILDING.md) → *Packaging & Release*):
  `linuxdeploy`, `linuxdeploy-plugin-qt`, `appimagetool`, `appimageupdatetool`,
  `zsync`, `libfuse2t64`.

---

## 0. Bump the version
Edit `src/Core/version.h` (`PINPOINT_VERSION_MAJOR/MINOR/POSTFIX`), commit, push.
The new version is **strictly newer** than what's installed in the wild ([BT-24](../controls/release.md)), or
the updater won't offer it.

## 0.5 Run the full test suite — ALL must pass (MANDATORY GATE)
This is the gate [BT-17](../controls/release.md) requires, run as [BT-25](../controls/release.md) says. PinPoint's unit
tests are *not* part of the app build — they are nine standalone CTest suites that the
`tests/` umbrella configures, builds and runs as a single registry (see
[`../../BUILDING.md`](../../BUILDING.md) § Testing). No `OpenCV_DIR` is needed: the first
configure builds the pinned OpenCV from source (`cmake/PinPointOpenCV.cmake`):
```bash
QT=$(ls -d ~/Qt/6.11.*/gcc_64 | tail -1)        # the newest Qt 6.11 kit installed
JOBS="${JOBS:?set JOBS to the machine's build-job limit}"   # from its local file (BT-1); never a bare -j
rm -rf build/tests \
  && cmake -S tests -B build/tests -DCMAKE_PREFIX_PATH="$QT" \
  && cmake --build build/tests -j "$JOBS" \
  && ctest --test-dir build/tests --output-on-failure -j "$JOBS" -LE qml_ui \
  && ctest --test-dir build/tests --output-on-failure -j "$JOBS" -L qml_ui --no-tests=error \
  || echo "❌ RELEASE BLOCKED"
```
The last line must read `100% tests passed, 0 tests failed out of N` (N grows as suites
and tests are added: 121 as of v0.1-alpha11). If the umbrella fails to configure or build,
or any test fails, stop ([BT-17](../controls/release.md)): fix it and re-run before you tag. Do not proceed to
the build below.

> The `rm -rf build/tests` and the umbrella are [BT-25](../controls/release.md), not housekeeping. The umbrella
> works because `tests/CMakeLists.txt` enumerates the suites, so one added later is picked
> up automatically, and it builds the `pinpoint_buffer` library first and then pulls in
> `src/Buffer/tests`. The job limit is [BT-1](../controls/build.md): a bare `-j` is unbounded.

## Build, sign and upload the Linux build
CI is not the release route ([BT-10](../controls/release.md)): the key and the build are both on your machine. The
release carries all four builds ([BT-29](../controls/release.md)); this is the Linux one.

```bash
TAG=<the release tag>      # free-form; the same tag on all four build machines
export CMAKE_PREFIX=$(ls -d ~/Qt/6.11.*/gcc_64 | tail -1)
export SIGN_KEY=C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0
# Optional GPU bundle (else CPU-only). Spinnaker optional.
# The CUDA and cuDNN library folders are named in the machine's local file.
export CUDA_LIB_DIR=<the CUDA lib folder> CUDNN_LIB_DIR=<the cuDNN lib folder>
tools/package_appimage.sh            # builds + signs all three (agent prompts for passphrase)
AI=$(ls -t dist/PinPointStudio-*-x86_64.AppImage | head -1)

# Verify locally exactly as the app will (ephemeral keyring + pinned fingerprint; BT-28).
H=$(mktemp -d); PIN=C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0
gpg --homedir "$H" --batch --import src/Resources/keys/pinpoint_release_pubkey.asc
gpg --homedir "$H" --batch --status-fd 1 --verify "$AI.sig" "$AI" \
  | grep -q "VALIDSIG $PIN" && echo "✅ verifies" || echo "❌ DO NOT PUBLISH"
rm -rf "$H"

# The four builds meet on one draft (BT-29). No draft yet: this machine is the first to finish.
gh release view "$TAG" -R PinPoint-Golf/PinPointStudio >/dev/null 2>&1 || \
  gh release create "$TAG" -R PinPoint-Golf/PinPointStudio --draft \
    --title "PinPoint Studio $TAG" --notes "…release notes (shown in the in-app banner)…"
gh release upload "$TAG" -R PinPoint-Golf/PinPointStudio --clobber \
  "$AI" "$AI.zsync" "$AI.sig"
```
> Scripted/non-interactive signing: set `GPG_PASSPHRASE=…` and the script feeds it
> via gpg loopback. Prefer the interactive agent prompt when you can (keeps the
> passphrase out of your shell/history). To sign by hand instead of through the script:
> `gpg --local-user C15A1C82CE718ED190B7C3C00F677C2EDA4F7BF0 --output "$AI.sig" --detach-sign "$AI"`.

## Publish, once all four builds are on the draft
Only the last machine to finish publishes, after checking the asset list ([BT-29](../controls/release.md)):
```bash
gh release view "$TAG" -R PinPoint-Golf/PinPointStudio --json assets --jq '.assets[].name'
# expect all four builds: .AppImage, .AppImage.zsync, .AppImage.sig (Linux); -core.exe, -cuda.exe,
# appcast-win.xml (Windows); the x86_64 DMG + appcast-mac.xml; the arm64 DMG + appcast-mac-arm64.xml
gh release edit "$TAG" -R PinPoint-Golf/PinPointStudio --draft=false --prerelease=false
```

---

## 1. Post-publish check
```bash
gh release view "$TAG" -R PinPoint-Golf/PinPointStudio --json assets \
  --jq '.assets[].name'      # expect: .AppImage, .AppImage.zsync, .AppImage.sig
```
End-to-end: run an *older* installed AppImage → Settings → General → **Check now**
(or wait for the 4 s launch check) → it should offer the new version, download the
delta, verify, and relaunch.

## Gotchas
- **Sign the bytes you publish** ([BT-28](../controls/release.md)): the AppImage you upload, never a
  rebuild (zsync won't match either).
- **Never publish without the `.sig`** ([BT-29](../controls/release.md)). Clients fail the gate and surface an
  error. Keep it a draft until the signature is uploaded.
- **Every release carries all four builds** ([BT-29](../controls/release.md)), the Linux AppImage among
  them; the Linux updater looks only at its own assets.
- **Rollback:** delete or re-draft the release (`gh release delete <tag>` /
  `gh release edit <tag> --draft=true`); the updater stops offering it immediately.
