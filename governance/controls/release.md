# Controls: 7. Release

Building, publishing and installing a release. The steps are the platform procedures: [`release-macos.md`](../procedures/release-macos.md), [`release-windows.md`](../procedures/release-windows.md), [`release-linux.md`](../procedures/release-linux.md), and for macOS signing [`code-signing-macos.md`](../procedures/code-signing-macos.md). Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

**BT-17.** A release must pass every test before it is published; a known failure needs the Product Owner's recorded acceptance for that release, listed in its release notes.
The release procedures' test gates hold the steps (`procedures/release-*.md`); how the gate is run is [BT-25](release.md). Accepted RTM gaps are listed beside the accepted failures ([BT-42](release.md)). A standing red is accepted by its line in the accepted-gaps register, with its issue and owner; the register is what makes the gate green ([`test_strategy.md`](../artefacts/test_strategy.md), "Words used here")[^bt-17e].
*Why:* each acceptance of a known failure stays a decision.[^om-11]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-10.** Release binaries must be built locally on each platform; CI must not be the release route.
Never switch a platform to CI builds, or change the release route, without asking the Product Owner first, with the options and the cost of each; telling afterwards is not asking. Failures of the CI release workflow are not bugs to track. The release procedures carry no CI build path, only the line that CI is not the release route.[^bt-10r]
*Why:* a release also tests the local build path; one unilateral switch cost two failed CI rounds, two extra commits and hours.[^bt-10]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-11.** A release must be checked as published, not as built: after publishing, each update feed must be fetched from the release's public download URL and checked for the new build number and the intended minimum OS (against the previous release), and the update signature verified against the downloaded bytes, with the signing key passed explicitly to every verify and a deliberately broken signature as a negative control.
The release procedures (`procedures/release-*.md`) hold the steps.
*Why:* a release "can be internally perfect and still ship the wrong feed file": one platform's feed was a stale copy from the previous release for a month; a verify without the key file failed in a way that looked like a bad signature.[^bt-11]
*Owner:* Product Owner (proposed) · *Stage:* Release · *Compliance:* none yet

**BT-12.** A developer or agent must not assume which build a second machine that shares the library has installed.
Keep an installer of the latest `main` ready, named by its commit, built when a plan step closes or on request, not for every commit. A change that touches the shared library's files is called out with "install the new build before that machine opens these files".
*Why:* migrations and schema bumps change the shared library's files, and an old build could misread or rewrite them.[^bt-12]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-13.** GPU memory and performance must be judged on the primary platform, as the product runs there: with the simulator and launch-monitor software running beside the app, and that process named in any GPU or memory check.
A bench run without it proves nothing about GPU memory. Windows is the primary platform (`docs/implementation/platform_refactor_assessment.md`).
*Why:* in a real studio session both programs share one card; both stalled together on it.[^bt-13]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-18.** Installers in the shared local-release folder must not be staged or deleted without asking the owner first.
*Why:* that folder is what a second machine installs from ([BT-12](release.md)).[^om-13]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-24.** Every release must carry a build number strictly greater than the last release's: `PINPOINT_VERSION_BUILD` in `src/Core/version.h` on macOS and Windows, the version in the AppImage file name on Linux.
It is the only thing the macOS and Windows updaters compare; a local release keeps the last official number ([BT-33](release.md)).
*Why:* an update whose number is not greater is never offered: installed apps see "no newer version".[^bt-24]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-25.** The release test gate ([BT-17](release.md)) must run the `tests/` umbrella in a freshly deleted `build/tests`, never over an existing test build tree and never as a per-suite loop.
Configure, build and test failures all block. The total grows as tests are added, so no fixed count is pinned; a run that configured fewer tests than expected is still not green ([BT-22](test.md)). The job limit is the machine's own ([BT-1](build.md)).
*Why:* an incremental configure reported 134/134 passing while running binaries from an older checkout, and a fresh configure at the same commit failed at once; a hand-rolled suite loop silently stopped covering two suites added after it was written.[^bt-25]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-42.** A release must not be published unless the RTM, with its issue snapshot refreshed and regenerated at the release's commit, has no flag outside its accepted gaps and its baseline; the release smoke checklist has been observed on this release's build; and the release notes list every other hardware requirement ([`test_strategy.md`](../artefacts/test_strategy.md) §3) with the build it was last observed on, or "never observed".
- **The baseline does not block.** Lines in the RTM's baseline (today's gaps, which may only shrink) do not stop a release; the release notes give their count and list each hardware requirement among them by its last observed build. Accepted gaps are listed as BT-17 lists accepted failures.
- **The smoke checklist** is a fixed, committed list the Product Owner rules once (install and start; each device class connects; three real shots end to end with the launch monitor, the cameras and a phone; one calibration routine started and finished; End, then quit), each step naming the requirements it observes. Its steps that need no shot are done before the release, on this release's build, as an automated check or a short check at a desk; only its shot steps are done at the capture rig, in a short visit (the smoke visit). What counts as a shot step is [BT-43](test.md)'s. The list, and where each moved step runs, is not yet written: planned, in a release smoke procedure, once the Product Owner rules the checklist.[^bt-42-smoke] A code change after the smoke checklist means the smoke checklist again, not a full visit. Every other hardware requirement is observed at full visits, at a feature release or when the Product Owner asks; requirements the Product Owner marks safety-critical (lost work, lost shots) carry an age limit, set by the Product Owner, after which they need a fresh observation.
- **This release's build** is the release's commit on the primary platform, or an ancestor that `test_strategy.md` §7.1's script accepts; a local installer of it counts.
- **Before and after publishing.** Only the checks made before publishing count here (BT-26, BT-27, BT-28, BT-34). BT-11 is checked after publishing; it is recorded on the release, and a failed BT-11 reopens the release under its procedure.
*Why:* a visit observation proves only the build it was made on; a full visit every release is not possible for one person at one capture rig, so the release shows each device's last observation honestly instead of carrying it forward.[^bt-42]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* `rtm_check --release <commit>` (its release mode, `test_strategy.md` §8) and the observations register; until `rtm_check` and its baseline exist, the release notes' list of hardware requirements observed on this build is the compliance

**BT-26.** A macOS bundle must be launched, and its main window seen to open, before it is notarized; a crash on launch stops the release until it is fixed.
On Apple Silicon the bundle is signed first (ad hoc, or with the Developer ID and no notarization); only a code-signature kill of an unsigned arm64 bundle is explained away.
*Why:* the test suite and the relocation check never load the bundled libraries; a leaked build-machine library path loaded a second OpenMP runtime and the app died before `main()`, and notarization is the slow, irreversible step.[^bt-26]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-27.** A macOS DMG must pass Gatekeeper's assessment (notarized Developer ID), staple validation and a deep, strict signature verify before it is published; any failure stops the release.
*Why:* a DMG that is not notarized and stapled, or is signed with the wrong identity, reaches users as "damaged and can't be opened".[^bt-27]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-28.** An update must be signed over the exact bytes that are published, never a rebuild, and its signature verified the way the app verifies it before it is uploaded; a failed verify stops the release.
On every platform: the notarized DMG, the `-core` installer and the AppImage, each as uploaded. Verifying after publishing is [BT-11](release.md).
*Why:* a rebuild differs byte for byte, so its signature fails and every client rejects the update.[^bt-28]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-29.** Every release must carry all four builds: Windows (Intel x86-64), Linux (Intel x86-64), macOS Intel (`x86_64`) and macOS Apple Silicon (`arm64`); no release is published without all four, and it is published non-draft and non-prerelease only once every build's assets are on its draft.
Each build's assets: Windows the `-core` installer, the `-cuda` runtime and `appcast-win.xml` (the core and CUDA split is inside the Windows build, and the `-cuda` runtime is never in the appcast); Linux the AppImage, its `.zsync` and its `.sig`; macOS Intel its DMG and `appcast-mac.xml`; macOS Apple Silicon its DMG and `appcast-mac-arm64.xml`. The builds meet on one draft: the machine that finishes first creates it, the one that finishes last checks the asset list and publishes. The order of the Windows uploads is [BT-30](release.md).
*Why:* the updaters read only the latest non-prerelease release: a missing feed 404s (twice, for about 40 minutes and from 16:33 UTC), a stale feed inherited from the last release offered nothing, a missing `-cuda` runtime left the GPU offer on a page with no installer, and a release with one macOS architecture is worse than none for the other.[^bt-29][^bt-29r]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-30.** The Windows update feed (`appcast-win.xml`) must be uploaded after the installers it names, and not at all if an installer's upload failed.
*Why:* the feed promises an installer at a URL with a byte length; uploaded first, it advertises one that 404s. A mac-first publish has left Windows broken until this step ran on three releases.[^bt-30]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-31.** A signing Mac must be set up by importing the backed-up Developer ID certificate; a new Developer ID certificate must never be issued.
*Why:* macOS keys every user's camera, microphone, Bluetooth and speech grants to the signing certificate, and Sparkle treats an update signed by another certificate as a different app: a new certificate silently resets the grants and stops in-place updates.[^bt-31]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-32.** Release signing secrets must stay offline and backed up offline: the Developer ID certificate and its key, the notarization app-specific password, and each platform's update-signing key; never committed, never a CI secret.
The Developer ID stays in the login keychain and signs only from a desktop session, never from a keychain whose password is on disk (that is the development certificate's arrangement only). Notarization uses an app-specific password, never the account password. Each signing Mac holds its own copy of the notary profile and the update-signing key.
*Why:* a leaked update key lets anyone ship a "trusted" update to every user; a lost certificate or key ends verifiable updates.[^bt-32]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-33.** A local release build must never be signed or uploaded, must not bump the version, and must reach another account only through its installer, never by copying files.
Staging it in the shared folder is [BT-18](release.md).
*Why:* the installer is the contract that keeps the other account's install and its uninstall log honest; the commit in the file name is the build's only identity.[^bt-33]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-34.** Every Windows release's installer must be checked on a clean virtual machine to still create the phone-link listener's firewall rule; never on an upgrade.
*Why:* once the rule exists it persists, so an installer that stopped creating it still works on every machine that ever ran a build, and fails only at a studio, weeks later, as "pairing doesn't work".[^bt-34]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-35.** Nothing may write into a macOS app bundle after it is signed: a build or packaging step that writes into the bundle must run before the signing step, in the development build and the release alike.
*Why:* any write after signing breaks the seal: the release is rejected by Gatekeeper ("notarization succeeded but the app is damaged") and the development build loses its permission grants.[^bt-35]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-36.** The macOS bundle id (`com.pinpoint-golf.pinpointstudio`) must not change.
*Why:* it is part of the designated requirement the permission grants are keyed to; changing it orphans every existing grant.[^bt-36]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-37.** The release entitlements file must hold only the entitlements a launch has proved necessary: add the minimum a hardened-runtime crash needs, never the set.
*Why:* library validation is opted out only because the bundled third-party code is signed by its vendors, not by the team; `allow-jit`, `allow-unsigned-executable-memory` and `allow-dyld-environment-variables` stay out until a launch crash proves one is needed.[^bt-37]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-45.** An uncertainty census and its fixes must be done before validation and before the first full public release; a beta does not wait on it.
The census and its gate are `docs/design/uncertainty_census_design.md`, "Gate".
*Why:* a reading's σ reaches the chance that a fault fired, the session's counts and the ranking of root causes, so that chain is only as honest as the σ fed into it; on 2 Oct 2026, 42 of one session's 58 metrics carried no per-reading σ.[^bt-45]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**Guidance, lessons that bite here:** [L-26](../guidance/lessons.md) macOS permissions and signing.

[^om-11]: Product Owner, 25 Nov 2026 (#195, OM-11).
[^bt-45]: Product Owner, 2 Oct 2026; a control from 26 Nov 2026 (the memory audit, item 18).
[^bt-10]: Product Owner, 5–6 Nov 2026.
[^bt-10r]: Product Owner, 25 Nov 2026 (#195): no CI release route; the CI build paths were removed from the release procedures.
[^bt-11]: Incidents 30 Jul 2026 (the key file) and 3 Sep 2026 (the stale feed). Owning role (proposed): Product Owner.
[^bt-12]: Product Owner, 22 Nov 2026.
[^bt-13]: Product Owner, 11 Nov 2026 (Windows is the primary platform) and 20 Nov 2026 (the shared GPU).
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^bt-24]: Moved from the release procedures (macOS §1.1, Windows step 1, Linux §0), 25 Nov 2026 (#195), on the Product Owner's instruction that the procedures' mandatory rules are controls.
[^bt-25]: Moved from the release procedures' test gates (macOS §1.2, Windows step 2, Linux §0.5), 25 Nov 2026 (#195). Incidents: 16 Aug 2026 (the stale test tree); the suite loop, retired at v0.1-alpha11.
[^bt-26]: Moved from `release-macos.md` §1.4 (a mandatory gate), 25 Nov 2026 (#195).
[^bt-27]: Moved from `release-macos.md` §1.6, 25 Nov 2026 (#195).
[^bt-28]: Moved from the release procedures (macOS §1.7 and "Gotchas", Windows steps 4–5 and "Gotchas", Linux signing and "Gotchas"), 25 Nov 2026 (#195).
[^bt-29]: Moved from the release procedures (macOS §1.8, Windows steps 6–7 and "Gotchas", Linux "Facts you need" and "Gotchas"), 25 Nov 2026 (#195). Incidents: v0.1-alpha11 (the stale Windows feed; the missing `-cuda` runtime), v0.1-beta2 and v0.1-beta3 (22 Sep 2026) (the 404s).
[^bt-29r]: Product Owner, 25 Nov 2026 (#195): every release carries four builds (Windows, Linux, macOS Intel, macOS Apple Silicon); this replaces the per-platform wording.
[^bt-30]: Moved from `release-windows.md` step 6, 25 Nov 2026 (#195). Incidents: v0.1-alpha11, v0.1-beta2, v0.1-beta3.
[^bt-31]: Moved from `release-macos.md` §0.3a and `code-signing-macos.md` §5 and §8, 25 Nov 2026 (#195).
[^bt-32]: Moved from the release procedures (macOS §0.3, §0.5, §0.6, "Gotchas & key custody"; Windows and Linux introductions) and `code-signing-macos.md` §5, 25 Nov 2026 (#195).
[^bt-33]: Moved from `release-windows.md`, "Local release", 25 Nov 2026 (#195).
[^bt-34]: Moved from `release-windows.md` (the per-release checklist and "Gotchas"), 25 Nov 2026 (#195); the installer's rule shipped in v0.1-beta5.
[^bt-35]: Moved from `code-signing-macos.md` §6, 25 Nov 2026 (#195).
[^bt-36]: Moved from `code-signing-macos.md` §8, 25 Nov 2026 (#195).
[^bt-37]: Moved from `code-signing-macos.md` §5 and `release-macos.md`, "Troubleshooting", 25 Nov 2026 (#195).
[^bt-17e]: Product Owner, 25 Nov 2026 ([POL-9](../policies.md)); the accepted-gaps sentences approved with BT-42 by the Product Owner and the Solution Architect, 26 Nov 2026.
[^bt-42]: Product Owner, 25 Nov 2026 ([POL-9](../policies.md): a visit observation lasts for its build only). The smoke visit, the last-observed listing, the age limit, and a local installer counting as this release's build: approved with the control by the Product Owner and the Solution Architect, 26 Nov 2026.
[^bt-42-smoke]: The Solution Architect and the Product Owner, 26 Nov 2026 (the Solution Architect's afternoon batch, item 11: "I approve"): the smoke checklist's no-shot steps move to an automated or desk check before the release, and only its shot steps stay at the capture rig, as the visit rule (BT-43) has it.
