# PinPointStudio — GUI component catalogue

A catalogue of every component in the GUI layer, built to (1) seed fuller developer documentation later and (2) find duplicated code in the GUI. It was built outside the repo while another session was editing, then moved here to `docs/implementation/gui-catalogue/`.

Based on repo commit `8f5225a8` (the catalogue pages were not regenerated after later commits, so counts and line numbers may have drifted) (`src/Gui` and `cmake/` clean at the time of the final run). Scope: `src/Gui/**` — 290 app QML files (90k lines), 3 helper `.js`, 157 C++ files (73k lines). Test QML (80 files) and tool QML (15 files) are listed but not catalogued.

## Read in this order

| File | What it is |
|---|---|
| [00-overview.md](00-overview.md) | How the layer is put together, sizes, naming, most-reused and largest components, files nothing instantiates, Theme bypass |
| [duplicates.md](duplicates.md) | **The findings.** 16 ranked themes, a **merge map** (§17: which components could become one, by confidence and by decision needed), **copied-code list** (§18), what is *not* duplicated (§19), **§20 — how the new typeset aesthetic changes the merge plan**, and dead/parked files |
| [areas/](areas/) | One page per area (23 areas, 290 components): purpose, public API, what it uses, who uses it |
| [cpp-seam.md](cpp-seam.md) | The C++↔QML boundary: 39 context-property globals (and which components lean on them), `QML_ELEMENT` types, C++ classes |
| [data/components.csv](data/components.csv) | One row per QML file (area, lines, root type, fan-in, API size, Theme refs…) for sorting/filtering |
| [data/duplicate-evidence.md](data/duplicate-evidence.md) | The mechanical signals behind the duplicate findings |
| `findings/` | Raw per-cluster reports from the four review agents (before consolidation into duplicates.md) |
| `inventory.json`, `duplicates.json` | Not committed (2 MB, regenerable): produced by the first two commands under *Regenerating* |
| `tools/` | The scripts that produced all of the above |

## How it was made (and what to trust)

1. **`tools/extract_qml.py`** parses every `.qml`/`.js`/GUI `.h`/`.cpp` (regex-based, not a QML parser) → `inventory.json`: root type, root-level properties/signals/functions, local components, instantiated types, reverse "used by" index, Theme and hard-coded-colour counts, header comment, C++ classes, context properties.
2. **`tools/find_duplicates.py`** → `duplicates.json`: shingle similarity between files, shared runs of ≥7 normalised lines, identical function bodies.
3. **`tools/build_catalogue.py`**, **`tools/build_evidence.py`** render the markdown and CSV.
4. **Four read-only agents** read the code behind the mechanical hits and the "same job, different code" cases the scripts can't see, one per cluster (interactive primitives; containers/scaffolding; charts/dataviz; screens/flows). Their raw reports are in `findings/`; [duplicates.md](duplicates.md) consolidates them.

**Descriptions.** Each component's description is its own header comment from the source (shown as a quote, up to 16 lines) — 250 of 290 have one. The 40 without were described by hand from their code and are marked *"description written for this catalogue"*; treat those as drafts. Counts (lines, fan-in, API size) are measured. "Used by" = other app files that name the component; it can miss components loaded from a string path, and `Used by 0` is checked against `src/`, `tools/`, `tests/` and `cmake/` in the overview.

**Known limits.** The extractor is regex-based: root-level API is "declared at ≤4 columns of indent"; a handful of C++ classes declared in unusual ways could be missed; the clone detector reports textual clones, so semantically-equal-but-differently-written code is only found by the agents' reading.

## Regenerating

```sh
R=/Users/markliversedge/Projects/PinPointStudio
C=$R/docs/implementation/gui-catalogue
python3 -I $C/tools/extract_qml.py      $R $C/inventory.json
python3 -I $C/tools/find_duplicates.py  $R $C/inventory.json $C/duplicates.json
python3 -I $C/tools/build_catalogue.py  $C $R
python3 -I $C/tools/build_evidence.py   $C
```
`areas/`, `00-overview.md`, `cpp-seam.md`, `data/*` are regenerated; **`README.md` and `duplicates.md` are hand-assembled and are not touched.** The 40 hand-written descriptions (used only where a file has no header comment) live in `tools/purposes_override.json`.

## If this becomes developer docs

The pages are deliberately shaped as the *reference* half of a developer guide (one entry per component, uniform fields). Suggested next steps, in order of value: fix the duplicates that [duplicates.md](duplicates.md) ranks highest (they change what the docs would have to describe); move the generator into `tools/` in the repo with a CI check that every `.qml` has a header comment; then write the *explanatory* half by hand — the layering rules, the global-context-property convention, the typeset system and the coaching-card look — which the design-system doc (`docs/design/pinpoint_qml_design_system.md`) already half covers.
