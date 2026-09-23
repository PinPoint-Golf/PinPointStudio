"""pp_swingdoc — read a PinPoint swing document from Python, whichever format it is in.

Since the Phase 2 storage switch (docs/implementation/swing_storage_impl.md) a swing directory
holds swing.ppsw; a JSON-era one still holds swing.json. Every tool under tools/ reads through
here instead of json.load(open(".../swing.json")), so it works on both and on a library that is
part-converted.

    import sys, os
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))  # tools/
    from pp_swingdoc import load_swing, has_swing, find_swing_dirs

The reader is libppswing's pure-Python one (python/ppswing), found in the sibling checkout
../libppswing beside this repository — the same sibling rule the C++ build uses — or on
sys.path if installed. It needs Python 3.14 (stdlib compression.zstd) or the `zstandard` package.

load_swing() returns what json.load() of the swing.json would have: plain dicts, lists and
numbers. The one visible difference (libppswing design §7): a real-valued array that held a
whole number such as 37 reads back from swing.ppsw as 37.0.
"""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SIBLING = os.path.normpath(os.path.join(_HERE, "..", "..", "libppswing", "python"))
if os.path.isdir(os.path.join(_SIBLING, "ppswing")) and _SIBLING not in sys.path:
    sys.path.insert(0, _SIBLING)

try:
    import ppswing as _ppswing
except ImportError as e:  # pragma: no cover - environment problem, said plainly
    raise ImportError(
        "pp_swingdoc needs libppswing's Python reader: clone github.com/PinPoint-Golf/libppswing "
        f"beside this repository (looked in {_SIBLING}) or pip install its python/ directory"
    ) from e

DOC_NAMES = ("swing.ppsw", "swing.json")


def _swing_dir(path):
    """The directory a path names: a swing dir itself, or the dir of a swing.json/.ppsw path."""
    path = os.fspath(path)
    if os.path.isdir(path):
        return path
    base = os.path.basename(path)
    if base in DOC_NAMES:
        return os.path.dirname(path)
    return None


def document_path(path):
    """The document file for a swing dir (or a swing.json / swing.ppsw path), preferring
    swing.ppsw; None when there is none. A '<dir>/swing.json' path whose swing has been converted
    resolves to the swing.ppsw beside it, so older call sites that build that path keep working."""
    d = _swing_dir(path)
    if d is None:
        return os.fspath(path) if os.path.exists(path) else None
    for name in DOC_NAMES:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def has_swing(path):
    """True when the swing dir (or document path) holds a document in either format."""
    return document_path(path) is not None


def load_swing(path):
    """The swing document as json.load would give it. `path` is a swing dir, a swing.ppsw, a
    swing.json, or a '<dir>/swing.json' path whose swing is now swing.ppsw. Any other file path
    (result.json, a pose cache) is read as the format its name says."""
    p = document_path(path)
    if p is None:
        raise FileNotFoundError(f"no swing document at {path}")
    return _ppswing.load_swing(p)


def find_swing_dirs(root):
    """Every directory under root holding a swing document, sorted — the replacement for
    root.rglob('swing.json'). Dot-directories (.pinpoint-trash) are skipped."""
    out = []
    for dirpath, dirnames, filenames in os.walk(os.fspath(root)):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        if any(n in filenames for n in DOC_NAMES):
            out.append(dirpath)
    return sorted(out)
