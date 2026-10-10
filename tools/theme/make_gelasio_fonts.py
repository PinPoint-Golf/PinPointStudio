#!/usr/bin/env python3
"""Build the Instrument theme's serif from a Gelasio checkout (https://github.com/SorkinType/Gelasio,
OFL-1.1). Gelasio replaces Georgia, which was bundled without a licence to redistribute it.

    python3 tools/theme/make_gelasio_fonts.py <Gelasio-main dir>

Writes src/Resources/fonts/Gelasio-{Regular,Italic,Bold,BoldItalic}.ttf and Gelasio-OFL.txt.

THE FOUR STYLE-LINKED FACES, AND NOTHING ELSE. Georgia shipped exactly Regular / Italic / Bold /
Bold Italic, all named family "Georgia" in nameID 1 with a RIBBI subfamily (Regular, Italic, Bold, Bold Italic). That is the
one arrangement every font stack groups the same way: CoreText, DirectWrite, GDI and fontconfig all
put the four in one family and style-link them. A Medium or SemiBold static would carry nameID 1
"Gelasio Medium", which Windows may file as a family of its own. So Instrument asks for 400 and 700 and
gets concrete faces everywhere, and a 500/600 request resolves to the nearest of them exactly as it
did with Georgia.

GEORGIA'S LINE BOX. Gelasio is metric-compatible with Georgia across (every ASCII advance is equal at
2048 upem) but not down: its line box is 1900 + 700 units against Georgia's 1878 + 449, ~12% taller,
and every Instrument layout — and the QML layout suite, which renders in Instrument — was built on
Georgia's. So the vertical metrics are set to Georgia's: hhea and OS/2 typo ascender 1878, descender
-449, line gap 0, with USE_TYPO_METRICS kept on so Windows (which would otherwise use winAscent/
winDescent) spaces lines from the same numbers macOS reads from hhea. win ascent/descent are set to
cover the glyph bounding box, because on Windows they are the CLIP box. The result lays out exactly
like Georgia, the same on every platform. Gelasio's licence names no Reserved Font Name, so the
modified faces keep the family name; Gelasio-OFL.txt says what was changed. Needs fontTools.
"""
import pathlib, sys
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "src/Resources/fonts"
FACES = ["Regular", "Italic", "Bold", "BoldItalic"]
ASC, DESC = 1878, -449            # Georgia's hhea line box at 2048 upem

def main():
    src = pathlib.Path(sys.argv[1])
    for face in FACES:
        f = TTFont(src / f"fonts/ttf/Gelasio-{face}.ttf")
        assert f["head"].unitsPerEm == 2048
        hh, os2, head = f["hhea"], f["OS/2"], f["head"]
        hh.ascent, hh.descent, hh.lineGap = ASC, DESC, 0
        os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = ASC, DESC, 0
        os2.fsSelection |= 0x80                     # USE_TYPO_METRICS
        os2.usWinAscent = max(ASC, head.yMax)
        os2.usWinDescent = max(-DESC, -head.yMin)
        f.save(OUT / f"Gelasio-{face}.ttf")
        print("wrote", OUT / f"Gelasio-{face}.ttf")
    (OUT / "Gelasio-OFL.txt").write_text((src / "OFL.txt").read_text() +
        "\n\nModified for PinPoint Studio (tools/theme/make_gelasio_fonts.py): the vertical line "
        "metrics (hhea, OS/2 typo and win ascent/descent) are set to Georgia's line box so the faces "
        "lay out as the Georgia they replace. Outlines, advances and names are unchanged. Same licence.\n")

if __name__ == "__main__":
    main()
