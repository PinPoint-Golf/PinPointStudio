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
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <functional>
#include <memory>

// THE CHARTS PANEL'S POSTURE PRESENTER — the Posture section of This shot and Session
// (docs/design/posture_chart_design.md §5, stage 2b), built the way ChartsWristPresenter is. The core
// (session_posture.h) decides every number and word; this serialises it, adds the findings off the
// shot's diagnostics and the Against ghost, and the QML only places what it is given.
//
// ── ONE JUDGE (as the core) ──────────────────────────────────────────────────────────────────
//   · CELLS are the measure engine's readings (postureInputFromShot), the BANDS the chosen pattern's
//     (posture_level / posture_squat_push), the FAULT LINES the corridors of the conditions that grade
//     the cell, at the shot's grading context.
//   · FINDINGS are the shot's fired diagnostics conditions that name a departure (D1–D7, the core's
//     postureDepartureSpecs) plus `trail_knee_straighten` when it fired (Q3: it stays a fault, and it
//     never counts toward the match). Severity is the session tier, as the wrist tab says it.
//   · WORKING WELL is the `golferWell` of a posture condition (postureConditionList) graded CLEAN whose
//     reading sits inside its band: the ledger's |z| ≤ 1 where it has one, and every cell its fault
//     line draws on inside the chart's band. One per axis; none where the axis fired.
//   · THE PATTERN is the session's (≥ 5 shots), else the shot's own, said so; `pattern` ≥ 0 overrides
//     both (§4.4.6). It is a separate choice from Against.
//
// ── INPUTS ───────────────────────────────────────────────────────────────────────────────────
// model (SessionSeriesModel), pickedSwingDir + pickedDetail (shotReplay.analysisDetail: a shot not yet
// in the snapshot still reads, ungraded), register ("analyse" | "replay"), playheadUs (the picked shot's
// window-relative clock), scope ("shot" | "session", following body.shownView), against ("address" |
// "previous" | "reference" | "median"; a ghost value per cell), pattern (-1 Auto | 0 Level | 1 Squat
// and push), referenceSwingDir (the ★ swing, any session).
//
// ── THREADING (the wrist presenter's, exactly) ───────────────────────────────────────────────
// Any input or snapshot change schedules ONE coalesced job on a private single-thread pool; results are
// swapped in by a queued invocation and `resultsChanged` fires once per delivered job. The playhead is a
// GUI-thread lookup into the delivered P times: `selected` / `standfirst` change (selectedChanged) only
// when it crosses into another position. setSynchronous(true) runs the job inline when an output is next
// read (tests). Each shot's reading (postureInputFromShot) is cached by the shot's shared_ptr, so a
// re-analysed shot (a new pointer) is read again and an unchanged one is not.
//
// ── OUTPUT SHAPES ────────────────────────────────────────────────────────────────────────────
// A Sentence is words::Sentence::toVariant(): { text, runs: [{ text, fig }] }. A missing number is null
// and its text "-", never 0. Figures use U+2212 for minus; cm to one decimal, ° and % stance whole. A
// value is the CHANGE FROM ADDRESS in the row's unit. A BOX is box_map.h's { n, min, max, q1, med, q3,
// lo, hi, outliers: [ordinal], texts }.
//
//  available: bool        — some shot of the session (or the picked shot's own detail) reads a posture row
//
//  choices: {
//    scope, scopes: [{ id: "shot"|"session", label, current }],
//    against (the id asked), againstOptions: [{ id: "address"|"previous"|"reference"|"median", label
//        ("address", "previous", "reference ★", "session median"), current, enabled, why }],
//    pattern (the mode, -1..1), patterns: [{ id: -1|0|1, label: "Auto"|"Level"|"Squat and push", current }],
//    patternResolved (0|1: the pattern the picked shot is judged against, else the session's),
//    patternAfter ("→ Level" under Auto; "" by hand),
//    reference: { set, swingDir, label ("shot 3, 18 Aug 2026"), isThisShot, linkText, canSet },
//    summary ("Pattern Auto → Level  ·  against address"),
//    onShowKeys: [metric key] (the series whose curve on show adds the Posture section: the rows' and headRise) }
//
//  thisShot: {
//    available, empty ("Select a swing to review. …" | "No posture readings for this swing. …" | ""),
//    ordinal, shotId, swingDir, inSession, kicker ("Shot 3 of 6  ·  7 iron  ·  Posture"),
//    graded (the diagnostics graded this shot),
//    head: Sentence (the top-ranked FIRED departure condition, read on its row with the band — session_posture.h
//          decision 11 — else the W-D6 words, else "stays inside"), headText,
//    pattern: PATTERN, patternLine ("Level (nearest, of your session) · trail knee released"),
//    profile: { positions: [POS], rows: [ROW], ghostWord ("" | "previous swing" | "the reference ★" |
//               "session median, n 6"), ghostRefusal (why the asked-for ghost is not drawn, else ""),
//               sub ("change from address  ·  dashed: previous swing"), caption },
//    exits: [EXIT], departures: [DEPARTURE],
//    whatMoved: { top: MOVED, impact: MOVED },
//    findings: [FINDING], workingWell: [FINDING], findingsEmpty, workingWellEmpty,
//    address: ADDRESSBLOCK, absolutes: [ADDRESS], trust: TRUST,
//    picture: PICTURE (posture_picture.h: the frames at address, the top and impact, the frozen coach lines and the
//             bands at impact, every coordinate in the frame's own pixels; the stills are made by the app's still maker,
//             setStillMaker, off the GUI thread, and the picture re-delivered with their `src`),
//    standfirsts: [8 × Sentence] (one per position; the live one is `standfirst`) }
//
//  PATTERN = { mode (-1..1), pattern (0|1), name ("Level"), source ("session"|"shot"|"chosen"), sourceWords,
//              line (= patternLine), then MATCH fields of the match the line speaks for (the session's when it
//              chose, else the shot's own), own: MATCH (this shot's own distances; = the above on Session),
//              trail: { known, kept, word ("kept"|"released"), words, changeAtTop (| null), changeText } }
//  MATCH = { ok, nearest (0|1 | null), nearestName, fit ("close"|"nearest"|"between"|"none"), fitWords
//            ("nearest to Level"), basis ("both"|"faceOn"|"dtl"|"none"), basisWords, dLevel, dSquatPush (| null),
//            dLevelText, dSquatPushText ("1.41" | "-"), gap (| null), cells (compared), rows: [row keys] }
//  POS   = { index (0..7), tag ("P4"), phase (Phase id), name ("the top"), available, tUs (−1 none), seekUs (= tUs) }
//  ROW   = { key ("hips"), label ("Hip depth"), metricKey ("pelvisThrust"), hueKey (= metricKey: the metric's
//            named colour paints the wash), camera ("down the line"), unit ("cm"), decimals, poleWord ("in"),
//            poleText ("in ▲"), plusWord, minusWord, present, style (the trail knee: shown, never matched),
//            styleWord ("released" | ""), styleText ("style: released" | ""), outCount, yLo, yHi (the row's
//            own scale over values, bands, fault edges, ghost and boxes), cells: [8 × CELL],
//            boxes: [8 × BOXCELL] | null (Session only) }
//  CELL  = { index, tag, available, value (| null), text ("+6.5" | "-"), valueText ("+6.5 cm" | "-"),
//            warned, warn ("⚠" | "": drawn only on a cell with a value — a refused reading is a plain "-"),
//            warnReason, warnShort (the ⚠'s few words under the figure, "" with no value: the gate's, else
//            "the turn" | "hip turn" | "joint drift"; the full reason is said once, in TRUST), gateWords, counted (enters the match),
//            banded, bandLo, bandHi, bandMu, watchLo, watchHi (| null), bandContext, bandInherited, bandSource,
//            wash ("band" | "none"), z (| null), outside (never under a ⚠), ring (= outside), ringWord (the
//            side of the band, the row's pole word: "in" | "straighter" | ""; no number — the figure is above it),
//            exit, beyondFault, faultHi: FAULT | null, faultLo: FAULT | null, firedShots, note (W-D6 | ""),
//            mark ("ring" | "dot" | "none": ring = outside, dot = a value inside, none = no value),
//            ghost (| null), ghostText ("+4.1" | "-"), seekUs (−1 none),
//            picked (Session: this shot's value | null; This shot: = value), pickedText }
//  FAULT = { edge, edgeText ("+5.0"), conditionId, label }
//  BOXCELL = { box: BOX, n, median (| null), medianText, wide (q3 − q1 | null), wideText ("2.1 cm wide" | ""; "" at P1),
//              picked (this shot's value | null), values: [per shot, index = ordinal − 1, null unmeasured] }
//  EXIT  = { row (key), rowLabel, pos, tag, value, valueText, z, zText, departure (the candidate: "D1" | ""), name
//            (the candidate's), conditionIds, named (one of conditionIds fired on this shot / is a session pattern:
//            only then do the words name it), words ("P6 hips +6.5 cm: Hips in" | "P2 head +4.7 cm: outside the
//            good-player band at P2, not far enough to be a fault"), seekUs }
//  (departures: the NAMED ones only; session departures count a shot only where the condition fired on it)
//  DEPARTURE = { id, name, conditionIds, positions: [tags], positionsText ("P5–P7"), firstTag, seekUs }
//  MOVED = { pos, tag, when ("At the top"), line, head ("up"|"down"|"level"|"inside"|""), bend ("lost"|"gained"|""),
//            attribution, hipsIn, hipsWarned, bendWarned, seekUs }
//  FINDING = { id, name (the condition's label; Working well: its golferWell, capitalised), kind ("departure" |
//              "trailKnee" | "finding" (loss_of_posture: fired, names no departure) | "working"), departure ("D1" | ""),
//              departureName, severity ("pattern"|"watching"|"good"),
//              severityWord ("pattern"|"watching"|"working"), tier ("clean"|"watching"|"pattern"),
//              positions: [tags], positionsText ("P5–P7" | "at P7"), tag (the P to seek), seekUs (−1 none),
//              seekText ("Go to P7" | ""), value (the ledger's graded value, only in a row's own unit — cm, °,
//              % stance — else null: a composite's z is never shown), valueText ("" with no value), z (| null),
//              explanation (golferWhy, else the golfer phrase; "" on Working well), protect ("" always: Working
//              well says its golferWell line only — golferWhy describes the fault) }
//  (findings: in the session's work-on order — roots before symptoms, SessionSnapshot::workOnOrder — then
//   the session's other patterns, then |z|)
//  ADDRESSBLOCK = the "At address" block (§7.4.2), under the profile: { available (a row), title ("At address"), sub,
//            rows: [ADDRESS], legend, lines (its trust lines, said once in TRUST.lines too: the rig rule verbatim, what
//            "this set-up" is keyed on, where the bands come from, the readings this capture cannot produce) }
//  ADDRESS (= ABSOLUTE: `absolutes` is the same rows) = one rig-sensitive reading (the pack's `rigSensitive` measures, a
//            row per one this capture can produce; one with no value on the shot is "-"):
//            { measureId, label (the golfer's words: "Forward bend from your hips"), measureLabel, unit, camera
//              ("down the line"), cameraKey ("dtl"|"faceOn"), metricKey, hueKey, value (| null: this shot's, the session's
//              median on Session), text ("39°" | "-"), valueWord ("this shot" | "median"), banded, bandLo, bandHi, bandMu
//              (| null: the norm row resolved at the grading context), bandText ("good players 32 to 48°" | "no good-player
//              band yet"), bandSource, bandContext, bandInherited, box: BOX (this session's shots), boxN, boxText ("this
//              session: median 39°, n 10"), values: [per shot, index = ordinal − 1], dot (This shot: = value; Session null),
//              history: BOX | null (the golfer's EARLIER sessions from the same set-up: the same view and camera serial —
//              where the camera stood is recorded by nothing — each session's ledger `value` per swing), historyN,
//              historySessions, hasHistory, historyText ("your earlier sessions: median 38°, n 34 shots over 4 sessions"),
//              historyWhy ("No earlier session of yours from this set-up yet." | ""), warned (true until a capture can be
//              declared trusted), warn ("⚠"), warnShort ("depends on where the camera stands"), xLo, xHi (the row's
//              scale over the band, both boxes and the number), trust }
//  TRUST = { lines: [string] (said once, in order), cameras, absent (a row with no value anywhere, and the gate
//            that refused it: "Hip depth: not measured on this shot — no ruler: …"), warnings, bandSources, trunkDatum, rigRule }
//
//  session: {
//    available (some shot reads a posture row), n (shots in the snapshot), shots (shots with a reading),
//    small (shots < 10), kicker ("15 shots  ·  7 iron  ·  Posture"), fromSession (≥ 5 shots, or chosen),
//    head: Sentence, headText ("Level on 13 of 15 shots; you leave it the same way every time: hips in from P5." | a session
//          between the two: "Between Level and Squat and push: Level on 17, between on 30 of 50 shots; …"),
//    pattern: PATTERN (on the medians), patternLine,
//    profile: { positions: [POS] (the picked shot's, else the first shot with a reading), rows: [ROW with
//               cells = the MEDIAN against the session's pattern, `picked` = this shot, and boxes], sub, caption },
//    strip: { marks: [{ ordinal, shotId, swingDir, ok, nearest (0|1 | null), nearestName, between, differs
//               (ringed), picked, dLevel, dSquatPush (| null), word ("Level" | "Squat and push" | "between" | "-"),
//               label ("shot 3: between") }],
//             onPattern, line ("Level on 13 of 15 shots"), legend },
//    departures: [{ id, name, conditionIds, shots, countText ("15 of 15 shots"), ordinals, marks: [{ ordinal,
//                   swingDir }], firstTag, line ("Hips in, from P5: 15 of 15 shots"),
//                   detail ("from P5  ·  15 of 15 shots": the line without the name, beside the name's link) }],
//    departuresEmpty ("No shot left the pattern's band." ),
//    repeat: { tightest: REPEAT, loosest: REPEAT, words }   REPEAT = { has, row, rowLabel, tag, iqr (| null), iqrText, words },
//    whatMoved: { top: MOVED, impact: MOVED } (on the medians; seekUs = the picked shot's),
//    address: ADDRESSBLOCK (the session's box per reading, the median as its number), absolutes: [ADDRESS], trust: TRUST }
//
//  selected: int (the playhead's position, 0..7), standfirst: Sentence (thisShot.standfirsts[selected]).
namespace pinpoint::charts {

// THE SESSION REPORT'S POSTURE PAGE (§5.7, stage 3b): the `session` map above for a snapshot, with no
// picked shot (every `picked` null, no shot ringed) and no QObject — built on the caller's thread with
// the shipped pack and norms (postureSharedEnv). `patternMode` −1 Auto, 0 Level, 1 Squat and push.
// The same shape as `session`; `available` false when no shot reads a posture row. `noun`: what its sentences
// count, plural — "shots" as on Charts, "swings" for the report (its own word; session_posture.h decision 14).
// It adds `picture` (posture_picture.h) for the session's TYPICAL swing — the one whose distance to the session's
// pattern is nearest the session's median distance — with `typical` ("A typical swing: swing 4", the presenter's
// ordinal: the report renumbers it) and `typicalWhy`; its frames carry their `still` and no `src` (the report makes them).
QVariantMap postureSessionReportPayload(const SessionSnapshot &snapshot, int patternMode = -1,
                                        const QString &noun = QStringLiteral("shots"));

class ChartsPosturePresenter : public QObject
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
    Q_PROPERTY(int pattern READ pattern WRITE setPattern NOTIFY inputsChanged)
    Q_PROPERTY(QString referenceSwingDir READ referenceSwingDir WRITE setReferenceSwingDir NOTIFY inputsChanged)

    // ── outputs ──
    Q_PROPERTY(bool available READ available NOTIFY resultsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap choices READ choices NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap thisShot READ thisShot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap session READ session NOTIFY resultsChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap standfirst READ standfirst NOTIFY selectedChanged)

public:
    explicit ChartsPosturePresenter(QObject *parent = nullptr);
    ~ChartsPosturePresenter() override;

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
    int pattern() const { return m_in.pattern; }
    void setPattern(int p);
    QString referenceSwingDir() const { return m_in.referenceSwingDir; }
    void setReferenceSwingDir(const QString &d);

    bool available();
    bool busy() const { return m_running; }
    QVariantMap choices();
    QVariantMap thisShot();
    QVariantMap session();
    int selected() const { return m_selected; }
    QVariantMap standfirst() const;

    // The picked shot's window-relative µs of position `tag` ("P1" … "P8"), for shotReplay.seekToUs; −1
    // when the swing has no such position. A lookup into the delivered result.
    Q_INVOKABLE qint64 seekUsFor(const QString &tag) const;

    // THE STILL MAKER (the app's: main.cpp hands it ReportStills): makes the picture's stills, each a
    // thisShot.picture frame's `still` map { swingDir, camera, phase, x, y, w, h, maxPx, outPath }, synchronously, on
    // a pool thread; returns outPath → why for each it could not make ("" or absent = made). Without one (the tests,
    // a build without the renderer) the frames keep src "" and draw their lines on an empty frame.
    using StillMaker = std::function<QHash<QString, QString>(const QVariantList &stills)>;
    static void setStillMaker(StillMaker maker);

    // ── test seams ──
    void setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot);   // used while no model is set
    // The picture's document for a swing folder, from a JSON file (a swing document) instead of the disk's: kept
    // whatever the disk says. An empty path forgets it.
    Q_INVOKABLE void setPictureDocumentForTest(const QString &swingDir, const QString &jsonFile);
    Q_INVOKABLE void setSynchronous(bool on);
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);

    struct Inputs {
        std::shared_ptr<const SessionSnapshot> snapshot;
        QString pickedSwingDir;
        QVariantMap pickedDetail;
        QString reg = QStringLiteral("analyse");
        QString scope = QStringLiteral("shot");
        QString against = QStringLiteral("address");
        int pattern = -1;
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

    void makeStills();

    std::shared_ptr<Result> m_result;
    std::shared_ptr<Worker> m_worker;
    bool m_stillsBusy = false;
    QSet<QString> m_stillsAsked;                       // outPaths handed to the maker (once each)
    QThreadPool m_pool;
};

} // namespace pinpoint::charts
