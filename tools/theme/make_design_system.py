#!/usr/bin/env python3
"""PinPoint design system: docs/design/system/tokens.json, generated from src/Gui/theme/Theme.qml.

Theme.qml is the source of truth. This reads its theme tables as TEXT (never runs them) and writes the
token file the design docs and Claude Code read. Run after any Theme.qml change; --check for CI.
"""
import argparse, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ap = argparse.ArgumentParser(description="Regenerate docs/design/system/tokens.json from src/Gui/theme/Theme.qml "
                             "(read as text, never executed). --check exits 1 when the committed file is stale.")
ap.add_argument("--check", action="store_true")
args = ap.parse_args()
SRC = os.path.join(ROOT, "src", "Gui", "theme", "Theme.qml")
OUT = os.path.join(ROOT, "docs", "design", "system", "tokens.json")
qml = open(SRC, encoding="utf-8").read()


def geo(name):
    """A geometry token's design-px value(s) from Theme.qml: (compact, default, spacious) or a single int."""
    m = re.search(r"property\s+(?:int|real)\s+%s:\s*_v\(\"%s\",\s*_dens\((\d+),\s*(\d+),\s*(\d+)\)\)" % (name, name), qml)
    if m: return tuple(int(x) for x in m.groups())
    for pat in (r"property\s+(?:int|real)\s+%s:\s*_v\(\"%s\",\s*(\d+)\)" % (name, name), r"property\s+(?:int|real)\s+%s:\s*(\d+)\b" % name,
                r"property\s+(?:int|real)\s+%s:\s*sp\((\d+)\)" % name, r"property\s+(?:int|real)\s+%s:\s*([\d.]+)" % name):
        m = re.search(pat, qml)
        if m: return int(float(m.group(1)))
    raise SystemExit("make_design_system: cannot find %s in Theme.qml" % name)


def real(name):
    """A real token's value from Theme.qml, as written there ("1.5"): `_v("name", x)` or a plain `x`."""
    for pat in (r"property\s+real\s+%s:\s*_v\(\"%s\",\s*([\d.]+)\)" % (name, name), r"property\s+real\s+%s:\s*([\d.]+)\s*$" % name):
        m = re.search(pat, qml, re.M)
        if m: return m.group(1)
    raise SystemExit("make_design_system: cannot find %s in Theme.qml" % name)


# Theme.variabilityHysteresis is the QML copy of the grader's kHysteresis (spec §8.9, §10): the two must
# never disagree, so the token file refuses to generate (and --check fails) when they do.
_vc = open(os.path.join(ROOT, "src", "Metrics", "variability", "variability_constants.h"), encoding="utf-8").read()
m = re.search(r"constexpr\s+double\s+kHysteresis\s*=\s*([\d.]+)", _vc)
if not m: raise SystemExit("make_design_system: kHysteresis not found in src/Metrics/variability/variability_constants.h")
if float(m.group(1)) != float(real("variabilityHysteresis")):
    raise SystemExit("make_design_system: Theme.variabilityHysteresis (%s) != kHysteresis (%s) in variability_constants.h"
                     % (real("variabilityHysteresis"), m.group(1)))


m = re.search(r'densityScale:\s*density === "compact" \? ([\d.]+) : density === "spacious" \? ([\d.]+) : ([\d.]+)', qml)
if not m: raise SystemExit("make_design_system: Theme.densityScale not found — apply the density change first")
DENS = {"compact": m.group(1), "spacious": m.group(2), "default": m.group(3)}

AES = ["instrument", "editorial", "studio", "vector"]
LABEL = {"instrument": "Instrument", "editorial": "Editorial", "studio": "Studio", "vector": "Vector"}


def block(text, start_pat):
    """Return the text of the {...} block following start_pat."""
    m = re.search(start_pat, text)
    assert m, start_pat
    i = text.index("{", m.end() - 1)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise ValueError(start_pat)


def qcolor(v):
    """QML #AARRGGBB -> CSS #rrggbbaa; #RRGGBB lowercased."""
    v = v.lower()
    if len(v) == 9:
        return "#" + v[3:] + v[1:3]
    return v


themes_txt = block(qml, r"readonly property var _themes: \(\{")
theme = {}
for a in AES:
    t = block(themes_txt, r"\n\s+%s: \{" % a)
    shared = t.split("light:")[0]
    modes = {}
    for mode in ("light", "dark"):
        b = block(t, r"\b%s:\s*\{" % mode)
        cols = {k: qcolor(v) for k, v in re.findall(r'(\w+): "(#[0-9A-Fa-f]+)"', b)}
        cs = re.search(r"chartSeries: \[([^\]]+)\]", b).group(1)
        series = [qcolor(x) for x in re.findall(r'"(#[0-9A-Fa-f]+)"', cs)]
        modes[mode] = (cols, series)
    num = lambda k: int(re.search(r"\b%s: (\d+)" % k, shared).group(1))
    theme[a] = dict(modes=modes, radius=num("radius"), radiusLg=num("radiusLg"),
                    railWidth=num("railWidth"), displayBase=num("displayBase"),
                    fontBody=re.search(r"fontBody: (\"[^\"]+\"|\w+)", shared).group(1).strip('"'),
                    fontData=re.search(r"fontData: \"([^\"]+)\"", shared).group(1),
                    fontDisplay=re.search(r"fontDisplay: (\"[^\"]+\"|\w+)", shared).group(1).strip('"'),
                    bodyW=re.search(r"fontBodyWeight: Font\.(\w+)", shared).group(1),
                    dispW=re.search(r"fontDisplayWeight: Font\.(\w+)", shared).group(1),
                    dispItalic=re.search(r"fontDisplayItalic: (\w+)", shared).group(1) == "true",
                    railTone=(re.search(r'railTone: "(\w+)"', shared) or [None, "bg"])[1],
                    toolbarTone=(re.search(r'toolbarTone: "(\w+)"', shared) or [None, "surface"])[1],
                    railActiveBar="railActiveBar: true" in shared)

mp_txt = block(qml, r"readonly property var _metricPalettes: \(\{")
metric = {}
for a in AES:
    t = block(mp_txt, r'"%s": \{' % a)
    metric[a] = {}
    for mode in ("light", "dark"):
        b = block(t, r"\b%s: \{" % mode)
        metric[a][mode] = {k: qcolor(v) for k, v in re.findall(r'(\w+): "(#[0-9A-Fa-f]+)"', b)}
METRIC_NAMES = list(metric["studio"]["dark"].keys())

THEMES = [f"{a}-{m}" for a in AES for m in ("light", "dark")]

# Theme.variabilityWashAlpha: an optional key whose default differs by mode (`dark ? D : L`); a theme
# or its mode block may set its own. Read per theme so tokens.json shows what each one draws.
m = re.search(r'property\s+real\s+variabilityWashAlpha:\s*_v\("variabilityWashAlpha",\s*dark \? ([\d.]+) : ([\d.]+)\)', qml)
if not m: raise SystemExit("make_design_system: cannot find variabilityWashAlpha in Theme.qml")
WASH_DEFAULT = {"dark": m.group(1), "light": m.group(2)}
def wash_alpha(a, mode):
    t = block(themes_txt, r"\n\s+%s: \{" % a)
    mb = re.search(r"\bvariabilityWashAlpha:\s*([\d.]+)", block(t, r"\b%s:\s*\{" % mode))
    sh = re.search(r"\bvariabilityWashAlpha:\s*([\d.]+)", t.split("light:")[0])
    return (mb or sh).group(1) if (mb or sh) else WASH_DEFAULT[mode]

def per(fn):
    return {f"{a}-{m}": fn(a, m) for a in AES for m in ("light", "dark")}

USAGE = {
    "colorBg": "Page ground — the window behind cards and panels.",
    "colorBg2": "Second ground: the rail (Instrument), toolbar strips, a hovered quiet control (choiceFill 'clear').",
    "colorBg3": "Third ground: wells, pressed states, the deepest recess.",
    "colorSurface": "Card and panel surface; hollow chart markers fill with it. The ground text tokens are tuned for.",
    "colorBorder": "Hairline at a card's edge (1 px, translucent ink — reads as 0.5 px).",
    "colorBorderMid": "Mid hairline: dividers in controls, unlit meter segments, Appearance tile borders.",
    "colorBorderStrong": "Strong hairline: an unselected control's border (choiceBorder rest).",
    "colorText": "Primary ink on colorBg, colorBg2 and colorSurface.",
    "colorText2": "Secondary ink: supporting copy, values beside a label, on colorSurface and colorBg.",
    "colorText3": "Muted ink for captions, micro labels and 'no verdict' dots only. Kept exact from the source: about 2.1–2.8:1 on colorSurface in every theme but Instrument Dark (7.1:1), so never body copy or anything a reader must read.",
    "colorAccent": "The one accent: links, selected borders, focus, the current tab's underline, primary buttons.",
    "colorAccentLight": "Accent wash: a selected chip or tile's fill.",
    "colorAccentMid": "Accent mid: a hovered control's border, an accent bar at rest.",
    "gradientWarm": "Brand sweep start (pelvis amber) — gradient display titles and accent bars.",
    "gradientWarmLit": "Brand sweep middle stop at 55%.",
    "gradientCool": "Brand sweep end (club cyan).",
    "colorGood": "Good / connected / in range. Pair with a word or a tick — never colour alone.",
    "colorGoodLight": "Wash behind good text or a good chip.",
    "colorWarn": "Caution, unexpected, a habit fault (the coral — never the alarm red). Studio Light is 3.9:1 on surface: there, pair it with a mark or set it 24px+.",
    "colorWarnLight": "Wash behind warn text.",
    "colorError": "Critical failure — distinctly red, darker than warn.",
    "colorErrorLight": "Wash behind error text.",
    "colorAttention": "Call to action ('do this next'), the watch band — a confident amber frame. Vector Light is 2.8:1 and Studio Light 4.1:1 on surface: use it as a frame or fill with colorText words there, not as small text.",
    "colorAttentionLight": "Wash behind attention text or a frame needing action.",
    "colorBandGreen": "Expected-corridor fill on a trajectory strip (~0x24 alpha).",
    "colorBandAmber": "Watch-corridor fill on a trajectory strip.",
}

color_tokens = []
def add(name, value, usage):
    color_tokens.append({"name": name, "value": value, "usage": usage})

for k in USAGE:
    add(k, per(lambda a, m, k=k: theme[a]["modes"][m][0][k]), USAGE[k])

# Ink on accent fills (computed in Theme.qml from other tokens)
add("colorOnAccent", per(lambda a, m: "{colorBg}" if m == "dark" else "{colorSurface}"),
    "Ink on a solid accent or status fill: checks, toolbar, filters, markup. Surface in light, page ground in dark.")
add("colorOnAccentBright", per(lambda a, m: "{colorBg}" if m == "dark" else "#ffffff"),
    "Ink on a filled button, a picked segment, the quality pill: pure white in light, page ground in dark.")
add("colorKnob", per(lambda a, m: "{colorText}" if m == "dark" else "{colorSurface}"),
    "A switch's knob: surface in light, the text colour in dark.")

# Semantic aliases
for n, tgt, u in [("colorRagGood", "colorGood", "RAG reading: good. Always with a shape and a label."),
                  ("colorRagWatch", "colorAttention", "RAG reading: watch."),
                  ("colorRagFault", "colorError", "RAG reading: fault."),
                  ("colorRagNone", "colorText3", "RAG reading: no data.")]:
    add(n, "{%s}" % tgt, u)

# The heat of a table cell: colorAttention at three strengths (computed in Theme.qml)
def heat(alpha):
    return per(lambda a, m: theme[a]["modes"][m][0]["colorAttention"].lower() + "%02x" % round(alpha * 255))
add("colorHeatSome", heat(0.15), "Table cell heat: 'some' (PpChMatrix). colorAttention at 0.15. Says how much, never good or bad; the word stays in the cell.")
add("colorHeatLoose", heat(0.40), "Table cell heat: 'loose' (PpChMatrix). colorAttention at 0.40.")
add("colorHeatAfter", heat(0.12), "Table cell heat: a span after the ball, drawn lighter (PpChMatrix). colorAttention at 0.12.")

def hairline(a, m):
    v = theme[a]["modes"][m][0]["colorBorder"]
    alpha = int(v[7:9], 16) * 0.6
    return v[:7] + "%02x" % round(alpha)
add("colorHairline", per(hairline), "Hairline between table rows: colorBorder at 0.6 of its alpha (PpDivider).")
add("colorMeterUnlit", "{colorBorderMid}", "An unlit meter segment; the empty ground of any count or share mark.")
add("colorRail", per(lambda a, m: "{colorBg2}" if theme[a]["railTone"] == "bg2" else "{colorBg}"),
    "The nav rail's ground (Instrument: bg2; others: bg).")
add("colorToolbar", per(lambda a, m: "{colorBg2}" if theme[a]["toolbarTone"] == "bg2" else "{colorSurface}"),
    "The session toolbar's ground (Instrument: bg2; others: surface).")

for i in range(6):
    add(f"chartSeries{i+1}", per(lambda a, m, i=i: theme[a]["modes"][m][1][i]),
        f"Categorical chart hue #{i+1} for series that are NOT catalogue metrics; index with wrap (chartSeriesColor).")

IMU = {"A": ("#d32f2f", "#ff3b30", "red"), "B": ("#e0a400", "#ffd60a", "yellow"),
       "C": ("#2e9e4f", "#34c759", "green"), "D": ("#0a6cff", "#3399ff", "blue")}
for k, (l, d) in ((k, v[:2]) for k, v in IMU.items()):
    add(f"colorImu{k}", per(lambda a, m, l=l, d=d: d if m == "dark" else l),
        f"IMU sensor {k} identity ({IMU[k][2]}) — fixed hue in every aesthetic; 3D markers and IMU UI.")

for n, v, u in [("poseBone", "#74d0e6", "Pose overlay: bones, joint rings, head, dots. FIXED in every theme — an instrument readout over video."),
                ("poseInk", "#121a23c8", "Pose overlay: joint-ring fill (dark ink, ~200 alpha)."),
                ("poseSpineTop", "#d6f2ff", "Pose overlay: spine neck end, end tick, diamonds."),
                ("poseSpineBottom", "#4aa6c4", "Pose overlay: spine pelvis end."),
                ("poseSpineTick2", "#6cc3dc", "Pose overlay: pelvis-end spine tick."),
                ("colorScrim", "#121a2399", "Dark backing behind text laid over video (poseInk at 0.6 alpha).")]:
    add(n, v, u)
add("colorOnScrim", per(lambda a, m: "{colorText}" if m == "dark" else "{colorBg}"),
    "Words and marks ON the picture over colorScrim: near-white in either mode.")

METRIC_FAMILY = {"crimson": "pelvis", "cornflower": "clubhead speed", "mint": "chest"}
for n in METRIC_NAMES:
    tag = f" (e.g. {METRIC_FAMILY[n]})" if n in METRIC_FAMILY else ""
    add("metric" + n.capitalize(), per(lambda a, m, n=n: metric[a][m][n]),
        f"Metric identity colour '{n}'{tag}: one metric draws in its named colour wherever it is plotted; clears 3:1 on colorSurface.")

# ── Type ────────────────────────────────────────────────────────────────
families = {
    "instrumentserif": "Georgia, Gelasio, \"Times New Roman\", serif",
    "dmmono": "\"DM Mono\", ui-monospace, monospace",
    "instrumentsans": "\"Instrument Sans\", system-ui, sans-serif",
    "jetbrainsmono": "\"JetBrains Mono\", ui-monospace, monospace",
    "sourceserif": "\"Source Serif 4\", Georgia, serif",
    "hankengrotesk": "\"Hanken Grotesk\", system-ui, sans-serif",
    "geistmono": "\"Geist Mono\", ui-monospace, monospace",
    "spacegrotesk": "\"Space Grotesk\", system-ui, sans-serif",
    "spacemono": "\"Space Mono\", ui-monospace, monospace",
}
FAMKEY = {"serifFamily": "instrumentserif", "DM Mono": "dmmono", "Instrument Sans": "instrumentsans",
          "JetBrains Mono": "jetbrainsmono", "Source Serif 4": "sourceserif", "Hanken Grotesk": "hankengrotesk",
          "Geist Mono": "geistmono", "Space Grotesk": "spacegrotesk", "Space Mono": "spacemono"}
fonts = [
    {"family": "Gelasio", "file": "fonts/Gelasio-Regular.ttf", "weight": "400", "style": "normal"},
    {"family": "Gelasio", "file": "fonts/Gelasio-Italic.ttf", "weight": "400", "style": "italic"},
    {"family": "Gelasio", "file": "fonts/Gelasio-Bold.ttf", "weight": "700", "style": "normal"},
    {"family": "Gelasio", "file": "fonts/Gelasio-BoldItalic.ttf", "weight": "700", "style": "italic"},
    {"family": "DM Mono", "file": "fonts/DMMono-Regular.ttf", "weight": "400", "style": "normal"},
    {"family": "DM Mono", "file": "fonts/DMMono-Medium.ttf", "weight": "500", "style": "normal"},
    {"family": "Instrument Sans", "file": "fonts/InstrumentSans-Variable.ttf", "weight": "400 700", "style": "normal"},
    {"family": "JetBrains Mono", "file": "fonts/JetBrainsMono-Variable.ttf", "weight": "100 800", "style": "normal"},
    {"family": "Source Serif 4", "file": "fonts/SourceSerif4-Variable.woff2", "weight": "200 900", "style": "normal"},
    {"family": "Source Serif 4", "file": "fonts/SourceSerif4-Italic-Variable.ttf", "weight": "200 900", "style": "italic"},
    {"family": "Hanken Grotesk", "file": "fonts/HankenGrotesk-Variable.ttf", "weight": "100 900", "style": "normal"},
    {"family": "Hanken Grotesk", "file": "fonts/HankenGrotesk-Italic-Variable.ttf", "weight": "100 900", "style": "italic"},
    {"family": "Geist Mono", "file": "fonts/GeistMono-Variable.ttf", "weight": "100 900", "style": "normal"},
    {"family": "Space Grotesk", "file": "fonts/SpaceGrotesk-Variable.ttf", "weight": "300 700", "style": "normal"},
    {"family": "Space Mono", "file": "fonts/SpaceMono-Regular.ttf", "weight": "400", "style": "normal"},
    {"family": "Space Mono", "file": "fonts/SpaceMono-Bold.ttf", "weight": "700", "style": "normal"},
]
W = {"Light": 300, "Normal": 400, "Medium": 500, "DemiBold": 600, "Bold": 700}
groups = []
for a in AES:
    t = theme[a]
    body, data, disp = FAMKEY[t["fontBody"]], FAMKEY[t["fontData"]], FAMKEY[t["fontDisplay"]]
    bw, dw = W[t["bodyW"]], W[t["dispW"]]
    ds = t["displayBase"]
    def st(name, size, fam, weight, usage, ls=None, italic=False, sample=None):
        s = {"name": f"{a}-{name}", "fontSize": f"{size}px", "family": fam, "fontWeight": weight, "usage": usage}
        if ls is not None: s["letterSpacing"] = f"{ls}px"
        if italic: s["fontStyle"] = "italic"
        if sample: s["sample"] = sample
        return s
    it = t["dispItalic"]
    groups.append({"name": LABEL[a], "family": body, "styles": [
        st("hero", ds * 2, disp, dw, "fontSzHero: the one title bigger than display — the home screen's 'PinPoint Studio' (2 × display).", italic=it, sample="PinPoint Studio"),
        st("display", ds, disp, dw, "fontSzDisplay: page and session titles (PpDisplayText, gradient sweep when gradientTitlesActive).", italic=it, sample="Swing diagnostics"),
        st("heading", 16, body, bw, "fontSzHeading: card and section titles.", sample="Your focus"),
        st("body", 13, body, bw, "fontSzBody: running copy, row titles.", sample="Your hips open early on the downswing."),
        st("body2", 12, body, bw, "fontSzBody2: secondary copy, a row's second line.", sample="Seen in 7 of your last 10 swings"),
        st("label", 11, body, bw, "fontSzLabel: control labels and small headings; headings set in capitals via Theme.caps().", ls=0.6, sample="CHARTS"),
        st("micro", 10, data, 400, "fontSzMicro: micro labels and live values (PpMicro), tracked.", ls=0.8, sample="P4 · TOP"),
        st("data-lg", 26, data, 400, "fontSzDataLg: the launch-monitor board's headline figure, read from a few feet away.", ls=0.3, sample="104.2"),
        st("data", 20, data, 400, "fontSzData: large metric readouts.", ls=0.3, sample="98.4"),
        st("data-sm", 13, data, 400, "fontSzDataSm: small figures, status values, timestamps.", ls=0.3, sample="240 fps"),
    ]})

ROWS = [("rowH", "A table row (PpTableRow)"), ("rowH2", "A table row with a second line"),
        ("itemRowH", "An expanded area's item row"), ("tableHeaderH", "A table card's caption row"),
        ("rowPadH", "A row's horizontal padding"), ("itemIndent", "An item row's indent under its expand row (rowPadH + the badge column)")]
spacing = []
for n, u in ROWS:
    c, d, sp = geo(n)
    spacing += [(n, d, u + " — Default density."), (n + "Compact", c, u + " — Compact density."), (n + "Spacious", sp, u + " — Spacious density.")]
spacing += [
    ("headerHeight", geo("headerHeight"), "The app header bar (fixed, not scaled)."), ("statusBarHeight", geo("statusBarHeight"), "The bottom status strip (fixed)."),
    ("carouselHeight", geo("carouselHeight"), "The capture carousel strip (sp-scaled)."), ("sidenavWidth", geo("sidenavWidth"), "The side navigation column (sp-scaled)."),
]
for a_ in AES:
    spacing.append(("railWidth" + LABEL[a_], theme[a_]["railWidth"], "Nav rail width, %s." % LABEL[a_]))
for n, u in [("meterSegW", "A how-often meter segment's width (PpMeter, ten segments)."), ("meterSegH", "A meter segment's height."),
             ("meterSegGap", "Gap between meter segments."), ("pipSize", "A session pip (PpPip)."), ("pipGap", "Gap between pips."),
             ("bandBarH", "The area band bar's height (PpBandBar)."), ("tabUnderline", "The current title-size tab's accent underline."),
             ("fontSzTab", "Title-size tab label size (PpTabs size 'title')."), ("capDot", "A capability row's dot."),
             ("cardRule", "The tone rule across a card's top."), ("heroRule", "The tone rule across the hero card's top."),
             ("borderWidth", "Every border and hairline: 1 px at translucent ink."),
             ("variabilityIconSize", "A variability mark's Icon edge: PpVariabilityMark's default `size`, and the Icon badge's mark (PpVariabilityBadge). A `dense` badge draws its mark at sp(16), a number, not a token."),
             ("variabilityCellW", "A variability mark's Cell width at variabilityCellH; a Cell fits Theme.rowH at every density. No component reads it: a Cell is drawn `size` × 80 / 24 wide, which is this at the token height. Use it to size a table column."),
             ("variabilityCellH", "A variability mark's Cell height: the Cell badge's mark. A bare Cell mark defaults to variabilityIconSize, so set `size: Theme.sp(Theme.variabilityCellH)` on one."),
             ("variabilityFunnelH", "The spread funnel's graphic height (PpVariabilityFunnel), compact and wide alike. The compact graphic is sp(120) wide, a number, not a token.")]:
    spacing.append((n, geo(n), u))
radius = []
for a in AES:
    radius.append({"name": f"radius{LABEL[a]}", "value": f"{theme[a]['radius']}px",
                   "usage": f"Theme.radius in {LABEL[a]}: buttons, chips, fields, pills' squarer siblings."})
    radius.append({"name": f"radiusLg{LABEL[a]}", "value": f"{theme[a]['radiusLg']}px",
                   "usage": f"Theme.radiusLg in {LABEL[a]}: cards, table cards (tableRadius), popovers."})
radius.append({"name": "meterSegRadius", "value": "%dpx" % geo("meterSegRadius"), "usage": "A meter segment's corner."})

tokens = {
    "name": "PinPoint Studio", "version": 1,
    "color": {"themes": [{"id": t, "name": f"{LABEL[t.split('-')[0]]} {t.split('-')[1].capitalize()}"} for t in THEMES],
              "tokens": color_tokens},
    "type": {"fonts": fonts, "families": families, "groups": groups},
    "spacing": {"note": "PinPoint has no spacing scale: layouts use Theme.gap(n) (space, × spacingScale) and Theme.sp(n) (size), both × the user's fontScale. These are the named geometry tokens, in design px.",
                "tokens": [{"name": n, "value": f"{v}px", "usage": u} for n, v, u in spacing]},
    "radius": {"note": "Radius is per aesthetic, not per mode: use the pair for the active aesthetic (Theme.radius / Theme.radiusLg).", "tokens": radius},
    "density": {"note": "Settings → Appearance → Density (appSettings ui/density: compact | default | spacious). The scale multiplies Theme.gap(n) — the space BETWEEN things — on top of fontScale and spacingScale; the size of a thing (Theme.sp) never changes. Row geometry has its own per-density values in Spacing.",
                "tokens": [
        {"name": "densityCompact", "value": DENS["compact"], "usage": "Compact: gap(n) × densityCompact. Reduces rail, panel and card padding; rows tighten to the *Compact spacing tokens."},
        {"name": "densityDefault", "value": DENS["default"], "usage": "Default (standard): gap(n) unchanged; rows at rowH, rowH2 … — exactly today's app."},
        {"name": "densitySpacious", "value": DENS["spacious"], "usage": "Spacious: gap(n) × densitySpacious; rows open to the *Spacious spacing tokens."},
    ]},
    "opacity": {"tokens": [
        {"name": "borderOpacityNormal", "value": "0.5", "usage": "Border opacity, normal."},
        {"name": "borderOpacityStrong", "value": "0.75", "usage": "Border opacity, strong."},
        {"name": "overlayOpacity", "value": "0.7", "usage": "Default opacity of overlays drawn on video (user setting)."},
        {"name": "variabilityPrintAlphaScale", "value": real("variabilityPrintAlphaScale"),
         "usage": "On paper (medium \"print\"), every translucent fill of the variability marks multiplies its alpha by this, clamped to 1, so the wash still prints: the Dispersion dots and group ellipse, the Needle's wedges, the typicality inner zone and ghost dots, the trace's envelope and the funnel's band. Strokes keep theirs (the Overlay traces, rings, lines, the band's edge). Used in place of roles.tintK, never with it."},
        {"name": "variabilityWashAlpha", "value": per(wash_alpha),
         "usage": "On screen, every translucent wash of the variability marks multiplies its alpha by this, clamped to 1: the Dispersion Cell's group ellipse, the Needle's wedges (Icon and Cell, small-size alphas too), the funnel's band fill, the typicality inner zone and the trace's envelope. Not washes: strokes, the band's edge, the 70–85 % dots, the ghost dots, the dashed outer zone, any line. 1 in light themes, 3 in dark (Mark, 7 Nov 2026: the 12/16/24 % Needle wedges could not be told apart in dark themes); a theme or mode block may set its own. Print never reads it: on paper the fills take variabilityPrintAlphaScale instead."},
    ]},
    "behaviour": {"note": "Not a look: numbers that decide WHEN a look changes (the grader in C++ applies the hysteresis; Theme mirrors it for QML). Not theme keys.", "tokens": [
        {"name": "variabilityHysteresis", "value": real("variabilityHysteresis"),
         "usage": "Live-session grade hysteresis (variability spec §8.9): a grade changes only when ρ (or s′, or ρm) crosses a band edge by this much. The C++ grader applies it (hysteresisGrade); no component reads it, and a mark draws whatever grade it is given. Equals kHysteresis in src/Metrics/variability/variability_constants.h (the generator checks); frozen session assessments use the plain bands."},
    ]},
    "meta": {"source": "src/Gui/theme/Theme.qml", "generator": "tools/theme/make_design_system.py",
             "aesthetics": AES, "note": "Generated — edit Theme.qml and rerun, never this file."},
}
text = json.dumps(tokens, indent=1, ensure_ascii=False) + "\n"
if args.check:
    cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
    if cur != text:
        print("docs/design/system/tokens.json is stale: run tools/theme/make_design_system.py", file=sys.stderr)
        sys.exit(1)
    print("docs/design/system/tokens.json is current")
    sys.exit(0)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write(text)
print(len(color_tokens), "colours;", sum(len(g["styles"]) for g in groups), "styles")
