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

#include "measure_vocabulary.h"    // Measure
#include "../../Vocabulary/Analysis/phase_grid.h"   // the grid's value types (P-3)
#include "../../Core/pp_tuned_constants.h"
#include "../../Core/sidecar_policy.h"   // SidecarPolicy: what a miss may cost (3.7-0)
#include "../../Metrics/metric_reducer.h"
#include "../../Vocabulary/Analysis/gate_record.h"   // GateRecord — why a reading is absent (schema 8)

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <limits>
#include <optional>
#include <vector>

// Reading a MEASURE off a swing that is already on disk.
//
// This is the missing half of norm_measure_source.h. `IMeasureValueSource` declares the seam — "a
// value source knows what the swing did and knows nothing about what is normal" — but until now
// nothing implemented it outside a test fake, so nothing in the app could put a real number next to
// a norm. The corridor editor's live histogram is not decoration: a corridor grading almost
// everything Action is only visibly wrong if there are real swings drawn beneath it.
//
// ── Why a phase grid and not the measures themselves ────────────────────────────────────────────
//
// The obvious cache is "measureId -> value per swing". It is the wrong one: the pack is EDITABLE,
// so minting a measure would stale every sidecar in the library and force a full re-parse of a
// 2 GB corpus to look at one new corridor.
//
// A measure is a metric series reduced at phases, and the phases are a property of the SWING, not
// of the pack. So the cache stores the grid the reduction reads — per metric, the value at each
// segmented phase plus the extremes between adjacent phases — and the reduction happens in memory.
// New measures over already-produced metrics then cost nothing, which is exactly the case the
// editor creates.
//
// ── Why adjacent-phase spans ───────────────────────────────────────────────────────────────────
//
// An Extremum reducer searches a window for a peak, which the phase values alone cannot answer: a
// pelvis that sways back and recovers before the next phase has a peak no endpoint sees, and that
// is the entire reason Extremum is first-class (metric_reducer.h). Storing min/max between each
// pair of ADJACENT segmented phases makes any phase-bounded window exact by aggregation, at a cost
// linear in phases rather than quadratic.
//
// "EXACT BY AGGREGATION" RESTS ON ONE RULE, and it is the reducers' rule, not this module's:
// A CANDIDATE'S SUPPORT IS QUERY-INDEPENDENT. reduceExtremum scores a sample over every VALID sample
// within its window (widening unclamped until the sample floor is met) — the neighbourhood comes
// from the CURVE, never from the search bounds. So the union of the spans' candidate sets is the
// whole window's candidate set, every candidate scores the same in a span as in a whole-window
// query, and min/max being idempotent makes the shared boundary sample harmless. That is why this
// cache can answer a multi-span window at all, and why it answers what the review chart answers.
//
// The rule was briefly broken by clamping a candidate's support to [from, to], to stop an extremum
// borrowing samples from outside a metric's domain. Measured on rich_7iron that cost 20 of 514
// engine-vs-card agreements (worst 1.45 on clubheadSpeed P1→P10 max), and it has no per-span fix:
// under a query-dependent support a sample 10 ms before a phase is scored over a truncated window
// inside its span and a full one inside the whole query, so no cached span is reusable.
//
// THE DOMAIN IS A MASK, NOT A CLAMP — that is the fix, and it belongs to the producers. A metric
// narrower than the swing marks the samples outside its Address→Impact domain `valid = 0` in
// swing.json (design §5.1), and both consumers already honour that mask: an out-of-domain sample is
// then neither a candidate nor part of anyone's support, at every window, without any reducer
// needing to know where a domain ends. See the mask note in buildPhaseGrid below.
//
// ── Sampling convention ────────────────────────────────────────────────────────────────────────
//
// A SHORT WINDOWED MEDIAN about each phase timestamp — the same convention WristAngleSampler
// already uses, from the same tuned constants. Deliberately not a second convention: the wrist grid
// and a norm seated from the corridor editor must be reading the same number off the same swing, or
// the two surfaces disagree about what the golfer did.
//
// The arithmetic itself is not written here any more: both the phase values and the span extremes
// come from `src/Metrics/series_reduce.h`, the one implementation the REVIEW CHART also reduces
// through (design §5.2). That is not tidiness — a corridor is authored by looking at the chart, so a
// card and an engine that reduced the same curve with two hand-rolled loops would eventually
// disagree about the number the corridor was drawn around, and nothing on screen would say so.
//
// Where the curve has nothing in the window, the metric's own `phaseSamples` reading is used
// instead. That fallback is not a nicety: an entire CLASS of metric ships with an empty curve and
// nothing but phaseSamples — every setup metric in a real swing.json (stanceWidth, ballPosition,
// tempoRatio, the foot-flare and toe-line angles) is read once at a position and has no curve to
// sample. Reading only the curve produced nothing for all of them, and the symptom is
// indistinguishable from "no swing carries this measure".

namespace pinpoint::analysis {

// The schema this build writes. A sidecar declaring a HIGHER version is ignored and rebuilt rather
// than partially read — it is a pure cache, so discarding it is always safe.
//
// 2: `club` now resolves through swingDocClub(), since retired (review.club, else capture.club.name, else the
//    stub). A v1 sidecar cached the review-or-stub answer and its size+mtime guard still matches,
//    since the fix rewrote no swing.json — the version bump is what retires it.
// 3: `spans` min/max are the extremes of the CENTRED-WINDOW MEAN (±extremumWindowUs/2) instead of
//    the extremes of the raw samples (design §5.2). Every Extremum measure's value therefore moves,
//    typically toward the mean by about one σ, and a v2 sidecar's size+mtime guard still matches the
//    unchanged swing.json — so as with v1, the version bump is the ONLY thing that retires the
//    stale numbers. `values` are untouched: the same ±15 ms median, now computed by reduceAt().
// 4: spans are CLOSED at both ends, a zero-width span is not stored at all, and the endpoint-median
//    seed is gone from the aggregation (see PhaseGridSpan and reduceOverGrid). Together those three
//    were making the engine disagree with the review chart on 36 of 170 extremum cases on one corpus
//    swing, by up to 9.4° — and a cache is only worth having if it answers what the chart answers.
// 5: every value and span carries the measurement σ, gross risk and where the σ came from
//    (session_diagnostics_design.md §A8.3). Additive — the numbers are unchanged — but a v4 sidecar
//    has no σ to serve, so the bump retires it.
// 6: every metric carries the producer route it came from (`routeId`, `routeDirect`; the ratio
//    test's same-scale rule, trunk_imu_sequence_grf_design.md §10.7 E3). Additive again, and the
//    bump is NOT optional: a v5 sidecar serves no route, and two readings with no route are allowed
//    to form a ratio — so a stale cache would quietly permit exactly the cross-scale quotient the
//    rule exists to refuse.
// 7: a metric may carry `withheld` (MetricSeries::withheld — the camera club peak, KB-18): drawn but
//    not judged. Additive, and the bump is NOT optional for the same reason as 6: a v6 sidecar serves
//    no `withheld`, so a stale cache would let the club peak fire the very verdicts it must not.
// 8: the swing's gate records (`gates`, `gatesVersion` — analysis.gates, trust_and_gates_design.md §4.4):
//    why a measure the engine asks for is absent, so a finding it stopped says "not judged: pelvis peak
//    out of sight" rather than "metric not produced". Additive; the bump retires v7 sidecars, which
//    carry no records and would read every refusal as a plain missing metric.
// 9: a span's extreme at a MASKED END — a curve its producer masks after its last measured sample,
//    as clubhead speed and the Address→Impact channels are after impact — is read off the window's
//    own line at its anchor, clamped into the window's range (series_reduce.h windowMeanValueAt,
//    issue #1): clubhead speed's P6→P7 maximum on 2026-07-04 moves 80.1 → 82.3 mph, up to the impact
//    reading it used to sit below, and the Address→Impact channels' extremes at impact move too.
//    Every other value is unchanged to the bit, and `values` (the ±15 ms median) not at all — but a
//    v8 sidecar's size+mtime guard still matches the unchanged swing document, so as with 3 the bump
//    is the only thing that retires the stale extremes. The session's graded rows carry this number
//    in their content stamp for the same reason.
// 10: the two plane curves' `values` are the curve AT the phase instant, not a ±15 ms median about it
//    (fitted_curve.h, 8 Nov 2026). Other series are unchanged to the bit.
// 11: `club` is the shot's EFFECTIVE club, the one rule (club_vocabulary.h swingDocEffectiveClub; refactor 6.2,
//    R-CLUB-2): the board's correction wins, else review.club, else the device club follow-club stored, else the
//    captured club; empty when nothing declared it. Before, the board's correction never reached the grid, so a corrected
//    shot's findings were graded at the club it was recorded under. A v10 sidecar's guard still matches the unchanged
//    document of a shot corrected before this build, so the bump retires it, and through firingStamp it is the re-judge:
//    the library's findings fire once more on the new build's first launch (J-82).
inline constexpr int kPhaseGridSchemaVersion = 11;

// The grid's value types (PhaseGridMsWindow, PhaseGridConfig, PhaseGridValue, PhaseGridSpan, PhaseGridMsSpan,
// MetricPhaseGrid, SwingPhaseGrid) live in src/Vocabulary/Analysis/phase_grid.h (platform refactor P-3, R-1), so the
// Recording's reader contract names them without including the sampler.

// Every millisecond span the given measures read, deduplicated, in a stable order (anchor, start, end).
std::vector<PhaseGridMsWindow> msWindowsOf(const std::vector<Measure> &measures);
// `cfg` with `extra` merged into its msWindows (deduplicated, the same stable order).
PhaseGridConfig withMsWindows(PhaseGridConfig cfg, const std::vector<PhaseGridMsWindow> &extra);

// ── Building ────────────────────────────────────────────────────────────────
//
// From the `analysis` object of a swing.json — `metrics[]` (key/unit/t_us[]/value[]) and `phases[]`
// (phase/t_us/conf), the shapes swing_doc_analysis.cpp writes and wrist_analysis_adapter.cpp already reads.
//
// A metric may also carry `valid[]`, a 0/1 array parallel to `t_us` marking samples that were
// BRIDGED across a gated or absent run rather than measured (design §5.1). Those enter neither a
// phase's windowed median nor a span's extremes, and a phase whose whole window is bridged gets no
// entry — the same path an unsampled phase already takes. The key is written only when something is
// invalid, so a swing without it grids identically to a swing carrying an all-ones mask — which is
// what makes the mask additive. (It no longer means "identical to the previous release": schema 3
// moved every span. `values` are still identical either way, and that is what the mask tests pin.)
//
// The grid remembers the `cfg` it was built with, because the sidecar guard compares it.
SwingPhaseGrid buildPhaseGrid(const QJsonObject &analysis, const PhaseGridConfig &cfg = {});

// The grid of a whole swing document (its root): buildPhaseGrid over its `analysis`, with the swing's identity fields
// (its ordinal, its capture instant, its effective club) read off the document as the carousel reads them. Pure: the
// swing folder and session id are the caller's (PhaseGridStore, src/Export/phase_grid_store.h; platform refactor P-3).
SwingPhaseGrid phaseGridOfDocument(const QJsonObject &root, const PhaseGridConfig &cfg = {});

// ── Reduction ───────────────────────────────────────────────────────────────
//
// nullopt => this measure could not be produced FOR THIS SWING: no such metric, or a phase the
// reducer needs was never segmented. Never a zero, because "not assessed" and "assessed and fine"
// are different statements and the whole Diagnostics module exists to keep them apart.
//
// Extremum semantics, made explicit because metric_reducer.h's own comment is ambiguous: the result
// is the SIGNED deviation `extremum(window, sense) - value(anchor)`, not `max |value - anchor|`. An
// absolute deviation cannot carry a `sense`, and every anchored Extremum in the shipped pack means
// the signed reading — m_pelvisSwayBack is the most negative sway relative to address, not the
// largest excursion in either direction.
//
// An Extremum is the aggregate of the SPANS the window covers and nothing else. It used to be seeded
// with the two endpoint windowed medians as well, on the reasoning that a peak sitting on a phase is
// still a peak — but a ±15 ms median and a 40 ms windowed mean are two different smoothing scales,
// and mixing them in one min/max made the engine report numbers the chart cannot produce (36 of 170
// extremum cases on one corpus swing). The endpoints lie inside the closed spans anyway, so nothing
// is lost. The medians survive only as a LAST RESORT: when no span in the window answered at all —
// every interval zero-width or wholly bridged — min/max of the two endpoint values still beats
// darkening a measure whose value is sitting right there, and it is what the chart falls back to.
std::optional<double> reduceOverGrid(const SwingPhaseGrid &grid, const Measure &m);

// The same, for a bare metric key + reducer, so a caller with no Measure in hand (the sidecar
// tests, a future producer) reaches the identical arithmetic.
std::optional<double> reduceOverGrid(const SwingPhaseGrid &grid, const QString &metricKey,
                                     const Reducer &r);

// The same reduction with its measurement uncertainty (§A8.3). `value` is bit-identical to
// reduceOverGrid's; σ propagates as:
//   At       the value's σ
//   Delta    the endpoints' σ in quadrature
//   Rate     the endpoints' σ in quadrature over the elapsed time (timing σ NOT included — stated)
//   Extremum the WINNING span's σ (the one whose extreme was taken), ⊕ the anchor's σ when anchored
//   windowMs Extremum: the millisecond span's σ ⊕ the anchor's σ; Delta: the start's σ (the anchor's at
//            startMs 0, else the series σ) ⊕ the end's (the series σ) — an instant between phases
//            carries no labelled σ of its own
// Any term without a σ makes the whole reading unquantified (sigma NaN): a partial budget would
// claim a precision nobody measured. grossRisk is the largest of the terms', −1 when none had one.
struct GridReading {
    double      value     = 0.0;
    double      sigma     = std::numeric_limits<double>::quiet_NaN();
    float       grossRisk = -1.f;
    SigmaSource sigmaSrc  = SigmaSource::None;   // the WEAKEST source among the terms
    // The route of the metric that answered — the winning rung's, on a ladder.
    QString     route;
    bool        direct    = false;
    QString     withheld;   // the metric's MetricSeries::withheld
};
std::optional<GridReading> reduceOverGridReading(const SwingPhaseGrid &grid, const QString &metricKey,
                                                 const Reducer &r);
std::optional<GridReading> reduceOverGridReading(const SwingPhaseGrid &grid, const Measure &m);

// True when `m` reads a millisecond span, some rung of its ladder has the anchor's value and the span,
// and the span is too little covered to be read (maxGapUs over the limit, or nothing valid in it) — the
// reason LiveMeasureSource gives (MissingKind::WindowNotCovered) instead of an unsegmented phase.
bool msSpanUncovered(const SwingPhaseGrid &grid, const Measure &m);

// ── Sidecar ─────────────────────────────────────────────────────────────────
//
// <swingDir>/swing_phasegrid.json — a few KB beside a 32 MB swing.json. Guarded on the source
// file's size and mtime exactly as swing_summary.json is: a stale guard means rebuild, never a
// silently wrong number. Always safe to delete. The comparison is SwingStore::sameDocumentStamp's,
// the one rule every reader of the stamp uses: a copied or restored swing whose document kept its
// mtime only to the whole second is the same document, so its grid stays current (issue #174).
//
// The guard also carries the REDUCTION the grid was built under (both windows and the sample floor),
// because size and mtime cannot see it: sweep `extremumWindowUs` and every sidecar in the library
// still matches its swing.json byte-for-byte while holding numbers from the previous window.
// `loadPhaseGrid` therefore takes the configuration the caller intends to use and refuses a grid
// that was not built that way. The sidecar's path, read and write are its file door's (PhaseGridStore,
// src/Export/phase_grid_store.h; platform refactor P-3).

//
// The millisecond spans (PhaseGridConfig::msWindows) are a SUBSET test: a sidecar built for spans
// {A, B} serves a reader asking for {A}, or for none; a reader asking for C is refused (guardOk
// false, though the grid is still filled: its numbers are current, only C is missing), and
// `storedMsWindows` (when given) reports what the sidecar held, so the door (PhaseGridStore::read) can rebuild with the
// union — or, when it may not fat-parse, serve the filled grid without C. A sidecar written before the spans existed holds none, so no schema bump was needed: every
// reader that names none still accepts it, and every reader that names one rebuilds it.
QJsonObject    savePhaseGrid(const SwingPhaseGrid &grid, qint64 sourceSize, qint64 sourceMtimeMs);
SwingPhaseGrid loadPhaseGrid(const QJsonObject &root, qint64 sourceSize, qint64 sourceMtimeMs,
                             bool *guardOk = nullptr, const PhaseGridConfig &cfg = {},
                             std::vector<PhaseGridMsWindow> *storedMsWindows = nullptr);

// The model browser's folder read of a swing's grid (the corridor scan, FILE-SHOT-07's recorded exception, and the gate
// count's fallback) is the model browser's own: src/Gui/diagnosticmodel/corridor_grid_read.h (B1.14, R11). The sampler
// does no file work and includes no repository.

} // namespace pinpoint::analysis
