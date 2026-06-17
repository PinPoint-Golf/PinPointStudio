# Linux In-App Update — Build Plan

*Phased plan to deliver the design in [`../design/linux_update.md`](../design/linux_update.md):
ship Linux as a signed, self-updating AppImage with a prompt-then-install flow in
Settings → General and on launch. Status legend: ☐ todo · ◑ in progress · ☑ done.*

The honest shape of this work: **the updater is the easy 20%; producing a
relocatable, signed AppImage from a tree that today runs out of `build/` is the
80%.** Phase P0 is the long pole and carries all the risk. Do it first and prove it
on a clean machine before writing a line of update code.

---

## Phase ordering & rationale

| Phase | Outcome | Risk |
|---|---|---|
| **P0** | A signed AppImage that launches on a clean Ubuntu with GPU + BLE + cameras working | **High** — native dep bundling |
| **P1** | `UpdateController` + QML wiring: check, prompt, download, progress, relaunch | Low |
| **P2** | Signature-pinning gate + GitHub Actions release pipeline | Medium — release key custody |
| **P3** | Polish: launch banner, skip-version, changelog render, channels | Low |

P1 can be prototyped against a stub AppImage in parallel with P0 (the in-app flow
is reviewable before bundling is perfect), but P0 must land before P2/release.

---

## P0 — AppImage packaging  ☐

Goal: `PinPointStudio-<tag>-x86_64.AppImage` runs on a stock Ubuntu LTS with
**no Qt/OpenCV/FFmpeg/ORT installed**, and BLE IMUs, V4L2 + industrial cameras,
and GPU inference all work.

- ☐ **AppDir assembly** via `linuxdeploy` + `linuxdeploy-plugin-qt` (Qt6 libs,
  QML modules, platform/multimedia/RHI plugins). Replace the dev
  `PinPointStudio.desktop` (currently hardcodes the `build/` exec path) with a
  relocatable one (`Exec=PinPointStudio`, icon from `src/Resources/icons`).
- ☐ **Bundle the heavy native deps not on a stock host:**
  - ORT (the GPU `.so`s CMake already fetches), **FFmpeg GPL build *with
    libx264*** (same trap as the documented `windeployqt` x264 issue — the
    bundled libav* must be the x264-capable build or swing export breaks),
    OpenCV, Aravis, espeak-ng, the bundled `yt-dlp`.
  - **CUDA/cuDNN runtime:** bundle the ORT-required CUDA 12 + cuDNN 9 `.so`s
    (large, ~1 GB) so GPU ORT works without the user installing a CUDA toolkit;
    require only the **NVIDIA driver** on the host. Confirm `ggml`/Vulkan path
    bundling too (Vulkan loader is on host; bundle `glslc` outputs not the SDK).
  - **Spinnaker** (proprietary): bundle its `.so`s if redistribution terms
    allow, else load-if-present and degrade — the device enumerator already
    tolerates absence.
- ☐ **`appimagetool` seal** with update info + sign flags:
  `appimagetool AppDir -u "gh-releases-zsync|PinPoint-Golf|PinPointStudio|latest|PinPointStudio-*-x86_64.AppImage.zsync" -s --sign-key <FPR>`
  → emits the `.AppImage`, `.AppImage.zsync`, embeds GPG sig + update info.
- ☐ **CMake:** a packaging target/script (`cmake/linux_appimage.cmake` or a
  `tools/package_appimage.sh`) that builds Release, runs linuxdeploy, copies the
  x264 FFmpeg over any Qt-bundled libav*, and runs appimagetool. Keep it out of
  the default build; invoke explicitly (mirrors how Windows deploy is a separate
  step).
- ◑ **Acceptance:** script now runs end-to-end on the dev host (Ubuntu 26.04) and
  produces a working AppImage + `.zsync` — it FUSE-mounts, runs from the bundle
  with zero missing libs, loads QML, stays alive headless. First-run fixes:
  libtiff.so.5 shim (26.04 Qt-installer), bundled offscreen plugin, `.zsync` CWD
  placement, QMAKE derivation, extract-and-run default. **Still pending:** a
  *Release* build (this was the Debug binary), and clean-VM validation of BLE +
  cameras + GPU (CUDA/Vulkan) + x264 export, + signature `validate`.

## P1 — UpdateController + QML  ☑ (built; compile + headless-load verified)

Goal: the full prompt-then-install flow against a real (or stub) AppImage feed.

- ☑ **Transport = `appimageupdatetool` via `QProcess`** (design §3 as-built) — **no**
  libappimageupdate submodule, **no** gpgme/libcurl/zsync2 build deps. Controller is
  pure Qt (Network + Core + Concurrent, already linked). The CLI binary is bundled in
  P0; when absent (dev / `$APPIMAGE` unset) the controller is inert (`isDevBuild`).
- ☑ **`src/Update/appimage_update.{h,cpp}`** — QProcess wrapper: `--check-for-update`,
  `--overwrite` on a working **copy** of `$APPIMAGE` (never the live file), stdout
  `NN%` progress parse, and the "never overwrite before verify" invariant.
- ☑ **`src/Update/update_controller.{h,cpp}`** — the state machine (design §5.1),
  `Q_PROPERTY`s (`state`, `available`, `supported`, `currentVersion`,
  `latestVersion`, `releaseNotes`, `progress`, `statusMessage`, `errorString`),
  invokables `checkNow()`, `download()`, `relaunch()`, `installOnNextLaunch()`.
  Takes `AppSettings*` + `SessionController*`. Includes the pinned-GPG signature
  gate (`verifySignatureBlocking()`, design §6) so `main` has no insecure
  intermediate — the gate refuses every update until the real key + fingerprint
  land in P2 (kPinnedKeyFpr is still all-zero = safe default). `skipVersion()` +
  the launch banner are deferred to P3 (they need the `skippedVersion` pref).
  - Feed query: async `QNetworkAccessManager` to
    `api.github.com/repos/PinPoint-Golf/PinPointStudio/releases` (parse
    `tag_name`/`body`/`assets`; newest non-draft, prereleases included per §7).
  - Verify off the GUI thread (`QtConcurrent`); the AppImageUpdater itself
    polls/copies off-thread, so no extra QTimer is needed.
  - Not an AppImage → `state == "devbuild"`; non-Linux → `"unsupported"`; both inert.
- ☑ **`main.cpp`:** constructed on **all** platforms (inert off-Linux) for QML
  uniformity, registered as `updateController`. (Deviation from the draft "Linux
  only" so the shared GeneralPanel binds without null-property errors.)
- ☑ **QML wiring (`GeneralPanel.qml`):** Version-row badge bound to
  `updateController.state` (up-to-date / available / downloading N% / verifying /
  restart-to-update / error / development-build), a contextual action chip
  (Check now → Download & install → Restart now), and a status/error line.
  The "Check for updates automatically" toggle is unchanged (gates the P3 launch
  check). Changelog disclosure deferred to P3.
- ◑ **Acceptance:** build + headless QML-load verified (no binding/controller
  errors; app stays alive). Live-feed acceptance (real release → offer → delta
  download → progress → relaunch) needs a published signed release + AppImage —
  gated on P0 clean-VM + P2 key, validated there.

## P2 — Signature gate + release pipeline  ◑ (built; awaits a first published release)

- ☑ **Release key DONE + OFFLINE:** ed25519 sign-only, passphrase-protected,
  fingerprint `C15A1C82…F0` pinned in `update_controller.cpp`; public half committed
  at `src/Resources/keys/pinpoint_release_pubkey.asc`. The **private key stays on the
  maintainer's machine — NOT a CI secret** (local-signing model; this key is the
  pinned root of trust for all auto-updates).
- ☑ **Verification gate (design §6):** `verifySignatureBlocking()` (shipped in P1 so
  `main` has no insecure intermediate). Ephemeral `GNUPGHOME`, `gpg --verify` of the
  detached sig, fingerprint **==** pin, off-thread; any failure discards → `Error`.
  Verified end-to-end with the real key (VALIDSIG accepted; tampered → BADSIG rejected).
- ☑ **Version from the AppImage asset filename, not the git tag** — tag format is
  release-process-constrained (`Version0-1-0-alpha1`); updater + CI are independent
  of it (`versionFromAssetName()`).
- ☑ **CI `.github/workflows/release.yml`** (tag push `v*`): builds the **unsigned**
  AppImage + zsync and stages a **draft** release. **No signing secrets** — the
  maintainer signs locally and publishes per the runbook. **Authored, not yet run.**
- ☑ **Release runbook** `docs/implementation/linux_release_runbook.md` — local-sign
  flow (Path A: CI draft → download → sign → upload `.sig` → publish; Path B: fully
  local build+sign+create). Covers the byte-match rule, never-publish-unsigned, and
  the post-publish check.
- ◑ **Acceptance:** publish a release per the runbook; a prior-version AppImage on a
  clean machine detects it, delta-downloads, **passes** the pinning gate, relaunches;
  a tampered/wrong-key AppImage is **rejected**. Blocked on a first published release.

## P3 — Polish  ☑ (built; compile + headless-load verified)

- ☑ **Launch banner (surface A):** `src/Gui/shell/PpUpdateBanner.qml` — non-modal,
  bottom-centred (stacked above the error toasts), What's new / Skip / Later /
  Install (and Restart now / Later when ready). Suppressed during a session, on the
  wizard (`allowed`), and for skipped versions. The launch check itself fires 4 s
  after startup, gated on `appSettings.checkForUpdates` (UpdateController ctor).
- ☑ **`skippedUpdateVersion` pref** added to `AppSettings` (persisted, `General/`
  group); `UpdateController::skipVersion()` records the offered version. A newer
  version overrides the skip (compared in the banner's `offerFresh`). Settings →
  General always still shows the offer.
- ☑ **Changelog render:** `releaseNotes` (GitHub release `body`) rendered as
  `Text.MarkdownText` in the banner's "What's new" disclosure (bounded + clipped).
- ☑ **SettingsIndex:** `setting_version` actions string extended with the new
  update verbs (done in P1).
- ☑ **Read-only-location handling** (design §8): the working copy is staged in the
  AppImage's own directory; if it isn't writable the copy fails and the controller
  surfaces an error rather than failing silently.
- ☐ **(Deferred) channels:** `appSettings.updateChannel` (Stable/Beta) filtering
  `prerelease`, landing with GA.

---

## Files touched

As built (no libappimageupdate submodule — QProcess path, design §3):
```
NEW  docs/design/linux_update.md                      (the contract)                       P0
NEW  docs/implementation/linux_update_impl.md         (this plan)                           P0
NEW  tools/package_appimage.sh                         (build → bundle → sign → zsync)      P0
NEW  src/Update/appimage_update.{h,cpp}                (appimageupdatetool QProcess driver) P1
NEW  src/Update/update_controller.{h,cpp}              (state machine + signature gate)     P1
NEW  src/Resources/keys/pinpoint_release_pubkey.asc    (pinned pubkey — PLACEHOLDER)        P1
NEW  src/Gui/shell/PpUpdateBanner.qml                  (launch banner)                      P3
NEW  .github/workflows/release.yml                     (tag-triggered: build unsigned draft) P2
NEW  docs/implementation/linux_release_runbook.md      (local-signing release runbook)      P2
EDIT CMakeLists.txt                                    (src/Update sources + :/keys + banner)P1/P3
EDIT src/Gui/main.cpp                                  (construct + register updateController)P1
EDIT src/Gui/shell/Main.qml                            (instantiate PpUpdateBanner)         P3
EDIT src/Gui/settings/GeneralPanel.qml                 (dynamic Version badge + actions)    P1
EDIT src/Gui/settings/SettingsIndex.qml                (search strings)                     P1
EDIT src/Gui/app/app_settings.h                        (skippedUpdateVersion pref)          P3
EDIT PinPointStudio.desktop                            (relocatable Exec/Icon)              P0
EDIT BUILDING.md                                       (Linux packaging + release section)  P0
```

## Risks & open questions

- **Transport (resolved):** the dev host has no gpgme/libcurl and no AppImage
  tooling, so embedding libappimageupdate was rejected for v1 in favour of bundling
  `appimageupdatetool` and driving it via `QProcess` (design §3). Same UX; the cost
  is doubled disk I/O (work on a copy) and stdout-parsed progress. Embedding remains
  a future option if tighter in-proc progress is wanted.
- **CUDA/cuDNN bundle size** (~1 GB+) inflates the AppImage; acceptable because
  zsync deltas make code-only updates tiny, but confirm GitHub Release asset size
  limits are fine (2 GB/asset is OK).
- **Spinnaker redistribution terms** — confirm before bundling; degrade-if-absent
  is the safe default.
- **Release key custody** — private key stays on the maintainer's machine (local
  signing); GitHub holds nothing secret. Back it up offline and document rotation. A
  leaked key is the one thing that breaks the §6 trust model; losing it means no
  verifiable updates until a new pinned key ships out-of-band.
- **Prerelease/`latest` interaction** (design §4.2/§7) — verify on a real test
  release that `gh-releases-zsync … latest` resolves while we publish non-prerelease.

## Done-when

A user on `v<n>` clicks **Download & install** (or accepts the launch banner),
watches a few-MB delta download, the signature verifies against the pinned key,
the app relaunches on `v<n+1>` — with no terminal, no Snap, no Flatpak, no root —
and the same flow exists on macOS (Sparkle) and Windows (WinSparkle).
