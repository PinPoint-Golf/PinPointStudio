# Windows In-App Update — Build Plan

*Phased plan to deliver the design in
[`../design/windows_update.md`](../design/windows_update.md): ship the Windows build
as a WinSparkle-driven, EdDSA-signed, prompt-then-install auto-update over GitHub
Releases, reusing the existing per-user Inno Setup installer. Status legend:
☐ todo · ◑ in progress · ☑ done.*

The honest shape of this work, and how it differs from Linux: **on Linux the updater
was the easy 20% and producing a relocatable signed AppImage was the 80%. On Windows
the packaging 80% is already done** — `cmake/PinPointPackaging.cmake` +
`packaging/build_installer.ps1` already produce a per-user, no-UAC installer with a
stable `AppId`, `AppMutex`, run-on-finish, and a core/cuda component split that was
explicitly built as "groundwork for in-app auto-update". So the remaining work is
mostly **(a) bolt on WinSparkle**, **(b) stand up signing + an appcast + a release
step**, and **(c) settle the core-vs-cuda enclosure question** so a small update
doesn't disturb the CUDA runtime. WinSparkle does the download/verify/UI/relaunch we
hand-rolled on Linux, so there is *no* state machine to write here.

---

## Phase ordering & rationale

| Phase | Outcome | Risk |
|---|---|---|
| **P0** | WinSparkle linked, shipped, initialised; manual "Check for updates" shows WinSparkle UI against a test appcast | Low |
| **P1** | `WinSparkleBackend` (factory-selected) + `main.cpp` wiring + session-safety callbacks + Settings action | Low |
| **P2** | EdDSA signing key (offline) + appcast generator + release pipeline; version unification | Medium — key custody + version reconciliation |
| **P3** | CUDA runtime split to its own `AppId` + hardware-adaptive "enable GPU acceleration" sidecar; polish; runbook | Medium — Inno AppId surgery + a second install flow |

Unlike Linux, **P0 is low-risk** (the package already exists). The decided shape:
the WinSparkle enclosure is **always the hardware-agnostic `-core` installer** (design
§4.3); the **CUDA runtime is a separate, app-managed, hardware-driven sidecar** (design
§4.4) so users who add/remove a GPU adapt without reinstalling. The two real questions
left are **P2 version unification** (two version sources today) and **P3 the CUDA
`AppId` split + adaptive offer** — sequence those deliberately. P0–P2 (the core app
updater) do **not** depend on P3.

P0+P1 can be proven against a hand-written test appcast hosted on a throwaway
release before any signing exists (point the EdDSA pin at a test key). Real
end-to-end (offer → download → verify → relaunch) gates on P2.

---

## P0 — WinSparkle in the build  ☐

Goal: an installed build links and ships `WinSparkle.dll`, initialises WinSparkle on
startup, and a manual "Check for updates" pops WinSparkle's window against a test
appcast.

- ☐ **Fetch WinSparkle (prebuilt).** A prebuilt `WinSparkle-<ver>` (≥ **0.9.0** for
  EdDSA) zip via `FetchContent` (or vendored under `third_party/`), mirroring the ORT
  prebuilt pattern in `CMakeLists.txt`. Expose `WinSparkle.lib` (import lib) +
  `<winsparkle.h>` include + `WinSparkle.dll`. Windows-only; guarded behind `WIN32`.
- ☐ **Link + bundle.** `target_link_libraries(PinPointStudio … WinSparkle.lib)` and
  route `WinSparkle.dll` through the existing **`pp_win_bundle_dll(core …)`** helper
  so it is copied next to the dev exe (POST_BUILD) **and** installed under `bin/` in
  the **core** component — so it travels with the small auto-update payload.
- ☐ **`src/Update/win_sparkle_update.{h,cpp}`** — `WinSparkleUpdater` (QObject):
  - `configureAndInit(AppSettings*, SessionController*)`: set appcast URL, app
    details (display = `PINPOINT_VERSION_STRING`), build version (the monotonic int,
    §P2), **EdDSA public key (assert the call returns 1 — refuse to init if not)**,
    automatic-check from `appSettings.checkForUpdates`, check interval, the two
    shutdown callbacks, the error callback; then schedule `launchCheck()` ~3 s out.
  - `launchCheck()` → `doInit()` (`win_sparkle_init()`) then, when
    `appSettings.checkForUpdates`, a silent `win_sparkle_check_update_without_ui()` —
    the **per-launch** check (Linux 4 s-launch parity; WinSparkle's own automatic check
    is interval-gated and never fires on first run — design §5A).
  - `checkNow()` → `win_sparkle_check_update_with_ui()`.
  - `setAutomaticChecks(bool)` → `win_sparkle_set_automatic_check_for_updates()`.
  - `shutdown()` → `win_sparkle_cleanup()`.
  - **"installed build" guard:** inert unless running from the install tree (a
    `-DPINPOINT_INSTALLED` define baked only into packaged builds, or an exe-path
    check) — the analogue of the Linux `$APPIMAGE`-unset → `devbuild`.
- ☐ **Add `src/Update` WinSparkle sources to `target_sources` under `if(WIN32)`**
  (the existing `src/Update/*` block compiles on all platforms; the WinSparkle pair
  is Windows-only).
- ☐ **Acceptance:** install a build, run from the install dir, Settings → "Check for
  updates" → WinSparkle's window appears and parses a **hand-written test appcast**
  (hosted on a scratch release) offering a higher `sparkle:version`; with a test
  EdDSA key the signed test installer downloads and runs. No real key/release yet.

## P1 — Windows backend (WinSparkleBackend) + QML wiring  ☐

Goal: the existing `updateController` QML property drives WinSparkle on Windows with
no new state machine, and the session guard is honoured.

> **Refactor note.** Since this plan was written, the updater moved to a polymorphic
> `UpdateBackend` hierarchy, so the Windows engine is a **new backend class**, not a
> branch inside `update_controller.cpp`. `UpdateController` is now the unchanged shared
> façade; the only platform `#ifdef` is in `update_backend_factory.cpp`. The bullet
> below is restated against that structure — see [`../design/windows_update.md`](../design/windows_update.md) §3.

- ☐ **`src/Update/win_sparkle_backend.{h,cpp}`** — `WinSparkleBackend` (an
  `UpdateBackend`), selected by `makeUpdateBackend()` under `Q_OS_WIN && HAVE_WINSPARKLE`:
  - On Windows in an installed build: `supported()==true`, `ownsStateMachine()==false`,
    owns a `WinSparkleUpdater`, `checkNow()` → `WinSparkleUpdater::checkNow()`. Dev tree
    (`isInstalledBuild()==false`) → initial state `DevBuild`, inert.
  - `setAutomaticChecks()` forwards to `WinSparkleUpdater::setAutomaticChecks()`; the
    controller wires `appSettings.checkForUpdates` → the active backend's
    `setAutomaticChecks()` once, unconditionally, so the toggle stays live.
  - `download()/relaunch()/installOnNextLaunch()` inherit the base no-ops (WinSparkle's
    UI handles those); `skipVersion()` stays in `UpdateController`. `UpdateController` is
    **not** edited.
- ☐ **`main.cpp`:** the `UpdateController updateController(&appSettings,
  &sessionController)` already exists and is registered. Add (Windows-only) the
  `win_sparkle_cleanup()` hook into the existing `aboutToQuit` lambda (next to
  `eventBuffer.stop()`), and ensure WinSparkle is initialised **after** the main
  window is shown (per WinSparkle's contract) — either at the end of `main` after
  `engine.loadFromModule`, or from the controller on first window-ready.
- ☐ **Session-safety callbacks** (design §5.1): can-shutdown reads an
  `std::atomic<bool>` mirror of `sessionController.running()` (updated via a queued
  connection to `runningChanged`); shutdown-request posts `QCoreApplication::quit()`
  to the GUI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
  **No `QObject` access from the callback thread.**
- ☐ **`GeneralPanel.qml` Version row** — minimal, Windows-aware:
  - The `updAction` chip already keys off `updateController.supported` and `uState`.
    On Windows `uState` is `"idle"`, so `mode` resolves to `"check"` → the chip reads
    "Check for updates" and calls `checkNow()`. Reword the `"check"` label to
    "Check for updates" (or keep "Check now") so it reads well on Windows.
  - The Linux-only states (`downloading`/`verifying`/`ready`) never fire on Windows —
    no change needed; the badge falls through to the neutral "✓ Up to date" /
    current version. Optionally hide the status/error line on Windows (WinSparkle
    surfaces its own errors).
- ☐ **Acceptance:** build the installer, run installed; the toggle flips WinSparkle's
  automatic-check registry state; "Check for updates" shows WinSparkle UI; starting a
  session and accepting an update is refused until the session ends (can-shutdown
  returns false); ending it lets the install proceed; quit is graceful (merger stops,
  no teardown crash).

## P2 — EdDSA signing + appcast + release pipeline  ◑ (built; awaits the offline key + a first published release)

- ◑ **Release key — wiring DONE, key-gen is the maintainer's offline step.** The app
  reads the pinned key from the compiled-in resource
  `src/Resources/keys/pinpoint_release_win_eddsa.pub` (single source of truth, mirroring
  the Linux `.asc`) and `win_sparkle_update.cpp` **refuses to init while it is the
  `PLACEHOLDER` value** — so an unsigned feed can never drive an update (the analogue of
  the Linux all-zero fingerprint). Remaining: the maintainer runs
  `winsparkle-tool generate-key` **offline** (Ed25519; private key file kept off GitHub,
  NOT a CI secret), pastes the base64 public half into the `.pub`, and commits it. The
  exact commands are in the runbook.
- ☑ **Version unification (design §7).** `PINPOINT_VERSION_BUILD` (monotonic int) added
  to `src/Core/version.h`; `WinSparkleUpdater` feeds it to `set_app_build_version` and
  the appcast carries it as `sparkle:version`. `CMakeLists.txt` now **derives**
  `project(VERSION)` = `MAJOR.MINOR.BUILD` from `version.h`, so the Inno/installer
  version, the app version, and the appcast can't drift — **bump version.h only**.
- ☑ **Appcast generator** `packaging/make_appcast.ps1`: takes the built `-core`
  installer, signs it with the offline key via `winsparkle-tool sign` (→ `edSignature`),
  reads version (display + build int) from `version.h`, computes byte length, and emits
  `appcast-win.xml` with the **specific** release-tag enclosure URL (not `latest/`, so
  the bytes match the sig). Optional `-NotesUrl` for the "what's new" link.
- ☑ **Release pipeline.** `.github/workflows/release.yml` gains a **`windows` job**
  (tag push `v*`): MSVC + Qt 6.11 + Inno Setup + Ninja, builds the **unsigned `-core`**
  installer and stages it on the **draft** release. **No signing secrets** — the
  maintainer signs the exact CI-built `.exe` locally, generates `appcast-win.xml`,
  uploads both, and publishes. **Authored, not yet run** (heavy FetchContent; validate
  with a test tag). The hosted runner has no GPU, so it builds the hardware-agnostic
  `-core` only; `-cuda` is built/managed separately (design §4.4).
- ☑ **`windows_release_runbook.md`** — local-signing flow (Path A: CI draft → download →
  `make_appcast.ps1` sign+emit → verify with `winsparkle-tool verify` → upload appcast →
  publish; Path B: fully local). Covers sign-the-bytes-you-publish, the pin-must-already-
  ship bootstrap, never-publish-without-the-appcast, the `latest/download` non-prerelease
  rule, CUDA-is-separate, and rollback.
- ◑ **Acceptance:** generate the key + publish a release per the runbook; an older
  installed build → the passive **launch check** (`launchCheck()`, ~3 s after startup)
  **or** the manual "Check for updates" offers the new version, downloads the `-core`
  enclosure, WinSparkle **verifies** the EdDSA signature against the pin, relaunches; a
  tampered/wrong-key installer is **rejected** (never runs). Blocked on the offline key +
  a first published release.

## P3 — CUDA `AppId` split + hardware-adaptive sidecar + polish  ◑ (app side built; packaging authored, awaits clean-VM validation)

- ◑ **CUDA runtime as its own Inno `AppId`** (design §4.3) — authored in
  `packaging/build_installer.ps1`: `-Components cuda` now packages with a **distinct,
  stable `AppId`** (and no app shortcut/run-on-finish), installing to the **same** dir as
  core (`%LOCALAPPDATA%\Programs\PinPointStudio`), so the DLLs sit next to the exe (no
  DLL-search change) but live in their **own uninstall log** — a `-core` auto-update
  can't touch them. **Needs clean-VM validation:** install core, install cuda, run a
  `-core` upgrade, confirm the cuDNN/CUDA DLLs survive. (CPack `INNOSETUP` override
  behaviour for the cleared shortcut/run vars is the untested part.)
- ☑ **Hardware-adaptive "enable GPU acceleration" offer (design §4.4)** — built +
  compile-verified. `src/Update/cuda_runtime_controller.{h,cpp}` (`CudaRuntimeController`,
  registered as `cudaRuntime`): probes (a) NVIDIA GPU via the `nvcuda.dll` load and (b)
  the runtime via **`cudnn64_9.dll` next to the exe** (the filename encodes the required
  cuDNN major → presence + right-major in one check; no registry/installer coupling).
  Exposes `supported`/`gpuPresent`/`runtimeInstalled`/`shouldOffer` + `refresh()`.
  `GeneralPanel.qml` gains a **GPU acceleration row** (visible only on a CUDA-capable
  installed Windows build) showing status + an **Install GPU acceleration** action; it
  re-probes on `Component.onCompleted` so a GPU added since launch shows without a
  restart. Inert off Windows (`supported=false`).
  - **v1 install action (decided):** WinSparkle exposes **no app-side verify API**, so to
    avoid auto-running unverified bytes the action **opens the official GitHub Releases
    page** (HTTPS-trusted) for the user to fetch + run the `-cuda` installer. GA upgrade:
    one-click in-app fetch behind a vendored Ed25519 verify (the `-cuda` installer signed
    with the same pinned key) **or** an Authenticode-signed `-cuda` installer.
- ☑ **Error/diagnostics.** `win_sparkle_set_error_callback` → `PpMessageLog` (wired in
  P0/P1); a failed automatic check is silent and never blocks startup.
- ☑ **Settings copy.** The shared updater action reads "Check now" on both platforms
  (acceptable on Windows); the new GPU row uses "Install GPU acceleration"; the
  "Check for updates automatically" subtitle already fits WinSparkle's behaviour.
- ☐ **(Optional, GA) Authenticode.** Code-sign the installers + app binaries with an
  OV/EV cert to drop SmartScreen friction (orthogonal to the EdDSA gate; also unlocks the
  one-click `-cuda` fetch above). Deferred — no cert yet.
- ☐ **(Deferred) channels.** `appSettings.updateChannel` (Stable/Beta) → a different
  appcast asset URL set before `win_sparkle_init()`; lands with GA alongside the Linux
  channel work.

---

## Files touched (planned)

```
NEW  docs/design/windows_update.md                     (the contract)                        P0
NEW  docs/implementation/windows_update_impl.md        (this plan)                            P0
NEW  src/Update/win_sparkle_update.{h,cpp}             (WinSparkle shim: config/init/cb)      P0/P1
NEW  src/Update/win_sparkle_backend.{h,cpp}            (WinSparkleBackend: UpdateBackend over the shim) P1
NEW  src/Update/cuda_runtime_controller.{h,cpp}        (hardware-adaptive GPU-runtime offer)  P3
NEW  src/Resources/keys/pinpoint_release_win_eddsa.pub (pinned Ed25519 public key)            P2
NEW  packaging/make_appcast.ps1                         (sign + emit appcast-win.xml)         P2
NEW  docs/implementation/windows_release_runbook.md     (local-signing release runbook)       P2
EDIT CMakeLists.txt                                     (FetchContent WinSparkle; link; bundle
                                                         core; sources; version derive; key)  P0/P2/P3
(UpdateController is NOT edited — the factory + WinSparkleBackend carry the Windows port)
EDIT src/Gui/main.cpp                                   (updater init/cleanup; cudaRuntime)    P1/P3
EDIT src/Gui/settings/GeneralPanel.qml                 (updater row reuse; GPU-accel row)      P1/P3
EDIT src/Core/version.h                                 (monotonic build version for compare)  P2
EDIT packaging/build_installer.ps1                     (CUDA own-AppId variant)               P3
EDIT .github/workflows/release.yml                     (Windows draft-build job)               P2
EDIT BUILDING.md                                        (Windows packaging + release section)  P2/P3
```

## Risks & open questions

- **CUDA `AppId` split + adaptive sidecar (design §4.3–4.4)** — decided: enclosure is
  always `-core`; CUDA is a separate `AppId` the app installs/updates by detected
  hardware so users adapt without reinstalling. Cost: Inno `AppId` surgery + a second
  (small) prompt-then-install flow for the `-cuda` asset. Sequenced as P3; P0–P2 don't
  depend on it. The WinSparkle client stays agnostic — it only ever fetches `-core`.
- **Version reconciliation (design §7)** — two version sources today (`version.h`
  display vs `project(VERSION)` numeric, currently inconsistent). WinSparkle's compare
  needs a monotonic build int; get the SoT right in P2 or updates mis-order around
  prerelease suffixes.
- **WinSparkle native UI vs the QML house style** — accepted divergence: WinSparkle
  owns its window on Windows as Sparkle does on macOS. A custom QML flow is not
  practical (no granular progress callback) and is explicitly out of scope.
- **Callback threading** — can-shutdown/shutdown-request are *not* on the main thread;
  atomic reads + queued invokes only, or risk a teardown race.
- **CI can't build `-cuda`** (no GPU runner) — `-core` (the enclosure) builds on a
  hosted runner; `-cuda` needs a self-hosted runner or local build.
- **Key custody** — the Ed25519 private key is the pinned root of trust; stays
  offline, never a CI secret. Back it up; document rotation. A leak is the one thing
  that breaks the trust model (identical to the Linux GPG key risk).
- **No Authenticode in v1** — SmartScreen will warn on first manual install until an
  OV/EV cert exists; orthogonal to auto-update trust but worth flagging for GA.

## Done-when

An installed Windows user on `v<n>` sees WinSparkle's "update available" window on
launch (or via Settings → **Check for updates**), reads the changelog, accepts,
watches the `-core` installer download, WinSparkle verifies it against the pinned
EdDSA key, the per-user installer upgrades in place with **no UAC prompt** and
relaunches on `v<n+1>` — the CUDA runtime untouched — and the same prompt-then-install
flow exists on macOS (Sparkle) and Linux (AppImage).
