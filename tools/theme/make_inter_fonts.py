#!/usr/bin/env python3
"""Build Folio's bundled faces from an Inter release (https://github.com/rsms/inter, OFL-1.1).

    python3 tools/theme/make_inter_fonts.py <unzipped Inter-4.1 dir>

Writes into src/Resources/fonts/:
  * Inter-{Light,Regular,Italic,Medium,MediumItalic,SemiBold,Bold}.ttf and
    InterDisplay-{Regular,Medium}.ttf — STATIC faces, not the variable file: macOS/CoreText will not
    interpolate a variable weight axis for an application font (see the Fraunces/Literata note in
    src/Gui/main.cpp), so every weight the app asks for needs its own face;
  * InterTabular-{Regular,Medium,SemiBold,Bold}.ttf — "Inter Tabular", the same faces with the
    tabular figures (OpenType `tnum`) frozen in as the DEFAULT digits and the family renamed. Theme's
    fontData is this family in Folio, so every number in the app lines up in columns without a single
    call site having to ask for font.features. (Inter's licence reserves no font name, so a renamed
    derivative is allowed; Inter-OFL.txt travels with the files.)
Each face is subset to the scripts and symbols the app draws (Latin, Greek, punctuation, arrows,
maths, shapes, dingbats), which takes them from ~410 KB to ~290 KB. Needs fontTools.
"""
import pathlib, subprocess, sys, tempfile
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "src/Resources/fonts"
UNICODES = ("U+0000-024F,U+0259,U+02B0-02FF,U+0300-036F,U+0370-03FF,U+1E00-1EFF,U+2000-206F,"
            "U+2070-209F,U+20A0-20CF,U+2100-214F,U+2150-218F,U+2190-21FF,U+2200-22FF,U+2300-23FF,"
            "U+2460-24FF,U+2500-257F,U+25A0-25FF,U+2600-26FF,U+2700-27BF,U+FB00-FB06,U+FEFF,U+FFFD")
TEXT = ["Light", "Regular", "Italic", "Medium", "MediumItalic", "SemiBold", "Bold"]
DISPLAY = ["Regular", "Medium"]
TABULAR = ["Regular", "Medium", "SemiBold", "Bold"]

def freeze_tnum(src, dst):
    f = TTFont(src)
    gsub, subst = f["GSUB"].table, {}
    for fr in gsub.FeatureList.FeatureRecord:
        if fr.FeatureTag != "tnum":
            continue
        for li in fr.Feature.LookupListIndex:
            lk = gsub.LookupList.Lookup[li]
            for st in lk.SubTable:
                st = st.ExtSubTable if lk.LookupType == 7 else st
                if hasattr(st, "mapping"):
                    subst.update(st.mapping)
    for t in f["cmap"].tables:
        if t.isUnicode():
            for cp, g in list(t.cmap.items()):
                if g in subst:
                    t.cmap[cp] = subst[g]
    for rec in f["name"].names:
        s = rec.toUnicode()
        if rec.nameID in (1, 3, 4, 16):
            rec.string = s.replace("Inter", "Inter Tabular", 1)
        elif rec.nameID == 6:
            rec.string = s.replace("Inter", "InterTabular", 1)
    f.save(dst)

def subset(src, dst):
    subprocess.run([sys.executable, "-m", "fontTools.subset", str(src), f"--unicodes={UNICODES}",
                    "--layout-features=*", "--name-IDs=*", "--name-languages=*", "--glyph-names",
                    "--notdef-outline", f"--output-file={dst}"], check=True)

def main():
    rel = pathlib.Path(sys.argv[1])
    ttf = rel / "extras/ttf"
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        jobs = [(ttf / f"Inter-{w}.ttf", f"Inter-{w}.ttf") for w in TEXT]
        jobs += [(ttf / f"InterDisplay-{w}.ttf", f"InterDisplay-{w}.ttf") for w in DISPLAY]
        for w in TABULAR:
            freeze_tnum(ttf / f"Inter-{w}.ttf", tmp / f"InterTabular-{w}.ttf")
            jobs.append((tmp / f"InterTabular-{w}.ttf", f"InterTabular-{w}.ttf"))
        for src, name in jobs:
            subset(src, OUT / name)
            print("wrote", OUT / name)
    (OUT / "Inter-OFL.txt").write_text((rel / "LICENSE.txt").read_text() +
        "\n\nInter Tabular (InterTabular-*.ttf) is a modified version of Inter made for PinPoint "
        "Studio: tabular figures set as the default digits, subset, and renamed. Same licence.\n")

if __name__ == "__main__":
    main()
