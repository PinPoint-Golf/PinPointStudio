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

// THE RELATIONS PRESENTER — direction B of the Charts panel, "the workbench", as numbers and sentences
// the QML only positions and paints (docs/design/charts_panel_traces_relations.md §2.1 KeyFigures and
// "Apart from the session", §2.3 Relations, §2.4 Kinds, §3 Replay Session; build/charts-build/STEP2A.md).
// Every number and every sentence variant_b's mock computed in JS (Bench.qml, Words.qml, Analyse.qml,
// Replay.qml and lib/*.qml) is computed here; the arithmetic itself is the session maths'
// (session_relations.h, session_traces.h, session_warp.h, session_sequence.h) and is never repeated.
//
// ── THE INPUTS ───────────────────────────────────────────────────────────────────────────────
// `model` (a SessionSeriesModel), `pickedSwingDir` (= shotReplay.swingDir), `register` ("replay" |
// "analyse"), and the reader's choices: `family` (a catalogue group or preset name; "" = the register's
// default, Club delivery in Replay, Kinematic sequence in Analyse), `upId` / `acrossId` (reading ids
// "key@phase"; "" = the defaults: clubhead speed at P7 up, its strongest partner that is not the same
// quantity across), `facetId` ("" none, "half", "club", "shape", or a condition id), `openId` (the
// profile row opened in place), `playheadUs` (shotReplay.positionUs, the picked shot's window-relative
// clock) and `pickedDetail` (the picked shot's analysisDetail, used ONLY while that shot is not yet in
// the snapshot — a live shot arriving: it is added to the session for this presenter so This shot still
// speaks for it). The choices are persisted by the QML body, never here.
//
// ── READ-AT INSTANTS ─────────────────────────────────────────────────────────────────────────
// A reading is key@phase at the instants the metric's CARD reads it: MetricDescriptor::card.readAt of the
// catalogue (pinpoint::analysis::makeMetricCatalogue — the C++ under ChartMetrics::cardSpecFor), the
// default card's impact for an uncatalogued key, filtered by sessionReadings() to the phases some shot
// sampled. Family = the descriptor's group (manifest order); presets (MetricDescriptor::presets) are
// offered as families too when two or more of their keys have readings.
//
// ── FIRED ────────────────────────────────────────────────────────────────────────────────────
// Only from the condition link, never by matching values: per shot, a fired condition marks the reading
// its ShotDiagnostics::drivenBy names (the reading that drove it on that shot), else the reading
// ConditionInfo names; a link with phase −1 lands only on a key read at one instant. A link whose
// key@phase is not a reading marks nothing (it is listed in thisShot.firedElsewhere for the picked shot).
// The condition is said with how it read the reading (ConditionInfo / drivenBy `reducer`): "at P7" for an
// At link, "peak before P7" for an extremum (the peak lies inside the window ENDING at P7), "change to P7"
// for a delta, "rate of change to P7" for a rate. Every kind marks the key@phase reading; the word says
// what was graded there. thisShot.firedElsewhere rows carry { reducer, instant } too.
//
// ── THREADING AND COST (STEP2A) ──────────────────────────────────────────────────────────────
// Everything but the playhead runs on one private single-thread pool, coalesced (a change while a job
// runs queues ONE re-run with the latest inputs), delivered by queued invocation; `busy` meanwhile, and
// the previous results stay published. Cached on the worker by snapshot revision (readings, fired links,
// kinds, sequence, alignment, drifts, families), per key (aligned curves, kind fits) and per Up reading
// (the ledger). `playheadUs` is a GUI-thread lookup into results already computed (one entry per
// P-position the lead reading is read at) and emits playheadChanged only when what it shows changes.
// setSynchronous(true) runs the job inline (tests).
//
// ── CONVENTIONS OF EVERY OUTPUT ──────────────────────────────────────────────────────────────
//   · A Sentence is { text, runs: [{ text, fig }] } (charts_words.h Sentence::toVariant()).
//   · A number meets its unit across U+00A0 (charts_words.h figUnit / signedMs, and every join made here:
//     "95 %", the slope's "+1.3 mph"), so a line never breaks between them.
//   · A missing number is null in a number field and "-" in a text field; never 0.
//   · A SHOT MARK carries { ordinal, shotId, swingDir } so the body can emit shotRequested.
//   · `values` lists are one entry per shot of the session in strike order (index = ordinal − 1), null
//     where unmeasured. `box` = { n, min, max, q1, med, q3, lo, hi, outliers: [ordinal] } (lo/hi the
//     whiskers' ends), numbers null when n = 0.
//   · A READING ROW (thisShot rows, family rows) =
//       { readingId, key, phase, tag ("P7"), phaseName ("Impact"), label, short, unit ("mph", "°", "%"),
//         unitText (the unit column: "" for ° and :1), group, multi (key read at several instants),
//         measured, value (number|null), valueText ("82"), valueFigure ("82 mph"), medianText ("72"),
//         place ("3rd highest" | "highest of 15" | ""), rankHi, rankLo, rz, side (−1|0|1), apart (bool),
//         box, values, firedShots: [bool per shot], fired (bool, this shot), firedWord ("out of its
//         corridor" for an At link; "the peak before P7 out of its corridor", "the change to P7 out of its
//         corridor", "the rate of change to P7 …" otherwise; "" when not fired), conditions: [texts that
//         fired on this shot here, "Early extension, peak before P7"], firedConditions: [{ id, name,
//         reducer (0 At 1 Delta 2 Rate 3 Extremum −1), instant ("at P7" | "peak before P7" | "change to P7" |
//         "rate of change to P7" | ""), text }], verdict ("fault" past the fault line — "past its fault
//         line" replaces "out of its corridor" in firedWord then — | "outside" | "inside" graded clean |
//         "" not graded on this shot; the Readings tab's filter), corridor ({ known, lowOpen, highOpen,
//         signalLo, signalHi, faultLo, faultHi } — the norm's bands under the grade policy, as Session
//         diagnostics draws them; known false when no condition grades the reading), boxText ({ lo, q1, med, q3, hi
//         [, signalLo, signalHi, faultLo, faultHi] } — the plot's numbers as text, for the hover labels), hasCurve (this shot
//         has the curve and the catalogue draws it), opens ("trace"|"run") }
//   · A RUN (RunBox) = { readingId, key, label, tag, unit, unitText, caption (string),
//       points: [{ ordinal, shotId, swingDir, value|null, measured, fired, picked }], box,
//       yRange { lo, hi }, drift { defined, r, lo, hi, clear, rText, ciText, slope, intercept,
//       line: { x0, y0, x1, y1 } | null (ordinal axis; only when it clears chance) } }
//   · A TRACE (Trace.qml) = { readingId, key, label, tag, unit, unitText, caption,
//       window { t0, t1 } (ms from impact, THIS SHOT'S clock), yRange { lo, hi },
//       curve { t: [ms], v: [windowed mean, null where bridged], raw: [ms values], ok: [bool] },
//       phases: [{ phase, tag, shotMs, impact }] (this shot's P-positions in the window),
//       instants: [{ phase, tag, shotMs, box, values, firedShots, pickedValue|null, pickedText, own (the
//       reading's own instant) }] } — a box of all shots at each P-position the key is read at.
//       Seeking: QML turns a plot x into shot ms and calls seekUsAtShotMs(ms).
//   · A SCATTER = { xId, yId, xKey, yKey (metric keys, for Theme.metricColor), xLabel ("Pelvis ω at P7, °/s  →"), yLabel ("↑  Clubhead speed at P7, mph"),
//       xUnit, yUnit, xRange, yRange (shared by every facet panel), xBox, yBox (the shown shots'),
//       points: [{ ordinal, shotId, swingDir, x, y, picked, fired }], defined, r, lo, hi, n, clear,
//       rText, ciText, line { x0, y0, x1, y1 } | null (solid when clear, dashed when not: `clear`),
//       caption ("r .62 (.17 to .86)  ·  n 15  ·  inside chance" | "Too few shots for a line.") }
//
// ── THE OUTPUTS (all NOTIFY resultsChanged, except replaySession and playhead) ─────────────────
// status: { ready, shotCount, pickedOrdinal (0 none), pickedShotId, pickedSwingDir, pickedInSession,
//   club is "mixed clubs" when the session mixes clubs;
//   pickedFromDetail, pickedImpactUs (window-relative µs; −1 none — QML draws the playhead on a trace at
//   (shotReplay.positionUs − pickedImpactUs) / 1000 ms), club ("" when mixed), clubsMixed, clubsSentence,
//   kicker ("Shot 8 of 15  ·  7 iron"), family (the effective one), readingCount, refused (n < 3),
//   shots: [{ ordinal, shotId, swingDir, club }] (every shot of the session, strike order: ordinal → a
//   carousel request), shotLabel ("Shot 8"; "" with no pick), countLabel ("15 shots, low to high"),
//   countLabelShort ("15 shots") — the profile's column heads,
//   refusal (Sentence), timings { jobMs (worker), applyMs (GUI thread) } }
// families: [{ name, count, note ("9 readings"), preset (bool), current (bool), sequence (bool) }]
// thisShot: { available, kicker, head (Sentence), standfirst (Sentence), apartCount, total,
//   keyFigures (published at ANY n with a picked shot: below three shots the value alone, median "" and
//     place "", foot "Two shots: too few to place it" / "the session's only shot"; the family defaults BY
//     DATA — the reader's choice, else the register's default, Club delivery, then manifest order, the
//     first holding a reading this shot measured):
//     { family, sequence (bool), items: [{ readingId, key, label, figure, unit, foot, median,
//     place, warn, warnWord (as a row's firedWord; "late" for a sequence peak), sentence ("82 mph, median 72, 3rd highest") }] },
//   lead: { readingId, trace (TRACE) | null }      — the family's lead reading on this shot (any n)
//   familyRows: [READING ROW]                      — the family's other readings (Replay)
//   apart: [READING ROW]                           — past a whisker, most extreme first
//   groups: [{ name, rows: [READING ROW] }]         — EVERY reading by family (D5)
//   notes (string; the apart definition),
//   firedElsewhere: [{ condition, name, metricKey, phase }],
//   asks: [{ id, text, answer, lit: [ordinal], go ("" | "relations" | "kinds"), upId }] }
// openRow: {} or { readingId, opens: "trace" | "run", trace: TRACE | null, run: RUN }
// sequence (SeqTiming): { available, chain: [{ segment, label, shotMs, msText, late, lateWord }],
//   chainSentence (Sentence, "Lead arm −58 ms  →  Club −3 ms  →  Pelvis +25 ms, late"),
//   verdict (Sentence), unplaced (string), orderSame, orderCount,
//   rows: [{ segment, label, valueText, shotMs|null, placed, placedText ("14 of 15"), box (ms),
//     values: [ms|null], lateShots: [bool], hasBox, notPlacedText }], axis { lo, hi } (ms),
//   ticks: [{ ms, text }], note (Sentence: the late-pelvis note, said neutrally, or "") }
// relations: { available, refusal (Sentence: below five shots — r needs five — or "No session is loaded."),
//   upId, acrossId,
//   catalogue: [{ name, readings: [{ readingId, short, tag, text ("Clubhead speed P7"), label }] }],
//   up, across: { readingId, key, label, short, tag, unit, group, text },
//   head (Sentence "What moves with clubhead speed"), short (Sentence), standfirst (Sentence),
//   chance: { n, tests, clearCount, chanceCount, rCritical, rCriticalText (".51"),
//     line (Sentence "|r| under .51 is chance at n = 15"),
//     sentence (Sentence "4 of 67 readings clear chance; chance alone would clear about 3. <co-note>") },
//   scatter: SCATTER,
//   ledger: [{ rank (0 for a same-quantity row), readingId, key, label, short, tag, same, sameMark ("‡"),
//     finding (not same and clear), r, rText, lo, hi, loText, hiText, ciText, n, clear, slope,
//     slopeText ("+1.3 mph per °"), pair: { xs: [..], ys: [..] (per shot, null where missing),
//     xRange { lo, hi }, yRange { lo, hi } (each reading's session min and max: the sparkline's scale) } }],
//   upValues: [per shot] (the Up reading's values),
//   ledgerNote (Sentence), restText,
//   facets: [{ id, kind ("condition"|"half"|"club"|"shape"), name, label, n }],
//   facet: {} or { id, kind, name, sentence (Sentence "fired on 11 / clean on 4. <co-note>"),
//     panels: [{ title, n, ordinals, fired (bool: the fired side), scatter: SCATTER }] },
//   matrix: { group, readings: [{ readingId, short, tag, text }], cells: [[{ defined, r, rText, clear,
//     xId, yId, on }]] (row i has i cells: the lower triangle) },
//   upRun: RUN (the Up reading through the session),
//   drifts: { clearCount, tests, sentence (Sentence), items: [{ readingId, label, tag, r, rText, lo, hi,
//     ciText, clear, direction ("rising"|"falling"), sentence (Sentence "Shaft lean at P7, r −.88,
//     falling through the session"), run: RUN }] }      — the drifts that clear chance, strongest first
//   asks: [{ id, text, answer, lit, go, upId }] }
// kinds: { available, refusal (Sentence; {} when available — "No session is loaded.", "Six shots are needed …"),
//   kicker ("Kinds of swing  ·  fifteen shots  ·  7 iron" | "…  ·  mixed clubs"),
//   method ("kmedoids"|"split"|"clubs"|"none"), structure (bool),
//   clubsNote (mixed clubs, kinds NOT the clubs: also appended to the standfirst),
//   method "clubs" — the kinds' membership agrees with the club labels (each kind's majority club differs,
//   ≥ 75 % of grouped shots with it): head "The two kinds are the two clubs: driver and 7 iron" (≥ 90 %) or
//   "… are mostly the two clubs …"; standfirst says kinds across clubs are not meaningful; groups are the
//   CLUBS; clubAgreement { agree, grouped }; separation.rows, curves empty; withinClub { club, n, sentence,
//   kinds (this same shape, run on the largest club's shots alone, ordinals the session's) } | null when no
//   club has six shots; silhouette, silhouetteText,
//   readingsUsed, minSize, head (Sentence), standfirst (Sentence), splitLine (Sentence, "" for kmedoids),
//   names: [A, B], pickedKind (name | ""), pickedSentence,
//   groups: [{ name, n, members: [{ ordinal, shotId, swingDir, picked }], medoid, medoidText, neutral }] —
//     the two kinds, then (only when some shots could not be placed: the split reading is unmeasured on
//     them) a third NEUTRAL set { name "Not measured", neutral: true, medoid 0, text "not measured: shots
//     3 and 9" }; the head then ends "; sixteen not measured". unplacedCount = its size.
//   separation: { dCritical, dCriticalText, note (Sentence), rows: [{ readingId, key, label, tag, same,
//     sameMark, d, dText, clear, medAText, medBText, unitText, scale { lo, hi }, boxA, boxB, valuesA,
//     valuesB }] },
//   curves: [{ readingId, key, label, unit, unitText, window { t0, t1 } | null (canonical ms: the span where
//     at least one fit has a finite value, padded 5 % of it (≥ 2 ms) each side, an end stretched to a
//     canonical P-position within 20 ms beyond the data; null when no fit has a value), yRange { lo, hi }
//     (every fit's v ± sd, padded 5 %),
//     phases: [{ phase, tag, tMs }], fits: [{ name, n, contributing (shots with the curve: the group
//     fitted), label ("Faster, 6 shots"), refused (< 3 contributing), refusal (sentence),
//     drawn (finite points), t: [ms], v: [..|null], sd: [..|null], lo: [v − sd | null], hi: [v + sd | null]
//     (all trimmed to the window; the ±1 sd
//     hairlines), nShots: [int], cover: [{ ordinal, picked, frac,
//     runs: [[t0, t1]] }] }] }],
//   shapes: { found, sentence, groups: [{ name ("shaped like shot 12"), label ("shaped like shot 12 · 11"),
//     medoid, n, ordinals }] },
//   alignNote (Sentence), fitNote, methodNote }
// replaySession (NOTIFY playheadChanged, emitted only when the P-position passed changes or the readout
//   text does — never per frame for nothing): {} or { readingId, key, label, tag, phaseName,
//   head (Sentence "Clubhead speed at P7, impact, every shot"), follows ("follows the video"),
//   phases: [{ phase, tag, seekUs, current }], run: RUN,
//   now: { valueText, medianText, place, sentence ("This shot 82 mph; typical 72 mph, the session median") },
//   partner: { readingId, label, tag, rText, scatter: SCATTER } | null,
//   movesWith: [{ readingId, label, tag, same, sameMark, r, rText, lo, hi, ciText, n, clear, sentence }] (3),
//   note (Sentence "With 15 shots an r inside ±.51 is what chance gives. <co-note>") }
// playhead (NOTIFY playheadChanged): { openValueText ("82 mph" | ""), leadValueText, replayIndex } — the
//   open row's and the lead trace's value at the playhead, as text.
//
// Unit-tested in src/Gui/tests/charts_relations_presenter_test.cpp.

#include "session_curves.h"
#include "session_series_types.h"

#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <vector>

namespace pinpoint::charts {

class ChartsRelationsPresenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── inputs ──
    Q_PROPERTY(QObject *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString pickedSwingDir READ pickedSwingDir WRITE setPickedSwingDir NOTIFY pickedSwingDirChanged)
    Q_PROPERTY(QString register READ registerMode WRITE setRegisterMode NOTIFY registerChanged)
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY familyChanged)
    Q_PROPERTY(QString upId READ upId WRITE setUpId NOTIFY upIdChanged)
    Q_PROPERTY(QString acrossId READ acrossId WRITE setAcrossId NOTIFY acrossIdChanged)
    Q_PROPERTY(QString facetId READ facetId WRITE setFacetId NOTIFY facetIdChanged)
    Q_PROPERTY(QString openId READ openId WRITE setOpenId NOTIFY openIdChanged)
    // the Diagnostics grade policy's name (AppSettings.diagnosticsGradePolicy): where a fired reading is past its fault line
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY gradePolicyChanged)
    Q_PROPERTY(qint64 playheadUs READ playheadUs WRITE setPlayheadUs NOTIFY playheadUsChanged)
    Q_PROPERTY(QVariantMap pickedDetail READ pickedDetail WRITE setPickedDetail NOTIFY pickedDetailChanged)

    // ── outputs ──
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList families READ families NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap thisShot READ thisShot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap openRow READ openRow NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap sequence READ sequence NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap relations READ relations NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap kinds READ kinds NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap replaySession READ replaySession NOTIFY playheadChanged)
    Q_PROPERTY(QVariantMap playhead READ playhead NOTIFY playheadChanged)

public:
    explicit ChartsRelationsPresenter(QObject *parent = nullptr);
    ~ChartsRelationsPresenter() override;

    QObject *model() const { return m_model; }
    void     setModel(QObject *m);
    QString  pickedSwingDir() const { return m_picked; }
    void     setPickedSwingDir(const QString &d);
    QString  registerMode() const { return m_register; }
    void     setRegisterMode(const QString &r);
    QString  family() const { return m_family; }
    void     setFamily(const QString &f);
    QString  upId() const { return m_upId; }
    void     setUpId(const QString &id);
    QString  acrossId() const { return m_acrossId; }
    void     setAcrossId(const QString &id);
    QString  facetId() const { return m_facetId; }
    void     setFacetId(const QString &id);
    QString  openId() const { return m_openId; }
    void     setOpenId(const QString &id);
    QString  gradePolicy() const { return m_gradePolicy; }
    void     setGradePolicy(const QString &p);
    qint64   playheadUs() const { return m_playheadUs; }
    void     setPlayheadUs(qint64 us);
    QVariantMap pickedDetail() const { return m_pickedDetail; }
    void     setPickedDetail(const QVariantMap &d);

    bool busy() const { return m_running; }
    bool ready() const;
    QVariantMap  status() const;
    QVariantList families() const;
    QVariantMap  thisShot() const;
    QVariantMap  openRow() const;
    QVariantMap  sequence() const;
    QVariantMap  relations() const;
    QVariantMap  kinds() const;
    QVariantMap  replaySession() const;
    QVariantMap  playhead() const { return m_playheadMap; }

    // The picked shot's window-relative µs of a P-position (−1 when it has none), and of a time in ms
    // from impact on its own clock (−1 when it has no impact) — hand either to shotReplay.seekToUs.
    Q_INVOKABLE qint64 seekUsFor(int phase) const;
    Q_INVOKABLE qint64 seekUsAtShotMs(double shotMs) const;

    // Nice axis ticks across [lo, hi] (Stats.js ticks: steps of 1, 2, 5 × 10^k, at most maxTicks), each
    // { v, text } with the text in the unit's display precision (or finer when the step needs it), U+2212
    // minus. For every value axis the figures draw (trace, run, scatter, kind curve); call it when a range
    // or a width changes, never per frame.
    Q_INVOKABLE QVariantList ticks(double lo, double hi, int maxTicks, const QString &unit = QString()) const;

    // Test seams.
    Q_INVOKABLE void setSynchronous(bool on) { m_synchronous = on; }
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);
    // Feed a snapshot directly (no SessionSeriesModel): tests on the fixture. Replaces `model`'s.
    void setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot);
    // The read-at instants (default: the metric catalogue's card readAt).
    using ReadAt = std::function<std::vector<int>(const QString &key)>;
    void setReadAt(ReadAt readAt);

    // ADDED 15 Oct (build/wrist-tab): "what moves with <readingId>" for a caller that asks its own
    // question, whatever the Relations tab's Up is — the Wrist tab's "what moves with the lead wrist at
    // impact". The SAME readings, r, interval and ledger rows as `relations.ledger` (one row builder),
    // computed on the CALLER's thread from `snapshot` (the Wrist presenter's worker): the top `k`
    // readings that are not the same quantity, then the top `lmK` launch-monitor (lm.*) readings not
    // already among them, each with its rank in the full ledger. Shape:
    //   { available, refusal (Sentence; below five shots, or the reading not read on three),
    //     upId, up: { readingId, key, label, short, tag, unit, group, text }, head (Sentence),
    //     chance: { n, tests, clearCount, chanceCount, rCritical, rCriticalText, line (Sentence) },
    //     rows: [LEDGER ROW as relations.ledger], note (Sentence: "A small session. " + co-occurrence) }
    static QVariantMap movesWith(const std::shared_ptr<const SessionSnapshot> &snapshot, const QString &readingId,
                                 int k, int lmK, const ReadAt &readAt = {});

    struct Output;
    struct Cache;

signals:
    void modelChanged();
    void pickedSwingDirChanged();
    void registerChanged();
    void familyChanged();
    void upIdChanged();
    void acrossIdChanged();
    void facetIdChanged();
    void openIdChanged();
    void gradePolicyChanged();
    void playheadUsChanged();
    void pickedDetailChanged();
    void busyChanged();
    void resultsChanged();
    void playheadChanged();

private:
    struct Inputs {
        std::shared_ptr<const SessionSnapshot> snapshot;
        QString picked, reg, family, up, across, facet, open;
        QVariantMap pickedDetail;
        ReadAt readAt;
        int generation = 0;
    };

    void onSnapshotChanged();
    void schedule();
    void start();
    void deliver(std::shared_ptr<const Output> out);
    void updatePlayhead(bool force);
    std::shared_ptr<const SessionSnapshot> currentSnapshot() const;

    // inputs
    QPointer<QObject> m_model;
    std::shared_ptr<const SessionSnapshot> m_directSnapshot;
    QString m_picked, m_register = QStringLiteral("analyse"), m_family, m_upId, m_acrossId, m_facetId, m_openId;
    QString m_gradePolicy = QStringLiteral("standard");
    qint64  m_playheadUs = -1;
    QVariantMap m_pickedDetail;
    ReadAt  m_readAt;

    // outputs
    std::shared_ptr<const Output> m_out;
    int         m_replayIndex = -1;
    QVariantMap m_playheadMap;

    // the worker
    QThreadPool m_pool;
    std::shared_ptr<Cache> m_cache;        // touched only inside a job (jobs never overlap)
    bool m_synchronous = false;
    bool m_running = false;
    bool m_dirty = false;
    int  m_generation = 0;
};

} // namespace pinpoint::charts
