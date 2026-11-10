# Ball Flight Height Classification (Low / Mid / High)

Reference spec for classifying shot trajectory height in PinPoint Studio.

## 1. Key principle

There is **no official definition** of low, mid or high ball flight. Height is always relative to **ball speed** and **club**, because a slower player can't reach tour apex heights. A driver peaking at 25 yd is mid for a typical club golfer and low for a tour player.

Classification should therefore use speed-normalised metrics, in this order of preference:

1. **Landing (descent) angle**: the preferred metric. It depends much less on speed and is what fitters and coaches use.
2. **Apex / carry ratio**: a speed-normalised substitute when landing angle isn't available.
3. **Absolute apex height**: only meaningful against a reference matched to the player's speed. Never classify on absolute apex alone.

## 2. Metric definitions

| Metric | Definition | Units |
|---|---|---|
| Max height (apex) | Maximum height of the ball above the launch level during flight | yards (or metres) |
| Landing angle | Angle of the ball's descent relative to the ground at first landing | degrees |
| Carry | Distance through the air to a landing point level with the launch point ("carry flat") | yards (or metres) |
| Apex/carry ratio | `max_height / carry`, both in the same units | dimensionless |

## 3. Reference data (TrackMan Tour averages)

| Tour | Club | Ball speed (mph) | Launch (°) | Spin (rpm) | Max height (yd) | Landing (°) | Carry (yd) | Apex/carry |
|---|---|---|---|---|---|---|---|---|
| PGA | Driver | 167 | 10.9 | 2686 | 32 | 38 | 275 | 0.116 |
| PGA | 7-iron | 120 | 16.3 | 7097 | 32 | 50 | 172 | 0.186 |
| PGA | PW | 102 | 24.2 | 9304 | 29 | 52 | 136 | 0.213 |
| LPGA | Driver | 140 | 13.2 | 2611 | 25 | 37 | 218 | 0.115 |
| LPGA | 7-iron | 104 | 19.0 | 6699 | 26 | 47 | 141 | 0.184 |
| LPGA | PW | 86 | 25.7 | 8403 | 23 | 48 | 107 | 0.215 |

Observations:
- **Apex is nearly constant through the bag.** TrackMan reports only about 3 yd between driver and PW on the PGA Tour. Lofted clubs reach a similar height over a much shorter carry, so they land steeper.
- **Apex/carry ratio is almost identical between PGA and LPGA** for each club, despite a ~27 mph difference in driver ball speed. This supports using it as a speed-normalised metric.
- Tour driver apex is trending upward. TrackMan's 2023 update reported about 35 yd at 171 mph ball speed.

### Amateur driver reference (by club speed)

Optimal driver windows published by Golf.com, based on TrackMan data:

| Club speed (mph) | Peak height (ft) | Descent angle (°) |
|---|---|---|
| > 105 | 100–120 | 34–38 |
| 97–104 | 87–100 | 33–37 |
| 84–96 | 70–86 | 32–36 |
| 72–83 | 58–70 | 31–35 |
| < 72 | 45–58 | 27–31 |

## 4. Classification thresholds

> **Status: provisional.** These are heuristic starting values derived from the reference data above. They are not an industry standard and should be tuned against real PinPoint capture data.

Bands are contiguous: `low` if value < L, `high` if value > H, otherwise `mid`.

### 4.1 Landing angle (preferred)

| Club category | Clubs | L (°) | H (°) |
|---|---|---|---|
| Driver | Driver | 33 | 41 |
| Fairway / hybrid | 3W, 5W, 7W, hybrids | 37 | 45 |
| Long irons | 2–4 iron | 41 | 49 |
| Mid irons | 5–7 iron | 44 | 52 |
| Short irons | 8, 9, PW | 47 | 54 |
| Wedges | GW, SW, LW | 49 | 56 |

Context:
- Driver: flatter landings produce more roll. At tour speed, each degree flatter is worth close to 2 yd of total distance.
- Irons: about 45° is the usual threshold for a shot that holds a green, and many amateurs land below it.

### 4.2 Apex/carry ratio (fallback)

| Club category | L | H |
|---|---|---|
| Driver | 0.10 | 0.14 |
| Fairway / hybrid | 0.12 | 0.16 |
| Long irons | 0.14 | 0.18 |
| Mid irons | 0.16 | 0.21 |
| Short irons | 0.18 | 0.23 |
| Wedges | 0.19 | 0.25 |

Note: wedge ratios are less reliable for partial shots. Restrict classification to full swings where possible.

### 4.3 Absolute apex (last resort)

Use only when neither landing angle nor carry is available. Compare against a speed-matched reference:
- Driver: interpolate between the tour points (140 mph ball speed → 25 yd, 167 mph → 32 yd), roughly 0.26 yd per mph. Extrapolate cautiously below 140 mph and cross-check against the Golf.com amateur table.
- Apply the same ±15–20% band around the reference as the other methods imply.

## 5. Suggested algorithm

```
classify_flight(shot):
    category = club_category(shot.club)
    if shot.landing_angle is known:
        return band(shot.landing_angle, LANDING[category])
    if shot.max_height and shot.carry are known:
        return band(shot.max_height / shot.carry, APEX_CARRY[category])
    if shot.max_height and shot.ball_speed are known:
        ref = reference_apex(category, shot.ball_speed)
        return band(shot.max_height / ref, (0.85, 1.15))
    return UNKNOWN

band(value, (L, H)):
    if value < L: return LOW
    if value > H: return HIGH
    return MID
```

Implementation notes:
- Keep thresholds in a data table or config, not hard-coded, so they can be tuned.
- Record which method produced each classification. Landing angle is the most trustworthy result and the absolute-apex method the least.
- All reference values assume flat landing, calm conditions and sea level. Elevation, wind and an uphill or downhill landing all shift apex, carry and landing angle.
- Store the raw values alongside the label so shots can be reclassified if thresholds change.

## 6. Sources

- TrackMan PGA/LPGA Tour averages: https://support.trackmangolf.com/hc/en-us/article_attachments/7349802113051 and https://support.trackmangolf.com/hc/en-us/article_attachments/7349801987483
- TrackMan, "What is Apex Height?": https://www.trackman.com/blog/apex-height
- Golf Monthly, 2023 TrackMan PGA Tour averages: https://www.golfmonthly.com/tour/how-far-pga-tour-players-hit-every-club-in-the-bag
- Golf.com, optimal TrackMan numbers by swing speed: https://golf.com/gear/swing-speed-optimal-trackman-numbers-to-hit-your-drives-farther/
- The Golf News Net, interpreting TrackMan numbers: https://thegolfnewsnet.com/golfnewsnetteam/2016/10/24/what-do-trackman-numbers-mean-interpret-results-launch-monitor-100881/
