# PpPips

A row of session pips (`PpPip`), oldest first.

Source: `src/Gui/components/PpPips.qml`.

**Use** in a table row or summary to show, session by session, where a pattern was seen. A card that lines its rows up sizes its pip column to the widest row.

The consumer supplies `marks` (seen/unseen bools) or `ticks` (`{ state }` with `fired` / `clean` / `notAssessable`); ticks win when there are any.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `marks` | var | `[]` | summary bools |
| `ticks` | var | `[]` | PpTickRun states |
| `tone` | color | `colorText3` | seen / fired dots |
| `cleanTone` | color | `colorGood` | clean dashes |
| `size` | int | `sp(pipSize)` = 6 px | pip size |
| `gap` | int | `sp(pipGap)` = 3 px | between pips |
| `maxPips` | int | `0` | keep the latest N (0 = all) |

Tokens: `pipSize`, `pipGap`, `colorText3`, `colorGood`, `colorBorderStrong`.
