# PinPoint Studio — Windows In-App Update

*The authoritative design for self-updating the Windows build. Mirrors the macOS
(Sparkle) and Linux (AppImage) update stories with the native Windows idiom:
the existing **per-user Inno Setup installer** delivered as a **WinSparkle**
appcast item, gated by a **pinned EdDSA (Ed25519) signature**, surfaced as a
**prompt-then-install** flow on launch and from Settings → General. Distribution
and the feed are **GitHub Releases** — the same host the Linux updater uses. This
doc is the contract; the phased build is
[`../implementation/windows_update_impl.md`](../implementation/windows_update_impl.md).
Read it alongside the Linux design [`linux_update.md`](linux_update.md) — the two
are deliberately symmetric.*

---

## 1. Goals & non-goals

**Goals**
- Same product behaviour as Sparkle/the Linux AppImage updater: the app notices a
  newer release, tells the user what changed, downloads **only after
  confirmation**, verifies authenticity against a pinned key, and relaunches into
  the new build.
- Reuse the **existing** Windows packaging unchanged in spirit: the per-user,
  no-UAC Inno Setup installer (`cmake/PinPointPackaging.cmake`) already carries the
  groundwork — stable `AppId`, `AppMutex`, `PrivilegesRequired=lowest`, run-on-
  finish — explicitly "groundwork for in-app auto-update". This design consumes it.
- Distribution and the update feed are **GitHub Releases** — no separate server,
  same repo (`PinPoint-Golf/PinPointStudio`) and the same publish step as Linux.
- The update engine is **WinSparkle** (the Windows analogue of Sparkle). We do
  **not** re-implement a download/verify/relaunch state machine on Windows — that
  is exactly what WinSparkle is, and re-using it is the whole point of "mirror the
  Sparkle/WinSparkle story".

**Non-goals**
- Background silent auto-install (rejected, as on Linux — see §5,
  prompt-then-install). WinSparkle's `check_update_with_ui_and_install()` exists but
  we do not use it.
- Delta updates. WinSparkle/Sparkle ship a **full installer** per release; the size
  lever is the **core/cuda installer split** (§4.3), not binary deltas. (The Linux
  build deltas via zsync because an AppImage is one giant file; the Windows installer
  is already componentised, so the equivalent win is "don't re-download the 1 GB CUDA
  runtime for a code-only release.")
- Per-machine / all-users installs, MSI, the Microsoft Store, or winget. Out of
  scope. The install is per-user precisely so an update can replace files with no
  elevation.

## 2. The cross-platform mapping

| Concern | macOS | **Windows (this doc)** | Linux |
|---|---|---|---|
| Package | `.app`/`.dmg` | **Inno Setup installer (`.exe`)** | AppImage (single file) |
| Update engine | Sparkle | **WinSparkle** | appimageupdatetool (zsync) |
| Feed / "appcast" | appcast XML | **Sparkle appcast XML** on GitHub Releases (§4.1) | GitHub Releases JSON |
| Binary transport | full DMG | **`-core` installer** (app); **CUDA runtime managed adaptively** (§4.3–4.4) | zsync delta |
| Authenticity | EdDSA on appcast | **EdDSA (Ed25519) on appcast** (§6) | pinned GPG sig on AppImage |
| UX | Sparkle's native UI | **WinSparkle's native UI** (§5) | PinPoint QML banner |
| Signing custody | local/offline key | **local/offline key** (§6) | local/offline key |

The asymmetry worth internalising up front: on **Linux** we hand-rolled a full
backend (`LinuxAppImageBackend` — feed query + zsync driver + GPG gate, behind the
shared QML UI) because there is no "Sparkle for Linux" we trust to bundle. On
**Windows there is** — WinSparkle — so the Windows backend shrinks to *configuration
+ a "Check now" button + session-safety callbacks*, and WinSparkle owns the feed
parse, version compare, download, signature verify, progress UI, and installer
launch. **Less PinPoint code, not more.** Both are concrete `UpdateBackend`s behind
the same `UpdateController` façade (the WinSparkle one reports
`ownsStateMachine() == false`); the rich QML state-machine UI (`PpUpdateBanner.qml`,
the live Settings badge) only lights up for the Linux backend — on Windows the update
UI is WinSparkle's, exactly as on macOS it is Sparkle's.

## 3. Components

```
src/Update/
  win_sparkle_update.h/.cpp    WinSparkleUpdater — thin Qt/Win32 shim: configures
                               WinSparkle (appcast URL, app details, pinned EdDSA
                               key, automatic-check pref), wires the can-shutdown /
                               shutdown-request callbacks to the session guard,
                               and exposes checkNow() + setAutomaticChecks().
                               Compiled on Windows only (HAVE_WINSPARKLE).
  win_sparkle_backend.h/.cpp   WinSparkleBackend — the Windows UpdateBackend: owns a
                               WinSparkleUpdater, reports supported + ownsStateMachine()
                               == false, and forwards checkNow()/setAutomaticChecks()/
                               shutdown() to it. The structural twin of MacSparkleBackend.
  update_backend_factory.h/.cpp  makeUpdateBackend() returns a WinSparkleBackend under
                               `Q_OS_WIN && HAVE_WINSPARKLE` — the SOLE platform #ifdef.
  update_controller.h/.cpp     UNCHANGED by the Windows port — the shared QML façade
                               (it already delegates to whatever backend the factory
                               returns). The rich Downloading/Verifying/ReadyToRelaunch
                               states never light up here (WinSparkle owns that UI).
  (linked DLL)                 WinSparkle.dll — prebuilt, fetched by CMake
                               (FetchContent), copied next to the exe and installed
                               under bin/ in the "core" component.
  (embedded key)               Ed25519 release public key, base64, passed to
                               win_sparkle_set_eddsa_public_key() at init (or via the
                               "EdDSAPub"/"EDDSA" Win32 resource).
```

- **WinSparkle is a C DLL.** We link `WinSparkle.lib`, ship `WinSparkle.dll`, and
  call its C API (`<winsparkle.h>`). No COM, no extra runtime. EdDSA support is
  WinSparkle ≥ 0.9.0 (`win_sparkle_set_eddsa_public_key`) — pin a release at or
  above that.
- `UpdateController` is the single QML context property `updateController`, registered
  in `main.cpp` on all platforms. It is **always** a thin façade over an
  `UpdateBackend`; on Windows the factory hands it a `WinSparkleBackend`, on Linux a
  `LinuxAppImageBackend`, and off both an inert fallback (`unsupported`). The port adds
  the Windows backend — it does **not** touch `UpdateController`.
- WinSparkle is **only meaningful in an installed build.** A dev build run from
  `build/…` has no installer to relaunch and (deliberately) no signed appcast it
  trusts. `WinSparkleBackend` detects "running from the install tree" (via
  `WinSparkleUpdater::isInstalledBuild()` — the exe lives under
  `%LOCALAPPDATA%\Programs\PinPointStudio`, or simpler: a CMake `-DPINPOINT_INSTALLED`
  define baked only into packaged builds) and reports `devbuild` otherwise — the
  analogue of the Linux `$APPIMAGE`-unset → `devbuild` check.

## 4. The feed and the transport

### 4.1 Appcast — Sparkle XML on GitHub Releases

WinSparkle consumes a classic **Sparkle appcast** (RSS/XML), set via
`win_sparkle_set_appcast_url()` (HTTPS only). We host it as a **release asset** and
point WinSparkle at GitHub's stable "latest release" redirect:

```
https://github.com/PinPoint-Golf/PinPointStudio/releases/latest/download/appcast-win.xml
```

`releases/latest/download/<asset>` always 302-redirects to that asset on the latest
**non-prerelease, non-draft** release — the exact same GitHub behaviour the Linux
`gh-releases-zsync … latest` transport relies on. So the appcast URL is **static and
baked into the binary** (it never encodes a version), and "what's the latest
version?" is answered by whatever `appcast-win.xml` the newest published release
carries. This is the Windows mirror of "GitHub Releases is the feed".

A release's `appcast-win.xml` has one `<item>`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>PinPoint Studio</title>
    <item>
      <title>PinPoint Studio v0.1-alpha2</title>
      <sparkle:releaseNotesLink>
        https://github.com/PinPoint-Golf/PinPointStudio/releases/latest/download/release-notes-win.html
      </sparkle:releaseNotesLink>
      <pubDate>Tue, 17 Jun 2026 09:00:00 +0000</pubDate>
      <enclosure
        url="https://github.com/PinPoint-Golf/PinPointStudio/releases/download/v0.1-alpha2/PinPointStudioSetup-v0.1-alpha2-core.exe"
        sparkle:version="10002"
        sparkle:shortVersionString="v0.1-alpha2"
        sparkle:os="windows"
        length="268435456"
        type="application/octet-stream"
        sparkle:edSignature="<base64 Ed25519 signature of the .exe>" />
    </item>
  </channel>
</rss>
```

Read by WinSparkle:
- `enclosure@url` → the installer to download (a **specific** release URL, not
  `latest/`, so the bytes match the signature exactly — §6).
- `sparkle:version` → the **monotonic build number** WinSparkle compares against the
  running app's build version (§7). **This is the comparison key, not the display
  string.**
- `sparkle:shortVersionString` → the human display version (`v0.1-alpha2`).
- `sparkle:edSignature` → the pinned-key signature WinSparkle verifies before
  running the installer (§6).
- `releaseNotesLink` (or an inline `<description>`) → "what's new", rendered in
  WinSparkle's own update window.

> **Channel caveat (same as Linux §7).** `releases/latest/download/…` resolves to
> the latest **non-prerelease** release. During alpha/beta we therefore **publish
> releases as non-prerelease** so both the Windows `latest/download/appcast-win.xml`
> and the Linux `gh-releases-zsync … latest` resolve. A real Stable/Beta split lands
> with GA as an `appSettings.updateChannel` pref that selects a different appcast
> asset name (e.g. `appcast-win-beta.xml`) — WinSparkle just needs a different URL,
> set before `win_sparkle_init()`.

### 4.2 Why an appcast, not the GitHub JSON API

The Linux updater queries `api.github.com/.../releases` directly because it parses
the feed itself. WinSparkle expects a Sparkle appcast and parses it for us, so we
generate that XML at release time (§ release pipeline) rather than teaching
WinSparkle the GitHub JSON shape. Net: **the Linux feed is the GitHub API; the
Windows feed is a generated XML on the same Releases** — both "GitHub Releases",
different representations dictated by each engine.

### 4.3 Transport — the `-core` installer is the only WinSparkle payload

There is **no delta** on Windows. WinSparkle downloads the `enclosure` installer and
runs it. The full installer is ~1.8 GB only because of the CUDA + cuDNN runtime
(`cmake/PinPointPackaging.cmake`, the `cuda` component). The existing packaging
already splits this and `packaging/build_installer.ps1` already produces the variants
`PinPointStudioSetup-<ver>-core.exe` and `…-cuda.exe`:

- **core** — app + Qt + ONNX Runtime + OpenCV + FFmpeg + Spinnaker + models + espeak
  data + yt-dlp. Hundreds of MB. **Hardware-agnostic** — runs on any machine, CPU or
  GPU; ORT just uses the CUDA EP *if* the runtime is present (the existing
  `nvcuda.dll` probe in `KokoroTTSEngine.cpp` already degrades gracefully when it is
  not).
- **cuda** — the NVIDIA CUDA + cuDNN runtime. ~1 GB+, changes only when ORT/CUDA
  versions bump (rare), and is **only useful on a machine with an NVIDIA GPU**.

**Decision: the WinSparkle enclosure is ALWAYS the `-core` installer.** Every user, GPU
or not, auto-updates the small hardware-agnostic app. WinSparkle never ships the CUDA
gigabyte. The CUDA runtime is **not** part of the app's version-driven update at all —
it is a separate, hardware-driven component the app manages itself (§4.4). This is what
makes updates small *and* lets us "adapt to the hardware in front of us rather than
make the user reinstall" when that hardware changes.

For the `-core` installer to upgrade in place **without disturbing a previously-
installed CUDA runtime**, the CUDA runtime is installed under its **own Inno `AppId`**,
separate from the core app's stable `AppId`. Today's single two-component installer is
therefore split into two Inno products: the **core app** (the existing stable `AppId`,
the WinSparkle target) and a **CUDA runtime** (its own `AppId`, its own version — §4.4).
A `-core` auto-update then *provably* cannot touch CUDA files (they are not in its
uninstall log), and CUDA is installed/updated/removed on its own cadence. (The
implementation plan sequences this `AppId` split as P3; the WinSparkle client is
agnostic — it only ever downloads the `-core` enclosure it is handed.)

### 4.4 Hardware-adaptive CUDA runtime (don't make the user reinstall)

A user's hardware changes over time — they add a GPU, swap a card, or move the install
to a different machine. The app must **adapt to the hardware present at each launch**,
not freeze whatever was decided at first install. So the CUDA runtime is treated as a
hardware-driven sidecar the app keeps in sync, **independent of the WinSparkle app
update**:

At startup (and whenever the resource monitor's GPU detection changes), the app
evaluates two facts it can already determine locally:
1. **Is an NVIDIA GPU + driver present?** — the existing `nvcuda.dll` probe (the same
   one `KokoroTTSEngine.cpp` uses to decide whether to try the ORT CUDA EP).
2. **Is the matching CUDA runtime installed?** — checked by the **presence of
   `cudnn64_<major>.dll` next to the executable** (`cudnn64_9.dll` today). The filename
   encodes the cuDNN major this build requires, so "installed" and "right major" are a
   single, dependency-free file check — no registry or installer cooperation needed.

From those two facts:

| GPU present? | CUDA runtime state | Action |
|---|---|---|
| No | (any) | Nothing. Core app runs CPU/cloud. A stale runtime is harmless. |
| Yes | Absent | **Offer** "Enable GPU acceleration" → user fetches + runs the `-cuda` installer (v1, below). |
| Yes | Present, current major | Nothing — already accelerated. |
| Yes | Present, **older** major | (GA) offer to update the runtime to the required major. |

**As-built decisions (v1):**
- **Same install dir, own `AppId` — no DLL relocation.** The `-cuda` installer targets
  the *same* per-user directory as core (both `PrivilegesRequired=lowest` →
  `%LOCALAPPDATA%\Programs\PinPointStudio`), so the cuDNN/CUDA DLLs land next to the exe
  exactly as today and need **no DLL-search-path changes**. Because they are recorded in
  the `-cuda` product's **own uninstall log** (its own `AppId`), a `-core` auto-update
  never lists or removes them. This is the whole `AppId` split — install location is
  shared, *identity* is not.
- **Version-independent of the app.** A user already on the latest app version who *adds
  a GPU* is offered the runtime on the next launch — there is no app update to piggy-back
  on, so this cannot be a WinSparkle appcast item (WinSparkle only offers strictly-newer
  *app* versions). It is its own check, re-evaluated every launch.
- **The install action is user-initiated and secure-by-construction (v1).** WinSparkle
  exposes **no app-side "verify this file" API**, so the app cannot reuse the EdDSA gate
  to verify a self-downloaded `-cuda` installer. Rather than auto-run unverified bytes,
  v1 **opens the official GitHub Releases page** so the user fetches the `-cuda` installer
  over HTTPS from the trusted source and runs it (the running app + AppMutex let it close
  for the DLL drop). No new crypto dependency, no weakened trust.

> **v1 vs GA.** v1: detect (GPU + `cudnn64_9.dll`) → a Settings → General "GPU
> acceleration" row that offers **Install GPU acceleration** (opens the release page),
> re-checked each launch. GA, to make it a one-click in-app fetch without weakening the
> pin, needs **either** a vendored Ed25519 verifier (the `-cuda` installer is signed with
> the same pinned key via `winsparkle-tool sign`, and the app verifies it before running)
> **or** an Authenticode-signed `-cuda` installer (Windows verifies the publisher on
> launch). GA can also track the installed major in the registry to handle the "older
> major" row. Either way the principle holds: **the app adapts the runtime to the
> hardware; the user never reinstalls to gain or lose GPU support.** This sidecar is
> sequenced as P3; the core WinSparkle app updater (§4.1–4.3, §5–§9) does not depend on
> it.

## 5. UX — prompt-then-install (WinSparkle's native UI)

**WinSparkle owns the update UI on Windows, exactly as Sparkle does on macOS.** We
do **not** drive a custom QML flow from WinSparkle: its API intentionally keeps the
"update available → release notes → download progress → install" window inside the
library (there is no granular progress callback to feed a custom UI; the public
surface is "check with UI" / "check without UI" / a few lifecycle callbacks). Fighting
that to re-skin it in QML is exactly the kind of effort the "use WinSparkle" decision
exists to avoid. So:

**A. Launch check (passive).** Gated on the existing `appSettings.checkForUpdates`
pref (default **on**). On every startup the backend fires a **silent** check ~3 s in
(`WinSparkleUpdater::launchCheck()` → `win_sparkle_check_update_without_ui()`), which
shows **WinSparkle's own** "update available" window **only when** an update exists —
never nags when up to date. This is the Windows parity for the Linux 4 s launch check;
the existing toggle copy in `GeneralPanel.qml` ("Checks on launch — never downloads
without confirmation") fits exactly.

> **Why an explicit launch check rather than relying on WinSparkle's automatic check.**
> `win_sparkle_set_automatic_check_for_updates(pref)` + `win_sparkle_init()` give
> WinSparkle a built-in background check, but it is **interval-gated**, not per-launch:
> `init()` only checks if the configured interval (`win_sparkle_set_update_check_interval`,
> 24 h; floored by WinSparkle at 1 h) has elapsed since WinSparkle's last-check timestamp
> (stored in the registry, `HKCU\Software\Mark Liversedge\PinPoint Studio\WinSparkle`).
> So on its own it would **not** fire on the first run after install (no prior timestamp →
> it records a baseline and waits) nor on launches inside the interval. The **Linux**
> backend (`LinuxAppImageBackend`) instead fires an explicit
> `QTimer::singleShot(4000, …, &checkNow)` on **every** startup. To match that, the
> Windows backend forces a silent `win_sparkle_check_update_without_ui()` from
> `launchCheck()` each startup (gated on the same pref). The two layers coexist: our
> explicit call covers **every launch**; WinSparkle's interval timer covers **long-running
> sessions** that never restart. (A redundant check on the rare launch where the interval
> has also elapsed is harmless — WinSparkle serialises checks.) The automatic-check pref
> is still mapped to WinSparkle via `win_sparkle_set_automatic_check_for_updates()` so the
> interval timer and the launch check are governed by the **same** toggle.

**B. Settings → General (active).** The **Version row** in `GeneralPanel.qml` is the
manual surface, shared with Linux but behaving per-platform:
- The exis­ting **"Check for updates automatically"** toggle binds to
  `appSettings.checkForUpdates`; the controller forwards the change to the active
  backend's `setAutomaticChecks()`, which on Windows calls
  `win_sparkle_set_automatic_check_for_updates()` (`WinSparkleBackend` → `WinSparkleUpdater`).
- The **Version badge + action chip**: on Windows the rich live states
  (`downloading`/`verifying`/`ready`) never appear — those are WinSparkle's window.
  The badge shows the current version and the action chip is **"Check for updates"**,
  which calls `updateController.checkNow()` → `win_sparkle_check_update_with_ui()`
  (a manual check shows WinSparkle's UI whether or not an update is found, and
  ignores any prior "Skip this version"). The chip is shown whenever
  `updateController.supported` (true on an installed Windows build).

So `GeneralPanel.qml` needs **only** a Windows-aware `mode`/`supported` path, not a
new state machine. The Linux-specific badge palette (`downloading N%`,
`verifying`, `restart to update`) simply never triggers on Windows because the
Windows controller never enters those states.

### 5.1 Session safety

The single-active-session invariant holds identically. WinSparkle asks the host
before it quits the app to run the installer, via two callbacks
(`win_sparkle_set_can_shutdown_callback`, `win_sparkle_set_shutdown_request_callback`):

- **can-shutdown** returns `FALSE` while `SessionController::running()` — WinSparkle
  then will not launch the installer / will keep the app alive (the user finishes the
  session first, mirroring the Linux "End the session to install" guard).
- **shutdown-request** performs a *graceful* quit through the normal close path
  (`QCoreApplication::quit()` on the GUI thread), so controllers' `aboutToQuit`
  teardown runs and the merger stops cleanly — never a hard `TerminateProcess`.

> **Threading caveat (from the WinSparkle headers):** neither callback runs on the
> app's main thread. They must be thread-safe. Implementation: read a
> `std::atomic<bool>` mirror of `sessionController.running()` in *can-shutdown*, and
> *post* the quit to the GUI thread (`QMetaObject::invokeMethod(qApp, …,
> Qt::QueuedConnection)`) in *shutdown-request*. Do **not** touch `QObject`s directly
> from the callback thread.

## 6. Security model — EdDSA signature pinning

Transport integrity is HTTPS; **authenticity** is a pinned **Ed25519** signature on
the enclosure — the same trust model as macOS Sparkle and the direct analogue of the
Linux pinned-GPG gate. "Any signature present" is **not** acceptable; the key is
pinned.

**Build side — signing is OFFLINE/LOCAL (identical custody rules to Linux §6).**
- Generate an Ed25519 key pair with WinSparkle's bundled tool (`winsparkle-tool` /
  `sign_update`, shipped in the WinSparkle binary/NuGet `bin/`). This is a **separate
  key from the Linux GPG key** — same algorithm family (Ed25519), different format
  (Sparkle's raw base64, not OpenPGP). The **private key never leaves the
  maintainer's machine and is NOT a CI secret.**
- The **public** half (base64) is compiled into the app and pinned: passed to
  `win_sparkle_set_eddsa_public_key()` at init (or embedded as the `EdDSAPub`/`EDDSA`
  Win32 resource). It also lives in the repo for transparency, e.g.
  `src/Resources/keys/pinpoint_release_win_eddsa.pub`, mirroring the Linux
  `pinpoint_release_pubkey.asc`.
- At release time the maintainer signs the **exact** installer bytes that will be
  published (`sign_update PinPointStudioSetup-…-core.exe`), takes the emitted base64
  signature, and writes it into the `sparkle:edSignature` of `appcast-win.xml`.

**Client side (the gate is WinSparkle's).** WinSparkle downloads the enclosure and,
**before running the installer**, verifies its Ed25519 signature against the pinned
public key; a missing/invalid/wrong-key signature is rejected and the installer is
never executed. Because the signed artifact is the *whole installer* (not a delta),
there is no "reconstruct then verify" subtlety as on Linux — WinSparkle verifies the
downloaded file as-is. Pinning (the key is in the binary), not mere presence, is the
requirement; this is enforced by WinSparkle when (and only when) a valid EdDSA public
key is configured — so the key **must** be set at init, never left unset.

> **Defence in depth (optional, recommended for GA).** Also **Authenticode-sign**
> the installer `.exe` and the app `.exe`/DLLs with an OV/EV code-signing cert. That
> removes SmartScreen friction on first install and is orthogonal to the WinSparkle
> EdDSA gate (one satisfies Windows/the user, the other satisfies the updater).
> Out of scope for v1 (no cert yet); the EdDSA pin is the auto-update trust anchor.

## 7. Versioning & channels

WinSparkle compares the **running app's build version** against the appcast's
`sparkle:version`. Today the project carries **two** version sources that are not in
lockstep, and this must be reconciled:

- `src/Core/version.h` → `PINPOINT_VERSION_STRING` = `"v0.1-alpha2"` — the **display**
  version (`appSettings.appVersion`, the Linux updater, the Settings badge).
- CMake `project(PinPointStudio VERSION 0.1.0)` → `PP_APP_VERSION`, the CPack/Inno
  version, and the installer filename (`…Setup-0.1.0…`). **Not** auto-derived from
  `version.h`, and currently inconsistent with it (`0.1.0` vs `v0.1-alpha2`).

**Design rule:**
- **Display** version (`win_sparkle_set_app_details(company, app, version)` and
  `sparkle:shortVersionString`) = `PINPOINT_VERSION_STRING` (`v0.1-alpha2`). This is
  also the registry/User-Agent identity (`HKCU\Software\<company>\<app>\WinSparkle`).
- **Comparison** key (`win_sparkle_set_app_build_version(build)` and
  `sparkle:version`) = a **monotonic integer** that strictly increases every release,
  derived from `version.h` by one formula used in **both** the C++ (passed to
  WinSparkle) and the appcast generator, so they cannot drift. Recommended single
  source of truth: add `PINPOINT_VERSION_BUILD` to `version.h` (a hand-bumped
  integer), or compute it as
  `MAJOR*1_000_000 + MINOR*10_000 + PATCH*100 + prereleaseOrdinal` where
  `prereleaseOrdinal` encodes `-alphaN < -betaN < -rcN < (release)` exactly like the
  Linux `postfixRank`. Using `set_app_build_version` is the Sparkle-blessed way to
  get correct ordering for prerelease versions (string comparison of `0.1-alpha2`
  vs `0.1` is unreliable; an integer is not).
- **Installer** version (CMake `project(VERSION)`, Inno `AppVersion`) should be
  bumped **in lockstep** with `version.h`. Ideally derive `project(VERSION)` from
  `version.h` so there is one edit per release; at minimum, the release runbook makes
  bumping both a single checklist step. Inno uses its own `AppVersion` for its
  upgrade detection, independent of WinSparkle's compare — both must move forward
  together or an installed user sees "downgrade"/"already installed" oddities.

**Compare rule (who decides "newer"):** WinSparkle, using the integer
`sparkle:version` vs the running `set_app_build_version`. Offer only if strictly
greater. This is the Windows analogue of the Linux `compareVersion()`; we delegate it
to WinSparkle rather than re-implement it.

**Channels:** v1 ships a single implicit channel, selected — as on Linux — by
publishing releases as **non-prerelease** so `latest/download/appcast-win.xml`
resolves. A future `appSettings.updateChannel` pref points WinSparkle at a different
appcast asset (`appcast-win-beta.xml`) before `win_sparkle_init()`.

## 8. Threading & failure modes

- **WinSparkle runs its own threads.** `win_sparkle_init()` returns immediately and
  does its check/download/UI off the main thread; we call it once after the main
  window is up, and `win_sparkle_cleanup()` on shutdown (from `aboutToQuit`, before
  Qt tears down — alongside the existing `eventBuffer.stop()`). No PinPoint QTimer
  poll loop (unlike the Linux `appimageupdatetool` driver) — WinSparkle is the loop.
- **Callbacks are off-main-thread** (§5.1) — atomic reads + queued invokes only.
- **Network down / GitHub 5xx / rate-limit:** WinSparkle surfaces a quiet error in
  its own UI for a manual check, and silently no-ops a failed automatic check; it
  never blocks startup. An optional `win_sparkle_set_error_callback` can log to
  `PpMessageLog` for diagnostics.
- **Bad/missing/wrong-key signature:** WinSparkle rejects the file and does not run
  the installer (§6) — treated as hostile, same posture as the Linux gate.
- **Not an installed build (dev tree):** the Windows controller stays inert
  (`supported = false`, badge reads "Development build"), WinSparkle is never
  initialised — the analogue of `$APPIMAGE` unset.
- **Installer needs elevation:** it must not. The per-user, `PrivilegesRequired=
  lowest` install (already configured) is precisely what lets WinSparkle run the
  installer and replace files with **no UAC prompt**. A per-machine install would
  break silent auto-update; this design depends on the existing per-user choice.
- **Update interrupted / app killed mid-install:** the installer is transactional
  (Inno) and the old install remains runnable until the installer completes; the
  `AppMutex` lets Inno detect and close a running instance for in-place replacement.

## 9. Relaunch / install

WinSparkle's install path, with our session guard in the loop:
1. User accepts the update in WinSparkle's window → WinSparkle calls **can-shutdown**.
   We return `FALSE` if a session is live (the user is told to finish first); else
   `TRUE`.
2. WinSparkle calls **shutdown-request**; we post a graceful `QCoreApplication::quit()`
   to the GUI thread. Controllers tear down, the merger stops.
3. WinSparkle launches the downloaded `-core` installer. Inno (per-user, no UAC)
   detects the now-exiting instance via `AppMutex`, upgrades in place under the
   stable `AppId`, and — via `CPACK_INNOSETUP_RUN_EXECUTABLES` — relaunches
   `PinPointStudio` on finish.
4. The user lands on the new version; the previously-installed CUDA runtime is
   untouched (§4.3).

There is no PinPoint-side file swap (unlike the Linux `rename(2)` over `$APPIMAGE`) —
the installer is the swap mechanism, and it is the artifact WinSparkle verified.

## 10. What this is **not** doing

- Not re-implementing Sparkle in QML on Windows. WinSparkle's native UI is the
  Windows idiom, as the QML banner is the Linux idiom and Sparkle's UI is the macOS
  idiom.
- Not delivering binary deltas. The size win is the core/cuda split, not patching.
- Not requiring admin/UAC, a service, or a per-machine install. Per-user is load-
  bearing for silent in-place update.
- Not Authenticode-signing in v1 (recommended for GA; orthogonal to the EdDSA gate).
- Not changing the Linux updater. The two share only the `updateController` QML
  context property and the "GitHub Releases, locally-signed, prompt-then-install"
  philosophy; their engines are independent.

---

### Appendix A — WinSparkle API used

Configured once at startup, before `win_sparkle_init()` (all from `<winsparkle.h>`,
linked against `WinSparkle.dll`):

| Call | Use |
|---|---|
| `win_sparkle_set_appcast_url(".../releases/latest/download/appcast-win.xml")` | static feed URL (GitHub latest-release redirect, §4.1) |
| `win_sparkle_set_app_details(L"Mark Liversedge", L"PinPoint Studio", L"v0.1-alpha2")` | display version + registry/UA identity (§7) |
| `win_sparkle_set_app_build_version(L"100002")` | monotonic compare key vs `sparkle:version` (§7) |
| `win_sparkle_set_eddsa_public_key("<base64>")` | **the pinned trust anchor** — must succeed (returns 1), else refuse to init (§6) |
| `win_sparkle_set_automatic_check_for_updates(appSettings.checkForUpdates)` | maps the existing launch-check pref (§5A) |
| `win_sparkle_set_update_check_interval(86400)` | daily background check (≥ 3600 floor) |
| `win_sparkle_set_can_shutdown_callback(cb)` / `…_shutdown_request_callback(cb)` | session-safety gate + graceful quit (§5.1, §9) |
| `win_sparkle_set_error_callback(cb)` | optional → log to `PpMessageLog` |
| `win_sparkle_init()` | start (after the main window is shown) |
| `win_sparkle_check_update_without_ui()` | the passive **launch check** — silent, every startup, surfaces UI only if an update exists (§5A) |
| `win_sparkle_check_update_with_ui()` | the Settings "Check for updates" action (§5B) |
| `win_sparkle_cleanup()` | on `aboutToQuit`, before Qt teardown (§8) |

### Appendix B — release artifacts (per GitHub Release, Windows side)

```
PinPointStudioSetup-<ver>-core.exe     # the WinSparkle enclosure (EdDSA-signed bytes)
appcast-win.xml                        # the Sparkle feed: version, URL, edSignature, notes link
release-notes-win.html                 # (optional) "what's new", shown in WinSparkle's window
PinPointStudioSetup-<ver>-cuda.exe     # standalone CUDA runtime (own AppId) — fetched adaptively by the app when a GPU is present (§4.4)
```
`<ver>` = the **`version.h` string / release tag** (e.g. `v0.1-alpha3`) — the asset
filename reflects the tag, while the *internal* Inno `AppVersion` is the numeric
`MAJOR.MINOR.BUILD` CMake derives from the same `version.h`. `appcast-win.xml` and the
installer co-exist with the Linux
`*.AppImage*` assets on the **same** release; each platform's updater reads only its
own assets and ignores the other's (the Linux updater already skips Windows-only
releases, and WinSparkle only ever looks at `appcast-win.xml`).
</invoke>
