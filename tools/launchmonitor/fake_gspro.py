#!/usr/bin/env python3
"""Play a launch monitor: connect over GSPro Open Connect and hit shots at PinPoint.

The sibling of fake_shot.py, for the other connector. That one writes a file the GCQuad
path watches; this one DIALS IN, because a GSPro Open Connect device is a client and
PinPoint is the server it connects to.

    # Settings -> Devices -> Launch monitor: GSPro Connect, port 921, All interfaces
    python3 tools/launchmonitor/fake_gspro.py                 # a shot every 15 s, forever
    python3 tools/launchmonitor/fake_gspro.py --shots 5        # five, then disconnect
    python3 tools/launchmonitor/fake_gspro.py --club driver --interval 5
    python3 tools/launchmonitor/fake_gspro.py --port 9210 --wait 0

⚠ IT WAITS BEFORE THE FIRST SHOT, fifteen seconds by default, and that is deliberate
rather than lazy. The protocol has no handshake, so a device that has connected and said
nothing is anonymous — it shows in the device list as "connecting…" in amber until its
first message names it. That state is real, a device can sit in it for minutes, and it is
worth seeing on purpose. `--wait 0` skips it.

NO DEPENDENCIES, NOT EVEN libgspro's. A plain socket and json, so it runs on any machine
with Python and needs neither the FFI object nor a built checkout of the library. That is
the point: when the connector is not receiving, this has to be the half of the pair you
can trust without building anything.

Values are jittered per club and kept INTERNALLY CONSISTENT, because the connector derives
from them and the app grades on them: ball speed follows club speed through a plausible
smash factor, the spin axis follows face-to-path, side and back spin resolve to the total,
and carry follows ball speed at that club's efficiency, docked for spin and for a start
line well offline. A shot whose numbers contradict each other would exercise arithmetic no
device produces — and would look, in the session board, like a bug in PinPoint.

⛔ IT REFUSES A NON-LOOPBACK ADDRESS ON 921 OR 922 unless told --yes-i-mean-it. Those are
GSPro's own ports, and a machine across the network answering on one is probably somebody's
actual GSPro mid-round. Driving that is not this tool's business.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import math
import random
import socket
import sys
import time

# Per club: club speed, smash, launch angle, total spin, angle of attack, loft, and the
# carry EFFICIENCY in yards per mph of ball speed. A driver launched well carries about
# 1.7 yards per mph; a wedge barely 1.3, being thrown high and spun hard.
#
# ⚠ The efficiency is a per-club figure and not a formula, because a formula got this
# badly wrong first time round — a power law gave a 171 mph drive a 76-yard carry, which
# is the sort of number that makes a reviewer doubt the app rather than the fixture.
CLUBS = {
    "driver": ((108, 116), (1.46, 1.50), (11.0, 14.5), (2100, 2700),  (1.0, 4.0),   (11.5, 14.0), 1.70),
    "3wood":  (( 99, 105), (1.44, 1.48), (11.5, 14.0), (3100, 3800),  (-1.5, 1.0),  (14.0, 16.0), 1.62),
    "7iron":  (( 82,  88), (1.32, 1.38), (16.5, 19.5), (6000, 7200),  (-4.5, -2.5), (26.0, 30.0), 1.47),
    "wedge":  (( 62,  70), (1.20, 1.28), (27.0, 32.0), (8800, 10500), (-6.0, -3.5), (46.0, 52.0), 1.30),
}


def build_shot(name: str, number: int, device: str, rng: random.Random) -> dict:
    cs, sm, la, sp, aoa, loft, eff = CLUBS[name]
    club_speed = rng.uniform(*cs)
    ball_speed = club_speed * rng.uniform(*sm)
    total_spin = rng.uniform(*sp)
    # A shape: the face and the path disagree a little, and everything else follows from
    # that — start line mostly off the face, axis tilt off face-to-path, side spin off the
    # tilt. This is where a fixture earns the right to be graded.
    path      = rng.uniform(-3.5, 3.5)
    face      = path + rng.uniform(-2.5, 2.5)
    hla       = face * 0.85 + path * 0.15
    spin_axis = (face - path) * 3.2
    side_spin = total_spin * math.sin(math.radians(spin_axis))
    back_spin = total_spin * math.cos(math.radians(spin_axis))
    spin_mid  = (sp[0] + sp[1]) / 2.0
    carry     = max(20.0, ball_speed * eff - (total_spin - spin_mid) / 260.0 - abs(hla) * 0.8)

    return {
        "DeviceID": device,
        "Units": "Yards",
        "ShotNumber": number,
        "APIversion": "1",
        "BallData": {
            "Speed": round(ball_speed, 1),
            "SpinAxis": round(spin_axis, 1),
            "TotalSpin": round(total_spin, 0),
            "BackSpin": round(back_spin, 0),
            "SideSpin": round(side_spin, 0),
            "HLA": round(hla, 1),
            "VLA": round(rng.uniform(*la), 1),
            "CarryDistance": round(carry, 1),
        },
        "ClubData": {
            "Speed": round(club_speed, 1),
            "AngleOfAttack": round(rng.uniform(*aoa), 1),
            "FaceToTarget": round(face, 1),
            "Lie": round(rng.uniform(-2.0, 2.0), 1),
            "Loft": round(rng.uniform(*loft), 1),
            "Path": round(path, 1),
            "SpeedAtImpact": round(club_speed * rng.uniform(0.985, 1.0), 1),
            "VerticalFaceImpact": round(rng.uniform(-6.0, 6.0), 1),
            "HorizontalFaceImpact": round(rng.uniform(-8.0, 8.0), 1),
            "ClosureRate": round(rng.uniform(40.0, 130.0), 1),
        },
        "ShotDataOptions": {
            "ContainsBallData": True,
            "ContainsClubData": True,
            "LaunchMonitorIsReady": True,
            "LaunchMonitorBallDetected": True,
            "IsHeartBeat": False,
        },
    }


def heartbeat(device: str) -> dict:
    """The shot message with both flags false — which is all a heartbeat is."""
    return {
        "DeviceID": device, "Units": "Yards", "ShotNumber": 0, "APIversion": "1",
        "ShotDataOptions": {
            "ContainsBallData": False, "ContainsClubData": False,
            "LaunchMonitorIsReady": True, "LaunchMonitorBallDetected": False,
            "IsHeartBeat": True,
        },
    }


def read_codes(sock: socket.socket) -> list[int]:
    """Whatever replies are waiting, by response code.

    ⚠ Framed the way a real client must frame them: there is no delimiter on the wire, so
    a reply may arrive split, and a 200 and a 201 may arrive together in one read. Nothing
    here may assume one read is one message.
    """
    codes: list[int] = []
    sock.settimeout(2.0)
    try:
        buf = sock.recv(4096).decode("utf-8", "replace")
    except (TimeoutError, socket.timeout, OSError):
        return codes
    decoder = json.JSONDecoder()
    buf = buf.lstrip()
    while buf:
        try:
            obj, end = decoder.raw_decode(buf)
        except ValueError:
            break
        if isinstance(obj, dict) and "Code" in obj:
            codes.append(int(obj["Code"]))
        buf = buf[end:].lstrip()
    return codes


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1",
                    help="where PinPoint is listening (default 127.0.0.1)")
    ap.add_argument("--port", type=int, default=921,
                    help="the port set in Settings -> Devices -> Launch monitor")
    ap.add_argument("--interval", type=float, default=15.0,
                    help="seconds between shots (default 15)")
    ap.add_argument("--wait", type=float, default=15.0,
                    help="seconds to sit connected and SILENT first, so the device list "
                         "shows the unidentified state (default 15; 0 to skip)")
    ap.add_argument("--shots", type=int, default=0,
                    help="how many, then disconnect. 0 (the default) means until ctrl-c")
    ap.add_argument("--club", choices=sorted(CLUBS), default=None,
                    help="always this club (default: a different one each shot)")
    ap.add_argument("--device", default="PinPoint Bench Rig",
                    help="the DeviceID this claims to be — what the device list shows")
    ap.add_argument("--seed", type=int, default=None,
                    help="fix the jitter, for a repeatable session")
    ap.add_argument("--yes-i-mean-it", dest="force", action="store_true",
                    help="⛔ allow a NON-LOOPBACK address on 921/922 — see the note above")
    args = ap.parse_args(argv)

    # ⛔ The one combination that probably is not PinPoint.
    try:
        loopback = ipaddress.ip_address(socket.gethostbyname(args.host)).is_loopback
    except (OSError, ValueError):
        loopback = False
    if args.port in (921, 922) and not loopback and not args.force:
        print(f"⛔ {args.host}:{args.port} is not loopback, and {args.port} is GSPro's own "
              f"port — a machine answering there is probably somebody's real GSPro "
              f"mid-round.\n   Aim at 127.0.0.1, use a different port, or pass "
              f"--yes-i-mean-it if you are certain.", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    try:
        sock = socket.create_connection((args.host, args.port), timeout=5.0)
    except OSError as exc:
        print(f"⛔ cannot connect to {args.host}:{args.port}: {exc}\n"
              f"   Is the launch monitor switched ON in Settings -> Devices, and is the "
              f"port the one set there?", file=sys.stderr)
        return 1

    print(f"connected to {args.host}:{args.port} as {args.device!r}")
    try:
        if args.wait > 0:
            print(f"silent for {args.wait:.0f}s — the device list should show "
                  f"\"connecting…\" until the first shot names it")
            time.sleep(args.wait)

        n = 0
        while args.shots == 0 or n < args.shots:
            n += 1
            club = args.club or rng.choice(sorted(CLUBS))
            shot = build_shot(club, n, args.device, rng)
            sock.sendall(json.dumps(shot).encode())
            b = shot["BallData"]
            codes = read_codes(sock)
            print(f"shot {n:3d}  {club:6s}  ball {b['Speed']:5.1f} mph  "
                  f"launch {b['VLA']:4.1f}°  spin {b['TotalSpin']:6.0f}  "
                  f"axis {b['SpinAxis']:+5.1f}°  carry {b['CarryDistance']:5.1f} yd"
                  f"  ->  {', '.join(str(c) for c in codes) or 'no reply'}")
            if args.shots == 0 or n < args.shots:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print()
    finally:
        sock.close()
        print("disconnected — the device list should drop the row within a couple of seconds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
