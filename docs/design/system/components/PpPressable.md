# PpPressable

The standard PinPoint hover-grow / press-dip motion as a drop-in replacement for a MouseArea: it drives its parent's `scale`.

Source: `src/Gui/components/PpPressable.qml`.

## When to use
Inside any visually separated clickable element (tiles, shot cards, toolbar pills, buttons). Never on a contiguous segmented control — a scale breaks the shared seam; brighten those instead. Keep a hover brighten on the host and read `containsMouse` via the id.

## Consumer supplies
A parent item to scale, and `onClicked`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| hoverScale | real | 1.02 | Scale while hovered (1.0 opts out). |
| pressScale | real | 0.97 | Scale while pressed. |
| held | bool | false | Hold the grown state without hover (its popup is open). |
| moves | bool | true | false keeps hover and clicks but drops the motion. |
| confirmOnClick | bool | false | One-shot recoil (0.90 and back) on a completed click, for momentary actions. |
| (MouseArea) | | | `containsMouse`, `pressed`, `clicked`, `enabled`… |

## Tokens
`Theme.durationFast` (Theme.durationFast, 120 ms, OutCubic; the confirm dip takes half of it). Reduce-motion zeroes both.

## States
Rest 1.0 · hover 1.02 · held 1.02 · press 0.97 · disabled or `moves:false` 1.0. Scale is a render transform about the centre: it never reflows neighbours.
