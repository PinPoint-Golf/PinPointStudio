# PpCard

A card in one of two looks: the coaching card (shell, mono title in the card's tone, an aside, a padded column of content) or the table card (a hairline box of full-bleed rows on a shared column template).

Source: `src/Gui/components/PpCard.qml`.

## When to use
- Coaching card (default): anything a golfer reads — a focus, a summary, a verdict.
- Table card (`table: true`, §2.5 visual language): lists of areas, sessions, devices — rows of PpTableRow / PpExpandRow.

## Consumer supplies
Content as children (coaching: stacked with `itemGap`; table: rows stacked with no gap), `title`, optional `aside`, and for tables an optional `columns` template.

## Props
| name | type | default | meaning |
|---|---|---|---|
| tone | color | `colorAccent` | Title colour and the shell's rule. |
| title | string | "" | Mono micro title (PpMicro), spelled through Theme.caps. |
| aside | string | "" | Micro text on the title's baseline at the right, `trackingData`. |
| asideWidth | int | 0 | Right-align the aside over a column this wide. |
| pad | int | gap(20) | Coaching padding. |
| itemGap | int | gap(18) | Gap between children. |
| bodyGap | int | gap(18) | Title → body. |
| table | bool | false | The table card look. |
| columns | var | [] | `[{ title, width | fill, align }]`; any titled column draws a caption row. |
| columnGap | int | gap(16) | Between columns. |
Read-only: `innerWidth`, `titleItem`, `columnLayout`, `bodyItem`, `cornerRadius`, `headerHeight`; function `resolveColumns(cols)`.

## Tokens & values
- Coaching: PpCardShell (`colorSurface`, `colorBorderMid`, `cardRule`), title at pad + 2 px.
- Table: ground `colorBg2`, edge 1 px `colorBorder`, corner `tableRadius` (= `Theme.radiusLg`), no tone rule. Title band and caption band each `tableHeaderH` (38 px); captions are PpCaption (body face 12 px, `colorText3`, sentence case); `colorHairline` under the header and above every visible row but the first. Row padding `rowPadH` (18 px).

## Do / don't
- Do write captions in sentence case; quiet, never caps.
- Don't give a table card a tone rule — PpCardShell is not used there.
