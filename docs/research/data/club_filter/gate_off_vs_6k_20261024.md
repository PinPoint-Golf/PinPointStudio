# Club filter gate — off

## Clubhead speed at impact against the launch monitor

| set | n | median ratio | IQR | median \|1−ratio\| | bias mph | SD mph | < 0.7 |
|---|---:|---:|---:|---:|---:|---:|---:|
| all pairs | 74 | 0.832 | 0.787–0.895 | 0.175 | -11.7 | 10.9 | 6 |
| tuning | 44 | 0.843 | 0.800–0.890 | 0.160 | -11.0 | 7.0 | 1 |
| held-out | 30 | 0.818 | 0.748–0.951 | 0.183 | -12.6 | 14.8 | 5 |

## Club angular speed (KS) through the downswing

- swings with a club ω series: 74
- two or more humps in the last 250 ms: 49 (66 %)
- correlation of club ω with clubhead speed, last 250 ms: median 0.898, 10th percentile 0.670
- club ω peak minus clubhead-speed peak: median +17 ms, IQR -16…+25 ms
- club ω peak before impact: median -46 ms

# Club filter gate — on

## Clubhead speed at impact against the launch monitor

| set | n | median ratio | IQR | median \|1−ratio\| | bias mph | SD mph | < 0.7 |
|---|---:|---:|---:|---:|---:|---:|---:|
| all pairs | 74 | 0.988 | 0.931–1.068 | 0.070 | -0.4 | 6.4 | 0 |
| tuning | 44 | 1.008 | 0.954–1.071 | 0.051 | +0.7 | 4.9 | 0 |
| held-out | 30 | 0.948 | 0.892–1.060 | 0.085 | -2.0 | 7.9 | 0 |

## Club angular speed (KS) through the downswing

- swings with a club ω series: 74
- two or more humps in the last 250 ms: 7 (9 %)
- correlation of club ω with clubhead speed, last 250 ms: median 0.980, 10th percentile 0.911
- club ω peak minus clubhead-speed peak: median -25 ms, IQR -42…-5 ms
- club ω peak before impact: median -28 ms

## Off → on, swing by swing (no-regression check)

| metric | swings compared | changed | median \|Δ\| | max \|Δ\| |
|---|---:|---:|---:|---:|
| attack | 74 | 50 | 2.39 | 93.89 |
| lowpoint | 72 | 66 | 0.61 | 7.98 |
| lag | 74 | 74 | 7.58 | 38.63 |
| impact_mph | 74 | 74 | 14.01 | 43.31 |

P-position times identical on 77 of 77 swings.
