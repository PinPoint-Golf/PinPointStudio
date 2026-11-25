# PpCardNote

The app's quiet body line: an empty card saying so in one line, a page saying what it is for, an empty list saying why.

Source: `src/Gui/components/PpCardNote.qml`.

## When to use
- Empty states, page descriptions, settings paragraphs, setup page bodies (via PpPageHeader).
- Not for primary copy — that is body (13 px) in `colorText`.

## Consumer supplies
`text`, width (it word-wraps), optionally `kind` and `color`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| kind | string | "" | Leading: "" 1.3 · "plain" 1.0 · "aside" 1.0 italic · "list" 1.4 · "form" 1.5 · "setup" 1.65 |
(Plus Text's own `text`, `color`, `width`.)

## Tokens & values
Body face (`Theme.fontBody`), 12 px (fontSzBody2), `Theme.fontBodyWeight`, `colorText3`, word wrap. PpPageHeader's setup style recolours it `colorText2`.
