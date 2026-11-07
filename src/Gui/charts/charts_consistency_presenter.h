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

// THE CONSISTENCY PRESENTER (docs/design/consistency_design.md §3, §4.3 to §4.5): the Consistency tab as words and
// figures the QML only places. The rules are session_consistency.h's, and the rows of a shot or a cell are the ones
// Charts' Apart builds (ConsistencyReadingRows, charts_relations_presenter.h), so the two surfaces cannot disagree.
//
// ── THE INPUTS ───────────────────────────────────────────────────────────────────────────────
// `model` (a SessionSeriesModel), `pickedSwingDir` (shotReplay.swingDir) or `pickedOrdinal`. A swing folder wins, and
// one that is not in the session is no pick. A new session clears `pickedOrdinal` and `openCell`. `openCell` ("area:column", the column 0-based, or ""); `medium`
// ("screen", else paper: nothing to press); `gradePolicy` (AppSettings.diagnosticsGradePolicy, as Charts takes it, for
// the rows' fired words).
//
// ── THE OUTPUTS (all NOTIFY resultsChanged) ──────────────────────────────────────────────────
// A Sentence is { text, runs: [{ text, fig }] } (charts_words.h). A row is a READING ROW (charts_relations_presenter.h).
// THE GRADES (docs/design/system/variability.md; Mark's rulings R1, R2a, R3, R8, R11, 7 Nov). Every reading and cell is
// graded in C++ (consistency_grades.h); a result reaches QML as a RESULT MAP, the façade's variabilityResult field for
// field (variability_facade.h): { grade, graded, basis, ungradedReason, measurementLimited, n, hasRho, rho, basisText,
// domain }. NO VARIABILITY WORD IS SPELLED HERE: the words are the vocabularies' (ConsistencyVocabulary,
// MeasurementVocabulary), and QML puts them in. Two strings are TEMPLATES for that: `key` and `keyMarks` hold %1 to %5,
// the Consistency words of grades 5 to 1, %6 its grade-0 word, %7 the Measurement grade-0 word ("Checking"); the
// headline's `grades` fill its %1 and %2 (below).
//
// head:   { kicker ("Consistency  ·  31 shots  ·  Gap wedge"), headline, ball (Sentence, {} with no carry),
//           notes: [Sentence] (a club left out; carry with no offline) }
//           headline: a Sentence + { kind, grades, fallback }. kind "halves": text "Your backswing is %1, your downswing
//           %2; your sequence moves most." and grades [{ grade, measurementLimited }] for the backswing and the downswing
//           (each half's cellGrade over its cells), the words lower case; `fallback` "Your sequence moves most." for a
//           surface that will not say a half's word (the golfer layer, a measurement-limited half). "most": that
//           sentence alone. "grade0": text "", the Consistency vocabulary's grade-0 line in its place. "ungraded":
//           "Nothing here has a range to judge it against." "empty": what the table cannot say (the empty sentence).
// matrix: { columns: [{ text, short }], rows: [{ id, name, cells: [{ text, short, strong, heavy, on, mark, tap, heat, soft,
//           result }] }] }. PpChMatrix's rectangle: one cell per column. A cell with a graded `result` (its cellGrade, R1)
//           has text "" and QML sets the word: ConsistencyVocabulary.word(grade), or MeasurementVocabulary.word(0)
//           ("Checking") when measurementLimited; "-" for an applicable cell with no graded reading; "" with no result
//           where the column does not apply. Short forms: the grade's figure, "⚠" for Checking, "-". No marks: the marks
//           go on the readings a cell opens to.
// key:    the words of the table (a template), for a note under it in either form. keyMarks: the short forms (a template),
//           for keyText.
// shots:  [{ ordinal, shotId, swingDir, club, flags (["short", "offline"], either or neither), guarded }], strike order
// cell:   {} or { area, areaId, column, tag, result (the cell's), title, sub, rows: [READING ROW + { variability: { kind:
//           "consistency", result }, graded }], unjudged (readings with no scale), unjudgedNote, headLabel, shotLabel,
//           countLabel }. Rows widest spread first (ρ), then those under N_MIN, then those with no scale; each shows the
//           middle of its span, its `place` the reading row's own (this shot's rank). `sub` is a Sentence. On a guarded
//           shot a row resting on the ladder carries its ⚠ as the row's caution.
// shot:   { available: false, note } or { available: true, ordinal, kicker, shotLabel, countLabel, headLabel, lm,
//           sentence (Sentence), carry, guarded, guardNote, groups: [{ name, rows }] (The ball, The club, Your swing, those
//           that have rows), count (rows), note (the co-occurrence line, under two rows "") }
//           A club or swing row of `groups` with eight or more other shots of its club carries `variability`: { kind:
//           "typicality", values (PpVariabilityMark's Typicality shape), basisText ("this swing 4.0° · median 3.1°") }
//           (R8), over the row's own `values`, a guarded shot left out of a reading on its P-ladder; with fewer it has
//           none. A ball row never has one (R4). `carry`: always null (ball flight has no badge); the sentence gives the
//           carry against its club's median.
// empty:  {} or { kind ("noSession" | "noShots" | "noCamera" | "tooFew"), sentence }: what the table cannot say.
//           Everything else is still published: a no-camera session keeps its ball line and its shots.
// status: { ready, shotCount, pickedOrdinal, loading, settling (Sentence "Reading this session…"), timings { jobMs,
//           consistencyMs } }
//           `settling` is always there, from before the first job, for the view to show when it has no table yet.
//           `loading`: the model is still reading a session with no shots to show yet. Until it finishes `empty` stays
//           {}, the head says only "Consistency" and those words, and key, keyMarks and shot.note are "".
//
// ── THREADING AND COST ───────────────────────────────────────────────────────────────────────
// One private single-thread pool, coalesced, delivered by queued invocation; `busy` meanwhile, and the previous
// results stay published. The session-level work (the maths, the head) runs once per snapshot base and revision, so
// picking a shot or a cell costs only its own rows. The env is taken once in the constructor, on the GUI thread, and
// a job holds a copy (consistency_env.h). setSynchronous(true) runs the job inline (tests).
//
// ── THE REPORT ───────────────────────────────────────────────────────────────────────────────
// consistencySessionReportPayload (below): the session report's Consistency page, off the same builders, for one call
// on a worker. Nothing picked and nothing to press, so no ◆ in the table or the key. report/report_consistency.h cuts it.
//
// Unit-tested in src/Gui/tests/charts_consistency_presenter_test.cpp.

#include "charts_relations_presenter.h"     // ConsistencyReadingRows
#include "consistency_grades.h"             // ConsistencyHead

#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace pinpoint::charts {

class IVariabilityScaleProvider;

class ChartsConsistencyPresenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── inputs ──
    Q_PROPERTY(QObject *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString pickedSwingDir READ pickedSwingDir WRITE setPickedSwingDir NOTIFY pickedSwingDirChanged)
    Q_PROPERTY(int pickedOrdinal READ pickedOrdinal WRITE setPickedOrdinal NOTIFY pickedOrdinalChanged)
    Q_PROPERTY(QString openCell READ openCell WRITE setOpenCell NOTIFY openCellChanged)
    Q_PROPERTY(QString medium READ medium WRITE setMedium NOTIFY mediumChanged)
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY gradePolicyChanged)

    // ── outputs ──
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap head READ head NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap matrix READ matrix NOTIFY resultsChanged)
    Q_PROPERTY(QString key READ key NOTIFY resultsChanged)
    Q_PROPERTY(QString keyMarks READ keyMarks NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList shots READ shots NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap cell READ cell NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap shot READ shot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap empty READ empty NOTIFY resultsChanged)

public:
    explicit ChartsConsistencyPresenter(QObject *parent = nullptr);
    ~ChartsConsistencyPresenter() override;

    QObject *model() const { return m_model; }
    void     setModel(QObject *m);
    QString  pickedSwingDir() const { return m_pickedDir; }
    void     setPickedSwingDir(const QString &d);
    int      pickedOrdinal() const { return m_pickedOrdinal; }
    void     setPickedOrdinal(int ordinal);
    QString  openCell() const { return m_openCell; }
    void     setOpenCell(const QString &c);
    QString  medium() const { return m_medium; }
    void     setMedium(const QString &m);
    QString  gradePolicy() const { return m_gradePolicy; }
    void     setGradePolicy(const QString &p);

    bool busy() const { return m_running; }
    bool ready() const;
    QVariantMap  status() const;
    QVariantMap  head() const;
    QVariantMap  matrix() const;
    QString      key() const;
    QString      keyMarks() const;
    QVariantList shots() const;
    QVariantMap  cell() const;
    QVariantMap  shot() const;
    QVariantMap  empty() const;

    // The table in one sentence (doc §3), as `head.headline` (above): consistencyHead's kind as words, the grade words
    // left for QML.
    static QVariantMap headline(const ConsistencyHead &head);

    // Test seams.
    Q_INVOKABLE void setSynchronous(bool on) { m_synchronous = on; }
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);
    void setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot);     // replaces `model`'s
    int sessionRuns() const;                        // how many times the session-level work has run
    const void *lastJobThread() const;              // the QThread that ran the latest job

    struct Output;
    struct Cache;

signals:
    void modelChanged();
    void pickedSwingDirChanged();
    void pickedOrdinalChanged();
    void openCellChanged();
    void mediumChanged();
    void gradePolicyChanged();
    void busyChanged();
    void resultsChanged();

private:
    void onSnapshotChanged();
    void schedule();
    void start();
    void deliver(std::shared_ptr<const Output> out);
    std::shared_ptr<const SessionSnapshot> currentSnapshot() const;

    // inputs
    QPointer<QObject> m_model;
    std::shared_ptr<const SessionSnapshot> m_directSnapshot;
    QString m_pickedDir, m_openCell, m_medium = QStringLiteral("screen"), m_gradePolicy = QStringLiteral("standard");
    int     m_pickedOrdinal = 0;
    QString m_sessionDir;               // the session the pick and the open cell belong to

    // outputs
    std::shared_ptr<const Output> m_out;

    // the worker
    QThreadPool m_pool;
    std::shared_ptr<Cache> m_cache;     // touched only inside a job (jobs never overlap)
    ConsistencyEnv m_env;               // taken on the GUI thread; a job holds a copy
    std::shared_ptr<const IVariabilityScaleProvider> m_scales;   // made on the GUI thread; a job holds a reference
    bool m_synchronous = false;
    bool m_running = false;
    bool m_dirty = false;
};

// THE SESSION REPORT'S READING (doc §3, Print): the head, the table and every launch-monitor flagged shot, in the tab's
// own words and off its own rows. Runs on the caller's thread and holds nothing. `env` and `norms` are taken on the GUI
// thread and handed over, as a job's are (consistency_env.h); `noun` is the kicker's plural ("swings" in the report).
//   { available (the table has a word, or there is a ball line, or a shot is flagged),
//     kicker, headline, ball, notes  (as `head`),
//     empty (string: what the table cannot say, "" when it can, or when the headline has said it),
//     table (bool: any cell is graded), matrix, key, keyMarks (templates, as the tab's; nothing picked, so no ◆),
//     flagged: [`shot` + { swingDir, club }], in strike order, ball rows first }
// `scales`: the variability scale provider (variability_env.h); null makes the production one here, which is safe once
// `env` has been taken on the GUI thread.
QVariantMap consistencySessionReportPayload(const std::shared_ptr<const SessionSnapshot> &snapshot, const ConsistencyEnv &env,
                                            const QString &gradePolicy,
                                            const std::shared_ptr<const pinpoint::analysis::INormProvider> &norms,
                                            const QString &noun = QStringLiteral("shots"),
                                            const IVariabilityScaleProvider *scales = nullptr);

} // namespace pinpoint::charts
