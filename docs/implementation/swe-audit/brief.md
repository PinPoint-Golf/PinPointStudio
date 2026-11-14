# SWE audit — reviewer brief (shared by every reviewer)

You are one of eight independent reviewers auditing PinPoint Studio's analysis and diagnostics code for
software-engineering discipline. The repo is at `/Users/markliversedge/Projects/PinPointStudio`, pinned at
commit `37289e33` — cite code as it is there. **Read-only: never edit, build, run tests, commit, or touch any
file outside your own report.** Write your report to the path your task names, and nothing else.

## Why this audit exists

The app grew from prototypes, through an organic change of direction, to a UX refactor, and is now ~500k
lines. The owner found a basic MVC fault (a session's diagnostics model was created *inside a view* and did
not exist unless that panel was on) and asked: is weak engineering discipline everywhere? A June review
(`docs/implementation/code_quality_review.md`) looked at mechanical duplication only and called the code
"well-engineered". This audit is **architecture first**: ownership, identity, layering, single sources of
truth — then duplication and hygiene. Be rigorous and honest in both directions: a clean area must read as
clean, and a structural fault must not be softened into a style note.

## The domain, as the owner states it

- Containment: **athlete → session → shot** (one swing folder `swing_NNNN/` with `swing.json`) → analysis → LM
  (launch monitor) reading.
- **Diagnostics have three levels, each with its own owner:**
  - **Shot:** that shot's condition rows (after the strike-vs-flight check), composed outcome, three factors
    (strike · direction · flight), carry.
  - **Session:** the reduced ledger, links/tiers, flight usuals, per-club mixes, carry today, patterns, the
    session report.
  - **Athlete:** tendencies, work-ons, club baselines and usual distances, `fault_profile`, consistency, the
    golfer's own norms.
  Each level derives from the one below and gives context back down (session usuals feed a shot's Sky and
  strike check; athlete baselines feed session grading). Each should be owned at its own level, created and
  destroyed with its parent (a reviewed or live session; the selected athlete), and readable by any view.

## The checklist — apply all 15 to your area

1. **Ownership and lifetime** — who creates/owns/destroys each object; does it live and die with its parent
   (athlete, session, shot)? Are the three diagnostic levels owned at their own level or mixed into one object?
   View-owned models, duplicate copies, app-wide objects holding per-session/per-athlete state, state leaking
   across an athlete or session switch.
2. **Identity** — one identity per domain object; every id/handle/string key and every translation between them.
3. **Layering / dependency direction** — assumed order: Core · producers · Export/Analysis/Metrics · Diagnostics
   · Coaching · Gui. Upward includes, domain code doing file I/O, domain rules written in QML, presentation
   wording built inside domain code.
4. **One source per derived fact** — computed once, at the right diagnostic level, stored on its owner, read by
   all; not re-derived per surface; a level re-deriving a lower level's facts itself.
5. **Duplication** — copied functions, parallel pipelines doing one job, parallel tables/enums, ids as repeated
   string literals.
6. **Size and cohesion** — god classes/files, functions > ~150 lines, too many responsibilities per class.
7. **Contracts** — `QVariantMap`/JSON shapes documented only in comments, string-typed interfaces, read paths
   that deep-copy.
8. **State and concurrency** — `mutable` caches + dirty flags in `const` methods, worker threads inside models,
   signal cascades, re-entry, races.
9. **Persistence and versioning** — one reader/writer per format, where migration lives, `k*Version` discipline.
10. **Configuration vs code** — thresholds hard-coded or defined twice, norms in code rather than data.
11. **Errors and missing values** — mixed "nothing" conventions (NaN, −1, "", `{}`), silent failures, fallbacks
    that mask a fault.
12. **Testability** — logic reachable without Qt/ONNX/GUI; tests at the right level; seams that exist only for tests.
13. **Comments as a symptom** — long ⚠ war stories explaining a workaround; each points at a structural fault
    patched where it showed up. Read them: they are often the best evidence.
14. **Dead and leftover code** — retired keys, legacy paths, unread settings, shims.
15. **Names and namespaces** — ODR hazards (a known collision in `pinpoint::analysis`), names that hide their layer.

## Inputs you should use

- Mechanical baseline at `docs/implementation/swe-audit/data/`: `include_graph.md`, `size_table.md`,
  `seam_inventory.md`, `identity_inventory.md`, `constants_inventory.md`, `cpp_duplicates.md` (+ `.json`).
  These are heuristic counts — confirm anything you cite by reading the code.
- `docs/implementation/code_quality_review.md` (June): for findings in your area, say whether each is now
  **fixed, open, or worse**, with evidence.
- Design docs under `docs/design/` and developer guides under `docs/developer/` describe intent; drift between
  stated design and code is itself a finding.
- If your area includes QML, CLAUDE.md's UI rules apply to what you judge (design system in
  `docs/design/system/README.md`; never hand-draw controls; Theme tokens only). Any design-system change needs
  the owner's approval before it is built — you are not building anything, but don't recommend one as if it
  were free.

## Evidence rule

- Every finding cites `file:line` at `37289e33` and quotes the decisive line(s) (short).
- A **runtime defect** you have not reproduced is labelled **suspected**, with the exact scenario that would
  show it. Do not claim a bug from a code smell alone.
- A **structural** finding needs the cited code that shows the structure (the include, the instantiation, the
  two copies side by side, the function and its length).
- No speculation about intent; if the code is ambiguous, say so.

## Finding schema (one block per finding)

```
### <AREA>-<NN> · <title>
- Category: <checklist number + name>      - Severity: Critical | High | Medium | Low
- Evidence: path:line — `quoted code`  (one or more)
- What goes wrong: the concrete failure it causes or could cause (scenario, not adjectives)
- Fix: what the right structure is, concretely
- Effort: S | M | L        - Related: other finding ids / June review item, if any
```

Severity: **Critical** = wrong data shown or persisted, or data lost, at HEAD (needs evidence) · **High** = a
structural violation every change must work around, or copies that already disagree · **Medium** = raises the
cost or risk of change · **Low** = hygiene.

## Report shape

1. **Summary** (≤ 15 lines): the state of your area in plain words, the 3–5 things that matter most.
2. **How the area is put together** (≤ 1 page): its objects, who owns them, how data flows in and out.
3. **Findings**, most severe first, in the schema.
4. **Done well** — what is sound and should be kept (with evidence).
5. **June review status** for your area.
6. **Questions** you could not settle from the code.

Be thorough rather than quick: read the large files properly, not just their headers. Quality over count —
twenty verified findings beat sixty guesses.
