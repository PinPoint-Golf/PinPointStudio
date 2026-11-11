# PpMenuItem

One line of a popup menu: a 34 px hover row the menu's width, with its words, an optional quiet note, a leading glyph and a right-hand count; inactive or destructive.

Source: `src/Gui/components/PpMenuItem.qml`. Glyphs in the preview stand in for the platform symbol font (`Theme.fontSymbol`).

## When to use
- Inside `Popup { background: PpPopoverCard {} }` menus (Sessions row ⋯, shot action bar). Group a destructive line after a PpMenuRule.
- `style: "compact"` for the model editor's ⋯ drawer rows.

## Props
| name | type | default | meaning |
|---|---|---|---|
| text | string | "" | The words. |
| note | string | "" | Quiet words after: "· why not". |
| glyph | string | "" | Leading symbol (fontSymbol, 13 px). |
| tail | string | "" | Right-aligned count, micro face, `trackingData`; the words wrap short of it, a gap(10) clear. |
| active | bool | true | Inactive: greyed, no press. |
| warn | bool | false | Destructive: warn ink and warn hover wash. |
| linkNote | bool | false | The note is the fix: `colorAccent`. |
| fontWeight | int | fontBodyWeight | The words' weight. |
| style | string | "" | "" or "compact". |
Signal `triggered()`. Read-only `contentImplicitWidth`: the glyph and the gap after it, the words, and the count `tail` with the same gap(10) before it — no margins. A menu sized to its lines takes the largest over all of them and adds the margins, so no line's words run into its count. Default style only; a compact row is sized by its layout and does not use it.

## Tokens & values
- Default: height max(34, words + 14) px, `Theme.radius`, gap(10) margins and spacing; words body 13 px `colorText`; glyph `colorText2`; hover `colorBg2` fading over `durationFast`. Inactive: `colorText3`. Warn: `colorWarn` (50 % when inactive), hover `colorWarnLight`, tail at 0.7 opacity.
- Compact: 30 px, square, gap(14) margins, 12 px words elided, tail in the data face 10 px, `colorBg2` hover without fade, destructive in `colorError` (tail 0.75).
