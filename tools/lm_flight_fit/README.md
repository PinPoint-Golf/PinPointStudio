# lm_flight_fit

Refits the aero constants in `LmFlightModel` (`src/Analysis/lm_flight_path.h`): `cd0`, `cdS`,
`clS`, `clS2` and `clMax`. The target is every GC Quad shot in the corpus that reports carry,
peak height and descent.

There are two steps, and both only read from the share.

```sh
# 1. Extract the shots. The CSV holds corpus data, so write it OUTSIDE the repo.
python3 tools/lm_flight_fit/extract_gcquad_shots.py /mnt/swingdata/corpus/swings "$TMPDIR/gcquad_shots.csv"

# 2. Build the fitter against the live header, then run it.
c++ -O2 -std=c++17 -I src/Analysis tools/lm_flight_fit/lm_flight_fit.cpp -o "$TMPDIR/lm_flight_fit"
"$TMPDIR/lm_flight_fit" "$TMPDIR/gcquad_shots.csv"          # compiled defaults, fit, fitted
"$TMPDIR/lm_flight_fit" "$TMPDIR/gcquad_shots.csv" --eval 0.2017 0.3313 2.3648 4.8747 0.30
```

The extractor needs libppswing's Python reader in `../libppswing`, the same as
`tools/pp_swingdoc.py`. A run of the fitter takes about 90 s.

## What it does

- **The model is the header's own.** The fitter `#include`s `lm_flight_path.h` and calls
  `lmIntegrateFlight()` directly, so it cannot drift away from the model it tunes.
- **The cost** is the weighted mean, over shots, of the squared relative errors in carry,
  apex and descent.
- **Weighting.** The low-spin shots (< 3000 rpm) and the rest each carry half the total
  weight. The corpus is mostly mid-spin irons, and with every shot weighted alike they
  outvote the few fast, low-spin shots, which are exactly the shots the old constants
  carried 15% short. `--uniform` weights every shot alike instead. On the Oct 2026 corpus
  that leaves the low-spin shots 7.7 yd short at the median, against 3.9 yd balanced, and
  improves the mid-spin median by only 0.3 yd.
- **Plausibility is a hard penalty:**
  - Cd stays within 0.20–0.35 over S = 0…0.4.
  - The lift peak sits between S = 0.15 and 0.60. Past the peak Cl is held flat; see
    `LmFlightModel::lift()`.
  - `clMax` stays within 0.15–0.40.
- **Search.** Nelder–Mead runs from six starts spread over that box, and the best result
  wins. All six converge to the same point.
- **Output.** It prints the median and worst error in carry, apex and descent, overall and by
  spin band (< 3000, 3000–6000 and > 6000 rpm), followed by every shot.

## Reproducing the "before" numbers

The pre-fit model also had a different lift curve: its quadratic was not held at its peak.
To reproduce its numbers, build the same tool against that header:

```sh
mkdir -p "$TMPDIR/oldhdr"
git show 16f12352:src/Analysis/lm_flight_path.h > "$TMPDIR/oldhdr/lm_flight_path.h"
c++ -O2 -std=c++17 -I "$TMPDIR/oldhdr" tools/lm_flight_fit/lm_flight_fit.cpp -o "$TMPDIR/lm_flight_fit_old"
"$TMPDIR/lm_flight_fit_old" "$TMPDIR/gcquad_shots.csv" --eval 0.255 0.16 1.90 3.25 0.45
```

## Known limits (Oct 2026 corpus: 36 shots, 1,964–6,174 rpm, 96–133 mph)

- **Low-spin shots.** On their own they want `cd0` below 0.20, and even there they come up
  about 3 yd short. In this model drag depends only on S, and the residual looks like the
  speed (Reynolds number) dependence that leaves out. The shipped fit sits at the bottom of
  the Cd band and leaves those shots about 2% short.
- **High spin is untested.** The corpus has one shot above 6000 rpm and no wedges. The held
  lift peak (Cl 0.287 from S 0.243) is what a wedge would fly on, and no data here checks it.
- **`clMax` is not identifiable.** The fit puts it above the quadratic's peak, where it never
  binds. The shipped 0.30 is only a backstop.
- **The target is itself a model.** GC Quad computes its carry, apex and descent with its own
  flight model, and this fit matches the panel to the device rather than to physics.
