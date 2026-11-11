#!/opt/homebrew/bin/python3
# build_fixtures.py — the session report's QML test data: src/Gui/tests/qml/data/session_report/<case>.json, fixtures.js
# (the same maps as a `.pragma library`, because the QuickTest suite cannot read a local file with XMLHttpRequest) and
# stills/ (two neutral placeholder JPEGs every still points at). Read by src/Gui/tests/qml/tst_session_report.qml, the
# report's layout contract on the stress set (docs/design/session_report_design.md §9.2). The .json files are the
# canonical copies; fixtures.js is written from them.
#
# Run it through fixtures.sh beside it (it dumps the CURRENT SessionReportModel's map for each source session first):
#     zsh tools/session-report-mock/fixtures.sh            dump + build
#     zsh tools/session-report-mock/fixtures.sh build      build again from the dumps already on the share
# or directly:  build_fixtures.py --dumps <dir of <tag>.json report maps> --lib <folder of the per-tag scratch libraries>
#                                 [--pseudonymise]
# After a regenerate, test_01's expectedPages (the page count per case) must be re-recorded AND looked at: a count that
# moves is a layout change. A re-run is byte-identical while the source sessions in the library are unchanged.
#
# EVERY CASE IS A MODEL DUMP, OR A STRESS TRANSFORM OF ONE (fixtures.sh names each tag's session):
#   model dumps     nick (Nick 20 Oct s3: 10 swings, five work-ons, launch monitor, earlier sessions that day) — the base of
#                   every transform; jess (Jess 20 Oct s3: two "Linked, not proven" links, a held habit); no-launch-monitor
#                   (Mark 9 Sep: cameras only, no ball, no earlier session); long-session (Mark 7 Oct: 50 swings, five
#                   work-ons, no launch monitor); note-one-swing (Nick 13 Oct: the library's one-swing note, no monitor).
#   transforms      clean · one-work-on · five-work-ons-longest-text · causes-at-caps · sixty-swings-three-clubs ·
#   of nick         junior-no-photos · longest-names-and-note · note — each function says what it stresses and changes only
#                   that. RULES: every sentence whose meaning a transform changes is rebuilt with the model's own template
#                   (the Python port of report_words.h below, each function named after its C++ original); every edit of
#                   a model sentence goes through edit()/sub(), which stop the build when the text they expect is not there,
#                   so a model that rewords a sentence fails here instead of leaving a contradiction in a fixture.
#   page blocks     each on ONE session's map, with that session's own page blocks (no page is grafted from another):
#                   launch-monitor-appendix, posture, tendencies, consistency = Nick's map + that block; wrist = Mark 18 Aug
#                   Wrist_02's map + its Posture and Wrist; delivery = Jess 20 Oct Wrist_01's map + its Posture, Wrist and
#                   Club delivery (the fit over her day); consistency-mixed-clubs = Mark 20 Oct Wrist_01's map + its
#                   Consistency (7 iron and gap wedge).
#   Every dump is NORMALISED the same way, and only so:
#     · the brand is a fictional studio: the brand is the studio's settings, not the session, and no real contact details
#       belong in the repo;
#     · the prepared date is the session's date: the model stamps the day it ran;
#     · paths into the scratch library read "/lib/…";
#     · every still the model made points at a placeholder in stills/ (the repo is public: no photograph of anyone is in
#       it): stills/frame.jpg for a Posture picture frame, stills/still.jpg for every other still. tst_session_report
#       resolves "stills/…" against the fixture folder when it loads a map;
#     · a base map carries no page block (posture … lmAppendix are null): each page has its own case;
#     · --pseudonymise renames every golfer by PSEUDONYMS; off by default (Mark, 11 Nov 2026: keep the real names for now).
# keys_not_written() prints every key a case carries that no dump writes (KNOWN_KEYS: the ones the model writes into a list
# that is empty in every source session).
import argparse, copy, json, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "src/Gui/tests/qml/data/session_report")
STILLS = os.path.join(OUT, "stills")
DOT = "  ·  "
# the page blocks: none in a base map; each block case adds one, cut from a dump
BLOCKS = ["posture", "wrist", "delivery", "tendencies", "consistency", "lmAppendix"]
DUMP_TAGS = ["nick", "jess3", "nolm", "long", "rich", "jess", "mixed", "clean", "note"]
# the fictional studio every case carries (stamp: the model's hash of the brand settings; none for a made-up brand)
BRAND = {"studio": "Northfield Golf Studio", "monogram": "NG", "logo": "", "accent": "#1F5A4C", "coach": "Alex Morgan",
         "coachTitle": "Head coach", "coachLine": "Alex Morgan  ·  Head coach", "contact": "alex@northfieldgolf.example  ·  01234 567890",
         "web": "northfieldgolf.example", "footLine": "Alex Morgan, Northfield Golf Studio  ·  alex@northfieldgolf.example  ·  01234 567890",
         "signoff": "Book your next session at northfieldgolf.example or reply to this email.", "coachPhoto": "", "stamp": ""}
# --pseudonymise: the one mapping, full name → (full name, first name); applied to every string, folder names included
PSEUDONYMS = {"Nick Henn": ("Sam Carter", "Sam"), "Jess Ratcliffe": ("Lucy Hart", "Lucy"),
              "Mark Liversedge": ("Tom Fairley", "Tom"), "Chris Marriott": ("Dan Ellis", "Dan"),
              "Paddy Marriott": ("Joe Ellis", "Joe")}
# keys the model writes that no source session happens to carry (its list is empty in every dump), with where it writes them
KNOWN_KEYS = {".watching[].golfer": "session_report_model.cpp:1403 (watching, a clean session's)",
              ".watching[].count": "session_report_model.cpp:1403 (watching, a clean session's)"}


def fail(msg):
    sys.exit("TRANSFORM: " + msg)


def edit(s, old, new, what):
    """Replace `old` in a model sentence; stop the build when it is not there."""
    if old not in s:
        fail("%s: %r is not in %r" % (what, old, s[:160]))
    return s.replace(old, new)


def sub(pattern, repl, s, what, at_least=1):
    out, n = re.subn(pattern, repl, s)
    if n < at_least:
        fail("%s: /%s/ matched nothing in %r" % (what, pattern, s[:160]))
    return out


# ── report_words.h, ported (only what the transforms rebuild) ────────────────────────────────────

NUM_WORDS = ["no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
             "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"]


def numberWord(n): return NUM_WORDS[n] if 0 <= n <= 20 else str(n)
def cap1(s): return s[:1].upper() + s[1:]
def low1(s):
    # an acronym or a name the pack wrote in capitals stays as written ("GC Quad", "OTT")
    if len(s) >= 2 and s[0].isupper() and s[1].isupper(): return s
    return s[:1].lower() + s[1:]
def andList(xs): return "".join(xs) if len(xs) <= 1 else ", ".join(xs[:-1]) + " and " + xs[-1]
def swingsN(n): return "%d swing%s" % (n, "" if n == 1 else "s")
def countOf(f, j): return "%d of %d swing%s" % (f, j, "" if j == 1 else "s")
def gridCount(f, j): return "%d of %d" % (f, j)
def habitsWord(n): return numberWord(n) + (" habit" if n == 1 else " habits")
def habitsCell(f, of): return gridCount(f, of)


def swingsOn(f, j):
    if j <= 0: return ""
    if f == j: return "on the one swing we could measure" if j == 1 else "on all %d swings" % j
    return "on " + countOf(f, j)


def everyWords(least):
    if least >= 1.0 - 1e-9: return "every swing"
    if least >= 0.8: return "almost every swing"
    if least >= 0.5: return "most swings"
    return "many of your swings"


def headline(per, sess, least, part, goal):
    n = per + sess
    if n <= 0: return ""
    if sess == 0: s = cap1(habitsWord(n)) + " showed on " + everyWords(least) + "."
    elif per == 0: s = cap1(habitsWord(n)) + " showed over the whole session."
    else: s = cap1(habitsWord(n)) + " showed: %s on %s, and %s over the whole session." % (numberWord(per), everyWords(least), numberWord(sess))
    if goal: s += " We start with %s: %s." % (part, low1(goal))
    return s


def headlineClean(watching):
    return ("Nothing repeated often enough to call a habit. Below: what went well, and what showed now and then — worth keeping an eye on."
            if watching > 0 else "Nothing repeated often enough to call a habit. Below: what went well.")


def headlineNote(swings, lm):
    s = cap1(numberWord(swings)) + (" swing is" if swings == 1 else " swings is") + " too few to call a habit, so this is a short note rather than a full report"
    return s + (": here is what the ball did." if lm else ".")


def gridTitle(every, per, clean):
    if clean: return "What we measured, swing by swing"
    if every and per > 1: return "The same %s, every swing" % numberWord(per)
    return "Your habits, swing by swing"


def gridStandfirst(swings, stub):
    s = "Each row is one thing we measured; each column is " + ("your one swing" if swings == 1 else
        "one of your %s swings, in the order you hit them" % numberWord(swings)) + ". A filled dot means it showed on that swing."
    return s + (" A short stub means it could not be measured on that swing." if stub else "")


def gridGroupTitle(key, wos):
    if key == "habit": return "Your habit — the one work-on" if wos == 1 else "Your habits — the %s work-ons" % numberWord(wos)
    return {"seen": "Seen, not yet habits", "ball": "What the ball did"}.get(key, "")


def referenceText(swing, ball, carry, speed, longest, inside, judged):
    s = "Swing %d came closest to your targets" % swing
    if ball and carry != "-":
        b = "" if speed == "-" else " at %s mph ball speed" % speed
        s += (" — and it was your longest, a %s yard carry%s." % (carry, b)) if longest else (": a %s yard carry%s." % (carry, b))
    else:
        s += "."
    if judged > 0:
        if inside == judged:
            s += " It was inside the target range for your work-on." if judged == 1 else " It was inside the target range for every work-on we could measure on it."
        elif inside > 0:
            s += " It was inside the target range for %s of the %s work-ons we could measure on it." % (numberWord(inside), numberWord(judged))
        else:
            s += " No swing was inside every target range; this one missed by least."
    if longest and ball: s += " Keep it in mind: it is the swing the work-ons are moving you toward."
    return s


def todayColumn(i, swings): return "Session %d · %s" % (i, swingsN(swings))


def todayCell(pattern, f, j, session_level=False):
    if session_level: return "habit" if pattern else ""
    if j <= 0: return "-"
    return ("habit " if pattern else "") + "%d/%d" % (f, j)


def todayAlready(already, habits):
    if habits <= 0: return ""
    if already <= 0:
        return "Today's habit was not a habit in your first session." if habits == 1 else "None of today's habits was a habit in your first session."
    if already == habits:
        return "Today's habit was already there in your first session." if habits == 1 else "All %s of today's habits were already there in your first session." % numberWord(habits)
    return "%s of today's habits %s already there in your first session." % (cap1(numberWord(already)), "was" if already == 1 else "were")


def todayEarlierHabit(name, f, j, min_judged=3):
    if j < min_judged:
        if j <= 0: return "%s was a habit earlier today; this session could not measure it." % name
        return "%s was a habit earlier today; this session could only judge it on %s, too few to say." % (name, "one swing" if j == 1 else numberWord(j) + " swings")
    if f == 0: return "%s was a habit earlier today and did not show here (%s)." % (name, countOf(0, j))
    return "%s was a habit earlier today and came down to %s here." % (name, countOf(f, j))


def ruleStillHappeningToday(resolving, listed):
    if resolving <= 0:
        return ("Your habit did not ease during the session, so this rule changed nothing today." if listed == 1
                else "None of the %s eased during the session, so this rule changed nothing today." % numberWord(listed))
    return "One eased during the session and moved to the back." if resolving == 1 else "%s eased during the session and moved to the back." % cap1(numberWord(resolving))


def ruleSwingOrderToday(decided):
    return ("This is what set today's order: from the earliest point in the swing to the latest." if decided > 0
            else "Today's habits sit at the same point in the swing, so this rule changed nothing.")


def ruleBiggestMissToday(swing_order_ran, decided):
    if decided <= 0:
        return ("No two of today's habits sit at the same point in the swing, so this rule changed nothing." if swing_order_ran
                else "Nothing was left for it to decide today.")
    if swing_order_ran: return "It settled the order between habits at the same point in the swing. The bars below show how far each one missed."
    return "This is what set today's order. The bars below show how far each one missed."


def timelineNote(any_mark, first_is_earliest, why, session_names):
    s = ""
    if any_mark:
        s = ("Your first work-on is also the earliest in the swing. Habits early in the swing tend to set up the ones after them, which is one more reason to start there."
             if first_is_earliest else
             "Your first work-on is not the earliest in the swing; it leads because %s. Your coach may still choose to start earlier in the swing."
             % ("your swings show it setting up another" if why == 0 else "it missed by most"))
    if session_names:
        s += (" " if s else "") + andList(session_names) + (" is" if len(session_names) == 1 else " are") + " read over the whole session, so " \
             + ("it has" if len(session_names) == 1 else "they have") + " no place on it."
    return s


def notListedOverCap(cap): return "A habit too, but the list stops at %s. It moves up when one of these clears." % numberWord(cap)


def phaseCode(pos):
    if pos >= 11.0 or pos < 1.0: return ""
    r = round(pos)
    return "P%d" % r if abs(pos - r) < 1e-9 else ""


def phaseWords(pos):
    for lim, w in [(1.25, "at address"), (3.75, "going back"), (4.25, "at the top"), (4.75, "at the start of the downswing"),
                   (6.75, "coming down"), (7.25, "at impact"), (9.75, "after impact")]:
        if pos >= 11.0: return "over the session"
        if pos < lim: return w
    return "at the finish"


def partWords(pos):
    if pos >= 11.0: return "your timing"
    for lim, w in [(1.25, "your set-up"), (4.25, "your backswing"), (4.75, "your transition"), (6.75, "your downswing"),
                   (7.25, "impact"), (9.75, "your follow-through")]:
        if pos < lim: return w
    return "your finish"


def workOnKicker(rank, of, pos, f, j):
    head = "Work-on %d of %d" % (rank, of)
    if pos >= 11.0: return head + DOT + "Over the whole session"
    code, where = phaseCode(pos), phaseWords(pos)
    p = cap1(where) if not code else code + ", " + where
    return head + DOT + p + ((DOT + countOf(f, j)) if j > 0 else "")


def causesLead(lead, spotted, supported, every, could, ruled, authored):
    groups = (["what we spotted"] if spotted > 0 else []) + (["what we couldn't check"] if could else []) + (["what we've ruled out"] if ruled else [])
    here = "" if not groups else "here is " + andList(groups)
    if lead == 1:
        who = "One possible cause showed up on the same swings" if spotted == 1 else "%s possible causes showed up on the same swings" % cap1(numberWord(spotted))
        if supported: return who + ", and your swings point to it: on the swings where it did not show, this one went away too. " + cap1(here) + "."
        if every: return who + ". Because both happened on every swing, we can't prove one drives the other — so " + here + "."
        return who + ". Showing up together is not proof that one drives the other — so " + here + "."
    fail("causesLead: only the spotted lead is ported")


def causesSpottedItem(name, f, j, rank): return name + " — on " + countOf(f, j) + ((", work-on %d" % rank) if rank > 0 else "")
def causesSpottedNote(supported): return "your swings point to it" if supported else "it fits, but isn't proof"
def causesCouldBeMore(more): return "%d more, less likely" % more if more > 0 else ""
def causesRuledOutMore(more): return "%d more" % more if more > 0 else ""


def cameWithShown(golfer, f, j): return "%s (%d of the %d swings we could see)" % (low1(golfer), f, j)


def cameWithText(shown, clean, every):
    out = []
    if shown: out.append("It came with %s. That fits the coaching model, though showing up together is not proof that one causes the other." % andList(shown))
    if clean:
        e = "on every swing" if every else "on every swing we could see"
        out.append(("The coaching model says it can bring other habits with it. Not today — %s, %s." if not shown else "And %s, %s.") % (e, andList(clean)))
    return " ".join(out)


def ballNote(swings, several):
    if several: return "Each club's typical value (the middle of its swings), with its range from lowest to highest."
    if swings <= 2: return "Your one swing's numbers." if swings == 1 else "Middle of your two swings in large type; the lower and higher beneath."
    return "Typical value (the middle of your %s swings) in large type; the range from lowest to highest beneath." % numberWord(swings)


def numbersNote(left, per, swings, habits, several):
    s = "Path, face and offline: positive is right of target, negative left (%s-handed golfer)." % ("left" if left else "right")
    if habits and per > 0:
        s += " Habits: whether your work-on showed on that swing." if per == 1 else " Habits: how many of your %s work-ons showed on that swing." % numberWord(per)
    if swings >= 3:
        s += " Typical: the middle value of each club's swings." if several else " Typical: the middle value of your %s swings." % numberWord(swings)
    return s


def article(nxt):
    w = nxt.strip().lower()
    if not w: return "a"
    m = re.match(r"\d+", w)
    if m:
        n = int(m.group(0))
        return "an" if n in (8, 11, 18) or 80 <= n <= 89 or 800 <= n <= 899 else "a"
    return "an" if w[0] in "aeio" else "a"


def clubWord(club):
    out = []
    for w in club.split():
        short = (len(w) <= 3 and w == w.upper() and any(c.isdigit() for c in w) and any(c.isalpha() for c in w)) or (len(w) <= 2 and w == w.upper())
        out.append(w if short else w.lower())
    return " ".join(out)


def clubsWords(clubs):
    w = []
    for c in clubs:
        x = clubWord(c)
        if x and x not in w: w.append(x)
    return andList(w)


def kicker(date_long, clubs, swings, of_day):
    return DOT.join(p for p in ["Session of " + date_long, clubs, swingsN(swings) if swings > 0 else "", of_day] if p)


def runningHead(golfer, date_short, clubs):
    return DOT.join(p for p in [golfer, "session of " + date_short, clubs] if p)


def aboutRanges(ruled_out_shown):
    s = ("Each habit is judged against a target range. Most ranges are coaching judgement, set from what good ball-strikers do; "
         "a few come from published measurements. They are not fitted to you. A swing outside the range is not a mistake in itself — "
         "it is where the evidence points.")
    return s + (" “Ruled out” on a work-on page means measured, and fine, on these swings with these cameras — not gone for good." if ruled_out_shown else "")


def aboutFootnote(photos):
    return ("Measured and prepared with PinPoint Studio. Swing images show only the golfer; video stays in the studio." if photos
            else "Measured and prepared with PinPoint Studio. Video stays in the studio.")


NOT_CHECKED_TOUCH = " Where that touches one of your work-ons, its page says so."
NOT_CHECKED_NONE = " Nothing in this report depends on them."


def lmFig(v, dp, thousands=False):
    """session_report_model.cpp lmFig: fixed decimals, the typographic minus, a thousands comma when asked."""
    s = ("{:,.%df}" if thousands else "{:.%df}") .replace("%d", str(dp)).format(v)
    s = s.replace("-", "−")
    return "0" if s in ("−0",) else s


def figSigned(v, dp):
    s = "{:.%df}".replace("%d", str(dp)).format(abs(v))
    return ("−" if v < 0 and float(s) != 0 else "+") + s


# ── reading the base ────────────────────────────────────────────────────────────────────────────

POS_OF_WORDS = {"at address": 1.0, "going back": 2.5, "at the top": 4.0, "at the start of the downswing": 4.5, "coming down": 5.5,
                "at impact": 7.0, "after impact": 8.5, "at the finish": 10.0}


def work_on_facts(w):
    """(swingPos, fired, judged) of a work-on, read back from its kicker (workOnKicker's own shape)."""
    k = w["kicker"]
    if k.endswith("Over the whole session"):
        return 11.0, 0, 0
    m = re.match(r"^Work-on \d+ of \d+  ·  (?:P(\d+), )?([^·]+?)(?:  ·  (\d+) of (\d+) swings?)?$", k)
    if not m:
        fail("work-on kicker %r is not workOnKicker's shape" % k)
    pos = float(m.group(1)) if m.group(1) else POS_OF_WORDS.get(m.group(2).strip().lower())
    if pos is None:
        fail("work-on kicker %r: no position for %r" % (k, m.group(2)))
    return pos, int(m.group(3) or 0), int(m.group(4) or 0)


def col(numbers, head):
    if head not in numbers["cols"]:
        fail("the numbers table has no %r column" % head)
    return numbers["cols"].index(head)


def num(s):
    try:
        return float(s.replace(",", "").replace("−", "-"))
    except (ValueError, AttributeError):
        return None


def marks_count(marks):
    return sum(1 for x in marks if x == "fired"), sum(1 for x in marks if x in ("fired", "clean"))


# ── normalising a dump ────────────────────────────────────────────────────────────────────────────

def walk_strings(v, fn):
    if isinstance(v, dict):
        return {k: walk_strings(x, fn) for k, x in v.items()}
    if isinstance(v, list):
        return [walk_strings(x, fn) for x in v]
    if isinstance(v, str):
        return fn(v)
    return v


def normalise(m, lib):
    m = copy.deepcopy(m)
    meta = m["meta"]
    long_date = re.sub(r"^[A-Za-z]+ ", "", meta["date"])            # "Tuesday 20 October 2026" → "20 October 2026"
    dates = [(meta["prepared"], long_date), (meta["preparedShort"], meta["dateShort"])]

    def fix(s):
        for a, b in dates:
            s = s.replace(a, b)
        if s.startswith("file://") and "/report-stills/" in s:
            return "stills/frame.jpg" if os.path.basename(s).startswith("posture_") else "stills/still.jpg"
        return s.replace(lib, "/lib")
    m = walk_strings(m, fix)
    m["brand"] = {k: BRAND.get(k, "") for k in m["brand"]}
    for k in BLOCKS:
        m.setdefault(k, None)
    bad = leaks(m)
    if bad:
        sys.exit("a dump still names local files:\n  " + "\n  ".join(bad[:10]))
    return m


def leaks(v, path=""):
    """Strings that still name this machine (a home folder, the app's cache, the share)."""
    if isinstance(v, dict):
        return [x for k, y in v.items() for x in leaks(y, path + "." + k)]
    if isinstance(v, list):
        return [x for i, y in enumerate(v) for x in leaks(y, "%s[%d]" % (path, i))]
    if isinstance(v, str) and re.search(r"/Users/|/mnt/|/Library/|file://", v):
        return [path + ": " + v[:120]]
    return []


def pseudonymise(m):
    def fix(s):
        for full, (nf, nfirst) in PSEUDONYMS.items():
            first = full.split()[0]
            s = s.replace(full, nf).replace(full.replace(" ", "-"), nf.replace(" ", "-"))
            s = re.sub(r"\b%s\b" % re.escape(first), nfirst, s)
        return s
    return walk_strings(m, fix)


def base_of(m):
    """A normalised dump with its page blocks taken out: what every stress transform starts from."""
    r = copy.deepcopy(m)
    for k in BLOCKS:
        r[k] = None
    return r


def with_block(base, key, dump):
    if not dump.get(key):
        sys.exit("the dump for `%s` has no `%s` block: the session no longer reads one, pick another source in fixtures.sh" % (dump["meta"]["golfer"], key))
    r = copy.deepcopy(base)
    r[key] = copy.deepcopy(dump[key])
    return r


def key_paths(v, p, out):
    if isinstance(v, dict):
        for k, x in v.items():
            out.add(p + "." + k)
            key_paths(x, p + "." + k, out)
    elif isinstance(v, list):
        for x in v:
            key_paths(x, p + "[]", out)
    return out


def keys_not_written(cases, dumps):
    """Every key a case carries that no dump carries and KNOWN_KEYS does not vouch for: a transform must not invent a field."""
    known = set(KNOWN_KEYS)
    for d in dumps.values():
        key_paths(d, "", known)
    for name, c in cases.items():
        for p in sorted(key_paths(c, "", set()) - known):
            print("NOT IN ANY DUMP (%s): %s" % (name, p))


# ── the stress transforms (each of the nick base, each changing only what it stresses) ──────────────

def today_rebuilt(r, keep_names, habits_kept, first_session_state):
    """Earlier today for a case that keeps only `keep_names` of today's work-ons: their rows, then the base's earlier-habit
    rows; the note from todayAlready and todayEarlierHabit (what the model writes, session_report_model.cpp "Earlier today")."""
    t = r.get("today")
    if not t:
        return
    base_wos = {w["name"] for w in r["workOns"]}
    earlier = [row for row in t["rows"] if row["name"] not in base_wos]
    t["rows"] = [row for row in t["rows"] if row["name"] in keep_names] + earlier
    note = [todayAlready(sum(1 for n in keep_names if first_session_state.get(n) == "pattern"), habits_kept)] if habits_kept else []
    for row in earlier:
        m = re.match(r"^(?:habit )?(\d+)/(\d+)$", row["marks"][-1]["text"])
        if not m:
            fail("today: %r's mark in this session is %r" % (row["name"], row["marks"][-1]["text"]))
        note.append(todayEarlierHabit(row["name"], int(m.group(1)), int(m.group(2))))
    t["note"] = " ".join(n for n in note if n)


def clean(base, pack):
    """No habits: no work-on pages, no order page, no practice, no reference swing; the cover's watch list (the model fills
    it only for a clean session, by its rule below); the
    grid without its habit rows; Earlier today with only the earlier habits, none of them a habit in this session."""
    r = copy.deepcopy(base)
    by_label = {c.get("label", ""): c for c in pack["conditions"]}
    today_rebuilt(r, [], 0, {})
    for row in (r.get("today") or {}).get("rows", []):          # nothing is a habit in this session
        mk = row["marks"][-1]
        if mk["state"] == "pattern":
            mk["state"] = "inconclusive"
            mk["text"] = edit(mk["text"], "habit ", "", "clean today cell")
    # the watch list by the model's rule (session_report_model.cpp "watching"): every listable condition the case keeps
    # that fired on a swing — the grid's "seen" rows and Earlier today's earlier habits, this session's counts — most
    # fired first, then by id, kWatchingCap (5) at most; its golfer words cap1()'d and its count countOf()
    fired = {}
    for row in r["grid"]["rows"]:
        if row["group"] == "seen":
            fired[row["name"]] = marks_count(row["marks"])
    for row in (r.get("today") or {}).get("rows", []):
        m = re.match(r"^(\d+)/(\d+)$", row["marks"][-1]["text"])
        if m:
            fired.setdefault(row["name"], (int(m.group(1)), int(m.group(2))))
    watch = []
    for name, (f, j) in fired.items():
        c = by_label.get(name)
        if not c or not c.get("golfer"):
            fail("clean: the pack has no condition %r with golfer words" % name)
        if f > 0:
            watch.append((-f, c["id"], {"golfer": cap1(c["golfer"].strip()), "count": countOf(f, j)}))
    watching = [w for _, _, w in sorted(watch, key=lambda x: (x[0], x[1]))]
    r["kind"] = "clean"
    r["watching"] = watching[:5]                                       # kWatchingCap
    r["headline"] = headlineClean(len(r["watching"]))
    r["found"], r["workOnsBrief"], r["workOns"] = [], [], []
    r["order"], r["checks"], r["practice"] = None, [], None
    g = r["grid"]
    g["rows"] = [row for row in g["rows"] if row["group"] != "habit"]
    g["groups"] = [x for x in g["groups"] if x["key"] != "habit"]
    g["more"] = {k: v for k, v in g.get("more", {}).items() if k != "habit"}
    g["title"] = gridTitle(False, 0, True)
    g["sessionNote"] = ""
    g["best"], g["bestText"], g["bestStills"], g["bestLabels"] = 0, "", [], []   # no habit, no reference swing
    nb = r["numbers"]
    for row in nb["rows"]:
        row.pop("habitsText", None)
    nb["habitsHead"] = ""
    nb["note"] = numbersNote(False, 0, r["meta"]["swings"], False, False)
    a = r["about"]
    a["ranges"] = aboutRanges(False)
    if a.get("notChecked"):
        a["notChecked"] = edit(a["notChecked"], NOT_CHECKED_TOUCH, NOT_CHECKED_NONE, "clean about.notChecked") \
            if NOT_CHECKED_TOUCH in a["notChecked"] else a["notChecked"]
    return r


def keep_workons(base, n):
    """One work-on only: the singular words everywhere (headline, grid title and group, work-on kicker, numbers note, order
    rules, Habits column), one work-on page, the order page with one item, no links and no over-the-cap habits."""
    r = copy.deepcopy(base)
    total = len(r["workOns"])
    facts = [work_on_facts(w) for w in r["workOns"]]
    kept = r["workOns"][:n]
    names = {w["name"] for w in kept}
    per = sum(1 for (pos, _, _) in facts[:n] if pos < 11.0)
    sess = n - per
    least = min([f / j for (pos, f, j) in facts[:n] if pos < 11.0 and j > 0] or [1.0])
    r["headline"] = headline(per, sess, least, partWords(facts[0][0]), kept[0]["goal"])
    first_state = {row["name"]: row["marks"][0]["state"] for row in (r.get("today") or {}).get("rows", [])}
    today_rebuilt(r, [w["name"] for w in kept], n, first_state)
    for w, (pos, f, j) in zip(kept, facts):
        w["kicker"] = workOnKicker(w["rank"], n, pos, f, j)
    r["workOns"] = kept
    r["found"] = r["found"][:n]
    r["workOnsBrief"] = r["workOnsBrief"][:n]
    g = r["grid"]
    g["rows"] = [row for row in g["rows"] if row["group"] != "habit" or row["name"] in names]
    habit_rows = [row for row in g["rows"] if row["group"] == "habit"]
    every = bool(habit_rows) and all(marks_count(x["marks"])[0] == marks_count(x["marks"])[1] for x in habit_rows)
    g["title"] = gridTitle(every, per, False)
    g["groups"] = [dict(x, title=gridGroupTitle("habit", n)) if x["key"] == "habit" else x for x in g["groups"]]
    g["more"] = {k: v for k, v in g.get("more", {}).items() if k != "habit"}   # a one-habit session has no habit past the list
    if g.get("best"):
        k = g["best"] - 1
        hm = [row["marks"][k] for row in habit_rows]
        inside, judged = sum(1 for x in hm if x == "clean"), sum(1 for x in hm if x in ("fired", "clean"))
        nb = r["numbers"]
        carry = nb["rows"][k]["cells"][col(nb, "Carry")]
        longest = num(carry) is not None and num(carry) >= max(num(x["cells"][col(nb, "Carry")]) or 0 for x in nb["rows"])
        g["bestText"] = referenceText(g["best"], True, carry, nb["rows"][k]["cells"][col(nb, "Ball speed")], longest, inside, judged)
    o = r["order"]
    o["items"] = o["items"][:n]
    o["timeline"] = [t for t in o["timeline"] if t["rank"] <= n]
    o["links"] = []
    o["timelineNote"] = timelineNote(bool(o["timeline"]), True, 1, [w["name"] for w, (pos, _, _) in zip(kept, facts) if pos >= 11.0])
    swing_order_ran = any(x["title"] == "Then the order of your swing" for x in o["rules"])
    for rule in o["rules"]:
        if rule["title"] == "What is still happening comes first":
            rule["today"] = ruleStillHappeningToday(0 if rule["today"].startswith("None of the") else fail("one-work-on: a habit eased"), n)
        elif rule["title"] == "Then the order of your swing":
            rule["today"] = ruleSwingOrderToday(0)
        elif rule["title"] == "Then the biggest miss first":
            rule["today"] = ruleBiggestMissToday(swing_order_ran, 0)
    over = notListedOverCap(total)
    o["notListed"] = [x for x in o["notListed"] if x["why"] != over]
    if any("(work-on " in x["why"] for x in o["notListed"]):
        fail("one-work-on: a not-listed result names a work-on rank")
    checks = []
    for c in r["checks"]:
        items = []
        for it in c["body"].split(" · "):
            m = re.match(r"^(.*) \(([\d, ]+)\)$", it)
            if not m:
                fail("one-work-on: check item %r has no work-on ranks" % it)
            ranks = [int(x) for x in m.group(2).split(", ") if int(x) <= n]
            if ranks:
                items.append("%s (%s)" % (m.group(1), ", ".join(map(str, ranks))))
        if items:
            title = "A quick physical test would tell:" if c["title"].startswith("A quick physical test would tell") else c["title"]
            checks.append({"title": title, "body": " · ".join(items)})
    r["checks"] = checks
    for x in r["practice"]["steps"]:
        if "work-on" in x["body"] and "work-on 1" not in x["body"]:
            fail("one-work-on: a practice step names another work-on: %r" % x["body"])
    nb = r["numbers"]
    hr = habit_rows[0] if habit_rows else None
    for k, row in enumerate(nb["rows"]):
        row["habitsText"] = habitsCell(1 if hr and hr["marks"][k] == "fired" else 0, per)
    nb["note"] = numbersNote(False, per, r["meta"]["swings"], True, False)
    return r


LONG_GOAL = "Keep your lead arm connected to your chest until past impact"
LONG_CUE = ("Feel your trail knee stay soft and flexed, pointing at the ball, while your trail pocket turns behind you and your "
            "weight settles into the inside of your trail foot. Let the turn happen above the knee, never through it.")
LONG_CHECK = ("Film from down the line with the phone at hip height, level, pointing at your hands. Pause at the top: your trail "
              "knee should be about as bent as it was at address, and still over the ball of your foot, not behind it.")


def longest_text(base, pack):
    """Five work-ons, each with the longest goal, cue, check, why, What came with it and drill text: the work-on pages at
    their longest (the pack's longest whys, golfer words, wells, drill instructions, notes and targets; the cue and check
    are written to the cue's and check's length limits). The headline is the model's for the new first goal."""
    r = copy.deepcopy(base)
    n = r["meta"]["swings"]
    whys = sorted([c.get("golferWhy", "") for c in pack["conditions"] if c.get("golferWhy")], key=len)
    golfers = sorted({c.get("golfer", "") for c in pack["conditions"] if c.get("golfer")}, key=lambda s: (-len(s), s))
    wells = sorted([c.get("golferWell", "") for c in pack["conditions"] if c.get("golferWell")], key=len)
    drills = sorted(pack["drills"], key=lambda d: len(d.get("instruction", "")))
    notes = sorted([d.get("note", "") for d in pack["drills"] if d.get("note")], key=len)
    targets = sorted([d.get("targets", "") for d in pack["drills"] if d.get("targets")], key=len)
    facts = [work_on_facts(w) for w in r["workOns"]]
    for i, w in enumerate(r["workOns"]):
        goal = (LONG_GOAL if i % 2 == 0 else cap1(wells[-1 - i]))[:60]
        w["goal"] = goal
        r["workOnsBrief"][i]["goal"] = goal
        r["order"]["items"][i]["goal"] = goal
        w["cue"] = LONG_CUE
        w["check"] = LONG_CHECK
        w["why"] = whys[-1 - i]
        w["cameWith"] = cameWithText([cameWithShown(golfers[2 * i], n - 1, n), cameWithShown(golfers[2 * i + 1], n - 3, n)],
                                     [low1(x.rstrip(".")) for x in wells[-4 - i:-1 - i]], True)
        d0, d1 = drills[-1 - i], drills[-6 - i]
        w["drills"] = [
            {"label": d0["label"], "instruction": d0["instruction"], "targets": targets[-1 - i], "note": notes[-1 - i],
             "equipment": "an alignment stick and a mirror or a phone on a stand"},
            {"label": d1["label"], "instruction": d1["instruction"], "targets": d1.get("targets", ""), "note": d1.get("note", ""),
             "equipment": d1.get("equipment", "") if isinstance(d1.get("equipment", ""), str) else ""},
        ]
        r["workOnsBrief"][i]["drill"] = d0["label"]
    per = sum(1 for (pos, _, _) in facts if pos < 11.0)
    least = min([f / j for (pos, f, j) in facts if pos < 11.0 and j > 0] or [1.0])
    r["headline"] = headline(per, len(facts) - per, least, partWords(facts[0][0]), r["workOns"][0]["goal"])
    return r


def causes_at_caps(base, pack):
    """Every cause group of every work-on at its cap (src/Coaching/session_report.h: spotted is uncapped — three here;
    could-be kCouldBeCap 6 and "7 more, less likely"; ruled-out kRuledOutCap 4 and "6 more"), with the pack's longest
    condition names, the model's lead for that many spotted; the order page's checks and "not on the list" at length."""
    r = copy.deepcopy(base)
    # longest first; equal lengths alphabetically (a set has no order: without the tie-break every run differed)
    names = sorted({c.get("golfer", "") for c in pack["conditions"] if c.get("golfer")}, key=lambda s: (-len(s), s))
    n = r["meta"]["swings"]
    nw = len(r["workOns"])
    for i, w in enumerate(r["workOns"]):
        k = i * 3
        spotted = [causesSpottedItem(cap1(names[k + j]), n, n, (i + j + 1) % nw + 1) for j in range(3)]
        w["causes"] = {
            "lead": causesLead(1, len(spotted), False, True, True, True, 0),
            "rows": [
                {"kind": "seen", "title": "What we spotted", "items": spotted, "more": "", "note": causesSpottedNote(False)},
                {"kind": "possible", "title": "Could be behind it — we couldn't check",
                 "items": ["<i>A quick physical test would tell:</i> Lead-hip mobility · Turning your hips without your chest · Trail-hip mobility"
                           + "<br><i>Hidden on today's swings:</i> " + cap1(names[k + 12]) + " · " + cap1(names[k + 13])
                           + "<br><i>Your coach can look:</i> " + cap1(names[k + 15])],
                 "more": causesCouldBeMore(7), "note": ""},
                {"kind": "ruledOut", "title": "Ruled out — measured, and fine",
                 "items": [cap1(names[k + 20 + j]) for j in range(4)], "more": causesRuledOutMore(6), "note": ""},
            ]}
    r["checks"] = [
        {"title": "A quick physical test would tell. Each could sit behind two or more of your work-ons:",
         "body": "Lead-hip mobility (1, 3, 4) · Upper-back mobility (2, 4, 5) · Trail-hip mobility (1, 3, 5) · Core stability (3, 5) · Turning your hips without your chest (3, 4)"},
        {"title": "Your coach can look; the cameras can't see these yet:",
         "body": "Across the line at the top (4) · Flat lower back at address (5) · Flat shoulder plane (4) · Club taken inside (2, 4) · Club taken outside (4)"},
    ]
    listed = {re.sub(r" \(\d+ of \d+\)$", "", x["name"]) for x in r["order"]["notListed"]}
    extra = [("Face closed to the path", n // 2), ("Face open to the path", n // 2 - 1), ("Path too far out-to-in", n // 3)]
    add = [(nm, f) for nm, f in extra if nm not in listed][:1]
    if not add:
        fail("causes-at-caps: every extra not-listed name is already listed")
    r["order"]["notListed"] = r["order"]["notListed"] + [
        {"name": "%s (%s)" % (nm, gridCount(f, n)),
         "why": "A result, not a movement to practice. It is what the club or ball did, and it changes when the movements behind it do."} for nm, f in add]
    return r


CLUBS = ["7 IRON", "5 IRON", "DRIVER"]
CLUB_CARRY = [0, 18, 85]            # yards added to carry and total for the second and third club
TIMES = 6                           # sixty swings of the ten: each base swing six times (src())


def sixty_three_clubs(base):
    """Sixty swings with three clubs (twenty each): the grid, the work-on figures and the numbers table at sixty columns /
    rows, the ball tiles and the typical row once per club. Swing i is the base's swing src(i) = (7i + i // 10) mod 10, the
    same for its marks, carry, figures and numbers, so each base swing is there exactly six times (twice per club) and
    every count of this session is the base's times six — rewritten wherever the map says one; per club the median of
    each reading is the base's, carry and total moved by that club's offset."""
    r = copy.deepcopy(base)
    n0 = r["meta"]["swings"]
    n = n0 * TIMES
    per_club = n // len(CLUBS)
    src = lambda i: (i * 7 + i // n0) % n0
    if sorted(src(i) for i in range(n)) != sorted(list(range(n0)) * TIMES):
        fail("sixty: src() does not use every base swing %d times" % TIMES)

    # every count of this session, times six (the today table's earlier sessions are other sessions: left alone)
    def scale(s):
        s = re.sub(r"\b(\d+) of (\d+) swings?\b", lambda m: countOf(int(m.group(1)) * TIMES, int(m.group(2)) * TIMES), s)
        s = re.sub(r"\b(\d+) of the (\d+) swings\b", lambda m: "%d of the %d swings" % (int(m.group(1)) * TIMES, int(m.group(2)) * TIMES), s)
        s = re.sub(r"\((\d+) of (\d+)\)", lambda m: "(%s)" % gridCount(int(m.group(1)) * TIMES, int(m.group(2)) * TIMES), s)
        s = re.sub(r"\ball (\d+) swings\b", lambda m: "all %d swings" % (int(m.group(1)) * TIMES), s)
        s = s.replace("on the one swing we could measure", "on all %d swings" % TIMES).replace("the one swing", "all %d swings" % TIMES)
        return s
    today = r.pop("today", None)
    r = walk_strings(r, scale)
    r["today"] = today
    m = r["meta"]
    m["swings"] = n
    m["club"] = clubsWords(CLUBS)
    m["kicker"] = kicker(m["date"], m["club"], n, m.get("sessionOfDay", ""))
    m["runningHead"] = runningHead(m["golfer"], m["dateShort"], m["club"])
    g = r["grid"]
    g["swings"] = list(range(1, n + 1))
    for row in g["rows"]:
        row["marks"] = [row["marks"][src(i)] for i in range(n)]
        row["count"] = gridCount(*marks_count(row["marks"]))
    g["carry"] = [g["carry"][src(i)] + CLUB_CARRY[i // per_club] for i in range(n)]
    g["standfirst"] = gridStandfirst(n, "A short stub" in g["standfirst"])
    if g.get("best"):
        new_best = next(i for i in range(per_club) if src(i) == g["best"] - 1) + 1     # the base's reference swing, first time round
        g["bestText"] = edit(g["bestText"], "Swing %d came" % g["best"], "Swing %d came" % new_best, "sixty bestText")
        g["best"] = new_best
    for w in r["workOns"]:
        f = w.get("figure")
        if f:
            by = {p["swing"]: p for p in f["points"]}
            f["points"] = [dict(by[src(i) + 1], swing=i + 1) for i in range(n) if src(i) + 1 in by]
    tiles = r["ball"]["groups"][0]["tiles"]

    def shift(ts, d):
        out = []
        for t in ts:
            t = dict(t)
            if t["label"] == "Carry" and d:
                t["value"] = str(int(num(t["value"])) + d)
                lo, hi = t["range"].split("–")
                t["range"] = "%d–%d" % (int(num(lo)) + d, int(num(hi)) + d)
            out.append(t)
        return out
    r["ball"]["groups"] = [{"club": cap1(clubWord(c)), "tiles": shift(tiles, d)} for c, d in zip(CLUBS, CLUB_CARRY)]
    r["ball"]["note"] = ballNote(n, True)
    nb = r["numbers"]
    ci, ti = col(nb, "Carry"), col(nb, "Total")
    nb["cols"] = ["Swing", "Club"] + nb["cols"][1:]
    nb["units"] = ["", ""] + nb["units"][1:]
    rows0 = nb["rows"]
    rows = []
    for i in range(n):
        s0, d = rows0[src(i)], CLUB_CARRY[i // per_club]
        cells = list(s0["cells"])
        for c in (ci, ti):
            if num(cells[c]) is not None:
                cells[c] = str(int(num(cells[c])) + d)
        rows.append({"cells": [str(i + 1), clubWord(CLUBS[i // per_club])] + cells[1:], "habitsText": s0["habitsText"]})
    nb["rows"] = rows
    med = nb["typical"][0]["cells"]
    typ = []
    for c, d in zip(CLUBS, CLUB_CARRY):
        cells = list(med)
        for k in (ci, ti):
            if num(cells[k]) is not None:
                cells[k] = str(int(num(cells[k])) + d)
        typ.append({"cells": ["Typical", clubWord(c)] + cells[1:]})
    nb["typical"] = typ
    per = len([w for w in r["workOns"] if work_on_facts(w)[0] < 11.0])
    nb["note"] = numbersNote(False, per, n, True, True)
    if r.get("today"):
        r["today"]["sessions"][-1] = edit(r["today"]["sessions"][-1], swingsN(n0), swingsN(n), "sixty today column")
        for row in r["today"]["rows"]:
            mk = row["marks"][-1]
            mm = re.match(r"^(habit )?(\d+)/(\d+)$", mk["text"])
            if mm:
                mk["text"] = "%s%d/%d" % (mm.group(1) or "", int(mm.group(2)) * TIMES, int(mm.group(3)) * TIMES)
        r["today"]["note"] = scale(r["today"]["note"])
    a = r["about"]
    a["measured"] = sub(r"^%s with an? [^,]+" % re.escape(swingsN(n0)), "%s with %s %s" % (swingsN(n), article(m["club"]), m["club"]),
                        a["measured"], "sixty about.measured")
    # nothing of this session may still count to the base's swings
    left = [p for p in re.findall(r"\b\d+ of %d swings?\b|\ball %d swings\b|\(\d+ of %d\)" % (n0, n0, n0), json.dumps({k: v for k, v in r.items() if k != "today"}, ensure_ascii=False))]
    if left:
        fail("sixty: counts of the base's %d swings left: %s" % (n0, left[:5]))
    return r


def junior_no_photos(base):
    """A junior with no photo approval: no still anywhere, the cover says why (report_words.h photoReason 2), the back
    page's footnote says so (aboutFootnote(false))."""
    r = copy.deepcopy(base)
    r["photos"] = {"included": False, "reason": "%s is under 18 — no parent or guardian approval recorded, so no swing photos" % r["meta"]["first"]}
    for w in r["workOns"]:
        w["stills"] = []
    g = r["grid"]
    g["bestStills"], g["bestLabels"] = [], []
    if r["about"]["footnote"] != aboutFootnote(True):
        fail("junior: the footnote is not aboutFootnote(true): %r" % r["about"]["footnote"])
    r["about"]["footnote"] = aboutFootnote(False)
    return r


LONG_NOTE = ("Really good session today. Your trail knee is the one to own between now and next time: the slow rehearsals "
             "at the start were the best I have seen from you, and the first few swings with a ball kept most of it. Keep the "
             "practice short and often, film every fifth swing from behind, and send me a clip if anything feels odd. We will "
             "check the knee first next time before we touch anything else on the list.")


def longest_names(base):
    """The longest studio, coach, contact and golfer names and a full coach's note: the cover's fitted lines, the folio and
    the running head at their longest. The golfer is renamed everywhere the map names them."""
    r = copy.deepcopy(base)
    b = r["brand"]
    b["studio"] = "The Royal Northfield Golf Academy and Performance Centre"
    b["monogram"] = "RN"
    b["coach"] = "Christopher Wainwright-Buchanan"
    b["coachTitle"] = "Head of Coaching and Player Development"
    b["coachLine"] = DOT.join([b["coach"], b["coachTitle"]])
    b["contact"] = "christopher.wainwright-buchanan@royalnorthfieldgolfacademy.example" + DOT + "+44 (0)1234 567 890"
    b["footLine"] = b["coach"] + ", " + b["studio"]
    b["signoff"] = "Book your next session at royalnorthfieldgolfacademy.example/book or reply to this email and we will find a time that suits you."
    m = r["meta"]
    old_full, old_first = m["golfer"], m["first"]
    new_full, new_first = "Alexandra Montgomery-Fotheringham", "Alexandra"

    def rename(s):
        s = s.replace(old_full, new_full).replace(old_full.replace(" ", "-"), new_full.replace(" ", "-"))
        return re.sub(r"\b%s\b" % re.escape(old_first), new_first, s)
    r = walk_strings(r, rename)
    if r["meta"]["golfer"] != new_full:
        fail("longest-names: the golfer was not renamed")
    r["coachNote"] = LONG_NOTE
    return r


def note_two_swings(base):
    """Two swings: the one-page note (the model's note shape: no grid, order, practice or back matter, no Earlier today)
    with the ball tiles and a two-row numbers table, each tile recomputed from the two rows kept as ballTiles() does
    (session_report_model.cpp: the middle value, lowest–highest; path signed with in-to-out / out-to-in; offline the
    widest either side)."""
    r = copy.deepcopy(base)
    r["kind"] = "note"
    n = 2
    m = r["meta"]
    m["swings"] = n
    m["kicker"] = kicker(m["date"], m["club"], n, m.get("sessionOfDay", ""))
    r["headline"] = headlineNote(n, True)
    r["coachNote"] = ""
    for k in ["strengths", "found", "workOnsBrief", "workOns", "watching", "checks", "held"]:
        r[k] = []
    for k in ["grid", "today", "order", "practice", "about"]:
        r[k] = None
    r["appendix"] = False
    nb = r["numbers"]
    nb["rows"] = [{"cells": row["cells"]} for row in nb["rows"][:n]]
    nb["habitsHead"] = ""
    nb["typical"] = []
    nb["note"] = numbersNote(False, 0, n, False, False)
    vals = lambda head: [num(row["cells"][col(nb, head)]) for row in nb["rows"] if num(row["cells"][col(nb, head)]) is not None]
    spec = {"Carry": ("Carry", 0, False, ""), "Ball speed": ("Ball speed", 0, False, ""), "Club speed": ("Club speed", 0, False, ""),
            "Smash": ("Smash", 2, False, ""), "Launch": ("Launch", 0, False, "°"), "Spin": ("Spin", 0, True, "")}
    tiles = []
    for t in r["ball"]["groups"][0]["tiles"]:
        t = dict(t)
        if t["label"] in spec:
            head, dp, th, deg = spec[t["label"]]
            v = vals(head)
            t["value"] = lmFig(statistics.median(v), dp, th) + deg
            t["range"] = lmFig(min(v), dp, th) + "–" + lmFig(max(v), dp, th)
        elif t["label"] == "Club path":
            v = vals("Path")
            med = statistics.median(v)
            t["value"] = lmFig(abs(med), 0) + "°"
            t["unit"] = "square" if abs(med) < 0.5 else ("in-to-out" if med > 0 else "out-to-in")
            t["range"] = figSigned(min(v), 1) + " to " + figSigned(max(v), 1)
        elif t["label"] == "Offline":
            t["value"] = lmFig(max(abs(x) for x in vals("Offline")), 0)
        else:
            fail("note: no rule for the %r tile" % t["label"])
        tiles.append(t)
    r["ball"]["groups"][0]["tiles"] = tiles
    r["ball"]["note"] = ballNote(n, False)
    return r


# ── the placeholder stills ─────────────────────────────────────────────────────────────────────────

def placeholders():
    """Two small neutral JPEGs (no person): stills/still.jpg at the work-on stills' 2:3, stills/frame.jpg at a Posture
    picture frame's shape. Written only into the fixture folder's own stills/ (and nothing else there is touched)."""
    from PIL import Image, ImageDraw
    os.makedirs(STILLS, exist_ok=True)
    for name, (w, h) in {"still.jpg": (120, 180), "frame.jpg": (80, 196)}.items():
        im = Image.new("RGB", (w, h), (176, 180, 178))
        d = ImageDraw.Draw(im)
        d.rectangle([0, int(h * 0.82), w, h], fill=(150, 156, 150))          # a floor line, nothing else
        d.rectangle([0, 0, w - 1, h - 1], outline=(140, 140, 140))
        path = os.path.join(STILLS, name)
        if os.path.dirname(os.path.abspath(path)) != os.path.abspath(STILLS):
            sys.exit("refusing to write outside %s" % STILLS)
        im.save(path, "JPEG", quality=60)


# ── the cases ──────────────────────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="the session report's QML test data, from the model's maps (see the header)")
    ap.add_argument("--dumps", required=True, help="folder of <tag>.json report maps made by fixtures.sh dump")
    ap.add_argument("--lib", required=True, help="the scratch libraries' folder: <lib>/<tag> is the one a dump was made in (its paths become /lib)")
    ap.add_argument("--pseudonymise", action="store_true", help="rename every golfer by PSEUDONYMS (off by default)")
    args = ap.parse_args()
    lib = os.path.normpath(args.lib)
    raw = {}
    for tag in DUMP_TAGS:
        with open(os.path.join(args.dumps, tag + ".json")) as f:
            raw[tag] = json.load(f)
    dumps = {tag: normalise(m, os.path.join(lib, tag)) for tag, m in raw.items()}
    if args.pseudonymise:
        dumps = {tag: pseudonymise(m) for tag, m in dumps.items()}
    pack = json.load(open(os.path.join(REPO, "src/Resources/diagnostics/core.json")))
    pack["drills"] = json.load(open(os.path.join(REPO, "src/Resources/diagnostics/drills.json")))["drills"]
    nick = base_of(dumps["nick"])
    sixty = sixty_three_clubs(nick)
    posture = with_block(nick, "posture", dumps["nick"])

    def own(tag, *keys):
        """A page case on ONE session: that session's map with its own page blocks (every page from the same session)."""
        r = base_of(dumps[tag])
        for k in keys:
            r = with_block(r, k, dumps[tag])
        return r
    cases = {
        "nick": nick, "jess": base_of(dumps["jess3"]),
        "clean": clean(nick, pack),
        "one-work-on": keep_workons(nick, 1),
        "five-work-ons-longest-text": longest_text(nick, pack),
        "causes-at-caps": causes_at_caps(nick, pack),
        "sixty-swings-three-clubs": sixty,
        "junior-no-photos": junior_no_photos(nick),
        "longest-names-and-note": longest_names(nick),
        "note": note_two_swings(nick),
        "note-one-swing": base_of(dumps["note"]),
        "no-launch-monitor": base_of(dumps["nolm"]),
        "long-session": base_of(dumps["long"]),
        "launch-monitor-appendix": with_block(nick, "lmAppendix", dumps["nick"]),
        "posture": posture,
        "wrist": own("rich", "posture", "wrist"),
        "delivery": own("jess", "posture", "wrist", "delivery"),
        "consistency": with_block(nick, "consistency", dumps["nick"]),
        "consistency-mixed-clubs": own("mixed", "consistency"),
        "tendencies": with_block(nick, "tendencies", dumps["nick"]),
    }
    keys_not_written(cases, dumps)
    placeholders()
    for k, v in cases.items():
        with open(os.path.join(OUT, k + ".json"), "w") as f:
            json.dump(v, f, indent=1, ensure_ascii=False)
            f.write("\n")
    with open(os.path.join(OUT, "fixtures.js"), "w") as f:
        f.write(".pragma library\n\n// GENERATED by tools/session-report-mock/build_fixtures.py from the .json beside it. Do not edit.\n")
        f.write("var names = " + json.dumps(list(cases.keys())) + ";\n")
        f.write("var maps = {\n")
        for k, v in cases.items():
            f.write("  " + json.dumps(k) + ": " + json.dumps(v, ensure_ascii=False) + ",\n")
        f.write("};\n")
    for s in sorted(f[:-5] for f in os.listdir(OUT) if f.endswith(".json") and f[:-5] not in cases):
        print("STALE FIXTURE (no case makes it): %s.json" % s)
    print("wrote %d fixtures to %s" % (len(cases), OUT))


if __name__ == "__main__":
    main()
