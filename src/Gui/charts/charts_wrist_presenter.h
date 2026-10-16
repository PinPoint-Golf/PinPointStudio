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

#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

// THE CHARTS PANEL'S WRIST PRESENTER — the Wrist tab (docs/design/wrist_motion_into_charts.md §4 B;
// build/wrist-tab/CONTRACT.md I4; the mock build/run-me/wrist-charts/qml/variant_b/). Every word and
// number variant B computed in JavaScript (WristCalc.js, WristShot/WristSession/WristProfile/
// WristLedger.qml) is computed here; QML positions and paints.
//
// ── ONE VERDICT (CONTRACT decisions 1–4) ─────────────────────────────────────────────────────
//   · THE CELLS are the wrist engine's: each DOF at each checkpoint against its norm band
//     (ShotSeries::wrist, session_wrist.h), graded against the session's archetype.
//   · THE FINDINGS AND THE SCORE are the diagnostics conditions' in the wrist domain
//     (Condition::domain == "wrist"): a finding is a wrist condition that FIRED on the shot, a
//     strength one that read clean on every tail of its axis; the score is scoreWristDomain()
//     (src/Diagnostics/engine/wrist_domain_score.h) over this shot's verdicts. The engine's rules and
//     score do not reach the tab (its score rides along as `engineScore` only).
//   · THE VERDICTS ARE READ OFF THE SNAPSHOT — the ledger SessionDiagnosticsModel writes
//     (diagnostics.json), joined per shot by SessionSeriesModel (ShotDiagnostics: fired / clean,
//     graded value, corridor, confidence, driving reading) with the session tiers
//     (SessionSnapshot::tiers). So no diagnostics object is an input: the panel already refreshes the
//     snapshot when that model writes (PpChartsPanel: busy → false → refreshDiagnostics). The
//     condition texts (label, golfer words, consequence) come from the shipped pack, read on the worker.
//   · SEVERITY is the session tier: a condition that is a "pattern" this session is a FAULT on the
//     shot it fired on; one fired but not (yet) a pattern is WATCH. The score is scored the same way:
//     each assessed wrist condition passes tier = fired ? (pattern ? "pattern" : "watching") : "clean".
//   · A CHECKPOINT OUTSIDE ITS BAND WITH NO FINDING (W-D6) is marked in the profile and the head says
//     it: "No finding fired; 2 checkpoints sat outside a band".
//
// ── INPUTS ───────────────────────────────────────────────────────────────────────────────────
// model (SessionSeriesModel), pickedSwingDir + pickedDetail (shotReplay.analysisDetail: a shot not
// yet in the snapshot still reads), register ("analyse" | "replay"), playheadUs (the picked shot's
// window-relative clock), scope ("shot" | "session"), against ("address" | "previous" | "reference" |
// "median"), curvesBehind, archetype (-1 Auto | 0 Neutral | 1 Bowed | 2 Cupped; FORWARDED to the
// model's wristArchetype, so every shot is graded against it, and applied here to any shot the model
// has not re-graded yet), referenceSwingDir (appSettings.wristReferenceSwingDir, any session).
//
// ── THREADING (the ChartsTracesPresenter pattern) ────────────────────────────────────────────
// Any input or snapshot change schedules ONE coalesced job on a private single-thread pool; results
// are swapped in by a queued invocation and `resultsChanged` fires once per delivered job. The
// playhead is a GUI-thread lookup into the delivered checkpoint times: `selected` / `standfirst`
// change (selectedChanged) only when the playhead crosses into another checkpoint. setSynchronous(true)
// runs the job inline when an output is next read (tests).
//
// ── OUTPUT SHAPES ────────────────────────────────────────────────────────────────────────────
// A Sentence is words::Sentence::toVariant(): { text, runs: [{ text, fig }] }. A missing number is
// null (JS null) and its text "-", never 0. Figure texts use U+2212 for minus. Angles are degrees, Δ
// from address. A BOX is { n, min, max, q1, med, q3, lo, hi, outliers: [ordinals] } (PpChBoxCell's).
//
//  available: bool        — some shot of the session (or the picked shot's own detail) has a wrist DOF
//
//  choices: {
//    scope, scopes: [{ id: "shot"|"session", label, current }],
//    against (resolved id), againstOptions: [{ id: "address"|"previous"|"reference"|"median", label,
//        current, enabled, why }]   (labels "address", "previous", "reference ★", "session median"),
//    curvesBehind: bool, curves: [{ id: "off"|"on", label, current }],
//    archetype (the mode, -1..2), archetypes: [{ id: -1..2, label: "Auto"|"Neutral"|"Bowed"|"Cupped", current }],
//    archetypeAfter ("→ Cupped" under Auto: what Auto resolved the picked shot to; "" by hand),
//    reference: { set: bool, swingDir, label ("shot 3, 18 Aug 2026"), isThisShot: bool,
//                 linkText ("★ set this swing as your reference" | "★ this swing is your reference"),
//                 canSet: bool (a picked swing exists) },
//    summary: string   (the one-line fold below medium: "Bands Auto → Cupped  ·  against address  ·  curves off") }
//
//  thisShot: {
//    available, empty: string (W14: "Select a swing to review. …" | "No wrist metrics for this swing. …"),
//    ordinal, shotId, swingDir, inSession, kicker ("Shot 3 of 6  ·  7 iron  ·  Wrist"),
//    head: Sentence, graded: bool (the diagnostics have graded this shot),
//    outCells: int (cells outside their band, strips only), standfirsts: [8 × Sentence] (one per
//      checkpoint; the live one is `standfirst`),
//    score: { score (int | null), scoreText ("84" | "-"), band ("good"|"watch"|"weak"|"fault"|""),
//             base: 100, sumText ("100 − 16 = 84" | ""), line ("Wrist score 84 of 100, good  ·  100 − 16 = 84"),
//             contributions: [{ id, label, penalty (int), penaltyText ("−9") }], engineScore (int | null) },
//    style: { hasIndex, archetype, archetypeName, mode, byHand: bool, line: Sentence (W4),
//             items: [{ key: "neutral"|"bowed"|"cupped", name, pct (int|null), pctText ("84 %"), graded }] },
//    profile: {
//      positions: [POS], rows: [ROW], ghostWord ("" | "previous swing" | "the reference ★" |
//      "session median, n 6"), ghostRefusal (string; why the asked-for ghost is not drawn, else ""),
//      sub ("Δ from address, degrees  ·  dashed: previous swing  ·  faint: the 40 ms curve, …"),
//      caption ("Shaded: the Cupped band … outside it, graded by how far (fault, watch). A finding's word is
//      the session's: pattern …; watching …" — cells carry band grades, findings the session tier),
//      missing: ["trail wrist", …], missingText ("Not measured on this swing: …." | "", plus a line when the
//      forearm ROLL strip is absent but the forearm's rotation from address is carried: monitored, not graded) },
//    findings: [FINDING], strengths: [FINDING],
//    findingsEmpty ("No faults and nothing to watch." | "Diagnostics have not graded this swing yet."),
//    strengthsEmpty ("Nothing stood out as a strength on this swing.") }
//
//  POS  = { index (0..7), tag ("P4"), phase, name ("Top"), note ("face checkpoint"), available,
//           tUs (window-relative, −1 none), seekUs (= tUs; −1 none) }
//  ROW  = strip: { numbersOnly: false, dof, key (metricKey), name, sub, source, confidence, pips ("●●●○"),
//                  sourceText ("IMU ●●●○"), short ("bow / cup"), poleTop, poleBottom, poleTopText ("↑ closed"),
//                  poleBottomText ("↓ open"),
//                  points: [8 × POINT], ghost: [8 × number|null] | null, trace: [{ x (0..7, checkpoint
//                  index), v (Δ°|null) }] ([] when off or unavailable), boxes: [8 × BOXCELL] | null
//                  (Session scope only), yLo, yHi (the row's own scale from its data), outCount }
//         numbers-only (the grid-only lead elbow): { numbersOnly: true, dof, key, name, sub
//                  ("no corridor: the angle only"), cells: [8 × { available, value, valueText }] }
//  POINT = { available, value (Δ° | null), valueText ("−19°" | "-"), rag ("green"|"amber"|"red"|"ref"|"none"),
//            banded, bandLo, bandHi (null when !banded), out (amber|red), fault (red), watch (amber),
//            outBy (° past the band | null), outPole (the pole it left by: "open" | ""),
//            word ("fault · 19° open" | "watch · 2° closed" | ""), wordShort ("fault open" | "") }
//  FINDING = { id, name, severity ("pattern"|"watching"|"good": the SESSION tier, as Session diagnostics
//              words it), severityWord ("pattern"|"watching"|"working"),
//            tier ("clean"|"watching"|"pattern"), pointsLost (int), pointsText ("−9 points" | ""),
//            positions: ["P4"], positionsText ("at P4"), phase, seekUs (−1 none),
//            value (graded reading | null), valueText, outBy (| null), magnitude (int | 0),
//            magnitudeText ("19°" | ""), confidence (0..1), pips, lowConfidence,
//            explanation (golferWhy, else the golfer phrase), coaching ("" — the pack has no golfer-facing
//            coaching field; the author's `consequence` is never shown), protect (golferWhy, strengths),
//            linkedTo: [labels], corroboratedBy: [labels],
//            more: [{ k ("Coaching"|"Protect"|"Linked to"|"Corroborated by"), v }] (the open row, non-empty only) }
//
//  session: {
//    available (ready > 0), n, ready, small (n < 10), kicker ("6 shots  ·  7 iron  ·  Wrist"),
//    head: Sentence, standfirst: Sentence, positions: [POS] (the picked shot's, else the first ready),
//    rows: [ROW with boxes] (points = the PICKED shot's, for its band wash; 8 × unavailable when it has
//      none), caption ("Shaded: shot 3’s Cupped band. Each shot is graded against its own …"),
//    sub ("Δ from address, degrees  ·  a box per checkpoint, n 6, this shot ringed  ·  …"),
//    scoreRun: RUN { readingId "wristScore", key "", label, tag "", unit "", unitText "", caption,
//      points: [{ ordinal, shotId, swingDir, value|null, measured, fired: false, picked }], box: BOX,
//      yRange { lo: 0, hi: 100 }, drift: { defined, r, lo, hi, clear, rText, ciText, slope, intercept, line } },
//    scoreBox: BOX,
//    styles: { columns: [{ ordinal, shotId, swingDir, label ("Shot 3"), short ("3"), picked }],
//              rows: [{ key, name, cells: [{ text ("84" | "-"), graded }] }], counts: [{ name, n }],
//              hasIndex, note ("" | "No shot in this session carries a resemblance index, …": then
//              columns and rows are empty and the note stands in for the grid) },
//    recurring: [{ id, name, severity, tier, count, countText ("4 of 6 shots, at P4"),
//                  positions: [{ tag, n }], marks: [{ ordinal, shotId, swingDir, fired }] }],
//    recurringEmpty ("No wrist finding fired on any shot.") }
//  BOXCELL = { box: BOX, values: [per shot Δ° | null], n, median (| null, n < 3), medianText,
//              wide (q3 − q1 | null), wideText ("2° wide" | ""), picked (this shot's Δ | null) }
//
//  movesWith: ChartsRelationsPresenter::movesWith(snapshot, "<lead bow/cup key>@5", 4, 3) — { available,
//    refusal, upId, up, head, chance{…, line}, rows: [LEDGER ROW], note } — plus sub (string),
//    relationsUpId (the readingId to open Relations on).
//
//  selected: int (the playhead's checkpoint, 0..7), standfirst: Sentence (thisShot.standfirsts[selected]).
namespace pinpoint::charts {

class ChartsWristPresenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── inputs ──
    Q_PROPERTY(QObject *model READ model WRITE setModel NOTIFY inputsChanged)
    Q_PROPERTY(QString pickedSwingDir READ pickedSwingDir WRITE setPickedSwingDir NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap pickedDetail READ pickedDetail WRITE setPickedDetail NOTIFY inputsChanged)
    Q_PROPERTY(QString register READ registerName WRITE setRegisterName NOTIFY inputsChanged)
    Q_PROPERTY(qint64 playheadUs READ playheadUs WRITE setPlayheadUs NOTIFY playheadChanged)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY inputsChanged)
    Q_PROPERTY(QString against READ against WRITE setAgainst NOTIFY inputsChanged)
    Q_PROPERTY(bool curvesBehind READ curvesBehind WRITE setCurvesBehind NOTIFY inputsChanged)
    Q_PROPERTY(int archetype READ archetype WRITE setArchetype NOTIFY inputsChanged)
    Q_PROPERTY(QString referenceSwingDir READ referenceSwingDir WRITE setReferenceSwingDir NOTIFY inputsChanged)

    // ── outputs ──
    Q_PROPERTY(bool available READ available NOTIFY resultsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap choices READ choices NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap thisShot READ thisShot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap session READ session NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap movesWith READ movesWith NOTIFY resultsChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap standfirst READ standfirst NOTIFY selectedChanged)

public:
    explicit ChartsWristPresenter(QObject *parent = nullptr);
    ~ChartsWristPresenter() override;

    QObject *model() const;
    void setModel(QObject *m);
    QString pickedSwingDir() const { return m_in.pickedSwingDir; }
    void setPickedSwingDir(const QString &d);
    QVariantMap pickedDetail() const { return m_in.pickedDetail; }
    void setPickedDetail(const QVariantMap &d);
    QString registerName() const { return m_in.reg; }
    void setRegisterName(const QString &r);
    qint64 playheadUs() const { return m_playheadUs; }
    void setPlayheadUs(qint64 us);
    QString scope() const { return m_in.scope; }
    void setScope(const QString &s);
    QString against() const { return m_in.against; }
    void setAgainst(const QString &a);
    bool curvesBehind() const { return m_in.curvesBehind; }
    void setCurvesBehind(bool on);
    int archetype() const { return m_in.archetype; }
    void setArchetype(int a);
    QString referenceSwingDir() const { return m_in.referenceSwingDir; }
    void setReferenceSwingDir(const QString &d);

    bool available();
    bool busy() const { return m_running; }
    QVariantMap choices();
    QVariantMap thisShot();
    QVariantMap session();
    QVariantMap movesWith();
    int selected() const { return m_selected; }
    QVariantMap standfirst() const;

    // The picked shot's window-relative µs of checkpoint `tag` ("P4"), for shotReplay.seekToUs; −1 when
    // the swing has no such checkpoint. A lookup into the delivered result.
    Q_INVOKABLE qint64 seekUsFor(const QString &tag) const;

    // ── test seams ──
    void setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot);   // used while no model is set
    Q_INVOKABLE void setSynchronous(bool on);
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);

    struct Inputs {
        std::shared_ptr<const SessionSnapshot> snapshot;
        QString pickedSwingDir;
        QVariantMap pickedDetail;
        QString reg = QStringLiteral("analyse");
        QString scope = QStringLiteral("shot");
        QString against = QStringLiteral("address");
        bool curvesBehind = false;
        int archetype = -1;
        QString referenceSwingDir;
    };
    struct Result;
    struct Worker;

signals:
    void inputsChanged();
    void busyChanged();
    void resultsChanged();
    void playheadChanged();
    void selectedChanged();

private:
    void schedule();
    void startJob();
    void apply(std::shared_ptr<Result> r);
    void flushIfSynchronous();
    void updateSelected(bool force);
    void forwardArchetype();
    std::shared_ptr<const SessionSnapshot> currentSnapshot() const;

    Inputs m_in;
    QPointer<QObject> m_model;
    std::shared_ptr<const SessionSnapshot> m_directSnapshot;
    qint64 m_playheadUs = -1;
    int m_selected = 0;

    bool m_synchronous = false;
    bool m_dirty = true;
    bool m_scheduled = false;
    bool m_running = false;

    std::shared_ptr<Result> m_result;
    std::shared_ptr<Worker> m_worker;
    QThreadPool m_pool;
};

} // namespace pinpoint::charts
