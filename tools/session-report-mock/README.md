# Session report mock

The mock that the session report design was judged on (`docs/design/session_report_design.md`), kept as working
tooling for the build:

- `assemble.py` is a **prototype of `SessionReportModel`**: it turns one session's diagnostics into the single
  `report` map the report binds to, with every sentence generated from a template over the model and the
  diagnostics library (`src/Resources/diagnostics`). Content the library does not hold yet is in the `PROPOSED*`
  tables at its top. They are first drafts for the content phase, not finished wording.
- `SessionReportMock.qml` is the mock register: fixed A4 pages on the app's print roles, printed by
  `print_mock.qml` through the app's own `PpPdfPrinter`. The build replaces it with `PpSessionReport` on
  `PpPager` under the design's §9 layout contract.
- `overlay.py` makes the stills: the frame at a swing position, with the measured body line drawn on it.

## Run it

Needs the Debug app at `build/Qt_6_11_1_for_macOS_Debug`, the share mounted, ffmpeg, and Pillow
(`/opt/homebrew/bin/python3`). Output goes to `/mnt/swingdata/scratch/session-report/`, never the repo.

```sh
# 1. dump the diagnostics model for a session (from a scratch copy: loading a session writes into it)
tools/session-report-mock/dump-session.sh /mnt/swingdata/Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03 \
    /mnt/swingdata/scratch/session-report/dumps/jess03.json
# 2. assemble and print; checks every page for text running into the footer
tools/session-report-mock/render-session.sh jess /mnt/swingdata/Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03 \
    /mnt/swingdata/scratch/session-report/dumps/jess03.json "optional coach's note"
```

The dumps used on 20 October 2026 (Nick Henn and Jess Ratcliffe, 20 October, session 3) are kept in
`/mnt/swingdata/scratch/session-report/dumps/`.

## Known limits (the design says how the build differs)

- The pages are fixed boxes, so long content can overflow; `overflow_check.py` catches it. The build flows.
- The swing grid's watch-list and ball rows are capped at 3 to fit a fixed page; the design's cap is 4.
- Stills come from ffmpeg, with no exposure correction; the build uses the replay's frame source.
- The studio, the coach and the sample note are placeholders.
- The work-on order mimics `reduceSessionWorkOns` for the range-restricted case (no link is supported). It does not
  run the Fisher test.
