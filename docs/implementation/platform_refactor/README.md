# The platform refactor's working inputs

The platform refactor's remaining work is GitHub issues labelled `platform refactor` (#375 to #401). How they are worked is the standing block, #402; decisions waiting on a role are #403. This folder holds the inputs those issues' briefs depend on, so no step needs anything outside the repository. They were the programme's local working files until 26 Nov 2026, copied here with machine names, share paths and home paths removed.

These are working documents, not governance: where one disagrees with `governance/` or with a later ruling recorded on an issue, the governance text or the later ruling wins. Each brief says to check it against every ruling made since it was written.

| Path | What it is | Used by |
|---|---|---|
| `moves.md` | How a scripted file move is run and proved (the move tool, `--verify --configure`, the includer count, parity) | every step that moves files by script: #377–#380, #382, #383, #400 |
| `duties/` | Duties inventories: everything the old code does that its replacement must keep, written before each cut-over (AG-10) | #377 (cameras), #378 (sensors), #380 (the trunk calibration) and the calibration screens' steps |
| `plans/` | The stage plans (B1, B2, C, D, E, the screens' presenters as U), the Stage B Part 2 planning notes, the Stage E sizing, the forecast | every brief that names its plan; the 58 steps with no brief yet are written from these |
| `rulings/` | The rulings the briefs cite by id: `LEVERS-U-C-D.md` (D-V1 to D-V4, C-V1, U-V0 to U-V3), `JUDGEMENT-CALLS.md` (PR-*, and every call and ruling of the programme in time order), the two morning-rulings pages (Stage C, Stage E), the lever follow-ups | any brief that cites a ruling id |
| `visits/` | The visit plan: the 26 Nov re-cut (`RECUT-26nov.md`), visit 2 folded into visit 3 (`FOLD-2-into-3.md`), and the earlier visits' procedures as the pattern for visit 3's (#397's step 96 writes `VISIT-3.md`) | #390, #395–#401 |
| `standing-C.md` | The rules every Stage C brief carries, beyond #402 | #377–#380, #400 |
| `pending-tools/` | Three scripts not yet in `tools/`: `ring_census.py` and `link_census.py` (step 18, #381, moves them into `tools/refactor/` with their tests), and `count_includers.py` (the includer count a scripted move's proof uses) | #381–#383, every scripted move |
| `parked/281-overlap.patch` | The overlapping-capture change for #281, built and parked on 25 Nov 2026 | #394, step 85 |
| `notes/` | Earlier items' notes that later briefs read (B0.2b, B0.4, B0.5, B1.4 and its review; the #281 investigation behind the parked patch; every Stage B Part 1 item's notes and review in `stage-B1/`, for #376's count of leftovers), and the Stage B build-time baseline (`build-times-e10eddf7.txt`, `build-times-a7a6b4b2.txt`, taken on the studio PC on 24 Nov 2026) | #376, #381–#384, #394 |

Earlier items' notes that these files mention by a `build/platform/…` path, and the per-item information-model excerpts, were not kept: the items they describe have landed, and their commits carry what was done. Read the information model by section instead of an excerpt.
