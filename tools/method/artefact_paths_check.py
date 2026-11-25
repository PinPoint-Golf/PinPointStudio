#!/usr/bin/env python3
"""artefact_paths_check.py: fails when a tracked file still cites a governing document at the path it had before #195
moved it into governance/artefacts/ (governance/artefacts/README.md).

  python3 tools/method/artefact_paths_check.py        exit 0 clean, 1 with each stale citation listed, 77 without git

The moves: docs/developer/information_model.md, docs/design/system/, docs/design/pinpoint_sign_conventions.md,
docs/reference/golf_swing_normative_reference.md and docs/design/imu_frame_contract.md, all now in governance/artefacts/
(the sign conventions as sign_conventions.md, the normative reference as normative_reference.md). It reads every
tracked text file and fails on:
  - an old path, written out or as an os.path.join of its parts;
  - the two old file names that changed, even without a path (the other two names are unchanged, so a bare
    `information_model.md` or `imu_frame_contract.md` is still right);
  - in a markdown file, a relative link that resolves to an old home (e.g. `../developer/information_model.md`).
A dated record that must keep the old spelling is listed in KEPT with its reason; nothing else passes.
ctest: artefact_paths_check (src/Gui/tests/CMakeLists.txt).
"""
import os
import posixpath
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ME = 'tools/method/artefact_paths_check.py'   # it spells the old paths to find them

# (file, a substring of the line, why it keeps the old path)
KEPT = [
    ('governance/artefacts/information_model.md', '| 1.11.1 | 25 Nov 2026 |',
     'the change-log row that records the move names where the documents came from'),
    ('docs/implementation/swe-audit/06-refactor-report.md', 'git show origin/main:docs/developer/information_model.md',
     'a dated record: the command the count was taken with on 24 Nov 2026, at the commit it names'),
]

OLD_FILES = ('docs/developer/information_model.md', 'docs/design/pinpoint_sign_conventions.md',
             'docs/reference/golf_swing_normative_reference.md', 'docs/design/imu_frame_contract.md')
OLD_DIR = 'docs/design/system'
OLD = re.compile(
    r'docs/developer/information_model\.md'
    r'|docs/design/system(?![\w.-])'
    r'|docs/design/imu_frame_contract\.md'
    r'|pinpoint_sign_conventions'
    r'|golf_swing_normative_reference'
    r"""|['"]docs['"]\s*,\s*['"](?:developer['"]\s*,\s*['"]information_model\.md"""
    r"""|design['"]\s*,\s*['"](?:system|imu_frame_contract\.md)['"]"""
    r"""|reference['"]\s*,\s*['"]golf_swing_normative_reference\.md)""")
LINK = re.compile(r'\]\(<?([^)\s>]+)>?(?:\s+"[^"]*")?\)|^\s{0,3}\[(?!\^)[^\]]+\]:\s*(\S+)', re.M)
SCHEME = re.compile(r'^[A-Za-z][A-Za-z0-9+.-]*:')


def old_home(resolved):
    return resolved in OLD_FILES or resolved == OLD_DIR or resolved.startswith(OLD_DIR + '/')


def stale_lines(path, text):
    """(line number, what) for each stale citation in one file's text."""
    keep = [s for f, s, _ in KEPT if f == path]
    out = []
    for n, line in enumerate(text.split('\n'), 1):
        if any(s in line for s in keep):
            continue
        m = OLD.search(line)
        if m:
            out.append((n, 'old path ' + m.group(0)))
            continue
        if path.endswith('.md'):
            for lm in LINK.finditer(line):
                t = (lm.group(1) or lm.group(2) or '').split('#')[0]
                if not t or SCHEME.match(t) or t.startswith('/'):
                    continue
                r = posixpath.normpath(posixpath.join(posixpath.dirname(path), t))
                if old_home(r):
                    out.append((n, 'link to an old home ' + t))
    return out


def self_check():
    """The matcher on strings, every run: a check that passes everything proves nothing."""
    bad = ['docs/developer/information_model.md §10', 'see docs/design/system/README.md', '(docs/design/system, "D")',
           'pinpoint_sign_conventions.md rule 2', 'golf_swing_normative_reference.md §2',
           'docs/design/imu_frame_contract.md §5', "os.path.join('docs', 'developer', 'information_model.md')",
           'os.path.join(ROOT, "docs", "design", "system", "tokens.json")']
    good = ['governance/artefacts/information_model.md', 'information_model.md §5.11', 'imu_frame_contract.md §5',
            'docs/developer/information_model_walkthrough.pptx', 'docs/design/pinpoint_qml_design_system.md §15',
            'governance/artefacts/design_system/README.md', 'sign_conventions.md', 'normative_reference.md',
            'os.path.join(REPO, "docs", "developer", "analysis_dag.json")']
    fails = [s for s in bad if not stale_lines('x.txt', s)] + [s for s in good if stale_lines('x.txt', s)]
    links = {'docs/design/a.md': '[x](system/README.md)', 'docs/developer/a.md': '[x](information_model.md)',
             'docs/design/b.md': '[x](../reference/golf_swing_normative_reference.md#s2)'}
    fails += [k for k, v in links.items() if not stale_lines(k, v)]
    if stale_lines('governance/a.md', '[x](artefacts/information_model.md)'):
        fails.append('a link to the new home')
    return fails


def main():
    fails = self_check()
    if fails:
        print('artefact_paths_check: its own matcher is wrong on: %s' % fails)
        return 1
    try:
        out = subprocess.run(['git', '-C', ROOT, 'ls-files', '-z'], capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print('artefact_paths_check: skipped, no git checkout (%s)' % e)
        return 77
    files = [f for f in out.decode('utf-8', 'replace').split('\0') if f and f != ME]
    found, kept_used = [], set()
    for f in files:
        p = os.path.join(ROOT, f)
        if not os.path.isfile(p):
            continue                 # deleted in the working tree, not yet committed
        try:
            with open(p, encoding='utf-8') as fh:
                text = fh.read()
        except (UnicodeDecodeError, OSError):
            continue                 # binary
        for f2, s, _ in KEPT:
            if f2 == f and s in text:
                kept_used.add((f2, s))
        found += ['%s:%d: %s' % (f, n, w) for n, w in stale_lines(f, text)]
    missing = [f for f in ('governance/artefacts/information_model.md', 'governance/artefacts/design_system/README.md',
                           'governance/artefacts/sign_conventions.md', 'governance/artefacts/normative_reference.md',
                           'governance/artefacts/imu_frame_contract.md') if not os.path.isfile(os.path.join(ROOT, f))]
    unused = ['%s: %s' % (f, s) for f, s, _ in KEPT if (f, s) not in kept_used]
    for line in found:
        print(line)
    for f in missing:
        print('missing: %s' % f)
    for k in unused:
        print('a KEPT entry matches nothing (remove it): %s' % k)
    print('artefact_paths_check: %d tracked files, %d stale citations, %d kept by name'
          % (len(files), len(found), len(kept_used)))
    return 1 if found or missing or unused else 0


if __name__ == '__main__':
    sys.exit(main())
