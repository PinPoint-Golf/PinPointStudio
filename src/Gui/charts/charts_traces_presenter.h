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

#include "session_series_types.h"
#include "session_traces.h"
#include "session_warp.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

class ChartMetrics;
class TimelineLabels;

// THE CHARTS PANEL'S TRACES PRESENTER — direction A, "the instrument" (docs/design/
// charts_panel_traces_relations.md §2.1 This shot + Against + the sequence chain, §2.2 Session, §4 the
// group fit, §4.1 alignment; build/charts-build/CONTRACT.md and its Addendum: every single-shot
// capability of today's panel, F1–F45 of build/charts-build/INVENTORY.md).
//
// Every number and every sentence variant A's mock computed in JavaScript (variant_a/lib/
// TraceModel.qml, rows.js, TracesPage.qml, SeqChain.qml, SeqSession.qml, traces.js) is computed here,
// and the single-shot ones by the code today's panel already uses (ChartMetrics: windowedMean,
// summaryMasked, domainFor, cardSpecFor, dataWindow, segments, sigmaInfo, sequenceTable, seriesGroups —
// called, never re-implemented). QML positions and paints; it reads only the properties below and
// calls only the invokables below.
//
// ── WHAT IT IS GIVEN ─────────────────────────────────────────────────────────────────────────
// `model` (a SessionSeriesModel), the reviewed shot (`pickedSwingDir` + `pickedDetail` =
// shotReplay.analysisDetail, so This shot works when the shot is not in the snapshot: a live shot
// arriving, another session, n < 3), `register`, and the reader's choices (persisted by the QML body
// in AppSettings, as today — NOT here): preset + metricsOff ("Custom"), window, band, facet, against,
// difference, view. `playheadUs` is the playhead on the picked shot's window-relative clock
// (shotReplay.positionUs); it drives only `now`.
//
// ── THREADING (STEP2A, the SessionSeriesModel pattern) ──────────────────────────────────────
// Any input change or snapshot change schedules ONE coalesced recompute (posted, so a burst of
// property writes in one event-loop turn costs one job) on a private single-thread pool. The job
// reads an immutable snapshot and the choices, and returns complete QVariantMaps that are swapped
// in by a queued invocation; `resultsChanged` fires once per delivered job. A change arriving while
// a job runs marks the presenter dirty and re-runs once the job lands. setSynchronous(true) runs
// the same job inline when an output is next read (tests).
//
// CACHES (worker side, in the job's own state): the alignment, the session sequence and the facets
// per snapshot revision; aligned curves per (key, revision); envelopes / group fits per (key, group,
// window); shape split and typical shots per (keys, window); every shot's windowed peak per (key,
// window). A choice change (band, facet, against) recomputes from those, not from the documents.
//
// PLAYHEAD: setPlayheadUs is a GUI-thread lookup into arrays cached with the last result (binary
// search per row, no QVariant marshalling of series), and `nowChanged` fires only when a printed
// value changed — never a recompute per frame.
//
// ── OUTPUT SHAPES (every row a QVariantMap with stable keys) ─────────────────────────────────
// A Sentence is words::Sentence::toVariant(): { text, runs: [{ text, fig }] } (empty text = none).
// Numeric arrays are QList<double> (QML sees a JS array; NaN = missing, draw nothing there).
// A TICK LIST is [{ v, text }]: the value and its figure text (U+2212 minus; time axes signed "+50").
// A BRIDGE LIST is [{ t0, v0, t1, v1 }]: a dashed straight line across a run of samples that are not
// measurements (bridged / non-finite), from the last measured sample before it to the first after it,
// on the curve's own axis. Inside such a run every value array is NaN: the placeholder a bridged
// sample carries (4 July shot 8's thorax: 0 °/s) is never published as a value.
// Figure texts join number and unit with a no-break space (U+00A0): "82 mph", "58 ms".
// Times: `shotMs` = ms from impact on the shot's own clock; `canonMs` = the session's P-aligned axis.
// Verdicts are Theme's names ("good" | "attention" | "warn" | "" = not judged) with their word;
// warn is a flag plus a word (`late: true, lateWord: "late"`), never a colour.
//
//  choices: {
//    register, view,
//    presets: [{ id, label, count }]              the catalogue's groups on this shot's data
//    preset, presetTitle, custom: bool            resolved preset; custom = a member switched off
//    metrics: [{ key, label, unit, on, curve }]   the preset's members (the legend chips; curve = draws a trace)
//    windows: [{ key, label, current, startUs, endUs, phaseA, phaseB }]
//              keys "full" | "swing" | "data" | "back" | "down" | "seg:<A>-<B>" | "custom" (only while current)
//    window,                                      resolved key ("auto" resolves to data / swing / full)
//    bands:   [{ id: "middle"|"every"|"fit", label, current }]
//    facets:  [{ id, label, kind: "none"|"condition"|"half"|"shape"|"club", n, current }]
//    against: [{ id, label, current, enabled, why }]   ids "none"|"reference"|"median"|"typical"|
//                                                       "leastTypical"|"previous"|"next"|"like"
//              ("reference" — the ★ reference swing, `referenceSwingDir`, any session — comes first
//               after "none": the picker lists it first)
//    difference: bool }
//
//  thisShot: {
//    available, empty: Sentence (S1/S2 when !available),
//    ordinal, shotId, swingDir, inSession, sessionCount, kicker ("Shot 8 of 15  ·  7 iron"),
//    head: Sentence, note: Sentence (the late-pelvis neutral note wherever the pelvis is said late),
//    standfirst: Sentence, figures: [{ label, text, warn, lateWord }] (Replay's key figures),
//    window: { key, label, header ("Top → Impact" | "Full recording"), spanText ("290 ms"),
//              metricsText ("4/4 metrics"), tagsText ("P4→P8"), startUs, endUs, t0Ms, t1Ms, phaseA, phaseB },
//    ticks: [{ phase, tag, name, shotMs, landmark: "top"|"impact"|"" }],   (P tags inside the window)
//    msTicks: [{ v (shotMs), text }],                                      (the window's ms axis)
//    recordingTicks: [{ phase, tag, us, landmark: "top"|"impact"|"" }],    (the brush: whole recording, µs)
//    overview: [{ key, t: QList<double> (µs, whole recording), v (the windowed mean, NaN = not
//               measured), vLo, vHi }]   (the brush's sparklines; ≤ 200 points each, min/max per bucket),
//    rows: [ROW], cards: [CARD] (the preset's members with no trace: instant-only, curve-less),
//    overlay: { yLo, yHi, yTicks: TICKS, sameUnit } (one shared Y for the overlay option, F6),
//    noTraces: Sentence (a preset with no drawable curve — Club delivery — S6; empty otherwise),
//    dotKey: [{ verdict, word }] (the P-dot kinds present, "position, not judged" for ""),
//    sequence: SEQ ({} when the preset carries no sequence curve or the shot no sequence) }
//  ROW = { key, label, unit, dec, segment ("" unless a sequence curve),
//          t, mean, raw: QList<double> (shotMs; the 40 ms windowed mean drawn; the raw dots;
//                 NaN inside a bridged run),
//          bridges: BRIDGES (dashed, across the bridged runs),
//          runs: [{ from, to, style: "measured"|"bridged"|"outside" }]  (inclusive index ranges:
//                 bridged = not a measurement: no values, draw its bridge; outside the metric's phase
//                 domain = dimmed),
//          yLo, yHi (measured samples only), yTicks: TICKS (over the window),
//          peaks: [{ shotMs, value, text, sigmaMs, valueSigmaText, placed, late, lateWord, rising, risingWord }]
//                 (rising: draw the open chevron at the impact edge, no ring, text "rising"),
//          dots:  [{ phase, tag, shotMs, value, verdict, verdictWord }]   (P dots, measured only),
//          tiles: [TILE], partial, partialWord ("partial"), windowEmpty,
//          figures: [{ label, text, warn }] (the mock's compact trio: peak ± σ, order / at, at P7),
//          sigmaText, sigmaTip, grossWarn, grossWord ("⚠"), grossTip,
//          sequence: { segment, word, placed, rank, rankText, late, lateWord, rising,
//                      peakText, speedText, pctText } | {} }
//  TILE = { label, text, ok, verdict, verdictWord, sub (the ±), unit, tip, window (PARTIAL speaks for it) }
//  CARD = { key, label, unit, tiles, partial, partialWord, sigmaText, sigmaTip, grossWarn, grossWord, grossTip }
//  SEQ  = { verdictText, verdictState, anyLate, chain: Sentence ("Lead arm — 58 ms → Club — 69 ms →
//           Pelvis, late"), links: [{ segment, word, late, lateWord, gapMs, gapText }],
//           note: Sentence (expected order; what was not placed and why), proNote,
//           table: [{ segment, label, shouldRank, actualRank, placed, late, lateWord, peakText,
//                     speedText, pctText, rising }] }   (% of pro, Cheetham 2008; rising chevron)
//
//  reference (Against): {
//    mode, active, refused, why: Sentence, label ("session median" | "shot 12, the most typical" |
//    "7 shots shaped like this one" | "the reference ★"), ordinal, shotId, swingDir, n, group: [ordinals],
//    refLabel ("shot 3, 18 Aug 2026" — "reference" only: which swing the ★ is),
//    head: Sentence (the largest gap said: "Shot 8 left your usual swing furthest 40 ms before
//    impact, in club angular speed"), difference: bool, diffLabel ("shot 8 − session median"),
//    rows: [{ key, t, v: QList<double> (THIS shot's clock, NaN = blank: fewer than 3 contributed, or a
//                                        reference shot's bridged run),
//             bridges: BRIDGES (a reference SHOT's bridged runs; [] for the median / a fit, whose blanks stay blank),
//             diff: QList<double> (this − reference on t), diffLo, diffHi (symmetric about 0, NaN when
//             nothing overlaps), gap: { found, shotMs, diff, text, when } }] }
//
//  session: {
//    available, why: Sentence (refused: n < 3, nothing aligned), n, aligned, kicker,
//    head: Sentence (where the curves part / the facet / the fit's payoff), standfirst: Sentence,
//    alignNote: Sentence (§4.1 routes), clubNote: Sentence (mixed clubs), caveat: Sentence (co-occurrence),
//    t0, t1 (canonMs), ticks: [{ phase, tag, canonMs, q1Ms, q3Ms, n, when ("−312 to −296 ms") }],
//    msTicks: TICKS (canonMs, the P-aligned axis),
//    axisCaption: string (the axis caption, from the alignment routes used: P-positions / landmarks /
//                 tempo-fitted / impact only, or the mix with counts),
//    band, facet, groups: [{ key, label, short, n, ordinals, own }],
//    rows: [{ key, label, unit, dec, yLo, yHi, yTicks: TICKS,
//             panels: [{ group, label, n, t: QList<double> (the canon grid),
//                        q1, med, q3 (middle/every; NaN where < 3 measured),
//                        others: [{ ordinal, shotId, swingDir, v, bridges }] (every: each shot on the
//                                 grid, NaN across its bridged runs; bridges on the canonical axis),
//                        fit: { v, lo, hi, nShots, refused, why, label } (fit: ± 1 sd band, D2),
//                        alt: { v, label, dashed } (the other group's median / fit) }],
//             picked: { ordinal, shotId, swingDir, t, v (NaN where bridged), runs, bridges } | {}
//                     (this shot, full strength),
//             coverage: [{ ordinal, shotId, swingDir, picked, runs: [{ t0, t1 }] }] (fit only, picked first),
//             part: { found, canonMs, q1, q3, med, when },
//             figures: [{ label, text }] }],
//    sequence: { present, stand: Sentence, lo, hi, ticks: TICKS (ms from impact, "+50"), pTicks: [{ tag, ms }],
//                segments: [{ segment, word, never,
//                             cells: [{ ordinal, shotId, swingDir, rank, text, late, picked }],
//                             points: [{ ordinal, shotId, swingDir, ms, late, picked }],
//                             median, q1, q3, placed, figText }] },
//    questions: [{ id, text, answer: Sentence, lit: [ordinals], setFacet, setBand }] }
//
//  now: { valid, us, shotMs, canonMs, when ("+12 ms"),
//         rows: [{ key, value, text, measured, addrText ("Δ +12°" from Address), typical,
//                  typicalText (session median at the playhead), refText, gapText (Against) }] }
//
// ── WHAT QML CALLS ───────────────────────────────────────────────────────────────────────────
// seekUsFor(ordinal, canonMs) / seekUsForShotMs(shotMs): the µs to hand shotReplay.seekToUs.
// shotAt(key, canonMs, value): the nearest shot's curve, for brushing. hoverAt(shotMs): the hover
// tooltip's rows. All four are lookups into cached arrays: call them on a pointer event, never in a
// binding that re-evaluates per frame.
namespace pinpoint::charts {

class ChartsTracesPresenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── inputs ──
    Q_PROPERTY(QObject *model READ model WRITE setModel NOTIFY inputsChanged)
    Q_PROPERTY(QString pickedSwingDir READ pickedSwingDir WRITE setPickedSwingDir NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap pickedDetail READ pickedDetail WRITE setPickedDetail NOTIFY inputsChanged)
    Q_PROPERTY(QString register READ registerName WRITE setRegisterName NOTIFY inputsChanged)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY inputsChanged)           // "shot" | "session" | "" (both)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY inputsChanged)     // "" = the register's default
    Q_PROPERTY(QStringList metricsOff READ metricsOff WRITE setMetricsOff NOTIFY inputsChanged)
    Q_PROPERTY(QString window READ window WRITE setWindow NOTIFY inputsChanged)     // "auto" default
    Q_PROPERTY(qint64 customStartUs READ customStartUs WRITE setCustomStartUs NOTIFY inputsChanged)
    Q_PROPERTY(qint64 customEndUs READ customEndUs WRITE setCustomEndUs NOTIFY inputsChanged)
    Q_PROPERTY(qint64 spanUs READ spanUs WRITE setSpanUs NOTIFY inputsChanged)      // shotReplay.endUs − startUs
    Q_PROPERTY(QString band READ band WRITE setBand NOTIFY inputsChanged)           // "middle" | "every" | "fit"
    Q_PROPERTY(QString facet READ facet WRITE setFacet NOTIFY inputsChanged)        // "" | "half" | "shape" | "club" | conditionId
    Q_PROPERTY(QString against READ against WRITE setAgainst NOTIFY inputsChanged)
    Q_PROPERTY(bool difference READ difference WRITE setDifference NOTIFY inputsChanged)
    // ADDED 15 Oct (build/wrist-tab): the ★ reference swing for Against "reference" — a swing dir in ANY
    // session (QML binds appSettings.wristReferenceSwingDir). Read off the GUI thread through
    // SwingStore::loadAnalysis + SessionSeriesModel::extractShot, cached by its document stamp, and lined
    // up on the picked shot's own clock by their shared P-positions (warpAgainst). "" = none set.
    Q_PROPERTY(QString referenceSwingDir READ referenceSwingDir WRITE setReferenceSwingDir NOTIFY inputsChanged)
    Q_PROPERTY(qint64 playheadUs READ playheadUs WRITE setPlayheadUs NOTIFY playheadChanged)

    // ── outputs ──
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap choices READ choices NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap thisShot READ thisShot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap reference READ reference NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap session READ session NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap now READ now NOTIFY nowChanged)

public:
    explicit ChartsTracesPresenter(QObject *parent = nullptr);
    ~ChartsTracesPresenter() override;

    QObject *model() const;
    void setModel(QObject *m);
    QString pickedSwingDir() const { return m_in.pickedSwingDir; }
    void setPickedSwingDir(const QString &d);
    QVariantMap pickedDetail() const { return m_in.pickedDetail; }
    void setPickedDetail(const QVariantMap &d);
    QString registerName() const { return m_in.reg; }
    void setRegisterName(const QString &r);
    QString view() const { return m_in.view; }
    void setView(const QString &v);
    QString preset() const { return m_in.preset; }
    void setPreset(const QString &p);
    QStringList metricsOff() const { return m_in.metricsOff; }
    void setMetricsOff(const QStringList &k);
    QString window() const { return m_in.window; }
    void setWindow(const QString &w);
    qint64 customStartUs() const { return m_in.customStartUs; }
    void setCustomStartUs(qint64 us);
    qint64 customEndUs() const { return m_in.customEndUs; }
    void setCustomEndUs(qint64 us);
    qint64 spanUs() const { return m_in.spanUs; }
    void setSpanUs(qint64 us);
    QString band() const { return m_in.band; }
    void setBand(const QString &b);
    QString facet() const { return m_in.facet; }
    void setFacet(const QString &f);
    QString against() const { return m_in.against; }
    void setAgainst(const QString &a);
    bool difference() const { return m_in.difference; }
    void setDifference(bool on);
    QString referenceSwingDir() const { return m_in.referenceSwingDir; }
    void setReferenceSwingDir(const QString &d);
    qint64 playheadUs() const { return m_playheadUs; }
    void setPlayheadUs(qint64 us);

    bool busy() const { return m_running; }
    QVariantMap choices();
    QVariantMap thisShot();
    QVariantMap reference();
    QVariantMap session();
    QVariantMap now();

    // ── what QML calls (lookups into cached arrays; never per frame) ──
    // The µs on `ordinal`'s own window-relative clock of canonical time canonMs (−1 when unknown).
    Q_INVOKABLE qint64 seekUsFor(int ordinal, double canonMs) const;
    // The µs on the picked shot's clock of shotMs (This shot's axis) (−1 when there is no impact).
    Q_INVOKABLE qint64 seekUsForShotMs(double shotMs) const;
    // The canonical time of the picked shot's own shotMs (NaN when it is not aligned).
    Q_INVOKABLE double canonMsForShotMs(double shotMs) const;
    // The shot whose aligned `key` curve passes nearest `value` at canonMs:
    //   { found, ordinal, shotId, swingDir, value }.
    Q_INVOKABLE QVariantMap shotAt(const QString &key, double canonMs, double value) const;
    // This shot's hover tooltip at shotMs: { shotMs, when ("+12 ms · impact"), rows: [{ key, label,
    // text (the mean, "-" where not measured), raw ("raw 81" | ""), measured, grossWarn }] }.
    Q_INVOKABLE QVariantMap hoverAt(double shotMs) const;

    // ── test and measurement seams ──
    // A snapshot given directly, used while no SessionSeriesModel is set (tests; the fixture).
    void setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot);
    Q_INVOKABLE void setSynchronous(bool on);
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);
    // The last job's cost: { jobMs, thisShotMs, referenceMs, sessionMs, cacheHit, revision }.
    Q_INVOKABLE QVariantMap lastTimings() const { return m_timings; }

    struct Inputs {
        std::shared_ptr<const SessionSnapshot> snapshot;
        QString pickedSwingDir;
        QVariantMap pickedDetail;
        QString reg = QStringLiteral("analyse");
        QString view;
        QString preset;
        QStringList metricsOff;
        QString window = QStringLiteral("auto");
        qint64 customStartUs = -1, customEndUs = -1, spanUs = -1;
        QString band = QStringLiteral("middle");
        QString facet;
        QString against = QStringLiteral("none");
        bool difference = false;
        QString referenceSwingDir;
    };
    struct NowRow;
    struct NowCache;
    struct Result;
    struct Worker;

signals:
    void inputsChanged();
    void busyChanged();
    void resultsChanged();
    void nowChanged();
    void playheadChanged();

private:
    void schedule();
    void startJob();
    void apply(std::shared_ptr<Result> r);
    void flushIfSynchronous();
    void updateNow(bool force);
    void onSnapshotChanged();

    Inputs m_in;
    QPointer<QObject> m_model;
    std::shared_ptr<const SessionSnapshot> m_directSnapshot;
    std::shared_ptr<const SessionSnapshot> currentSnapshot() const;
    qint64 m_playheadUs = -1;

    bool m_synchronous = false;
    bool m_dirty = true;          // inputs changed since the last job started
    bool m_scheduled = false;     // a startJob is posted
    bool m_running = false;       // a job is on the pool

    std::shared_ptr<Result> m_result;   // the last delivered result (GUI thread only)
    QVariantMap m_now;
    QVariantMap m_timings;

    std::shared_ptr<Worker> m_worker;   // the worker's caches; touched only by jobs
    QThreadPool m_pool;
};

} // namespace pinpoint::charts
