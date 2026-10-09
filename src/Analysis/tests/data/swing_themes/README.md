# Swing themes golden fixture

The reference the C++ swing themes (`src/Analysis/swing_themes.h`) are tested against. The Python
prototype in `tools/themes/` is the reference implementation; this directory pins its output.

- `ledgers/<session>/diagnostics.json` — copies of Mark's 5 library sessions' ledgers
  (`/mnt/swingdata/Mark-Liversedge/*/diagnostics.json`, as of 9 Oct 2026; 135 swings), cut down
  to what the themes read: per shot `shotId` and `rows`; per row `conditionId` and `state`, plus
  `drivingMeasureId`, `value`, `corridorLo`, `corridorHi`, `corridorShape` and `readings`
  (`measureId`, `value` and the same three corridor fields) on rows that are not
  `notAssessable` — the corridor is what "what you do well" judges each reading against, by
  value. Compact JSON. They still parse with `fromJson(root["ledger"].toObject())`
  (`diagnostic_ledger.h`).
- `expected.json` — what the full pipeline gives on those ledgers with the repo's
  `src/Resources/diagnostics/core.json`: the prepared matrix, k, eigenvalues and parallel-analysis
  thresholds, rotated loadings, each theme's members, tier, stability, trend, layer 1 ("what we
  see most", with a pip per judged session), `doWellCandidates` (every row "what you do well"
  rule v1 keeps after the needs-work exclusion, with its families — metricKey roots, a trailing
  `Signed` removed — before the view drops the rows a SHOWN theme's members touch, and before
  its cap), `conditionFocus` (every condition's drill — the first of its `drills` the shipped
  `src/Resources/diagnostics/drills.json` holds — and its earliest swing position),
  `focusOrder` (focus rule v1: every needs-work fault grouped by drill, a fault with none alone,
  ranked earliest first, then drill, more faults, higher share, key), the summary lines, and
  `view` — exactly what the home screen draws: `subtitle`, `focus` (title, aimFor, rightNow, why,
  practiseLabel, practise, reason, conditionIds), `next`, `doWell`, `needsWork`, `together` and
  `note`. Random numbers are `std::mt19937_64` (`DetRng`), one seed
  per replicate (`tools/themes/theme_rng.py`), so both languages draw the same rows.

## Regenerating

From the repo root:

```
/opt/homebrew/bin/python3 -I -B tools/themes/make_golden.py strip \
    /mnt/swingdata/Mark-Liversedge src/Analysis/tests/data/swing_themes/ledgers
/opt/homebrew/bin/python3 -I -B tools/themes/make_golden.py expected \
    src/Analysis/tests/data/swing_themes/ledgers src/Analysis/tests/data/swing_themes/expected.json
```

`strip` re-copies the ledgers (only needed to take in newer sessions; it asserts the swing table,
the fired/assessed counts and the in-band counts read from the copies equal the originals').
`expected` takes about a minute.

Regenerate `expected.json` whenever core.json's orientation (measure `shape`, signal
`direction`, `unwatchedTail`), reducers (anchor / window phases, which give each measure's swing
position), golfer phrases (`golfer`, `golferWell`, `golferWhy`, `golferHigh`, `golferLow`) or a
condition's `kind`, `detection`, `prominence`, `detectedBy` or `drills` change, `drills.json`'s
ids, labels or instructions change, or the algorithm in `tools/themes/` does.
