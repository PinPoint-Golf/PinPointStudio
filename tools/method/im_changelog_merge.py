#!/usr/bin/env python3
"""im_changelog_merge.py — resolves the information model's changelog clashes when one lane's change is rebased onto
another's: both lanes added a changelog row with the same next version, so git leaves a conflict block.

  python3 tools/method/im_changelog_merge.py governance/artefacts/information_model.md    (mid-rebase, then git add it)
  python3 tools/method/im_changelog_merge.py --self-test                             proves it on fixtures in a temp dir

Each conflict block whose two sides are each a single changelog row ("| 1.N.M | ...") keeps both rows: the first
side's as it is, then the second side's renumbered to the next free version (one past the highest 1.N.M anywhere in
the file, counting both sides). Any other conflict block is left in place, markers and all, and counted. The file is
rewritten in place either way. Prints "unresolved: <count>"; exit 0 when every block was resolved, 1 otherwise.
ctest: im_changelog_merge_selftest (src/Gui/tests/CMakeLists.txt).
"""
import os
import re
import subprocess
import sys
import tempfile


def merge(lines):
    vers = [tuple(map(int, m.groups())) for l in lines for m in [re.match(r'^\| 1\.(\d+)\.(\d+) \|', l)] if m]
    nxt = max(vers) if vers else (9, 0)
    out, i, left = [], 0, 0
    while i < len(lines):
        if lines[i].startswith('<<<<<<< '):
            j = lines.index('=======', i)
            k = next(x for x in range(j, len(lines)) if lines[x].startswith('>>>>>>> '))
            ours, theirs = lines[i + 1:j], lines[j + 1:k]
            if len(ours) == 1 and len(theirs) == 1 and ours[0].startswith('| 1.') and theirs[0].startswith('| 1.'):
                nxt = (nxt[0], nxt[1] + 1)
                out += [ours[0], re.sub(r'^\| 1\.\d+\.\d+ \|', f'| 1.{nxt[0]}.{nxt[1]} |', theirs[0])]
            else:
                out += lines[i:k + 1]
                left += 1
            i = k + 1
        else:
            out.append(lines[i])
            i += 1
    return out, left


RESOLVABLE = """| Version | Change |
|---|---|
| 1.9.40 | the base row |
<<<<<<< HEAD
| 1.9.41 | lane one's row |
=======
| 1.9.41 | lane two's row |
>>>>>>> 1234abc (lane two: its change)
| 1.9.39 | an older row |
<<<<<<< HEAD
| 1.9.42 | lane one's second row |
=======
| 1.9.41 | lane two's second row |
>>>>>>> 5678def (lane two: another change)
"""

RESOLVED = """| Version | Change |
|---|---|
| 1.9.40 | the base row |
| 1.9.41 | lane one's row |
| 1.9.43 | lane two's row |
| 1.9.39 | an older row |
| 1.9.42 | lane one's second row |
| 1.9.44 | lane two's second row |
"""

MIXED = """| 1.9.7 | the base row |
<<<<<<< HEAD
| 1.9.8 | lane one's row |
=======
| 1.9.8 | lane two's row |
>>>>>>> 1234abc (lane two)

<<<<<<< HEAD
A prose line lane one changed.
=======
A prose line lane two changed.
>>>>>>> 1234abc (lane two)
"""


def self_test():
    fails = []

    def check(cond, what):
        print(('ok   - ' if cond else 'FAIL - ') + what)
        if not cond:
            fails.append(what)

    me = os.path.abspath(__file__)
    with tempfile.TemporaryDirectory(prefix='im_changelog_merge_selftest.') as d:
        p = os.path.join(d, 'information_model.md')
        with open(p, 'w') as f:
            f.write(RESOLVABLE)
        r = subprocess.run([sys.executable, '-I', me, p], capture_output=True, text=True, cwd=d)
        got = open(p).read()
        check(r.returncode == 0 and 'unresolved: 0' in r.stdout,
              'two row clashes: exit 0, unresolved 0 (got exit %d, %r)' % (r.returncode, r.stdout.strip()))
        check(got == RESOLVED, 'both rows kept, the second side\'s renumbered past the highest version (1.9.43, 1.9.44)')
        check('<<<<<<<' not in got and '=======' not in got and '>>>>>>>' not in got, 'no conflict markers remain')

        with open(p, 'w') as f:
            f.write(MIXED)
        r = subprocess.run([sys.executable, '-I', me, p], capture_output=True, text=True, cwd=d)
        got = open(p).read()
        check(r.returncode == 1 and 'unresolved: 1' in r.stdout,
              'a prose clash beside a row clash: exit 1, unresolved 1 (got exit %d, %r)' % (r.returncode, r.stdout.strip()))
        check('| 1.9.8 | lane one\'s row |\n| 1.9.9 | lane two\'s row |' in got, 'the row clash is still resolved (1.9.9)')
        check('<<<<<<< HEAD\nA prose line lane one changed.\n=======\nA prose line lane two changed.\n>>>>>>> 1234abc (lane two)'
              in got, 'the prose clash is left in place, markers and all')
    print('self-test: %s' % ('all cases hold' if not fails else '%d FAILED' % len(fails)))
    return 1 if fails else 0


def main(argv):
    if argv == ['--self-test']:
        return self_test()
    if len(argv) != 1 or argv[0].startswith('-'):
        print('usage: im_changelog_merge.py <path to information_model.md> | --self-test', file=sys.stderr)
        return 2
    p = argv[0]
    with open(p) as f:
        lines = f.read().split('\n')
    out, left = merge(lines)
    with open(p, 'w') as f:
        f.write('\n'.join(out))
    print(f'unresolved: {left}')
    return 1 if left else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
