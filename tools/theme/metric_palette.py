#!/usr/bin/env python3
"""Generate and check the metric palette (Theme.qml `_metricPalettes`, MetricDescriptor::color).

Every metric is drawn in one NAMED colour (cornflower, crimson, ...). Each theme defines every name
for its own background, light and dark. This tool:

  * generates the twelve names for each theme and mode in OKLCH — two lightness tiers alternating
    around the hue wheel, so names that neighbour in hue never also share a lightness (what keeps
    them apart under deuteranopia/protanopia), each nudged until it clears 3:1 on colorSurface;
  * reads the manifest's `.color` names and reports, per group and per preset, the worst pair of
    co-plotted time series — normal vision (OKLab dE x100) and Machado 2009 protan/deutan;
  * with --qml, prints the `_metricPalettes` block to paste into src/Gui/theme/Theme.qml.

No dependencies.   python3 tools/theme/metric_palette.py [--qml]
"""
import itertools, math, re, sys, collections, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
MACH={"protan":[[0.152286,1.052583,-0.204868],[0.114503,0.786281,0.099216],[-0.003882,-0.048116,1.051998]],
"deutan":[[0.367322,0.860646,-0.227968],[0.280085,0.672501,0.047413],[-0.011820,0.042940,0.968881]]}
def s2l(c): return c/12.92 if c<=0.04045 else ((c+0.055)/1.055)**2.4
def l2s(c): c=max(0,min(1,c)); return 12.92*c if c<=0.0031308 else 1.055*c**(1/2.4)-0.055
def lin(h): return [s2l(int(h[i:i+2],16)/255) for i in (1,3,5)]
def oklab_lin(rgb):
    r,g,b=rgb
    l=(0.4122214708*r+0.5363325363*g+0.0514459929*b)**(1/3)
    m=(0.2119034982*r+0.6806995451*g+0.1073969566*b)**(1/3)
    s=(0.0883024619*r+0.2817188376*g+0.6299787005*b)**(1/3)
    return [0.2104542553*l+0.7936177850*m-0.0040720468*s,1.9779984951*l-2.4285922050*m+0.4505937099*s,0.0259040371*l+0.7827717662*m-0.8086757660*s]
def sim(h,k):
    r=lin(h);M=MACH[k];return [max(0,min(1,sum(M[i][j]*r[j] for j in range(3)))) for i in range(3)]
def dE(a,b,k=None):
    A=oklab_lin(sim(a,k) if k else lin(a));B=oklab_lin(sim(b,k) if k else lin(b))
    return 100*math.dist(A,B)
def dist(a,b): return min(dE(a,b), dE(a,b,"deutan"), dE(a,b,"protan"))
def oklch2lin(L,C,h):
    a=C*math.cos(math.radians(h)); b=C*math.sin(math.radians(h))
    l_=L+0.3963377774*a+0.2158037573*b; m_=L-0.1055613458*a-0.0638541728*b; s_=L-0.0894841775*a-1.2914855480*b
    l,m,s=l_**3,m_**3,s_**3
    return (4.0767416621*l-3.3077115913*m+0.2309699292*s,-1.2684380046*l+2.6097574011*m-0.3413193965*s,-0.0041960863*l-0.7034186147*m+1.7076147010*s)
def tohex(L,C,h):
    while C>0 and not all(-1e-4<=c<=1+1e-4 for c in oklch2lin(L,C,h)): C-=0.002
    return "#%02X%02X%02X"%tuple(round(l2s(c)*255) for c in oklch2lin(L,C,h))
def relLum(h): r,g,b=lin(h); return 0.2126*r+0.7152*g+0.0722*b
def contrast(a,b):
    x,y=sorted([relLum(a),relLum(b)],reverse=True); return (x+.05)/(y+.05)

# name, OKLCH hue, tier (0 deeper, 1 lighter) — order matches metricColorNames() in metric_descriptor.h
NAMES = [("crimson", 20, 0), ("coral", 48, 1), ("ochre", 75, 0), ("gold", 100, 1), ("moss", 130, 0),
         ("mint", 162, 1), ("teal", 195, 0), ("sky", 225, 1), ("cornflower", 262, 0),
         ("lavender", 288, 1), ("violet", 308, 0), ("orchid", 338, 1)]
# aesthetic: dark (deep L, light L, C), light (deep L, light L, C), warm hue bias
THEMES = {
    "studio":     ((0.64, 0.81, 0.150), (0.45, 0.61, 0.150), 0),
    "instrument": ((0.65, 0.82, 0.125), (0.44, 0.60, 0.125), 6),
    "editorial":  ((0.66, 0.83, 0.120), (0.42, 0.59, 0.130), 0),
    "vector":     ((0.64, 0.81, 0.200), (0.46, 0.62, 0.190), 0),
    "terrain":    ((0.65, 0.82, 0.120), (0.43, 0.60, 0.120), 8),
    "links":      ((0.63, 0.80, 0.115), (0.42, 0.58, 0.115), 4)}
# Theme.colorSurface (dark, light) — what the chart is drawn on
SURF = {"studio": ("#131519", "#FBFCFD"), "instrument": ("#0A0F13", "#FBF8F0"),
        "editorial": ("#191612", "#FFFFFF"), "vector": ("#13151A", "#FAFBFC"),
        "terrain": ("#0B110D", "#FAFBF5"), "links": ("#131820", "#F6F1E4")}

def build(t, mode):
    d, l, bias = THEMES[t]
    lo, hi, C = d if mode == "dark" else l
    surf = SURF[t][0 if mode == "dark" else 1]
    out = {}
    for n, h, tier in NAMES:
        hh = h - bias * math.sin(math.radians(h - 60))
        L = hi if tier else lo
        while True:
            v = tohex(L, C, hh)
            if contrast(v, surf) >= 3.0: break
            L += 0.01 if mode == "dark" else -0.01
        out[n] = v
    return out

PAL = {t: {m: build(t, m) for m in ("dark", "light")} for t in THEMES}

def manifest():
    src = (ROOT / "src/Metrics/metric_catalogue_manifest.cpp").read_text()
    rows, presets, key = [], collections.defaultdict(list), None
    cur = {}
    for line in src.split("\n"):
        if m := re.search(r'\.key = QStringLiteral\("([^"]+)"\)', line): cur = {"key": m.group(1)}
        if m := re.search(r'\.type = MetricType::(\w+)', line): cur["type"] = m.group(1)
        if m := re.search(r'\.group = QStringLiteral\("([^"]+)"\)', line): cur["group"] = m.group(1)
        if m := re.search(r'\.color = QStringLiteral\("([^"]+)"\)', line):
            cur["color"] = m.group(1); rows.append(cur)
        if ".presets = " in line:
            for p in re.findall(r'QStringLiteral\("([^"]+)"\)', line): presets[p].append(cur["key"])
    return rows, presets

def worst(names_):
    n = c = 99.0
    for a, b in itertools.combinations(names_, 2):
        for t in PAL:
            for m in PAL[t]:
                p = PAL[t][m]
                n = min(n, dE(p[a], p[b]))
                c = min(c, dE(p[a], p[b], "deutan"), dE(p[a], p[b], "protan"))
    return n, c

def qml():
    order = [n for n, _, _ in NAMES]
    print("    readonly property var _metricPalettes: ({")
    for i, t in enumerate(THEMES):
        print(f'        "{t}": {{')
        for j, m in enumerate(("dark", "light")):
            a = ", ".join(f'{n}: "{PAL[t][m][n]}"' for n in order[:6])
            b = ", ".join(f'{n}: "{PAL[t][m][n]}"' for n in order[6:])
            print(f'            {m}: {{ {a},\n            {" " * len(m)}   {b} }}' + ("," if j == 0 else ""))
        print("        }" + ("," if i < len(THEMES) - 1 else ""))
    print("    })")

if __name__ == "__main__":
    if "--qml" in sys.argv: qml(); sys.exit(0)
    for t in PAL:
        for m in PAL[t]:
            s = SURF[t][0 if m == "dark" else 1]
            print(f"{t:10s} {m:5s} min contrast {min(contrast(v, s) for v in PAL[t][m].values()):.2f}")
    rows, presets = manifest()
    by = collections.defaultdict(list)
    for r in rows:
        if r["type"] == "TimeSeries": by[r["group"]].append(r)
    byk = {r["key"]: r for r in rows}
    for p, ks in presets.items(): by["preset: " + p] = [byk[k] for k in ks]
    print("\nworst co-plotted pair, over all themes and modes (normal dE >= 15 is easy; CVD >= 8 target):")
    for g, rs in by.items():
        cols = [r["color"] for r in rs]
        dup = len(set(cols)) != len(cols)
        n, c = worst(cols)
        print(f"  {g:28s} {len(rs)} curves  normal {n:5.1f}  cvd {c:5.1f}{'  DUPLICATE NAME' if dup else ''}")
