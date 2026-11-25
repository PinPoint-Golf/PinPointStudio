# PpPageHeader

A page's head: its eyebrow, its display title and the quiet line that says what the page is for.

Source: `src/Gui/components/PpPageHeader.qml`.

## Styles
- `"settings"` (default): body-face caps eyebrow (PpMicro "body"), the title at its own width, description PpCardNote "plain"; `spacing` gap(20).
- `"screen"`: data-face eyebrow `eyebrowGap` (gap(10)) over the title, no spacing; the screen lays out its own lead.
- `"setup"`: eyebrow, title at min(20 px, display) wrapping, body PpCardNote "setup" in `colorText2`, a 1 px `colorBorderMid` rule under it; gap(8) apart.

## Props
| name | type | default | meaning |
|---|---|---|---|
| style | string | "settings" | See above. |
| eyebrow | string | "" | Spell with Theme.caps. |
| title | string | "" | PpDisplayText (gradient). |
| description | string | "" | Omitted when empty. |
| titlePixelSize | real | display (setup: min(sp(20), display)) | |
| rule | bool | style === "setup" | Hairline under the text. |
| flatTitle | bool | false | A plain display-font title in `titleColor` (Ready page: amber "Not quite ready"). |
| titleColor | color | `colorText` | Flat colour. |
| titleObjectName | string | "" | Probe name of whichever title is shown. |
| eyebrowGap | real | screen: gap(10), else 0 | |
