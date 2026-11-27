#!/usr/bin/env python3
"""requirements_table.py: one reader of governance/artefacts/requirements.md, the tests' requirement labels and the
live GitHub issues, shared by the traceability report (make_rtm.py) and the growth chart's readiness panel
(tools/history/loc_history.py).

Nothing here writes anything. The issues are read live with `gh`; read_issues() returns None when gh can't say, and
callers then leave the bug columns out rather than guess.
"""
import json
import re
import subprocess
from pathlib import Path

STATUSES = ("done", "partial", "planned", "proposed", "won't have")
RELEASES = ("v1", "v2", "later", "never")
MOSCOW = ("Must", "Should", "Could")
REQ_ID = re.compile(r"\bREQ-[A-Z]+-\d+\b")
ISSUE = re.compile(r"#(\d+)")
REPO = "PinPoint-Golf/PinPointStudio"


def _cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def read_requirements(root):
    """Every live requirement row, in page order: dicts with id, area, text, status (one of STATUSES), raw_status,
    release, moscow, to_confirm, evidence and issues (the issue numbers its status and evidence cite)."""
    rows, area = [], None
    path = Path(root) / "governance/artefacts/requirements.md"
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            area = line[3:].strip()
            if area.lower().startswith("non-functional: "):
                area = area[len("non-functional: "):].capitalize()
            continue
        if not line.startswith("| REQ-"):
            continue
        c = _cells(line)
        if len(c) != 9:               # the withdrawn table: id and a reason only
            continue
        rid, _owner, text, _why, _sources, status, release, moscow, evidence = c
        base = status.split("·")[0].split("(")[0].split("[^")[0].strip()
        if base not in STATUSES:
            raise ValueError(f"{rid}: status '{status}' is not one of {', '.join(STATUSES)}")
        rows.append(dict(id=rid, area=area, text=re.sub(r"\[\^[^\]]*\]", "", text), status=base, raw_status=status,
                         release=release, moscow=moscow if moscow in MOSCOW else "",
                         to_confirm="to confirm" in status, evidence=evidence,
                         issues=sorted({int(n) for n in ISSUE.findall(status + " " + evidence)})))
    return rows


def read_test_labels(root):
    """{REQ id: sorted test names}: every pp_requirement(<test> REQ-...) call under the source tree, and every
    `// REQ:` line in a QML test file (its test is qml_ui.<file name>)."""
    root = Path(root)
    labels = {}
    files = [root / "CMakeLists.txt"] + [p for d in ("src", "tests", "cmake") for p in (root / d).rglob("*")
                                          if p.name == "CMakeLists.txt" or p.suffix == ".cmake"]
    call = re.compile(r"^\s*pp_requirement\((\S+)\s+([^)]*)\)", re.M)
    for f in files:
        for test, ids in call.findall(f.read_text(encoding="utf-8", errors="replace")):
            for rid in REQ_ID.findall(ids):
                labels.setdefault(rid, set()).add(test)
    for f in (root / "src/Gui/tests/qml").glob("tst_*.qml"):
        for line in f.read_text(encoding="utf-8", errors="replace").splitlines():
            if re.match(r"^\s*//\s*REQ:", line):
                for rid in REQ_ID.findall(line):
                    labels.setdefault(rid, set()).add(f"qml_ui.{f.stem}")
    return {k: sorted(v) for k, v in labels.items()}


def read_issues(repo=REPO):
    """{number: dict(state, title, labels, bug, reqs)} for every issue, or None when gh can't say. `reqs` are the
    ids on the issue's `Requirement:` lines."""
    try:
        out = subprocess.run(["gh", "issue", "list", "-R", repo, "--state", "all", "--limit", "5000",
                              "--json", "number,state,title,labels,body"],
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    issues = {}
    for it in json.loads(out):
        reqs = set()
        for line in (it.get("body") or "").splitlines():
            if re.match(r"^\W*Requirements?:", line):
                reqs.update(REQ_ID.findall(line))
        labels = [l["name"] for l in it["labels"]]
        issues[it["number"]] = dict(state=it["state"], title=it["title"], labels=labels, bug="bug" in labels,
                                    reqs=reqs)
    return issues


def link_issues(rows, issues):
    """Adds open_bugs and open_other (open issue numbers) to each row: the issues its row cites, plus those whose
    Requirement: line names it. A row with an open bug is blocked."""
    named = {}
    for n, it in (issues or {}).items():
        for rid in it["reqs"]:
            named.setdefault(rid, set()).add(n)
    for r in rows:
        nums = set(r["issues"]) | named.get(r["id"], set())
        live = [n for n in sorted(nums) if issues and n in issues and issues[n]["state"] == "OPEN"]
        r["open_bugs"] = [n for n in live if issues[n]["bug"]]
        r["open_other"] = [n for n in live if not issues[n]["bug"]]
    return rows


def column(row):
    """The readiness column a row falls in: 'v1 Must', 'v1 Should', 'v1 Could', 'v2', or None (later, never)."""
    if row["release"] == "v1" and row["moscow"]:
        return f"v1 {row['moscow']}"
    return "v2" if row["release"] == "v2" else None
