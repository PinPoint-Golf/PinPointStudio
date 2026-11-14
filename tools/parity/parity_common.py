"""parity_common - shared paths and the canonical JSON writer for the refactor's parity baseline.

Used by parity_select.py, parity_baseline.py and parity_diff.py (tools/parity/). Standard library
only, and kept to Python 3.7 so the analysis half runs under the cabin PC's swinglab venv as well
as the Mac's python3. Scripts import it after putting their own folder on sys.path, because they
are run with `python3 -I` (which leaves the script's folder off sys.path).

Design: build/refactor/step-G/BRIEF.md section 2; usage: docs/developer/testing_developer_guide.md,
"Parity baseline".
"""
import hashlib
import json
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
# The checkout whose commit a run records; PARITY_REPO when the tools run from a copy (the cabin
# PC can run a copy of tools/parity/ on the share; run_analysis_golfsimpc.cmd sets it).
REPO = os.environ.get("PARITY_REPO") or os.path.normpath(os.path.join(HERE, "..", ".."))

ON_WINDOWS = os.name == "nt"
# The SwingData share: /mnt/swingdata on the Mac is C:\PinPointStudio on the cabin PC.
SHARE = r"C:\PinPointStudio" if ON_WINDOWS else "/mnt/swingdata"
LIBRARY = SHARE                                   # athlete folders live at the share root
CORPUS = os.path.join(SHARE, "corpus")
SCRATCH_ROOT = os.path.join(SHARE, "scratch", "parity")
DEFAULT_SET = os.path.join(HERE, "parity_set.json")

# The three content files a baseline writes under <label>/out/, and what made each.
CONTENT_FILES = ("analysis.json", "diagnostics.json", "athlete.json")


def write_canonical(path, obj):
    """Keys sorted at every depth, floats as Python's shortest round-trip repr, ASCII, a newline
    at the end. Two writes of equal data are byte-identical."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, sort_keys=True, indent=1, ensure_ascii=True, allow_nan=False)
        f.write("\n")
    os.replace(tmp, path)


def read_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_head(repo=REPO):
    """(short sha, number of changed/untracked paths) of the checkout, or (None, None)."""
    try:
        r = subprocess.run(["git", "-C", repo, "rev-parse", "--short", "HEAD"],
                           capture_output=True, text=True, timeout=30)
        st = subprocess.run(["git", "-C", repo, "status", "--porcelain"],
                            capture_output=True, text=True, timeout=60)
        if r.returncode != 0 or st.returncode != 0:
            return None, None
        return r.stdout.strip(), len([l for l in st.stdout.splitlines() if l.strip()])
    except (OSError, subprocess.SubprocessError):
        return None, None


def require_scratch(path):
    """Refuse any destination that is not strictly inside the parity scratch root (SCRATCH_ROOT:
    /mnt/swingdata/scratch/parity, C:\\PinPointStudio\\scratch\\parity), compared as REAL paths with
    symlinks resolved. The baseline writes and deletes there, so it must never reach the live
    library, the corpus, or another scratch tree."""
    real = os.path.normcase(os.path.realpath(path))
    root = os.path.normcase(os.path.realpath(SCRATCH_ROOT))
    if not real.startswith(root + os.sep) or real == root:
        raise SystemExit("parity: refusing to write to %s - not inside %s" % (os.path.realpath(path), root))
    return os.path.realpath(path)


def label_dir(label, scratch_root=None):
    """A label names <scratch_root>/<label>; a path (anything with a separator) is taken as-is."""
    if os.sep in label or "/" in label:
        return os.path.abspath(label)
    return os.path.join(scratch_root or SCRATCH_ROOT, label)
