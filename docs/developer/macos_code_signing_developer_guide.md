# PinPoint Studio — macOS Code Signing Developer Guide

**Audience**: Developers building, running, or releasing PinPoint Studio on macOS
**Location**: `cmake/DevCodesign.cmake` (debug), `tools/package_macos.sh` + `packaging/macos/entitlements.plist` (release), `CMakeLists.txt` (wiring)
**Scope**: Why both debug *and* release builds must be signed, the two distinct signing identities, how each is wired, and the gotchas
**Status**: Debug signing wired into every macOS build; release signing driven by the packaging script

> This guide deliberately contains **no certificate names, Team IDs, or account
> details**. Wherever a real identity is needed it is shown as a placeholder
> (`<DEV_ID_APPLICATION>`, `<TEAM_ID>`, `<NOTARY_PROFILE>`). The actual values and
> first-time account setup live in `docs/implementation/macos_release_runbook.md`.

---

## Contents

1. [Why macOS builds must be signed](#1-why-macos-builds-must-be-signed)
2. [Two builds, two identities](#2-two-builds-two-identities)
3. [Debug signing — what it's for and how it's wired](#3-debug-signing--what-its-for-and-how-its-wired)
4. [Creating the debug signing certificate](#4-creating-the-debug-signing-certificate)
5. [Release signing — the distribution path](#5-release-signing--the-distribution-path)
6. [The ordering rule: sign last, never touch the bundle after](#6-the-ordering-rule-sign-last-never-touch-the-bundle-after)
7. [Verifying a signature](#7-verifying-a-signature)
8. [Gotchas](#8-gotchas)
9. [Quick reference](#9-quick-reference)
10. [File map](#10-file-map)

---

## 1. Why macOS builds must be signed

On macOS a code signature is not optional polish — it is the **identity** the
operating system uses to recognise an app across launches. Two OS subsystems
depend on it, and they bite at *different* stages:

- **TCC (Transparency, Consent & Control)** — the privacy permission system
  behind camera, microphone, speech recognition and Bluetooth. When you grant a
  permission, macOS stores it keyed to the app's **code-signing identity**, not
  merely its path or bundle id. This affects **debug builds**.
- **Gatekeeper** — the launch-time check that decides whether an app downloaded
  from outside the App Store is allowed to run on *someone else's* Mac. This
  affects **release builds**.

The failure modes are different but the root cause is the same — a missing or
unstable identity:

| Build | Symptom if unsigned / unstable identity | Subsystem |
|-------|------------------------------------------|-----------|
| Debug | Camera/mic/speech/Bluetooth return *Denied* at runtime even though System Settings shows the app as enabled. Every rebuild "forgets" granted permissions. | TCC |
| Release | Other users get *"PinPoint Studio is damaged / from an unidentified developer and can't be opened."* | Gatekeeper |

### Why an unsigned debug build "forgets" permissions

TCC matches a stored grant against the running process's code identity:

- **Properly signed** (stable cert) → grant is keyed to the certificate's
  *designated requirement*, which is constant across rebuilds. Grant sticks. ✅
- **Ad-hoc signed** (`codesign -s -`) → no certificate, so the requirement falls
  back to the **cdhash** (a hash of the actual Mach-O). Every rebuild changes the
  binary → new cdhash → the grant no longer matches → *Denied*. ❌
- **Unsigned** → no durable identity at all. The System Settings row still shows
  "enabled" (that pane is keyed loosely by bundle id), but runtime enforcement
  has nothing to match, so every entitled API returns *Denied*. ❌

Because all four subsystems gate through the same identity check, an unsigned
build fails *all of them at once* — the tell that it's an identity problem, not
four separate permission bugs.

---

## 2. Two builds, two identities

PinPoint uses **two completely separate signing identities**, on purpose:

| | Debug / dev loop | Release / distribution |
|---|---|---|
| **Identity** | Self-signed local cert (default name `PinPoint Dev`) | `Developer ID Application` cert (`<DEV_ID_APPLICATION>`) |
| **Trusted by other Macs?** | No (irrelevant — never leaves your machine) | Yes (issued by Apple) |
| **Notarized?** | No | Yes |
| **Hardened runtime?** | No | Yes |
| **Purpose** | Stable identity so TCC grants persist across rebuilds | Gatekeeper trust so users can launch it |
| **Where wired** | `cmake/DevCodesign.cmake`, run every build | `tools/package_macos.sh`, run at release time |
| **Cost if leaked** | None — disposable | High — it's your distribution key |

**Why not reuse the release cert for debug builds?** The only thing TCC needs is
a *stable* identity — it does not care whether the cert is Apple-trusted. Using
the real `Developer ID` key on every automatic build would expose a sensitive
secret in the build's hot path, drag in the hardened-runtime apparatus the dev
tree doesn't want (see §8), couple the inner loop to release credentials, and buy
nothing in return. Keep the valuable key for distribution; sign dev builds with a
free, disposable, stable local cert.

---

## 3. Debug signing — what it's for and how it's wired

Every macOS build signs the finished `.app` with the local cert automatically.
The wiring lives in `CMakeLists.txt` (inside `if(APPLE)`) and `cmake/DevCodesign.cmake`.

```cmake
set(PINPOINT_DEV_CODESIGN_IDENTITY "PinPoint Dev" CACHE STRING
    "Keychain identity used to sign the dev bundle so TCC permission grants persist across rebuilds")
add_custom_target(PinPointStudio_codesign ALL
    COMMAND ${CMAKE_COMMAND}
        "-DBUNDLE=$<TARGET_BUNDLE_DIR:PinPointStudio>"
        "-DIDENTITY=${PINPOINT_DEV_CODESIGN_IDENTITY}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DevCodesign.cmake")
add_dependencies(PinPointStudio_codesign PinPointStudio_force_plist)
```

Key properties:

- **Runs on every build.** It's a custom target with no output, so it's always
  considered out of date — necessary because the per-build `Info.plist`
  force-copy would otherwise invalidate a prior signature (see §6).
- **Runs last.** It depends on `PinPointStudio_force_plist`, which is the final
  step that mutates the bundle. Signing must come after every bundle write.
- **Non-fatal when the cert is missing.** If `PINPOINT_DEV_CODESIGN_IDENTITY`
  isn't in the keychain, `DevCodesign.cmake` prints setup instructions and the
  build still succeeds (so Linux/CI and un-provisioned Macs aren't broken) — the
  app simply won't retain permissions until the cert exists.
- **Identity-agnostic.** Override the cert with
  `-DPINPOINT_DEV_CODESIGN_IDENTITY=<name>` at configure time.

After the first signed build, **macOS re-prompts for each permission once** (the
identity changed from "unsigned" to the dev cert). Grant them once; they persist
across all future rebuilds. To clear stale grants from a previous unsigned
identity:

```sh
tccutil reset All com.pinpoint-golf.pinpointstudio
```

---

## 4. Creating the debug signing certificate

A one-time setup. The cert is a **self-signed Code Signing certificate** in your
login keychain; no Apple Developer account is required.

### Option A — Keychain Access (GUI)

1. Open **Keychain Access** (⌘-Space → "Keychain Access").
2. Menu: **Keychain Access ▸ Certificate Assistant ▸ Create a Certificate…**
3. Fill in:
   - **Name**: `PinPoint Dev` (must match `PINPOINT_DEV_CODESIGN_IDENTITY`)
   - **Identity Type**: `Self Signed Root`
   - **Certificate Type**: `Code Signing`
4. Click **Create**, accept the self-signed warning, **Done**.
   - *Optional*: tick "Let me override defaults" to extend the **Validity period**
     from the 365-day default to e.g. 3650 days, so it doesn't expire yearly.

### Option B — Command line

The macOS keychain importer is fussy about modern OpenSSL output. Both flags
below matter (see §8):

```sh
cd "$(mktemp -d)"
cat > cert.conf <<'EOF'
[ req ]
distinguished_name = dn
x509_extensions = v3
prompt = no
[ dn ]
CN = PinPoint Dev
[ v3 ]
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
basicConstraints = critical, CA:false
EOF

openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
  -keyout dev.key -out dev.crt -config cert.conf

# -legacy + a NON-empty password are both required for `security import` to
# accept the bundle (see Gotchas).
openssl pkcs12 -export -legacy -inkey dev.key -in dev.crt \
  -out dev.p12 -passout pass:tmp-pass

# -T /usr/bin/codesign authorises codesign to use the key without prompting.
security import dev.p12 -P tmp-pass -T /usr/bin/codesign

rm -f dev.key dev.crt dev.p12 cert.conf   # scrub the private-key material
```

### Confirm it's installed

A self-signed cert is an *untrusted* root, so it is hidden by the `-v` ("valid
only") filter. List **without** `-v`:

```sh
security find-identity -p codesigning | grep "PinPoint Dev"
# → ... "PinPoint Dev" (CSSMERR_TP_NOT_TRUSTED)
```

`CSSMERR_TP_NOT_TRUSTED` is expected and harmless — `codesign` signs with the
cert regardless of trust, and TCC keys the grant to the signature either way.
`DevCodesign.cmake` deliberately queries without `-v` for this reason.

On the first build that uses the key you may get a *"codesign wants to use key
'PinPoint Dev'"* dialog — click **Always Allow** once.

---

## 5. Release signing — the distribution path

Release signing is for **other people's Macs**. It is driven end-to-end by
`tools/package_macos.sh` and requires two things from your keychain (set up once,
per the release runbook):

- a `Developer ID Application` certificate (`<DEV_ID_APPLICATION>`), and
- a stored **notarytool profile** (`<NOTARY_PROFILE>`) holding an app-specific
  password.

```sh
export SIGN_IDENTITY="Developer ID Application: <NAME> (<TEAM_ID>)"   # or omit → auto-detect
export NOTARY_PROFILE=<NOTARY_PROFILE>
tools/package_macos.sh
```

If those env vars are unset the script still produces a valid **unsigned** DMG
(useful for local packaging tests). With them set it runs the full chain
automatically:

1. **Sign inside-out, with Hardened Runtime.** Every nested dylib/framework is
   signed first, then the `.app` last, using `packaging/macos/entitlements.plist`:
   ```sh
   codesign --force --options runtime --timestamp \
            --entitlements packaging/macos/entitlements.plist \
            --sign "$SIGN_IDENTITY" <each nested binary, then the .app>
   ```
2. **Sign the DMG.**
   ```sh
   codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG"
   ```
3. **Notarize** — upload to Apple, which scans for malware and issues a ticket:
   ```sh
   xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
   ```
4. **Staple** the ticket into the DMG so it's trusted offline:
   ```sh
   xcrun stapler staple "$DMG"
   ```

### The entitlements file

`packaging/macos/entitlements.plist` currently carries a single key:

```xml
<key>com.apple.security.cs.disable-library-validation</key>
<true/>
```

This is **required**: Hardened Runtime turns on library validation, which would
reject the bundled Qt frameworks because they are signed by Qt/Apple rather than
by `<TEAM_ID>`. Disabling library validation lets the signed app load them. (This
is one more reason the debug path skips Hardened Runtime entirely — see §8.)

---

## 6. The ordering rule: sign last, never touch the bundle after

**Any write into a `.app` after it is signed invalidates the signature.** macOS
then treats the app as tampered — Gatekeeper rejects the release build, and TCC
rejects the debug build's grants, exactly as if it were unsigned.

This is why both paths sign **last**:

- **Debug**: `PinPointStudio_codesign` depends on `PinPointStudio_force_plist`,
  which force-copies `Info.plist` in as the final bundle mutation. Sign after the
  plist, never before. If you add a new POST_BUILD step that writes into the
  bundle, it **must** run before the codesign step or it will break the seal.
- **Release**: `package_macos.sh` deploys Qt, relocates libraries, and copies the
  plist *before* the signing stage. Resources copied in after signing are the
  classic "notarization succeeded but the app is damaged" cause.

---

## 7. Verifying a signature

```sh
APP=path/to/PinPointStudio.app

# Who signed it, and the sealed identifier:
codesign -dv --verbose=4 "$APP"
#   Debug   → Authority=PinPoint Dev,  Signature=...,  TeamIdentifier=not set
#   Release → Authority=Developer ID Application: …,   TeamIdentifier=<TEAM_ID>

# Structural integrity of the seal (both builds should pass):
codesign --verify --deep --strict --verbose=2 "$APP"

# Release only — Gatekeeper acceptance (needs notarize + staple):
spctl -a -t open --context context:primary-signature -vv "$DMG"   # → accepted, source=Notarized Developer ID
xcrun stapler validate "$DMG"
```

A debug build will **not** pass `spctl` / Gatekeeper (untrusted, un-notarized) —
that's expected and fine; it only needs to satisfy TCC on your own machine.

---

## 8. Gotchas

- **Modifying the bundle after signing invalidates the seal.** The single most
  common cause of both "permissions denied despite signed" and "notarized but
  damaged". Sign last. (§6)
- **`security find-identity -v` hides self-signed certs.** `-v` means "valid
  (trusted) only"; a self-signed root is untrusted, so it won't appear. Query
  without `-v` to find the dev cert. (§4)
- **OpenSSL 3 PKCS12 won't import into the keychain by default.** `security
  import` fails with *"MAC verification failed (wrong password?)"*. Two fixes are
  needed together: pass `-legacy` to `openssl pkcs12 -export` (3DES/RC2 + SHA1
  MAC, which Apple's importer understands), **and** use a non-empty password —
  the importer also chokes on a passwordless PKCS12 MAC. (§4)
- **Hardened Runtime + library validation rejects the bundled Qt.** Qt frameworks
  are signed by Qt/Apple, not your Team ID, so a hardened-runtime app refuses to
  load them unless `com.apple.security.cs.disable-library-validation` is set.
  This is why release uses an entitlements file and debug skips hardened runtime
  altogether. (§5)
- **TCC keys grants to the signing identity.** Changing the identity
  (unsigned → signed, or swapping certs) makes macOS re-prompt. After first
  enabling signing, expect one round of prompts, then persistence. Clear stale
  grants with `tccutil reset All com.pinpoint-golf.pinpointstudio`. (§3)
- **Debug and release are distinct TCC identities.** Granting permission to the
  signed release app does not grant it to your dev build, and vice versa — they
  have different designated requirements.
- **Bundle id must stay constant** (`com.pinpoint-golf.pinpointstudio`). It is the
  coarse key for the System Settings row and part of the designated requirement;
  changing it orphans every existing grant.

---

## 9. Quick reference

| Task | Command |
|------|---------|
| Create dev cert (CLI) | see §4 Option B |
| List code-signing identities (incl. untrusted) | `security find-identity -p codesigning` |
| Override dev cert name | configure with `-DPINPOINT_DEV_CODESIGN_IDENTITY=<name>` |
| Re-sign dev bundle manually | `cmake -DBUNDLE=<app> -DIDENTITY="PinPoint Dev" -P cmake/DevCodesign.cmake` |
| Clear stale permission grants | `tccutil reset All com.pinpoint-golf.pinpointstudio` |
| Inspect a signature | `codesign -dv --verbose=4 <app>` |
| Verify seal integrity | `codesign --verify --deep --strict <app>` |
| Build signed release DMG | `SIGN_IDENTITY=… NOTARY_PROFILE=… tools/package_macos.sh` |
| Gatekeeper check (release) | `spctl -a -t open --context context:primary-signature -vv <dmg>` |

---

## 10. File map

| Path | Role |
|------|------|
| `cmake/DevCodesign.cmake` | Signs the dev `.app` with the local cert; non-fatal if absent |
| `CMakeLists.txt` (`if(APPLE)`) | `PinPointStudio_codesign` target + `PINPOINT_DEV_CODESIGN_IDENTITY`, ordered after `PinPointStudio_force_plist` |
| `tools/package_macos.sh` | Release build → deploy → sign → notarize → staple → DMG |
| `packaging/macos/entitlements.plist` | Hardened-runtime entitlements (library-validation opt-out) |
| `Info.plist.in` | Bundle id + `NS*UsageDescription` strings TCC shows in its prompts |
| `docs/implementation/macos_release_runbook.md` | First-time account/cert/notary setup and the real identity values |

---

*Related: `docs/implementation/macos_release_runbook.md` (release account setup),
`docs/design/macos_update.md` (Sparkle auto-update), and the "Critical gotchas"
section of `CLAUDE.md`.*
