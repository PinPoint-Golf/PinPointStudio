# Print

*Directory:* `src/Gui/print/`  
*1 component · 188 lines of QML*

PDF printing of paged registers.

**C++ in this area:** `OffscreenSheet`, `PdfDocumentWriter`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [PpPdfPrinter](#pppdfprinter) | 188 | `QtObject` | 1 | 0 |  |

### PpPdfPrinter
`src/Gui/print/PpPdfPrinter.qml` · 188 lines · root `QtObject` · no direct `Theme.*` refs

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
> `encoding` is the writer's: "lossless" (the default, every page Flate RGB) or "auto" (a palette
> page, photographs as JPEG — a third of the size; see pdf_document_writer.h). For "auto" a target
> that draws photographs says so per page with `readonly property bool printPageHasPhoto`, read
> once the page has settled; a target without it is taken to have none.
> A target may also set its own resolution page by page with `readonly property int printPageDpi`
> …

- **Properties:** `int dpi`, `int settleMs`, `string title`, `string author`, `string subject`, `string encoding`, `var grabber`, `readonly bool busy`
- **Signals:** `pageWritten(int index, int count)`, `finished(bool ok, string path, int pageCount, string error)`
- **Functions:** `printToPdf(target, path, landscape)`, `cancel()`
- **Used by (1):** `PpReportPrintHost`
- **Exercised by tests/tools:** `diag_print_pdf`, `print_mock`, `report_delivery`, `report_posture`, `report_wrist`
