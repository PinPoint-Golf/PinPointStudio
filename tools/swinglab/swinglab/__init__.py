# SwingLab — offline analysis lab for PinPointStudio (swinglab_impl.md).
# Shared helpers: paths, json io, swing-dir model.

import json
import os
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


def default_binary():
    """Locate swinglab_run: $SWINGLAB_BIN, else the standard build dir."""
    env = os.environ.get("SWINGLAB_BIN")
    if env:
        return Path(env)
    return REPO_ROOT / "build" / "Desktop_Qt_6_11_0-Debug" / "swinglab_run"


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save_json(path, obj):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)


def git_sha():
    try:
        return subprocess.run(["git", "-C", str(REPO_ROOT), "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
    except Exception:
        return "unknown"


class Swing:
    """A recorded (or synthetic) swing dir."""

    def __init__(self, path):
        self.path = Path(path)
        self.doc = load_json(self.path / "swing.json")

    @property
    def name(self):
        return self.path.name

    def video_streams(self):
        return [s for s in self.doc.get("streams", []) if s.get("kind") == "video"]

    def face_on(self, needle="Face"):
        # Recorded perspective (stream "setup", FaceOn == 2) wins when present;
        # legacy swings fall back to the alias substring guess.
        for s in self.video_streams():
            if s.get("setup", {}).get("perspective") == 2:
                return s
        for s in self.video_streams():
            if needle.lower() in (s.get("alias", "") + s.get("file", "")).lower():
                return s
        return self.video_streams()[0] if self.video_streams() else None

    def capture(self):
        """Top-level capture block (session context + host provenance); {} on legacy swings."""
        return self.doc.get("capture", {})

    def bindings(self):
        return self.doc.get("analysis", {}).get("bindings", [])

    def calibrated(self):
        """All-bindings calibration verdict: True/False when recorded, None when
           unknown (legacy swing or no bindings carrying the field)."""
        known = [b["calibrated"] for b in self.bindings() if "calibrated" in b]
        return all(known) if known else None

    def calib_age_sec(self):
        """Worst (max) calibration age across bindings; None when not recorded."""
        ages = [b["calibAgeSec"] for b in self.bindings()
                if b.get("calibAgeSec", -1) >= 0]
        return max(ages) if ages else None

    def impact_from_swingjson(self):
        """Impact the runner can resolve from swing.json ALONE (no truth.json):
           recorded capture.impactUs, else a recorded Impact phase (enum 5).
           None ⇒ swing.json carries no impact (an IMU-less capture before markup)."""
        iu = self.capture().get("impactUs")
        if isinstance(iu, (int, float)) and iu > 0:
            return int(iu)
        for p in self.doc.get("analysis", {}).get("phases", []):
            if p.get("phase") == 5:
                return int(p["t_us"])
        return None

    def impact_from_truth(self):
        """Impact from the markup ground-truth label: the P7 (impact) event, or the
           legacy "impact" event — t0_us + seconds. This is how an IMU-less swing,
           which has no recorded impact, gets a usable impact once it is marked up
           (score.py maps p7 → Impact phase)."""
        ev = (self.truth() or {}).get("events", {}) or {}
        t0 = ev.get("t0_us")
        if t0 is None:
            return None
        for key in ("p7_s", "impact_s"):
            s = ev.get(key)
            if s is not None:
                return int(t0) + int(round(float(s) * 1e6))
        return None

    def impact_us(self):
        """Best available impact: swing.json first, then the markup label."""
        return self.impact_from_swingjson() or self.impact_from_truth()

    def truth(self):
        t = self.path / "truth.json"
        return load_json(t) if t.exists() else None

    def truth_meta(self):
        """Capture conditions the markup tool records under truth.json "meta"
        (lighting / shaft / club); {} when absent. For corpus filtering and
        reporting — not scored."""
        return (self.truth() or {}).get("meta", {})


class RunResult:
    """One swinglab_run output dir."""

    def __init__(self, path):
        self.path = Path(path)
        self.meta = load_json(self.path / "runmeta.json")
        rj = self.path / "result.json"
        self.result = load_json(rj) if rj.exists() else {}

    @property
    def analysis(self):
        return self.result.get("analysis", {})

    @property
    def club(self):
        return self.analysis.get("club", {})

    def club_samples(self):
        return self.club.get("samples", [])

    def trace_lines(self):
        t = self.path / "trace.jsonl"
        if not t.exists():
            return []
        with open(t) as f:
            return [json.loads(line) for line in f if line.strip()]
