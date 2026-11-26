#!/usr/bin/env python3
"""make_rtm.py — writes the requirements traceability matrix, governance/artefacts/rtm.md, and checks it.

  python3 tools/method/make_rtm.py                    write rtm.md from the committed inputs
  python3 tools/method/make_rtm.py --check            exit 1 when rtm.md would change, or on a flag that is neither an
                                                      accepted gap nor in the baseline, or on a baseline line whose
                                                      gap has closed (ctest: rtm_check)
  python3 tools/method/make_rtm.py --init             write the baseline (today's gaps) and rtm.md; refused when a
                                                      baseline exists, unless --force-init is given
  python3 tools/method/make_rtm.py --rename <old> <new>
                                                      a mechanical rename of an unlabelled test (BT-39: it needs no
                                                      new label): swap the baseline's "4 <old>" line for "4 <new>"
                                                      and rewrite rtm.md; refused, with the reason, unless <old> is
                                                      in the baseline and registered nowhere, and <new> is
                                                      registered, unlabelled and not in the baseline
  python3 tools/method/make_rtm.py --refresh-issues --library <the library folder>
                                                      rewrite the issue snapshot from GitHub (`gh`; the only mode that
                                                      uses the network; it writes nothing to GitHub), titles included;
                                                      it writes nothing, and names each issue, when a title it would
                                                      keep names a golfer (an athlete folder's or a session folder's
                                                      name, read from the library, never written or printed: POL-7)
  python3 tools/method/make_rtm.py --platform-list <configured ctest tree> <platform>
                                                      write governance/artefacts/rtm_platform_tests/<platform>.txt, the
                                                      tests that tree configured (`ctest --show-only=json-v1`)
  python3 tools/method/make_rtm.py --self-test        prove every flag, red then green, on fixtures in a temporary
                                                      directory (ctest: rtm_selftest)
  --root <dir>                                        read and write another checkout (default: the one this script
                                                      lives in)

Design: governance/artefacts/test_strategy.md §8 and the requirements notes' phase 2. It reads committed source files
only, never a configured build tree, so the matrix is the same on every machine:
  - governance/artefacts/requirements.md (its requirement tables; the "Withdrawn before approval" table), uc-map.csv,
    rtm_observations.csv, rtm_issues.json (the issue snapshot), rtm_accepted_gaps.txt, rtm_baseline.txt,
    rtm_kind_defaults.csv and rtm_platform_tests/<platform>.txt;
  - the test registrations, grepped as text whatever guards surround them: every pp_add_test(...) and add_test(...) in
    the root CMakeLists.txt and the CMakeLists.txt and *.cmake files under cmake/, src/ and tests/; every
    pp_requirement(<test> <ID>...) call; every `// REQ:` line in src/Gui/tests/qml/tst_*.qml;
  - governance/controls/ (which CTL- ids exist, and which are retired); docs/design/ (which design documents exist).
A test name built in a loop is expanded when the foreach list is literal in the same file (`foreach(v a b)`,
`foreach(v IN ITEMS ...)`, `foreach(v IN LISTS l)` over a literal set(), `foreach(v RANGE ...)`, or a file(GLOB) of
${CMAKE_CURRENT_SOURCE_DIR} files with get_filename_component); a name it cannot resolve that way must be listed in
tools/method/rtm_loop_tests.txt, which must agree with the source; any other variable fails, naming the file and line.
Registrations inside function() and macro() bodies are the helpers themselves, and are skipped.

The flags, in this order (rtm.md and the self-test use the same numbers):
   1 rtm.md is stale.
   2 An id cited anywhere (a pp_requirement call, a `// REQ:` line, an issue's Requirement: or Control: line, the gap
     files, uc-map.csv, the observations) does not exist, or is withdrawn (a retired control, for a CTL- id).
   3 A delivered, built or partial row without evidence of every kind it needs (one gap per missing kind).
   4 A test with no requirement or control label.
   5 An open enhancement with no Requirement: line, or one naming an unknown id.
   6 An open bug naming a delivered row: the row is failing.
   7 A planned row whose issues are all closed.
   8 A row that needs a ruling change (its status says "proposed, for the Solution Architect") but is marked built or
     delivered.
   9 A test labelled for a requirement but in no platform's gate list (only when at least one list exists).
  10 A hardware row (one that needs E6) never observed: delivered, built or partial rows.
  11 The evidence supports a different status from requirements.md's (delivered against not delivered, either way).
  12 A row not delivered, with no open issue and no accepted gap (exempt: release never, won't have, withdrawn).
  13 A baseline line whose gap has closed: the line must come out, so the baseline only shrinks.
An observation counts only when its "What was seen" starts with "pass". A corpus line (Kind E3, Record "corpus:
<session date> shot <n>[ at <HH:MM> UTC][, captured on <commit>]") counts for E6 as well, never ages, and an open bug
naming its row shows beside it (flag 6) without removing it. The time of day is required, on each, where two swings
(two observation ids) share a date and a shot number, and must then tell them apart; the build is given only when the
swing records one.
Warnings never fail the check: a proposed row with no issue; a design document no row cites; an open `engineering`
issue with neither a Requirement: nor a Control: line; an issue a row cites that the snapshot lacks; an accepted gap
that is no longer open; a gate-list test the source does not register.

The page, in this order: the summary; the requirements, area by area (each row's words, status, evidence present and
missing in words, tests, observations, issues with titles, and its corpus swings as one line: the count, then the
count by date, never a per-swing list or a build); the flags, in words, and the warnings; the controls, the labelled
tests and the documents; every gap, flag by flag, its baseline key in small print. Every requirement id carries its
Requirement sentence (cut to about 12 words, at a word boundary, with "…"), every control id its first sentence,
every issue number its title; text from the inputs is escaped with HTML entities, never a backslash. The wording
never changes a gap's key, so the baseline does not change.
The matrix never overrides requirements.md: where the evidence supports another status it raises flag 11, and the
owner of requirements.md changes it. Pass or fail is ctest's report, not the matrix's. Output is byte-identical on every
platform: sorted, LF, no dates or times, no absolute paths. Python 3 standard library only; exit 0 clean, 1 flags or
stale, 2 an input it cannot read (the message names the file and line).
"""
import csv
import fnmatch
import io
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile

ART = 'governance/artefacts'
P_REQ = ART + '/requirements.md'
P_UC = ART + '/uc-map.csv'
P_OBS = ART + '/rtm_observations.csv'
P_ISSUES = ART + '/rtm_issues.json'
P_ACCEPTED = ART + '/rtm_accepted_gaps.txt'
P_BASELINE = ART + '/rtm_baseline.txt'
P_PLATFORMS = ART + '/rtm_platform_tests'
P_KINDS = ART + '/rtm_kind_defaults.csv'
P_RTM = ART + '/rtm.md'
P_LOOPS = 'tools/method/rtm_loop_tests.txt'
P_CONTROLS = 'governance/controls'
P_DESIGN = 'docs/design'
P_QML = 'src/Gui/tests/qml'
SCAN_FILES = ['CMakeLists.txt']
SCAN_DIRS = ['cmake', 'src', 'tests']
KNOWN_PLATFORMS = ['linux', 'macos', 'windows']
KINDS = ['E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E7']
OBS_KINDS = ['E3', 'E4', 'E5', 'E6', 'E7']
OBS_HEADER = ['Id', 'Date', 'Requirements', 'Build', 'Kind', 'What was done', 'What was seen', 'Record', 'Observed by']
REQ_COLS = ['Id', 'Owner', 'Requirement', 'Why', 'Sources', 'Status', 'Release', 'MoSCoW', 'Evidence']
WITHDRAWN_SECTION = 'Withdrawn before approval'
ROLES = ['Product Owner', 'Solution Architect']

REQ_ID = r'REQ-[A-Z]+-[0-9]+'
CTL_ID = r'CTL-[A-Z]+-[0-9]+[a-z]?'
LABEL_RE = re.compile(r'^(?:' + REQ_ID + '|' + CTL_ID + r')$')
LABEL_FIND = re.compile(r'(?<![A-Za-z0-9-])(' + REQ_ID + '|' + CTL_ID + r')(?![A-Za-z0-9-])')
REQ_FIND = re.compile(r'(?<![A-Za-z0-9-])(' + REQ_ID + r')(?![A-Za-z0-9-])')
ISSUE_REF = re.compile(r'(?<![\w&/])#([0-9]+)\b')
QML_REQ_LINE = re.compile(r'^\s*//\s*REQ:(.*)$')
# A corpus swing replayed (Kind E3) also meets E6, in every area, with no build or age limit (test_strategy.md §3, §4;
# the Solution Architect, 26 Nov 2026). Its Record names the session date and the shot number; the time of day (UTC)
# only where two swings share both; the build that captured the swing only when the swing records one ("evidence
# regardless of the build": the Solution Architect, 26 Nov 2026); and nothing else (POL-7: never a golfer).
CORPUS_RECORD = re.compile(r'^corpus: ([0-9]{4}-[0-9]{2}-[0-9]{2}) shot ([0-9]+)'
                           r'(?: at ((?:[01][0-9]|2[0-3]):[0-5][0-9]) UTC)?(?:, captured on ([0-9a-f]{7,40}))?$')

FLAG_TITLES = {
    1: 'rtm.md is stale',
    2: 'An id cited anywhere does not exist, or is withdrawn',
    3: 'A delivered, built or partial row without evidence of every kind it needs',
    4: 'A test with no requirement or control label',
    5: 'An open enhancement with no Requirement: line, or one naming an unknown id',
    6: 'An open bug naming a delivered row: the row is failing',
    7: 'A planned row whose issues are all closed',
    8: 'A row that needs a ruling change but is marked built or delivered',
    9: "A test labelled for a requirement but in no platform's gate list",
    10: 'A hardware row never observed',
    11: "The evidence supports a different status from requirements.md's",
    12: 'A row not delivered, with no open issue and no accepted gap',
    13: 'A baseline line whose gap has closed',
}
BASE_WORDS = {'delivered': 'delivered', 'built': 'built, not verified', 'partial': 'partial', 'planned': 'planned',
              'proposed': 'proposed', 'wont': "won't have"}
STATUS_BASES = [('delivered', 'delivered'), ('built, not verified', 'built'), ('partial', 'partial'),
                ('planned', 'planned'), ('proposed, for the solution architect', 'proposed'),
                ('proposed', 'proposed'), ("won't have", 'wont')]
RULING_MARKER = 'proposed, for the solution architect'


class RtmError(Exception):
    """An input the tool cannot read; the message names the file (and line)."""


def nat_key(s):
    return [(0, int(t), '') if t.isdigit() else (1, 0, t) for t in re.split(r'([0-9]+)', s)]


def nat_sorted(items):
    return sorted(items, key=nat_key)


def read_text(root, rel):
    p = os.path.join(root, *rel.split('/'))
    if not os.path.isfile(p):
        return None
    with open(p, encoding='utf-8', newline='') as f:
        t = f.read()
    if t.startswith('\ufeff'):
        t = t[1:]
    return t.replace('\r\n', '\n').replace('\r', '\n')


def write_text(root, rel, text):
    p = os.path.join(root, *rel.split('/'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def _pruned(d):
    return d.startswith('.') or d == '_deps' or d == 'third_party' or d == 'build' or re.match(r'^build[-_.]', d)


def walk_files(root, rel_dir, pred, recursive=True):
    base = os.path.join(root, *rel_dir.split('/'))
    out = []
    if not os.path.isdir(base):
        return out
    for dp, dns, fns in os.walk(base):
        dns[:] = sorted(d for d in dns if not _pruned(d)) if recursive else []
        for fn in fns:
            if pred(fn):
                out.append(os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, '/'))
    return sorted(out)


def md(s):
    """Plain text made safe for a Markdown table cell or list line: HTML entities, never a backslash."""
    out = []
    for ch in s.replace('\n', ' '):
        out.append({'&': '&amp;', '<': '&lt;', '>': '&gt;', '|': '&#124;', '*': '&#42;', '`': '&#96;',
                    '[': '&#91;', ']': '&#93;', '\\': '&#92;', '~': '&#126;'}.get(ch, ch))
    return ''.join(out)


# The words beside an id. A requirement's words are its row's Requirement sentence and a control's its first sentence,
# never paraphrased: footnote markers and emphasis are dropped, and the sentence is cut to SHORT_WORDS words at a word
# boundary, with "…", when it is longer than SHORT_WORDS + SHORT_SLACK. A cut that would open a bracket leaves that
# bracketed aside out and cuts again, so the words never stop short of what the sentence says.
SHORT_WORDS = 12
SHORT_SLACK = 3
KIND_WORDS = {'E1': 'an automated test of the logic', 'E2': 'a frozen record of old behaviour',
              'E3': 'a run over real swings', 'E4': 'an automated screen test', 'E5': 'a run on Windows',
              'E6': 'seen at the rig or in a corpus swing', 'E7': 'the installer checked'}
KIND_MISSING = {'E1': 'no automated test of the logic yet', 'E2': 'no frozen record of old behaviour yet',
                'E3': 'no run over real swings yet', 'E4': 'no automated screen test yet', 'E5': 'no run on Windows yet',
                'E6': 'never seen at the rig or in a corpus swing', 'E7': 'the installer never checked'}
STATUS_PLAIN = {'delivered': 'delivered', 'built': 'built, not verified', 'partial': 'partial',
                'planned': 'planned', 'proposed': 'proposed', 'wont': "won't have"}


def plain_words(s):
    s = re.sub(r'\[\^[^\]]*\]', '', s).replace('\\|', '|')
    s = re.sub(r'\*\*([^*]+)\*\*', r'\1', s)
    s = re.sub(r'(?<![\w*])\*([^*\n]+)\*(?![\w*])', r'\1', s)
    s = re.sub(r'(?<![\w_])_([^_\n]+)_(?![\w_])', r'\1', s)
    return ' '.join(s.split())


def first_sentence(s):
    return re.split(r'(?<=[.!?])\s+(?=[A-Z])', plain_words(s), maxsplit=1)[0]


def short_words(s):
    words = first_sentence(s).split()
    if len(words) <= SHORT_WORDS + SHORT_SLACK:
        return ' '.join(words)
    cut = words[:SHORT_WORDS]
    opened = _open_bracket(cut)
    if opened is not None:
        closed = _bracket_close(words, opened)
        if closed is not None:
            # A cut would open a bracket: leave the bracketed aside out (its trailing punctuation kept), then cut again.
            tail = words[closed][words[closed].rindex(')') + 1:]
            rest = words[:opened]
            if tail and rest:
                rest = rest[:-1] + [rest[-1] + tail]
            return short_words(' '.join(rest + words[closed + 1:]))
    while len(cut) > SHORT_WORDS // 2 and _open_bracket(cut) is not None:
        cut.pop()  # a bracket that never closes: cut before it
    cut[-1] = cut[-1].rstrip(',;:')
    return ' '.join(cut) + ' …'


def _open_bracket(words):
    """The index of the word that opens the first bracket left open in words, or None."""
    stack = []
    for i, w in enumerate(words):
        for ch in w:
            if ch == '(':
                stack.append(i)
            elif ch == ')' and stack:
                stack.pop()
    return stack[0] if stack else None


def _bracket_close(words, opened):
    """The index of the word that closes the bracket opened in words[opened], or None."""
    depth = 0
    for i in range(opened, len(words)):
        for ch in words[i][words[i].index('(') if i == opened else 0:]:
            depth += {'(': 1, ')': -1}.get(ch, 0)
            if depth == 0:
                return i
    return None


def id_words(m, x):
    """'REQ-CAP-3 (The app shall …)' or 'CTL-UI-1 (its first sentence)': plain text."""
    if x.startswith('REQ-'):
        r = m['req']['rows'].get(x)
        if r:
            return '%s (%s)' % (x, short_words(r['text']))
        return '%s (%s)' % (x, 'withdrawn' if x in m['req']['withdrawn'] else 'no such requirement')
    t = m['control_text'].get(x)
    return '%s (%s)' % (x, short_words(t) if t is not None else 'no such control')


def issue_words(m, n):
    """'#77 (its title)', or '#77' while the snapshot holds no title for it: plain text."""
    iss = m['issues'].get(n)
    if iss is None:
        return '#%d (not in the issue snapshot)' % n
    return '#%d (%s)' % (n, iss['title']) if iss.get('title') else '#%d' % n


def with_words(m, text):
    """Every REQ- id and #n in a sentence from the page, with its words or its title beside it."""
    text = REQ_FIND.sub(lambda mm: id_words(m, mm.group(1)), text)
    return ISSUE_REF.sub(lambda mm: issue_words(m, int(mm.group(1))), text)


def joined(items):
    if len(items) <= 1:
        return ''.join(items)
    return ', '.join(items[:-1]) + ', and ' + items[-1]


def kinds_in_words(kinds, missing=False):
    return joined(['%s (%s)' % ((KIND_MISSING if missing else KIND_WORDS)[k], k) for k in kinds])


# ---------------------------------------------------------------------------------------------------------------------
# requirements.md

def split_row(line):
    s = line.strip()
    if s.startswith('|'):
        s = s[1:]
    if s.endswith('|') and not s.endswith('\\|'):
        s = s[:-1]
    return [c.strip().replace('\\|', '|') for c in re.split(r'(?<!\\)\|', s)]


def parse_status(raw):
    s = re.sub(r'\[\^[^\]]*\]', '', raw).replace('\u2019', "'").strip()
    segs = [x.strip() for x in s.split('\u00b7')]
    head = re.sub(r'\s*\([^)]*\)\s*$', '', segs[0]).strip().lower()
    base = None
    for words, b in STATUS_BASES:
        if head == words:
            base = b
            break
    bugs = []
    for seg in segs[1:]:
        if seg.lower().startswith('open bug'):
            bugs += [int(n) for n in ISSUE_REF.findall(seg)]
    return {
        'text': s,
        'base': base,
        'needs_ruling': RULING_MARKER in s.lower(),
        'to_confirm': any(x.lower() == 'to confirm' for x in segs[1:]),
        'open_bugs': sorted(set(bugs)),
    }


def parse_kind_list(cell, where):
    kinds = [k for k in re.split(r'[\s,;]+', cell.strip()) if k]
    for k in kinds:
        if k not in KINDS:
            raise RtmError('%s: %r is not a kind of evidence (E1 to E7)' % (where, k))
    return sorted(set(kinds), key=nat_key)


def parse_requirements(text):
    rows, withdrawn, problems = {}, {}, []
    order = []
    section, hdr = None, None
    for n, line in enumerate(text.split('\n'), 1):
        if line.startswith('## '):
            section, hdr = line[3:].strip(), None
            continue
        if not line.startswith('|'):
            if line.strip():
                hdr = None
            continue
        cells = split_row(line)
        if cells and cells[0] == 'Id':
            hdr = cells
            if section != WITHDRAWN_SECTION and not all(c in hdr for c in REQ_COLS):
                problems.append('%s:%d: a table headed Id lacks the columns %s' % (
                    P_REQ, n, ', '.join(c for c in REQ_COLS if c not in hdr)))
                hdr = None
            continue
        if hdr is None or all(set(c) <= set('-: ') for c in cells):
            continue
        rid = re.sub(r'[*`]', '', cells[0]).strip()
        if not re.fullmatch(REQ_ID, rid):
            continue
        if len(cells) != len(hdr):
            problems.append('%s:%d: %s has %d cells, its table %d' % (P_REQ, n, rid, len(cells), len(hdr)))
            continue
        if rid in rows or rid in withdrawn:
            problems.append('%s:%d: %s appears twice' % (P_REQ, n, rid))
            continue
        d = dict(zip(hdr, cells))
        if section == WITHDRAWN_SECTION:
            withdrawn[rid] = n
            continue
        st = parse_status(d['Status'])
        if st['base'] is None:
            problems.append('%s:%d: %s: status %r is not one of the page\'s status values' % (P_REQ, n, rid, st['text']))
        refs = set()
        for col in ('Status', 'Sources', 'Evidence'):
            refs.update(int(x) for x in ISSUE_REF.findall(d[col]))
        rows[rid] = {
            'id': rid, 'line': n, 'section': section, 'area': rid.split('-')[1],
            'status': st, 'release': d['Release'].strip(), 'moscow': d['MoSCoW'].strip(),
            'sources': d['Sources'], 'issues': sorted(refs), 'text': d['Requirement'],
            'needs_cell': d.get('Needs', '').strip(),
        }
        order.append(rid)
    sections = []
    for rid in order:
        if rows[rid]['section'] not in sections:
            sections.append(rows[rid]['section'])
    return {'rows': rows, 'order': order, 'withdrawn': withdrawn, 'problems': problems, 'sections': sections}


def uncomment(text):
    return '\n'.join(l for l in text.split('\n') if not l.lstrip().startswith('#'))


def parse_kind_defaults(text):
    if text is None:
        raise RtmError('%s is missing' % P_KINDS)
    out = {}
    rdr = csv.DictReader(io.StringIO(uncomment(text)))
    if not rdr.fieldnames or 'area' not in rdr.fieldnames or 'needs' not in rdr.fieldnames:
        raise RtmError('%s: the header must name the columns area and needs' % P_KINDS)
    for i, r in enumerate(rdr, 2):
        area = (r.get('area') or '').strip()
        if not area:
            continue
        if area in out:
            raise RtmError('%s: area %s appears twice' % (P_KINDS, area))
        out[area] = parse_kind_list(r.get('needs') or '', '%s (area %s)' % (P_KINDS, area))
    return out


def parse_ucmap(text):
    if text is None:
        raise RtmError('%s is missing' % P_UC)
    rdr = csv.DictReader(io.StringIO(text))
    if not rdr.fieldnames or 'req_id' not in rdr.fieldnames:
        raise RtmError('%s: the header has no req_id column' % P_UC)
    out = {}
    for n, r in enumerate(rdr, 2):
        rid = (r.get('req_id') or '').strip()
        if rid:
            out[rid] = {'line': n, 'use_cases': (r.get('use_cases') or '').strip(),
                        'status': (r.get('status') or '').strip()}
    return out


def parse_observations(text):
    if text is None:
        raise RtmError('%s is missing' % P_OBS)
    rdr = csv.reader(io.StringIO(text))
    rows = list(rdr)
    if not rows or [c.strip() for c in rows[0]] != OBS_HEADER:
        raise RtmError('%s: the header must be exactly: %s' % (P_OBS, ','.join(OBS_HEADER)))
    out = []
    swings = {}  # (session date, shot) -> [(observation id, time of day, line)]: two swings must be told apart
    for n, r in enumerate(rows[1:], 2):
        if not any(c.strip() for c in r):
            continue
        if len(r) != len(OBS_HEADER):
            raise RtmError('%s:%d: %d fields, the header has %d' % (P_OBS, n, len(r), len(OBS_HEADER)))
        d = dict(zip(OBS_HEADER, [c.strip() for c in r]))
        if not re.fullmatch(r'OBS-[0-9]+', d['Id']):
            raise RtmError('%s:%d: id %r is not OBS-n' % (P_OBS, n, d['Id']))
        if d['Kind'] not in OBS_KINDS:
            raise RtmError('%s:%d: kind %r is not one of %s' % (P_OBS, n, d['Kind'], ', '.join(OBS_KINDS)))
        reqs = [x for x in re.split(r'[\s,;]+', d['Requirements']) if x]
        corpus = None
        if d['Record'].startswith('corpus:'):
            cm = CORPUS_RECORD.match(d['Record'])
            if d['Kind'] != 'E3' or not cm:
                raise RtmError('%s:%d: a corpus line is Kind E3 and its Record is exactly "corpus: <session date> shot '
                               '<n>[ at <HH:MM> UTC][, captured on <commit>]", for example "corpus: 2026-08-04 shot 7, '
                               'captured on 1a2b3c4d": a date and a shot number, a time of day only where two swings '
                               'share both, the build only when the swing records one, never a golfer' % (P_OBS, n))
            corpus = {'captured': cm.group(4), 'date': cm.group(1), 'shot': int(cm.group(2)), 'tod': cm.group(3)}
            key, tod = (cm.group(1), cm.group(2)), cm.group(3)
            others = [s for s in swings.get(key, []) if s[0] != d['Id']]
            if others and (tod is None or any(s[1] is None or s[1] == tod for s in others)):
                raise RtmError('%s:%d: %s and %s (line %d) share %s shot %s: each must name its own time of day, '
                               '"corpus: %s shot %s at <HH:MM> UTC"' % (P_OBS, n, d['Id'], others[0][0], others[0][2],
                                                                        key[0], key[1], key[0], key[1]))
            if not any(s[0] == d['Id'] for s in swings.get(key, [])):
                swings.setdefault(key, []).append((d['Id'], tod, n))
        out.append({'id': d['Id'], 'line': n, 'date': d['Date'], 'reqs': reqs, 'build': d['Build'],
                    'kind': d['Kind'], 'passed': d['What was seen'].lower().startswith('pass'), 'corpus': corpus})
    return out


def parse_issue_snapshot(text):
    if text is None:
        raise RtmError('%s is missing: run --refresh-issues' % P_ISSUES)
    try:
        d = json.loads(text)
    except ValueError as e:
        raise RtmError('%s: not JSON (%s)' % (P_ISSUES, e))
    out = {}
    for i in d.get('issues', []):
        out[int(i['number'])] = {
            'number': int(i['number']), 'state': i['state'], 'labels': sorted(i.get('labels') or []),
            'milestone': i.get('milestone'), 'requirement': i.get('requirement'), 'control': i.get('control'),
            'title': i.get('title'),
        }
    return out


def parse_gap_lines(text, path, accepted):
    out = []
    if text is None:
        return None
    for n, line in enumerate(text.split('\n'), 1):
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        if accepted:
            parts = [p.strip() for p in s.split('|')]
            if len(parts) != 4 or not all(parts):
                raise RtmError('%s:%d: a line is "<flag> <gap> | <reason> | <approving role> | <date>"' % (path, n))
            key, reason, role, date = parts
            if role not in ROLES:
                raise RtmError('%s:%d: the approving role must be the Product Owner or the Solution Architect' % (path, n))
            if not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}|[0-9]{1,2} [A-Z][a-z]{2} [0-9]{4}', date):
                raise RtmError('%s:%d: date %r is not 2026-11-26 or 26 Nov 2026' % (path, n, date))
        else:
            key, reason, role, date = s, '', '', ''
        key = ' '.join(key.split())
        m = re.match(r'^([0-9]+) \S', key)
        if not m or not (2 <= int(m.group(1)) <= 12):
            raise RtmError('%s:%d: %r does not start with a flag number from 2 to 12 and its gap' % (path, n, key))
        flag = int(m.group(1))
        if accepted and role == 'Solution Architect' and flag not in (4, 9) and 'CTL-' not in key:
            raise RtmError('%s:%d: the Solution Architect approves only a test\'s or a control\'s labelling gap; '
                           'this one needs the Product Owner' % (path, n))
        out.append({'key': key, 'flag': flag, 'line': n, 'reason': reason, 'role': role, 'date': date})
    return out


def load_platform_lists(root):
    out = {}
    for rel in walk_files(root, P_PLATFORMS, lambda f: f.endswith('.txt'), recursive=False):
        name = posixpath.basename(rel)[:-4]
        names = set(l.strip() for l in read_text(root, rel).split('\n') if l.strip() and not l.startswith('#'))
        out[name] = names
    return out


def parse_loop_table(text):
    out = {}
    if text is None:
        return out
    for n, line in enumerate(text.split('\n'), 1):
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        parts = [p.strip() for p in s.split('|')]
        if len(parts) != 4 or not all(parts):
            raise RtmError('%s:%d: a line is "<file> | <name as written> | <loop item> | <test name>"' % (P_LOOPS, n))
        f, tmpl, item, name = parts
        out.setdefault((f, tmpl), {})[item] = name
    return out


def parse_controls(root):
    """{CTL-id: (active or retired, the control's text after its bold id)}."""
    out = {}
    for rel in walk_files(root, P_CONTROLS, lambda f: f.endswith('.md')):
        for line in read_text(root, rel).split('\n'):
            m = re.match(r'^\*\*([A-Z]+-[0-9]+[a-z]?)\.\*\*\s*(.*)$', line)
            if m:
                out['CTL-' + m.group(1)] = ('retired' if m.group(2).lower().startswith('retired') else 'active',
                                            m.group(2))
    return out


# ---------------------------------------------------------------------------------------------------------------------
# The CMake scan: a small, literal-only evaluator over the text

IDENT = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
BRACKET_OPEN = re.compile(r'\[(=*)\[')
VAR_REF = re.compile(r'\$\{([A-Za-z0-9_./+-]+)\}')


class Unresolved(Exception):
    pass


def cmake_commands(text):
    """[(name_lower, [(arg_text, kind)], line)], kind 'q' quoted, 'b' bracket, 'u' unquoted."""
    cmds, i, n, line = [], 0, len(text), 1

    def skip_comment(j, ln):
        m = BRACKET_OPEN.match(text, j + 1)
        if m:
            end = text.find(']' + m.group(1) + ']', m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            return end, ln + text.count('\n', j, end)
        end = text.find('\n', j)
        return (n if end < 0 else end), ln

    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c in ' \t':
            i += 1
            continue
        if c == '#':
            i, line = skip_comment(i, line)
            continue
        m = IDENT.match(text, i)
        if not m:
            i += 1
            continue
        j = m.end()
        while j < n and text[j] in ' \t':
            j += 1
        if j >= n or text[j] != '(':
            i = m.end()
            continue
        name, start, args, depth = m.group(0).lower(), line, [], 1
        j += 1
        while j < n and depth > 0:
            c = text[j]
            if c == '\n':
                line += 1
                j += 1
            elif c in ' \t':
                j += 1
            elif c == '#':
                j, line = skip_comment(j, line)
            elif c == '(':
                depth += 1
                args.append(('(', 'u'))
                j += 1
            elif c == ')':
                depth -= 1
                if depth > 0:
                    args.append((')', 'u'))
                j += 1
            elif c == '"':
                k, buf = j + 1, []
                while k < n and text[k] != '"':
                    if text[k] == '\\' and k + 1 < n:
                        nx = text[k + 1]
                        if nx == '\n':
                            line += 1
                        else:
                            buf.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\'}.get(nx, '\\' + nx))
                        k += 2
                        continue
                    if text[k] == '\n':
                        line += 1
                    buf.append(text[k])
                    k += 1
                args.append((''.join(buf), 'q'))
                j = k + 1
            elif c == '[' and BRACKET_OPEN.match(text, j):
                bm = BRACKET_OPEN.match(text, j)
                end = text.find(']' + bm.group(1) + ']', bm.end())
                end = n if end < 0 else end
                args.append((text[bm.end():end], 'b'))
                line += text.count('\n', j, end)
                j = end + len(bm.group(1)) + 2
            else:
                k = j
                while k < n and text[k] not in ' \t\n()#"':
                    k += 2 if text[k] == '\\' else 1
                args.append((text[j:k], 'u'))
                j = k
        cmds.append((name, args, start))
        i = j
    return cmds


def expand_str(s, env):
    out = s
    for _ in range(16):
        def rep(m):
            v = env.get(m.group(1))
            if v is None:
                raise Unresolved(m.group(1))
            return v
        new = VAR_REF.sub(rep, out)
        if new == out:
            break
        out = new
    if '${' in out or '$ENV{' in out or '$CACHE{' in out:
        raise Unresolved(out)
    return out


def split_list(v):
    return [x.replace('\\;', ';') for x in re.split(r'(?<!\\);', v) if x != '']


def expand_arg(arg, env):
    text, kind = arg
    if kind == 'b':
        return [text]
    v = expand_str(text, env)
    return [v] if kind == 'q' else split_list(v)


def expand_args(args, env):
    out = []
    for a in args:
        out += expand_arg(a, env)
    return out


VAR_WRITERS = {'string', 'list', 'math', 'file', 'get_filename_component', 'cmake_path', 'separate_arguments',
               'execute_process', 'find_program', 'find_library', 'find_path', 'find_file', 'get_property',
               'get_target_property', 'get_directory_property', 'get_test_property', 'cmake_parse_arguments'}


class Scan:
    def __init__(self, loops):
        self.tests = {}         # name -> [(file, line)]
        self.req_calls = []     # (test, [ids], file, line)
        self.label_sets = []    # (test, [labels], file, line, append?)
        self.errors = []
        self.loops = loops
        self.loops_seen = {k: set() for k in loops}
        self.qml = {}           # test -> {'file': rel, 'ids': [...]}

    def error(self, msg):
        self.errors.append(msg)


def find_end(cmds, i, open_name, close_name):
    depth = 0
    for j in range(i, len(cmds)):
        if cmds[j][0] == open_name:
            depth += 1
        elif cmds[j][0] == close_name:
            depth -= 1
            if depth == 0:
                return j
    return len(cmds)


def foreach_items(args, env):
    if not args:
        return None, None
    try:
        var = expand_arg(args[0], env)[0]
    except Unresolved:
        return None, None
    rest = args[1:]
    words = [a[0] if a[1] == 'u' else None for a in rest]
    try:
        if words and words[0] == 'RANGE':
            nums = [int(x) for x in expand_args(rest[1:], env)]
            if len(nums) == 1:
                return var, [str(x) for x in range(0, nums[0] + 1)]
            step = nums[2] if len(nums) > 2 else 1
            return var, [str(x) for x in range(nums[0], nums[1] + 1, step)]
        if words and words[0] == 'IN':
            items, mode = [], None
            for a in rest[1:]:
                if a[1] == 'u' and a[0] in ('LISTS', 'ITEMS', 'ZIP_LISTS'):
                    mode = a[0]
                    continue
                if mode == 'LISTS':
                    for lname in expand_arg(a, env):
                        if env.get(lname) is None:
                            raise Unresolved(lname)
                        items += split_list(env[lname])
                elif mode == 'ITEMS':
                    items += expand_arg(a, env)
                else:
                    return var, None
            return var, items
        return var, expand_args(rest, env)
    except (Unresolved, ValueError):
        return var, None


def glob_files(root, pattern):
    d, pat = posixpath.split(posixpath.normpath(pattern))
    if any(ch in d for ch in '*?['):
        raise Unresolved(pattern)
    base = os.path.join(root, *[p for p in d.split('/') if p and p != '.'])
    if not os.path.isdir(base):
        return []
    hits = [f for f in os.listdir(base) if fnmatch.fnmatchcase(f, pat) and os.path.isfile(os.path.join(base, f))]
    return [posixpath.normpath(posixpath.join(d, f)) for f in sorted(hits)]


def resolve_names(scan, arg, env, loopstack, path, line, what, strict):
    try:
        return expand_arg(arg, env)
    except Unresolved as e:
        key = (path, arg[0])
        item = loopstack[-1] if loopstack else None
        if key in scan.loops and item is not None:
            scan.loops_seen[key].add(item)
            name = scan.loops[key].get(item)
            if name:
                return [name]
            scan.error('%s:%d: %s %s: the loop item %s is not in %s' % (path, line, what, arg[0], item, P_LOOPS))
            return []
        if strict:
            scan.error('%s:%d: %s %s: cannot resolve ${%s}; a test name must be literal, a literal foreach item, '
                       'or listed in %s' % (path, line, what, arg[0], e.args[0], P_LOOPS))
        return []


def run_block(scan, root, path, cmds, a, b, env, loopstack):
    i = a
    while i < b:
        name, args, line = cmds[i]
        if name in ('function', 'macro'):
            i = find_end(cmds, i, name, 'end' + name) + 1
            continue
        if name == 'foreach':
            j = find_end(cmds, i, 'foreach', 'endforeach')
            var, items = foreach_items(args, env)
            if var is None:
                run_block(scan, root, path, cmds, i + 1, j, env, loopstack + [None])
            else:
                saved = env.get(var)
                if items is None:
                    env.pop(var, None)
                    run_block(scan, root, path, cmds, i + 1, j, env, loopstack + [None])
                else:
                    for it in items:
                        env[var] = it
                        run_block(scan, root, path, cmds, i + 1, j, env, loopstack + [it])
                if saved is None:
                    env.pop(var, None)
                else:
                    env[var] = saved
            i = j + 1
            continue
        command(scan, root, path, name, args, line, env, loopstack)
        i += 1


def command(scan, root, path, name, args, line, env, loopstack):
    words = [a[0] if a[1] == 'u' else None for a in args]
    if name == 'set' and args:
        try:
            var = expand_arg(args[0], env)[0]
        except (Unresolved, IndexError):
            return
        vals = []
        for a in args[1:]:
            if a[1] == 'u' and a[0] in ('CACHE', 'PARENT_SCOPE'):
                break
            vals.append(a)
        try:
            v = [x for a in vals for x in expand_arg(a, env)]
            env[var] = ';'.join(v)
            if not vals:
                env.pop(var, None)
        except Unresolved:
            env.pop(var, None)
        return
    if name == 'unset' and args:
        env.pop(args[0][0], None)
        return
    if name == 'list' and len(args) >= 2 and words[0] == 'APPEND':
        var = args[1][0]
        try:
            add = expand_args(args[2:], env)
            if env.get(var) is None and var in env:
                raise Unresolved(var)
            env[var] = ';'.join(([env[var]] if env.get(var) else []) + add)
        except Unresolved:
            env.pop(var, None)
        return
    if name == 'file' and len(args) >= 2 and words[0] == 'GLOB':
        var = args[1][0]
        try:
            pats, rest = [], args[2:]
            for a in rest:
                if a[1] == 'u' and a[0] in ('RELATIVE', 'LIST_DIRECTORIES'):
                    raise Unresolved(a[0])
                if a[1] == 'u' and a[0] == 'CONFIGURE_DEPENDS':
                    continue
                pats += expand_arg(a, env)
            hits = []
            for p in pats:
                hits += glob_files(root, p)
            env[var] = ';'.join(sorted(set(hits)))
        except Unresolved:
            env.pop(var, None)
        return
    if name == 'get_filename_component' and len(args) >= 3:
        var = args[0][0]
        try:
            v = expand_arg(args[1], env)[0]
            mode = args[2][0]
            if mode == 'NAME_WE':
                env[var] = posixpath.basename(v).split('.')[0]
            elif mode == 'NAME':
                env[var] = posixpath.basename(v)
            elif mode in ('DIRECTORY', 'PATH'):
                env[var] = posixpath.dirname(v)
            else:
                env.pop(var, None)
        except (Unresolved, IndexError):
            env.pop(var, None)
        return
    if name in VAR_WRITERS:
        for a in args:
            if a[1] == 'u' and IDENT.fullmatch(a[0]) and a[0] in env:
                env.pop(a[0], None)
        return
    if name in ('add_test', 'pp_add_test') and args:
        arg = args[1] if (name == 'add_test' and words[0] == 'NAME' and len(args) > 1) else args[0]
        for t in resolve_names(scan, arg, env, loopstack, path, line, name, True):
            scan.tests.setdefault(t, []).append((path, line))
        return
    if name == 'pp_requirement':
        if not args:
            scan.error('%s:%d: pp_requirement() names no test' % (path, line))
            return
        tests = resolve_names(scan, args[0], env, loopstack, path, line, 'pp_requirement', True)
        try:
            ids = expand_args(args[1:], env)
        except Unresolved as e:
            scan.error('%s:%d: pp_requirement: cannot resolve ${%s}; its ids must be literal' % (path, line, e.args[0]))
            return
        if not ids:
            scan.error('%s:%d: pp_requirement(%s) names no id' % (path, line, args[0][0]))
        for x in ids:
            if not LABEL_RE.match(x):
                scan.error('%s:%d: pp_requirement: %r is not REQ-<AREA>-n or CTL-<control id>' % (path, line, x))
        for t in tests:
            scan.req_calls.append((t, [x for x in ids if LABEL_RE.match(x)], path, line))
        return
    if name == 'set_tests_properties':
        if 'PROPERTIES' not in words:
            return
        k = words.index('PROPERTIES')
        props = args[k + 1:]
        for p in range(0, len(props) - 1, 2):
            if props[p][1] == 'u' and props[p][0] == 'LABELS':
                try:
                    labels = expand_arg(props[p + 1], env)
                except Unresolved:
                    labels = None
                for a in args[:k]:
                    for t in resolve_names(scan, a, env, loopstack, path, line, name, False):
                        scan.label_sets.append((t, labels, path, line, False))
        return
    if name == 'set_property' and words and words[0] == 'TEST' and 'PROPERTY' in words:
        k = words.index('PROPERTY')
        if k + 1 >= len(args) or args[k + 1][0] != 'LABELS':
            return
        append = 'APPEND' in words[:k] or 'APPEND_STRING' in words[:k]
        try:
            labels = expand_args(args[k + 2:], env)
        except Unresolved:
            labels = None
        for a in args[1:k]:
            if a[1] == 'u' and a[0] in ('APPEND', 'APPEND_STRING'):
                continue
            for t in resolve_names(scan, a, env, loopstack, path, line, name, False):
                scan.label_sets.append((t, labels, path, line, append))
        return


def cmake_files(root):
    files = [f for f in SCAN_FILES if os.path.isfile(os.path.join(root, f))]
    for d in SCAN_DIRS:
        files += walk_files(root, d, lambda f: f == 'CMakeLists.txt' or f.endswith('.cmake'))
    return sorted(set(files))


def scan_tests(root, loops):
    scan = Scan(loops)
    for rel in cmake_files(root):
        text = read_text(root, rel)
        cur = posixpath.dirname(rel) or '.'
        env = {'CMAKE_CURRENT_SOURCE_DIR': cur, 'CMAKE_CURRENT_LIST_DIR': cur}
        cmds = cmake_commands(text)
        run_block(scan, root, rel, cmds, 0, len(cmds), env, [])
    for key, items in sorted(loops.items()):
        seen = scan.loops_seen[key]
        if not seen:
            scan.error('%s: %s | %s: no such loop registration in the source' % (P_LOOPS, key[0], key[1]))
        elif seen != set(items):
            scan.error('%s: %s | %s: the table lists %s, the source loops over %s' % (
                P_LOOPS, key[0], key[1], ' '.join(nat_sorted(items)), ' '.join(nat_sorted(seen))))
    for rel in walk_files(root, P_QML, lambda f: fnmatch.fnmatchcase(f, 'tst_*.qml'), recursive=False):
        test = 'qml_ui.' + posixpath.basename(rel)[:-4]
        ids = []
        for n, line in enumerate(read_text(root, rel).split('\n'), 1):
            m = QML_REQ_LINE.match(line)
            if not m:
                continue
            found = LABEL_FIND.findall(m.group(1))
            rest = LABEL_FIND.sub('', m.group(1))
            if not found or re.sub(r'[\s,;]+', '', rest):
                scan.error('%s:%d: a // REQ: line holds only REQ-<AREA>-n and CTL-<control id> ids' % (rel, n))
            ids += found
        if ids:
            if test not in scan.tests:
                scan.error('%s: its // REQ: lines label %s, which no registration makes' % (rel, test))
            scan.qml[test] = {'file': rel, 'ids': ids}
    # pp_requirement's order: after the registration and after any LABELS that would replace its labels.
    for t, ids, path, line in scan.req_calls:
        regs = scan.tests.get(t)
        if not regs:
            scan.error('%s:%d: pp_requirement(%s): no test of that name is registered' % (path, line, t))
            continue
        same = [l for f, l in regs if f == path]
        if same and len(same) == len(regs) and min(same) > line:
            scan.error('%s:%d: pp_requirement(%s) comes before its registration; call it after' % (path, line, t))
        for lt, labels, lpath, lline, append in scan.label_sets:
            if lt == t and not append and lpath == path and lline > line:
                scan.error('%s:%d: set_tests_properties(%s ... LABELS) replaces the labels pp_requirement set at line '
                           '%d; set LABELS first' % (lpath, lline, t, line))
    return scan


# ---------------------------------------------------------------------------------------------------------------------
# The model, the flags, the matrix

def load(root):
    req_text = read_text(root, P_REQ)
    if req_text is None:
        raise RtmError('%s is missing' % P_REQ)
    m = {'root': root}
    m['req'] = parse_requirements(req_text)
    m['kinds'] = parse_kind_defaults(read_text(root, P_KINDS))
    m['uc'] = parse_ucmap(read_text(root, P_UC))
    m['obs'] = parse_observations(read_text(root, P_OBS))
    m['issues'] = parse_issue_snapshot(read_text(root, P_ISSUES))
    acc = read_text(root, P_ACCEPTED)
    if acc is None:
        raise RtmError('%s is missing' % P_ACCEPTED)
    m['accepted'] = parse_gap_lines(acc, P_ACCEPTED, True)
    m['baseline'] = parse_gap_lines(read_text(root, P_BASELINE), P_BASELINE, False)
    m['platforms'] = load_platform_lists(root)
    ctl = parse_controls(root)
    m['controls'] = {k: v[0] for k, v in ctl.items()}
    m['control_text'] = {k: v[1] for k, v in ctl.items()}
    m['design'] = walk_files(root, P_DESIGN, lambda f: f.endswith('.md'))
    scan = scan_tests(root, parse_loop_table(read_text(root, P_LOOPS)))
    errors = list(scan.errors)
    for rid, row in m['req']['rows'].items():
        if row['needs_cell']:
            row['needs'] = parse_kind_list(row['needs_cell'], '%s:%d (%s, Needs)' % (P_REQ, row['line'], rid))
        elif row['area'] in m['kinds']:
            row['needs'] = m['kinds'][row['area']]
        else:
            errors.append('%s: no default kinds for area %s (%s)' % (P_KINDS, row['area'], rid))
            row['needs'] = []
    if errors:
        raise RtmError('\n'.join(errors))
    m['scan'] = scan
    derive(m)
    return m


def derive(m):
    scan, rows = m['scan'], m['req']['rows']
    tests = {}
    for t in scan.tests:
        tests[t] = {'name': t, 'files': sorted(set(f for f, _ in scan.tests[t])), 'ids': set(), 'ctest': set()}
    for t, ids, _, _ in scan.req_calls:
        tests[t]['ids'].update(ids)
    for t, q in scan.qml.items():
        if t in tests:
            tests[t]['ids'].update(q['ids'])
    for t, labels, _, _, append in scan.label_sets:
        if t in tests and labels:
            tests[t]['ctest'].update(labels)
    plats = sorted(set(KNOWN_PLATFORMS) | set(m['platforms']))
    for t in tests.values():
        k = set()
        if 'golden' in t['ctest']:
            k.add('E2')
        if 'qml_ui' in t['ctest']:
            k.add('E4')
        t['kinds'] = sorted(k) or ['E1']
        t['lists'] = [p for p in sorted(m['platforms']) if t['name'] in m['platforms'][p]]
        t['counts'] = any((p in m['platforms'] and t['name'] in m['platforms'][p]) or p not in m['platforms']
                          for p in plats)
    m['tests'] = tests
    m['plats'] = plats
    by_req = {}
    for t in tests.values():
        for i in t['ids']:
            by_req.setdefault(i, []).append(t['name'])
    m['by_req'] = {k: nat_sorted(v) for k, v in by_req.items()}
    named = {}
    for n, iss in m['issues'].items():
        for x in iss['requirement'] or []:
            named.setdefault(x, set()).add(n)
    for rid, row in rows.items():
        row['all_issues'] = sorted(set(row['issues']) | named.get(rid, set()))
        present = set()
        for t in m['by_req'].get(rid, []):
            if tests[t]['counts']:
                present.update(tests[t]['kinds'])
        row['obs'] = [o for o in m['obs'] if rid in o['reqs']]
        for o in row['obs']:
            if o['passed']:
                present.add(o['kind'])
                if o['corpus']:
                    present.add('E6')
        row['present'] = sorted(present, key=nat_key)
        row['missing'] = [k for k in row['needs'] if k not in present]
        bugs = set(row['status']['open_bugs'])
        bugs |= set(n for n in named.get(rid, set()) if 'bug' in m['issues'][n]['labels'])
        row['open_bug'] = any(m['issues'].get(n, {}).get('state') == 'open' for n in bugs)
        complete = bool(row['needs']) and not row['missing']
        base = row['status']['base']
        # An open bug never lowers what the evidence supports: it shows beside it as flag 6 (the Solution Architect,
        # 26 Nov 2026: "a regression does not take that away").
        if complete:
            row['supports'] = 'delivered'
        elif base == 'delivered':
            row['supports'] = 'built'
        else:
            row['supports'] = base
    issue_ids = {}
    for rid in rows:
        for n in rows[rid]['all_issues']:
            issue_ids.setdefault(n, []).append(rid)
    m['cited_issues'] = issue_ids


def id_state(m, x):
    if x.startswith('REQ-'):
        if x in m['req']['rows']:
            return 'ok'
        return 'withdrawn' if x in m['req']['withdrawn'] else 'unknown'
    st = m['controls'].get(x)
    if st == 'active':
        return 'ok'
    return 'withdrawn' if st == 'retired' else 'unknown'


def _gap(flag, key, msg, row=None):
    # key: the baseline's and the accepted gaps' spelling, never changed by the wording; msg: plain words.
    return {'flag': flag, 'key': '%d %s' % (flag, key), 'msg': msg, 'row': row}


def _where_words(m, where):
    kind, _, what = where.partition(':')
    return {'test': lambda: 'the test %s' % what,
            'issue': lambda: 'issue %s' % issue_words(m, int(what.lstrip('#'))),
            'observation': lambda: 'the observation %s' % what,
            'uc-map': lambda: 'uc-map.csv', 'accepted-gaps': lambda: 'the accepted gaps',
            'baseline': lambda: 'the baseline'}.get(kind, lambda: where)()


def _flag2(m):
    cites = []
    for t in m['tests'].values():
        for x in t['ids']:
            cites.append((x, 'test:' + t['name']))
    for n, iss in m['issues'].items():
        for x in (iss['requirement'] or []) + (iss['control'] or []):
            if x != 'none':
                cites.append((x, 'issue:#%d' % n))
    for rid in m['uc']:
        cites.append((rid, 'uc-map'))
    for o in m['obs']:
        for x in o['reqs']:
            cites.append((x, 'observation:' + o['id']))
    for name, lines in (('accepted-gaps', m['accepted']), ('baseline', m['baseline'] or [])):
        for g in lines:
            if g['flag'] == 2:
                continue  # the line records that very gap; flag 13 or the accepted-gap warning says when it closes
            for x in LABEL_FIND.findall(g['key']):
                cites.append((x, name))
    out = []
    for x, where in sorted(set(cites), key=lambda c: (nat_key(c[0]), c[1])):
        st = id_state(m, x)
        if st != 'ok':
            out.append(_gap(2, '%s %s' % (x, where), '%s, cited by %s, %s' % (
                x, _where_words(m, where), 'is withdrawn' if st == 'withdrawn' else 'does not exist')))
    return out


def _flag3(m):
    out = []
    for rid in m['req']['order']:
        row = m['req']['rows'][rid]
        if row['status']['base'] in ('delivered', 'built', 'partial'):
            for k in row['missing']:
                out.append(_gap(3, '%s %s' % (rid, k), '%s: %s; %s (%s)' % (
                    id_words(m, rid), STATUS_PLAIN[row['status']['base']], KIND_MISSING[k], k), row=rid))
    return out


def _flag4(m):
    return [_gap(4, t, '%s names no requirement or control it proves' % t)
            for t in nat_sorted(m['tests']) if not m['tests'][t]['ids']]


def _flag5(m):
    out = []
    for n in sorted(m['issues']):
        iss = m['issues'][n]
        if iss['state'] != 'open' or 'enhancement' not in iss['labels']:
            continue
        req = iss['requirement']
        if req is None:
            out.append(_gap(5, '#%d' % n, '%s, an open enhancement, says no requirement it serves (no Requirement: '
                            'line)' % issue_words(m, n)))
        elif not req:
            out.append(_gap(5, '#%d' % n, '%s: its Requirement: line names no id and is not "none"' % issue_words(m, n)))
        else:
            bad = [x for x in req if x != 'none' and id_state(m, x) != 'ok']
            if bad:
                out.append(_gap(5, '#%d' % n, '%s: its Requirement: line names %s, which %s' % (
                    issue_words(m, n), ', '.join(id_words(m, x) for x in bad),
                    'is unknown or withdrawn' if len(bad) == 1 else 'are unknown or withdrawn')))
    return out


def _flag6(m):
    out, seen = [], set()
    rows = m['req']['rows']
    for n in sorted(m['issues']):
        iss = m['issues'][n]
        if iss['state'] != 'open' or 'bug' not in iss['labels']:
            continue
        names = set(x for x in iss['requirement'] or [] if x in rows)
        names |= set(rid for rid in rows if n in rows[rid]['status']['open_bugs'])
        for rid in nat_sorted(names):
            if rows[rid]['status']['base'] == 'delivered' and (n, rid) not in seen:
                seen.add((n, rid))
                out.append(_gap(6, '#%d %s' % (n, rid), 'open bug %s names %s, which requirements.md calls '
                                'delivered: the row is failing' % (issue_words(m, n), id_words(m, rid)), row=rid))
    return out


def _flag7(m):
    out = []
    for rid in m['req']['order']:
        row = m['req']['rows'][rid]
        if row['status']['base'] != 'planned' or not row['all_issues']:
            continue
        states = [m['issues'].get(n, {}).get('state') for n in row['all_issues']]
        if all(s == 'closed' for s in states):
            out.append(_gap(7, rid, '%s is planned, but its issues are all closed: %s' % (
                id_words(m, rid), '; '.join(issue_words(m, n) for n in row['all_issues'])), row=rid))
    return out


def _flag8(m):
    out = []
    for rid in m['req']['order']:
        row = m['req']['rows'][rid]
        if row['status']['needs_ruling'] and row['status']['base'] in ('built', 'delivered'):
            out.append(_gap(8, rid, '%s waits on a ruling change (proposed, for the Solution Architect) but is marked %s'
                            % (id_words(m, rid), STATUS_PLAIN[row['status']['base']]), row=rid))
    return out


def _flag9(m):
    if not m['platforms']:
        return []
    out = []
    for t in nat_sorted(m['tests']):
        tt = m['tests'][t]
        if any(x.startswith('REQ-') for x in tt['ids']) and not tt['lists']:
            out.append(_gap(9, t, '%s is labelled for a requirement but is in no gate list (%s)' % (
                t, ', '.join(sorted(m['platforms'])))))
    return out


def _flag10(m):
    out = []
    for rid in m['req']['order']:
        row = m['req']['rows'][rid]
        if 'E6' in row['needs'] and row['status']['base'] in ('delivered', 'built', 'partial') \
                and 'E6' not in row['present']:
            out.append(_gap(10, rid, '%s needs to be seen at the rig or in a corpus swing (E6), and never has been'
                            % id_words(m, rid), row=rid))
    return out


def _flag11(m):
    out = []
    for rid in m['req']['order']:
        row = m['req']['rows'][rid]
        base = row['status']['base']
        if base in (None, 'wont'):
            continue
        if row['supports'] == 'delivered' and base != 'delivered':
            out.append(_gap(11, rid, '%s: the evidence supports delivered; requirements.md says %s' % (
                id_words(m, rid), STATUS_PLAIN[base]), row=rid))
        elif base == 'delivered' and row['supports'] != 'delivered':
            out.append(_gap(11, rid, '%s: the evidence supports built, not verified (%s); requirements.md says '
                                     'delivered' % (
                id_words(m, rid), kinds_in_words(row['missing'], True)), row=rid))
    return out


def _flag12(m):
    out = []
    for rid in m['req']['order']:
        row = m['req']['rows'][rid]
        base = row['status']['base']
        if base in ('delivered', 'wont') or row['release'] == 'never' or row['moscow'].lower().startswith("won"):
            continue
        if not any(m['issues'].get(n, {}).get('state') == 'open' for n in row['all_issues']):
            out.append(_gap(12, rid, '%s is %s, and has no open issue and no accepted gap' % (
                id_words(m, rid), STATUS_PLAIN.get(base, row['status']['text'])), row=rid))
    return out


FLAG_FUNCS = [(2, _flag2), (3, _flag3), (4, _flag4), (5, _flag5), (6, _flag6), (7, _flag7), (8, _flag8),
              (9, _flag9), (10, _flag10), (11, _flag11), (12, _flag12)]


def warnings(m):
    w = []
    rows = m['req']['rows']
    for rid in m['req']['order']:
        if rows[rid]['status']['base'] == 'proposed' and not rows[rid]['all_issues']:
            w.append('a proposed row with no issue: %s' % id_words(m, rid))
    cited = ' '.join(rows[r]['sources'] for r in rows)
    for d in m['design']:
        b = posixpath.basename(d)
        if not re.search(r'(?<![\w.-])' + re.escape(b) + r'(?![\w-])', cited):
            w.append('a design document no row cites: %s' % d)
    for n in sorted(m['issues']):
        iss = m['issues'][n]
        if iss['state'] == 'open' and 'engineering' in iss['labels'] and iss['requirement'] is None \
                and iss['control'] is None:
            w.append('an open engineering issue with neither a Requirement: nor a Control: line: %s' % issue_words(m, n))
    for n in sorted(m['cited_issues']):
        if n not in m['issues']:
            w.append('#%d, cited by %s, is not in the issue snapshot' % (
                n, ', '.join(id_words(m, r) for r in m['cited_issues'][n])))
    for p in sorted(m['platforms']):
        for t in nat_sorted(m['platforms'][p] - set(m['tests'])):
            w.append('%s, in the %s gate list, is registered nowhere in the source' % (t, p))
    for p in m['req']['problems']:
        w.append('requirements.md: ' + p)
    return w


def evaluate(m):
    gaps = []
    for flag, fn in FLAG_FUNCS:
        gaps += fn(m)
    acc = {g['key']: g for g in m['accepted']}
    base = set(g['key'] for g in m['baseline'] or [])
    keys = set(g['key'] for g in gaps)
    for g in gaps:
        g['state'] = 'accepted' if g['key'] in acc else 'baseline' if g['key'] in base else 'NEW'
    for k in sorted(base - keys, key=nat_key):
        gaps.append({'flag': 13, 'key': '13 ' + k, 'msg': 'the baseline still lists "%s", which is no longer a gap: '
                     'take the line out' % k, 'state': 'NEW', 'row': None})
    w = warnings(m)
    for k in sorted(set(acc) - keys, key=nat_key):
        w.append('an accepted gap that is no longer open: "%s"' % k)
    return gaps, w


FLAG_HELP = {
    1: 'The page differs from what the inputs make now. It clears when python3 tools/method/make_rtm.py is run and '
       'rtm.md committed.',
    2: 'A test, an issue, uc-map.csv, an observation or a gap file names a requirement or control that does not exist, '
       'or was withdrawn. It clears when the citation is corrected.',
    3: 'A row requirements.md calls delivered, built or partial lacks a kind of evidence it needs. It clears when that '
       'evidence exists: a labelled test, or a passing observation in rtm_observations.csv.',
    4: 'A test does not say what it proves. It clears with pp_requirement(<test> <id>...) beside its registration, or '
       'a // REQ: line in a tst_*.qml file; a mechanical rename is make_rtm.py --rename <old> <new>; a test that '
       'proves neither is an accepted gap, approved by the Solution Architect.',
    5: 'An open enhancement does not say which requirement it serves. It clears with a Requirement: line in the issue '
       '(an id, or "none").',
    6: 'An open bug names a row requirements.md calls delivered, so the row is failing. It clears when the bug is '
       'closed, or no longer names the row.',
    7: "A planned row's issues are all closed. It clears when requirements.md's owner updates the status, or an "
       'issue is opened for what is left.',
    8: 'A row whose status says "proposed, for the Solution Architect" (it waits on a ruling change) is marked built '
       'or delivered. It clears when requirements.md drops that marker, or the status changes.',
    9: "A test labelled for a requirement is in no platform's gate list. A commit that adds a labelled test retakes "
       "macos.txt until BT-25's release gate writes the lists: python3 tools/method/make_rtm.py --platform-list "
       '<configured test tree> macos. It clears when the test is in a gate list.',
    10: 'A row that needs hardware has never been seen at the rig or in a corpus swing (E6). It clears with a passing '
        'observation at the rig, or a corpus swing.',
    11: "The evidence supports a different status from requirements.md's. It clears when requirements.md's owner "
        'changes the status: the matrix never does.',
    12: 'A row not delivered has no open issue and no accepted gap. It clears with an open issue, or an accepted gap.',
    13: 'The baseline lists a gap that has closed. It clears when the line is taken out of rtm_baseline.txt, so the '
        'baseline only shrinks.',
}
STATE_WORDS = {'accepted': 'accepted', 'baseline': 'baseline', 'NEW': 'new'}


MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')


def _corpus_line(swings):
    """A row's corpus swings as one line: the count, then the count by date ("130 corpus swings in 2026: 3 Jul (10),
    ..., 20 Oct (52)."). No per-swing list and no build: the build is a record only, kept in rtm_observations.csv. The
    year is said once when every date shares it, and on each date otherwise. A date says how many of its swings failed,
    unless they all did. The month names are fixed, never the locale's."""
    by_date, failed = {}, {}
    for o in swings:
        d = o['corpus']['date']
        by_date[d] = by_date.get(d, 0) + 1
        if not o['passed']:
            failed[d] = failed.get(d, 0) + 1
    dates = sorted(by_date)
    one_year = len(set(d[:4] for d in dates)) == 1

    def day(d):
        y, mo, dd = d.split('-')
        return '%d %s%s' % (int(dd), MONTHS[int(mo) - 1], '' if one_year else ' ' + y)

    def failures(k, of):
        return '' if not k else ', failed' if of == 1 else ', all failed' if k == of else ', %d failed' % k

    n, nf = len(swings), sum(failed.values())
    head = '%d corpus swing%s' % (n, '' if n == 1 else 's')
    if one_year:
        head += ' in ' + dates[0][:4]
    head += failures(nf, n)
    return '%s: %s.' % (head, ', '.join('%s (%d%s)' % (day(d), by_date[d], '' if nf == n else failures(
        failed.get(d, 0), by_date[d])) for d in dates))


def _row_lines(m, rid):
    r = m['req']['rows'][rid]
    L = ['- **%s**: %s' % (rid, md(with_words(m, first_sentence(r['text']))))]
    bits = ['Status: ' + with_words(m, r['status']['text']), 'release ' + (r['release'] or 'none'),
            r['moscow'] or 'no MoSCoW']
    uc = m['uc'].get(rid, {}).get('use_cases', '')
    if uc:
        bits.append('use cases ' + uc)
    L.append('  - ' + md('; '.join(bits)) + '.')
    wont = r['status']['base'] == 'wont'
    ev = 'Evidence: ' + (kinds_in_words(r['present']) if r['present'] else
                         "none needed (won't have)" if wont else 'none yet')
    if wont:
        pass  # a row that will never be built lacks nothing
    elif r['missing']:
        ev += '; ' + kinds_in_words(r['missing'], True)
    elif r['needs']:
        ev += '; every kind it needs is present'
    L.append('  - ' + md(ev) + '.')
    base, sup = r['status']['base'], r['supports']
    if sup != base and base not in (None, 'wont'):
        L.append('  - ' + md('The evidence supports %s; requirements.md says %s (flag 11).' % (
            STATUS_PLAIN.get(sup, sup), STATUS_PLAIN[base])))
    if m['by_req'].get(rid):
        L.append('  - Tests: ' + ', '.join('`%s`' % t for t in m['by_req'][rid]) + '.')
    plain = [o for o in r['obs'] if not o['corpus']]
    if plain:
        L.append('  - Observations: ' + md('; '.join('%s, %s (%s), %s on %s, build %s' % (
            o['id'], KIND_WORDS[o['kind']], o['kind'], 'passed' if o['passed'] else 'failed', o['date'], o['build'])
            for o in plain)) + '.')
    opened = [n for n in r['all_issues'] if m['issues'].get(n, {}).get('state') == 'open']
    other = [n for n in r['all_issues'] if n not in opened]
    if opened or other:
        parts = []
        if opened:
            parts.append('Open issues: ' + '; '.join(issue_words(m, n) for n in opened))
        if other:
            parts.append(('Closed issues: ' if all(n in m['issues'] for n in other) else 'Other issues: ')
                         + '; '.join(issue_words(m, n) for n in other))
        L.append('  - ' + md('. '.join(parts)) + '.')
    swings = {}
    for o in r['obs']:
        if o['corpus']:
            swings.setdefault(o['id'], o)
    if swings:
        L.append('  - ' + md(_corpus_line(list(swings.values()))))
    return L


def render(m, gaps, warns):
    rows, tests = m['req']['rows'], m['tests']
    L = []
    have = sorted(m['platforms'])
    lack = [p for p in m['plats'] if p not in m['platforms']]
    titled = any(i.get('title') for i in m['issues'].values())
    L += ['# Requirements traceability matrix', '',
          '**Generated by `tools/method/make_rtm.py` from committed source files only: never edit it by hand.** '
          '`rtm_check` (ctest) fails when this page is stale, or on a flag below that is neither an accepted gap '
          '(`rtm_accepted_gaps.txt`) nor in the baseline (`rtm_baseline.txt`). What it checks evidence against is '
          '[`test_strategy.md`](test_strategy.md) §8; the needs are [`requirements.md`](requirements.md).', '',
          "- **Pass or fail is ctest's report, not the matrix's.** The matrix shows which tests and observations bear "
          'on which requirement; ctest says at every run whether each test passes.',
          '- **A test registered for one platform only counts on every platform, except where a platform\'s gate '
          'list says otherwise** (`rtm_platform_tests/<platform>.txt`, `test_strategy.md` §8.3). Gate lists: %s. '
          'No gate list: %s.' % (', '.join(have) or 'none', ', '.join(lack) or 'none'),
          '- **The matrix never overrides requirements.md.** Where the evidence supports another status, flag 11 says '
          "so; requirements.md's owner changes the status.",
          '- **Kinds of evidence** (`test_strategy.md` §2): ' + '; '.join(
              '%s (%s)' % (KIND_WORDS[k], k) for k in KINDS) + '. A labelled test is an automated screen test (E4) '
          'when it carries the `qml_ui` label, a frozen record of old behaviour (E2) when it carries `golden`, and an '
          'automated test of the logic (E1) otherwise. An observation is the kind its line in `rtm_observations.csv` '
          'names, and counts only when what was seen is a pass; a corpus swing (an E3 line whose Record begins '
          '`corpus:`) also counts as seen in a corpus swing (E6), with no build or age limit (`test_strategy.md` §3, '
          "§4). The kinds a row needs are its Needs column, or its area's default in `rtm_kind_defaults.csv`.",
          "- **The words beside an id.** A requirement id carries its row's Requirement sentence, shortened to about "
          "%d words; a control id its control's first sentence; an issue number its title, from the issue snapshot "
          '(`rtm_issues.json`) as of its last refresh.%s' % (
              SHORT_WORDS, '' if titled else ' The snapshot holds no titles yet, so an issue shows by its number '
              'until `make_rtm.py --refresh-issues --library <the library folder>` adds them.'), '']
    # 1. The summary
    counts = {}
    for rid in m['req']['order']:
        counts[rows[rid]['status']['base']] = counts.get(rows[rid]['status']['base'], 0) + 1
    L += ['## Summary', '', '| | Rows |', '|---|---|']
    for b in ('delivered', 'built', 'partial', 'planned', 'proposed', 'wont'):
        L.append('| %s (requirements.md) | %d |' % (BASE_WORDS[b], counts.get(b, 0)))
    if counts.get(None):
        L.append('| a status not understood | %d |' % counts[None])
    L.append('| **all** | **%d** |' % len(rows))
    L.append('| every kind it needs present | %d |' % sum(1 for r in rows.values() if r['needs'] and not r['missing']))
    L.append('| withdrawn ids | %d |' % len(m['req']['withdrawn']))
    st = [g['state'] for g in gaps]
    L += ['', '%d gaps: %d accepted, %d in the baseline, %d new (a new gap fails the check); %d warnings. "The flags" '
          'says what each kind of gap means; every gap is listed last.' % (
              len(gaps), st.count('accepted'), st.count('baseline'), st.count('NEW'), len(warns)), '']
    # 2. The requirements, area by area
    L += ['## Requirements, area by area', '',
          "Each row: its id and Requirement sentence; its status in requirements.md, release and MoSCoW; the kinds of "
          'evidence present and still missing; its tests and observations; its issues, with titles (every `#n` in the '
          "row's Status, Sources and Evidence, and every issue whose Requirement: line names it). A row's corpus "
          'swings are one line: the count, then the count by date (each swing\'s build is in '
          '`rtm_observations.csv`).', '']
    for sec in m['req']['sections']:
        L += ['### ' + sec, '']
        for rid in m['req']['order']:
            if rows[rid]['section'] == sec:
                L += _row_lines(m, rid)
        if L[-1] != '':
            L.append('')
    # 3. The flags
    L += ['## The flags', '', 'Each flag is a kind of gap. A gap fails `rtm_check` unless it is an accepted gap '
          '(`rtm_accepted_gaps.txt`) or in the baseline (`rtm_baseline.txt`), which only shrinks.', '',
          '| Flag | What it means | Gaps | Accepted | Baseline | New |', '|---|---|---|---|---|---|',
          '| 1. %s | %s | checked by `--check` | | | |' % (md(FLAG_TITLES[1]), md(FLAG_HELP[1]))]
    for f in range(2, 14):
        gs = [g for g in gaps if g['flag'] == f]
        L.append('| %d. %s | %s | %d | %d | %d | %d |' % (f, md(FLAG_TITLES[f]), md(FLAG_HELP[f]), len(gs),
                 sum(1 for g in gs if g['state'] == 'accepted'), sum(1 for g in gs if g['state'] == 'baseline'),
                 sum(1 for g in gs if g['state'] == 'NEW')))
    L += ['| warnings | Printed by every mode; they never fail the check. | %d | | | |' % len(warns), '']
    L += ['## Warnings', '', 'Printed by every mode; they never fail the check.', '']
    L += ['- ' + md(x) for x in warns] or ['None.']
    L.append('')
    # 4. The controls, the labelled tests and the documents
    L += ['## Controls checked by tests', '']
    ctl = nat_sorted(k for k in m['by_req'] if k.startswith('CTL-'))
    if ctl:
        L += ['| Control | Tests |', '|---|---|']
        L += ['| %s | %s |' % (md(id_words(m, c)), ', '.join('`%s`' % t for t in m['by_req'][c])) for c in ctl]
    else:
        L.append('None yet.')
    L.append('')
    labelled = [t for t in nat_sorted(tests) if tests[t]['ids']]
    L += ['## Labelled tests', '', '%d tests are registered in the source; %d carry a requirement or control label. '
          'Gate lists: the platform lists that hold the test.' % (len(tests), len(labelled)), '']
    if labelled:
        L += ['| Test | Kind | Labels | Gate lists | Registered in |', '|---|---|---|---|---|']
        for t in labelled:
            tt = tests[t]
            L.append('| `%s` | %s | %s | %s | %s |' % (
                t, md(kinds_in_words(tt['kinds'])), md('; '.join(id_words(m, x) for x in nat_sorted(tt['ids']))),
                ', '.join(tt['lists']) or '-', ', '.join('`%s`' % f for f in tt['files'])))
    else:
        L.append('None yet.')
    L.append('')
    docs = {}
    for rid in m['req']['order']:
        for name in re.findall(r'(?<![\w.-])([\w.-]+\.md)(?![\w-])', rows[rid]['sources']):
            docs.setdefault(name, []).append(rid)
    L += ['## Documents and the rows that cite them', '', 'The reverse view of the Sources column, the one home of the '
          'design link.', '', '| Document | Rows |', '|---|---|']
    for name in sorted(docs):
        L.append('| %s | %s |' % (md(name), md('; '.join(id_words(m, x) for x in nat_sorted(set(docs[name]))))))
    L.append('')
    # 5. Every gap, last
    L += ['## Every gap, flag by flag', '', 'Each line says the gap in words. The small print after it is the gap as '
          'the baseline and the accepted gaps spell it, and its state: accepted, baseline, or new (a new gap fails the '
          'check).', '']
    for f in range(2, 14):
        L += ['### %d. %s' % (f, md(FLAG_TITLES[f])), '']
        gs = [g for g in gaps if g['flag'] == f]
        if not gs:
            L.append('None.')
        if f == 3:
            by_row = {}
            for g in gs:
                by_row.setdefault(g['row'], []).append(g)
            for rid in m['req']['order']:
                if rid not in by_row:
                    continue
                kinds = [g['key'].split(' ')[-1] for g in by_row[rid]]
                text = '%s: %s; %s' % (id_words(m, rid), STATUS_PLAIN[rows[rid]['status']['base']],
                                       kinds_in_words(kinds, True))
                L.append('- %s <sub>%s</sub>' % (md(text), '; '.join(
                    '`%s` %s' % (g['key'], STATE_WORDS[g['state']]) for g in by_row[rid])))
        else:
            for g in gs:
                L.append('- %s <sub>`%s` %s</sub>' % (md(g['msg']), g['key'], STATE_WORDS[g['state']]))
        L.append('')
    return '\n'.join(L)


def generate(root):
    m = load(root)
    gaps, warns = evaluate(m)
    return m, gaps, warns, render(m, gaps, warns)


# ---------------------------------------------------------------------------------------------------------------------
# The modes

def baseline_text(gaps):
    keys = nat_sorted(set(g['key'] for g in gaps if 2 <= g['flag'] <= 12 and g['state'] != 'accepted'))
    head = ['# rtm_baseline.txt: today\'s gaps, to burn down. Generated once by `tools/method/make_rtm.py --init`;',
            '# never hand-edited, and no command regrows it. A line whose gap has closed fails rtm_check (flag 13) until',
            '# it is taken out, so the baseline only shrinks. Each line is "<flag> <gap>", as rtm.md spells it:',
            '#   2 <id> <where it is cited>   3 <row> <missing kind>   4 <test>   5 #<issue>   6 #<bug> <row>',
            '#   7 <row>   8 <row>   9 <test>   10 <row>   11 <row>   12 <row>',
            '# The one allowed regrowth: a ruled change to rtm_kind_defaults.csv, regenerated with --force-init in the',
            '# same commit as the change, which says so. A mechanical rename of an unlabelled test needs no new label',
            '# (BT-39): `make_rtm.py --rename <old> <new>` swaps its "4 <old>" line for "4 <new>", one line for one.']
    return '\n'.join(head + keys) + '\n'


def report(gaps, warns, stale, out):
    new = [g for g in gaps if g['state'] == 'NEW']
    for w in warns:
        out.write('warning: %s\n' % w)
    for f in range(2, 14):
        gs = [g for g in gaps if g['flag'] == f]
        if gs:
            out.write('flag %d (%s): %d gaps, %d new\n' % (f, FLAG_TITLES[f], len(gs),
                                                          sum(1 for g in gs if g['state'] == 'NEW')))
    for g in new:
        out.write('FLAG %s: %s\n' % (g['key'], g['msg']))
    if stale:
        out.write('FLAG 1: %s is stale: run python3 tools/method/make_rtm.py and commit it\n' % P_RTM)
    return 1 if (new or stale) else 0


def mode_check(root, out):
    m, gaps, warns, text = generate(root)
    if m['baseline'] is None:
        out.write('%s is missing: run --init once\n' % P_BASELINE)
        return 1
    cur = read_text(root, P_RTM)
    rc = report(gaps, warns, cur != text, out)
    out.write('rtm_check: %s\n' % ('fail' if rc else 'ok'))
    return rc


def mode_write(root, out):
    m, gaps, warns, text = generate(root)
    write_text(root, P_RTM, text)
    report(gaps, warns, False, out)
    out.write('wrote %s\n' % P_RTM)
    return 0


def mode_init(root, out, force):
    if read_text(root, P_BASELINE) is not None and not force:
        raise RtmError('%s exists; the baseline is written once (--force-init only with a ruled change to %s)'
                       % (P_BASELINE, P_KINDS))
    if os.path.isfile(os.path.join(root, *P_BASELINE.split('/'))):
        os.remove(os.path.join(root, *P_BASELINE.split('/')))
    m, gaps, warns, _ = generate(root)
    write_text(root, P_BASELINE, baseline_text(gaps))
    m, gaps, warns, text = generate(root)
    write_text(root, P_RTM, text)
    sizes = {}
    for g in gaps:
        if g['state'] == 'baseline':
            sizes[g['flag']] = sizes.get(g['flag'], 0) + 1
    for f in sorted(sizes):
        out.write('baseline: flag %d: %d lines\n' % (f, sizes[f]))
    out.write('wrote %s (%d lines) and %s\n' % (P_BASELINE, sum(sizes.values()), P_RTM))
    return 0


def mode_rename(root, old, new, out):
    """Swaps the baseline's "4 <old>" for "4 <new>"; every refusal leaves the baseline untouched."""
    m = load(root)
    if m['baseline'] is None:
        raise RtmError('--rename: %s is missing' % P_BASELINE)
    keys = [g['key'] for g in m['baseline']]
    if '4 ' + old not in keys:
        raise RtmError('--rename: "4 %s" is not in the baseline: only an unlabelled test the baseline holds is renamed'
                       % old)
    if old in m['tests']:
        raise RtmError('--rename: %s is still registered (%s): a rename leaves the old name registered nowhere' % (
            old, ', '.join(m['tests'][old]['files'])))
    if new not in m['tests']:
        raise RtmError('--rename: %s is registered nowhere in the source' % new)
    if m['tests'][new]['ids']:
        raise RtmError('--rename: %s carries labels (%s): it is no gap, so take "4 %s" out of the baseline instead' % (
            new, ' '.join(nat_sorted(m['tests'][new]['ids'])), old))
    if '4 ' + new in keys:
        raise RtmError('--rename: "4 %s" is already in the baseline' % new)
    text = read_text(root, P_BASELINE)
    lines = text.split('\n')
    head = [l for l in lines if l.strip().startswith('#') or not l.strip()]
    body = nat_sorted([k for k in keys if k != '4 ' + old] + ['4 ' + new])
    if len(body) != len(keys):
        raise RtmError('--rename: the baseline would change size (%d to %d)' % (len(keys), len(body)))
    write_text(root, P_BASELINE, '\n'.join([l for l in head if l.strip()] + body) + '\n')
    m, gaps, warns, text = generate(root)
    write_text(root, P_RTM, text)
    out.write('renamed "4 %s" to "4 %s" in %s (%d lines) and rewrote %s\n' % (old, new, P_BASELINE, len(body), P_RTM))
    return 0


def parse_marked_line(body, word):
    """None when no `<word>:` line; ['none'] for "none"; else the ids found (possibly empty). Never any other text."""
    found, any_line = [], False
    for line in body.replace('\r\n', '\n').split('\n'):
        mm = re.match(r'^\s*[*_>-]*\s*[*_]*' + word + r's?[*_]*\s*:[*_]*\s*(.*)$', line, re.I)
        if not mm:
            continue
        any_line = True
        rest = mm.group(1).strip().strip('*_`')
        if re.match(r'none\b', rest, re.I):
            found.append('none')
        elif word == 'Requirement':
            found += REQ_FIND.findall(rest)
        else:
            found += ['CTL-' + x for x in re.findall(r'(?<![A-Za-z0-9-])(?:CTL-)?([A-Z]+-[0-9]+[a-z]?)(?![A-Za-z0-9-])',
                                                     rest)]
    if not any_line:
        return None
    return nat_sorted(set(found))


SESSION_FOLDER = re.compile(r'^[0-9]{4}-[0-9]{2}-[0-9]{2}_([^_]+)_')


def golfer_names(library):
    """The golfers' names, read from the library's folders and never written or printed: every folder that holds an
    athlete.json, and the name in every session folder's name (<date>_<name>_...), up to three levels down (the
    athlete folders, and the corpus's copies), never inside a hidden folder (the library's trash). It fails closed: a
    missing library, or one with no athlete folder, is an error, never an empty list."""
    if not library or not os.path.isdir(library):
        raise RtmError('--refresh-issues: the library folder (--library) is missing or not a folder')
    names = set()

    def scan(d, depth):
        try:
            entries = sorted(os.listdir(d))
        except OSError as e:
            # Fail closed: a folder it cannot read could hold a golfer's name. The folder is never named.
            raise RtmError('--refresh-issues: a folder in the library folder (--library) could not be read (%s); '
                           'nothing was written' % (e.strerror or type(e).__name__))
        for e in entries:
            p = os.path.join(d, e)
            if e.startswith('.') or not os.path.isdir(p):
                continue
            sm = SESSION_FOLDER.match(e)
            if sm:
                names.add(sm.group(1))
                continue
            if os.path.isfile(os.path.join(p, 'athlete.json')):
                names.add(e)
            if depth < 3:
                scan(p, depth + 1)
    scan(library, 1)
    if not names:
        raise RtmError('--refresh-issues: the library folder (--library) holds no athlete folder')
    return sorted(names)


def golfer_title_hits(titles, names):
    """The issues whose title names a golfer: any part of a name (split at - _ . and spaces) that starts with a
    capital, as a whole word, case-sensitive. A name with no capital (a developer's test athlete) names no one; a
    library with no name it can match is an error (it fails closed), never a pass."""
    parts = sorted(set(p for n in names for p in re.split(r'[-_. ]+', n) if len(p) >= 2 and p[0].isupper()))
    if not parts:
        raise RtmError('--refresh-issues: the library folder (--library) holds no golfer\'s name the check can match')
    rx = re.compile(r'(?<![A-Za-z])(?:' + '|'.join(re.escape(p) for p in parts) + r')(?![A-Za-z])')
    return sorted(n for n, t in titles.items() if t and rx.search(t))


def snapshot_from_gh(items, cited, names):
    """gh's JSON (number, title, state, labels, milestone, body) -> the committed snapshot's text: the title kept, the
    body never. A kept issue whose title names a golfer (POL-7) stops it with an error, naming only the issue."""
    out = []
    for it in items:
        n = int(it['number'])
        body = it.get('body') or ''
        req = parse_marked_line(body, 'Requirement')
        ctl = parse_marked_line(body, 'Control')
        state = str(it.get('state', '')).lower()
        if state != 'open' and n not in cited and req is None and ctl is None:
            continue
        ms = it.get('milestone')
        out.append({'number': n, 'title': ' '.join(str(it.get('title') or '').split()) or None, 'state': state,
                    'labels': sorted(l['name'] for l in it.get('labels') or []),
                    'milestone': ms.get('title') if isinstance(ms, dict) else None,
                    'requirement': req, 'control': ctl})
    out.sort(key=lambda d: d['number'])
    hits = golfer_title_hits({d['number']: d['title'] for d in out}, names)
    if hits:
        raise RtmError('the snapshot is not written: retitle each issue, then refresh again:\n' + '\n'.join(
            "issue #%d's title names a golfer" % n for n in hits))
    doc = {'about': 'The issue snapshot rtm_check reads offline, written by tools/method/make_rtm.py --refresh-issues: '
                    'every open issue, every issue requirements.md cites, and every issue with a Requirement: or '
                    'Control: line. Number, title, state, labels, milestone and the parsed ids only: never a body '
                    '(POL-7), and a refresh whose titles would name a golfer writes nothing. requirement/control: '
                    'null when the issue has no such line, ["none"] for "none".',
           'issues': out}
    return json.dumps(doc, indent=1, sort_keys=True, ensure_ascii=False) + '\n'


def mode_refresh(root, out, library):
    names = golfer_names(library)
    gh = shutil.which('gh')
    if not gh:
        raise RtmError('gh is not on PATH')
    limit = 5000
    r = subprocess.run([gh, 'issue', 'list', '--state', 'all', '--limit', str(limit), '--json',
                        'number,title,state,labels,milestone,body'], cwd=root, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise RtmError('gh issue list failed (%d)' % r.returncode)
    items = json.loads(r.stdout.decode('utf-8'))
    if len(items) >= limit:
        raise RtmError('gh returned %d issues, the limit: raise it' % len(items))
    req_text = read_text(root, P_REQ) or ''
    cited = set(int(x) for x in ISSUE_REF.findall(req_text))
    text = snapshot_from_gh(items, cited, names)
    write_text(root, P_ISSUES, text)
    doc = json.loads(text)
    have = set(i['number'] for i in doc['issues'])
    out.write('wrote %s: %d issues (%d open) of %d on GitHub\n' % (
        P_ISSUES, len(have), sum(1 for i in doc['issues'] if i['state'] == 'open'), len(items)))
    missing = sorted(cited - set(int(i['number']) for i in items))
    if missing:
        out.write('cited by requirements.md but not an issue: %s\n' % ', '.join('#%d' % n for n in missing))
    return 0


def platform_list_text(names, platform, commit):
    head = ['# rtm_platform_tests/%s.txt: the tests a %s test tree configured, one name per line, read by' % (
            platform, platform),
            '# tools/method/make_rtm.py (test_strategy.md §8.3). Written by `make_rtm.py --platform-list`, from',
            '# `ctest --show-only=json-v1`, on a development test tree at %s.' % commit,
            "# It is not yet the release gate's list: BT-25's release gate will rewrite it."]
    return '\n'.join(head + nat_sorted(set(names))) + '\n'


def mode_platform_list(root, tree, platform, ctest, out):
    if not re.fullmatch(r'[a-z0-9_-]+', platform):
        raise RtmError('platform %r: lower-case letters, digits, - and _ only' % platform)
    if not ctest:
        cache = os.path.join(tree, 'CMakeCache.txt')
        if os.path.isfile(cache):
            with open(cache, encoding='utf-8', errors='replace') as f:
                for line in f:
                    if line.startswith('CMAKE_COMMAND:'):
                        cand = os.path.join(os.path.dirname(line.split('=', 1)[1].strip()), 'ctest')
                        if os.path.isfile(cand) or os.path.isfile(cand + '.exe'):
                            ctest = cand
        ctest = ctest or shutil.which('ctest')
    if not ctest:
        raise RtmError('no ctest found: pass --ctest <path>')
    r = subprocess.run([ctest, '--show-only=json-v1', '--test-dir', tree], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise RtmError('ctest --show-only failed (%d) in %s' % (r.returncode, tree))
    names = [t['name'] for t in json.loads(r.stdout.decode('utf-8')).get('tests', [])]
    commit = 'an unknown commit'
    g = shutil.which('git')
    if g:
        h = subprocess.run([g, 'rev-parse', '--short=8', 'HEAD'], cwd=root, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
        if h.returncode == 0:
            commit = 'commit ' + h.stdout.decode().strip()
            d = subprocess.run([g, 'status', '--porcelain', '--untracked-files=no'], cwd=root,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if d.stdout.strip():
                commit += ', with uncommitted changes'
    write_text(root, '%s/%s.txt' % (P_PLATFORMS, platform), platform_list_text(names, platform, commit))
    m = load(root)
    unknown = nat_sorted(set(names) - set(m['tests']))
    out.write('wrote %s/%s.txt: %d tests\n' % (P_PLATFORMS, platform, len(set(names))))
    for t in unknown:
        out.write('warning: %s is configured but the source scan does not find its registration\n' % t)
    return 1 if unknown else 0


# ---------------------------------------------------------------------------------------------------------------------
# The self-test: every flag on fixtures, red then green; the loops; determinism

FIX_REQ = """# Fixture requirements

## Analysis and metrics

| Id | Owner | Requirement | Why | Sources | Status | Release | MoSCoW | Evidence |
|---|---|---|---|---|---|---|---|---|
| REQ-ANA-1 | PO | The app shall compute A \\| B <now> for every *swing* the golfer hits in the bay, from every camera in use.[^x] *The rule is GU-9; this row only traces it.* | Why. | a_design.md §2 | delivered | v1 | Must | here |
| REQ-ANA-2 | PO | The app shall compute B. | Why. | #2 | planned (#2) | v1 | Should | |

## Capture

| Id | Owner | Requirement | Why | Sources | Status | Release | MoSCoW | Evidence |
|---|---|---|---|---|---|---|---|---|
| REQ-CAP-1 | PO | The app shall capture. | Why. | GU-1 | delivered[^x] | v1 | Must | seen |
| REQ-LIB-1 | PO | The app shall keep it. | Why. | GU-2 | planned (#2) | v1 | Could | |
| REQ-LIB-2 | PO | The app shall file it. | Why. | GU-3 | proposed | v2 | Could | #3 |
| REQ-LIB-3 | PO | The app shall not. | Why. | GU-4 | won't have | never | | |

## Withdrawn before approval

| Id | Why |
|---|---|
| REQ-LIB-4 | folded |

[^x]: a footnote.
"""
FIX_KINDS = "# fixture defaults\narea,needs,classes\nANA,E1 E3,x\nCAP,E4 E6,x\nLIB,E1,x\n"
FIX_UC = "req_id,status,epics,use_cases,as_step,as_path,as_constraint\nREQ-ANA-1,delivered,EP-1,UC-1,UC-1,,\n"
FIX_OBS = (','.join(OBS_HEADER) + '\nOBS-1,2026-11-26,REQ-ANA-1,abc12345 macOS arm64 Debug,E3,a parity run,'
           'pass: unchanged,build/x,agent\nOBS-2,2026-11-26,REQ-CAP-1,abc12345 macOS arm64 Debug,E6,a swing,'
           'pass: captured,visit 1 §2,Product Owner\n')
FIX_ISSUES = [
    {'number': 1, 'state': 'open', 'labels': ['bug'], 'milestone': None, 'requirement': ['REQ-LIB-1'], 'control': None,
     'title': 'Files go missing'},
    {'number': 2, 'state': 'open', 'labels': ['enhancement'], 'milestone': 'v1', 'requirement': ['REQ-LIB-1'],
     'control': None, 'title': 'Keep it | fast <soon> *now*'},
    {'number': 3, 'state': 'open', 'labels': ['enhancement'], 'milestone': None, 'requirement': ['REQ-LIB-2'],
     'control': None},
    {'number': 4, 'state': 'open', 'labels': ['engineering'], 'milestone': None, 'requirement': None,
     'control': ['CTL-BT-39']},
]
FIX_CONTROLS = "# Test\n\n**BT-39.** A test names what it proves.\n\n**AG-23.** Retired.\n"
FIX_HELPERS = """# helpers
function(pp_add_test name)
    add_executable(${name} ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()
"""
FIX_SUITE = """# a suite
pp_add_test(ana_test SOURCES ana_test.cpp ${SRC}/x.cpp)
pp_requirement(ana_test REQ-ANA-1)
#[[ a bracket comment: add_test(NAME commented_out COMMAND x) ]]
# add_test(NAME also_commented COMMAND x)
foreach(level 1 3)
    pp_add_test(loop_l${level}
        SOURCES loop.cpp
        DEFINES LEVEL=${level})
    pp_requirement(loop_l${level} CTL-BT-39)
endforeach()
set(_pins a
          b)
if(UNIX)
    foreach(_pin IN LISTS _pins)
        add_test(NAME quit.${_pin} COMMAND x --pin ${_pin})
        set_tests_properties(quit.${_pin} PROPERTIES TIMEOUT 9 LABELS quit_pins)
        pp_requirement(quit.${_pin} CTL-BT-39)
    endforeach()
endif()
foreach(_row IOP-1 IOP-2)
    string(TOLOWER "${_row}" _rowname)
    string(REPLACE "-" "_" _rowname "${_rowname}")
    add_test(NAME iop_${_rowname} COMMAND x ${_row})
    pp_requirement(iop_${_rowname} CTL-BT-39)
endforeach()
"""
FIX_GUI = """# the QML loop
file(GLOB _pp_qml_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_*.qml")
foreach(_q IN LISTS _pp_qml_tests)
    get_filename_component(_n "${_q}" NAME_WE)
    add_test(NAME qml_ui.${_n} COMMAND qml_ui_test -input "${_q}")
    set_tests_properties(qml_ui.${_n} PROPERTIES ENVIRONMENT "A=1;B=2" LABELS qml_ui TIMEOUT 9)
    file(STRINGS "${_q}" _req REGEX "^[ \\t]*//[ \\t]*REQ:")
    foreach(_l IN LISTS _req)
        string(REGEX MATCHALL "(REQ-[A-Z]+-[0-9]+|CTL-[A-Z]+-[0-9]+[a-z]?)" _ids "${_l}")
        set_property(TEST qml_ui.${_n} APPEND PROPERTY LABELS ${_ids})
    endforeach()
endforeach()
"""
FIX_QML = "import QtQuick\n// REQ: REQ-CAP-1\nTestCase { name: \"A\" }\n"
FIX_LOOPS = ("# fixture loop table\nsrc/A/tests/CMakeLists.txt | iop_${_rowname} | IOP-1 | iop_iop_1\n"
             "src/A/tests/CMakeLists.txt | iop_${_rowname} | IOP-2 | iop_iop_2\n")
FIX_ALL_TESTS = ['ana_test', 'iop_iop_1', 'iop_iop_2', 'loop_l1', 'loop_l3', 'qml_ui.tst_a', 'quit.a', 'quit.b']
FIX_ACCEPTED_HEAD = '# accepted gaps (fixture)\n'


def fixture_files():
    return {
        P_REQ: FIX_REQ, P_KINDS: FIX_KINDS, P_UC: FIX_UC, P_OBS: FIX_OBS,
        P_ISSUES: json.dumps({'about': 'fixture', 'issues': FIX_ISSUES}, indent=1, sort_keys=True) + '\n',
        P_ACCEPTED: FIX_ACCEPTED_HEAD, P_BASELINE: '# baseline (fixture)\n',
        P_PLATFORMS + '/macos.txt': '# fixture\n' + '\n'.join(FIX_ALL_TESTS) + '\n',
        P_CONTROLS + '/test.md': FIX_CONTROLS,
        P_DESIGN + '/a_design.md': '# A\n',
        'CMakeLists.txt': '# root\n',
        'tests/cmake/PinPointTests.cmake': FIX_HELPERS,
        'src/A/tests/CMakeLists.txt': FIX_SUITE,
        'src/Gui/CMakeLists.txt': FIX_GUI,
        P_QML + '/tst_a.qml': FIX_QML,
        P_LOOPS: FIX_LOOPS,
        'build/tests/_deps/x/CMakeLists.txt': 'add_test(NAME from_a_build_tree COMMAND x)\n',
    }


def make_fixture(base, files, crlf=False):
    root = tempfile.mkdtemp(dir=base)
    for rel, text in files.items():
        if text is None:
            continue
        write_text(root, rel, text.replace('\n', '\r\n') if crlf else text)
    return root


def issues_with(changes=None, extra=None):
    out = []
    for i in FIX_ISSUES:
        d = dict(i)
        d.update((changes or {}).get(i['number'], {}))
        out.append(d)
    out += extra or []
    return json.dumps({'about': 'fixture', 'issues': out}, indent=1, sort_keys=True) + '\n'


def self_test():
    results = []

    def ok(cond, what):
        results.append((bool(cond), what))
        print('%s  %s' % ('ok  ' if cond else 'FAIL', what))

    base = tempfile.mkdtemp(prefix='rtm_selftest_')
    try:
        green_files = fixture_files()

        def run(files):
            root = make_fixture(base, files)
            return root, generate(root)

        def keys_of(gaps, flag):
            return [g['key'] for g in gaps if g['flag'] == flag]

        # The green fixture: every flag clear, every loop form expanded, the helper's body skipped.
        root, (m, gaps, warns, text) = run(green_files)
        ok(set(m['tests']) == set(FIX_ALL_TESTS), 'the scan finds exactly the fixture\'s tests (literal, foreach, IN '
           'LISTS over a set(), the loop table, the QML glob; none from a function body, a comment or a build tree): '
           '%s' % ' '.join(nat_sorted(m['tests'])))
        ok(m['tests']['qml_ui.tst_a']['kinds'] == ['E4'] and m['tests']['ana_test']['kinds'] == ['E1'],
           'qml_ui means E4, anything else E1')
        ok(m['tests']['qml_ui.tst_a']['ids'] == {'REQ-CAP-1'}, 'a // REQ: line labels its QML test')
        ok(not gaps, 'green fixture: no gap (%s)' % ', '.join(g['key'] for g in gaps))
        ok(not warns, 'green fixture: no warning (%s)' % '; '.join(warns))
        write_text(root, P_RTM, text)
        ok(mode_check(root, io.StringIO()) == 0, 'green fixture: --check exits 0')
        ok(root not in text and '\r' not in text and '\\' not in text.replace('\\|', ''),
           'rtm.md holds no absolute path, no CR and no backslash path')
        ok(generate(root)[3] == text, 'two runs give the same rtm.md')
        crlf_root = make_fixture(base, green_files, crlf=True)
        ok(generate(crlf_root)[3] == text, 'a fixture written with CRLF gives the same rtm.md, byte for byte')

        # The page made readable: words beside every id, a title beside every issue, the flags in plain words, and an
        # order for a reader.
        short = 'REQ-ANA-1 (The app shall compute A &#124; B &lt;now&gt; for every swing the …)'
        ok(short in text, 'an id carries its Requirement sentence, shortened to 12 words at a word boundary with '
           '"…", | and < escaped, emphasis stripped: %s' % short)
        ok('**REQ-ANA-1**: The app shall compute A &#124; B &lt;now&gt; for every swing the golfer hits in the bay, '
           'from every camera in use.' in text, "a row's own entry carries its whole first Requirement sentence")
        ok('[^' not in text and '&#91;^' not in text, 'no footnote marker reaches rtm.md, raw or escaped')
        ok('REQ-CAP-1 (The app shall capture.)' in text, 'a short Requirement sentence is shown whole')
        ok(short_words('The app shall add everything a device reports for a shot (a reading, a clip) to it.')
           == 'The app shall add everything a device reports for a shot to it.',
           'a cut that would open a bracket leaves the bracketed aside out, never stops before it')
        bt40 = short_words('A test shown unable to fail (it stayed green with what it proves broken, on any '
                           'platform) must be strengthened or retired; it must never be kept as proof.')
        ok(bt40.startswith('A test shown unable to fail must be strengthened or retired'),
           'a control\'s words never read as its opposite: the aside goes, the rule stays (%s)' % bt40)
        ok(short_words('The app shall show one two three four five six seven (and never close it at all') ==
           'The app shall show one two three four five six seven \u2026',
           'a bracket that never closes is cut before, as before')
        ok('CTL-BT-39 (A test names what it proves.)' in text, "a control id carries its control's first sentence")
        fb = dict(green_files)
        fb[P_REQ] = FIX_REQ.replace('| REQ-ANA-2 | PO | The app shall compute B. |',
                                    '| REQ-ANA-2 | PO | The app shall compute B for REQ-CAP-1, as #2 asks. |')
        tb = run(fb)[1][3]
        ok('- **REQ-ANA-2**: The app shall compute B for REQ-CAP-1 (The app shall capture.), as #2 (Keep it &#124; '
           'fast &lt;soon&gt; &#42;now&#42;) asks.\n' in tb,
           "an id or #n inside a row's own sentence carries its words too")
        ok('| partial (requirements.md) |' in text and '(the page)' not in text and 'partly built' not in text
           and 'the page says' not in text and 'the page calls' not in text,
           'one word for a status everywhere ("partial"), and requirements.md named, never "the page"')
        lib3 = text[text.find('- **REQ-LIB-3**'):]
        lib3 = re.split(r'\n(?:- \*\*|\n)', lib3)[0]
        ok("  - Evidence: none needed (won't have).\n" in lib3 + '\n' and 'yet' not in lib3,
           "a won't-have row lists no missing kinds: %r" % lib3)
        ok('#2 (Keep it &#124; fast &lt;soon&gt; &#42;now&#42;)' in text, 'an issue number carries its title, escaped')
        bad_rows = []
        cols = None
        for ln in text.split('\n'):
            if ln.startswith('|'):
                n = len(re.findall(r'(?<!\\)\|', ln))
                if cols is None:
                    cols = n
                elif n != cols:
                    bad_rows.append(ln[:60])
            else:
                cols = None
        ok(not bad_rows, 'every table row has its header\'s cells: an escaped | never splits one (%s)' % bad_rows)
        legend = ['an automated test of the logic (E1)', 'a frozen record of old behaviour (E2)',
                  'a run over real swings (E3)', 'an automated screen test (E4)', 'a run on Windows (E5)',
                  'seen at the rig or in a corpus swing (E6)', 'the installer checked (E7)']
        ok(all(x in text for x in legend), 'the page names every kind of evidence in words, its code in brackets')
        heads = ['## Summary', '## Requirements, area by area', '## The flags', '## Warnings',
                 '## Controls checked by tests',
                 '## Labelled tests', '## Documents and the rows that cite them', '## Every gap, flag by flag']
        pos = [text.find(h) for h in heads]
        ok(all(p >= 0 for p in pos) and pos == sorted(pos), 'the page runs: summary, requirements area by area, the '
           'flags, the controls, tests and documents, the full gap list last (%s)' % pos)

        def flag_case(flag, files, expect, what):
            # Any other flag the fault also raises is held in the fixture's baseline, so the only NEW gap, and so
            # --check's exit 1, is this flag's.
            r, (mm, gs, ws, tx) = run(files)
            hits = keys_of(gs, flag)
            other = nat_sorted(set(g['key'] for g in gs if g['flag'] not in (flag, 13)))
            if other:
                write_text(r, P_BASELINE, read_text(r, P_BASELINE) + '\n'.join(other) + '\n')
            _, gs2, _, tx2 = generate(r)
            write_text(r, P_RTM, tx2)
            rc = mode_check(r, io.StringIO())
            new = sorted(set(g['flag'] for g in gs2 if g['state'] == 'NEW'))
            ok(expect in hits and rc == 1 and new == [flag], 'flag %d red: %s -> %s; the only new flag is %s, '
               '--check exits %d%s' % (flag, what, expect, new, rc,
                                       ' (also raised, held in the fixture baseline: %s)' % ', '.join(other)
                                       if other else ''))
            g = generate(root)[1]
            ok(not keys_of(g, flag), 'flag %d green: the green fixture has none' % flag)
            return r

        # 1: stale.
        r = make_fixture(base, green_files)
        write_text(r, P_RTM, generate(r)[3].replace('# Requirements', '# Requirement', 1))
        out = io.StringIO()
        rc = mode_check(r, out)
        ok(rc == 1 and 'FLAG 1:' in out.getvalue(), 'flag 1 red: a hand-edited rtm.md -> stale, --check exits %d' % rc)
        write_text(r, P_RTM, generate(r)[3])
        ok(mode_check(r, io.StringIO()) == 0, 'flag 1 green: regenerated, --check exits 0')

        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE.replace('pp_requirement(ana_test REQ-ANA-1)',
                                                            'pp_requirement(ana_test REQ-ANA-1 REQ-ANA-99)')
        flag_case(2, f, '2 REQ-ANA-99 test:ana_test', 'pp_requirement names an unknown id')
        f = dict(green_files)
        f[P_UC] = FIX_UC + 'REQ-LIB-4,planned,,,,,\n'
        flag_case(2, f, '2 REQ-LIB-4 uc-map', 'uc-map.csv names a withdrawn id')
        f = dict(green_files)
        f[P_ISSUES] = issues_with({4: {'control': ['CTL-AG-23']}})
        flag_case(2, f, '2 CTL-AG-23 issue:#4', 'an issue names a retired control')

        f = dict(green_files)
        f[P_REQ] = FIX_REQ.replace('| REQ-ANA-2 | PO | The app shall compute B. | Why. | #2 | planned (#2) |',
                                   '| REQ-ANA-2 | PO | The app shall compute B. | Why. | #2 | partial (#2) |')
        r3a = flag_case(3, f, '3 REQ-ANA-2 E1', 'a partial row with no test')
        t3a = generate(r3a)[3]
        ok('REQ-ANA-2 (The app shall compute B.): partial; no automated test of the logic yet (E1), and no run '
           'over real swings yet (E3)' in t3a and '`3 REQ-ANA-2 E1` new' in t3a and '`3 REQ-ANA-2 E3` new' in t3a,
           'flag 3 in words: one sentence for the row, its missing kinds named, each gap\'s key and state in small '
           'print')
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE + 'pp_add_test(ana2_test SOURCES x.cpp)\npp_requirement(ana2_test REQ-ANA-2)\n'
        f[P_PLATFORMS + '/macos.txt'] = green_files[P_PLATFORMS + '/macos.txt'] + 'ana2_test\n'
        r3 = flag_case(3, f, '3 REQ-ANA-2 E3', 'every kind: a partial row with its E1 test and no E3 run')
        ok('3 REQ-ANA-2 E1' not in keys_of(generate(r3)[1], 3), 'every kind: the kind present (E1) is not flagged; '
           'only the missing one (E3) is')

        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE + 'add_test(NAME bare_test COMMAND bare)\n'
        f[P_PLATFORMS + '/macos.txt'] = green_files[P_PLATFORMS + '/macos.txt'] + 'bare_test\n'
        r4 = flag_case(4, f, '4 bare_test', 'a test with no label')

        f = dict(green_files)
        f[P_ISSUES] = issues_with({3: {'requirement': None}})
        flag_case(5, f, '5 #3', 'an open enhancement with no Requirement: line')
        f[P_ISSUES] = issues_with({3: {'requirement': ['REQ-LIB-77']}})
        flag_case(5, f, '5 #3', 'an open enhancement naming an unknown id')

        f = dict(green_files)
        f[P_ISSUES] = issues_with(extra=[{'number': 5, 'state': 'open', 'labels': ['bug'], 'milestone': None,
                                          'requirement': ['REQ-ANA-1'], 'control': None}])
        r6 = flag_case(6, f, '6 #5 REQ-ANA-1', 'an open bug names a delivered row')
        g6 = generate(r6)[1]
        ok('6 #5 REQ-ANA-1' in [g['key'] for g in g6] and not keys_of(g6, 11),
           'a delivered row with every kind present and an open bug raises flag 6 and not flag 11: a regression '
           'never lowers what the evidence supports')

        f = dict(green_files)
        f[P_ISSUES] = issues_with({1: {'state': 'closed'}, 2: {'state': 'closed'}})
        flag_case(7, f, '7 REQ-LIB-1', 'a planned row whose issues are all closed')
        f = dict(green_files)
        f[P_REQ] = FIX_REQ.replace('| GU-2 | planned (#2) |', '| GU-2 | planned (platform refactor, Stage C) |')
        f[P_ISSUES] = issues_with({1: {'requirement': None}, 2: {'requirement': None}})
        r7 = make_fixture(base, f)
        ok(not keys_of(generate(r7)[1], 7), 'flag 7 is not raised for a planned row with no issue at all')

        f = dict(green_files)
        f[P_REQ] = FIX_REQ.replace('| GU-2 | planned (#2) |',
                                   '| GU-2 | built, not verified \u00b7 proposed, for the Solution Architect |')
        flag_case(8, f, '8 REQ-LIB-1', 'a built row waiting on a ruling change')

        f = dict(green_files)
        f[P_PLATFORMS + '/macos.txt'] = '# fixture\n' + '\n'.join(t for t in FIX_ALL_TESTS if t != 'ana_test') + '\n'
        flag_case(9, f, '9 ana_test', 'a labelled test in no gate list')
        del f[P_PLATFORMS + '/macos.txt']
        r9 = make_fixture(base, f)
        ok(not keys_of(generate(r9)[1], 9), 'flag 9 is not computed when no gate list exists')

        f = dict(green_files)
        f[P_OBS] = FIX_OBS.split('OBS-2')[0]
        r10 = flag_case(10, f, '10 REQ-CAP-1', 'a hardware row never observed')
        ok('REQ-CAP-1 (The app shall capture.) needs to be seen at the rig or in a corpus swing (E6), and never has '
           'been' in generate(r10)[3], 'flag 10 in words')

        f = dict(green_files)
        f[P_REQ] = FIX_REQ.replace('| a_design.md \u00a72 | delivered |', '| a_design.md \u00a72 | built, not verified |')
        flag_case(11, f, '11 REQ-ANA-1', 'every kind present, the page says built')
        f = dict(green_files)
        f[P_OBS] = FIX_OBS.replace('pass: unchanged', 'fail: changed')
        flag_case(11, f, '11 REQ-ANA-1', 'a failed observation is no evidence; the page says delivered')

        f = dict(green_files)
        f[P_ISSUES] = issues_with({3: {'state': 'closed'}})
        r12 = flag_case(12, f, '12 REQ-LIB-2', 'a proposed row whose only issue is closed')

        f = dict(green_files)
        f[P_BASELINE] = '# baseline\n4 gone_test\n'
        flag_case(13, f, '13 4 gone_test', 'the baseline lists a gap that has closed')

        # The baseline and the accepted gaps hold a gap; --init writes once.
        write_text(r4, P_ACCEPTED, FIX_ACCEPTED_HEAD + '4 bare_test | infrastructure | Solution Architect | 2026-11-26\n')
        write_text(r4, P_RTM, generate(r4)[3])
        ok(mode_check(r4, io.StringIO()) == 0, 'an accepted gap passes the check')
        write_text(r4, P_ACCEPTED, FIX_ACCEPTED_HEAD + '4 bare_test | infrastructure | Coaching Lead | 2026-11-26\n')
        try:
            generate(r4)
            ok(False, 'an accepted gap approved by another role is refused')
        except RtmError:
            ok(True, 'an accepted gap approved by another role is refused')
        write_text(r12, P_BASELINE, '# baseline\n')
        try:
            mode_init(r12, io.StringIO(), False)
            ok(False, '--init is refused when a baseline exists')
        except RtmError:
            ok(True, '--init is refused when a baseline exists')
        mode_init(r12, io.StringIO(), True)
        ok(read_text(r12, P_BASELINE).rstrip().endswith('12 REQ-LIB-2') and mode_check(r12, io.StringIO()) == 0,
           '--force-init writes the baseline; the check then passes')

        # A corpus swing replayed (E3, Record "corpus: ...") meets E6, in every area; a plain E3 line does not.
        corpus_obs = FIX_OBS.split('OBS-2')[0] + ('OBS-2,2026-11-26,REQ-CAP-1,abc12345 macOS arm64 Debug,E3,a corpus '
                                                  'swing replayed,pass: detected,"corpus: 2026-08-04 shot 7, captured on '
                                                  '1a2b3c4d",agent\n')
        f = dict(green_files)
        f[P_OBS] = corpus_obs
        rc_, (mc, gc, wc, tc) = run(f)
        ok('E6' in mc['req']['rows']['REQ-CAP-1']['present'] and not gc
           and '  - 1 corpus swing in 2026: 4 Aug (1).\n' in tc and '1a2b3c4d' not in tc and 'captured on' not in tc,
           'a corpus line meets the CAP row\'s E6, no gap, and the matrix shows "1 corpus swing in 2026: 4 Aug (1).", '
           'never its build')
        f[P_OBS] = corpus_obs.replace('"corpus: 2026-08-04 shot 7, captured on 1a2b3c4d"', 'build/replay-log')
        flag_case(10, f, '10 REQ-CAP-1', 'a plain E3 line, without corpus:, does not meet E6')
        f = dict(green_files)
        f[P_OBS] = corpus_obs
        f[P_ISSUES] = issues_with(extra=[{'number': 6, 'state': 'open', 'labels': ['bug'], 'milestone': None,
                                          'requirement': ['REQ-CAP-1'], 'control': None}])
        rb, (mb, gb, wb, tb) = run(f)
        ok('E6' in mb['req']['rows']['REQ-CAP-1']['present'] and '6 #6 REQ-CAP-1' in keys_of(gb, 6),
           'a regression never removes corpus evidence: E6 stays present, and the open bug shows beside it (flag 6)')
        for bad in ('corpus: Jane 2026-08-04 shot 7, captured on 1a2b3c4d',
                    'corpus: 2026-08-04 shot 7, captured on 1a2b3c4d (Jane)',
                    'corpus: 2026-08-04 Jane Golfer shot 7, captured on 1a2b3c4d'):
            f = dict(green_files)
            f[P_OBS] = corpus_obs.replace('corpus: 2026-08-04 shot 7, captured on 1a2b3c4d', bad)
            refused_here = None
            try:
                generate(make_fixture(base, f))
            except RtmError as e:
                refused_here = str(e)
            ok(refused_here is not None and 'rtm_observations.csv:3' in refused_here,
               'a corpus line naming anything but a date, a shot number and a build is refused: %r' % bad)

        # The captured-on build is optional (the swing's date is the proof); a time of day tells apart two swings
        # that share a date and a shot number, and is then required on each of them.
        def corpus_lines(*records):
            return FIX_OBS.split('OBS-2')[0] + ''.join(
                'OBS-%d,2026-11-26,REQ-CAP-1,unknown,E3,a corpus swing read,pass: detected,"%s",agent\n' % (n, rec)
                for n, rec in enumerate(records, 2))

        def refusal(obs):
            fx = dict(green_files)
            fx[P_OBS] = obs
            try:
                generate(make_fixture(base, fx))
            except RtmError as e:
                return str(e)
            return None

        def accepted(obs):
            fx = dict(green_files)
            fx[P_OBS] = obs
            try:
                return run(fx)[1]
            except RtmError as e:
                return None, None, None, str(e)

        mn, gn, wn, tn = accepted(corpus_lines('corpus: 2026-08-04 shot 7'))
        ok(mn is not None and 'E6' in mn['req']['rows']['REQ-CAP-1']['present'] and not gn
           and '1 corpus swing in 2026: 4 Aug (1).' in tn and 'captured on' not in tn,
           'a corpus line with no build is accepted, meets E6, and the matrix names no build%s' % (
               '' if mn is not None else ' (refused: %s)' % tn))
        mt, gt, wt, tt = accepted(corpus_lines('corpus: 2026-07-10 shot 1 at 17:58 UTC, captured on 1a2b3c4d',
                                               'corpus: 2026-07-10 shot 1 at 18:31 UTC'))
        ok(mt is not None and 'E6' in mt['req']['rows']['REQ-CAP-1']['present'] and not gt,
           'two swings sharing a date and a shot number are accepted when each names its time of day%s' % (
               '' if mt is not None else ' (refused: %s)' % tt))
        mf, gf, wf, tf = accepted(corpus_lines('corpus: 2026-08-04 shot 7',
                                               'corpus: 2026-07-10 shot 1 at 17:58 UTC, captured on 1a2b3c4d'))
        ok(tf is not None and '  - 2 corpus swings in 2026: 10 Jul (1), 4 Aug (1).\n' in tf
           and '<details' not in tf and 'shot 7' not in tf and '17:58' not in tf and '1a2b3c4d' not in tf,
           'a row\'s corpus swings are one line, the count then the count by date: no per-swing list, no build, '
           'no <details>')
        # The count by date: in date order, each date once; a failed swing counted on its date and in the total; a
        # second year puts the year on every date.
        my, gy, wy, ty = accepted(corpus_lines('corpus: 2026-10-20 shot 2 at 09:46 UTC', 'corpus: 2026-07-03 shot 2',
                                               'corpus: 2026-10-20 shot 2 at 10:10 UTC', 'corpus: 2026-07-03 shot 3'))
        ok(ty is not None and '  - 4 corpus swings in 2026: 3 Jul (2), 20 Oct (2).\n' in ty,
           'swings sharing a date are counted together, the dates in order: %r' % (
               [x for x in (ty or '').split('\n') if 'corpus swing' in x and x.startswith('  - ')],))
        fy = corpus_lines('corpus: 2025-12-30 shot 1', 'corpus: 2026-01-02 shot 1', 'corpus: 2026-01-02 shot 2')
        fy = fy.replace('pass: detected,"corpus: 2026-01-02 shot 2"', 'fail: missed,"corpus: 2026-01-02 shot 2"')
        mz, gz, wz, tz = accepted(fy)
        ok(tz is not None and '  - 3 corpus swings, 1 failed: 30 Dec 2025 (1), 2 Jan 2026 (2, 1 failed).\n'
           in tz, 'a failed swing is counted on its date and in the total; two years put the year on each date: %r' % (
               [x for x in (tz or '').split('\n') if 'corpus swing' in x and x.startswith('  - ')],))
        fa = corpus_lines('corpus: 2026-07-03 shot 2', 'corpus: 2026-07-03 shot 3').replace('pass: detected',
                                                                                             'fail: missed')
        ok('  - 2 corpus swings in 2026, all failed: 3 Jul (2).\n' in (accepted(fa)[3] or '')
           and '  - 1 corpus swing in 2026, failed: 3 Jul (1).\n' in (accepted(
               corpus_lines('corpus: 2026-07-03 shot 2').replace('pass: detected', 'fail: missed'))[3] or ''),
           'every swing failed reads "all failed" once, not on each date; a lone failed swing reads "failed"')
        for clash in (('corpus: 2026-07-10 shot 1, captured on 1a2b3c4d', 'corpus: 2026-07-10 shot 1, captured on '
                       '1a2b3c4d'),
                      ('corpus: 2026-07-10 shot 1 at 17:58 UTC, captured on 1a2b3c4d', 'corpus: 2026-07-10 shot 1, '
                       'captured on 1a2b3c4d'),
                      ('corpus: 2026-07-10 shot 1 at 17:58 UTC', 'corpus: 2026-07-10 shot 1 at 17:58 UTC')):
            e = refusal(corpus_lines(*clash))
            ok(e is not None and 'rtm_observations.csv:4' in e and 'share' in e,
               'two swings sharing a date and a shot number, not told apart by a time of day, are refused: %r' % (
                   clash,))
        for bad in ('corpus: 2026-07-10 shot 1 at 25:00 UTC', 'corpus: 2026-07-10 shot 1 at 17:60 UTC',
                    'corpus: 2026-07-10 shot 1 at 9:05 UTC', 'corpus: 2026-07-10 shot 1 at 17:58',
                    'corpus: 2026-07-10 shot 1 at 17:58 UTC captured on 1a2b3c4d',
                    'corpus: 2026-07-10 shot 1, captured on 1a2b3c4d at 17:58 UTC'):
            e = refusal(corpus_lines(bad))
            ok(e is not None and 'rtm_observations.csv:3' in e, 'a corpus line with a bad time of day is refused: %r'
               % bad)

        # --rename: a mechanical rename of an unlabelled test swaps its one baseline line; each refusal leaves it.
        ren = dict(green_files)
        ren['src/A/tests/CMakeLists.txt'] = FIX_SUITE + 'add_test(NAME new_test COMMAND x)\n'
        ren[P_PLATFORMS + '/macos.txt'] = green_files[P_PLATFORMS + '/macos.txt'] + 'new_test\n'
        ren[P_BASELINE] = '# baseline (fixture)\n4 old_test\n'
        rr = make_fixture(base, ren)
        mode_rename(rr, 'old_test', 'new_test', io.StringIO())
        bl = [l for l in read_text(rr, P_BASELINE).split('\n') if l and not l.startswith('#')]
        ok(bl == ['4 new_test'] and read_text(rr, P_BASELINE).startswith('# baseline (fixture)')
           and mode_check(rr, io.StringIO()) == 0,
           '--rename swaps "4 old_test" for "4 new_test", one line for one, keeps the header, and the check passes')

        def rename_refused(files, old, new, needle, what):
            r = make_fixture(base, files)
            before = read_text(r, P_BASELINE)
            try:
                mode_rename(r, old, new, io.StringIO())
                ok(False, '--rename refused: %s (it renamed)' % what)
            except RtmError as e:
                ok(needle in str(e) and read_text(r, P_BASELINE) == before,
                   '--rename refused, baseline untouched: %s: %s' % (what, e))

        rename_refused(ren, 'absent_test', 'new_test', 'is not in the baseline', 'the old name is not in the baseline')
        f = dict(ren)
        f['src/A/tests/CMakeLists.txt'] = ren['src/A/tests/CMakeLists.txt'] + 'add_test(NAME old_test COMMAND x)\n'
        rename_refused(f, 'old_test', 'new_test', 'is still registered', 'the old name is still registered')
        rename_refused(ren, 'old_test', 'ghost_test', 'registered nowhere', 'the new name is not registered')
        rename_refused(ren, 'old_test', 'ana_test', 'carries labels', 'the new name is labelled')
        f = dict(ren)
        f[P_BASELINE] = '# baseline (fixture)\n4 new_test\n4 old_test\n'
        rename_refused(f, 'old_test', 'new_test', 'already in the baseline', 'the new name is already in the baseline')

        # The scan's refusals.
        def refused(files, needle, what):
            try:
                generate(make_fixture(base, files))
                ok(False, '%s (no error)' % what)
            except RtmError as e:
                ok(needle in str(e), '%s: %s' % (what, str(e).split('\n')[0]))

        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE + 'pp_add_test(odd_${NOT_HERE} SOURCES x.cpp)\n'
        refused(f, 'src/A/tests/CMakeLists.txt:%d' % (FIX_SUITE.count('\n') + 1),
                'a name built from a variable it cannot resolve fails, naming the file and line')
        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE.replace('foreach(_row IOP-1 IOP-2)', 'foreach(_row IOP-1 IOP-2 IOP-3)')
        refused(f, 'IOP-3', 'a loop item missing from the loop table fails')
        f = dict(green_files)
        f[P_LOOPS] = FIX_LOOPS + 'src/A/tests/CMakeLists.txt | gone_${_x} | A | gone_a\n'
        refused(f, 'no such loop', 'a loop table line the source no longer has fails')
        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE + 'pp_requirement(no_such_test REQ-ANA-1)\n'
        refused(f, 'no test of that name', 'pp_requirement naming an unregistered test fails')
        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = 'pp_requirement(early REQ-ANA-1)\nadd_test(NAME early COMMAND x)\n' + FIX_SUITE
        refused(f, 'comes before', 'pp_requirement before its registration fails')
        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE + 'set_tests_properties(ana_test PROPERTIES LABELS other)\n'
        refused(f, 'replaces the labels', 'LABELS set after pp_requirement fails')
        f = dict(green_files)
        f['src/A/tests/CMakeLists.txt'] = FIX_SUITE.replace('REQ-ANA-1)', 'REQ-ana-1)')
        refused(f, 'is not REQ-', 'a malformed id fails')
        f = dict(green_files)
        f[P_QML + '/tst_a.qml'] = FIX_QML.replace('// REQ: REQ-CAP-1', '// REQ: REQ-CAP-1 and more')
        refused(f, 'tst_a.qml:2', 'a // REQ: line with other words fails, naming the file and line')
        f = dict(green_files)
        f[P_OBS] = FIX_OBS.replace(',E6,', ',E9,')
        refused(f, 'rtm_observations.csv:3', 'an observation of an unknown kind fails, naming the line')

        # The issue snapshot keeps the parsed ids and the title, never a body.
        snap = snapshot_from_gh([
            {'number': 9, 'state': 'OPEN', 'title': 'Cannot record', 'labels': [{'name': 'enhancement'}],
             'milestone': {'title': 'v1'}, 'body': 'Private words.\n\nRequirement: REQ-CAP-1, REQ-CAP-2\n'
                                                   '**Control:** UI-1\n'},
            {'number': 10, 'state': 'OPEN', 'title': 't', 'labels': [], 'milestone': None,
             'body': 'Requirement: none (a tidy-up, says Jane)'},
            {'number': 11, 'state': 'CLOSED', 'title': 't', 'labels': [], 'milestone': None, 'body': 'old'},
            {'number': 12, 'state': 'CLOSED', 'title': 't', 'labels': [], 'milestone': None, 'body': 'x'}], {12},
            ['Zed-Nobody'])
        ok('Private' not in snap and 'tidy' not in snap and json.loads(snap)['issues'][0].get('title') == 'Cannot record',
           'the snapshot keeps each title and no body text')
        d = {i['number']: i for i in json.loads(snap)['issues']}
        ok(d[9]['requirement'] == ['REQ-CAP-1', 'REQ-CAP-2'] and d[9]['control'] == ['CTL-UI-1']
           and d[10]['requirement'] == ['none'] and d[10]['control'] is None,
           'Requirement: and Control: lines parse to ids, or ["none"]')
        ok(set(d) == {9, 10, 12}, 'the snapshot keeps open issues and cited ones, and drops the rest')

        # The privacy check (POL-7): a title naming a golfer (an athlete folder of the library) stops the refresh.
        lib = tempfile.mkdtemp(dir=base)
        for p in ('Jane-Doeling/athlete.json', 'corpus/pose2/2026-01-02_Avery-Example_7i_01/swing.json',
                  '.pinpoint-trash/Hidden-Person/athlete.json', 'dev/athlete.json', 'scratch/notes.txt'):
            write_text(lib, p, '{}\n')
        try:
            names = golfer_names(lib)
        except Exception as e:  # noqa: BLE001 (red before it is built)
            names = 'error: %r' % e
        ok(names == ['Avery-Example', 'Jane-Doeling', 'dev'], 'the golfers\' names are the athlete folders (an '
           'athlete.json) and the session folders\' names, a level or two down, never a hidden folder: %s' % (names,))
        gh_items = [{'number': n, 'state': st, 'title': t, 'labels': [], 'milestone': None, 'body': ''} for n, st, t in (
            (21, 'OPEN', 'Jane cannot record'), (22, 'OPEN', "Doeling's view is empty"),
            (23, 'OPEN', 'A swing of Avery-Example'), (24, 'OPEN', 'janet and the dev tools'),
            (25, 'OPEN', 'Hidden here'), (26, 'CLOSED', 'Jane again'))]
        try:
            snapshot_from_gh(gh_items, set(), names)
            ok(False, 'a title naming a golfer stops the refresh (it wrote a snapshot)')
        except RtmError as e:
            msg = str(e)
            ok(all("issue #%d's title names a golfer" % n in msg for n in (21, 22, 23))
               and not any('#%d' % n in msg for n in (24, 25, 26))
               and not any(x in msg for x in ('Jane', 'Doeling', 'Avery', 'Example')),
               'a title naming a golfer (a first name, a surname, the folder\'s name) stops the refresh, naming the '
               'issue only; a lower-case word, a hidden folder and an issue the snapshot drops do not: %s' % msg.replace('\n', ' / '))
        except Exception as e:  # noqa: BLE001
            ok(False, 'a title naming a golfer stops the refresh: %r' % e)
        try:
            kept = json.loads(snapshot_from_gh(gh_items[3:], set(), names))['issues']
            ok([i['title'] for i in kept] == ['janet and the dev tools', 'Hidden here'],
               'titles naming no golfer are kept: %s' % [i['title'] for i in kept])
        except Exception as e:  # noqa: BLE001
            ok(False, 'titles naming no golfer are kept: %r' % e)
        lib2 = tempfile.mkdtemp(dir=base)
        write_text(lib2, 'dev/athlete.json', '{}\n')
        try:
            snapshot_from_gh(gh_items[3:], set(), golfer_names(lib2))
            ok(False, 'the refresh fails closed on a library whose names hold no capital (no name it can match)')
        except RtmError:
            ok(True, 'the refresh fails closed on a library whose names hold no capital (no name it can match)')
        except Exception as e:  # noqa: BLE001
            ok(False, 'the refresh fails closed on a library whose names hold no capital: %r' % e)
        for what, d in (('a missing library folder', os.path.join(lib, 'absent')),
                        ('a library with no athlete folder', os.path.join(lib, 'scratch'))):
            try:
                golfer_names(d)
                ok(False, 'the refresh fails closed on %s' % what)
            except RtmError:
                ok(True, 'the refresh fails closed on %s' % what)
            except Exception as e:  # noqa: BLE001
                ok(False, 'the refresh fails closed on %s: %r' % (what, e))
        # A folder it cannot read, at any level, stops the refresh (fails closed), naming no folder. os.listdir is
        # stood in for, so the check runs the same on every platform (a chmod does not stop a read on Windows).
        real_listdir = os.listdir
        for what, bad_dir in (('a sub-folder', os.path.join(lib, 'corpus')), ('the library itself', lib)):
            def flaky(d, bad_dir=bad_dir):
                if os.path.normpath(d) == os.path.normpath(bad_dir):
                    raise PermissionError(13, 'Permission denied')
                return real_listdir(d)
            os.listdir = flaky
            try:
                got = golfer_names(lib)
                ok(False, 'the refresh fails closed on %s it cannot read (it read %d names)' % (what, len(got)))
            except RtmError as e:
                ok('corpus' not in str(e) and lib not in str(e) and 'could not be read' in str(e),
                   'the refresh fails closed on %s it cannot read, naming no folder: %s' % (what, e))
            except Exception as e:  # noqa: BLE001
                ok(False, 'the refresh fails closed on %s it cannot read: %r' % (what, e))
            finally:
                os.listdir = real_listdir
        ok(platform_list_text(['b', 'a', 'a'], 'macos', 'commit x').split('\n')[4:6] == ['a', 'b'],
           'a gate list is sorted and unique')

        # The real tree's loops agree with the loop table (read only; nothing is written).
        real = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
        if os.path.isfile(os.path.join(real, *P_REQ.split('/'))):
            s = scan_tests(real, parse_loop_table(read_text(real, P_LOOPS)))
            ok(not s.errors, 'this checkout\'s registrations all resolve, and its loop table agrees with the source%s'
               % ('' if not s.errors else ': ' + '; '.join(s.errors)))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    bad = [w for c, w in results if not c]
    print('rtm_selftest: %d checks, %d failed' % (len(results), len(bad)))
    return 1 if bad else 0


def main(argv):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except AttributeError:
        pass
    args = list(argv)
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    if '--root' in args:
        k = args.index('--root')
        root = os.path.abspath(args[k + 1])
        del args[k:k + 2]
    library = None
    if '--library' in args:
        k = args.index('--library')
        library = os.path.abspath(args[k + 1])
        del args[k:k + 2]
    ctest = None
    if '--ctest' in args:
        k = args.index('--ctest')
        ctest = args[k + 1]
        del args[k:k + 2]
    out = sys.stdout
    try:
        if not args:
            return mode_write(root, out)
        if args == ['--check']:
            return mode_check(root, out)
        if args in (['--init'], ['--init', '--force-init'], ['--force-init']):
            return mode_init(root, out, '--force-init' in args)
        if len(args) == 3 and args[0] == '--rename':
            return mode_rename(root, args[1], args[2], out)
        if args == ['--refresh-issues']:
            if library is None:
                raise RtmError('--refresh-issues needs --library <the library folder>: it refuses a title naming a '
                               'golfer, so it reads the athlete folders\' names (never written or printed)')
            return mode_refresh(root, out, library)
        if len(args) == 3 and args[0] == '--platform-list':
            return mode_platform_list(root, os.path.abspath(args[1]), args[2], ctest, out)
        if args in (['--self-test'], ['--selftest']):
            return self_test()
    except RtmError as e:
        out.write('make_rtm.py: %s\n' % e)
        return 2
    sys.stderr.write(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
