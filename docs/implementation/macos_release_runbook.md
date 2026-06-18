# macOS Release Runbook

How to cut a macOS release that the in-app updater (Sparkle) will trust and offer to
users. Design: [`../design/macos_update.md`](../design/macos_update.md).

**macOS has TWO trust layers, both your responsibility, both kept OFF GitHub:**
1. **EdDSA signing key** — Sparkle's pinned update signature. GitHub only ever sees the
   **public** key (committed) and the per-DMG **signature** (inside `appcast-mac.xml`).
2. **Developer ID certificate + notarization** — Gatekeeper's trust. Without it the
   download is quarantined and App Translocation stops Sparkle updating in place
   (design §6), so on macOS this is a **v1 requirement**, not optional polish.

If the EdDSA private key leaks, anyone can ship a "trusted" update to every user; if you
lose it, you can't ship verifiable updates at all. Same for the Developer ID cert. Back
both up offline. Neither is ever a CI secret — CI builds only the **unsigned** DMG.

---

## Do this ONCE (first time only)

### A. Apple Developer ID + notarization credentials
1. Enrol in the **Apple Developer Program** ($99/yr) if not already.
2. Create + install a **"Developer ID Application"** certificate into your login
   keychain (Xcode → Settings → Accounts → Manage Certificates → +, or the Developer
   portal). Confirm:
   ```bash
   security find-identity -v -p codesigning | grep "Developer ID Application"
   ```
3. Store a **notarytool** credential profile (so the script can submit non-interactively):
   ```bash
   xcrun notarytool store-credentials pinpoint-notary \
     --apple-id "you@example.com" --team-id "<TEAMID>" --password "<app-specific-password>"
   ```
   (`<app-specific-password>` from appleid.apple.com → Sign-In and Security.) The profile
   name `pinpoint-notary` is what you pass as `NOTARY_PROFILE`.

### B. EdDSA signing key (Sparkle)
`generate_keys` / `sign_update` ship in the Sparkle distribution's `bin/` (once Stage 2
embeds Sparkle, CMake fetches it under `build/**/_deps/sparkle-*/bin/`; until then,
download a Sparkle 2 release and use its `bin/`). Then:
```bash
SPARKLE_BIN=/path/to/Sparkle/bin
"$SPARKLE_BIN/generate_keys"                 # creates the key in your login Keychain
"$SPARKLE_BIN/generate_keys" -x pinpoint_mac_eddsa.key   # EXPORT the private key — KEEP SECRET, BACK UP OFFLINE
"$SPARKLE_BIN/generate_keys" -p              # prints the PUBLIC key (base64)
```
Pin the public key: paste the base64 into
`src/Resources/keys/pinpoint_release_mac_eddsa.pub`, replacing `PLACEHOLDER`. Commit it.
CMake injects it into the bundle's `Info.plist` (`SUPublicEDKey`) at configure time.

> **Bootstrap:** until a build carrying the real key is in users' hands, the updater is
> inert by design (it refuses the placeholder). The **first** release that ships the
> pinned key bootstraps auto-update — users install that one normally; every release
> *after* it can auto-update. Plan the key rollout one release ahead.

---

## Do this FOR EACH RELEASE

### 1. Bump the version — `src/Core/version.h` only
- `PINPOINT_VERSION_MAJOR` / `MINOR` / `POSTFIX` — the human version (e.g. `-alpha3`, or
  `""` for a clean release) → `CFBundleShortVersionString` / `sparkle:shortVersionString`.
- **`PINPOINT_VERSION_BUILD`** — the monotonic integer Sparkle compares
  (`CFBundleVersion` / `sparkle:version`). **Must be strictly larger than the last
  release** or Sparkle won't offer the update. Formula is in the file.

CMake derives the bundle + DMG version from this; nothing else to bump. Commit + push.

### 2. Build the DMG (the update payload)
**Option A — CI builds it (recommended).** Push a tag; the `macos` job in
`.github/workflows/release.yml` (Intel `macos-13`) builds the **unsigned** DMG and
stages it on a **draft** release:
```bash
TAG=v0.1-alpha3
git tag "$TAG" && git push origin "$TAG"     # → triggers the build; wait for it
```
Then download the *exact* bytes CI built (you sign these — never a rebuild):
```bash
mkdir -p /tmp/rel
gh release download "$TAG" -R PinPoint-Golf/PinPointStudio -p '*-x86_64.dmg' -D /tmp/rel
DMG=$(ls /tmp/rel/PinPointStudio-*-x86_64.dmg)
```
> CI's DMG is **unsigned** — you sign + notarize it below. (Notarizing re-staples the
> DMG in place, which does not change the bytes Sparkle signs, because you sign AFTER
> notarizing — see step 3 order.)

**Option B — build locally** (signs + notarizes in one shot if your creds are set):
```bash
export SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"   # or let it auto-detect
export NOTARY_PROFILE=pinpoint-notary
tools/package_macos.sh            # builds Release, deploys, signs, notarizes, staples, makes the DMG
DMG=$(ls -t dist/PinPointStudio-*-x86_64.dmg | head -1)
```

### 3. Sign + notarize the DMG (if Option A, or to re-do it)
If you downloaded CI's unsigned DMG, sign + notarize it. The unit notarized is the DMG.
The simplest path re-runs the packaging signing helpers, but you can also do it directly:
```bash
export NOTARY_PROFILE=pinpoint-notary
ID="Developer ID Application: Your Name (TEAMID)"
# (the .app inside was unsigned in CI; for a fully-correct release prefer Option B, which
#  signs the .app BEFORE wrapping. If signing CI's DMG directly, sign + notarize it:)
codesign --force --timestamp --sign "$ID" "$DMG"
xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
xcrun stapler staple "$DMG" && xcrun stapler validate "$DMG"
spctl -a -t open --context context:primary-signature -vv "$DMG"   # expect: accepted, source=Notarized Developer ID
```
> For the cleanest result, **Option B** is preferred: it signs every nested dylib + the
> `.app` (Hardened Runtime) before building the DMG, then signs + notarizes the DMG.
> Signing only CI's outer DMG leaves the inner `.app` unsigned — acceptable for v1
> testing but fix before GA.

### 4. Generate the EdDSA-signed appcast (sign the EXACT bytes)
```bash
SPARKLE_BIN=/path/to/Sparkle/bin \
packaging/make_appcast_mac.sh --tag "$TAG" --dmg "$DMG" --key-file /path/to/pinpoint_mac_eddsa.key
```
Writes `appcast-mac.xml` next to the DMG (version, enclosure URL at the specific tag,
EdDSA signature, length). Optional: `--notes-url https://…/release-notes-mac.html`.
> Sign the **notarized** DMG (its bytes are what users download). Do step 4 after step 3.

### 5. Verify before publishing
```bash
spctl -a -t open --context context:primary-signature -vv "$DMG"   # Notarized Developer ID
xcrun stapler validate "$DMG"                                      # The validate action worked
"$SPARKLE_BIN/sign_update" --verify \
   -p "$(tr -d '\n' < src/Resources/keys/pinpoint_release_mac_eddsa.pub)" \
   "$DMG" <<<"$(sed -n 's/.*edSignature="\([^"]*\)".*/\1/p' "$(dirname "$DMG")/appcast-mac.xml")"
```
If signature or notarization does not verify, **stop** — do not publish. (Most common
cause: you signed a rebuild instead of the exact published DMG, or signed before
notarizing changed the file.)

### 6. Upload the assets + publish
```bash
APPCAST="$(dirname "$DMG")/appcast-mac.xml"
# Option A (CI draft already has the (old unsigned) DMG): replace it with the signed one + add the appcast:
gh release upload "$TAG" -R PinPoint-Golf/PinPointStudio "$DMG" "$APPCAST" --clobber
gh release edit   "$TAG" -R PinPoint-Golf/PinPointStudio --draft=false --prerelease=false

# Option B (local): create the release with BOTH assets:
gh release create "$TAG" -R PinPoint-Golf/PinPointStudio \
   --title "PinPoint Studio $TAG" --notes "…release notes…" "$DMG" "$APPCAST"
```
**Publish non-draft AND non-prerelease** — `releases/latest/download/appcast-mac.xml`
(the `SUFeedURL` baked into the app) only resolves to a non-prerelease release.

> `gh release create` (Option B) creates the `v*` tag, which fires `release.yml`. Its
> **`guard` job detects the already-published release and skips the build jobs**, so CI
> won't re-draft your release or clobber the signed DMG.

### 7. Confirm it's live
```bash
gh release view "$TAG" -R PinPoint-Golf/PinPointStudio --json assets \
  --jq '.assets[].name'      # expect: PinPointStudio-<ver>-x86_64.dmg AND appcast-mac.xml
```
On a machine running an **older installed** build: Settings → General → **Check now**
(or wait for the launch check) → Sparkle offers it, downloads the DMG, verifies the
EdDSA signature **and** the Developer ID Team-ID, and relaunches on the new version — no
Gatekeeper warning.

---

## Quick checklist (per release)

- [ ] Bump `PINPOINT_VERSION_BUILD` (+ MAJOR/MINOR/POSTFIX) in `version.h`, commit, push
- [ ] Build the DMG (CI tag push, or `tools/package_macos.sh`)
- [ ] If CI: `gh release download` the exact DMG
- [ ] Sign (Developer ID) + notarize + staple the DMG
- [ ] `make_appcast_mac.sh` → signs (EdDSA) + writes `appcast-mac.xml`
- [ ] Verify: `spctl` notarized, `stapler validate`, `sign_update --verify` — stop if any fail
- [ ] Upload DMG + `appcast-mac.xml`; publish **non-draft, non-prerelease**
- [ ] Confirm assets + test an update from an older build

---

## Gotchas & notes
- **Sign the bytes you publish.** Sign/notarize the DMG you will upload, then EdDSA-sign
  *that* exact file. A rebuild differs byte-for-byte and the signature won't verify, so
  every client rejects the update.
- **`BUILD` must always increase.** It's the only thing Sparkle compares. Forget to bump
  it and installed apps see "no newer version".
- **Never publish without `appcast-mac.xml`.** No appcast → no feed item. A missing or
  mismatched EdDSA signature, or a Team-ID mismatch, → the update is rejected.
- **No CUDA / no component split on macOS** (design §1, §4.3). The DMG is the whole,
  hardware-agnostic app — there is nothing to factor out (unlike the Windows `-core`/CUDA
  split). One artifact, one appcast item.
- **x86_64 only for v1.** Runs natively on Intel and under Rosetta 2 on Apple Silicon. A
  native `arm64` build is a GA add — a second DMG + `appcast-mac-arm64.xml`, selected by a
  build-time `SUFeedURL` (design §7).
- **Mixed-platform releases are fine.** The same GitHub release can also carry the Windows
  (`appcast-win.xml`, `*-core.exe`) and Linux (`*.AppImage*`) assets; Sparkle only ever
  looks at `appcast-mac.xml`.
- **Rollback:** re-draft or delete the release (`gh release edit <tag> --draft=true` /
  `gh release delete <tag>`) and the updater stops offering it immediately.
- **Rotating the key:** generate a new key, pin the new public key, ship a release with
  it — but only users who install that release can verify updates signed by the new key.
  Keep signing with the *old* key until that release has propagated.
