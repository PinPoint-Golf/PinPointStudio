# Plane-stack library check, 13 Nov 2026

The evidence for `docs/design/plane_stack_design.md` §2. It is read-only over the swing library at `/mnt/swingdata`. Nothing is re-analysed and nothing is written to the share.

To reproduce, run the steps in this folder, using `/opt/homebrew/bin/python3` with numpy:

    python3 extract.py swings.jsonl     # reads every swing.ppsw and diagnostics.json; one JSON line per swing
    python3 analyze.py                  # tables (stdout) and per_swing.csv
    python3 supplement.py               # elbow flexion, keypoint confidence, missing-reading reasons
    python3 extra.py                    # coupling and reliability checks

- `RESULTS.md` holds the full results.
- `per_swing.csv` is the per-swing table as it stood on 13 Nov, after the ab99dc53 sweep.
