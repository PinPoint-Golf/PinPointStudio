# Software-engineering audit — analysis and diagnostics (November 2026)

An architecture-first audit of how PinPoint Studio's analysis and diagnostics code is put together:
ownership, identity, layering and single sources of truth first, then duplication and hygiene. It began
when the carousel's shot summary could not be built cleanly, because the session's diagnostics model was
created inside a view and did not exist unless that panel was on.

- **Pinned at commit `37289e33`** (14 Nov 2026). Every `file:line` in these pages resolves there.
- **No production code was changed.** The deliverable is these documents and the scripts that produced
  their numbers.
- The June review (`../code_quality_review.md`) covered mechanical duplication only. Its findings are
  re-checked here, area by area.

## Read in this order

| File | What it is |
|---|---|
| [00-summary.md](00-summary.md) | Plain-language summary, the themes, the top actions |
| [01-information-model.md](01-information-model.md) | Athlete → session → shot and the three diagnostic levels (shot, session, athlete) as the code has them today |
| [02-findings.md](02-findings.md) | Every verified finding, by theme then severity |
| [03-target-architecture.md](03-target-architecture.md) | The proposed ownership model, identity and layer rules. **For approval** |
| [04-remediation-plan.md](04-remediation-plan.md) | Staged, dependency-ordered and testable plan |
| [brief.md](brief.md) | The brief every reviewer worked from: 15-point checklist, evidence rule, finding schema |
| `findings/` | The eight reviewers' raw reports (A–H) and the verification record |
| `data/` | Generated measurements (see below) |
| `tools/` | The scripts |

## Scope

| Ring | Directories |
|---|---|
| 1. Core | `src/Analysis`, `src/Metrics`, `src/Diagnostics`, `src/Coaching`, `src/Gui/diagnostics` |
| 2. Consumers | `src/Gui/shot`, `review`, `session`, `home`, `charts`, `report` |
| Boundary | `src/Core`, `src/LaunchMonitor`, `src/Export`, and the producers, at their seams only |

## Method

1. **Mechanical baseline** (`tools/`, outputs in `data/`):
   - include graph, with edges that run against an assumed layer order;
   - file and function sizes;
   - the C++↔QML seam, and which views create models;
   - shot, session and athlete identity keys and every place they are translated;
   - duplicated constants;
   - copied C++ and QML.
2. **The information model as it is.** Written from the baseline plus reading the wiring
   (`01-information-model.md`).
3. **Eight independent reviewers**, each given the code, the checklist and the baseline but not the
   lead's conclusions. Each also had to record what is done well.

   | | Area |
   |---|---|
   | A | Analysis + Metrics |
   | B | Diagnostics |
   | C | Coaching |
   | D | Gui/diagnostics |
   | E | shot / session / selection |
   | F | charts + report |
   | G | derived facts across the three levels |
   | H | persistence |
4. **Verification.**
   - Every finding's code was read by the lead before acceptance, and `tools/check_citations.py` checks
     each citation against the pinned commit.
   - Every Critical and High finding then went through an adversarial pass that tried to disprove it.
5. **Synthesis.** Summary, target architecture, remediation plan.

## Regenerating

```sh
R=/Users/markliversedge/Projects/PinPointStudio; T=$R/docs/implementation/swe-audit/tools
for t in include_graph size_table seam_inventory identity_inventory constants_inventory cpp_duplicates; do
  python3 -I $T/$t.py $R
done
python3 -I $T/check_citations.py $R 37289e33 $R/docs/implementation/swe-audit/*.md $R/docs/implementation/swe-audit/findings/*.md
```

## What to trust

- **The counts are heuristic.**
  - Function lengths use a brace-matching regex, not a C++ parser.
  - Clones are textual, so same-job-different-code was found only by the reviewers' reading.
  - The "same value, different name" table in `constants_inventory.md` is mostly coincidence. Its
    same-name table is the useful part.
- **The layer order is an assumption.** The include graph's "against the assumed order" list is judged
  against Core · producers · Export/Analysis/Metrics · Diagnostics · Coaching · Gui. That order is the
  audit's working hypothesis, not a rule the code was ever held to.
- **Runtime defects are "suspected" unless they were reproduced.** The audit ran no builds and no tests.
