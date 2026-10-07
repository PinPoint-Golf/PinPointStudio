/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "../../Metrics/metric_catalogue.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// ChartMetrics — the C++ home for the derivation maths that the "no JavaScript logic
// in QML" rule keeps out of PpMetricChart, exactly the TimelineLabels shape. Every method is
// const and depends only on its arguments, so one shared instance can be declared
// declaratively (ChartMetrics { id: metrics }) and reused by every chart.
//
// It is no longer literally stateless: seriesGroups() needs the metric catalogue, so one is
// assembled in the constructor and read-only thereafter (the same "built once, then const"
// shape MetricCatalog uses). No method mutates it, so the reuse property above is unchanged.
//
// Phase names/tags are NOT duplicated here — the chart composes segment labels in QML from
// phaseA/phaseB via TimelineLabels.phaseShortTag, and the crosshair value-at-cursor reuses
// TimelineLabels.valueAtNearest. This class owns only the segment vocabulary and the
// per-window summary statistics; short names are the catalogue's, read through shortLabel().
class ChartMetrics : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ChartMetrics(QObject *parent = nullptr)
        : QObject(parent), m_catalogue(pinpoint::analysis::makeMetricCatalogue()) {}

    // Segment list for a swing. [0] = Full — the whole RECORDING ({startUs:0, endUs:spanUs,
    // phaseA:-1, phaseB:-1}); [1] = the SWING (Address→Finish), present only when the swing carries
    // both landmarks with something between them; then one entry per adjacent phase pair, ordered
    // by time:
    //   { startUs, endUs, phaseA:int, phaseB:int } — plus `swing:true` on that one entry.
    //
    // The swing entry is what the chart opens on, so the reader starts on the golfer's motion
    // rather than on the seconds of standing still that bracket it in the recording.
    // The label is composed in QML from phaseA/phaseB via TimelineLabels.phaseShortTag (no
    // tag strings duplicated here). Mirrors swing_data_source.cpp segment logic so the
    // segment vocabulary is identical. `phases` is analysisDetail.phases ([{phase,t_us,…}]).
    Q_INVOKABLE QVariantList segments(const QVariantList &phases, qint64 spanUs) const;

    // THE WINDOW A TRIMMED CHART OPENS ON. Some families are drawn over part of the swing only —
    // the kinematic sequence's four curves run P4 → P8 and nowhere else — and opening those on
    // Address→Finish spends most of the axis on a blank either side of them. Given the drawn
    // series' time extent [dataStartUs, dataEndUs] and the window the chart would otherwise open on
    // [fromUs, toUs], this returns the P-POSITIONS that enclose the data inside that window:
    //   { startUs, endUs, phaseA:int, phaseB:int, trimmed:bool }
    // startUs is the last P-position at or before the first sample, endUs the first at or after
    // the last one (each with one frame of slack, kDataWindowSlackUs, because a curve cut AT a
    // phase starts on the first sample after it). A side with no such P-position keeps the
    // window's own edge, named for the P-position that sits on it (−1 when none does). `trimmed` is false — and the caller keeps its
    // window — when nothing would be dropped, when the data are empty, or when the result would
    // be an empty span. Bounded by P-positions so the window has the same vocabulary as the chips.
    Q_INVOKABLE QVariantMap dataWindow(const QVariantList &phases, qint64 dataStartUs, qint64 dataEndUs,
                                       qint64 fromUs, qint64 toUs) const;
    static constexpr qint64 kDataWindowSlackUs = 15000;

    // Per-metric summary over [startUs, endUs]:
    //   { start, end, min, max, peak, range, delta, rate, tPeakUs, partial,
    //     peakSigma, rateSigma, edgeOk, rateOk, tRateUs }
    // start/end = the ±15 ms windowed MEDIAN at each edge; min/max = the extremum of the 40 ms
    // centred-window MEAN inside the window; peak = whichever of those has the larger magnitude,
    // at tPeakUs, ± peakSigma; range = max-min; delta = end-start; rate = the steepest
    // least-squares slope over any ≥50 ms window, SIGNED, per 100 ms, at tRateUs, ± rateSigma,
    // and only when `rateOk` (see below). `tUs`/`value` are the parallel arrays from
    // analysisDetail.series[i] (tUs ascending).
    //
    // TWO KEYS SAY WHETHER THERE IS ANYTHING TO SHOW, and a display must consult them before it
    // prints anything: `edgeOk` false ⇒ the series carries no valid sample at all, so start, end,
    // min, max, peak, range and delta are zeros from nothing; `rateOk` false ⇒ no window qualified
    // for a slope. Both are the same principle as `partial`'s, one step stronger: partial qualifies
    // a number, these two say there is no number.
    //
    // ⚠ range AND delta DO NOT SHARE EVIDENCE. range = max − min, a span of 40 ms windowed MEANS
    // taken at the samples (ANCHORS) inside the window; delta = end − start, a difference of ±15 ms
    // MEDIANS taken at its two EDGES. Both supports may reach outside the window — neither is
    // clamped to it — so the difference is not the support but what each is anchored on: a window
    // with few anchors in it has little to span while its edges can still have moved. They agree on
    // any window more than a few samples wide, and on a window narrower than the sample spacing
    // they can openly contradict — RANGE 0.2 beside Δ 4, when the edge medians moved and nothing inside the window
    // had a span. That is left as it is on purpose: `range = max(range, |delta|)` would make range a
    // blend of two reducers (so it would no longer be the number the diagnostics engine's Extremum
    // measures compare against, design §7 item 5) and would hide the one signal a reader gets that
    // the window is too narrow for the curve in it. A caller that wants them consistent should
    // widen the window, not launder the number.
    //
    // ⚠ EVERY ONE OF THOSE REDUCTIONS IS src/Analysis/series_reduce.h's, NOT THIS CLASS'S — the
    // diagnostics engine's buildPhaseGrid calls the same four functions with the same tuned
    // windows, which is what makes design §7 item 5 ("the chart and the engine agree to display
    // precision on every authored measure") a property of the code rather than a hope. The only
    // arithmetic left here is the interpolated window edge that stands in when reduceAt has no
    // valid sample within ±15 ms to take a median of, and that case sets `partial`.
    //
    // Delegates to summaryMasked() with an EMPTY mask, which is the "every sample is valid"
    // case — so this overload is exactly the pre-validity behaviour and `partial` is always
    // false. Kept because most callers have no mask to pass and should not have to invent one.
    Q_INVOKABLE QVariantMap summary(const QVariantList &tUs, const QVariantList &value,
                                    qint64 startUs, qint64 endUs) const;

    // The same summary, respecting the series' per-sample validity mask (swing.json
    // `metrics[].valid`, design metric_presentation_honesty.md §5.1). `valid` is an int list
    // parallel to `tUs` where 0 marks a sample the grid BRIDGED across a gated or absent run;
    // EMPTY means every sample is valid, which is what every series that predates the field
    // carries and why summary() above can simply pass {}. A mask SHORTER than the curve is
    // discarded wholesale — see the short-mask rule on measuredAt() below, which this shares.
    //
    // An invalid sample is NOT a measurement, so it enters no reduction: it is not in an edge's
    // median window, not in an extremum's 40 ms mean, not in a rate window's fit. The one place
    // this has to reach across a gap is a window edge with no valid sample within ±15 ms of it,
    // and it says so rather than hiding it:
    //
    //   `partial` (bool) — the window's numbers do not rest on a continuous measurement. True
    //   when the window contains an invalid sample, or when an edge fell back to interpolating
    //   between the nearest valid samples because there was none within ±15 ms of it (which is
    //   also the case that catches a window sitting ENTIRELY inside a bridged run, where there is
    //   no sample to scan at all). The card renders it as a "PARTIAL" chip; it never changes a
    //   value.
    //
    //   ⚠ BOTH RULES REQUIRE A HONOURED MASK, so `partial` is unreachable on a series that
    //   declares nothing — exactly as it was before Phase 2. The fallback ALSO fires with no mask,
    //   whenever a window edge lands more than 15 ms from any sample, and on a real timeline that
    //   is ordinary rather than exotic (a fifth of a series' span can be that far from a sample,
    //   with gaps to 80 ms). A coarse series is not an incomplete one: it was measured everywhere
    //   it claims to have been. The chip's claim is "the producer BRIDGED part of this window", and
    //   the mask is the only thing that ever says so; a series with nothing readable at all is
    //   `edgeOk` false, not `partial` true.
    //
    //   `extremumOk` (bool) — false when NO VALID SAMPLE LIES INSIDE [startUs, endUs], so
    //   min/max/peak/range came from the two interpolated EDGES rather than from any measurement in
    //   the window. Ordinary rather than exotic: any window narrower than the sample spacing does it
    //   on a healthy series. It matters more since Phase 6, because the PEAK tile is otherwise a
    //   point on the DRAWN LINE (windowedMean) by construction and this is the one state where it
    //   cannot be — no point of the line is in the window to be the peak. `partial` does not cover
    //   it: that flag needs an honoured mask, so an unmasked series in this state wore no chip at
    //   all. Gate PEAK/MIN/MAX/RANGE on it exactly as PK RATE is gated on `rateOk`; Δ and the
    //   window edges are NOT gated, being statements about instants, which is what the edges are.
    //
    //   `edgeOk` (bool) — false when the series carries NO valid sample: every sample bridged, or
    //   an empty curve. There is then nothing for the window edges to interpolate between, and
    //   start/end/min/max/peak/range/delta are all 0.0 — a "PEAK 0, Δ 0, RANGE 0" card that reads
    //   as a still, well-behaved curve. The tiles print "—" instead, the same way PK RATE does on
    //   `rateOk` false. The zeros are still returned so a caller mid-migration degrades rather
    //   than crashes; a caller that displays them has been told not to.
    //
    // ⚠ THE RATE AND PEAK DEFINITIONS CHANGED IN PHASE 2 (design §5.2), and they changed the
    // numbers on every card, on every swing, whether or not anything is masked:
    //
    //   peak/min/max — was the raw argmax over the in-window samples plus the two interpolated
    //   edges; is now the extremum of the 40 ms centred-window MEAN of the valid samples. A
    //   one-sample outlier can no longer be the peak, because a peak now has to have been there
    //   for 40 ms: one 99 among 4s at 8 ms sampling reports about 23, not 99.
    //
    //   rate — was max |Δvalue/Δt| between consecutive samples, which on a still address is
    //   frame noise divided by 8 ms (39 and 291 units per 100 ms on the corpus, design §7 item 2);
    //   is now the steepest least-squares slope over a window of at least 50 ms carrying at least
    //   3 valid samples. It is SIGNED (a slope has a direction, and no consumer could recover one
    //   this class had thrown away), and it can be ABSENT:
    //
    //   `rateOk` (bool) — false when no window in [startUs, endUs] qualifies (a window shorter
    //   than 50 ms, or fewer than 3 valid samples in it, e.g. a two-sample series). `rate`,
    //   `rateSigma` and `tRateUs` are then 0 and MUST NOT be displayed: the card prints "—" and
    //   hides the per-100 ms unit with it. A fabricated 0 would read as a still, well-behaved
    //   curve, which is the exact class of confident absurdity this design exists to remove.
    //
    // `peakSigma` / `rateSigma` are the σ of the winning window, for the "± σ" the summary card
    // carries beside those two tiles — design §5.3. Both are the NOISE, never the motion:
    // peakSigma is the standard error of the window's mean about a LOCAL STRAIGHT LINE through it
    // (so a clean ramp reports 0 rather than reporting its own slope as uncertainty), rateSigma
    // the standard error of the fitted slope (so an exact fit reports 0).
    Q_INVOKABLE QVariantMap summaryMasked(const QVariantList &tUs, const QVariantList &value,
                                          const QVariantList &valid,
                                          qint64 startUs, qint64 endUs) const;

    // ── THE LINE THE CHART DRAWS — and it is the same reduction the PEAK tile reports ──────
    //
    //   { mean: [double…], sigma: [double…] }, both as long as the curve.
    //
    // ⚠ WHY THIS EXISTS AT ALL, because "the chart draws a smoothed curve" is exactly the sentence
    // design §4 principle 1 forbids. Phases 1–3 left the drawn line alone deliberately: the stroke
    // passed through every persisted sample, and only the STROKE said which of it was measured. The
    // cost was that the eye and the card were reading different things — the tile said PEAK 23
    // because a peak has to hold for 40 ms, while the line beside it visibly touched 99, and a
    // reader can only conclude that one of the two is lying. Phase 6 closes that by drawing THE
    // CANDIDATE MEANS reduceExtremum ranks (series_reduce.h windowedMeans): the same per-sample
    // centred 40 ms mean, the same ≥3-sample symmetric widening, the same exclusion of invalid
    // samples. So `summaryMasked().peak` IS an extremum of this array inside the window, bit-exact
    // and by construction rather than by agreement — which is what chart_metrics_test pins on every
    // fixture — and the drawn line is a REDUCTION with a definition, not a display filter with a
    // taste.
    //
    // ⚠ AND IT IS NOT A SMOOTHER, in the one sense that matters: nothing here changes what is
    // persisted (design §6 — no `value` moves in 5.1–5.3), and the raw samples stay ON SCREEN as
    // faint dots behind the line (PpChartPlot.showRawDots, default on). A reader can always see the
    // wobble the reduction is refusing to call a peak. There is no OTHER smoothing anywhere in the
    // chart: this one shared reduction or nothing.
    //
    // An INVALID sample's entry is its RAW value, not a mean, and that is deliberate: it is not a
    // measurement, it is drawn dashed, and it may not draw on its valid neighbours (nor they on it).
    // So a caller can draw `mean` for the WHOLE curve and the bridged runs still show the persisted
    // values, exactly as they did before this phase. `ok` is NOT exported — the QML side already
    // decides validity per sample (PpChartPlot._measured, the same short-mask rule), and a second
    // copy of that verdict crossing the bridge is a second chance to disagree with it.
    //
    // `sigma[i]` is the standard error of that mean about a local straight line — the same number
    // `peakSigma` carries for the winning window (never the spread of the samples; see
    // series_reduce.h detail::windowMeanSigma). It is 0 where the reduction has nothing to say, and
    // meaningless on an invalid entry (a raw value has no window), which costs nothing because the
    // ±σ ribbon draws over measured runs only.
    //
    // Short-mask rule as everywhere else in this class (see measuredAt): a `valid` list shorter than
    // the curve is a malformed document and is discarded wholesale.
    //
    // ⚠ NOT FOR PER-FRAME BINDINGS, and this one is the most expensive call in the class: it
    // marshals two whole series in and two whole arrays back. It is called ONCE PER DATA CHANGE, in
    // PpMetricChart._plottable, where the result is decorated onto the series entry as `mean` /
    // `meanSigma` and every plot, sparkline, crosshair and tooltip reads it from there.
    Q_INVOKABLE QVariantMap windowedMean(const QVariantList &tUs, const QVariantList &value,
                                         const QVariantList &valid) const;

    // Where a metric's geometry MEANS something:
    //   { firstPhase:int, lastPhase:int, firstNarrowed:bool, lastNarrowed:bool, narrowed:bool }
    // The phases are Phase ENUM values, not ladder indices — the caller resolves them against the
    // swing's own phases[] to get instants, because only the swing knows when its P4 happened.
    //
    // Straight off MetricDescriptor::domain, so the manifest is the single author of it and the
    // chart, the pack validator and the diagnostics engine cannot disagree about where a
    // pelvis-sway reading stops meaning translation and starts meaning rotation. A key the
    // catalogue has never heard of gets the descriptor default — the WHOLE swing — because an
    // unknown metric is not a licence to hide part of its curve.
    //
    // ⚠ THE NARROWED FLAGS ARE LOAD-BEARING, PER SIDE, and are why this returns five keys and not
    // two. The default domain is Address..Finish, but the chart's AXIS is the PADDED swing
    // (Segmentation swingStart/End ± boundPadUs = 250 ms), so Address sits 250 ms inside the axis
    // start and Finish 250 ms inside its end. A caller that clipped to the default domain would
    // therefore dash 250 ms off each end of EVERY whole-swing metric — headSway, xFactor,
    // clubheadSpeed — and change its Full-window PEAK/Δ/RATE on every swing, which is precisely
    // the "nothing changes where this does not fire" property phase 1 rests on. It would also
    // collapse any legitimately pre-address window (the still-address check reads
    // Address−300 ms → Address) to nothing.
    //
    // So: clip a side ONLY when the MANIFEST moved that side. `firstNarrowed` is
    // `domain.first != Phase::Address`, `lastNarrowed` is `domain.last != Phase::Finish`, and
    // `narrowed` is either. hipLineTilt is narrowed on the LAST side only.
    Q_INVOKABLE QVariantMap domainFor(const QString &key) const;

    // What the summary CARD shows for this metric — MetricDescriptor::card (metric_descriptor.h
    // says why a card is not the same four tiles for every metric):
    //   { readAt:[{phase:int, label:string}], peak:bool, peakSpan:bool, peakFrom:int, peakTo:int,
    //     delta:bool, deltaSpan:bool, deltaFrom:int, deltaTo:int, deltaLabel:string, rate:bool,
    //     windowedMean:bool, mergeInto:string }
    // Phases are Phase ENUM values, resolved to instants by the caller as domainFor's are. An
    // uncatalogued key gets the default — @ IMPACT, PEAK, Δ SEGMENT, PK RATE — the card as it was.
    Q_INVOKABLE QVariantMap cardSpecFor(const QString &key) const;

    // Was this series actually MEASURED at `us`? — the reference form of the predicate behind the
    // suppressed phase dots, the suppressed crosshair marker and the "—" in the hover and legend
    // readouts, so those cannot drift into slightly different notions of "no reading here".
    //
    // False when `us` lies outside [fromUs, toUs] — the metric's phase domain resolved to
    // instants by the caller, since only the swing knows when its P7 happened — or when the
    // NEAREST sample carries a 0 in `valid` (bridged across a gated or absent run). Nearest is
    // exact for its caller: a phase dot sits on a sample. Pass fromUs == toUs for "no domain".
    //
    // ⚠ THE SHORT-MASK RULE, shared with summaryMasked() and with measure_sample.cpp's
    // buildPhaseGrid: a `valid` list is honoured only when it covers the whole curve
    // (size >= t_us.size()). An EMPTY one is "every sample valid" (C4); a SHORTER one is a
    // malformed document, not a partial statement, and is discarded wholesale — guessing which
    // end it was truncated from would invent validity nobody stated, and bounding the scan at
    // qMin(sizes) instead would make this answer a different question than summaryMasked does
    // about the very same series.
    //
    // ⚠ NOT FOR PER-FRAME BINDINGS. Every call marshals the whole series across the QML boundary,
    // so a binding that depends on the cursor or the playhead must answer this in JS off a sample
    // INDEX (PpChartPlot._measured / PpMetricChart._measuredAt) rather than call in here.
    Q_INVOKABLE bool measuredAt(const QVariantList &tUs, const QVariantList &valid,
                                qint64 us, qint64 fromUs, qint64 toUs) const;

    // Compact display name for a metric key (e.g. "leadWristFlexExt" → "Bow/cup"), or ""
    // when the key is uncatalogued or its descriptor names no short form — the caller then
    // falls back to series.label. This is a straight read of MetricDescriptor::shortLabel,
    // so the chart, the Metric Library and the summary cards all say the same word for the
    // same metric, and a metric added to the manifest is short-named everywhere at once.
    Q_INVOKABLE QString shortLabel(const QString &key) const;

    // The palette NAME a metric is drawn in (MetricDescriptor::color), or "" for a key the
    // catalogue does not know. A name, not a colour: Theme.metricColor(key) turns it into the
    // current theme's hex, after the user's own re-pointing (appSettings.metricColors).
    Q_INVOKABLE QString colorName(const QString &key) const;
    // Every name in the metric palette, in hue order — what a colour picker offers.
    Q_INVOKABLE QStringList colorNames() const;

    // The DISPLAY form of a unit — what goes beside a number on the chart panel.
    //
    // The catalogue's unit is a full phrase where the denominator matters: "% stance width" and
    // "% shoulder width" are different quantities and the Metric Library, which is a reference
    // surface with room, is right to spell both out. On the chart it is repeated beside every
    // value in a data face, and "12 % stance width" next to "34 % stance width" overprinted its
    // neighbour in the summary grid — the phrase was longer than the number it qualified.
    //
    // So this returns the SHORT token: "%" for every percent-of-something, the unit unchanged for
    // everything else. It is keyed on the UNIT, not the metric, deliberately — six metrics share
    // "% stance width" and all six want the same token, so authoring it per descriptor would be six
    // chances to disagree. The canonical unit is untouched: it still has to match the norm's unit
    // (the loader refuses a mismatch) and measureUnitMismatch still compares it against the
    // producer's, so this cannot drift into being the real unit.
    //
    // What replaces the lost words is CONTEXT, not guesswork — see the rule below.
    Q_INVOKABLE QString shortUnit(const QString &unit) const;

    // ── The two value formatters, and why they live here ────────────────────────────────────────
    //
    // ONE rule, ONE implementation. This was three: PpMetricChart._fmt, PpChartSummary._fmt (whose
    // comment said "see PpMetricChart._fmt" — a copy that knew it was a copy), and a third in
    // PpTransitTimeline that concatenated value and unit with no separator and so read "12mph".
    // Three copies of a five-line rule is three chances to disagree, and they already did.
    //
    // It belongs in C++ for the reason this class exists at all: it is derivation, and the "no
    // JavaScript logic in QML" rule keeps derivation out of the .qml files. It is also the only
    // way to TEST it — chart_metrics_test can assert "-8°" and "12 %"; a QML function cannot be
    // asserted anywhere.
    //
    // ── READINGS PRINT IN WHOLE UNITS, WHATEVER σ SAYS ───────────────────────────────────────────
    //
    // σ USED TO GOVERN THE DIGITS (design §5.3, removed 3 Oct): a reading was rounded to the
    // smallest {1,2,5}×10ⁿ not below its σ, so 81.6 mph at σ 7.1 printed "80" and two thorax
    // readings of 398 and 345 °/s at σ 208 both printed "500". With the ± quoted beside every
    // reading the coarsening said the same thing twice, and said it badly — round tens read as a
    // rough guess, not a measurement. The ± is the statement of trust; the reading is the reading.

    // The series' σ as a DISPLAY number — `series` is one entry of analysisDetail.series (the QML
    // bridge's map), and the answer is its `sigma` when the producer characterised one and 0 when it
    // did not.
    //
    // ⚠ THE ABSENT→0 SUBSTITUTION LIVES HERE AND NOWHERE ELSE, and this function exists because it
    // was living in three places: a `_sigma()` copy in PpChartSummary, another in PpMetricChart, and
    // an inline expression inside PpChartPlot._sigmaRuns. Three copies of a four-clause guard
    // (undefined / null / non-finite / ≤ 0) is three chances to disagree about what absence means,
    // which is precisely the confusion the field's contract exists to prevent. It is also the only
    // form that can be tested.
    //
    // The substitution is legitimate ONLY at the display boundary: a reading with no characterised
    // noise is printed at the precision we always printed it, and a 0 out of here means "no claim",
    // never "exact". Nothing that touches DATA may call this to fill in a missing σ.
    //
    // ⚠ NOT FOR PER-FRAME BINDINGS, for the reason measuredAt() carries the same warning: a
    // QVariantMap argument marshals the WHOLE series (t_us, value, valid, phaseSamples) across the
    // QML boundary. Every caller resolves it once per card / chip / row / plot, on a binding that
    // changes with the DATA, and passes the resulting number down to the per-frame formatters.
    Q_INVOKABLE double seriesSigma(const QVariantMap &series) const;

    // What backs the σ chip, and whether the reading carries the gross-error caveat
    // (shaft_uncertainty_propagation_design.md §7, stage U7). → { tip: QString, warn: bool,
    // warnTip: QString }. `tip` names the provenance from the series' `sigmaKind` (calibrated,
    // propagated, an assumed input, or the frame-to-frame noise every older producer quotes);
    // `warn` is true when the headline reading's `grossRisk` (the Impact sample's, else the worst
    // phase sample's) exceeds tuned::uncertainty::kGrossWarn —
    // the probability the reading rests on the tracker following the wrong structure, reported
    // BESIDE σ and never folded into it. Same marshalling cost as seriesSigma: resolve it once per
    // card, on a binding that changes with the data.
    Q_INVOKABLE QVariantMap sigmaInfo(const QVariantMap &series) const;

    // formatValue = the number and its unit, for a surface with no header to lean on (legend
    // chips, the hover tooltip, the transit bead). Degrees keep the signed-deviation convention
    // ("+12°", closed up); every other unit takes a space ("75 mph", "12 %").
    //
    // Rounded to whole units, ties away from zero (llround). σ does not touch it — see the note
    // above seriesSigma.
    Q_INVOKABLE QString formatValue(double v, const QString &unit) const;

    // formatBare = the number ALONE, for a surface whose container already names the unit — the
    // summary card's header, the split-mode gutter. Same sign convention as formatValue, so one
    // reading does not change shape depending on where it is shown.
    Q_INVOKABLE QString formatBare(double v, const QString &unit) const;

    // Every "± x" on the panel: the series σ chip beside the card's unit, and the ± beside PEAK
    // (summaryMasked's `peakSigma`) and PK RATE (`rateSigma`). Returns the whole displayed string,
    // "± " included, so what a reader sees is one testable value; unsigned, because an uncertainty
    // has no direction. `unit` appends a display token with formatValue's spacing (degrees close up,
    // everything else spaced); EMPTY means no token, which is what the two tiles want because the
    // card names their unit above them — note that this is the OPPOSITE of formatBare/formatValue,
    // where an empty unit falls back to degrees.
    //
    // ── UNCERTAINTIES ARE QUOTED ─────────────────────────────────────────────────────────────────
    //
    // The σ step (removed 3 Oct, see above) once governed readings, and never had any business
    // touching a ±. An uncertainty is not a reading of the athlete taken at some precision, it is
    // the statement of how far the reading can be trusted, and it is only ever read AGAINST the
    // value beside it — so it is QUOTED at a fixed one decimal and never quantised:
    //   · one decimal, always ("± 2.4", "± 12.4");
    //   · "± <0.1" below 0.05, which is also where an exactly-zero `err` goes;
    //   · never "± 0.0", and never "± 0".
    //
    // Three concrete things went wrong when the step DID govern this, all found in review:
    //   · PK RATE — a fitted-slope standard error of 3.0 printed "± 5" on a series whose σ chose a
    //     5-unit step, inflating the stated uncertainty by two thirds for a reason belonging to
    //     another quantity entirely (the σ is in the metric's unit; a slope's error is per 100 ms).
    //   · PEAK — peakSigma is about σ/√k for a k-sample window, so it is SMALLER than σ by
    //     construction and a step chosen from σ rounded it to nothing on essentially every card;
    //     the "fall back to one decimal" branch was the only one that ever ran, and a rule whose
    //     main branch is unreachable is a rule that is not doing what it says.
    //   · THE σ CHIP — the plumb bob's own σ is 0.03–0.06 in, so quantising it printed "± 0.0in",
    //     which claims exactness in the one place on the card whose entire job is to deny it.
    // Quoting removes all three at once, and removes the discontinuity with them: an err of 2.4 and
    // an err of 2.6 now print "± 2.4" and "± 2.6" instead of jumping "± 0" → "± 5" across half a
    // step. There is no `sigma` parameter, deliberately — not an ignored one, an absent one, so a
    // future caller cannot reintroduce the coupling by passing it.
    //
    // An empty string for a non-finite `err` (NaN, or a ±inf from a degenerate fit), which the
    // caller renders as nothing rather than as "± nan".
    Q_INVOKABLE QString formatUncertainty(double err, const QString &unit = QString()) const;

    // "Nice" Y-axis tick values across [lo, hi] at a 1/2/5×10ⁿ step chosen so there are
    // about `maxTicks` of them. Returns the tick values (doubles) the chart labels + grids.
    Q_INVOKABLE QVariantList niceTicks(double lo, double hi, int maxTicks) const;

    // X-axis tick offsets in milliseconds relative to impact, for the domain
    // [domStartUs, domEndUs]. Each returned int `ms` marks a gridline at impactUs+ms*1000
    // that falls inside the domain; the step widens with the span. The chart labels them
    // "(+)ms" and positions each via its own xForT(impactUs + ms*1000).
    Q_INVOKABLE QVariantList timeTicksMs(qint64 domStartUs, qint64 domEndUs,
                                         qint64 impactUs) const;

    // Phase enum of the station nearest `us` (or -1 when `phases` is empty). Used to label
    // a free-dragged ("Custom") window with the phases bracketing its edges.
    Q_INVOKABLE int nearestPhase(const QVariantList &phases, qint64 us) const;

    // Band ("good"/"attention"/"warn") of the phaseSample nearest `us`, used to tint a summary
    // card's @impact value by the swing's state there.
    //
    // Returns "" — NO BAND, tint neutrally — when the list is empty or the nearest sample is
    // more than `kBandNearUs` (one generous frame) from `us`. It used to return "good" in both
    // cases, and that is a graded verdict invented out of nothing: a series whose producer
    // emitted no sample at impact (because the geometry was gated there, which is exactly what
    // design §5.1 makes happen) had its @impact reading tinted GREEN off an empty list, or off
    // the Address sample 900 ms away. The caller must treat "" as "no verdict", not as a pass.
    Q_INVOKABLE QString bandAtNearest(const QVariantList &phaseSamples, qint64 us) const;

    // ── Corridor-bar backing (dashboard_reductions.h) ───────────────────────────

    // The value→x domain of ONE corridor bar (NormativeBar):
    //   { lo, hi, valid }
    // Two-sided: the amber band padded 12% each side, falling back to green then to
    // value±1. One-sided: the open side runs past the furthest of (aspiration, reading)
    // by 35% of the graded span, leaving the room the caller fades the band across —
    // without it a floor's Ideal readings all clamp to the last pixel of the track.
    // valid=false when there is neither a corridor nor a finite reading; the bar then
    // draws its rail and no bands, rather than a band pinned to the left edge.
    Q_INVOKABLE QVariantMap barDomain(double greenLo, double greenHi,
                                      double amberLo, double amberHi,
                                      bool lowOpen, bool highOpen,
                                      double value, bool hasValue) const;

    // ── Chart metric presets ────────────────────────────────────────────────────
    //
    // The swing's series bucketed by the catalogue's `.group` — the combo in the chart's
    // CONTROLS section. `seriesList` is analysisDetail.series; returns
    //   [{ group:QString, keys:[QString…] }]
    // in MANIFEST order (MetricCatalogue::all()'s order, which is also the order the Metric
    // Library lists groups in, so the two surfaces agree).
    //
    // Only SHOWABLE series count: a curve of at least two samples, OR a series carrying at least
    // one phaseSample. The first is the chart's own `_plottable` test; the second is a metric that
    // has no curve but a reading at an instant (attack angle, hand path loop, every `lm.` number),
    // which since 2026-10-01 gets a SUMMARY CARD on its group's preset — so a group made only of
    // those is a card panel with an empty plot, and is offered. A series with neither is nothing
    // to show and does not count.
    //
    // A group is present only when this swing produced at least one of its members, which is
    // what makes the control degrade honestly: no IMU wrist data and there is simply no "Wrist
    // & forearm" preset. That gating is on the DATA, never on the session type.
    //
    // After the groups come the CROSS-CUTTING presets — MetricDescriptor::presets, which is how a
    // coaching read that spans groups ("Plumb Bob" = the hip centre over the stance plus the tilt
    // of the hip line plus pelvis sway) gets one entry without any of its members leaving the group
    // it is properly filed under. A preset is offered only when at least TWO of its members are
    // plottable on this swing: one curve is a legend chip, not a preset.
    //
    // Curve keys the catalogue has never heard of are collected into a trailing "Other" group
    // rather than dropped: a metric added to the pipeline before the manifest should be awkward
    // to find, not invisible.
    Q_INVOKABLE QVariantList seriesGroups(const QVariantList &seriesList) const;

    // Does this metric's curve belong on the chart? Straight off MetricCardSpec::drawsCurve
    // (metric_descriptor.h says why): false for a metric read only at fixed instants — shaft lean,
    // shaft lie, x-factor at the top … — whose card stands without a trace or a legend chip. An
    // uncatalogued key draws, as it always did: unknown is not a licence to hide a curve.
    Q_INVOKABLE bool drawsCurve(const QString &key) const;

    // ── The kinematic-sequence strip (design kinematic_sequence_design.md §8) ─────────────────
    //
    // `ks` is analysisDetail.kinematicSequence — the map kinematic_sequence_json.h writes on all
    // three payload paths: { impactUs, nodes[], order[], gapsMs[], gainsDps[], orderResolved,
    // verdict, routeSummary, pelvisDecelerates? }. The strip under the chart's "Kinematic sequence"
    // preset is nothing but these three answers laid out, and all three live here rather than in
    // the .qml for the reason this class exists: they are DERIVATION (an ordering walk, a string
    // rule) and the "no JavaScript logic in QML" rule keeps derivation out of the components — and
    // a QML function cannot be asserted anywhere, where chart_metrics_test asserts every row and
    // every string below.
    //
    // sequenceRows — the chips, in display order: first the PLACED nodes in the sequence's own
    // `order` (ascending peak time), then the nodes the route produced but could NOT place (a σ
    // wider than the placement threshold) so the reader sees that the segment was attempted and
    // from which view. Each row:
    //   { segment, label, placed, beforeImpactMs, tSigmaMs, peakDps, peakSigmaDps, routeId,
    //     quality ("direct"|"estimated"), method (routeMethodName slug), glyph ("I"|"T"|"P"),
    //     gapMs (to the NEXT placed chip, −1 on the last placed and on every unplaced row),
    //     atImpactRising, beforeText ("−87 ms"), sigmaText ("±19 ms"), peakText ("480 °/s"),
    //     gapText ("+19 ms" or ""), unplacedText }
    // The label vocabulary is the coach's ("Chest", not "Thorax"); the segment key stays the
    // catalogue's. An empty / invalid map returns an empty list.
    //
    // `unplacedText` says WHY, and since 2026-09-20 it distinguishes two different silences:
    //   · "peaked after −87 ms, out of this camera's sight" — the face-on trunk route's blind
    //     band (§12.4): the segment squared up and the span stopped carrying the rate;
    //   · "still accelerating at impact" — `peakNoEarlierThanMs` ≤ 0, which is what the PAIRED
    //     route (faceOn+dtl, no blind band) emits when the trunk rate is still climbing where the
    //     sequence domain ends. `atImpactRising` is that case as a flag. Nothing went out of
    //     sight; the body had not peaked by the time the club reached the ball, and on the one
    //     golfer measured (pair_span_turn_20260920.md) that is the answer on every swing.
    Q_INVOKABLE QVariantList sequenceRows(const QVariantMap &ks) const;

    // sequenceVerdictText — the ONE categorical sentence the normative reference calls robust
    // (golf_swing_normative_reference.md §2.3), and nothing graded:
    //   proximalToDistal → "pelvis → chest → arm → club"
    //   armBeforeThorax  → "arm peaks before chest"
    //   partial          → "placed nodes in order (n of 4)"
    //   other            → "out of order"
    //   unresolved       → "order not resolved at this fidelity — add a pelvis IMU or a second camera"
    //   anything else / empty map → ""
    // NR-03 is why the unresolved text names the upgrade rather than a number: the sequence's own
    // σ withheld the order, and the honest next step is a better route, not a wider corridor.
    //
    // ONE CASE OVERRIDES THE TABLE (2026-09-20). When a trunk node is `atImpactRising` and the arm
    // or the club is placed BEFORE impact, the line is the pattern instead of the count:
    //   "arms and club peak before the body — hips and chest still speeding up at impact"
    // …narrowed truthfully when only one segment of either pair qualifies ("the club peaks before
    // the body — chest still speeding up at impact"). "placed nodes in order (2 of 4)" is true
    // about such a swing and says nothing about it; this is the reading, and it is the inverse of
    // the professional signature the metric's howToRead sells.
    Q_INVOKABLE QString sequenceVerdictText(const QVariantMap &ks) const;

    // sequenceRouteText — how the nodes were obtained, for the strip's header suffix:
    //   "measured" (every placed node Direct), "estimated from the camera" (none Direct), or
    //   "mixed: pelvis, chest measured · arm, club estimated" — the per-segment split, because
    //   "mixed" alone tells a reader with one IMU nothing about which chip to trust.
    //   "" when the map carries no nodes.
    Q_INVOKABLE QString sequenceRouteText(const QVariantMap &ks) const;

    // sequenceOverlay — the sequence drawn ON THE PLOT rather than restated under it (2026-09-18):
    // the peaks are points on curves the preset already strokes, so they belong where the reader's
    // eye already is. Returns
    //   { peaks: [{ segment, seriesKey, label, placed, atImpactRising, tPeakUs, peakDps, tSigmaUs,
    //            text }] — EVERY node the routes found a peak for, the placed ones first in the
    //            sequence's order, then the rest (drawn dimmer: the curve's own style already says
    //            how far to trust it, and a reader of this chart needs no second telling); text is
    //            "Arm −87 ms". `atImpactRising` marks the paired route's "had not peaked by
    //            impact": the plot draws THAT one as an open chevron at the impact edge of its own
    //            curve, pointing up-right, text "rising", and no σ whisker — a dimmed ring at its
    //            recorded tPeakUs would sit on the ball and read as a peak at impact, which is the
    //            one claim the node was emitted to decline;
    //     gaps:  [{ fromUs, toUs, text ("+22 ms") }] — between consecutive PLACED peaks;
    //     chainText: the placed nodes in order with their leads — "Lead arm −87 ms → Club −4 ms
    //            (+83 ms)" — so a split view, which cannot bracket a lead between facets, still
    //            reads the order in one line; "" when nothing is placed. }
    // seriesKey is the catalogue key of the segment's rate series (pelvisAngularSpeed …), which is
    // how the plot finds the curve and its colour. Empty / invalid map ⇒ empty lists and "".
    Q_INVOKABLE QVariantMap sequenceOverlay(const QVariantMap &ks) const;

    // sequenceTable — the SEQUENCE TILE under the chart (2026-10-02). It MIRRORS THE PLOT: one
    // row per segment in the order they SHOULD peak (pelvis, chest, arm, club), and every node
    // the plot marks (a ring, dimmed when unplaced, or a "rising" chevron) carries its numbers
    // with their σ. Unplaced peaks are shown, not hidden — the reader can see them on the curve —
    // and their rank carries a "?". Returns
    //   { rows: [{ segment, label, shouldRank, actualRank ("2" | "2?" | "—"), placed, outOfTurn,
    //              peakText ("−107 ±22 ms" | "rising" | "—"), speedText ("890 ±116" | "≥ 640"),
    //              pctText ("91% ±12" | "≥ 134%") }],
    //     verdictText ("In sequence" | "Out of sequence" | "In order so far" | "Order unclear",
    //                  + " (? = uncertain)" when an unplaced peak is in the ranking),
    //     verdictState ("match" | "mismatch" | "incomplete" | "unresolved") }
    // Rising nodes rank after every ring. outOfTurn marks both ends of an inverted pair. % is of
    // the Cheetham 2008 professional peak (NR-03/05 amended at Mark's call).
    Q_INVOKABLE QVariantMap sequenceTable(const QVariantMap &ks) const;

private:
    pinpoint::analysis::MetricCatalogue m_catalogue;   // built once in the ctor; never mutated
};
