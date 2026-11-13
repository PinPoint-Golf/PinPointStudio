# calsim — everyday-kit camera calibration, simulated

Evidence for `docs/design/camera_calibration_design.md` §3b.7. Pure numpy + scipy.

    python3 intr2.py 15            # lens: typed vs sheet-waved (corners, + stick plumb line, centre fixed, 20 views, scorecard)
    python3 intr3.py 12            # lens: + the sheet's edges as lines (I2e)
    ONLY=true  python3 run.py 40 pose    # geometry on a perfect lens (layout limits)
    ONLY=typed python3 run.py 40 pose    # geometry on a typed lens (I1)
    ONLY=wave  python3 run.py 14 pose    # geometry on I2e, + the ball wave (slow: ~10 min)
    DESIGN_PLACEMENT=0 ONLY=true python3 run.py 30 pose   # the design's sticks placed perfectly
    TRUE_K=1100,642,508,-0.03,0.0 FO_AIM_Z=0.45 ...   # phone-like lens

Bay frame and cameras as in `camera_pose_sticks.h`: origin at the ball spot, X toward the target,
Z up, golfer on +Y. Noise and placement assumptions are inline in `sim.py`, under `scene()` and
`SIG`. They are the inputs §11 items 17–22 are meant to replace with measurements.
