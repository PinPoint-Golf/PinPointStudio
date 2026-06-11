#!/usr/bin/env python3
# SwingLab CLI (docs/implementation/SWINGLAB_IMPL.md).
#
#   lab.py synth   <out_dir> [--clutter] [--seed N]   ground-truthed synthetic swing
#   lab.py ingest  <corpus_root>                      build corpus.json
#   lab.py run     <corpus_root> <runs_root> [--params f] [--id NAME] [--no-trace]
#   lab.py one     <swing_dir>  <run_dir>   [--params f] [--no-trace]  (run+score+plot)
#   lab.py score   <run_dir>    <swing_dir>            scorecard for one run
#   lab.py plot    <run_dir>    <swing_dir>            contact sheet PNG
#   lab.py report  <run_root>                          regenerate REPORT.md
#   lab.py diff    <run_a> <run_b>                     regression diff
#   lab.py sweep   <corpus_root> <runs_root> <space.json> [--trials N]
#   lab.py label   <swing_dir> [--every N]             hand-label truth.json
#
# Run with the SwingLab venv:  ~/.swinglab-venv/bin/python lab.py …
# Binary override: SWINGLAB_BIN=/path/to/swinglab_run

import argparse
import json
import sys
from pathlib import Path

# Windows consoles default to cp1252 — reconfigure so unicode in reports
# (em-dashes, arrows) never crashes the tooling.
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, str(Path(__file__).parent))

from swinglab import save_json                              # noqa: E402
from swinglab.core import diff, ingest, report, run_corpus, run_one, sweep  # noqa: E402
from swinglab.score import scorecard                        # noqa: E402


def main():
    ap = argparse.ArgumentParser(prog="lab.py")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("synth")
    p.add_argument("out_dir")
    p.add_argument("--clutter", action="store_true")
    p.add_argument("--seed", type=int, default=7)

    sub.add_parser("doctor")

    p = sub.add_parser("ingest")
    p.add_argument("corpus_root")

    p = sub.add_parser("run")
    p.add_argument("corpus_root")
    p.add_argument("runs_root")
    p.add_argument("--params")
    p.add_argument("--id")
    p.add_argument("--no-trace", action="store_true")

    p = sub.add_parser("one")
    p.add_argument("swing_dir")
    p.add_argument("run_dir")
    p.add_argument("--params")
    p.add_argument("--no-trace", action="store_true")

    p = sub.add_parser("score")
    p.add_argument("run_dir")
    p.add_argument("swing_dir")

    p = sub.add_parser("plot")
    p.add_argument("run_dir")
    p.add_argument("swing_dir")

    p = sub.add_parser("report")
    p.add_argument("run_root")

    p = sub.add_parser("diff")
    p.add_argument("run_a")
    p.add_argument("run_b")

    p = sub.add_parser("sweep")
    p.add_argument("corpus_root")
    p.add_argument("runs_root")
    p.add_argument("space")
    p.add_argument("--trials", type=int, default=20)

    p = sub.add_parser("label")
    p.add_argument("swing_dir")
    p.add_argument("--every", type=int, default=20)

    a = ap.parse_args()

    if a.cmd == "doctor":
        from swinglab.core import doctor
        sys.exit(0 if doctor() else 1)
    elif a.cmd == "synth":
        from swinglab.synth import generate
        generate(a.out_dir, seed=a.seed, clutter=a.clutter)
    elif a.cmd == "ingest":
        ingest(a.corpus_root)
    elif a.cmd == "run":
        run_corpus(a.corpus_root, a.runs_root, run_id=a.id, params=a.params,
                   trace=not a.no_trace)
    elif a.cmd == "one":
        ok, rd = run_one(a.swing_dir, a.run_dir, params=a.params,
                         trace=not a.no_trace)
        card = scorecard(rd, a.swing_dir)
        save_json(Path(rd) / "scorecard.json", card)
        print(json.dumps({k: card[k] for k in
                          ("swing", "score", "ok", "failures", "warnings")}, indent=1))
        from swinglab.plots import contact_sheet
        png = contact_sheet(rd, a.swing_dir, Path(rd) / "contact_sheet.png")
        print(f"[plot] {png}")
        sys.exit(0 if ok else 2)
    elif a.cmd == "score":
        card = scorecard(a.run_dir, a.swing_dir)
        save_json(Path(a.run_dir) / "scorecard.json", card)
        print(json.dumps(card, indent=1))
    elif a.cmd == "plot":
        from swinglab.plots import contact_sheet
        print(contact_sheet(a.run_dir, a.swing_dir,
                            Path(a.run_dir) / "contact_sheet.png"))
    elif a.cmd == "report":
        report(a.run_root)
    elif a.cmd == "diff":
        sys.exit(1 if diff(a.run_a, a.run_b) else 0)
    elif a.cmd == "sweep":
        sweep(a.corpus_root, a.runs_root, a.space, trials=a.trials)
    elif a.cmd == "label":
        from swinglab.label import label
        label(a.swing_dir, every_n=a.every)


if __name__ == "__main__":
    main()
