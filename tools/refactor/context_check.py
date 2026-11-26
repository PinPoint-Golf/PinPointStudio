#!/usr/bin/env python3
"""Context-map check of every cross-context #include in src/ (tests excluded).

The kept copy (platform refactor B1.2) of build/context-map/check_includes.py, the side track for IM R-CTX-1
(build/context-map/BRIEF.md), which git ignores and which reads only the tree it sits in. Read-only: it greps src/ and
prints. Run from the repo root:
    python3 tools/refactor/context_check.py [--src <dir>] [--list a|b|c|all] [--matrix] [--csv FILE]
--src reads another tree's src/ (a lane's worktree, an archive of a commit). Every table below keys on a literal path
under src/, so tools/refactor/move_files.py re-keys them with each move map, as it does the lint's tables: never build a
key at run time. A directory under src/ whose files get no tag is an error (exit 1), not a default.

Two taggings of every file:
  ruled  = IM section 4.1's Context column as ruled on 24 Nov 2026 (capture code in Gui stays Domain + Application
           until Stage D; the drill pack stays Diagnostics), plus this script's placements for "to place in Stage B".
  true   = the same, with the two deferrals undone: the capture code tagged Acquisition, the drill pack Coaching.
Two rules, each applied on BOTH taggings to every include that crosses a context (the 2x2 is printed):
  today  = R-CTX-1 v1.10.1 as lint A9 words it (order Acq -> Diag -> Coaching; hub through contracts; Shared; Platform).
  map    = the context map: only through a named contract or the shared kernel.
Classes (the brief's), from today-on-ruled-tags and map-on-true-tags; the 2x2 of rule x tagging is printed too:
  a = fits a named relationship of the map
  b = passes today only by a carve-out and the map rejects it: "deferral" when today's rule on the true tags
      rejects it as well (the deferral tag alone hides it), "rule" when the two rules themselves differ
  c = fits neither rule
  d = the map allows it and today's rule rejects it
Options: --literal-l2 (IM's three L2 files only), --matrix, --list a|b|c|d|all, --grouped, --csv FILE, --ambiguous
"""
import os, re, sys, collections, csv

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src')
if '--src' in sys.argv:                       # another tree, e.g. a lane's worktree or an archived commit
    _i = sys.argv.index('--src'); SRC = os.path.abspath(sys.argv[_i + 1]); del sys.argv[_i:_i + 2]
SRC = os.path.normpath(SRC)
EXT = ('.h', '.hpp', '.cpp', '.mm', '.cc')
P, S, ACQ, DIA, COA, HUB = 'Platform', 'Shared', 'Acquisition', 'Diagnostics', 'Coaching', 'Domain+Application'
ORDER = {ACQ: 0, DIA: 1, COA: 2}

# ---- tags: IM 4.1's Context column. Longest directory prefix wins; a file stem overrides its directory. --------
DIRS = {
    'Vocabulary/Analysis/': DIA, 'Vocabulary/Rig/': ACQ, 'Vocabulary/Metrics/': DIA,   # Rig: the device keys (rig_design.md §5)
    'Vocabulary/Recording/': ACQ, 'Vocabulary/Identity/': S, 'Core/': P,  # Core is Platform by directory (R4)
    'Vocabulary/Club/': S, 'Vocabulary/Common/': S, 'Vocabulary/LaunchMonitor/': ACQ,
    'Pose/': DIA, 'Club/': DIA, 'Ball/': DIA, 'Kinematics/': DIA, 'Timeline/': DIA, 'Diagnostics/': DIA,
    'Diagnostics/derived/': DIA,                       # the derived facts (from the Coaching folder, Stage B, Part 1)
    'IMU/': DIA,                            # golf fusion; signal-level files and device back-ends below
    'Analysis/': DIA,
    'DomainModel/': HUB, 'App/': HUB, 'Gui/': HUB, 'Export/': HUB,
    'Buffer/': ACQ, 'Video/': ACQ, 'Ppcp/': ACQ,
    'LaunchMonitor/': ACQ,                                     # the connectors (R5, ruled)
    'LaunchMonitor/inferred/': DIA,                            # the inferred reads: rules that derive measurements from a reading (R5)
    # ---- "to place in Stage B": THIS SCRIPT'S placements (each stated in REPORT.md) ----
    'Body/': DIA, 'Wrist/': DIA, 'Skeleton3d/': DIA,          # measurement algorithms over the analysis
    'Audio/': ACQ,                                             # microphone input and the acoustic shot detector; output below
    'LLM/': HUB, 'STT/': HUB, 'TTS/': HUB, 'Secrets/': HUB, 'Update/': HUB,
}
FILES = {
    'Vocabulary/Recording/frame_decode': ACQ,                                                   # ruled: part of the Recording contract
    'IMU/imu_calibration': ACQ, 'IMU/trunk_angles': ACQ, 'IMU/trunk_stand_tall': ACQ,   # ruled (L2)
    # to place (this script's placements):
    'Core/device_enumerator': ACQ,
    'Analysis/mp4_frame_reader': HUB,       # "the store's" (lint comment): persistence
    'Pose/motion_capture_probe': HUB,       # the hardware probe behind a setting
    'Audio/audio_output': HUB, 'Audio/audio_output_base': HUB, 'Audio/ting_player': HUB,   # feedback sound, presentation
    # to place: device back-ends
    'IMU/ble_adapter_pool': ACQ, 'IMU/ble_imu_transport': ACQ, 'IMU/imu_base': ACQ, 'IMU/imu_capabilities': ACQ,
    'IMU/imu_factory': ACQ, 'IMU/imu_io_worker': ACQ, 'IMU/imu_sample_tap': ACQ, 'IMU/hm_session_worker': ACQ,
    'IMU/wt9011dcl': ACQ, 'IMU/wt9011dcl_ble': ACQ, 'IMU/wt9011dcl_base': ACQ, 'IMU/wt901_config_check': ACQ,
}

# ---- a gap in L2's list: this script's placement, applied to both taggings unless --literal-l2 ------------------
# IM 4.1 names three IMU files as Acquisition's signal maths. The device back-ends (Acquisition) also include these,
# which are device or signal-level types (a call made from their header comments and includes); IM's literal tag
# for them is Diagnostics. hm_frame (the HackMotion-to-anatomical frame map) is left in Diagnostics.
L2_GAP = {
    'IMU/hm_unit_id': ACQ, 'IMU/hm_session_types': ACQ, 'IMU/hm_capture_provenance': ACQ, 'IMU/hm_sample_convert': ACQ,
    'IMU/hm_skew_stats': ACQ, 'IMU/impact_detector': ACQ, 'IMU/iorientation_filter': ACQ, 'IMU/orientation_filter': ACQ,
    'IMU/eskf_orientation_filter': ACQ, 'IMU/trunk_ceremony': ACQ, 'IMU/trunk_ceremony_file': ACQ,
}

# ---- the two deferrals of today's rule, undone for the "true" tags ---------------------------------------------
TRUE_DIRS = {'Gui/cameras/': ACQ, 'Gui/imu/': ACQ}
TRUE_FILES = {'Gui/shot/shot_processor': ACQ, 'Gui/shot/capture_context': ACQ, 'Gui/shot/shot_controller': ACQ,
              'Gui/shot/shot_arbiter': ACQ, 'Gui/shot/shot_source_map': ACQ, 'Gui/shot/ppcp_clip_filer': ACQ,
              'Diagnostics/packs/drill_pack': COA}

# ---- the named contracts ----------------------------------------------------------------------------------------
# The Recording contract (Acquisition -> Diagnostics; IM 2.2, OBJ-RECORDING, CLS-RECORDING, R-REC-4, lint A8):
# exactly the Vocabulary-ring files 4.1 tags Acquisition.
REC_DIRS = ('Vocabulary/Recording/',)
REC_FILES = {'Buffer/types', 'Buffer/format_descriptor', 'Buffer/source_descriptor', 'Buffer/swing_window',
             'Buffer/swing_payload_source', 'Buffer/imu_sample', 'Buffer/pixel_format_names',
             'Vocabulary/LaunchMonitor/launch_monitor_reading', 'Vocabulary/LaunchMonitor/lm_device', 'Vocabulary/Recording/frame_decode'}
# Acquisition's pure signal rules (L2). NOT part of 2.2's Recording, so the map as stated rejects an include of them.
ACQ_RULES = {'IMU/imu_calibration', 'IMU/trunk_angles', 'IMU/trunk_stand_tall'}
# The hub's contracts (any context -> Domain + Application; R-CTX-1, Q26): interfaces and value types of DomainModel.
# Only recording_source, shot_command_values and recording_repository are included from outside the hub today;
# the rest of the set is this script's guess from the file names (IM has not named the set yet).
HUB_CONTRACT = {
    'DomainModel/recording_repository', 'DomainModel/recording_reader', 'DomainModel/recording_source',
    'DomainModel/session_repository', 'DomainModel/athlete_repository', 'DomainModel/rule_data_repository',
    'DomainModel/firing_reader', 'DomainModel/series_reader', 'DomainModel/shot_command_values',
    'DomainModel/report_record', 'DomainModel/session_input', 'DomainModel/session_roll_up',
    'DomainModel/golfer_input_rules', 'DomainModel/summary_rules'}
# The results contract (Diagnostics -> Coaching): IM names objects (OBJ-SHOT-DERIVED ...), not headers. This script's
# stand-in: the result value types, i.e. Diagnostics' vocabulary and the derived facts' headers.
RESULT_DIRS = ('Vocabulary/Analysis/', 'Vocabulary/Metrics/', 'Diagnostics/derived/')

LITERAL_L2 = '--literal-l2' in sys.argv

def stem(rel):
    s, d = rel.rfind('/'), rel.rfind('.')
    return rel[:d] if d > s else rel

def tag(rel, true=False):
    st = stem(rel)
    if true and st in TRUE_FILES: return TRUE_FILES[st]
    if not LITERAL_L2 and st in L2_GAP: return L2_GAP[st]
    if st in FILES: return FILES[st]
    best, tagv = -1, None
    tables = [DIRS] + ([TRUE_DIRS] if true else [])
    for t in tables:
        for d, c in t.items():
            if rel.startswith(d) and len(d) > best: best, tagv = len(d), c
    return tagv

def today(src, dst, target):
    """R-CTX-1 v1.10.1 / lint A9 as worded -> (ok, reason)."""
    if src == dst: return True, 'same context'
    if dst == P: return True, 'Platform'
    if dst == S: return True, 'Shared (Q27)'
    if src == P: return False, 'Platform includes a context'
    if src == S: return True, 'unruled: A9 does not say what Shared may include'
    if src == HUB: return True, 'hub composes (Q26)'
    if dst == HUB:
        return (True, 'hub contract (Q26)') if stem(target) in HUB_CONTRACT else (False, 'hub past its contracts')
    if ORDER[src] > ORDER[dst]: return True, 'later uses earlier'
    return False, 'earlier includes later'

def cmap(src, dst, target):
    """The context map -> (ok, relationship): only through a named contract or the shared kernel."""
    st = stem(target)
    if src == dst: return True, 'same context'
    if dst == P: return True, 'Platform'
    if src == P: return False, 'Platform includes a context'
    if dst == S: return True, 'shared kernel'
    if src == S: return False, 'the shared kernel includes a context'
    if dst == HUB:
        return (True, 'hub contract') if st in HUB_CONTRACT else (False, 'hub past its contracts')
    if src == HUB: return True, 'hub composes (unbounded)'
    if src == DIA and dst == ACQ:
        if target.startswith(REC_DIRS) or st in REC_FILES: return True, 'Recording'
        # Acquisition's signal rules (ACQ_RULES and the L2-gap files) are not the Recording: the map as stated has
        # no row for them, so it rejects them. Today's rule allows them ("contracts and rules").
        return False, "Diagnostics past the Recording into Acquisition's rules"
    if src == COA and dst == DIA:
        return (True, 'results') if target.startswith(RESULT_DIRS) else (False, 'Coaching past the results into Diagnostics')
    if src == COA and dst == ACQ: return False, 'Coaching includes Acquisition: no contract'
    return False, '%s includes %s: no relationship' % (src, dst)

def walk():
    files = {}
    for root, dirs, names in os.walk(SRC):
        dirs[:] = [d for d in dirs if d not in ('tests', 'Testing', 'Resources', 'Shaders', 'resources')]
        for n in names:
            if n.endswith(EXT):
                full = os.path.join(root, n)
                files[os.path.relpath(full, SRC).replace(os.sep, '/')] = full
    return files

INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"')

def resolve(files, by_name, frm, inc):
    d = frm.rsplit('/', 1)[0] if '/' in frm else ''
    for c in (os.path.normpath(os.path.join(d, inc)), os.path.normpath(inc), os.path.normpath('Gui/' + inc)):
        c = c.replace(os.sep, '/')
        if c in files: return c
    same = by_name.get(inc.rsplit('/', 1)[-1], [])
    return same[0] if len(same) == 1 else ('?' if same else None)

def main():
    args = sys.argv[1:]
    files = walk()
    by_name = collections.defaultdict(list)
    for rel in files: by_name[rel.rsplit('/', 1)[-1]].append(rel)
    rows, untagged, ambiguous, outside, total = [], set(), [], collections.Counter(), 0
    for rel in sorted(files):
        with open(files[rel], encoding='utf-8', errors='replace') as f:
            for ln, line in enumerate(f, 1):
                m = INC.match(line)
                if not m: continue
                t = resolve(files, by_name, rel, m.group(1))
                if t is None: outside[m.group(1)] += 1; continue     # Qt, third-party, generated, or a tests header
                if t == '?': ambiguous.append((rel, ln, m.group(1))); continue
                total += 1
                rs, rd, ts, td = tag(rel), tag(t), tag(rel, True), tag(t, True)
                for r, x in ((rel, rs), (t, rd)):
                    if x is None: untagged.add(r)
                if None in (rs, rd): continue
                if rs == rd and ts == td: continue         # inside one context under both taggings
                v = {'today_ruled': today(rs, rd, t), 'today_true': today(ts, td, t),
                     'map_ruled': cmap(rs, rd, t), 'map_true': cmap(ts, td, t)}
                tr, mt = v['today_ruled'][0], v['map_true'][0]
                cls = 'a' if mt and tr else 'b' if tr else 'c' if not mt else 'd'
                sub = ''
                if cls == 'b': sub = 'deferral' if not v['today_true'][0] else 'rule'   # rule: today passes on the true tags too
                row = dict(cls=cls, sub=sub, file=rel, line=ln, target=t, src=ts, dst=td, rsrc=rs, rdst=rd)
                for k, (ok, why) in v.items(): row[k] = 'ok' if ok else 'REJECT'; row[k + '_why'] = why
                rows.append(row)
    print('tags: %s' % ("IM 4.1 literally (three L2 files)" if LITERAL_L2 else "IM 4.1 + this script's L2-gap placements"))
    print('files read: %d   quoted includes resolved inside src: %d   crossing a context: %d' % (len(files), total, len(rows)))
    print('quoted includes not found in src (third-party, generated, tests): %d, e.g. %s' % (
        sum(outside.values()), ', '.join(k for k, _ in outside.most_common(5))))
    print('ambiguous bare includes skipped: %d   untagged files: %d' % (len(ambiguous), len(untagged)))
    names = {'a': '(a) fits a named relationship of the map', 'b': '(b) passes today only by a carve-out; the map rejects',
             'c': '(c) fits neither rule', 'd': '(d) the map allows, today rejects'}
    cnt = collections.Counter(r['cls'] for r in rows)
    print()
    for k in 'abcd': print('  %-56s %5d' % (names[k], cnt.get(k, 0)))
    print('     (b) by cause: %s' % dict(collections.Counter(r['sub'] for r in rows if r['cls'] == 'b')))
    print('\n(a) by relationship:')
    for k, v in collections.Counter(r['map_true_why'] for r in rows if r['cls'] == 'a').most_common(): print('  %-66s %5d' % (k, v))
    print("\ntoday's carve-outs at work (ruled tags): %s" % dict(collections.Counter(
        r['today_ruled_why'] for r in rows if 'Q2' in r['today_ruled_why'])))
    print('\nthe 2x2 (rejected includes):          ruled tags   true tags')
    for rule in ('today', 'map'):
        print('  %-34s %8d %11d' % ("today's R-CTX-1" if rule == 'today' else 'the context map',
              sum(r[rule + '_ruled'] == 'REJECT' for r in rows), sum(r[rule + '_true'] == 'REJECT' for r in rows)))
    print('  rules disagree on the same tags:     ruled %d, true %d' % (
        sum(r['today_ruled'] != r['map_ruled'] for r in rows), sum(r['today_true'] != r['map_true'] for r in rows)))
    if '--matrix' in args:
        print('\ncrossings by true tags (src -> dst):')
        for k, v in sorted(collections.Counter((r['src'], r['dst'], r['cls']) for r in rows).items()): print('  %-20s -> %-20s %s %5d' % (k[0], k[1], k[2], v))
    if '--list' in args:
        want = args[args.index('--list') + 1]
        for r in rows:
            if want == 'all' or r['cls'] in want:
                print('%s %-8s src/%s:%d -> %s   [%s -> %s]  today: %s; map: %s' % (
                    r['cls'], r['sub'], r['file'], r['line'], r['target'], r['src'], r['dst'], r['today_ruled_why'], r['map_true_why']))
    if '--grouped' in args:                                    # the (b) and (c) rows, one line per source file
        g = collections.defaultdict(list)
        for r in rows:
            if r['cls'] in 'bc': g[(r['cls'], r['sub'], r['file'], r['dst'])].append('%d %s' % (r['line'], r['target']))
        for k in sorted(g): print('%s %-8s src/%s -> %s: %s' % (k[0], k[1], k[2], k[3], '; '.join(g[k])))
    if '--csv' in args:
        with open(args[args.index('--csv') + 1], 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    if '--ambiguous' in args:
        for a in ambiguous: print('ambiguous  src/%s:%d  "%s"' % a)
    # a directory under src/ whose files get no tag is an error, never a default
    bare = collections.Counter(rel.rsplit('/', 1)[0] if '/' in rel else '.' for rel in files if tag(rel) is None)
    for d in sorted(bare):
        print('ERROR: untagged directory: src/%s/ (%d files): add a DIRS entry (or FILES entries)' % (d, bare[d]))
    return 1 if bare else 0

if __name__ == '__main__':
    sys.exit(main())
