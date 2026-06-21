# PinPoint Studio — Linux In-App Update

*The authoritative design for self-updating the Linux build. Mirrors the macOS
(Sparkle) and Windows (WinSparkle) update stories with the native Linux idiom:
an **AppImage** updated in place by **libappimageupdate** over a **zsync delta**,
gated by a **pinned GPG signature**, surfaced as a **prompt-then-install** flow in
Settings → General and on launch. This doc is the contract; the phased build is
[`../implementation/linux_update_impl.md`](../implementation/linux_update_impl.md).*

---

## 1. Goals & non-goals

**Goals**
- Same product behaviour as Sparkle/WinSparkle: the app notices a newer release,
  tells the user what changed, downloads in the background **only after
  confirmation**, verifies authenticity, and relaunches into the new build.
- No Snap, no Flatpak, no system package manager, no root.
- Distribution and the update feed are **GitHub Releases** — no separate server.
- Delta updates: a point release ships a few MB, not the whole ~1–2 GB bundle.

**Non-goals**
- Background silent auto-install (explicitly rejected — see §5, prompt-then-install).
- Sandboxing. The AppImage runs unconfined; this app needs raw access to BLE
  (BlueZ/HCI), V4L2 + Aravis/Spinnaker cameras, CUDA/Vulkan/NVML, and audio.
- Per-file OS package management (`.deb`/apt). Out of scope.

## 2. The cross-platform mapping

| Concern | macOS | Windows | **Linux (this doc)** |
|---|---|---|---|
| Package | `.app`/`.dmg` | installer (NSIS/Inno) | **AppImage** (single file) |
| Update engine | Sparkle | WinSparkle | **libappimageupdate** |
| Feed / "appcast" | appcast XML | appcast XML | **GitHub Releases JSON** (§4.1) |
| Binary transport | full DMG | full installer | **zsync delta** (§4.2) |
| Authenticity | EdDSA on appcast | DSA/Ed on appcast | **pinned GPG sig on AppImage** (§6) |
| UX | prompt-then-install | prompt-then-install | **prompt-then-install** (§5) |

The split worth internalising: **GitHub Releases JSON is the appcast (metadata:
version, notes, date); zsync is the binary delta transport.** They are independent
and serve different parts of the flow.

## 3. Components

> **As-built transport decision (v1).** The update *engine* is the AppImageUpdate
> project's **`appimageupdatetool` CLI, driven via `QProcess`** — **not** the
> embedded `libappimageupdate` C++ library. Rationale: embedding pulls in
> gpgme + libcurl + zsync2 as build-time deps (absent on the dev host and fragile
> to vendor), whereas the CLI is a single self-contained binary we already bundle
> in the AppImage (P0), the controller stays pure Qt (Network + Core + Concurrent,
> all already linked), and the whole path is buildable/verifiable without new
> third-party deps. This is the fallback the original draft contemplated, promoted
> to the chosen path. The state machine, feed, signature gate, and UX below are
> identical either way; embedding `libappimageupdate` remains a future option if
> tighter in-process progress is wanted. Where this doc says "the update engine"
> it means `appimageupdatetool`; the zsync delta + embedded update-info contract
> is unchanged.

```
src/Update/
  update_controller.h/.cpp       UpdateController — QML context property `updateController`
                                 on ALL platforms. A thin façade: owns the QML property
                                 surface, the State→string mapping (the single source of
                                 truth for the banner strings), the relaunch session-safety
                                 guard, and ONE polymorphic UpdateBackend chosen by
                                 makeUpdateBackend(). No platform #ifdef in its body.
  update_backend.h               UpdateBackend — abstract base + pp::update::{State,OfferInfo}.
                                 One concrete subclass per OS engine family.
  update_backend_factory.h/.cpp  makeUpdateBackend() — the SOLE platform #ifdef ladder.
  linux_appimage_backend.h/.cpp  LinuxAppImageBackend — the Linux engine and the only
                                 backend that drives the rich state machine: GitHub feed
                                 query, the appimageupdatetool driver, signature gate, and
                                 relaunch. GUI-thread + async I/O. (This is where the flow
                                 below lives; it used to be inline in UpdateController.)
  linux_update_logic.h/.cpp      Pure decision logic (version compare, AppImage asset
                                 selection, gpg-status parse, placeholder-key refusal) —
                                 no QObject/network/process, unit-tested in isolation.
  appimage_update.h/.cpp         Thin wrapper that drives `appimageupdatetool` via QProcess
                                 (download-to-temp / progress parse), isolating the tool
                                 invocation and the "never overwrite before verify" rule.
  platform_target.h/.cpp         PlatformTarget{os,arch} — CPU arch as DATA, so the AppImage
                                 asset token (x86_64 / aarch64) is selected, not hardcoded.
  inert_update_backend.h         Unsupported fallback (a build with no engine for its OS).
  (Qt resource)                  pinpoint_release_pubkey.asc — the project's GPG public key,
                                 compiled in as a :/keys/ resource for signature pinning.
  tests/                         src/Update/tests — pure-logic + controller-policy suites
                                 (FakeUpdateBackend), wired into the test umbrella.
```

- The macOS (Sparkle) and Windows (WinSparkle) backends are the structural twins of
  `LinuxAppImageBackend` — thin `UpdateBackend`s whose engines own their own UI, so
  they report `ownsStateMachine() == false` and never enter the rich Downloading/
  Verifying/Ready states. See [`macos_update.md`](macos_update.md) /
  [`windows_update.md`](windows_update.md).
- `UpdateController` follows the established controller idiom (cf. `TtsController`,
  `SecretsBridge`): a `QObject` with `Q_PROPERTY`s for QML, registered in `main.cpp`
  as `updateController` on **all** platforms (so the shared QML binds uniformly). The
  Linux *engine* — `LinuxAppImageBackend` — is what is Linux-only; the factory returns
  the native backend on macOS/Windows and an inert fallback elsewhere.
- The Linux backend + its logic compile under `if(UNIX AND NOT APPLE)` (the factory only
  instantiates it there). When the process is **not running as an AppImage** (`$APPIMAGE`
  unset — dev builds, run-from-`build/`), the backend reports an inert `DevBuild` state
  and never touches the network.

## 4. The feed and the transport

### 4.1 Appcast — GitHub Releases JSON

The metadata feed is the GitHub REST API, queried with `QNetworkAccessManager`:

```
GET https://api.github.com/repos/PinPoint-Golf/PinPointStudio/releases/latest
Accept: application/vnd.github+json
```

From the JSON we read:
- `assets[]`   → the same-arch `*.AppImage` (+ its `.AppImage.sig`); the **offered version
  is parsed from the AppImage asset filename** (`PinPointStudio-<ver>-<arch>.AppImage`,
  derived from `version.h`), **not** from `tag_name` — see §7. The `<arch>` token is the
  running binary's own (`PlatformTarget::assetArchToken()` → `x86_64` today, `aarch64`
  for an ARM build), so an other-arch asset is never mistaken for an offer.
- `body`       → Markdown changelog shown in the prompt ("what's new")
- `published_at` → date shown in the prompt
- `tag_name`   → **not used** by the updater (the release process constrains its
  format, e.g. `Version0-1-0-alpha1`); the updater is independent of it

The updater scans releases newest-first and picks the first non-draft release that
actually carries a Linux `*-x86_64.AppImage` asset — Windows-only releases (installer
`.exe`s) are skipped, not errored.

> **Channel caveat (matters today).** `/releases/latest` returns the latest
> **non-prerelease, non-draft** release. We are at `v0.1-alpha1`; during
> alpha/beta every release is a *prerelease*, so `/releases/latest` returns 404.
> Resolution: the controller queries `/releases?per_page=10` and selects the
> newest entry **whose prerelease flag matches the active channel** (§7). A
> future "Stable only" preference simply filters `prerelease == false`.

### 4.2 Transport — zsync delta via embedded update information

Each release AppImage carries an embedded *update information* string (ELF
`.upd-info` section), written at build time by `appimagetool -u`:

```
gh-releases-zsync|PinPoint-Golf|PinPointStudio|latest|PinPointStudio-*-x86_64.AppImage.zsync
```

`appimagetool -u …` both embeds this string **and** emits the companion
`PinPointStudio-<ver>-x86_64.AppImage.zsync` control file, which is uploaded as a
release asset alongside the AppImage. At update time, `libappimageupdate`:
1. reads the embedded string from the running AppImage,
2. resolves `latest` against GitHub Releases to find the newest matching
   `.zsync` asset,
3. fetches the `.zsync` control file (block checksums),
4. computes the delta against the **local** AppImage and downloads only changed
   blocks (the bundled ONNX models, ORT, FFmpeg, Qt, OpenCV are byte-identical
   across a code-only release → near-zero transfer),
5. assembles the new AppImage as a temp file next to the original.

> `gh-releases-zsync`'s `latest` follows the same GitHub "latest non-prerelease"
> rule as §4.1. During the prerelease phase, the zsync transport must therefore
> point at a release that GitHub treats as latest, **or** we publish each release
> as non-prerelease and gate channels purely in the JSON query. Decision recorded
> in §7.

### 4.3 Why two mechanisms, not one

The engine alone answers *"do the bytes differ?"*, not *"what version, what
changed?"*. Sparkle-style prompts need the latter — hence the JSON feed. The version
shown (and compared against the running `version.h`) is parsed from the **AppImage
asset filename**; the offer is gated on that comparison being strictly newer.

## 5. UX — prompt-then-install

Two surfaces, one state machine. **Never download without confirmation; never
relaunch without confirmation.**

**A. Launch check (passive).** On startup, iff `appSettings.checkForUpdates`
(existing pref, default true) and running as an AppImage: after the window is up
and **no session is starting**, query the feed (§4.1) + `checkForChanges()`. If an
update is offered, show a **non-modal, dismissible banner** (house style: muted
chrome, accent affordance — cf. the existing badge idiom) reading *"PinPoint
Studio <ver> is available"* with **What's new / Install / Later**. Dismiss is
remembered per-version (`skippedVersion` pref) so the same release is not re-nagged
every launch; a *newer* version overrides the skip.

**B. Settings → General (active).** The scaffolding already exists and is wired to
the controller:
- The **"Check for updates automatically"** toggle (`setting_checkUpdates`) stays
  bound to `appSettings.checkForUpdates` — it gates surface **A** only; the manual
  check below always works.
- The **Version row** (`setting_version`) badge — today the hardcoded
  `"✓ Up to date"` — becomes a live reflection of `updateController.state`
  (§5.1), with a **"Check now"** action and, when an update is available, a
  **"Download & install <ver>"** button plus a disclosure showing the changelog.

### 5.1 State machine (`updateController.state`)

```
Idle ──checkNow()/launch──▶ Checking ──┬─ no update ─▶ UpToDate ─▶ (Idle)
                                       ├─ offered ───▶ UpdateAvailable
                                       └─ error ─────▶ Error
UpdateAvailable ──user: Install──▶ Downloading(progress 0..1)
Downloading ──done──▶ Verifying ──┬─ sig ok ──▶ ReadyToRelaunch
                                  └─ sig bad ─▶ Error (download discarded)
ReadyToRelaunch ──user: Relaunch now──▶ Relaunching ─▶ (process replaced)
ReadyToRelaunch ──user: On next launch──▶ (apply at next clean exit)
```

`Q_PROPERTY`s exposed to QML:
`state` (enum→string), `available` (bool), `latestVersion` (QString),
`releaseNotes` (QString, Markdown), `progress` (double 0..1),
`statusMessage` (QString, last libappimageupdate line), `errorString` (QString),
`isDevBuild` (bool). Signals mirror the existing controllers'
`*Changed` conventions.

### 5.2 Session safety

Updates must respect the single-active-session model. `UpdateController` holds a
`SessionController*`; **`Relaunch` is refused while a session is running** and the
button explains why ("End the session to install"). The launch banner (surface A)
is suppressed while a session is active or starting. This mirrors `Main.qml`'s
`onClosing` guard — relaunch routes through the same "is it safe to quit?" gate,
never `Qt.quit()`.

## 6. Security model — signature pinning

Transport integrity comes from zsync-over-HTTPS; **authenticity** comes from a
GPG signature, the analogue of Sparkle's EdDSA pinning. Weak/"any signature
present" validation is **not** acceptable.

**Build side — signing is OFFLINE/LOCAL.** The signing key (ed25519, sign-only,
passphrase-protected) **never leaves the maintainer's machine** — it is *not* a CI
secret. Its **public** half lives in the repo, compiled in as the Qt resource
`:/keys/pinpoint_release_pubkey.asc`, and its fingerprint is pinned in
`linux_appimage_backend.cpp` (`kPinnedKeyFpr`). At release time the maintainer produces a
detached **`*.AppImage.sig`** with `gpg --detach-sign` (passphrase via the local
agent) and uploads it; CI only builds the *unsigned* AppImage and stages a **draft**
release. Step-by-step: [`../implementation/linux_release_runbook.md`](../implementation/linux_release_runbook.md).
Rationale: this key is the pinned root of trust for auto-updates to every user, so
keeping it off GitHub limits the blast radius of a CI/secret compromise.

We deliberately do **not** use `appimagetool`'s embedded `--sign`: the gate verifies
the detached `.sig` (below). Because zsync reconstructs the target AppImage
**byte-for-byte**, the detached signature over the released file verifies against the
locally-assembled copy. (A release is only discoverable by the updater once it is
published, non-prerelease, and carries the `.sig` — drafts are skipped.)

**Client side (the gate).** After the update engine assembles the new file **into a
temp/working path** (never the live `$APPIMAGE` — see §9) and **before** any swap or
relaunch, `LinuxAppImageBackend` verifies it was signed by **exactly** the pinned key
fingerprint (`verifySignatureBlocking()`, off the GUI thread):
1. create an **ephemeral** GnuPG home (temp `GNUPGHOME`, never the user's keyring)
   and import `:/keys/pinpoint_release_pubkey.asc`,
2. download the release's detached `*.AppImage.sig` asset,
3. `gpg --status-fd … --verify <sig> <newfile>` and parse the status output,
4. assert `VALIDSIG`/`GOODSIG` **and** that the signing key fingerprint **equals**
   the pinned constant (reject unknown/extra keys),
5. on any failure: delete the downloaded file, surface `Error`, do **not** swap or
   relaunch.

Pinning, not mere presence, is the requirement. The verify runs off the GUI thread
(`QtConcurrent`).

## 7. Versioning & channels

- **Single source of truth:** `src/Core/version.h` → `PINPOINT_VERSION_STRING`
  (`"v<MAJOR>.<MINOR><POSTFIX>"`, today `v0.1-alpha1`). `package_appimage.sh`
  embeds this in the **AppImage asset filename** (`PinPointStudio-<ver>-x86_64.AppImage`).
- **The git tag is free-form.** GitHub/the release process constrains tag names
  (the first release used `Version0-1-0-alpha1`), so the updater does **not** parse
  the tag. The offered version is read from the AppImage asset filename instead, and
  the `gh-releases-zsync … latest` transport matches assets by filename glob, not by
  tag — both are tag-agnostic. (Dots are fine in filenames; the Windows `.exe`
  assets prove it.)
- **Compare rule:** parse `v<MAJOR>.<MINOR>[-<word><n>]` from the asset filename and
  compare to the running `version.h`; offer only if strictly newer. Ordering:
  numeric `MAJOR.MINOR`, then postfix `release > -rcN > -betaN > -alphaN`, higher
  `N` newer within a type. Unparseable → "not newer" (never prompt on garbage).
- **Channels:** v1 ships a single implicit channel selected by the prerelease
  filter in §4.1. While the product is in alpha/beta, **releases are published as
  non-prerelease** so both `/releases/latest` and `gh-releases-zsync … latest`
  resolve (decision: simplest correct path now; a real Stable/Beta split lands
  with GA as a `appSettings.updateChannel` pref filtering `prerelease`).

## 8. Threading & failure modes

- **No GUI-thread stalls.** `AppImageUpdater::start()` does the multi-GB working-copy
  on a `QtConcurrent` worker, then drives `appimageupdatetool` as a `QProcess`,
  emitting `progress`/`status`/`finished` signals; `LinuxAppImageBackend` relays them
  to the controller (no GUI-thread polling loop). The feed query is async
  `QNetworkAccessManager`. Signature verification is a short blocking subprocess run on
  a `QtConcurrent` task so the UI never stalls.
- **Network down / GitHub 5xx / rate-limit:** `Checking` → `Error` with a quiet,
  non-blocking message; never blocks startup, never retries aggressively
  (one launch check; manual "Check now" otherwise).
- **Partial/aborted download:** zsync resumes from the partial on next attempt;
  the temp file is the only thing touched, the running AppImage is untouched
  until verified relaunch.
- **Signature failure:** treated as hostile — discard, `Error`, no relaunch (§6).
- **Not an AppImage (`$APPIMAGE` unset):** `isDevBuild = true`, all actions inert,
  Version badge reads "Development build".
- **Read-only location:** in-place overwrite needs write access to the AppImage
  file. If the file is not writable (e.g. installed to `/opt` by an admin),
  surface `Error` with guidance rather than failing silently; the assembled
  new file is left in the user cache for a manual move.

## 9. Relaunch

**Invariant: the live `$APPIMAGE` is never replaced with unverified bytes.** The
engine assembles the new image into a working file alongside `$APPIMAGE` (the local
copy is the zsync delta seed). Only **after** the signature gate (§6) passes does the
controller `rename(2)` the verified file over `$APPIMAGE` (atomic, same filesystem).
The running process keeps executing from the old inode — Linux unlinks-but-retains
the open file, safe until exit. Then, on user confirm:
`QProcess::startDetached($APPIMAGE)` and quit through the normal close path (session
guard honoured). **"On next launch"** defers the relaunch only — the verified swap
has already happened, so exiting normally lands the user on the new build next time.

## 10. What this is **not** doing

- Not bundling an updater UI from AppImageUpdate's own GTK frontend — the prompt
  is native PinPoint QML.
- Not using `appimaged`/desktop-integration daemons — irrelevant to in-app update.
- Not delta-ing across architecture or Qt-major bumps specially; those are just
  larger zsync deltas, still correct.

---

### Appendix A — `appimageupdatetool` invocations used

The bundled CLI is driven via `QProcess` (paths/flags centralised in
`appimage_update.cpp`; exact flags pinned against the bundled tool version in P0):

| Invocation | Use |
|---|---|
| `appimageupdatetool --check-for-update <copy>` | exit 1 = update available, 0 = none, 2 = error → offer gate |
| `appimageupdatetool --overwrite <copy>` | assemble the delta into the working copy; stdout `NN%` parsed → `progress` |
| (stdout lines) | surfaced verbatim → `statusMessage` |
| `gpg --status-fd … --verify <sig> <copy>` | §6 signature-pinning gate over the assembled file |
| `rename(2)` `<copy>` → `$APPIMAGE` | atomic swap, only after the gate passes (§9) |

The working `<copy>` is `$APPIMAGE` copied into the user cache first, so the live
binary is never the zsync target (the §9 invariant). Doubled disk I/O is the known
cost of the CLI path vs an embedded library writing its own temp; acceptable.

### Appendix B — release artifacts (per GitHub Release)

```
PinPointStudio-<ver>-x86_64.AppImage          # the app (gh-releases-zsync update-info embedded)
PinPointStudio-<ver>-x86_64.AppImage.zsync    # zsync control file (delta source)
PinPointStudio-<ver>-x86_64.AppImage.sig      # detached GPG sig — THE trust anchor the §6 gate verifies
```
`<ver>` = `PINPOINT_VERSION_STRING` from `version.h` (e.g. `v0.1-alpha1`) — this
filename carries the version the updater reads. The git **tag** is independent and
free-form. The release `body` is the changelog rendered in the prompt.
