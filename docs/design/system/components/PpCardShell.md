# PpCardShell

The coaching card's background and nothing else: the surface, a hairline, and a rule in the card's tone along the top edge that follows the rounded corners.

Source: `src/Gui/components/PpCardShell.qml`.

## When to use
- Behind any coaching card (PpCard, PpStageCard and PpPopoverCard all use it). Use it directly only when you lay your own content over it.
- Not for table cards — `PpCard { table: true }` draws its own colorBg2 box with no rule.

## Consumer supplies
Size (it fills its parent) and content laid over it; `tone` for the rule.

## Props
| name | type | default | meaning |
|---|---|---|---|
| tone | color | `colorAccent` | Rule colour (and the hero's wash/hairline). |
| hero | bool | false | The one card that matters: 4 px rule, tinted wash. |
| floating | bool | false | A popover: hairline becomes `colorBorderStrong`. |

## Tokens & values
- Surface `colorSurface`, corner `Theme.radiusLg` (Theme.radiusLg), 1 px `colorBorderMid` hairline (`colorBorderStrong` floating).
- Rule height `cardRule` (3 px) / `heroRule` (4 px); the rule bends with the corners. A rule of 0 draws nothing.
- Hero (when the theme's heroTinted): no surface hairline; a wash of the tone at 5 % (4.5 % dark) and a tone hairline at 36 % (32 % dark).

## Do / don't
- Do let the tone carry meaning (good / warn / accent); the card body stays neutral.
- Don't square the rule off with a plain bar — it must follow the corners.
