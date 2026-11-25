# Controls: 7. Release

Building, publishing and installing a release. The platform runbooks are listed in [`procedures/README.md`](../procedures/README.md). Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

**BT-17.** A release must pass every test before it is published; a known failure needs the Product Owner's recorded acceptance for that release, listed in its release notes.
The runbooks' mandatory gates hold the steps (`docs/implementation/*_release_runbook.md`).
*Why:* each acceptance of a known failure stays a decision.[^om-11]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-10.** Release binaries must be built locally on each platform; CI must not be the release route.
Never switch a platform to CI builds, or change the release route, without asking the Product Owner first, with the options and the cost of each; telling afterwards is not asking. Failures of the CI release workflow are not bugs to track.
*Why:* a release also tests the local build path; one unilateral switch cost two failed CI rounds, two extra commits and hours.[^bt-10]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**BT-11.** A release must be checked as published, not as built: after publishing, each update feed must be fetched from the release's public download URL and checked for the new build number and the intended minimum OS (against the previous release), and the update signature verified against the downloaded bytes, with the signing key passed explicitly to every verify and a deliberately broken signature as a negative control.
The per-platform runbooks (`docs/implementation/*_release_runbook.md`) hold the steps.
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
*Why:* that folder is what the studio machine installs from.[^om-13]
*Owner:* Product Owner · *Stage:* Release · *Compliance:* none yet

**Guidance, lessons that bite here:** [L-26](../guidance/lessons.md) macOS permissions and signing.

[^om-11]: Product Owner, 25 Nov 2026 (#195, OM-11).
[^bt-10]: Product Owner, 5–6 Nov 2026.
[^bt-11]: Incidents 30 Jul 2026 (the key file) and 3 Sep 2026 (the stale feed). Owning role (proposed): Product Owner.
[^bt-12]: Product Owner, 22 Nov 2026.
[^bt-13]: Product Owner, 11 Nov 2026 (Windows is the primary platform) and 20 Nov 2026 (the shared GPU).
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
