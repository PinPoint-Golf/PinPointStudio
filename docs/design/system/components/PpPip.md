# PpPip

One session pip: a dot where a session saw something, an empty ring where it did not — or, in tick-run terms, a dot where it fired, a dash where it was clean, a ring where it could not tell.

Source: `src/Gui/components/PpPip.qml`.

**Use** inside `PpPips`; use alone only for a single-session mark. Two vocabularies share it:

- `seen` / `unseen` — the summary's: a dot in `tone`, or an empty `colorBorderStrong` ring.
- `fired` / `clean` / `notAssessable` — PpTickRun's: a dot in `tone`; a 2 px-high dash in `cleanTone`; a 1 px `colorText3` ring — present, never a gap.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `kind` | string | `"seen"` | one of the five above |
| `tone` | color | `colorText3` | seen / fired dot |
| `cleanTone` | color | `colorGood` | the clean dash |
| `size` | int | `sp(pipSize)` = 6 px | width and height |

The dash is centred on the dot's line, so a mixed row keeps one centre.
