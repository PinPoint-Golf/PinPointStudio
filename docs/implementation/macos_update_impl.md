# macOS In-App Update — Build Plan

*Phased plan to deliver the design in
[`../design/macos_update.md`](../design/macos_update.md): ship the macOS build as a
Sparkle-driven, EdDSA-signed, **Developer ID-notarized**, prompt-then-install
auto-update over GitHub Releases. Status legend: ☐ todo · ◑ in progress · ☑ done.
Nothing here is built yet — macOS currently falls through `update_controller.cpp`'s
inert `#else` branch (`State::Unsupported`).*

The work splits into the **two stages the brief asked for**:

- **Stage 1 — Package, sign & publish.** Turn the build tree (which today runs out of
  `build/Qt_…-Debug/PinPointStudio.app`) into a distributable, **Developer
  ID-signed + notarized `.dmg`**, with EdDSA signing, an appcast, and a GitHub
  Releases publish step. This is the macOS **80%** — the analogue of "produce a
  relocatable signed AppImage" on Linux. No Sparkle client code is needed to prove
  Stage 1: a notarized DMG that installs cleanly on a second Mac is the gate.
- **Stage 2 — Spot & auto-update.** Embed Sparkle, wire the `UpdateController` macOS
  branch (a thin façade — Sparkle does the download/verify/UI/relaunch we hand-rolled
  on Linux), add the Info.plist feed + pinned key, the session-safety delegate, and the
  Settings reuse. This is the **easy 20%** — structurally a copy of the existing Windows
  WinSparkle branch.

The honest shape, vs the other platforms: **on Windows the packaging 80% was already
done** (the Inno installer pre-existed); **on macOS it is not** — there is no
notarized DMG today, and Gatekeeper makes signing a hard prerequisite, not GA polish
(design §6). So unlike Windows, **Stage 1 is the long pole and carries the risk.** Do
it first and prove a notarized DMG installs on a clean Mac before writing Sparkle code.

---

## Phase ordering & rationale

| Stage / Phase | Outcome | Risk |
|---|---|---|
| **S1·P0** | `macdeployqt`'d, relocatable `PinPointStudio.app` + `.dmg` that launches on a clean Mac (Qt/ORT/OpenCV/FFmpeg all bundled, BLE + camera + STT work) | **High** — bundling + framework rpaths |
| **S1·P1** | Developer ID codesign + notarize + staple the DMG; version unification (Info.plist ← version.h) | **High** — signing identity, notarytool, entitlements |
| **S1·P2** | EdDSA key (offline) + appcast generator + GitHub release pipeline (CI draft → local sign/notarize → publish) | Medium — key custody + release wiring |
| **S2·P3** | Embed Sparkle; `MacSparkleUpdater` shim + `UpdateController` macOS branch + session-safety delegate + Settings reuse + launch check | Low — mirrors the Windows branch |

S2·P3 (the client) can be prototyped against a hand-written test appcast on a throwaway
release in parallel with S1·P2, but **S1·P0–P1 must land first** — Sparkle cannot update
a bundle that is not signed/notarized/relocatable (design §6, §8). Real end-to-end
(offer → download → verify → relaunch) gates on a first published, signed release.

---

# Stage 1 — Package, sign & publish

## S1·P0 — DMG packaging  ☐

Goal: `PinPointStudio-<ver>-x86_64.dmg` mounts on a stock Mac with **no Qt/ORT/OpenCV/
FFmpeg installed**, the `.app` drag-installs to /Applications, launches, and BLE IMUs,
the camera (`VideoInputApple`), Apple/Whisper STT, and CoreML/Metal inference all work.

- ☐ **Relocatable bundle via `macdeployqt`.** Add a packaging script
  (`tools/package_macos.sh`, the mac analogue of `tools/package_appimage.sh`) that:
  Release-builds the `.app`, runs `macdeployqt PinPointStudio.app -qmldir=src` to copy
  Qt frameworks + QML modules + platform/multimedia/RHI plugins and rewrite their
  install names, then fixes up the **non-Qt** dylibs `macdeployqt` misses.
- ☐ **Bundle the heavy native deps not on a stock host.** The CMake `if(APPLE)` rules
  already stage the **models** and **espeak-ng data** into
  `Contents/Resources/` (CMakeLists ~1020–1686) — keep those. Add/verify the dylib
  bundling + `@rpath`/`@executable_path` rewrites (`install_name_tool`) for:
  - **ONNX Runtime** (`libonnxruntime*.dylib`, Intel build 1.20.1 per CMakeLists
    ~753–815) and ORT-GenAI where present,
  - **OpenCV**, **FFmpeg** (the swing-export libav*), **Aravis** — all currently
    probed from **Homebrew** (CMakeLists ~1721–1937), so they are **not** on a clean
    host and **must** be copied into `Contents/Frameworks` + relinked,
  - `yt-dlp_macos` (already copied next to the binary; move into the bundle).
  - **Spinnaker** (proprietary): bundle if redistribution terms allow, else
    load-if-present and degrade (the device enumerator already tolerates absence).
- ☑ **Bundle size — link only the OpenCV modules used (drop VTK).** `find_package(OpenCV)`
  + `${OpenCV_LIBS}` links **every** module, including `opencv_viz` (→ the whole VTK tree)
  and `opencv_dnn` (→ OpenVINO). The repo uses **only `core`/`imgproc`/`imgcodecs`**
  (the only `opencv2/` headers included anywhere). CMake now links just those (`PP_OPENCV_LIBS`,
  with `opencv_world`/full-set fallbacks), so macdeployqt never bundles viz/dnn and their
  trees. **Measured on the actual bundle: −68 MB VTK (57 dylibs) − 25 MB unused opencv
  modules (53 dylibs) − 14 MB OpenVINO ≈ −107 MB and ~115 fewer dylibs (265 → ~150).**
  > **The dominant size drivers are elsewhere, and are NOT packaging defects:**
  > **(a)** `Contents/Resources/models` ≈ **381 MB**, of which `vitpose-b-wholebody.onnx`
  > is **344 MB**. **Decision: bundle it, matching the Windows build** — on Windows the
  > model installs into the `-core` component (`install(FILES … COMPONENT core)`,
  > CMakeLists ~1555), so it ships in the primary artifact; macOS already bundles it into
  > `Resources/models` the same way. Kept bundled for cross-platform consistency and
  > offline use (not fetched on demand). **(b)** the **Debug** main binary is ~218 MB vs
  > ~30 MB in **Release** (~185 MB that disappears automatically in the shipped build —
  > the local proof used a Debug bundle via `--app`). `libopenblas` (61 MB) is a hard dep
  > of `opencv_core` and stays. A real Release DMG size lands at the CI/clean-VM gate; the
  > OpenCV narrowing is the packaging-level win and is done here.
- ☐ **DMG assembly.** Build a DMG with the `.app` + an `/Applications` symlink (the
  standard drag-install layout) — `hdiutil create`/`create-dmg`, scripted in
  `tools/package_macos.sh`. Filename `PinPointStudio-<ver>-x86_64.dmg`, `<ver>` from
  `version.h` (mirrors the AppImage asset-name rule). Keep it out of the default build;
  invoke explicitly (as the Linux/Windows packaging is a separate step).
- ☐ **Acceptance:** on a **second, clean Mac** (no Homebrew, no Qt): mount the DMG, drag
  to /Applications, launch — zero missing-dylib crashes, QML loads, a BLE IMU connects,
  the camera previews, STT runs, CoreML/Metal inference works, swing export (x264)
  works. This is the macOS clean-VM gate, the analogue of the Linux P0 clean-Ubuntu run.

## S1·P1 — Codesign + notarize + version unification  ☐

Goal: the DMG is Developer ID-signed, notarized, stapled, and Gatekeeper-clean; the
app version comes from `version.h`, not stale literals.

- ☐ **Developer ID signing.** In `tools/package_macos.sh`, after `macdeployqt`,
  `codesign --deep --force --options runtime --timestamp` the `.app` with the
  **Developer ID Application** identity (Hardened Runtime on — required for
  notarization). Sign embedded frameworks/dylibs/helpers bottom-up (Sparkle's helper
  too, once S2·P3 embeds it). Add an **entitlements** plist for the Hardened Runtime
  exceptions this app needs (JIT/unsigned-mem only if a dep requires it; camera/mic/BLE
  are Info.plist usage strings, already present — see CLAUDE.md macOS checklist).
- ☐ **Notarize + staple.** Submit the DMG with
  `xcrun notarytool submit --wait` (app-specific-password / keychain profile, **kept
  off CI** — local maintainer step), then `xcrun stapler staple` the DMG **and** the
  `.app` so first launch works offline. Verify with `spctl -a -vv` and
  `stapler validate`.
- ☐ **Version unification (design §7).** Convert `Info.plist.in` to a **`configure_file`
  template** and substitute from the `version.h`-derived `project(VERSION)` (CMakeLists
  ~19–39):
  - `CFBundleShortVersionString` ← `@PROJECT_VERSION_MAJOR@.@PROJECT_VERSION_MINOR@` +
    postfix (the `PINPOINT_VERSION_STRING` body, e.g. `0.1-alpha3`) — replaces the
    hardcoded `0.1`.
  - `CFBundleVersion` ← the monotonic `@PROJECT_VERSION_PATCH@` (= `PINPOINT_VERSION_BUILD`,
    `10003`) — replaces the literal `1`. This is Sparkle's compare key.
  - Add the Sparkle keys as templated placeholders now (filled in S2·P3): `SUFeedURL`,
    `SUPublicEDKey`, `SUEnableAutomaticChecks`. (The existing build already
    force-copies the configured `Info.plist` over Qt's — CMakeLists ~2043–2076 — so this
    just extends that template.)
- ☐ **`-DPINPOINT_INSTALLED`** baked only into the packaged target (design §3) — the
  dev-build/installed discriminator the S2 controller keys off. Not defined for plain
  `build/` runs.
- ☐ **Acceptance:** the notarized DMG, downloaded via a browser (so it carries the
  `com.apple.quarantine` flag), installs and launches with **no** Gatekeeper warning and
  **no** App Translocation (`spctl -a -vv <app>` → `accepted, source=Notarized
  Developer ID`); `defaults read .../Info CFBundleVersion` → `10003`.

## S1·P2 — EdDSA signing + appcast + release pipeline  ☐

Goal: a published GitHub Release whose `appcast-mac.xml` + signed DMG a Sparkle client
can consume — provable before the client exists, against a test key.

- ☐ **EdDSA release key — OFFLINE (design §6).** Maintainer runs Sparkle's
  `generate_keys` **once**, exports the private key to an offline file
  (`generate_keys -x`), keeps it off GitHub (**NOT a CI secret**), and commits the
  **public** half to `src/Resources/keys/pinpoint_release_mac_eddsa.pub` (mirrors the
  Linux `.asc` / Windows `.pub`). The build/feed **refuse to act** while it is the
  placeholder value (the analogue of the Linux all-zero fingerprint / Windows
  `PLACEHOLDER`).
- ☐ **Appcast generator** `packaging/make_appcast_mac.sh` (mac sibling of the Windows
  `make_appcast.ps1`): takes the built+notarized DMG, signs it with the offline key via
  Sparkle's `sign_update` (→ `edSignature` + `length`), reads the display + build
  version from `version.h`, and emits `appcast-mac.xml` with the **specific** release-tag
  enclosure URL (not `latest/`, so the bytes match the sig — design §4.1, §6). Optional
  notes link.
- ☐ **Release pipeline.** Add a **`macos` job** to `.github/workflows/release.yml` (tag
  push `v*`, behind the existing `guard` job): macOS runner + Qt 6.11 + the
  `tools/package_macos.sh` packaging, building the **unsigned, un-notarized** DMG and
  staging it on the **draft** release. **No signing/notarization secrets in CI** — the
  maintainer signs + notarizes the exact CI-built (or locally-built, Path B) DMG
  locally, runs `make_appcast_mac.sh`, uploads the DMG + `appcast-mac.xml`, and
  publishes. (Mirrors the Linux/Windows local-signing model; the `guard` job already
  skips CI when a published release exists, so Path B isn't clobbered.)
  - *Caveat:* GitHub macOS runners are **arm64** by default now — pin an **Intel
    (`macos-13`)** runner so the x86_64 DMG is actually built x86_64, matching the v1
    target arch. Heavy FetchContent (whisper/ViTPose/ORT) makes the first run slow;
    validate with a test tag.
- ☐ **`macos_release_runbook.md`** — the local sign+notarize+EdDSA flow (Path A: CI
  draft → download DMG → `codesign`/`notarytool`/`stapler` → `make_appcast_mac.sh`
  sign+emit → verify (`spctl`, `sign_update --verify`) → upload appcast → publish;
  Path B: fully local). Covers sign-the-bytes-you-publish, the pin-must-already-ship
  bootstrap, never-publish-without-the-appcast, the `latest/download` non-prerelease
  rule, and rollback. The mac sibling of `windows_release_runbook.md`.
- ☐ **Acceptance:** publish a release per the runbook; `appcast-mac.xml` validates,
  `sign_update --verify` passes against the pinned public key, the DMG is notarized.
  (End-to-end offer→install is exercised in S2·P3.)

---

# Stage 2 — Spot & auto-update

## S2·P3 — Sparkle client: detect & update  ◑

Goal: an installed, signed macOS build notices a newer published release, shows
Sparkle's native "update available" window (on launch and via Settings → Check now),
verifies + installs after confirmation, and honours the session guard — with **no new
state machine** (Sparkle owns it).

> **Status (2026-06-18): the client is implemented and verified inert.** All the
> implementation bullets below are done; the build compiles, links
> `@rpath/Sparkle.framework` (Sparkle 2.9.3), launches, and stays correctly inert while
> `SUPublicEDKey` is the placeholder (`[Sparkle] no pinned EdDSA key yet`). What remains
> are the two **maintainer gates**: generate the offline EdDSA key (S1·P2's key bullet)
> so the updater arms, then the end-to-end **Acceptance** item below.

- ☑ **Embed Sparkle 2.** Sparkle **2.9.3** fetched via `FetchContent` (`.tar.xz`, SHA256
  pinned) in the `if(APPLE)` block (twin of the `WIN32` WinSparkle block); linked with
  `-F`/`-framework Sparkle` + `HAVE_SPARKLE`, with `BUILD_RPATH` → `_deps/sparkle-src` so a
  dev/build-tree run loads it. `tools/package_macos.sh` §4e `ditto`-copies the whole
  `Sparkle.framework` (incl. `Autoupdate`/`Updater.app`/`XPCServices`) into
  `Contents/Frameworks/` **after** macdeployqt (which mangles nested-helper frameworks);
  `codesign_bundle()` signs those helpers bottom-up before the framework seal.
- ☑ **`src/Update/mac_sparkle_update.{h,mm}`** — `MacSparkleUpdater` (QObject, Obj-C++),
  the structural twin of `WinSparkleUpdater`:
  - `configureAndInit(AppSettings*, SessionController*)`: create
    `SPUStandardUpdaterController` (which reads `SUFeedURL` + `SUPublicEDKey` from
    Info.plist), set `automaticallyChecksForUpdates` from `appSettings.checkForUpdates`,
    set the check interval, install itself as the `SPUUpdaterDelegate`, start the
    updater. Refuse/return false if `SUPublicEDKey` is the placeholder (design §6).
  - `checkNow()` → `[updaterController checkForUpdates:nil]`.
  - `setAutomaticChecks(bool)` → `updater.automaticallyChecksForUpdates = …`.
  - `shutdown()` → release the controller (called from `shutdownUpdater()`).
  - **`isInstalledBuild()`** static: `-DPINPOINT_INSTALLED` defined **and** not running
    from a build tree / App-Translocated read-only path (design §3, §8).
  - `updater:shouldPostponeRelaunchForUpdate:untilInvokingBlock:` → retain the block
    while `m_session->running()`, invoke it on `runningChanged`→idle (design §5.1).
    **Main-thread callbacks — touch `SessionController` directly, no atomic/queued
    dance** (the macOS simplification over Windows).
- ☑ **`update_controller.{h,cpp}` macOS branch** — add an
  `#elif defined(Q_OS_MACOS) && defined(HAVE_SPARKLE)` arm **mirroring** the existing
  Windows `#elif` (lines ~129–146): own a `MacSparkleUpdater`, `m_supported` from
  `configureAndInit`, `state()` stays `"idle"` (Sparkle owns transient states),
  `checkNow()` delegates; forward `checkForUpdatesChanged` to `setAutomaticChecks`. Dev
  tree → `"devbuild"`, inert. `download()/relaunch()/installOnNextLaunch()/skipVersion()`
  are **no-ops on macOS** (Sparkle's UI handles them) — keep them so the shared QML
  binds. Add a `MacSparkleUpdater *m_macSparkle = nullptr;` member next to
  `m_winSparkle`.
- ☑ **`main.cpp`** — confirmed **no functional change** needed (constructs/registers
  `updateController`, calls `shutdownUpdater()` in `aboutToQuit`). Sparkle init is deferred
  inside `MacSparkleUpdater` via a 3 s `QTimer::singleShot` → `-startUpdater` (created with
  `startingUpdater:NO`), so it lands after the window is up without a `main.cpp` hook. (The
  stale Linux-only updater comment was refreshed to name all three engines.)
- ☑ **`GeneralPanel.qml`** — confirmed **no change needed.** The existing `supported`/`uState`
  switch resolves macOS's `state == "idle"` to the "Check now" chip (line ~788/804) and the
  inert `"devbuild"` to no-chip + neutral badge; the Linux-only states never fire (design §5B).
- ☑ **Error/diagnostics.** `PPSparkleDelegate` implements `updater:didAbortWithError:` and
  `updater:didFailToDownloadUpdate:error:` → `ppWarn()` (`PpMessageLog`); a failed automatic
  check is silent and never blocks startup (design §8).
- ☐ **Acceptance (end-to-end, design Done-when):** an installed build on `v<n>`, with a
  published `v<n+1>` release: launches → Sparkle's "update available" window appears
  (or via Settings → **Check now**); accepting downloads the DMG, Sparkle **verifies**
  the EdDSA signature **and** the Developer ID Team-ID against the pin, the app
  relaunches on `v<n+1>`; a tampered/wrong-key DMG is **rejected**; starting a session
  and accepting an update **postpones** the relaunch until the session ends.

---

## Files touched (planned)

```
NEW  docs/design/macos_update.md                        (the contract)                        S1·P0
NEW  docs/implementation/macos_update_impl.md           (this plan)                            S1·P0
NEW  tools/package_macos.sh                             (build → macdeployqt → sign → dmg)     S1·P0/P1
NEW  src/Update/mac_sparkle_update.{h,mm}               (Sparkle shim: config/init/delegate)   S2·P3
NEW  src/Resources/keys/pinpoint_release_mac_eddsa.pub  (pinned Ed25519 public key)            S1·P2
NEW  packaging/make_appcast_mac.sh                      (sign + emit appcast-mac.xml)          S1·P2
NEW  docs/implementation/macos_release_runbook.md       (local sign+notarize release runbook)  S1·P2
EDIT Info.plist.in                                       (version ← version.h; Sparkle keys)    S1·P1/S2·P3
EDIT CMakeLists.txt                                      (macdeployqt/dmg target; FetchContent
                                                          Sparkle; embed+link+sign; sources;
                                                          PINPOINT_INSTALLED define)            S1/S2
EDIT src/Update/update_controller.{h,cpp}               (macOS branch → Sparkle façade)         S2·P3
EDIT src/Gui/main.cpp                                    (Sparkle init after window-ready)       S2·P3
EDIT .github/workflows/release.yml                       (macOS draft-build job, macos-13)       S1·P2
EDIT BUILDING.md                                         (macOS packaging + release section)     S1·P1/P2
```
(`GeneralPanel.qml` is deliberately **not** in the list — the shared updater row already
covers macOS, design §5B.)

## Risks & open questions

- **Notarization + relocatable bundling is the long pole (design §6, §8)** — unlike
  Windows, there is no pre-existing installer, and Gatekeeper/App-Translocation make
  signing a hard prerequisite for in-place update, not GA polish. Prove a notarized DMG
  installs cleanly on a second Mac (S1·P0–P1) before any Sparkle code.
- **Homebrew-probed deps (OpenCV, FFmpeg, Aravis)** are linked from `/opt/homebrew` or
  `/usr/local` today and are **absent on a clean host** — they must be copied into the
  bundle and relinked (`install_name_tool`), the macOS equivalent of the Linux
  linuxdeploy bundling. This is the bulk of S1·P0 risk.
- **Architecture (decided: x86_64 only v1)** — runs under Rosetta 2 on Apple Silicon.
  Pin an **Intel `macos-13`** CI runner so CI actually builds x86_64. Native `arm64` is
  a GA add (second feed, design §7); the diverging ORT versions (arm64 1.26 vs Intel
  1.20.1) are why a universal binary is deferred.
- **Sparkle native UI vs the QML house style** — accepted divergence: Sparkle owns its
  window on macOS as WinSparkle does on Windows. No custom QML flow (design §5).
- **Key & cert custody** — the Ed25519 private key **and** the Developer ID
  certificate/notarytool credential are the roots of trust; all stay **offline**, never
  CI secrets (local sign+notarize model). Back them up; document rotation. A leak of
  either is what breaks the trust model.
- **Delta updates deferred** — full DMGs in v1; Sparkle `BinaryDelta` is a clean GA add
  needing the prior release's `.app` at release time (design §4.4).
- **Apple Developer Program dependency** — Developer ID signing + notarization require
  the paid ($99/yr) Apple Developer Program. Confirmed in scope (decided). Without it,
  v1 would regress to ad-hoc + EdDSA-only with Gatekeeper friction (the rejected
  alternative).

## Done-when

An installed macOS user on `v<n>` sees Sparkle's "update available" window on launch
(or via Settings → **Check now**), reads the changelog, accepts, watches the DMG
download, Sparkle verifies it against the pinned **EdDSA key** *and* the **Developer ID
Team-ID**, the app swaps in place and relaunches on `v<n+1>` — with no Gatekeeper
warning, no terminal, no App Store — and the same prompt-then-install flow exists on
Windows (WinSparkle) and Linux (AppImage).
