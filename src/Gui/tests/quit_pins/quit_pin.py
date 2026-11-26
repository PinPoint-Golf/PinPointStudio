#!/usr/bin/env python3
# Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
#
# This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later
# version. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
"""THE QUIT PINS (SE-1b): quit the real app at one point of its start-up, or while one of its threads is busy, and
check that it exits clean. One ctest per pin (quit_pin.<name>, src/Gui/tests/CMakeLists.txt); each pin runs twice.

    quit_pin.py --app <PinPointStudio binary> --repo <repo root> --work <scratch dir> --pin <name>
                [--runs 2] [--fixture <fixture dir>] [--list]

THE ROUTE. The app is started only through tools/probe/run_headless.sh (AG-15): PINPOINT_HEADLESS_PROBE=1, offscreen, a
scratch home, settings file and library under --work. The probe src/Gui/tests/probes/quit_pin.qml quits it at the
pin's moment and logs "QUIT-PIN: quit requested". For a busy pin, the driver watches the app log for the pin's busy
marker and then writes the probe's trigger file, so the quit lands while that thread is working.

A RUN IS CLEAN when all of these hold (the oracle; each is independent of what the code says it does):
  1. the app's own exit code is 0: not a signal (128+n), not run_headless's timeout (124) or failed check (3);
  2. the process is gone within LIMIT_S seconds of the quit request (GAP-20: a quit that waits with no window);
  3. no line after the quit request names a teardown fault (FAULTS below: a thread destroyed while running, a timer or
     a notifier touched from the wrong thread, an abort, a QML error; GAP-21);
  3a. no line after the quit request shows work running on after the application object has gone (SHUTDOWN below,
     the check "after-shutdown": Qt's "Please instantiate the QApplication object first", or a model file not found,
     because its path is read from the application that has gone; GAP-21, seen for a re-analysis, #294);
  4. the pin's busy marker was in the log BEFORE the quit and its "done" marker was not (else the pin proved nothing
     and FAILS as vacuous), and its expected after-quit lines are there;
  5. the memory guard (AG-16) did not have to kill it.
The quit-to-exit time is printed for every run.

EXPECTED FAILURES. A pin whose failure is an observed, filed fault carries its issue number (KNOWN below). It still
runs. If it fails the way the issue says (the same check), the test reports Not Run (exit 77) with the issue number,
never Passed; if it now passes, or fails some other way, the test FAILS, so a fix or a new fault is seen.

Exit: 0 every run clean; 1 a run not clean; 77 Not Run (no fixture for a busy pin, or a known, filed failure).
"""
import argparse, os, re, shutil, signal, subprocess, sys, threading, time

LIMIT_S = 5.0          # quit request -> process gone (proposed, for the Product Owner: GU-10's "a few seconds")
MEM_CAP_MB = 6144      # the agents' render guard (AG-16): kill above a 6 GB footprint
WARM_TIMEOUT_S = 400   # a pose pin's run: a cold CoreML compile is about 97 s, the Debug app slower

# A fault after the quit request (GAP-21's kind, and crashes). Matched case-sensitively, on lines after the request.
FAULTS = [
    r"QThread: Destroyed while thread",
    r"Timers cannot be stopped from another thread", r"QObject::killTimer", r"QObject::startTimer",
    r"QSocketNotifier: ", r"Cannot send events to objects owned by a different thread",
    r"QObject: Cannot create children for a parent that is in a different thread",
    r"terminate called", r"libc\+\+abi", r"mutex lock failed", r"pure virtual", r"Assertion failed", r"ASSERT",
    r"QMutex: destroying locked", r"QWaitCondition", r"QThreadStorage", r"Segmentation fault", r"Abort trap",
    r"\[HeadlessProbe\] REFUSED", r"TypeError", r"ReferenceError", r"QML .*: Binding loop",
    r"was deleted while", r"use after free", r"QCoreApplication::postEvent: Unexpected null receiver",
]

# Work running on after the application object has gone (GAP-21's kind; #294 observed it in a re-analysis). General,
# for any stage or thread: any Qt function's "Please instantiate the Q...Application object first" warning, and any
# model's "... model not found:" error (MoveNet, MediaPipe's two, the segmenter, ViTPose: each builds its path from
# the application's folder, which reads as empty once the application has gone). The colon leaves out ViTPose's
# "fp16 model not found at ...", a warning that falls back to the fp32 model, which a pose load logs at any time.
# Matched on lines after the quit request, as FAULTS; reported as its own check, "after-shutdown", so KNOWN can admit
# it for #294 without admitting the other faults.
SHUTDOWN = [
    r"Please instantiate the Q[A-Za-z]*Application object first",
    r"[Mm]odel not found:",
]

FIX_REANALYSE = "Nick-Henn/2026-10-13_Nick-Henn_Wrist_01/swing_0001"   # make_fixture.sh's one swing

# name -> the pin. at: the probe's --quit-pin-at ("trigger" = the driver's file). env: extra PINPOINT_* switches.
# busy: a marker that must be in the log before the quit (for "trigger", the quit is sent `delay` s after it).
# not_before: a marker that must NOT be in the log before the quit (the work was still in flight).
# after: markers that must follow the quit. ran_on: a marker of the busy work; a line of it after the quit means the
# quit waited for that work to run on with no window (GAP-20), which fails the "ran-on" check. fixture: needs make_fixture.sh's library. pose_home: share one scratch
# home across this pin's runs (True), or across every "shared" pin's, so the CoreML compile cache a finished load
# leaves is reused (a cold compile is ~97 s). The warm-up pin keeps its own: its load is always abandoned part-way.
PINS = {
    # ---- start-up (APP-S1 -> S2), headless, an empty scratch library ----
    "startup":      dict(at="startup", busy=r"QUIT-PIN: quit requested \(startup",
                         what="a quit that arrives while main() is still building (queued; delivered as exec() starts)"),
    "ready":        dict(at="ms:1", busy=r"QUIT-PIN: quit requested \(ms:1 ",
                         what="the event loop's first turn: the window is up, no timer has fired"),
    "ready-close":  dict(at="ms:500", route="close", busy=r"QUIT-PIN: quit requested \(ms:500 ",
                         what="the window's close with no session running (APP-T9), 0.5 s after the window is up"),
    "work-ons":     dict(at="ms:3200", busy=r"QUIT-PIN: quit requested \(ms:3200 ",
                         what="just after the work-ons list repoints (its 3 s timer: the catch-up's scan)"),
    "report-list":  dict(at="ms:3700", busy=r"QUIT-PIN: quit requested \(ms:3700 ",
                         what="just after the report list repoints (its 3.5 s timer)"),
    "housekeeping": dict(at="ms:61000", busy=r"QUIT-PIN: quit requested \(ms:61000 ",
                         what="just after housekeeping's first pass (its 60 s timer)"),
    # ---- the pose warm-up (APP-S2a): PINPOINT_PROBE_WARMUP=1 runs it under the headless switch ----
    "warm-up":      dict(at="trigger", env=["PINPOINT_PROBE_WARMUP=1"], delay=1.0, pose_home=True,
                         busy=r"\[PoseRunner\] warm-up: building the pose session",
                         not_before=r"\[PoseRunner\] warm-up: .* session ready in",
                         after=[r"warm-up still building at quit — abandoned, not waited for"],
                         what="during the warm-up: the pose session building on the inference thread (S2, S5)"),
    "after-warm-up": dict(at="trigger", env=["PINPOINT_PROBE_WARMUP=1"], delay=0.5, pose_home="shared",
                          busy=r"\[PoseRunner\] warm-up: .* session ready in",
                          not_after=[r"abandoned, not waited for"],
                          what="after the warm-up: the inference thread idle, stopped and joined at exit (S2)"),
    # ---- a busy thread, with make_fixture.sh's one-swing library ----
    "athlete-on-screen": dict(at="trigger", fixture=True, current_athlete=True, delay=0.0,
                         busy=r"\[CatchUp\]|\[AthleteDiagnostics\]",
                         what="the current golfer's latest session on screen, quit as the catch-up makes its first "
                              "decision (P1 is not shown busy: the one-swing fixture gives it almost nothing to do)"),
    "re-analysis":  dict(at="trigger", fixture=True, pose_home="shared", delay=2.0,
                         env=["PINPOINT_PROBE_REANALYSE=@LIB@/" + FIX_REANALYSE],
                         busy=r"\[Probe\] re-analysing", not_before=r"\[Reanalysis\] (re-analysed|failed)",
                         ran_on=r"\[WristAnalysis\]",
                         what="a re-analysis running on Qt's global pool (J2, H2); it reuses the swing's recorded pose"),
    "re-analysis-in-warm-up": dict(at="trigger", fixture=True, pose_home="shared", delay=0.5,
                         env=["PINPOINT_PROBE_WARMUP=1", "PINPOINT_PROBE_REANALYSE=@LIB@/" + FIX_REANALYSE],
                         busy=r"\[Probe\] re-analysing", busy_also=r"\[PoseRunner\] warm-up: building",
                         not_before=r"\[PoseRunner\] warm-up: .* session ready in|\[Reanalysis\] (re-analysed|failed)",
                         ran_on=r"\[WristAnalysis\]",
                         what="a re-analysis running while the warm-up builds on the inference thread (J2 and S2, S5)"),
}
# pin -> (issue number, the checks it fails as the issue says): only an observed, filed fault. Every run must fail,
# only on those checks (#294's "after-shutdown": the re-analysis ran on after the application had gone, in every run
# of both pins; #294's comment of 26 Nov), and a "slow" run must still be gone within KNOWN_SLOW_CEILING_S; anything else FAILS. The fix
# for #294 updates the pin with it (a re-analysis finished and written at quit would show as "ran-on").
KNOWN = {
    "re-analysis":            (294, {"ran-on", "after-shutdown"}),
    "re-analysis-in-warm-up": (294, {"ran-on", "slow", "after-shutdown"}),
}
KNOWN_SLOW_CEILING_S = 30.0
FIX_ATHLETE_ID = "2c4b2cf0-e5a4-406f-88d0-5b3420257402"   # make_fixture.sh's golfer (its athlete.json)


def now():
    return time.monotonic()


class Run:
    def __init__(self, args, pin, n):
        self.args, self.pin, self.n = args, pin, n
        p = PINS[pin]
        base = os.path.join(args.work, pin)
        self.dir = os.path.join(base, "run%d" % n)
        shutil.rmtree(self.dir, ignore_errors=True)
        os.makedirs(self.dir)
        ph = p.get("pose_home")
        self.home = (os.path.join(args.work, "pose-home") if ph == "shared" else
                     os.path.join(base, "pose-home") if ph else os.path.join(self.dir, "home"))
        os.makedirs(self.home, exist_ok=True)
        self.lib = os.path.join(self.dir, "lib")
        if p.get("fixture"):
            shutil.copytree(os.path.join(args.fixture, "lib"), self.lib)
        else:
            os.makedirs(self.lib)
        cfg = os.path.join(self.dir, "cfg", "PinPointStudio")
        os.makedirs(cfg)
        self.ini = os.path.join(cfg, "PinPointStudio.ini")
        with open(self.ini, "w") as f:
            if p.get("current_athlete"):
                f.write("[General]\ncurrentAthleteUuid=%s\n\n" % FIX_ATHLETE_ID)   # SET-02: the golfer on screen
            f.write("[%%General]\nathleteLibraryPath=%s\ncheckForUpdates=false\n" % self.lib)
        self.trigger = os.path.join(self.dir, "trigger")
        os.makedirs(self.trigger)
        self.log = os.path.join(self.dir, "app.log")
        self.lines = []          # (t, line)
        self.app_pid = None
        self.t_quit = None
        self.t_gone = None
        self.peak_mb = 0
        self.guard_killed = False
        self.trigger_sent = False

    def launch(self):
        p = PINS[self.pin]
        at = p["at"]
        if at == "trigger":
            at = "trigger:" + self.trigger
        cmd = [os.path.join(self.args.repo, "tools", "probe", "run_headless.sh"), "--app", self.args.app,
               "--home", self.home, "--settings", self.ini,
               "--probe", os.path.join(self.args.repo, "src", "Gui", "tests", "probes", "quit_pin.qml"),
               "--timeout", str(WARM_TIMEOUT_S if p.get("pose_home") else 150), "--log", self.log]
        for e in p.get("env", []):
            cmd += ["--env", e.replace("@LIB@", self.lib)]
        cmd += ["--", "--quit-pin-at", at, "--quit-pin-route", p.get("route", "quit")]
        self.t0 = now()
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                     errors="replace", start_new_session=True)
        threading.Thread(target=self.guard, daemon=True).start()
        threading.Thread(target=self.watch_gone, daemon=True).start()
        busy, also = re.compile(p["busy"]), re.compile(p["busy_also"]) if p.get("busy_also") else None
        seen_busy, seen_also = False, also is None
        for raw in self.proc.stderr:
            line = raw.rstrip("\n")
            t = now()
            self.lines.append((t, line))
            m = re.search(r"run_headless: checked: pid (\d+) ", line)
            if m:
                self.app_pid = int(m.group(1))
            if self.t_quit is None and "QUIT-PIN: quit requested" in line:
                self.t_quit = t
                self.do_plant()
            if p["at"] == "trigger" and not self.trigger_sent:
                seen_busy = seen_busy or bool(busy.search(line))
                seen_also = seen_also or bool(also.search(line))
                if seen_busy and seen_also:
                    self.trigger_sent = True
                    threading.Timer(p.get("delay", 0.0), self.send_trigger).start()
        self.rc = self.proc.wait()
        if self.t_gone is None:
            self.t_gone = now()

    def do_plant(self):
        """A test of the oracle, never of the app: break this run on purpose at the quit request."""
        plant = getattr(self, "plant", None)
        if plant == "abort" and self.app_pid:
            os.kill(self.app_pid, signal.SIGABRT)                        # must fail "exit"
        elif plant == "hang" and self.app_pid:
            os.kill(self.app_pid, signal.SIGSTOP)                        # must fail "slow"
            threading.Timer(LIMIT_S + 2.0, lambda: os.kill(self.app_pid, signal.SIGCONT)).start()
        elif plant == "fault":
            self.lines.append((now() + 0.001, "QThread: Destroyed while thread is still running (planted)"))
        elif plant in ("shutdown-app", "shutdown-model", "shutdown-near"):
            t = now() + 0.001
            planted = {"shutdown-app": ["QCoreApplication::applicationFilePath: Please instantiate the QApplication "
                                        "object first (planted)"],
                       "shutdown-model": ['[MoveNet] Model not found: "/../Resources/models/planted.onnx"']}
            if plant == "shutdown-near":
                # Near misses, which must NOT fail: both lines just before the quit, and the fp16 fallback after it.
                i = max(k for k, (_, l) in enumerate(self.lines) if "QUIT-PIN: quit requested" in l)
                tq = self.lines[i][0]
                self.lines[i:i] = [(tq - 0.002, "QCoreApplication::applicationDirPath: Please instantiate the "
                                                "QApplication object first (planted, before the quit)"),
                                   (tq - 0.001, '[Segmenter] Model not found: "/planted-before-the-quit.onnx"')]
                planted = {plant: ['[ViTPose -B ] fp16 model not found at "/planted.fp16.onnx" , falling back']}
            for l in planted[plant]:
                self.lines.append((t, l))
        elif plant == "nomarker":
            self.lines[:] = [(t, l) for t, l in self.lines if not re.search(PINS[self.pin]["busy"], l)]   # "vacuous"

    def send_trigger(self):
        with open(os.path.join(self.trigger, "quit"), "w") as f:
            f.write("quit\n")

    def watch_gone(self):
        while self.proc.poll() is None:
            if self.app_pid is not None:
                try:
                    os.kill(self.app_pid, 0)
                except ProcessLookupError:
                    self.t_gone = now()
                    return
                except PermissionError:
                    pass
            time.sleep(0.02)

    def footprint_mb(self, pid):
        try:
            out = subprocess.run(["top", "-l", "1", "-pid", str(pid), "-stats", "mem"], capture_output=True,
                                 text=True, timeout=10).stdout.strip().splitlines()
            v = out[-1].strip().rstrip("+-") if out else ""
            mult = {"K": 1 / 1024, "M": 1, "G": 1024, "B": 1 / 1048576}
            return float(v[:-1]) * mult.get(v[-1], 1) if v and v[-1] in mult else 0.0
        except Exception:
            return 0.0

    def guard(self):
        while self.proc.poll() is None:
            if self.app_pid:
                mb = self.footprint_mb(self.app_pid)
                self.peak_mb = max(self.peak_mb, mb)
                if mb > MEM_CAP_MB:
                    self.guard_killed = True
                    print("GUARD: KILLED pid %d at %.0f MB (cap %d MB)" % (self.app_pid, mb, MEM_CAP_MB))
                    try:
                        os.kill(self.app_pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    return
            time.sleep(2.0)

    def kill(self):
        for pid in (self.app_pid,):
            if pid:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except Exception:
            pass

    def verdict(self):
        """[(check, message)] of what failed; empty = clean."""
        p, bad = PINS[self.pin], []
        if self.guard_killed:
            bad.append(("guard", "the memory guard killed it (peak %.0f MB)" % self.peak_mb))
        if self.rc != 0:
            why = {124: "run_headless's timeout stopped it", 3: "run_headless's check failed",
                   2: "run_headless refused to launch"}.get(self.rc, "a signal" if self.rc > 128 else "")
            bad.append(("exit", "exit code %d %s" % (self.rc, why)))
        if self.t_quit is None:
            bad.append(("vacuous", "no 'QUIT-PIN: quit requested' line: the probe never quit"))
            return bad
        before = [l for t, l in self.lines if t <= self.t_quit]
        after = [l for t, l in self.lines if t > self.t_quit]
        if not any(re.search(p["busy"], l) for l in before):
            bad.append(("vacuous", "the busy marker /%s/ was not in the log before the quit" % p["busy"]))
        if p.get("busy_also") and not any(re.search(p["busy_also"], l) for l in before):
            bad.append(("vacuous", "the busy marker /%s/ was not in the log before the quit" % p["busy_also"]))
        if p.get("not_before"):
            hit = [l for l in before if re.search(p["not_before"], l)]
            if hit:
                bad.append(("vacuous", "the work had finished before the quit: %s" % hit[0][-160:]))
        for a in p.get("after", []):
            if not any(re.search(a, l) for l in after):
                bad.append(("after", "no /%s/ after the quit" % a))
        for a in p.get("not_after", []):
            hit = [l for l in after if re.search(a, l)]
            if hit:
                bad.append(("after", "/%s/ after the quit: %s" % (a, hit[0][-160:])))
        if p.get("ran_on"):
            ran = [l for l in after if re.search(p["ran_on"], l)]
            if ran:
                bad.append(("ran-on", "the busy work ran on after the quit, with no window: %d line(s), the last: %s"
                            % (len(ran), ran[-1][-140:])))
        faults = [l for l in after if any(re.search(f, l) for f in FAULTS)]
        if faults:
            bad.append(("fault", "teardown fault lines after the quit:\n      " + "\n      ".join(faults[:8])))
        gone = [l for l in after if any(re.search(f, l) for f in SHUTDOWN)]
        if gone:
            bad.append(("after-shutdown", "work ran on after the application had gone: %d line(s):\n      %s"
                        % (len(gone), "\n      ".join(gone[:8]))))
        dt = (self.t_gone or now()) - self.t_quit
        if dt > LIMIT_S:
            bad.append(("slow", "the process took %.1f s to go after the quit request (limit %.0f s)" % (dt, LIMIT_S)))
        return bad


def selftest(a):
    """quit_pins_selftest: the oracle catches each planted break of a clean pin ("ready"), on the right check, and
    passes the same pin unplanted. Proves the checks find faults rather than record today's behaviour (BT-7).
    "after-shutdown" gets two plants after the quit (Qt's warning from another of its functions than the one #294 saw,
    and another model's not-found line than the segmenter's, so the patterns are shown general) and one near miss
    that must still pass: both lines just before the quit, and ViTPose's fp16 fallback warning after it."""
    me = [sys.executable, "-I", os.path.abspath(__file__), "--app", a.app, "--repo", a.repo, "--runs", "1",
          "--pin", "ready"]
    want = {None: (0, "PASS: ready"), "abort": (1, "[exit]"), "hang": (1, "[slow]"),
            "nomarker": (1, "[vacuous]"), "fault": (1, "[fault]"),
            "shutdown-app": (1, "[after-shutdown]"), "shutdown-model": (1, "[after-shutdown]"),
            "shutdown-near": (0, "PASS: ready")}
    bad = 0
    for plant, (rc, text) in want.items():
        cmd = me + ["--work", os.path.join(a.work, "selftest-" + (plant or "clean"))] + (["--_plant", plant] if plant else [])
        r = subprocess.run(cmd, capture_output=True, text=True)
        ok = r.returncode == rc and text in r.stdout
        bad += not ok
        print("%s plant %-14s: exit %d (want %d), %s %s" % ("ok  " if ok else "FAIL", plant or "none", r.returncode, rc,
                                                        "says" if text in r.stdout else "does NOT say", text))
        if not ok:
            print("    " + r.stdout.replace("\n", "\n    "))
    print("PASS: the oracle caught every plant" if not bad else "FAIL: %d plant(s) not caught as planned" % bad)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app"); ap.add_argument("--repo"); ap.add_argument("--work"); ap.add_argument("--pin")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--fixture", default="/mnt/swingdata/scratch/quit-pins-fixture")
    ap.add_argument("--list", action="store_true")
    # The oracle's own proof (quit_pins_selftest): each plant must turn a clean pin into a FAIL on the named check.
    ap.add_argument("--_plant", choices=["abort", "hang", "nomarker", "fault", "shutdown-app", "shutdown-model",
                                         "shutdown-near"], default=None)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest(a)
    if a.list:
        for k, v in PINS.items():
            print("%-24s %s" % (k, v["what"]))
        return 0
    if a.pin not in PINS:
        print("FAIL: unknown pin '%s' (--list)" % a.pin); return 1
    p = PINS[a.pin]
    if a.app == "OFF":
        print("NOT RUN: the tests tree opted out of the app (PP_APP_BINARY=OFF): the quit is NOT checked")
        return 77
    if not a.app or not os.path.exists(a.app):
        print("FAIL: no app binary at '%s'" % a.app); return 1
    if p.get("fixture") and not os.path.isfile(os.path.join(a.fixture, "lib", FIX_REANALYSE, "swing.ppsw")):
        print("NOT RUN: no fixture library at %s/lib (zsh src/Gui/tests/quit_pins/make_fixture.sh builds it)"
              % a.fixture)
        return 77
    print("PIN %s: %s" % (a.pin, p["what"]))
    results = []
    for n in range(1, a.runs + 1):
        r = Run(a, a.pin, n)
        r.plant = a._plant
        try:
            r.launch()
        except BaseException:
            r.kill(); raise
        bad = r.verdict()
        dt = ((r.t_gone or now()) - r.t_quit) if r.t_quit else float("nan")
        print("  run %d: exit %s, quit -> gone %.2f s, peak %.0f MB, %s" %
              (n, r.rc, dt, r.peak_mb, "CLEAN" if not bad else "NOT CLEAN"))
        for check, msg in bad:
            print("    [%s] %s" % (check, msg))
        print("    log: %s" % r.log)
        results.append((bad, dt))
    failed = [c for bad, _ in results for c, _ in bad]
    if not failed:
        print("PASS: %s exits clean, %d runs of %d" % (a.pin, a.runs, a.runs))
        if a.pin in KNOWN:
            print("FAIL: UNEXPECTED PASS: issue #%d says this pin fails; close it and take the pin off KNOWN"
                  % KNOWN[a.pin][0])
            return 1
        return 0
    if (a.pin in KNOWN and set(failed) <= KNOWN[a.pin][1] and all(bad for bad, _ in results)
            and all(dt <= KNOWN_SLOW_CEILING_S for _, dt in results)):
        print("NOT RUN: a known, filed fault (#%d, checks %s): the pin fails as the issue says"
              % (KNOWN[a.pin][0], ", ".join(sorted(set(failed)))))
        return 77
    print("FAIL: %s did not exit clean (checks: %s)" % (a.pin, ", ".join(sorted(set(failed)))))
    return 1


if __name__ == "__main__":
    sys.exit(main())
