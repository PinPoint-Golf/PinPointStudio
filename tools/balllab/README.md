# balllab — Ball Detection v2 corpus evidence & (future) acceptance harness

Companion tooling for [`docs/design/ball_detection_v2.md`](../../docs/design/ball_detection_v2.md).
Runs offline against the recorded swing corpus (`/mnt/swingdata/<athlete>/`), python + OpenCV
(`/home/markl/venv/pinpoint/bin/python3`).

| Script | What it shows (design-doc section) |
|---|---|
| `corpus_separation.py` | §2 evidence: scale-matched DoG present/absent separation at the self-located ball spot, all swings. 43/44 clean on the 2026-06/07 corpus. |
| `launch_trace.py` | §2/§4.5 evidence: at-spot response through the whole swing — address stability and the 2-frame launch collapse. Edit `picks` to trace other swings. |

Planned (V0 of the implementation plan, §11): a full state-machine replay of the v2 algorithm
(SEARCH → CANDIDATE → LOCKED → VANISHED) over every corpus swing, enforcing the §9.1 acceptance
gates. That script becomes the parity reference for the C++ core (`src/Pose/ball_temporal.h`),
same discipline as the shaft-tracker port.
