# Instrumented-club capture protocol (retroreflective bands)

**Purpose.** A taped club turns every swing into dense, auto-labelable ground
truth for the **passive** shaft/clubhead detectors (the product path — see
`shaft_detection_improvements.md` §10 positioning note), and doubles as an
optional high-accuracy tracking mode. This protocol specifies the materials,
the band pattern, the per-club measurement record (`clubs.json`), and the
capture session plan. Companion docs: `shaft_validation_protocol.md`
(the passive-detector V&V this feeds), `pipeline_validation_and_tuning.md`
§5.5/§8 (where the resulting truth is consumed).

## 1. Materials

- **Tape:** glass-bead retroreflective tape, silver/white, **25 mm (1″) wide**
  (e.g. 3M Scotchlite 8830/580-class or equivalent *conformable* tape).
  Thin (~0.1–0.15 mm; no swing-feel effect). NOT prismatic/"diamond grade"
  (too stiff for a conical shaft); NOT plain chrome/shiny tape (that is just
  another specular).
- **Light: a small LED source AT the camera lens** (ring light or LEDs within
  a few degrees of the lens axis; visible-band, never IR — IR launch-monitor
  conflict + the RGB pose pipeline shares the camera). Retro tape returns
  light **to its source in a ~1–2° cone**: ceiling/off-axis lights get the
  return, the camera does not. A few watts on-axis outperforms hundreds of
  watts of off-axis flood. Matte-white tape + strong ambient is the
  budget/no-ring alternative (angle-tolerant but loses on dark-arc coverage
  and white-on-blown-mat contrast).
- Tape measure (mm), and a clean shaft (wipe before wrapping).

## 2. Band pattern — "2 – 1 – 3" (grip → head)

```
grip ▓▓  ▓▓ ──────────── ▓▓ ──────────── ▓▓  ▓▓  ▓▓ hosel→head
      2 bands           1 band            3 bands
```

- **Below the grip: 2 bands.** First band starts ~20 mm below the grip;
  bands and gaps both **25 mm**.
- **Mid-shaft: 1 band** (occlusion insurance — hands/body can hide the grip
  group, the mat can wash out the hosel group; also a third collinear point).
- **Above the hosel: 3 bands**, 25 mm bands/gaps, ending ~30 mm above the
  hosel (stay off the ferrule taper).
- **Optional: one ~15–20 mm retro patch on the crown of the head** — makes
  the stage-2 clubhead measurement direct instead of extrapolated.

**Why this shape (the design rationale, keep intact):**
- **25 mm ≈ 7 px** at the current capture scale (~3.5 mm/px): resolvable
  through sensor bloom (retro bands saturate) and compression. 10 mm bands
  (~3 px) with 10 mm gaps merge under bloom — rejected.
- **Asymmetric counts (2 vs 3)** give 180° orientation directly — flips are
  an adjudicated failure class in the passive tracker (findings §8, F6/F19).
- **Along-shaft distance RATIOS are preserved under foreshortening**
  (projection is linear), so the pattern signature matches at any club
  orientation — this is what makes bands unconfusable with specular flashes:
  a flash is one blob; five blobs, collinear through the grip, at the
  recorded ratios, are ours.
- Motion blur at speed is tangential (across the shaft) while the band
  separations run along it — the pattern survives the impact streak.
- **Band-pair projected spacing ÷ known spacing = per-frame direct
  foreshortening (ρ)** — this replaces the stage-2 length-model *prior* with
  a *measurement*, and the head position becomes a known extrapolation beyond
  the hosel group even when the head itself is invisible.

## 3. The per-club measurement record

**In production the app owns these records, per athlete**: Edit Athlete →
Clubs card (`AthleteClubsSection.qml`), persisted under
`athletes/<uuid>/clubs` in the app settings and served to consumers via
`AthleteController::clubsFor()` — record shape identical to the JSON below
plus `loftDeg` (shaftType, loftDeg, lengthMm, bandWidthMm, bandCentersMm,
hoselFromButtMm, headPatch, tapedOn, notes; keys = the canonical
club-vocabulary names markup writes to `truth.json meta.club`). Every athlete
is seeded once with a standard bag (driver, 5–9 iron, GW, SW, putter) carrying
factory spec defaults (T100-guided iron lofts/lengths, Vokey wedges, stock
metals — `defaultClubRecordFor()`); added clubs get the same defaults, and an
intentionally emptied bag stays empty. `clubs.json` remains the LAB
interchange format for offline tools; export from the app records when the
lab needs it.

### `clubs.json` (lab interchange)

Measure after taping, to the millimetre, **from the butt end of the grip** to
the CENTRE of each band. Without this record the tape is only "easier to
see"; with it, every swing is auto-labelable truth. One file per corpus root
(e.g. `/mnt/swingdata/clubs.json`); the `id` must match what markup writes in
`truth.json meta.club`.

```json
{
  "version": 1,
  "units": "mm",
  "clubs": [
    {
      "id": "GAP WEDGE",
      "type": "wedge",
      "shaft": "steel",
      "club_length_mm": 900,
      "band_width_mm": 25,
      "bands_from_butt_mm": [270, 320, 555, 760, 810, 860],
      "head_patch": false,
      "hosel_from_butt_mm": 890,
      "taped_on": "2026-07-06",
      "notes": "band order butt->tip; 2-1-3 pattern"
    }
  ]
}
```

Rules:
- `bands_from_butt_mm` lists ALL band centres in butt→tip order (2+1+3 = 6
  entries for the standard pattern). The consumer derives the group structure
  from the gaps; do not encode it separately (single source of truth).
- Re-measure and bump `taped_on` whenever tape is replaced (adhesive drift is
  real); a swing references the record via `meta.club` + capture date.
- `hosel_from_butt_mm` anchors the head extrapolation; with a `head_patch`
  the patch supersedes it.

## 4. Capture session plan (one session, four goals)

The first instrumented session should ALSO be the uncropped/coaching-studio-
like session, so a single capture serves: (a) auto-truth for passive-detector
tuning, (b) off/crop-tier validation (first ever), (c) down/thru measured
coverage, (d) multi-club length-model form selection.

- Wrist session (`sessionType = 1`), face-on `perspective = 2`, pose ON.
- **Framing: whole swing arc in frame** (trail-side room + headroom).
- **Lighting: normal downlight, no natural light** (per the interim-stratum
  decision) + the **camera ring light ON**.
- **Exposure: hitting-area highlights below clipping** (the bands are bright —
  use them to go SHORTER on exposure, which also narrows blur).
- **Clubs: ≥3 taped (wedge / mid-iron / driver minimum)**, several swings
  each; record `meta.club` in markup exactly matching `clubs.json` ids.
- Record in markup `meta`: `instrumented: true`, `lighting`, backdrop absence.
- Hand-label a small **untaped** control subset in the same session (the
  appearance bridge: tape changes the shaft's look, and the passive detector
  must be scored on unmarked clubs too).

## 5. What gets built against it (order of work)

1. **Stripe detector — Python exemplar first** (the exemplar-first rule holds;
   the one C++ shortcut taken in this programme was reverted): blob detection
   → collinear line fit through the grip → known-ratio pattern match →
   per-frame θ + ρ + head, with the same meas/pred honesty tiers.
2. **Auto-truth writer**: stripe tracks → dense `truth.json` shaft entries
   (flagged `source: "instrumented"`), feeding the passive detector's
   corpus-scale validation/tuning — the decisions currently deferred as
   corpus-gated (length-model form selection, finish-hold fixes).
3. Only after both passive stages re-freeze against that corpus: the C++ port,
   passive + marker mode together behind `IShaftDetector` (§10.3/§11.1).
