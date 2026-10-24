# Club track diagnostic

74 swings carry analysis.club.filter.

| statistic | median | 90th percentile | max |
|---|---:|---:|---:|
| rateWobble | 0.44 | 1.52 | 2.80 |
| residualRmsDeg | 5.33 | 6.58 | 8.59 |
| nDownweighted | 7.00 | 17.00 | 55.00 |
| nGated | 8.50 | 37.00 | 84.00 |
| nisMean | 1.69 | 2.87 | 10.14 |
| humpsRaw | 4.00 | 6.00 | 7.00 |

## Does the wobble mark error? (Spearman with |1 − clubhead speed / launch monitor| at impact)

| statistic | n | vs this run's error | vs the reference run's error |
|---|---:|---:|---:|
| rateWobble | 74 | +0.11 | +0.23 |
| residualRmsDeg | 74 | -0.06 | +0.16 |
| nDownweighted | 74 | +0.05 | +0.07 |
| nGated | 74 | +0.02 | +0.05 |
| nisMean | 74 | -0.00 | +0.23 |
| humpsRaw | 74 | +0.08 | +0.09 |

## Worst ten by rate wobble

| swing | rateWobble | residual ° | gated | down-weighted | humps raw → filtered |
|---|---:|---:|---:|---:|---|
| 2026-10-20_Nick-Henn_Wrist_03/swing_0007 | 2.80 | 6.8 | 39 | 11 | 4 → 1 |
| 2026-10-20_Nick-Henn_Wrist_02/swing_0001 | 2.54 | 5.7 | 76 | 47 | 5 → 1 |
| 2026-10-20_Nick-Henn_Wrist_03/swing_0011 | 2.12 | 6.5 | 37 | 5 | 4 → 1 |
| 2026-10-20_Nick-Henn_Wrist_03/swing_0008 | 1.97 | 5.3 | 4 | 4 | 6 → 1 |
| 2026-10-20_Nick-Henn_Wrist_03/swing_0002 | 1.78 | 5.4 | 40 | 7 | 3 → 1 |
| 2026-10-20_Nick-Henn_Wrist_02/swing_0003 | 1.76 | 6.9 | 13 | 12 | 2 → 1 |
| 2026-10-20_Nick-Henn_Wrist_03/swing_0012 | 1.61 | 5.3 | 45 | 9 | 5 → 1 |
| 2026-10-20_Nick-Henn_Wrist_02/swing_0008 | 1.60 | 5.5 | 43 | 21 | 7 → 1 |
| 2026-10-20_Nick-Henn_Wrist_03/swing_0004 | 1.52 | 5.7 | 48 | 11 | 3 → 1 |
| 2026-10-20_Mark-Liversedge_Wrist_01/swing_0015 | 1.38 | 5.4 | 8 | 9 | 2 → 1 |
