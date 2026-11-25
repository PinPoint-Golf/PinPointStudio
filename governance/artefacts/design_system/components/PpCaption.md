# PpCaption

A caption: the quiet sentence-case line a table card heads its columns with, and a row's own quiet words under its headline.

Source: `src/Gui/components/PpCaption.qml`.

## Props
Text's own (`text`, `horizontalAlignment`, `width`). One line, elided right; to wrap, set `elide: Text.ElideNone` and a `wrapMode`.

## Tokens & values
Body face, 12 px (fontSzBody2), `Theme.fontBodyWeight`, `colorText3`. Sentence case — never set captions in capitals.
