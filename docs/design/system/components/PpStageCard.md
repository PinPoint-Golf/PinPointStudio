# PpStageCard

A card for a session stage's panel: the coaching card's shell, tighter padding, and a body that fills (and clips to) the card rather than sizing it.

Source: `src/Gui/components/PpStageCard.qml`.

## When to use
- A stage panel given its size by the stage arrangement (a chart, a video, a table). Use PpCard when content should size the card.

## Consumer supplies
A size, the body content (fills the room; clipped), `title` or a `heading` component (tabs, a picker), optional `aside`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| tone | color | `colorText3` | Title and rule colour. |
| title | string | "" | Micro title, elided. |
| aside | string | "" | Micro, right of the first line, `trackingData`, ≤ 60 % width. |
| asideActive | bool | false | The aside is the selector: a "  ▾" after it, clickable. |
| asideOn | bool | false | Its picker is open (aside in `colorText`). |
| heading | Component | null | Replaces the title (tabs, a picker). |
| pad | int | gap(14) | Padding. |
| bodyGap | int | gap(10) | Heading row → body. |
Signal `asideClicked()`. Read-only `innerWidth`.

## States
Aside `colorText3`; active and hovered or open → `colorText`; hit area grows 4 px each side.

## Do / don't
- Do make an aside that names what is shown into the control that changes it, rather than saying it twice.
