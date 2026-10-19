#!/usr/bin/env python3
"""Render the GUI catalogue (markdown + csv) from inventory.json.

    build_catalogue.py <catalogue-dir>

Reads  <dir>/inventory.json, <dir>/tools/purposes_override.json
Writes <dir>/README-generated parts: 00-overview.md, areas/*.md, cpp-seam.md, data/components.csv
(README.md and duplicates.md are hand-assembled and are NOT touched.)
"""
import json, os, sys, re, csv, collections

D = os.path.abspath(sys.argv[1])
REPO = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else "/Users/markliversedge/Projects/PinPointStudio"
inv = json.load(open(os.path.join(D, "inventory.json")))
override = json.load(open(os.path.join(D, "tools/purposes_override.json")))
comps = [c for c in inv["components"]]
app = [c for c in comps if c["category"] == "app"]
by_path = {c["path"]: c for c in comps}

def area_of(c):
    p = c["dir"].replace("src/Gui/", "")
    return p.split("/")[0] if p.startswith("setup") else p

AREAS = collections.OrderedDict([
 ("theme", ("Theme", "The single source of visual tokens (colours, fonts, spacing functions, radii, per-aesthetic data). Every other component reads it; nothing should hard-code a colour, font or size.")),
 ("components", ("Shared components", "The app-wide primitives: buttons, chips, cards, pills, badges, toasts and the coaching-card shell (PpCard family). The intended single home for anything reused by more than one area.")),
 ("typeset", ("Typeset", "The 'typeset system' (design system §15): the per-panel helper PpTypeset plus a shared plot/typography vocabulary (box plot, zone plot, brush, marks, section heads, pager, folio) that draws identically on screen and in print.")),
 ("shell", ("Shell", "The application window and chrome: Main.qml, rail, header, about dialog, update banner, detect cluster, analysing badge, and the dev-hatch PlayPage.")),
 ("home", ("Home", "The home screen: onboarding, athlete/club/session-type selection, the 'your swing' summary (focus, work-ons, goes-together), swing-diagnostics page, and PPCP phone pairing/offers.")),
 ("athlete", ("Athlete", "Athlete picker, athlete create/edit form and per-athlete club records.")),
 ("setup", ("Session setup", "The session-setup flow (replaced the old wizard; docs/design/session_wizard_refactor_design.md): a non-visual flow engine, step descriptors as data, a hardware-context object, a WizardPage contract, nine step pages and a few shared parts.")),
 ("calibration", ("Calibration", "IMU arm calibration routines (HackMotion, Witmotion), camera-calibration stub and the calibration guide, hosted both by the setup flow and the Wrist toolbar panel.")),
 ("session", ("Session screens", "The live/replay/analyse session screens and their furniture: persistent toolbar, stage arranger (PpModeStage), toolbar panels (club, view, motion, launch monitor, markup), session drawer; plus the SessionMode and ViewLayout singletons.")),
 ("cameras", ("Cameras", "Camera frames/tiles, the toolbar camera panel and the analyse-view telestrator (annotation tool, layer, toolbar, icons).")),
 ("imu", ("IMU", "The toolbar IMU device panel, the sensor-mount vocabulary singleton, and the dev-hatch capture page.")),
 ("shot", ("Shots", "The shot carousel/card/filter/action bar, export options, swing edit panel, shot pip row, the data-warning format singleton, and the wall-cast session-diagnostics window.")),
 ("review", ("Review (replay/analyse)", "The older replay/analyse stack: metric charts, data viewer, coverage and sequence strips, replay transport, and the Metric Catalogue (library/detail/route).")),
 ("charts", ("Charts panel", "The Charts stage panel (PpCh*): This shot / Session / Relations / Kinds views and their blocks, built on the typeset vocabulary.")),
 ("diagnostics", ("Session diagnostics", "The Session-diagnostics stage panel and its three registers (Analyse, Replay, Print), built from typeset blocks: ledger, sparklines, small multiples, cause graph, history strips, questions.")),
 ("diagnosticmodel", ("Diagnostic Model editor", "Settings > Reference > Diagnostic Model: a three-pane editor (type rail, table, relationship inspector) for the diagnostics model pack, with graph, corridor plot, ring menu and edit/unsaved tooling.")),
 ("trust", ("Trust and gates", "'Readings and their limits': the Trust panel, inline reasons, gate notes and the TrustFocus routing singleton.")),
 ("monitor", ("Resource monitor", "The Resource Monitor screen and its device cards, source/stat/profiler/analysis-run rows, PPCP phone and camera rows, timeline chart.")),
 ("settings", ("Settings", "The Settings screen, its search index singleton and the panels: General, Appearance, Displays, Cameras, IMUs, Phones, Microphones, Launch Monitor, Storage, Archiving.")),
 ("media", ("Media (dev hatch)", "Audio (STT/TTS), AI Coach and Film pages; only reachable through PlayPage's developer hatch (Film not at all).")),
 ("viz", ("3-D body/IMU views", "QtQuick3D views of IMUs, the arm and the body, plus the View3D keep-alive guard.")),
 ("swing3d", ("Swing 3-D", "The skeleton3d swing visualisation view and its keep-alive host.")),
 ("print", ("Print", "PDF printing of paged registers.")),
])

# ---------- helpers ----------------------------------------------------------------
def users(c):
    return sorted(set(os.path.basename(u)[:-4] for u in c["used_by"] if by_path[u]["category"] == "app"))
def test_users(c):
    return sorted(set(os.path.basename(u)[:-4] for u in c["used_by"] if by_path[u]["category"] != "app"))
def purpose(c):
    if c["path"] in override and not [x for x in c["header"] if x]: return override[c["path"]], "written"
    h = [x for x in c["header"] if x]
    more = len(h) > 16
    return "\n".join("> " + x for x in h[:16]) + ("\n> …" if more else ""), "source"
def esc(s): return s.replace("|", "\\|")
def api_props(c):
    return [p for p in c["root_props"] if not p["name"].startswith("_") and p["name"] not in ("index", "modelData")]
def fmt_prop(p):
    m = ("required " if p["required"] else "") + ("readonly " if p["readonly"] else "")
    return f"`{m}{p['type']} {p['name']}`"

for c in comps:
    c["_users"] = users(c); c["_tusers"] = test_users(c)
    c["references"] = [r for r in c["references"] if r != "Theme"]

def entry(c):
    out = []
    pur, src = purpose(c)
    tags = []
    if c["singleton"]: tags.append("**singleton**")
    if not c["_users"] and not c["singleton"]: tags.append("⚠ **no app file instantiates this**")
    if c["n_theme_refs"] == 0 and c["code_lines"] > 30 and c["path"] != "src/Gui/theme/Theme.qml": tags.append("no direct `Theme.*` refs")
    if c["n_hard_colors"] and c["path"] != "src/Gui/theme/Theme.qml": tags.append(f"{c['n_hard_colors']} hard-coded colour(s)")
    out.append(f"### {c['name']}")
    out.append(f"`{c['path']}` · {c['lines']} lines · root `{c['root_type']}`" + (" · " + " · ".join(tags) if tags else ""))
    out.append("")
    out.append(pur if src == "source" else pur + "  \n*(description written for this catalogue — the file has no header comment)*")
    out.append("")
    props = api_props(c)
    if props:
        shown = ", ".join(fmt_prop(p) for p in props[:14]) + (f" … (+{len(props)-14} more)" if len(props) > 14 else "")
        out.append(f"- **Properties:** {shown}")
    if c["root_signals"]:
        out.append("- **Signals:** " + ", ".join(f"`{s['name']}{s['sig']}`" for s in c["root_signals"]))
    pf = [f for f in c["root_functions"] if not f["name"].startswith("_") and not f["name"].startswith("on")]
    if pf:
        out.append("- **Functions:** " + ", ".join(f"`{f['name']}{f['sig']}`" for f in pf[:10]) + (f" (+{len(pf)-10})" if len(pf) > 10 else ""))
    if c["inline_components"]:
        out.append("- **Local components:** " + ", ".join(f"`{i['name']}`" for i in c["inline_components"]))
    refs = [r for r in c["references"]]
    if refs:
        out.append("- **Uses:** " + ", ".join(f"`{r}`" for r in refs[:16]) + (f" (+{len(refs)-16})" if len(refs) > 16 else ""))
    if c["_users"]:
        out.append(f"- **Used by ({len(c['_users'])}):** " + ", ".join(f"`{u}`" for u in c["_users"][:10]) + (f" (+{len(c['_users'])-10})" if len(c["_users"]) > 10 else ""))
    if c["_tusers"]:
        out.append("- **Exercised by tests/tools:** " + ", ".join(f"`{u}`" for u in c["_tusers"][:6]) + (f" (+{len(c['_tusers'])-6})" if len(c["_tusers"]) > 6 else ""))
    out.append("")
    return "\n".join(out)

# ---------- per-area files ---------------------------------------------------------
os.makedirs(os.path.join(D, "areas"), exist_ok=True)
by_area = collections.defaultdict(list)
for c in app: by_area[area_of(c)].append(c)
unknown = [a for a in by_area if a not in AREAS]
assert not unknown, unknown

index_rows = []
for i, (key, (title, blurb)) in enumerate(AREAS.items(), 1):
    cs = sorted(by_area.get(key, []), key=lambda c: (c["dir"], c["name"]))
    if not cs: continue
    lines = sum(c["lines"] for c in cs)
    nfiles = f"{i:02d}-{key}.md"
    body = [f"# {title}", "", f"*Directory:* `src/Gui/{key}/`" + (" (+ `pages/`, `parts/`)" if key == "setup" else "") + "  ",
            f"*{len(cs)} component{'s' if len(cs) != 1 else ''} · {lines:,} lines of QML*", "", blurb, ""]
    cpp = [x for x in inv["cpp"] if x["category"] == "app" and x["path"].startswith(f"src/Gui/{key}/") and x["classes"]]
    if cpp:
        body.append("**C++ in this area:** " + ", ".join(f"`{k['name']}`" for x in cpp for k in x["classes"]) + "  (see [cpp-seam.md](../cpp-seam.md))")
        body.append("")
    body += ["| Component | Lines | Root | Used by | Uses | Notes |", "|---|---:|---|---:|---:|---|"]
    for c in cs:
        notes = []
        if c["singleton"]: notes.append("singleton")
        if not c["_users"] and not c["singleton"]: notes.append("⚠ unreferenced")
        if c["inline_components"]: notes.append(f"{len(c['inline_components'])} local comp.")
        body.append(f"| [{c['name']}](#{c['name'].lower()}) | {c['lines']} | `{c['root_type']}` | {len(c['_users'])} | {len(c['references'])} | {', '.join(notes)} |")
    body.append("")
    cur = None
    for c in cs:
        if key == "setup" and c["dir"] != cur:
            cur = c["dir"]; body += [f"## `{cur.replace('src/Gui/','')}/`", ""]
        body.append(entry(c))
    open(os.path.join(D, "areas", nfiles), "w").write("\n".join(body))
    index_rows.append((nfiles, title, len(cs), lines, key))

# ---------- tests and tools --------------------------------------------------------
tt = [c for c in comps if c["category"] != "app"]
body = ["# Test and tool QML", "", "QML that is not part of the shipped module: offscreen UI tests (`tst_*.qml`, probes, fakes), and developer tools (`tools/typeset-harness`, `tools/probes`).", "",
        "| File | Kind | Lines |", "|---|---|---:|"]
for c in sorted(tt, key=lambda c: c["path"]):
    body.append(f"| `{c['path']}` | {c['category']} | {c['lines']} |")
open(os.path.join(D, "areas", "90-tests-and-tools.md"), "w").write("\n".join(body) + "\n")

# ---------- csv --------------------------------------------------------------------
os.makedirs(os.path.join(D, "data"), exist_ok=True)
with open(os.path.join(D, "data/components.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["name", "path", "area", "lines", "code_lines", "root_type", "singleton", "used_by_app", "used_by_tests",
                "uses", "public_props", "signals", "functions", "local_components", "theme_refs", "hard_colors", "has_header_comment", "category"])
    for c in sorted(comps, key=lambda c: c["path"]):
        w.writerow([c["name"], c["path"], area_of(c) if c["category"] == "app" else c["category"], c["lines"], c["code_lines"], c["root_type"],
                    int(c["singleton"]), len(c["_users"]), len(c["_tusers"]), len(c["references"]), len(api_props(c)),
                    len(c["root_signals"]), len([f for f in c["root_functions"] if not f["name"].startswith("_")]),
                    len(c["inline_components"]), c["n_theme_refs"], c["n_hard_colors"], int(bool(c["header"])), c["category"]])

# ---------- overview ---------------------------------------------------------------
tot_lines = sum(c["lines"] for c in app)
cpp_app = [x for x in inv["cpp"] if x["category"] == "app"]
ov = ["# GUI layer — overview", "",
      f"Generated from the repository at the time of the run (see README for the commit). All numbers below are measured, not estimated.", "",
      "## Size", "",
      f"| | Files | Lines |", "|---|---:|---:|",
      f"| App QML (shipped module `PinPointStudio`) | {len(app)} | {tot_lines:,} |",
      f"| App QML helper `.js` | {len([j for j in inv['js'] if j['category']=='app'])} | {sum(j['lines'] for j in inv['js'] if j['category']=='app'):,} |",
      f"| GUI-layer C++ (`src/Gui/**`, excluding tests) | {len(cpp_app)} | {sum(x['lines'] for x in cpp_app):,} |",
      f"| Test QML + probes | {len([c for c in comps if c['category']=='test'])} | {sum(c['lines'] for c in comps if c['category']=='test'):,} |",
      f"| Developer-tool QML | {len([c for c in comps if c['category']=='tool'])} | {sum(c['lines'] for c in comps if c['category']=='tool'):,} |", "",
      "## How the layer is put together", "",
      "- **One flat QML module.** Every app `.qml` file is listed in `cmake/PinPointQmlModule.cmake` (`PP_QML_FILES`) and compiled into a single module, `import PinPointStudio`. There are no per-area sub-modules, so a type name is global: two files cannot share a name, and any file can instantiate any other without an import. The directory layout is a convention, not an encapsulation boundary.",
      "- **Singletons** (`pragma Singleton`, and also listed in `PP_QML_SINGLETONS`): " + ", ".join(f"`{c['name']}`" for c in sorted(app, key=lambda c: c['name']) if c['singleton']) + ".",
      "- **Two parallel vocabularies, not a stack.** `Theme` sits under everything. On top of it are (a) the *older* shared components in `components/` (buttons, chips, pills, badges, cards, toasts — the coaching-card look, design system §13) used by Settings, Home, Athlete, Setup, Monitor, Shell and the session toolbar, and (b) the *typeset* system (`typeset/` + the role objects `PpTypeset`/`PpDiagRoles`/`PpChRoles`, design system §15), used by the Charts, Session-diagnostics and Trust panels. Measured: the typeset-built areas use only 2 of the 37 `components/` files (`PpCardShell`, `PpMicro`) — they bring their own text, marks, tabs and plots. The design system calls typeset the target for every other stage panel and, later, whole screens (§15). Feature blocks and screens (`Screen*`, `Main.qml`) sit above both.",
      "- **C++ ↔ QML seam.** Application state reaches QML two ways: " + str(len(inv["context_properties"])) + " *context properties* set in `src/Gui/main.cpp` (globals such as `appSettings`, `shotReplay`, `cameraManager`, `controller`), and " + str(sum(x["qml_element"] for x in cpp_app)) + " `QML_ELEMENT` declarations registered through the module. See [cpp-seam.md](cpp-seam.md). QML files reach for the globals by bare name, not through properties, so a component's real inputs are not visible from its API.",
      "- **Local (inline) components.** Files may declare `component Foo: …` inside themselves; there are " + str(sum(len(c['inline_components']) for c in app)) + " across " + str(sum(1 for c in app if c['inline_components'])) + " files. They are invisible to the module (not instantiable elsewhere) and are where most re-implementation of shared primitives hides — see [duplicates.md](duplicates.md).",
      "",
      "## Areas", "", "| # | Area | Components | QML lines | Doc |", "|---:|---|---:|---:|---|"]
for nf, title, n, ln, key in index_rows:
    ov.append(f"| {nf[:2]} | {title} | {n} | {ln:,} | [{nf}](areas/{nf}) |")
ov += ["", "## Naming conventions (as found)", "",
       "| Prefix / pattern | Meaning | Count |", "|---|---|---:|"]
pat = collections.Counter()
for c in app:
    n = c["name"]
    if n.startswith("PpCh") and n[4:5].isupper(): pat["`PpCh*` — Charts panel blocks"] += 1
    elif n.startswith("Pp"): pat["`Pp*` — shared/panel component (house prefix)"] += 1
    elif n.startswith("Screen"): pat["`Screen*` — a full screen"] += 1
    elif n.startswith("Model"): pat["`Model*` — Diagnostic Model editor"] += 1
    elif n.startswith("Hm"): pat["`Hm*` — Home area"] += 1
    elif n.startswith("Rm"): pat["`Rm*` — Resource Monitor"] += 1
    elif n.startswith("Setup"): pat["`Setup*` — session-setup parts/engine"] += 1
    else: pat["other (unprefixed)"] += 1
for k, v in pat.most_common(): ov.append(f"| {k} | | {v} |")
ov += ["", "## Most reused components (fan-in from app files)", "", "| Component | Area | Used by (files) | Lines |", "|---|---|---:|---:|"]
for c in sorted(app, key=lambda c: -len(c["_users"]))[:25]:
    ov.append(f"| `{c['name']}` | {area_of(c)} | {len(c['_users'])} | {c['lines']} |")
ov += ["", "## Largest components", "", "| Component | Area | Lines | Local components |", "|---|---|---:|---:|"]
for c in sorted(app, key=lambda c: -c["lines"])[:20]:
    ov.append(f"| `{c['name']}` | {area_of(c)} | {c['lines']:,} | {len(c['inline_components'])} |")
unref = [c for c in app if not c["_users"] and not c["singleton"]]
# look for any other mention of the name anywhere (string loads, C++ tests, probes, cmake)
mention = {c["name"]: collections.defaultdict(int) for c in unref}
for base in ("src", "tools", "tests", "cmake"):
    for dp, dn, fn in os.walk(os.path.join(REPO, base)):
        for f in fn:
            if not f.endswith((".qml", ".cpp", ".h", ".js", ".txt", ".cmake", ".mm")): continue
            p = os.path.join(dp, f); rel = os.path.relpath(p, REPO)
            try: txt = open(p, encoding="utf-8", errors="replace").read()
            except Exception: continue
            code = "\n".join(re.sub(r"(?<!:)//.*$", "", l) for l in txt.splitlines() if not l.lstrip().startswith(("//", "*", "/*", "#")))
            for n in mention:
                if rel.endswith(n + ".qml"): continue
                if re.search(r"\b%s\b" % n, code) or (rel == "cmake/PinPointQmlModule.cmake" and re.search(r"\b%s\.qml\b" % n, txt)):
                    kind = "cmake list" if rel == "cmake/PinPointQmlModule.cmake" else ("test/probe" if "/tests/" in rel or rel.startswith(("tests/", "tools/")) else "app source")
                    mention[n][kind] += 1
ov += ["", "## Files nothing in the app instantiates", "",
       f"{len(unref)} app files are never instantiated by another app QML file. Each was searched for under `src/`, `tools/`, `tests/` and `cmake/` to separate *dead* from *staged/test-only*:", "",
       "| File | Lines | Other mentions found |", "|---|---:|---|"]
for c in unref:
    m = mention[c["name"]]
    only = [k for k in m if k != "cmake list"]
    desc = ", ".join(f"{k}: {v}" for k, v in sorted(m.items())) or "none"
    verdict = "only in the cmake file list" if not only else ("reached only from tests/probes" if set(only) <= {"test/probe"} else "mentioned in app source — check")
    ov.append(f"| `{c['path'].replace('src/Gui/','')}` | {c['lines']} | {verdict} ({desc}) |")
ov += ["", "## Theme bypass", "",
       f"{len([c for c in app if c['n_theme_refs']==0 and c['code_lines']>30 and c['name']!='Theme'])} of the larger components (>30 code lines) have no direct `Theme.*` reference — they draw through a `roles`/typeset object (Charts, Diagnostics, Trust) or are logic-only. Components with hard-coded colour literals (`#rrggbb`, `Qt.rgba`) instead of `Theme.*` tokens:", "",
       "| Component | Hard-coded colours | Theme refs |", "|---|---:|---:|"]
for c in sorted([c for c in app if c["n_hard_colors"] and c["name"] != "Theme"], key=lambda c: -c["n_hard_colors"])[:15]:
    ov.append(f"| `{c['name']}` | {c['n_hard_colors']} | {c['n_theme_refs']} |")
ov.append("")
open(os.path.join(D, "00-overview.md"), "w").write("\n".join(ov))

# ---------- C++ seam ---------------------------------------------------------------
cs_ = ["# C++ ↔ QML seam", "",
       "How GUI-layer C++ reaches QML. Two mechanisms: **context properties** (global names, set in `src/Gui/main.cpp`) and **`QML_ELEMENT` types** (declared in the module via `PP_QML_SOURCES`).", "",
       "## Context properties (QML globals)", "",
       "A QML file refers to these by bare name, so they are *implicit inputs* of every component that mentions them — not visible in the component's property list. `Used in` counts app QML files that mention the name.", "",
       "| Global | C++ type | Set at | Used in |", "|---|---|---|---:|"]
for cp in sorted(inv["context_properties"], key=lambda x: -len(x["used_in"])):
    t = cp.get("type") or ""
    if "::" in t: t = "(installed by debug hook)"
    if cp["name"] == "swingExporter": t = "swing ZIP exporter"
    cs_.append(f"| `{cp['name']}` | {('`'+t+'`') if t else ''} | `{cp['file']}:{cp['line']}` | {len(cp['used_in'])} |")
cs_ += ["", "Heaviest consumers of globals (distinct globals mentioned per file):", "", "| Component | Globals used |", "|---|---|"]
per = collections.defaultdict(set)
for cp in inv["context_properties"]:
    for u in cp["used_in"]: per[u].add(cp["name"])
for u, g in sorted(per.items(), key=lambda kv: -len(kv[1]))[:20]:
    cs_.append(f"| `{os.path.basename(u)[:-4]}` ({by_path[u]['lines']} lines) | {len(g)}: {', '.join(sorted(g))} |")
cs_ += ["", "## C++ classes in `src/Gui`", "",
        "Classes found by scanning headers for `class X : public Base`. `Prop` = `Q_PROPERTY`, `Inv` = `Q_INVOKABLE`, counted per *file* (a file with several classes shares the counts). `QML` marks `QML_ELEMENT`/`QML_NAMED_ELEMENT`.", "",
        "| Area | Class | Base | Header | Prop | Inv | QML |", "|---|---|---|---|---:|---:|:--:|"]
for x in sorted([x for x in cpp_app if x["path"].endswith(".h") and x["classes"]], key=lambda x: x["path"]):
    area = x["path"].split("/")[2]
    for k in x["classes"]:
        cs_.append(f"| {area} | `{k['name']}` | `{k['base']}` | `{x['path'].replace('src/Gui/','')}` | {x['q_properties']} | {x['q_invokables']} | {'✓' if x['qml_element'] else ''} |")
cs_ += ["", "## C++ volume by area", "", "| Area | Files | Lines |", "|---|---:|---:|"]
cnt = collections.defaultdict(lambda: [0, 0])
for x in cpp_app:
    a = x["path"].split("/")[2]; cnt[a][0] += 1; cnt[a][1] += x["lines"]
for a, (n, l) in sorted(cnt.items(), key=lambda kv: -kv[1][1]): cs_.append(f"| {a} | {n} | {l:,} |")
open(os.path.join(D, "cpp-seam.md"), "w").write("\n".join(cs_) + "\n")
print("areas:", len(index_rows), "components:", len(app))
