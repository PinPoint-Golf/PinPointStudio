# Print

*Directory:* `src/Gui/print/`  
*1 component · 152 lines of QML*

PDF printing of paged registers.

**C++ in this area:** `PdfDocumentWriter`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpPdfPrinter](#pppdfprinter) | 152 | `QtObject` | 0 | 0 | ⚠ unreferenced |

### PpPdfPrinter
`src/Gui/print/PpPdfPrinter.qml` · 152 lines · root `QtObject` · ⚠ **no app file instantiates this** · no direct `Theme.*` refs

> PpPdfPrinter — prints a paged register to one PDF.
> `printToPdf(target, path[, landscape])` steps target.printPage from 0 to target.printPageCount - 1,
> grabs each page at `dpi` and hands it to a PdfDocumentWriter; `finished` reports the outcome.
> The target is any Item laid out as an A4 sheet at 96 dpi logical (794×1123, or 1123×794 for
> landscape) that exposes `printPage` (writable) and `printPageCount` — the contract of the
> Session diagnostics print register. It must be in a window, because grabToImage renders
> through the window's scene graph; it need not be on screen.
> grabToImage is asynchronous, so the run is a chain: set the page, let it settle for
> `settleMs` (the pager lays a page out over a frame or two), grab, add, next. The target's
> printPage is put back as it was at the end, whatever the outcome. No dialogs: the caller
> chooses the path and reads the result from `finished`.
> Not wired to any control yet — print is the last thing on the plan.

- **Properties:** `int dpi`, `int settleMs`, `string title`, `string author`, `readonly bool busy`
- **Signals:** `pageWritten(int index, int count)`, `finished(bool ok, string path, int pageCount, string error)`
- **Functions:** `printToPdf(target, path, landscape)`, `cancel()`
- **Exercised by tests/tools:** `diag_print_pdf`
