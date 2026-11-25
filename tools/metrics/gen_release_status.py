#!/usr/bin/env python3
"""gen_release_status.py — write docs/validation/release_status.md, the release-progress dashboard for
metrics, measures and norms.

It reads, and never edits:
  src/Vocabulary/Metrics/metric_catalogue_manifest.cpp   every metric and its routes (a route marked PLANNED has no producer)
  src/Resources/diagnostics/core.json         measures (status), signals, conditions (draft / active / held from the report)
  src/Resources/diagnostics/norms.json        norm rows, their source (heuristic / literature / measured) and their
                                              STANDING: the typed keys `standing` (provisional | decided),
                                              `decidedBy`, `decidedOn` (DEC-127; IM-S3-6: DEC-186; refactor
                                              step 3.2, audit B-05) — never the prose in `citation`
  docs/validation/metric_evidence.json        the hand-kept evidence: verified, validated, synthetic-only

Run it from the repo root whenever any of those change:  python3 tools/metrics/gen_release_status.py
Add --check to exit 1 when the committed dashboard is stale, or when a norm row's standing keys are malformed,
or when the two rows audit B-05 found misread (m_takeawayPlane, m_clubSlotTop: one standing in two wordings)
read differently.
"""
import json, re, sys, collections, datetime, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
MANIFEST = os.path.join(ROOT, 'src/Vocabulary/Metrics/metric_catalogue_manifest.cpp')
CORE = os.path.join(ROOT, 'src/Resources/diagnostics/core.json')
NORMS = os.path.join(ROOT, 'src/Resources/diagnostics/norms.json')
EVID = os.path.join(ROOT, 'docs/validation/metric_evidence.json')
OUT = os.path.join(ROOT, 'docs/validation/release_status.md')


def parse_manifest():
    src = open(MANIFEST, encoding='utf-8').read()
    out = {}
    for block in re.split(r'cat\.addDescriptor\(\{', src)[1:]:
        k = re.search(r'\.key\s*=\s*QStringLiteral\("([^"]+)"\)', block)
        if not k:
            continue
        key = k.group(1)
        grp = re.search(r'\.group\s*=\s*QStringLiteral\("([^"]+)"\)', block)
        lab = re.search(r'\.label\s*=\s*QStringLiteral\("([^"]+)"\)', block)
        routes = []
        for chunk in block.split('via(')[1:]:
            name = re.match(r'\s*"([^"]+)"', chunk)
            if not name:
                continue
            # the route's own text runs to the next via( — PLANNED as its last argument marks it unbuilt
            body = chunk.split('.usedBy')[0]
            routes.append((name.group(1), 'PLANNED' in body))
        out[key] = {'group': grp.group(1) if grp else '', 'label': lab.group(1) if lab else key, 'routes': routes}
    return out


def load():
    core = json.load(open(CORE, encoding='utf-8'))
    nj = json.load(open(NORMS, encoding='utf-8'))
    norms = nj['norms'] if isinstance(nj, dict) and 'norms' in nj else nj
    ev = json.load(open(EVID, encoding='utf-8'))
    return core, norms, ev


def evidence_for(key, ev):
    e = dict(ev.get('metrics', {}).get(key, {}))
    for pre, pe in ev.get('prefixes', {}).items():
        if key.startswith(pre):
            for k, v in pe.items():
                e.setdefault(k, v)
    return e


STANDINGS = ('provisional', 'decided')


def standing_suffix(rows):
    """A norm's standing from its TYPED keys (DEC-127, IM-S3-6), never from the citation's prose. A recorded
    decision (`decidedBy`) is what the release gate asks of a heuristic norm, so a figure the owner accepted
    provisionally reads 'decided, provisional'; one still waiting for that decision reads 'provisional'."""
    if any(r.get('standing') == 'decided' for r in rows):
        return ' (decided)'
    if any(r.get('decidedBy') for r in rows):
        return ' (decided, provisional)'
    if any(r.get('standing') == 'provisional' for r in rows):
        return ' (provisional)'
    return ''


def standing_problems(norms):
    """The typed standing keys, checked: a known token, an ISO date, and who decided whenever when is said."""
    out = []
    for r in norms:
        key = f"{r.get('measure')} @ {r.get('context')}"
        st = r.get('standing')
        if st is not None and st not in STANDINGS:
            out.append(f'{key}: standing {st!r} is not one of {", ".join(STANDINGS)}')
        on = r.get('decidedOn')
        if on is not None:
            try:
                datetime.date.fromisoformat(on)
            except (TypeError, ValueError):
                out.append(f'{key}: decidedOn {on!r} is not an ISO date')
            if not r.get('decidedBy'):
                out.append(f'{key}: decidedOn without decidedBy')
        if (r.get('decidedBy') or on) and st is None:
            out.append(f'{key}: a decision recorded with no standing')
    return out


def main(check=False):
    cat = parse_manifest()
    core, norms, ev = load()
    measures = {m['id']: m for m in core['measures']}
    sig_measures = {s['id']: s.get('measures', []) for s in core['signals']}
    # measure -> conditions that read it
    graded_by = collections.defaultdict(list)
    for c in core['conditions']:
        for sid in c.get('detectedBy', []):
            for mid in sig_measures.get(sid, []):
                graded_by[mid].append(c)
    norms_by = collections.defaultdict(list)
    for r in norms:
        norms_by[r.get('measure')].append(r)

    def norm_basis(mid):
        rows = norms_by.get(mid, [])
        if not rows:
            return 'none (rule-based)'
        srcs = sorted({r.get('source', '?') for r in rows})
        return '/'.join(srcs) + standing_suffix(rows)

    # metric -> measures
    by_metric = collections.defaultdict(list)
    for m in core['measures']:
        mk = m.get('metricKey')
        by_metric[mk or '(composite)'].append(m)

    rows = []
    for key, d in cat.items():
        built = [r for r, planned in d['routes'] if not planned]
        planned = [r for r, planned in d['routes'] if planned]
        ms = by_metric.get(key, [])
        live_ms = [m for m in ms if m.get('status') == 'live']
        held_ms = [m for m in ms if m.get('status') in ('held', 'planned', 'noProducer', 'notCapturable', 'externalDevice')]
        conds = {c['id']: c for m in live_ms for c in graded_by.get(m['id'], [])}
        active = [c for c in conds.values() if c.get('state') == 'active' and not c.get('reportHold')]
        draft = [c for c in conds.values() if c.get('state') != 'active' or c.get('reportHold')]
        e = evidence_for(key, ev)
        # A measure that PREFERS another key (e.g. the club path read from lm.clubPath first) is built,
        # and as far along, as the preferred key wherever that key is built.
        via = sorted({pk for m in ms for pk in (m.get('preferKeys') or []) if pk != key and pk in cat
                      and any(not pl for _, pl in cat[pk]['routes'])})
        if via:
            built = built + [f'via {pk}' for pk in via]
            for pk in via:
                for kind, v in evidence_for(pk, ev).items():
                    e.setdefault(kind, v)
        if not built:
            stage = 'Planned'
        elif 'validated' in e:
            stage = 'Validated'
        elif 'verified' in e:
            stage = 'Verified'
        else:
            stage = 'Built'
        live = 'yes' if active else ('draft' if draft else ('shown' if live_ms else 'no'))
        nb = sorted({norm_basis(m['id']) for m in live_ms}) if live_ms else []
        rows.append({
            'key': key, 'group': d['group'] or '(other)', 'label': d['label'], 'stage': stage,
            'built': built, 'planned': planned, 'live': live,
            'nMeasures': len(ms), 'nLive': len(live_ms), 'nHeld': len(held_ms),
            'nActive': len(active), 'nDraft': len(draft), 'norms': nb, 'evidence': e,
        })

    # measure-level norm summary
    live_measures = [m for m in core['measures'] if m.get('status') == 'live']
    graded_measures = [m for m in live_measures if graded_by.get(m['id'])]
    basis_count = collections.Counter()
    for m in graded_measures:
        b = norm_basis(m['id'])
        basis_count['decided' if 'decided' in b else ('literature' if 'literature' in b else ('measured' if 'measured' in b else ('heuristic' if 'heuristic' in b else 'none')))] += 1

    stage_count = collections.Counter(r['stage'] for r in rows)
    live_count = collections.Counter(r['live'] for r in rows)
    cond_state = collections.Counter('held from report' if c.get('reportHold') else c.get('state') for c in core['conditions'])

    L = []
    L.append('# Metrics, measures and norms — release status\n')
    L.append('GENERATED by `tools/metrics/gen_release_status.py` — do not edit by hand. Re-run it after any change to the metric catalogue, the fault model (`core.json`), the norms (`norms.json`) or the evidence file (`docs/validation/metric_evidence.json`); `--check` fails when this page is stale.\n')
    L.append('## How to read it\n')
    L.append('Each metric sits at the highest stage it has reached:\n')
    L.append('- **Planned**: no producer yet for any of its sources.')
    L.append('- **Built**: produced by at least one source; the code is unit-tested, but nobody has checked it on real swings.')
    L.append('- **Verified**: also checked on real swings (a one-swing re-run, a frame sheet, or a library consistency check).')
    L.append('- **Validated**: compared against an independent reference (a device, hand-marked frames, an instrumented club, worn sensors) with the agreement measured.\n')
    L.append('Separately, **Live** says whether the golfer sees a verdict from it:\n')
    L.append('- **yes**: at least one active fault grades it.')
    L.append('- **draft**: only draft faults grade it, or faults held out of the report.')
    L.append('- **shown**: it is produced and shown, but grades nothing.')
    L.append('- **no**: not produced.\n')
    L.append('The **norm basis** says where a graded measure\'s norm comes from:\n')
    L.append('- **heuristic**: authored, not yet seated on evidence.')
    L.append('- **literature**: from published sources.')
    L.append('- **measured**: seated on our own measurements.')
    L.append('- **decided**: Mark has made a recorded decision on it — *decided, provisional* when he accepted the figure for now (to be seated later), from the norm\'s typed `standing` / `decidedBy` / `decidedOn` keys in `norms.json`. A norm marked *provisional* alone is still waiting for that decision.\n')
    L.append('Release needs the golfer-facing readings at **Validated** and **Live**, with norms that are **literature, measured or decided**. A heuristic norm going live without a conscious decision is a release blocker.\n')

    L.append('## Progress at a glance\n')
    tot = len(rows)
    L.append('| Stage | Metrics | Share |')
    L.append('|---|---|---|')
    for s in ('Planned', 'Built', 'Verified', 'Validated'):
        L.append(f'| {s} | {stage_count.get(s, 0)} | {100 * stage_count.get(s, 0) / tot:.0f} % |')
    L.append(f'| **Total** | **{tot}** | |\n')
    dev = sum(1 for r in rows if r['stage'] == 'Validated' and any(t in ((r['evidence'].get('validated') or {}).get('reference') or '') for t in ('launch monitor itself', 'HackMotion')))
    L.append(f'Of the {stage_count.get("Validated", 0)} validated metrics, {dev} are device readings taken as their own reference (the launch monitor\'s and HackMotion\'s); the rest were measured against a reference.\n')
    L.append('| Live | Metrics |')
    L.append('|---|---|')
    for s in ('yes', 'draft', 'shown', 'no'):
        L.append(f'| {s} | {live_count.get(s, 0)} |')
    L.append('')
    L.append(f'**Measures:** {len(core["measures"])} in the fault model, of which {len(live_measures)} are live and {len(graded_measures)} are graded by at least one fault.\n')
    L.append(f'**Norm basis of the {len(graded_measures)} graded measures:** ' + ', '.join(f'{k} {v}' for k, v in sorted(basis_count.items(), key=lambda x: -x[1])) + '.\n')
    L.append(f'**Faults:** {len(core["conditions"])} in all: ' + ', '.join(f'{k} {v}' for k, v in sorted(cond_state.items(), key=lambda x: -x[1])) + '.\n')

    # release blockers: live-yes metrics not validated
    blockers = [r for r in rows if r['live'] == 'yes' and r['stage'] != 'Validated']
    L.append(f'## Release gaps: graded by an active fault but not yet validated ({len(blockers)})\n')
    L.append('| Metric | Group | Stage | Sources built | Norm basis |')
    L.append('|---|---|---|---|---|')
    for r in sorted(blockers, key=lambda r: (r['group'], r['key'])):
        L.append(f"| `{r['key']}` | {r['group']} | {r['stage']} | {', '.join(r['built']) or '—'} | {'; '.join(r['norms']) or '—'} |")
    L.append('')

    L.append('## Every metric, by group\n')
    by_group = collections.defaultdict(list)
    for r in rows:
        by_group[r['group']].append(r)
    for g in sorted(by_group):
        L.append(f'### {g}\n')
        L.append('| Metric | Stage | Live | Sources built | Sources planned | Measures (live / held) | Faults (active / draft) | Norm basis | Evidence |')
        L.append('|---|---|---|---|---|---|---|---|---|')
        for r in sorted(by_group[g], key=lambda r: r['key']):
            e = r['evidence']
            bits = []
            for kind in ('validated', 'verified', 'syntheticOnly'):
                if kind in e:
                    x = e[kind]
                    date = x.get('date') or 'undated'
                    text = x.get('note') or x.get('evidence') or ''
                    ref = x.get('reference')
                    label = {'validated': 'Validated', 'verified': 'Verified', 'syntheticOnly': 'Simulated only'}[kind]
                    bits.append(f"{label} ({date}{', vs ' + ref if ref else ''}): {text}")
            L.append(f"| `{r['key']}` | {r['stage']} | {r['live']} | {', '.join(r['built']) or '—'} | {', '.join(r['planned']) or '—'} | {r['nLive']} / {r['nHeld']} | {r['nActive']} / {r['nDraft']} | {'; '.join(r['norms']) or '—'} | {' '.join(bits).replace('|', '/') or '—'} |")
        L.append('')
    text = '\n'.join(L) + '\n'
    if check:
        bad = standing_problems(norms)
        # Audit B-05: the pair Mark approved together on 13 Nov 2026 read 'decided' and 'heuristic' when the
        # standing was parsed out of prose. One standing, so one reading.
        pair = {norm_basis(m) for m in ('m_takeawayPlane', 'm_clubSlotTop')}
        if len(pair) != 1:
            bad.append('m_takeawayPlane and m_clubSlotTop read different standings: ' + ' / '.join(sorted(pair)))
        for b in bad:
            print('norm standing: ' + b)
        cur = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if cur != text:
            print('release_status.md is stale: run python3 tools/metrics/gen_release_status.py')
            return 1
        return 1 if bad else 0
    open(OUT, 'w', encoding='utf-8').write(text)
    print(f'wrote {OUT}: {tot} metrics, {len(core["measures"])} measures')
    return 0


if __name__ == '__main__':
    sys.exit(main('--check' in sys.argv))
