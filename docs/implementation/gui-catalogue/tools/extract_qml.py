#!/usr/bin/env python3
"""Extract a structural inventory of the PinPointStudio QML layer.

Read-only against the repo. Writes JSON to the path given as argv[2].
    extract_qml.py <repo-root> <out.json>
"""
import json, os, re, sys, hashlib, collections

root = os.path.abspath(sys.argv[1])
out = sys.argv[2]

# --- the canonical module membership, straight from cmake -----------------------------------
cm = open(os.path.join(root, "cmake/PinPointQmlModule.cmake"), encoding="utf-8").read()
def cmake_list(name):
    m = re.search(r"set\(%s\s*(.*?)\n\)" % name, cm, re.S)
    return [l.strip() for l in m.group(1).splitlines() if l.strip() and not l.strip().startswith("#")] if m else []
module_files = set(cmake_list("PP_QML_FILES"))
singletons = set(cmake_list("PP_QML_SINGLETONS"))

# --- find files ---------------------------------------------------------------------------
def classify(rel):
    if "/tests/" in rel or rel.startswith("tests/"): return "test"
    if rel.startswith("tools/"): return "tool"
    return "app"

qml, js = [], []
for base in ("src", "tools"):
    for dp, dn, fn in os.walk(os.path.join(root, base)):
        dn[:] = [d for d in dn if d not in ("build", "third_party", ".git")]
        for f in fn:
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, root)
            if f.endswith(".qml"): qml.append(rel)
            elif f.endswith(".js") and "/Gui/" in rel or (f.endswith(".js") and rel.startswith("tools/")): js.append(rel)
qml.sort(); js.sort()

LIC = re.compile(r"Copyright|GNU General Public|Free Software Foundation|MERCHANTABILITY|Franklin Street|more details|ANY WARRANTY|WITHOUT|Software Foundation|with this program|under the terms|This program is|any later version|^\s*\*/?\s*$", re.I)

def header_comment(src):
    """Descriptive comment: the block directly above the root object, else the first after the licence."""
    L = src.splitlines()
    for idx, ln in enumerate(L):
        if re.match(r"^[A-Z][\w.]*\s*\{", ln):
            blk, j = [], idx - 1
            while j >= 0 and L[j].strip().startswith("//"):
                blk.append(L[j].strip()[2:].strip()); j -= 1
            if blk: return [b for b in reversed(blk) if b]
            break
    return _header_after_licence(src)

def _header_after_licence(src):
    lines = src.splitlines()
    i, n = 0, len(lines)
    # skip the licence block comment
    if n and lines[0].lstrip().startswith("/*"):
        while i < n and "*/" not in lines[i]: i += 1
        i += 1
    buf = []
    while i < n and (lines[i].strip() == "" or lines[i].lstrip().startswith(("//", "/*", "*", "pragma", ".pragma", ".import"))):
        s = lines[i].strip()
        if s.startswith("pragma") or s.startswith(".pragma"): i += 1; continue
        if s.startswith("//"): buf.append(s[2:].strip())
        elif s.startswith("/*") or s.startswith("*"): buf.append(s.lstrip("/*").rstrip("*/").strip())
        elif s == "" and buf: break
        i += 1
    txt = [b for b in buf if b]
    return txt

def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"(?m)(?<!:)//.*$", "", s)
    return s

TYPE_RE = re.compile(r"(?m)^\s*(?:[A-Za-z_]\w*\s*:\s*)?([A-Z]\w*(?:\.[A-Z]\w*)*)\s*\{")
PROP_RE = re.compile(r"(?m)^([ \t]*)((?:required\s+|readonly\s+|default\s+)*)property\s+([\w.<>]+)\s+(\w+)")
ROOT_IND = 4   # direct children of the root object sit at <= 4 columns
SIG_RE = re.compile(r"(?m)^([ \t]*)signal\s+(\w+)\s*(\([^)]*\))?")
FN_RE = re.compile(r"(?m)^([ \t]*)function\s+(\w+)\s*(\([^)]*\))?")
def ind(x): return len(x.expandtabs(4))
INLINE_RE = re.compile(r"(?m)^\s*component\s+(\w+)\s*:\s*([A-Z]\w*)")
COLOR_RE = re.compile(r"""(?<![\w.])(?:"|')?#[0-9a-fA-F]{3,8}(?:"|')?|Qt\.rgba\(|Qt\.(?:lighter|darker|tint)\(""")
THEME_RE = re.compile(r"\bTheme\.(\w+)")
CTX_RE = re.compile(r"\b(?:ctx|ctl|controller|chartsController)\b")

# Known built-in / Qt types we do not count as project components
QT_TYPES = set("""Item Rectangle Text Image Row Column Grid Flow Repeater ListView GridView PathView Loader Timer Connections
MouseArea TapHandler HoverHandler DragHandler PinchHandler WheelHandler PointHandler Flickable ScrollView ScrollBar Canvas Shape ShapePath PathLine PathMove PathArc PathSvg PathQuad PathAngleArc PathCubic
Behavior NumberAnimation PropertyAnimation ColorAnimation SequentialAnimation ParallelAnimation PauseAnimation OpacityAnimator RotationAnimation Transition State StateGroup PropertyChanges AnchorChanges
RowLayout ColumnLayout GridLayout StackLayout StackView SwipeView Layout Button Label TextField TextInput TextEdit ComboBox CheckBox RadioButton Switch Slider SpinBox Popup Dialog Menu MenuItem ToolTip Overlay Drawer Page Pane Frame Control Action ButtonGroup
FileDialog FolderDialog ColorDialog MessageDialog Window ApplicationWindow QtObject Component Binding Instantiator FontLoader Settings ListModel ListElement ScaleAnimator XAnimator YAnimator
ShaderEffect ShaderEffectSource MultiEffect DropShadow Gradient GradientStop Scale Rotation Translate Matrix4x4 Shortcut Video MediaPlayer VideoOutput AudioOutput CameraDevice
View3D PerspectiveCamera OrthographicCamera DirectionalLight PointLight SceneEnvironment Node Model PrincipledMaterial DefaultMaterial Texture Repeater3D Loader3D Skeleton Joint
DelegateModel DelegateChoice DelegateChooser Item3D SplitView TabBar TabButton ToolBar ToolButton ToolSeparator Material AbstractButton Container ItemDelegate MenuSeparator Menu Binding WorkerScript
LoggingCategory TextMetrics FontMetrics Qt Math Date JSON Array ScrollIndicator SortFilterProxyModel QtQuick GridLayout PathView Path Rotator AnimatedImage BorderImage Item PaintedItem Palette
ImageSelector IconImage Gradient LinearGradient EffectItem DropArea Drag Keys KeyNavigation FocusScope Accessible InputPanel Screen TestCase SignalSpy ShapeGradient LinearGradient RadialGradient ConicalGradient
BusyIndicator ProgressBar TextArea Tumbler Dial RangeSlider RoundButton DelayButton ToolTip SelectionRectangle HeaderView TableView TreeView TableModelColumn TableModel ItemSelectionModel
Qt.labs""".split())

comps = {}
for rel in qml:
    p = os.path.join(root, rel)
    src = open(p, encoding="utf-8", errors="replace").read()
    nolic = strip_comments(src)
    lines = src.count("\n") + 1
    code_lines = sum(1 for l in nolic.splitlines() if l.strip())
    cat = classify(rel)
    name = os.path.basename(rel)[:-4]
    types = TYPE_RE.findall(nolic)
    root_type = types[0] if types else None
    props_m = PROP_RE.findall(nolic)
    props = [(t, n) for _, _, t, n in props_m]
    root_props = [dict(type=t, name=n, readonly=("readonly" in mods), required=("required" in mods))
                  for i, mods, t, n in props_m if ind(i) <= ROOT_IND]
    root_signals = [dict(name=n, sig=(a or "()")) for i, n, a in SIG_RE.findall(nolic) if ind(i) <= ROOT_IND]
    root_functions = [dict(name=n, sig=(a or "()")) for i, n, a in FN_RE.findall(nolic) if ind(i) <= ROOT_IND]
    comps[rel] = dict(
        path=rel, name=name, dir=os.path.dirname(rel), category=cat,
        lines=lines, code_lines=code_lines,
        singleton=("pragma Singleton" in src),
        in_module=(rel in module_files),
        in_singleton_list=(rel in singletons),
        root_type=root_type,
        imports=sorted(set(re.findall(r"(?m)^import\s+(\S+)", src))),
        header=header_comment(src),
        props=[dict(type=t, name=n) for t, n in props],
        signals=[n for _, n, _ in SIG_RE.findall(nolic)],
        functions=[n for _, n, _ in FN_RE.findall(nolic)],
        root_props=root_props, root_signals=root_signals, root_functions=root_functions,
        inline_components=[dict(name=a, base=b) for a, b in INLINE_RE.findall(nolic)],
        instantiates=sorted(set(t for t in types if t.split(".")[0] not in QT_TYPES and "." not in t)),
        n_instances=len(types),
        theme_tokens=sorted(set(THEME_RE.findall(nolic))),
        n_theme_refs=len(THEME_RE.findall(nolic)),
        n_hard_colors=len(COLOR_RE.findall(nolic)),
        n_loaders=nolic.count("Loader {") + nolic.count("Loader{"),
        uses_canvas=("Canvas {" in nolic),
        uses_shape=("ShapePath" in nolic),
        uses_view3d=("View3D" in nolic),
        md5=hashlib.md5(nolic.encode()).hexdigest(),
    )

# --- reverse index: who instantiates / references whom -------------------------------------
names = collections.defaultdict(list)
for rel, c in comps.items():
    names[c["name"]].append(rel)
for rel, c in comps.items():
    c["used_by"] = []
for rel, c in comps.items():
    refs = set(c["instantiates"]) | set(b["base"] for b in c["inline_components"])
    # singletons are referenced as Foo.bar, not Foo { } — grab bare references too
    nolic = strip_comments(open(os.path.join(root, rel), encoding="utf-8", errors="replace").read())
    for ident in set(re.findall(r"\b([A-Z]\w+)\b", nolic)):
        if ident in names and ident != c["name"]:
            refs.add(ident)
    c["references"] = sorted(r for r in refs if r in names)
    for r in c["references"]:
        for target in names[r]:
            comps[target]["used_by"].append(rel)
for c in comps.values():
    c["used_by"] = sorted(set(c["used_by"]))

# --- JS helpers -----------------------------------------------------------------------------
jsinfo = []
for rel in js:
    src = open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()
    nolic = strip_comments(src)
    fns = re.findall(r"(?m)^\s*function\s+(\w+)", nolic)
    jsinfo.append(dict(path=rel, lines=src.count("\n") + 1, functions=fns,
                       header=header_comment(src), library=(".pragma library" in src),
                       category=classify(rel)))

# --- C++ in the GUI layer ------------------------------------------------------------------
cpp = []
CLASS_RE = re.compile(r"(?m)^\s*(?:class|struct)\s+(?:\w+_EXPORT\s+)?(\w+)\s*(?:final\s*)?:\s*public\s+([\w:]+)|^\s*(?:class|struct)\s+(?:\w+_EXPORT\s+)?(\w+)\s*(?:final\s*)?\n\s*:\s*public\s+([\w:]+)")
for dp, dn, fn in os.walk(os.path.join(root, "src/Gui")):
    for f in sorted(fn):
        if not f.endswith((".h", ".cpp")): continue
        p = os.path.join(dp, f)
        rel = os.path.relpath(p, root)
        src = open(p, encoding="utf-8", errors="replace").read()
        cat = classify(rel)
        classes = [(a or c, b or d) for a, b, c, d in CLASS_RE.findall(src)] if f.endswith(".h") else []
        cpp.append(dict(
            path=rel, category=cat, lines=src.count("\n") + 1,
            classes=[dict(name=a, base=b) for a, b in classes],
            q_properties=len(re.findall(r"\bQ_PROPERTY\(", src)),
            q_invokables=len(re.findall(r"\bQ_INVOKABLE\b", src)),
            q_signals=len(re.findall(r"(?m)^\s*(?:Q_SIGNALS|signals)\s*:", src)) and len(re.findall(r"(?m)^\s+void\s+\w+\(.*\);\s*$", src.split("signals:")[1].split("public")[0])) if "signals:" in src else 0,
            has_q_object=("Q_OBJECT" in src),
            qml_element=len(re.findall(r"QML_ELEMENT|QML_NAMED_ELEMENT", src)),
            qml_singleton=("QML_SINGLETON" in src),
            context_props=re.findall(r'setContextProperty\(\s*(?:QStringLiteral\()?"(\w+)"', src),
            header=header_comment(src)[:6],
        ))

# context properties anywhere in src (main.cpp etc.)
ctxprops = []
for dp, dn, fn in os.walk(os.path.join(root, "src")):
    dn[:] = [d for d in dn if d not in ("tests",)]
    for f in fn:
        if f.endswith((".cpp", ".h", ".mm")):
            p = os.path.join(dp, f)
            for i, l in enumerate(open(p, encoding="utf-8", errors="replace"), 1):
                m = re.search(r'setContextProperty\(\s*(?:QStringLiteral\()?"(\w+)"\s*\)?,\s*([^)]*)', l)
                if m: ctxprops.append(dict(name=m.group(1), value=m.group(2).strip(), file=os.path.relpath(p, root), line=i))

for cp in ctxprops:
    v = cp["value"].lstrip("&").strip().rstrip(")").strip()
    cp["variable"] = v
    try:
        txt = open(os.path.join(root, cp["file"]), encoding="utf-8", errors="replace").read()
        m = re.search(r"(?m)^\s*(?:static\s+|const\s+)?([A-Z]\w+(?:::\w+)?)\s*[&*]?\s*" + re.escape(v) + r"\s*(?:[;({=]|$)", txt)
        cp["type"] = m.group(1) if m else None
    except Exception:
        cp["type"] = None

# who uses each context property (QML-side bare identifier reference)
qml_src = {rel: strip_comments(open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()) for rel in comps}
for cp in ctxprops:
    pat = re.compile(r"(?<![\w.\"'])%s\b" % re.escape(cp["name"]))
    cp["used_in"] = sorted(r for r, s in qml_src.items() if pat.search(s) and comps[r]["category"] == "app")

json.dump(dict(components=list(comps.values()), js=jsinfo, cpp=cpp, context_properties=ctxprops,
               module_file_count=len(module_files)), open(out, "w"), indent=1)
print("qml", len(comps), "js", len(jsinfo), "cpp", len(cpp), "ctx", len(ctxprops))
