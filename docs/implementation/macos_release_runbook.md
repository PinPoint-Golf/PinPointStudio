# macOS Release Runbook

The complete, do-this-from-zero guide to cutting a macOS release of PinPoint Studio:
version bump → build → **code sign** → **notarize** → publish, and (from Stage 2) the
Sparkle EdDSA appcast that drives in-app updates. Design:
[`../design/macos_update.md`](../design/macos_update.md). Siblings:
[`windows_release_runbook.md`](windows_release_runbook.md),
[`linux_release_runbook.md`](linux_release_runbook.md).

> **If this is your first time signing a Mac app, start at Part 0 and do every step in
> order.** Part 0 is once-ever setup; Part 1 is what you repeat for each release. The
> first time, budget ~30–45 min for Part 0 (mostly Apple web UI + one Xcode dialog).

---

## How macOS trust works (read this once)

A Mac app downloaded from the internet must clear **Gatekeeper**, or users get scary
"PinPoint Studio is damaged / can't be opened" warnings — and worse, **App
Translocation** runs the app from a random read-only path where Sparkle can't update it.
Clearing Gatekeeper needs **two** things, both done by you, both kept offline:

1. **Developer ID code signature** — you sign the app + DMG with a *Developer ID
   Application* certificate issued by Apple to your account. Proves "this came from
   Mark Liversedge, team `<TEAMID>`, and hasn't been tampered with."
2. **Notarization** — you upload the signed DMG to Apple; their service scans it for
   malware and issues a **ticket**; you **staple** the ticket into the DMG so it's
   trusted even offline.

A **third** layer applies to **auto-update** (Stage 2, now shipped):

3. **EdDSA (Ed25519) signature** — Sparkle verifies each update against a public key
   *pinned inside the app*. This is independent of Apple; it stops anyone (even with a
   stolen Apple cert) from pushing an update unless they also have your EdDSA private key.

> **Where we are now:** both stages have shipped. The in-app updater (Sparkle) is embedded,
> the offline EdDSA key is generated and its public half is committed + pinned, and
> `v0.1-alpha3` is published with the Sparkle-capable signed DMG **and** `appcast-mac.xml`.
> The **[Stage 2]** EdDSA / appcast steps below are therefore now a **required part of every
> release**, not optional. (The one-time key generation in 0.6 is already done — it's kept
> below for reference / disaster recovery.)

---

## Part 0 — One-time setup (do this once, ever)

### 0.1 Apple Developer Program ✅
You've enrolled ($99/yr individual). That gives you a **Team ID** and the right to issue
a Developer ID certificate. (Individual account = you are the "Account Holder", which is
required to create Developer ID certs.)

### 0.2 Create your "Developer ID Application" certificate

This certificate + its private key live in your **login keychain**. The private key is
generated *on your Mac* and never leaves it — Apple only signs the public half.

**Method A — Xcode (recommended; you have Xcode installed):**
1. Open **Xcode** → menu **Xcode ▸ Settings…** (⌘,) → **Accounts** tab.
2. Click **+** (bottom-left) → **Apple ID** → sign in with your developer Apple ID.
3. Select your team in the list → click **Manage Certificates…**
4. Click the **+** (bottom-left of the sheet) → choose **Developer ID Application**.
5. It appears in the list with today's date. Done — close the dialog. Xcode has created
   the cert **and** installed it + its private key into your login keychain.

> If **Developer ID Application** is greyed out / missing from the **+** menu: your Apple
> ID isn't recognised as the Account Holder, or the membership is still activating. Wait a
> few minutes after enrolment, or use Method B.

**Method B — Developer portal + manual CSR (fallback):**
1. **Keychain Access** → menu **Keychain Access ▸ Certificate Assistant ▸ Request a
   Certificate From a Certificate Authority…**
   - User Email: your Apple ID email · Common Name: "Mark Liversedge" ·
     **Saved to disk** (not "emailed") · check **Let me specify key pair information** →
     **2048 bits, RSA**. Save the `.certSigningRequest` file.
2. Go to **developer.apple.com/account** → **Certificates, IDs & Profiles** →
   **Certificates** → **+** → **Developer ID Application** → Continue → upload the
   `.certSigningRequest` → Continue → **Download** the `.cer`.
3. **Double-click the downloaded `.cer`** to install it into your login keychain (it pairs
   with the private key Keychain Access made in step 1).
4. **Install Apple's intermediate CA** (the manual path skips what Xcode does
   automatically — without it the cert shows as *not trusted* / "0 valid identities").
   Find your cert's issuer, then fetch the matching intermediate from
   <https://www.apple.com/certificateauthority/>:
   ```bash
   # see the issuer (e.g. "Developer ID Certification Authority, OU=G2"):
   security find-certificate -c "Developer ID Application" -p | openssl x509 -noout -issuer
   # G2 issuer → fetch + install the G2 intermediate:
   curl -fsSL -o /tmp/DeveloperIDG2CA.cer https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer
   security import /tmp/DeveloperIDG2CA.cer -k ~/Library/Keychains/login.keychain-db
   ```

**Verify either way** (this is the check the build script does too):
```bash
security find-identity -v -p codesigning | grep "Developer ID Application"
```
You should see one line like:
`1) ABCD…1234 "Developer ID Application: Mark Liversedge (TEAMID9999)"`

### 0.3 BACK UP the certificate + private key — do NOT skip

A Developer ID certificate **cannot be re-downloaded with its private key**. If you lose
this Mac or the keychain, you lose the ability to sign — and Sparkle/macOS treat updates
signed by a *different* cert as a different app. Back it up now:
1. **Keychain Access** → **login** keychain → **My Certificates**.
2. Right-click **"Developer ID Application: … (TEAMID)"** → **Export…** → save as
   `pinpoint_developer_id.p12`, set a strong password.
3. Store the `.p12` + its password somewhere offline (password manager / encrypted drive).
   **Never commit it. It is not a CI secret.**

### 0.4 Note your Team ID
It's the `(TEAMID9999)` in the identity name above, and on
**developer.apple.com/account** → **Membership**. You'll use it for notarization. Below,
`<TEAMID>` means this value and `<APPLE_ID>` means your developer Apple ID email.

### 0.5 App-specific password + notarytool credentials
Notarization logs in to Apple as you. Don't use your real password — make an
**app-specific password**:
1. Go to **appleid.apple.com** → **Sign-In and Security** → **App-Specific Passwords** →
   **+** → label it `pinpoint-notary` → copy the generated `xxxx-xxxx-xxxx-xxxx`.
2. Store it in your keychain as a reusable **notarytool profile** (so you never type it
   again, and it stays out of scripts):
   ```bash
   xcrun notarytool store-credentials pinpoint-notary \
     --apple-id "<APPLE_ID>" --team-id "<TEAMID>" --password "xxxx-xxxx-xxxx-xxxx"
   ```
   Now any `notarytool` call can use `--keychain-profile pinpoint-notary`.

> Run this one yourself (it contains the password). In this session you can prefix it with
> `! ` so it runs in your terminal and the secret never enters the chat.

### 0.6 [Stage 2] EdDSA signing key + pin the public key — ✅ DONE (kept for reference)
The release key was generated on 2026-06-18; the public half is committed to
`src/Resources/keys/pinpoint_release_mac_eddsa.pub` and CMake bakes it into the bundle's
`Info.plist` (`SUPublicEDKey`). The private half lives in this Mac's **login Keychain**
(default Sparkle account `ed25519`) — `sign_update` reads it automatically — with an offline
backup at `~/pinpoint_release_mac_eddsa_PRIVATE.pem` (**move this to encrypted offline
storage; it is the root of trust and is NOT a CI secret**). `generate_keys` / `sign_update`
ship in `build/macos-release/_deps/sparkle-src/bin/` once CMake has fetched Sparkle.

> You only ever do this once. To re-derive the steps (e.g. on a new machine, restoring from
> the `.pem` backup): `generate_keys` creates/keeps the Keychain key, `generate_keys -x
> <file>` exports the private half, `generate_keys -p` prints the public base64, and
> `generate_keys -f <file>` imports a private key from the backup. **Bootstrap:** the first
> release shipping both the updater and the real key (= `v0.1-alpha3`) lets all *later*
> releases auto-update; users install that baseline manually.

---

## Part 1 — Cutting a release (repeat every time)

### 1.1 Bump the version — `src/Core/version.h` only
Edit and commit + push:
- `PINPOINT_VERSION_MAJOR` / `MINOR` / `POSTFIX` — the human version (e.g. `-alpha4`, or
  `""` for a clean release). Becomes `CFBundleShortVersionString` / the DMG filename.
- **`PINPOINT_VERSION_BUILD`** — the monotonic integer (becomes `CFBundleVersion`, the key
  Sparkle compares). **Must strictly increase every release.** Formula is in the file.

CMake derives the bundle version + DMG name from these — nothing else to edit.

### 1.2 Run the full test suite — ALL must pass (MANDATORY GATE)
**A release MUST NOT be cut while any test is failing or not building.** PinPoint has
seven standalone CTest suites (they are *not* part of the app build — see
[`../../BUILDING.md`](../../BUILDING.md) § Testing). Build and run every one; each must
report `100% tests passed` (OpenCV comes from Homebrew, so no `OpenCV_DIR` needed):
```bash
QT=~/Qt/6.11.0/macos
for s in Buffer=src/Buffer Analysis=src/Analysis/tests Audio=src/Audio/tests \
         Core=src/Core/tests Gui=src/Gui/tests IMU=src/IMU/tests Pose=src/Pose/tests; do
  n=${s%%=*}; d=${s#*=}
  cmake -S "$d" -B "build/tests-$n" -DCMAKE_PREFIX_PATH="$QT" \
    && cmake --build "build/tests-$n" -j \
    && ctest --test-dir "build/tests-$n" --output-on-failure \
    || { echo "❌ RELEASE BLOCKED — $n failed"; break; }
done
```
**If any suite fails to build or any test fails, STOP — fix it and re-run before you
tag.** Do not proceed to the build/sign steps below.

### 1.3 Build the DMG
Two paths — pick one.

**Path A — CI builds it (recommended once the workflow is validated).** Push a tag; the
`macos` job (Intel `macos-13`) builds the **unsigned** DMG onto a **draft** release:
```bash
TAG=v0.1-alpha4
git tag "$TAG" && git push origin "$TAG"      # wait for the Actions run to finish
gh release download "$TAG" -R PinPoint-Golf/PinPointStudio -p '*-x86_64.dmg' -D /tmp/rel
```
Then sign + notarize that exact downloaded DMG (it's unsigned from CI).

**Path B — build locally (best for your first time — full control, signs in one shot).**
With the cert + notary profile from Part 0 in place, the packaging script does the whole
build → deploy → relocate → **sign → notarize → staple** → DMG automatically:
```bash
brew install opencv ffmpeg aravis     # build deps (once)
export SIGN_IDENTITY="Developer ID Application: Mark Liversedge (<TEAMID>)"   # or omit → auto-detect
export NOTARY_PROFILE=pinpoint-notary
tools/package_macos.sh                # ~build is slow the first time
DMG=$(ls -t dist/PinPointStudio-*-x86_64.dmg | head -1)
```
If `SIGN_IDENTITY`/`NOTARY_PROFILE` are unset, the script still builds a valid **unsigned**
DMG and tells you it skipped signing.

### 1.4 Smoke-launch the bundle before signing — STOP if it crashes (MANDATORY GATE)
The test suite (1.2) and the relocatability check (1.3's §5 gate) are both **static** — they
never actually load the bundled dylib closure. A **load-time** crash slips past both: e.g. a
stray build-machine `LC_RPATH` left in the binary makes dyld load a *second* copy of OpenMP
(Homebrew's, ahead of the bundled one), and `libomp` aborts in `__kmp_register_library_startup`
("OMP: Error #15") **before `main()` runs**. The app dies instantly on launch. Notarization is
the slow, irreversible step — never spend it on a bundle you haven't watched open.

So launch the freshly-built bundle once, **unsigned**, and confirm the window actually appears:
```bash
tools/package_macos.sh --no-sign            # build + deploy + relocate, no sign/notarize
open build/macos-pkg/PinPointStudio.app     # the main window MUST appear — quit it after a few seconds
```
If it crashes on launch, open the crash report and read the **faulting library** before
re-running:
- `__kmp_register_library_startup` / `__kmp_fatal` / "OMP Error #15" → a duplicate OpenMP
  runtime: a build-machine rpath (or absolute dep) leaked into the bundle. `package_macos.sh`
  now strips these (`strip_build_rpaths`) and its §5 gate fails the build on any stray
  `/usr/local`, `/opt/homebrew`, or build-tree rpath — so a clean re-run should fix it. If the
  gate *didn't* catch it, widen the prefixes it checks.
- "image not found" / dyld → an unrelocated dependency; same §5 gate territory.

Only once it launches cleanly, re-run with your signing env vars (1.5, Path B) to produce the
signed DMG. (Path A / CI DMG: `hdiutil attach` it and launch the `.app` from the mounted
volume the same way before you sign the downloaded DMG.)

### 1.5 What signing + notarizing actually does (so you understand the script)
`tools/package_macos.sh` runs these for you; here's the manual equivalent, useful for
learning and for re-signing a CI (Path A) DMG:
```bash
ID="Developer ID Application: Mark Liversedge (<TEAMID>)"
# (a) sign every nested dylib/framework, then the .app last, with Hardened Runtime:
#     the script signs inside-out using packaging/macos/entitlements.plist.
# (b) sign the DMG itself:
codesign --force --timestamp --sign "$ID" "$DMG"
# (c) notarize — uploads, waits for Apple's scan (~1–5 min), returns Accepted/Invalid:
xcrun notarytool submit "$DMG" --keychain-profile pinpoint-notary --wait
# (d) staple the ticket into the DMG so it's trusted offline:
xcrun stapler staple "$DMG"
```
If notarization says **Invalid**, get the reason: `xcrun notarytool log <submission-id>
--keychain-profile pinpoint-notary` (see Troubleshooting).

### 1.6 Verify — STOP if anything here fails
```bash
spctl -a -t open --context context:primary-signature -vv "$DMG"   # → accepted, source=Notarized Developer ID
xcrun stapler validate "$DMG"                                      # → The validate action worked
codesign --verify --deep --strict --verbose=2 "$DMG"              # → valid on disk
```
All three must pass before you publish. The most common first-time failure is a missing
or wrong entitlement / an unsigned nested binary — see Troubleshooting.

### 1.7 [Stage 2] EdDSA-sign the DMG + generate the appcast — REQUIRED every release
```bash
# Signs with the private key in your login Keychain (no --key-file needed); auto-finds
# sign_update under build/**/_deps/sparkle-src/bin/. Writes appcast-mac.xml next to the DMG.
packaging/make_appcast_mac.sh --tag "$TAG" --dmg "$DMG" \
  --notes-url "https://github.com/PinPoint-Golf/PinPointStudio/releases/tag/$TAG"

# Verify the signature against the published bytes before uploading (must exit 0):
SU=$(find build -name sign_update -type f | head -1)
SIG=$(sed -n 's/.*edSignature="\([^"]*\)".*/\1/p' "$(dirname "$DMG")/appcast-mac.xml")
"$SU" --verify "$DMG" "$SIG"; echo "verify exit: $?"
```
Sign the **notarized** DMG (its bytes are what users download). If signing from the
offline `.pem` instead of the Keychain, add `--key-file ~/pinpoint_release_mac_eddsa_PRIVATE.pem`.

### 1.8 Tag (if not already) + publish to GitHub
```bash
# Path B (local): create the release with the assets. For Stage 1, just the DMG:
gh release create "$TAG" -R PinPoint-Golf/PinPointStudio \
   --title "PinPoint Studio $TAG" --notes "…release notes…" "$DMG"
#   [Stage 2] also attach the appcast:  … "$DMG" "$(dirname "$DMG")/appcast-mac.xml"

# Path A (CI draft already holds an unsigned DMG): replace it with your signed one + publish:
gh release upload "$TAG" -R PinPoint-Golf/PinPointStudio "$DMG" --clobber
gh release edit   "$TAG" -R PinPoint-Golf/PinPointStudio --draft=false --prerelease=false
```
**Publish non-draft AND non-prerelease** — the `latest/download/…` URLs (Stage 2's
`SUFeedURL`) only resolve to a non-prerelease release. The `gh release create` tag fires
`release.yml`, whose `guard` job sees the published release and **skips** CI so it can't
clobber your signed DMG.

### 1.9 Confirm
```bash
gh release view "$TAG" -R PinPoint-Golf/PinPointStudio --json assets --jq '.assets[].name'
```
Download the DMG **in a browser** (so it gets the `com.apple.quarantine` flag a real user
sees), open it, drag to /Applications, launch — it must open with **no** Gatekeeper
warning. (Stage 2: an older installed build offers the update via Settings → Check now.)

---

## Quick checklist (per release)
- [ ] Bump `PINPOINT_VERSION_BUILD` (+ MAJOR/MINOR/POSTFIX) in `version.h`; commit, push
- [ ] **Run all 7 CTest suites — every one `100% tests passed` (mandatory; stop if any fail)**
- [ ] Build the DMG (Path A CI tag push, or Path B `tools/package_macos.sh`)
- [ ] **Smoke-launch the unsigned bundle (`--no-sign` → `open …`) — main window appears, no crash**
- [ ] Sign (Developer ID) + notarize + staple — automatic in Path B with the env vars set
- [ ] Verify: `spctl` → Notarized Developer ID, `stapler validate`, `codesign --verify`
- [ ] [Stage 2] `make_appcast_mac.sh` → EdDSA signature + `appcast-mac.xml`; `--verify`
- [ ] Publish DMG (+ appcast in Stage 2) **non-draft, non-prerelease**
- [ ] Confirm: browser-download the DMG, open with no Gatekeeper warning

---

## Troubleshooting (first-timer errors)
- **`security find-identity` shows nothing / "Developer ID Application" greyed out in
  Xcode** → membership still activating, or you're not the Account Holder. Wait, re-open
  Xcode Accounts, or use Method B (portal + CSR).
- **`security find-identity -v` says "0 valid identities" but `-v` omitted shows the cert
  with `CSSMERR_TP_NOT_TRUSTED`** → the Apple **intermediate CA is missing** (typical after
  the manual Method B path). Install it per Method B step 4 above, then re-check — it
  should flip to "1 valid identities found".
- **`errSecInternalComponent` during codesign** → the private key isn't accessible: open
  Keychain Access, confirm the cert in **login ▸ My Certificates** has a disclosure
  triangle revealing a private key. If not, the cert and key got separated (re-do 0.2).
- **notarytool returns `Invalid`** → run `xcrun notarytool log <id> --keychain-profile
  pinpoint-notary`. Usual causes: a nested binary wasn't signed, **Hardened Runtime not
  enabled** (`--options runtime` — the script does this), or `get-task-allow` left on (a
  Debug entitlement). Fix, re-sign, re-submit.
- **`spctl` says "rejected" / "Unnotarized Developer ID"** → the ticket isn't stapled
  (`xcrun stapler staple "$DMG"`), or notarization actually failed (check the log).
- **App opens but immediately crashes after notarization** → a Hardened-Runtime
  restriction. Check Console.app at launch time; you may need to add an entitlement in
  `packaging/macos/entitlements.plist` (e.g. `allow-unsigned-executable-memory`/`allow-jit`)
  and re-sign + re-notarize. Add the *minimum* needed.
- **"damaged and can't be opened" on the test machine** → you skipped notarization or
  stapling, or signed with the wrong identity. Re-verify §1.6.

## Gotchas & key custody
- **Sign the exact bytes you publish.** Sign + notarize the DMG you upload, then (Stage 2)
  EdDSA-sign *that* file. A rebuild differs byte-for-byte and breaks the signature.
- **`PINPOINT_VERSION_BUILD` must always increase** — it's the only thing Sparkle compares.
- **No CUDA / no component split on macOS** — the DMG is the whole, hardware-agnostic app
  (CoreML/Accelerate/Metal). The ViTPose model is bundled, same as the Windows `-core`
  component.
- **x86_64 only for v1** — runs natively on Intel, under Rosetta 2 on Apple Silicon. A
  native `arm64` build (second DMG + feed) is a GA add.
- **Custody:** the Developer ID `.p12` (0.3), the app-specific password (0.5), and the
  [Stage 2] EdDSA private key all stay **offline, never CI secrets**. Losing the cert key
  or the EdDSA key is the one thing that breaks future updates — back them up.
- **Rollback:** `gh release edit <tag> --draft=true` (or `--prerelease=true`) immediately
  stops the updater offering it.
