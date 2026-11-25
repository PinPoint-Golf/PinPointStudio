# Windows Release Runbook

How to cut a Windows release that the in-app updater (WinSparkle) will trust and
offer to users. Design: [`windows_update.md`](../../docs/design/windows_update.md). The rules these
steps carry out are the release controls ([`controls/release.md`](../controls/release.md)); a step
cites the control's id.

**The key's custody is [BT-32](../controls/release.md):** the EdDSA signing key never leaves your machine.
GitHub only ever sees the **public** key (committed) and the per-installer
**signature** (inside `appcast-win.xml`).

---

## Do this ONCE (first time only)

You only generate and pin the key a single time, ever (until you deliberately rotate
it). After this, skip straight to the per-release checklist.

1. **Install the tools** (see [`../../BUILDING.md`](../../BUILDING.md)): the GitHub
   CLI (`gh auth login`) and Inno Setup 6 (ISCC). `winsparkle-tool.exe` is fetched by
   CMake — after any build it's under
   `build\**\_deps\winsparkle-src\bin\winsparkle-tool.exe`.

2. **Generate the signing key OFFLINE:**
   ```powershell
   $WS = (Get-ChildItem build -Recurse -Filter winsparkle-tool.exe | Select-Object -First 1).FullName
   # $KeyFile: the signing key file, named in the machine's local file. Keep it as BT-32 says.
   if (-not $KeyFile) { throw 'set $KeyFile from the local file' }
   & $WS generate-key --file $KeyFile
   & $WS public-key   --private-key-file $KeyFile   # prints "Public key: <base64>"
   ```

3. **Pin the public key:** paste just the `<base64>` line (no "Public key:" prefix)
   into `src/Resources/keys/pinpoint_release_win_eddsa.pub`, replacing
   `PLACEHOLDER_UNTIL_P2_KEY_GENERATED`. Commit it.

   > Until a build carrying the real key is in users' hands, the updater is inert by
   > design (it refuses the placeholder). So the **first** release that includes the
   > pinned key is what "bootstraps" auto-update — users must install that one
   > normally; every release *after* it can auto-update. Plan key rollout one release
   > ahead of when you need updates to work.

4. **The installer's firewall rule — done in v0.1-beta5, `packaging/firewall_rule.pas`.**
   PinPointStudio is the PPCP *host*: the phone dials us, so we listen (and so does the
   GSPro Open Connect listener on 921). The first time an unregistered exe binds a listening
   socket, Windows raises the "Security Alert" box — and a user who clicks Cancel gets a
   permanent **block** rule, after which pairing never works again and nothing in the UI
   explains why. A golf pro will click Cancel. Clicking Allow with the default box ticked is
   barely better: it writes allow rules for **Private only** (every install on the dev box
   had exactly that before beta5), and Windows never asks again on a Public network.

   The installer does **not** elevate — it is per-user (`PrivilegesRequired=lowest`) so
   WinSparkle can update in place. So the rule is created by a `[Code]` step at
   `ssPostInstall`, included through `CPACK_INNOSETUP_CODE_FILES`:
   1. Read the rules **unelevated** (a standard user may read them) and stop if the exe has
      an enabled inbound allow rule on profile **Any** and no enabled inbound block rule.
      Every upgrade after the first takes this path — no UAC prompt.
   2. Otherwise `ShellExec('runas')` PowerShell **once**: delete every inbound rule naming
      the exe (the alert's block and Private-only rules — block beats allow, so these must
      go) or in group `PinPoint Studio` (our rule for an old install path), then add one
      rule `PinPoint Studio`, inbound, allow, program `{app}\bin\PinPointStudio.exe`,
      profile Any. Then re-check rather than trust the exit code.
   3. A declined UAC prompt does not fail the install: it logs, tells the user (suppressible
      message), and the next run of the installer asks again.

   - **Program-scoped, not port-scoped** — narrower, and it survives a port change.
   - **Profile Any** — a studio PC is often classified Public, because Windows asks "do you
     want this PC to be discoverable" on first connect and the cautious answer is No.
   - **Path match is case-insensitive** — the alert stores the path lowercased.
   - The `-cuda` package clears `CPACK_INNOSETUP_CODE_FILES`; it has no exe.
   - The rule is **left on uninstall** — removing it needs the same elevation, and an
     allow rule for a deleted exe admits nothing.
   - Look in the Inno log (`/LOG`) for lines starting `Firewall:`.

   > This is the **TCP listener only**. Discovery needs no UDP 5353 rule: with the native
   > `windns.h` API (`DnsServiceRegister`, Win10 1703+) the OS resolver owns that socket and
   > Windows ships its own mDNS rules for it. Apple's Bonjour would need one — one more reason
   > not to take that dependency.

   A standard user cannot add a firewall rule, and a UAC prompt mid-session is worse than the
   problem, so this cannot be done at runtime — the installer's one prompt is the place. The app may *detect* and log it (`INetFwPolicy2`,
   `INetworkListManager`) but must not nag: `PPCP-RV` 3.6a forbids treating discovery failure as
   an error state and 3.6b requires the fallback to be silent.

---

## Do this FOR EACH RELEASE

### 1. Bump the version — `src/Core/version.h` only
Edit these and commit + push:
- `PINPOINT_VERSION_MAJOR` / `PINPOINT_VERSION_MINOR` / `PINPOINT_VERSION_POSTFIX`
  — the human version (e.g. `-alpha3`, or `""` for a clean release).
- **`PINPOINT_VERSION_BUILD`** — the monotonic integer the updater compares. It is
  strictly larger than the last release's ([BT-24](../controls/release.md)), or WinSparkle won't offer the update.
  Formula in the file: `MAJOR*1_000_000 + MINOR*10_000 + PATCH*100 + prerelease`.

That's the *only* version edit — CMake derives the installer version from it, and the
app's WinSparkle display/build version comes from it too. Nothing else to bump.

### 2. Run the full test suite — ALL must pass (MANDATORY GATE)
This is the gate [BT-17](../controls/release.md) requires, run as [BT-25](../controls/release.md) says. PinPoint's unit
tests are *not* part of the app build — they are nine standalone CTest suites that the
`tests/` umbrella configures, builds and runs as a single registry (see
[`../../BUILDING.md`](../../BUILDING.md) § Testing). Run this from a Developer (vcvars64)
shell with CMake + Ninja on `PATH`:
```powershell
# Set from the machine's local file first; unset, PowerShell would expand -j $Jobs to a bare -j.
if (-not $Jobs -or -not $OpenSSLRoot) { throw 'RELEASE BLOCKED — set $Jobs and $OpenSSLRoot from the local file' }
# The newest Qt kit ACTUALLY INSTALLED — a hardcoded version here goes stale silently and
# the failure reads like a broken suite rather than a wrong path. (Same rule as build_installer.ps1.)
$Qt  = (Get-ChildItem C:\Qt -Directory | Where-Object { Test-Path "$($_.FullName)\msvc2022_64\bin\qmake.exe" } |
        Sort-Object Name -Descending | Select-Object -First 1).FullName + '\msvc2022_64'
$env:PATH = "$Qt/bin;$env:PATH"                         # so test exes resolve Qt DLLs
if (Test-Path build/tests) { Remove-Item -Recurse -Force build/tests }  # see the warning below
# $OpenSSLRoot (the vcpkg OpenSSL install) and $Jobs (the build-job limit, BT-1) are named in the
# machine's local file. Never a bare -j.
cmake -S tests -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=$Qt" "-DOPENSSL_ROOT_DIR=$OpenSSLRoot"
if ($LASTEXITCODE) { Write-Error 'RELEASE BLOCKED — configure failed'; return }
cmake --build build/tests -j $Jobs
if ($LASTEXITCODE) { Write-Error 'RELEASE BLOCKED — build failed'; return }
ctest --test-dir build/tests --output-on-failure -j $Jobs -LE qml_ui
if ($LASTEXITCODE) { Write-Error 'RELEASE BLOCKED — tests failed'; return }
ctest --test-dir build/tests --output-on-failure -j $Jobs -L qml_ui --no-tests=error
if ($LASTEXITCODE) { Write-Error 'RELEASE BLOCKED — QML tests failed' } else { 'ALL SUITES PASSED' }
```
The last line must read `100% tests passed, 0 tests failed out of N` (N grows as suites
and tests are added: 121 as of v0.1-alpha11). If the umbrella fails to configure or build,
or any test fails, stop ([BT-17](../controls/release.md)): fix it and re-run before you tag. Do not proceed to
the build/sign steps below.

> Deleting `build/tests` first and using the umbrella are [BT-25](../controls/release.md), not housekeeping. The
> umbrella works because `tests/CMakeLists.txt` enumerates the suites, so one added later is
> picked up automatically, and it builds the `pinpoint_buffer` library first and then pulls
> in `src/Buffer/tests`.

### 3. Build the Windows build: the `-core` installer and the `-cuda` runtime
CI is not the release route ([BT-10](../controls/release.md)): both halves of the Windows build are built here,
locally. The release carries all four builds ([BT-29](../controls/release.md)); this is the Windows one.
```powershell
$TAG = '<the release tag>'                   # free-form; the same tag on all four build machines
powershell -ExecutionPolicy Bypass -File packaging\build_installer.ps1 -Components core
$exe = (Get-ChildItem build\Release-Installer -Recurse -Filter 'PinPointStudioSetup-*-core.exe' |
        Sort-Object LastWriteTime -Desc | Select-Object -First 1).FullName
powershell -ExecutionPolicy Bypass -File packaging\build_installer.ps1 -Components cuda
$cuda = (Get-ChildItem build\Release-Installer -Recurse -Filter 'PinPointStudioSetup-*-cuda.exe' |
         Sort-Object LastWriteTime -Desc | Select-Object -First 1).FullName
```
The `-core` installer is the update payload. The `-cuda` runtime goes on every release
([BT-29](../controls/release.md)): the app's GPU offer opens **`releases/latest`** and expects the user to find a
`-cuda.exe` there (`src/Update/cuda_runtime_controller.cpp`), even when the CUDA payload
itself hasn't changed. The install rules are gated on `CUDAToolkit_FOUND` / the cuDNN glob,
so it needs this machine's CUDA toolkit and cuDNN. It is **unsigned and not in the appcast**
— it is a separate Inno product with its own `AppId` (design §4.3–4.4), fetched manually by
the user, never by WinSparkle. Sanity-check its size against the previous release's
`-cuda.exe` (~1 GB); a few-MB file means CMake didn't find CUDA/cuDNN and the component
came out empty.

### 4. Sign it + generate the appcast
```powershell
if (-not $KeyFile) { throw 'set $KeyFile from the local file' }
powershell -ExecutionPolicy Bypass -File packaging\make_appcast.ps1 -PrivateKeyFile $KeyFile -Tag $TAG -Installer $exe
```
`$KeyFile` is the signing key file, named in the machine's local file. This signs `$exe`
with your offline key and writes `appcast-win.xml` next to it (the
feed item: version, enclosure URL, signature, length). Optional: add
`-NotesUrl https://github.com/PinPoint-Golf/PinPointStudio/releases/download/$TAG/release-notes-win.html`
to show "what's new" in WinSparkle's window.

### 5. Verify the signature locally (exactly as the app will)
```powershell
$WS  = (Get-ChildItem build -Recurse -Filter winsparkle-tool.exe | Select-Object -First 1).FullName
$pub = (Get-Content src\Resources\keys\pinpoint_release_win_eddsa.pub -Raw).Trim()
$sig = ([regex]::Match((Get-Content (Join-Path (Split-Path $exe) appcast-win.xml) -Raw),
        'edSignature="([^"]+)"')).Groups[1].Value
& $WS verify --public-key $pub --signature $sig $exe     # must report a VALID signature
```
If this doesn't verify, stop ([BT-28](../controls/release.md)). (Most common cause: you signed a rebuild
instead of the exact bytes you are uploading.)

### 6. Upload the Windows build into the release's draft
The four builds meet on one draft ([BT-29](../controls/release.md)): the first machine to finish creates it, the
others upload into it, and nothing is published until all four are there.
```powershell
$appcast = Join-Path (Split-Path $exe) 'appcast-win.xml'
gh release view $TAG -R PinPoint-Golf/PinPointStudio *> $null
if ($LASTEXITCODE) {                          # no draft yet: this machine is the first to finish
  gh release create $TAG -R PinPoint-Golf/PinPointStudio --draft `
     --title "PinPoint Studio $TAG" --notes "…release notes…"
}
# --clobber replaces anything stale; mind the order (BT-30).
gh release upload $TAG -R PinPoint-Golf/PinPointStudio --clobber $exe      # 1. the enclosure
if ($LASTEXITCODE) { throw 'core upload failed: do not upload the appcast (BT-30)' }
gh release upload $TAG -R PinPoint-Golf/PinPointStudio --clobber $cuda     # 2. the GPU runtime
if ($LASTEXITCODE) { throw 'cuda upload failed: do not upload the appcast (BT-30)' }
gh release upload $TAG -R PinPoint-Golf/PinPointStudio --clobber $appcast  # 3. the feed, LAST
```

> ### ⛔ UPLOAD THE `.exe` FILES BEFORE THE APPCAST ([BT-30](../controls/release.md)).
> A release with no feed is recoverable; a feed pointing at nothing is what users actually hit.
>
> **This cost three releases, each published before its Windows build was on it:**
> - **alpha11** — mac published first, the `windows` job skipped, and the release inherited
>   **alpha10's** `appcast-win.xml`. Because alpha11 was `Latest`, every client compared
>   `10010 > 10010` → false and saw "no newer version". Silent: the UI looked complete and
>   the stale enclosure still returned 200.
> - **beta2** — published carrying only the `.dmg` + mac appcast, so for ~40 minutes
>   `releases/latest/download/appcast-win.xml` **404'd**. Worse to diagnose, but it at
>   least fails closed.
> - **beta3** — the same again (2026-09-22, 404 from 16:33 UTC until recovery).
>
> The release now stays a draft until all four builds are on it ([BT-29](../controls/release.md)).

### 7. Publish, once all four builds are on the draft
Only the last machine to finish publishes, after checking the asset list ([BT-29](../controls/release.md)):
```powershell
gh release view $TAG -R PinPoint-Golf/PinPointStudio --json assets --jq '.assets[].name'
# expect all four builds: -core.exe, -cuda.exe, appcast-win.xml (Windows); .AppImage, .AppImage.zsync,
# .AppImage.sig (Linux); the x86_64 DMG + appcast-mac.xml; the arm64 DMG + appcast-mac-arm64.xml
gh release edit $TAG -R PinPoint-Golf/PinPointStudio --draft=false --prerelease=false
```
**Publish non-draft AND non-prerelease** ([BT-29](../controls/release.md)) — `releases/latest/download/appcast-win.xml`
(the URL baked into the app) only resolves to a non-prerelease release.

> Publishing creates the `v*` tag, which fires `release.yml`. Its **`guard` job detects the
> already-published release and skips the build jobs**, so CI won't re-draft your release or
> overwrite the signed installer — no need to cancel the run by hand.

**Verify the feed is actually live** ([BT-11](../controls/release.md)) — the step that would have caught all three:
```powershell
$r = Invoke-WebRequest 'https://github.com/PinPoint-Golf/PinPointStudio/releases/latest/download/appcast-win.xml' -UseBasicParsing
# ⚠ -UseBasicParsing hands back .Content as byte[], so DECODE before matching or every regex
#   silently returns empty and you "verify" nothing:
$xml = if ($r.Content -is [byte[]]) { [Text.Encoding]::UTF8.GetString($r.Content) } else { [string]$r.Content }
$xml    # sparkle:version must be the NEW build number, length must match the signed bytes
(Invoke-WebRequest ([regex]::Match($xml,'url="([^"]+)"')).Groups[1].Value -Method Head -UseBasicParsing).Headers['Content-Length']
```

### 8. Confirm it's live
```bash
gh release view "$TAG" -R PinPoint-Golf/PinPointStudio --json assets \
  --jq '.assets[].name'      # expect: -core.exe, -cuda.exe AND appcast-win.xml
```
On a machine running an **older installed** build: Settings → General → **Check for
updates** (or wait for the launch check) → WinSparkle offers it, downloads the
installer, verifies the signature, and relaunches on the new version — no UAC prompt.

---

## Local release — promote a build to the production account on THIS PC

For testing a change under the production setup (the other Windows account that runs
the official binary) without a GitHub release. Not for every change; for the ones that
need the real rig, real settings and the real install. Nothing here is signed,
appcast-ed or uploaded, and nothing here bumps the version.

**Why it is an installer and not a file copy.** The official copy is a per-user Inno
install under that account's `%LOCALAPPDATA%\Programs\PinPointStudio`, which no other
account can write into. The same installer a release produces, run *from that account*,
upgrades it in place with no admin prompt and keeps the uninstall log honest.

```powershell
# from the dev account, in the repo root — builds Release + the -core installer
# (incremental in build\Release-Installer) and stages it for the other account
powershell -ExecutionPolicy Bypass -File packaging\local_release.ps1
#   -Components both     when the CUDA/cuDNN runtime itself changed (~1.7 GB)
#   -InstallHere         also upgrade THIS account's install silently, as a smoke test
```

It stages `PinPointStudioSetup-<ver>-core-local-<sha>.exe` in the shared local-release folder
(the script's `-DropDir`; the machine's path is in its local file) and writes `LATEST.txt` beside
it (commit, time, and whether the tree had uncommitted changes). Staging or deleting there
is [BT-18](../controls/release.md): ask the owner first.

Then, **signed in as the production account:** run that file (Next/Next, it upgrades in
place), or unattended:
```powershell
"<the shared local-release folder>\<file>.exe" /SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS
```

Notes:
- **Same version number on purpose** ([BT-33](../controls/release.md)). A local release carries the last official
  `version.h`, so About reads the same and WinSparkle sees an equal build number and
  offers nothing. The sha in the file name is the only identity — put it in the bug
  report. Bump `BUILD` only for an official release, exactly as above.
- **The next official release lands over it normally** (its `BUILD` is higher).
- **A local build is never signed or uploaded, and reaches another account only through
  its installer** ([BT-33](../controls/release.md)).
- Prereqs are the runbook's own (VS, Qt 6.11 MSVC, Inno Setup 6, vcpkg OpenSSL); the
  script picks the newest installed Qt kit, since `build_installer.ps1`'s default names
  one this box may not have.

## Quick checklist (per release)

- [ ] Bump `PINPOINT_VERSION_BUILD` (+ MAJOR/MINOR/POSTFIX) in `version.h`, commit, push (BT-24)
- [ ] **Run the `tests/` umbrella in a fresh `build/tests` — `0 tests failed` (BT-17, BT-25; stop if any fail)**
- [ ] Build the `-core` installer and the `-cuda` runtime locally (`build_installer.ps1 -Components core`, then `cuda`; BT-10)
- [ ] `make_appcast.ps1` → signs + writes `appcast-win.xml`
- [ ] `winsparkle-tool verify` the signature — stop if it fails (BT-28)
- [ ] Upload `-core.exe`, `-cuda.exe`, then `appcast-win.xml` last, into the release's draft (BT-30, BT-29)
- [ ] Publish **non-draft, non-prerelease** only once all four builds are on the draft (BT-29)
- [ ] Confirm assets + test an update from an older build
- [ ] **Installer still creates the PPCP listener firewall rule — verify on a CLEAN VM**, never
      on an upgrade (BT-34; see ONCE §4)

---

## Gotchas & notes
- **Sign the bytes you publish** ([BT-28](../controls/release.md)): the installer you upload, never a rebuild.
- **`BUILD` always increases** ([BT-24](../controls/release.md)).
- **Never publish without `appcast-win.xml`** ([BT-29](../controls/release.md)). A missing or mismatched
  signature → the installer is rejected and never runs.
- **CUDA is separate (design §4.4), but not optional (step 3).** The `-core` installer
  is the only thing in the appcast, and the CUDA/GPU runtime is offered by the app
  itself based on the hardware it detects — never put the `-cuda` installer in the
  appcast. "Separate" does NOT mean "skippable": the offer links to `releases/latest`,
  so the newest release still has to carry a `-cuda.exe` or the offer dead-ends.
- **Every release carries all four builds** ([BT-29](../controls/release.md)): Windows, Linux and both
  macOS architectures share one release. WinSparkle only ever looks at `appcast-win.xml`
  and the Linux updater only looks at its own assets.
- **A dropped firewall rule is invisible on your own machines**: test it on a clean VM
  ([BT-34](../controls/release.md)).
- **Rollback:** re-draft or delete the release
  (`gh release edit <tag> --draft=true` / `gh release delete <tag>`) and the updater
  stops offering it immediately.
- **Rotating the key:** generate a new key, pin the new public key, and ship a release
  with it — but remember only users who install that release can verify updates signed
  by the new key. Keep signing with the *old* key until that release has propagated.
