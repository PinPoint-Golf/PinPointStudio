# Typeset harness

Renders QML you write over a real stage panel in the real app, fed the real data, in the real stage
arrangement, with nothing built. The app's `--probe-qml` hook (dev builds only) loads a probe script,
and the probe loads your mock. It is how Session diagnostics was redesigned on 11 October 2026, and
how the other stage panels (Charts, Wrist motion, Table, Launch monitor, Markup, the camera's chrome)
are meant to be redesigned.

Read first: the method, `docs/design/stage_typeset_playbook.md`, and the spec, section 15 of
`docs/design/pinpoint_qml_design_system.md`. This README covers only the tools.

## The tools

| File | Does |
|---|---|
| `render.sh` + `render_overlay.qml` | One render run. Loads the fixture session, sets the stage arrangement, finds the target panel, lays your mock exactly over it (the real panel goes transparent underneath) and feeds it the panel's data. Steps themes × views × demo states and grabs `_window.png` and `_panel.png`. With overlay `none` it renders the real panel. Also renders the cast and A4 print pages, and dumps the panel's data as JSON. |
| `render_stage.sh` + `render_stage.qml` | The stage audit: no overlay. For each theme, layout and panel it renders the real panel and grabs the window and the panel's stage card. Use it for step 1's "before" renders. |
| `try.sh` + `try_overlay.qml` | The live try-out: the real app, windowed, with your mock laid over every instance of the panel, re-attaching whenever the stage rebuilds it. Mark uses the app normally. `--selftest` drives it offscreen and must pass before Mark looks. |
| `assemble_review.py` | Builds the review PDF from the renders: side-by-side comparisons, every layout at true scale, demo states, print pages, cover and notes. |
| `guard.sh` | Wraps every app run: at most two app instances machine-wide, and a memory kill. |
| `lib.sh` | Shared settings, slot creation and panel profiles. Sourced by the scripts. |
| `panels/<name>.env` | Per-panel profiles: the target, the data fed to the mock, the tab and view properties, the views, the default detail. |
| `screens/` | Offscreen screen configurations, and `empty.qml`, the null overlay. |
| `briefs/` | The four briefs the design agents were given on 11 October, as worked examples. |
| `examples/` | The diagnostics redesign as a worked example: inventory, live-state wrapper, port contract, test rewrite notes. |

## Environment

| Variable | Default | What |
|---|---|---|
| `PPS_APP` | `<repo>/build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio` | The app. Must be a dev build: a shipping build ignores `--probe-qml`. Never rebuilt by the harness. |
| `TYPESET_SCRATCH` | `/mnt/swingdata/scratch/typeset-harness` | Root of everything a run writes. Run trees go on the share, never in the repo or `/tmp`. |
| `TYPESET_OUT` | `$TYPESET_SCRATCH/out` | The PNGs, plus `logs/` (the full app log of each run) and `<tag>_data/` (dumps). |
| `TYPESET_SLOTS` | `$TYPESET_SCRATCH/slots` | One directory per slot. |
| `TYPESET_SESSION` | `/mnt/swingdata/Mark-Liversedge/2026-07-04_Mark-Liversedge_Wrist_01` | The fixture session: 7 iron, 15 shots, reviewed; shot 8 is the one picked. |
| `TYPESET_SETTINGS` | `~/.config/PinPointStudio/PinPointStudio.ini` | The settings each new slot copies. |
| `TYPESET_LOCKDIR` | `/tmp/pps-typeset-locks` | The guard's lock slots. Machine-wide on purpose. |
| `GUARD_GB` | `6` (`8` for `try.sh`) | The guard's memory limit. |

**Slots.** A slot is a private copy of the fixture session and of the settings
(`$TYPESET_SLOTS/<name>/{session,cfg}`, used as `XDG_CONFIG_HOME`). It is made on first use, by rsync,
without the session's `diagnostics.json.*` backups (about 310 MB, a minute or so on the share).
Parallel agents each use their own slot, so they never fight over settings, and Mark's own settings and
library are never touched. Defaults: `render.sh` uses `a`, `render_stage.sh` uses `base`, and `try.sh`
uses `try`. A slot's settings drift as probes switch layouts; delete the slot to start fresh. A slot
copies Mark's settings as they are at that moment, including his stage arrangement, collapsed carousel
and theme.

## render.sh

```
render.sh [options] <overlay.qml | none> <tag> <layout>
```

- **layout:** `full`, `stage`, `stagecam`, `half`, `third`, `cast`, `print-portrait` or `print-landscape`.
  - The probe sets the real arrangement first, with the timeline and carousel on, so the window grab
    shows the panel with its real neighbours.
  - `full` and `cast` use tabs, on the target's tab.
  - `half` adds one neighbour (Charts, or Table for Charts); `third` adds two.
  - `stage` uses the stage arrangement with two neighbours from Charts, Table and Markup.
  - `stagecam` puts the panel in the side column beside a dominant camera.
  - `cast` hosts the mock as a 1040 × 760 second-screen sheet and sets its `surface` to `"cast"`.
  - The print layouts render A4 pages at 2× on the dpr-2 screen (about 192 dpi).
  - Cast and print need an overlay.
- **--panel:** a profile in `panels/` (default `diagnostics`), or a raw target: an `objectName`, or
  `key:<stageKey>` for the panel loaded in that stage card (e.g. `key:charts`).
- **--slot** (default `a`), **--themes** (default `7,12`: 6 Vector light, 7 Vector dark, 12 Folio light,
  13 Folio dark; print takes 6 and 12), **--views** (default: the profile's), **--detail** (the
  condition the `detail` view opens), **--mode** `analyse|replay`, **--states** (sets the mock's
  `demoState` before each grab), **--screen** `normal|tall|mark|<file.json>`, **--ordinal** (the shot,
  default 8), **--timeout** (seconds, default 500).
- **--feed**, **--tab-prop**, **--target:** override the profile.
- **--dump:** writes each fed property (and, for a panel with `_openDetail`, each `--detail` id, comma
  separated) as JSON to `$TYPESET_OUT/<tag>_data/`, then quits. Read the data before designing (step 1).

**Output:**
- screen: `<tag>_<layout>_<aesthetic>_<light|dark>_<view>[_<state>]_{window,panel}.png`;
- print: `<tag>_print-<orient>_<aesthetic>_<light|dark>_p<N>.png`.

The file name carries no mode and no detail id: put them in the tag (`f3_replay`, `f3_analyse_ee`).

**Time:** 40–90 s a run. The guard allows two apps, so two lanes can run at once.

Examples:
```
H=tools/typeset-harness
$H/render.sh --themes 12 --views shot none before half                      # the real panel
$H/render.sh my/Analyse.qml r1_analyse half                                 # a mock, 7 and 12, every view
$H/render.sh --states hoverShot12,question-clean my/Analyse.qml r1_analyse half
$H/render.sh --mode replay my/Replay.qml r1_replay cast
$H/render.sh --themes 6,12 my/Print.qml r1_print print-portrait
$H/render.sh --screen tall --views detail --themes 12 my/Analyse.qml r1_analyse_tall half
$H/render.sh --screen mark my/Replay.qml r1_big half                       # Mark's 2560 × 1560 window
$H/render.sh --panel charts --dump none charts_data half                   # what Charts has to show
$H/render.sh --panel charts --themes 7,12 none charts_before third
```

**Screens** (`screens/`): `screen.json` 1680 × 1050; `screen_tall.json` 1680 × 2600 (whole pages);
`screen_print.json` 1680 × 1200 at dpr 2; `screen_mark.json` 2560 × 1560, Mark's window.

## The mock's contract

The mock is one QML file. It may `import PinPointStudio` and sibling files (`import "../lib"`). Its root
declares what it reads, and only reads it. The probe sets only what the root declares:

```qml
property string view            // the current view: a tab value, or "detail"
property var <fed properties>   // the profile's FEED, e.g. readout, cards, source, header, detail
property var body               // the live panel: call it, so clicks run the real paths
property string medium: "screen"   // "screen" | "print"
property string printOrientation: ""
property int printPage: 0
readonly property int printPageCount: 1
property string demoState: ""   // an interaction state to show, for a render
property string surface: "panel"   // "panel" | "cast"
property string register        // optional: "Replay" | "Analyse"; the try-out self-test checks it
```

Rules that keep a mock safe:
- Compute sizes from data, never from your own laid-out size. A loop there is what reached 97 GB.
- Bind to the body's size; never copy it once. The probes bind the overlay's geometry for you.
- Never use `roles` or `brush` as ids.

## Panel profiles

`panels/<name>.env` sets:
- `TARGET`: an objectName, or `key:<stageKey>`;
- `PANEL_KEY`: the stage key the layouts switch on;
- `FEED`: the body properties fed to the mock and dumped;
- `TAB_PROP`: the body property written to change view;
- `VIEW_PROP`: the body property the try-out binds the mock's `view` to;
- `VIEWS` and `DETAIL`.

| Panel | Target | State |
|---|---|---|
| `diagnostics` | `sdBody` (the only stage body with an objectName) | Proven: three rounds and a live try-out. |
| `charts`, `table`, `wristMotion`, `launchMonitor`, `camera`, `markup`, `swing3d` | `key:<stageKey>` | Starting points. `charts` has been rendered and dumped through `key:charts`; the others are untested. |

Only Session diagnostics' body has an `objectName`. The others are reached through their stage card
(`key:`), so no source change was needed. A panel whose data lives on an inner id (Wrist motion's
`dx`, Table's `src`) has to expose it on its root before a mock can read it. That is a source change
and costs a build: batch such changes into one build. A body that is to be redesigned this way should
expose its data, its current view (Session diagnostics: `tab`, `_tab`, `view`) and its actions
(`_openDetail(id)`, `_closeDetail()`, `detailOpen`).

## render_stage.sh

```
render_stage.sh [--slot base] [--screen normal|tall|mark] <tag> <analyse|replay> <themes> <layouts> <panels> [settle_ms=3000] [timeout_s=900]
render_stage.sh audit analyse 12,7 full,half,third camera,swing3d,launchMonitor,wristMotion,charts,table,markup
```

Output: `<tag>_<mode>_<layout>_<aesthetic>_<light|dark>_<panel>_{window,panel}.png`. The panel grab is
the stage card, with its title. For `assemble_review.py` that is tag `<tag>_<mode>`, view `<panel>`.

## try.sh

```
try.sh [--selftest] [--slot try] <panel profile | target> <overlay.qml>
try.sh --selftest diagnostics path/to/Panel.qml       # an agent runs this first, and it must pass
try.sh diagnostics path/to/Panel.qml                  # then Mark: windowed, quit the app to finish
```

- Mark gets a one-line wrapper in `build/run-me/try-<panel>-typeset.sh` that calls `try.sh` with
  absolute paths.
- The mock goes over every instance of the panel. Its fed properties and its `view` are **bound** to
  the body, so it follows every tab, mode and detail.
- The overlay re-attaches every second after a rebuild.
- In the tabs arrangement the probe selects the panel's tab.
- The self-test runs offscreen and muted. It drives every tab in Replay, switches to Analyse (which
  rebuilds the stage), drives every tab again, then opens a detail and goes Back. It prints PASS or
  FAIL for each step, then `RESULT`, and exits 0 only on a pass.
- The try-out runs under the guard with an 8 GB limit.

## assemble_review.py

```
/opt/homebrew/bin/python3 tools/typeset-harness/assemble_review.py --src $TYPESET_OUT --dest <dir> \
    --title "Charts — round 1" --tags r1_a,r1_b,r1_c [--labels A,B,C] [--views …] [--view-labels …] \
    [--layouts …] [--themes folio_light,vector_dark] [--states …] [--compare-layout half] \
    [--states-layout half] [--print r1_print] [--cover COVER.md] [--notes a.md,b.md] [--name review]
```

It writes `<dest>/<name>.pdf`, `<dest>/pages/NN.png`, and the print pages as A4 PDFs in
`<dest>/print/`. Views, layouts, themes and states default to whatever is found for the tags. A page
whose renders are all missing is left out; a single missing cell says "missing". It needs Pillow only
and uses the repo's Inter fonts. Run it with `/opt/homebrew/bin/python3`, without `-I`, because Pillow
is a site package.

## The memory guard, and why

On 11 October a runaway QML mock reached **97 GB** and filled swap: a binding or layout loop that
creates objects, for example a Repeater whose model depends on its own laid-out height.

`guard.sh` runs every app the harness starts:
- It kills the app above `GUARD_GB` (6 GB for renders, 8 GB for the try-out) and prints `GUARD: KILLED …`.
- It allows at most two app instances machine-wide, across all agents. A third waits for a slot.

Never run the app for a render without it. If a run prints a QML error in its first lines, kill it
(`pkill -f "render_overlay.qml"`) rather than wait out the timeout.

## Traps

- **A theme switch rebuilds panels in the split and stage arrangements, and a mode switch rebuilds the
  stage.** The body your overlay sat on is destroyed. The probes re-find the target after every switch
  and re-attach. In panel code, never hold a body across a switch: state that must survive belongs on
  the controller.
- **The print grab must be 2×.** Print pages are A4 at 96 dpi logical (794 × 1123), grabbed at twice
  the size on the dpr-2 print screen. Without that, type in the review is blurred and the pages can't
  be judged.
- **Offscreen draws no video and no 3-D.** In a render, Camera and 3-D swing show only their chrome.
  Judge video and 3-D in the live try-out.
- **A try-out must bind the view as well as the data.** Mark's first try-out showed only This shot,
  because the wrapper fed the data but never told the mock which tab was selected. A mode switch also
  rebuilt the panel, and the wrapper failed every second. Which body property carries the view is
  panel-specific: set it as the profile's `VIEW_PROP`.
- **Verify by driving every tab and mode.** One clean attach proves nothing about the second tab, the
  other mode, or opening and closing a detail. Run `try.sh --selftest`, and reproduce with
  `--probe-qml` before claiming a fix.
- **A `CurveRenderer` Shape ignores a scrolled ancestor's clip.** Round marks drawn with
  `Shape.CurveRenderer` floated outside a scrolled panel in the stage's side column. Draw them as
  Rectangles snapped to whole pixels with an even diameter (`src/Gui/typeset/PpMark.qml`). Shapes also
  ignore their clip in a paged print window: set an off-page figure's opacity to 0.
- **Height is a resource.** Render at Mark's window (`--screen mark`) as well as the reference sizes.
  Render the whole detail page with `--screen tall`.
- **A real layout can straddle a class boundary.** Folio's half is just under 52 em, so check both
  themes at half.
- **Silence.** Every probe mutes the app's ting players. No test or probe may play sound.
