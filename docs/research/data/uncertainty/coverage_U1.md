# U1 per-sample σθ — coverage on hand marks

Run root `/mnt/swingdata/scratch/uncert/base`; 996 marked face-on frames on 58 swings. Gross = |r| > max(15°, 4σ). Targets: 60–76 % within ±1σ, 90–98 % within ±2σ (non-gross frames).

## Held out, both rotations pooled (the per-group verdict; groups under 30 frames are not gated)

| group | n (non-gross) | within ±1σ | within ±2σ | n all | observed gross |
|---|---:|---:|---:|---:|---:|
| address | 51 | 65% | 92% | 51 | 0.0% |
| early_bs | 145 | 90% | 95% | 146 | 0.7% |
| backswing | 154 | 75% | 95% | 156 | 1.3% |
| top | 34 | 71% | 85% | 35 | 2.9% |
| downswing | 159 | 72% | 88% | 159 | 0.0% |
| impact | 99 | 62% | 89% | 102 | 2.9% |
| through | 102 | 81% | 92% | 107 | 4.7% |
| finish | 239 | 78% | 97% | 240 | 0.4% |
| ALL | 983 | 76% | 93% | 996 | 1.3% |

## Group inflation (gate rule) — {'top': 1.55, 'downswing': 1.4, 'impact': 1.05}

Held out, pooled, AFTER the inflation:

| group | n (non-gross) | within ±1σ | within ±2σ | n all | observed gross |
|---|---:|---:|---:|---:|---:|
| address | 51 | 65% | 92% | 51 | 0.0% |
| early_bs | 145 | 90% | 95% | 146 | 0.7% |
| backswing | 154 | 75% | 95% | 156 | 1.3% |
| top | 34 | 82% | 91% | 35 | 2.9% |
| downswing | 159 | 80% | 91% | 159 | 0.0% |
| impact | 99 | 63% | 92% | 102 | 2.9% |
| through | 102 | 81% | 92% | 107 | 4.7% |
| finish | 239 | 78% | 97% | 240 | 0.4% |
| ALL | 983 | 78% | 94% | 996 | 1.3% |

## Held out — fit on half A, scored on half B

| group | n (non-gross) | within ±1σ | within ±2σ | n all | observed gross |
|---|---:|---:|---:|---:|---:|
| address | 15 | 87% | 100% | 15 | 0.0% |
| early_bs | 41 | 90% | 95% | 42 | 2.4% |
| backswing | 48 | 77% | 94% | 49 | 2.0% |
| top | 14 | 57% | 79% | 15 | 6.7% |
| downswing | 52 | 79% | 98% | 52 | 0.0% |
| impact | 41 | 73% | 90% | 43 | 4.7% |
| through | 34 | 76% | 88% | 34 | 0.0% |
| finish | 77 | 68% | 96% | 77 | 0.0% |
| ALL | 322 | 76% | 94% | 327 | 1.5% |

## Held out — fit on half B, scored on half A

| group | n (non-gross) | within ±1σ | within ±2σ | n all | observed gross |
|---|---:|---:|---:|---:|---:|
| address | 36 | 56% | 89% | 36 | 0.0% |
| early_bs | 104 | 89% | 95% | 104 | 0.0% |
| backswing | 106 | 74% | 96% | 107 | 0.9% |
| top | 20 | 80% | 90% | 20 | 0.0% |
| downswing | 107 | 69% | 83% | 107 | 0.0% |
| impact | 58 | 53% | 88% | 59 | 1.7% |
| through | 68 | 84% | 94% | 73 | 6.8% |
| finish | 162 | 83% | 97% | 163 | 0.6% |
| ALL | 661 | 76% | 93% | 669 | 1.2% |

## In sample (the shipped table)

| group | n (non-gross) | within ±1σ | within ±2σ | n all | observed gross |
|---|---:|---:|---:|---:|---:|
| address | 43 | 65% | 91% | 51 | 15.7% |
| early_bs | 145 | 88% | 95% | 146 | 0.7% |
| backswing | 154 | 77% | 95% | 156 | 1.3% |
| top | 34 | 74% | 88% | 35 | 2.9% |
| downswing | 159 | 81% | 94% | 159 | 0.0% |
| impact | 100 | 72% | 95% | 102 | 2.0% |
| through | 102 | 82% | 94% | 107 | 4.7% |
| finish | 239 | 77% | 96% | 240 | 0.4% |
| ALL | 976 | 79% | 95% | 996 | 2.0% |

## Lag-1 ρ of consecutive marked residuals

- band: 0.36 over 283 pairs
- seg: 0.71 over 21 pairs
- ALL: 0.84 over 332 pairs

## DTL (band-template truth; σ fitted at ρ̂ ≥ 0.9, scored everywhere as σ/max(ρ̂, 0.5))

| tier | σ | pGross | bias | n near address | n all | within ±1σ | within ±2σ |
|---|---:|---:|---:|---:|---:|---:|---:|
| RAY | 0.50° | 0.001 | +0.25° | 425 | 425 | 90% | 100% |
| BAND | 1.00° | 0.020 | +0.00° | 0 | 0 | nan% | nan% |
| HELD | 4.00° | 0.100 | +0.00° | 0 | 0 | nan% | nan% |
