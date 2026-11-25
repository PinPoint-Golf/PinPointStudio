# PpFlowIndicator

The progress indicator of a multi-step flow: a row of numbered pips under group labels, joined by connectors, over a thin progress bar with "N of M done".

Hand-written from `src/Gui/components/PpFlowIndicator.qml` (static rendition — no grow/shrink animation, no collapsed-group or hover-tip states drawn).

It replaced the session wizard's tab strip and is generic: it knows nothing of session setup, so the toolbar calibration and the trunk ceremony reuse it (`compact`).

The consumer supplies the step `model`, the `groups` labels, `progress`, and optionally `canGoTo`; it handles `jumpRequested(key)`.

## Props

| name | type | default | meaning |
|---|---|---|---|
| `model` | var | `[]` | `[{ key, label, group, state, current, attention, number, tip }]` in flow order; state `pending` · `done` · `skipped` |
| `groups` | var | `{}` | group → label, drawn capitalised over each group's run |
| `progress` | var | `{passed:0,total:0}` | a skipped step counts as passed |
| `canGoTo` | function | null | key → bool; a done/skipped pip is clickable when yes |
| `compact` | bool | `false` | 56 px cells, 12 px pips, no group labels, 8 px glyphs |
| signal | | | `jumpRequested(key)` — future and current pips are inert |

## Geometry (full)

Cell 84 px · pip 16 px · group label row 14 px · label row 14 px · gaps 4 px · bar row 14 px, 8 px below the pips. Width = steps × cell; the row is centred.

## Pip states

| state | fill | border | glyph / label ink | glyph |
|---|---|---|---|---|
| current | `colorAccent` | `colorAccent` | `colorOnAccentBright` / `colorAccent` | number |
| done | `colorGoodLight` | `colorGood` | `colorGood` | ✓ |
| skipped | `colorWarnLight` | `colorWarn` | `colorWarn` | ⚠ |
| pending | `colorBg3` | `colorBorderMid` | `colorText3` | number |

- Attention (a visited step that needs attention): a `colorWarn` ring 6 px wider than the pip, 1.5 px stroke.
- Connectors: 1 px, `colorGood` once the earlier step of the pair is done, else `colorBorderMid`.
- Glyphs in the data face at micro (10 px); labels in the body face at micro, centred, eliding.
- Group labels: PpMicro `caps` kind, over the group's first pip.
- Progress: 2 px track `colorBorderMid`, fill `colorAccent`; "3 of 7 done" in the data face micro, `trackingData`, `colorText3`, 10 px right of the track.
- Narrow: groups other than the current one collapse to one pip with a count ("Cameras 3/3"); cells shrink, labels elide.
- Hover tip: `colorBg2` box, radius `Theme.radius`, `colorBorderMid` border (`colorWarn` when attention), micro body words in `colorText2`.
- Accessibility: each pip is named "<label>, <state>".
