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

// THE REPORT MODEL (docs/design/session_report_design.md §8.1, §11) — the façade the session list and
// the report sheet drive. It READS THE SESSION'S OWN DIAGNOSIS (refactor step 3.7-d): the Session's one
// SessionDiagnostics, through that Session's thin adapter for the "report" surface (SessionDiagnosticsModel made by C++,
// session_diagnostics_adapter.h), reached through the door main.cpp hands it (setDiagnosisDoor: app.sessionAt, then
// diagnosticsAdapters.of) — never a SessionDiagnosticsModel of its own. It builds the one `report` map the register binds
// to (every string made here, none in QML: build/report-build/REPORT_MAP.md is the schema), has the stills made, and asks
// PpReportPrintHost to lay the register out on a hidden A4 sheet and print it. Context property `sessionReport`.
//
// ── What a make runs, in order ──────────────────────────────────────────────────────────────────
//   1. the session's diagnosis is asked for (the door): the adapter takes the owner's applied diagnosis — its rows as
//      the reduction read them, so each shot AS THE GOLFER ANNOTATED IT on the launch monitor board (its club override,
//      a reading left out not read: board_annotations.h), its ledgers, links and stage — and the report waits until the
//      owner is idle; meanwhile the swing documents' small blocks — launch monitor, athlete, clock, cameras — are
//      read on a worker thread, and, once the owner is idle, the session's own series (the snapshot the Charts tab
//      reads: SessionSeries, refactor 5.5a) is taken on the GUI thread and, on another, the Posture, Wrist, Club delivery and Consistency
//      presenters' session payloads read off it (postureSessionReportPayload, wristSessionReportPayload,
//      deliverySessionReportPayload, consistencySessionReportPayload) for those four pages — the swings as the golfer
//      annotated them on the board, as the Charts tab publishes them (J-45) — and the Tendencies page's (the board's
//      left-out readings and club overrides honoured: WorkOnsController::tendenciesAsOf with the pack's monitor measures);
//   2. once all three have landed, the work-on list is derived FRESH (sessionWorkOns() over the owner's rows and
//      ledgers, never work_ons.json, which can be stale after a delete), the same-day sessions' records are read for
//      "Earlier today", and the pure reductions (src/Coaching/session_report.h) choose what to say. The launch monitor
//      board's own model (a private LmSessionModel over the session's shots) is run once, and EVERY launch monitor
//      figure — cover, numbers table, carry row, reference swing, appendix — reads its shots through report_lm.h, so
//      a shot the golfer left out or re-labelled on the board is treated the same on every page (F-03);
//   3. the stills are made off the GUI thread (ReportStills::renderAll) unless photos are off;
//   4. the map is assembled (report_words.h holds every sentence template) and handed to the print host
//      (printRequested); the host prints it and answers printFinished;
//   5. report_meta.json is written beside the PDF (note, options, photos, prepared times, SHA-256, size,
//      the fingerprint), the adapter is let go (with it the report's use of the Session), and made() / stateChanged() fire.
// prepare() runs 1–4 without the print, for the probe that dumps the map (tools/session-report-mock/
// dump_report.qml) and for any surface that wants the words without a PDF.
//
// A folder no Session holds (one no athlete of the library lists; a session with no golfer is the Unassigned owner's and
// reported since step 5.1; refactor 5.4b removed the old engine's model for a folder another athlete id names) has no
// diagnostics, so no report is made and
// the sheet says why (NOTES-d.md J-d2).
//
// ── What it never does ──────────────────────────────────────────────────────────────────────────
// It writes nothing of the session's analysis: no diagnostics.json, no ratchet, no work_ons.json, no
// club_baseline.json (its adapter judges and writes nothing; it reviews). The one write a report can CAUSE is not its own: asking for a session no screen has loaded loads it,
// and its owner — the session's one writer of diagnostics.json (DEC-135) — re-judges the shots whose stamps are stale
// and writes them, exactly as it does when the panel or the catch-up loads the same session.
//
// state() and preflight() are cheap and synchronous — report_meta.json, the fingerprint (the work-on
// record's recipe plus the studio's and the photo rule's inputs) and a directory listing — and never
// activate a session, so the session list can ask them for every row.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

class StudioProfile;
class AthleteController;
class SessionDiagnosticsModel;
class ReportStills;

namespace pinpoint { class AthleteLevel; class ShotSource; }

class SessionReportModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    busy          READ busy          NOTIFY busyChanged)
    Q_PROPERTY(QString busySession   READ busySession   NOTIFY busyChanged)
    Q_PROPERTY(int     progressPage  READ progressPage  NOTIFY progressChanged)
    Q_PROPERTY(int     progressPages READ progressPages NOTIFY progressChanged)
    // The session's diagnosis the make reads (its adapter: setDiagnosisDoor), exposed for the length of a print ONLY (null
    // otherwise): the print host feeds the technical appendix (PpDiagPrint) from it exactly as diag_print_pdf.qml feeds it
    // from the panel's body — cards, headerInfo, detail, and shotReadout() for the reference swing.
    Q_PROPERTY(QObject *printModel   READ printModel    NOTIFY printModelChanged)
    // The shot the appendix reads (the reference swing) and the condition its detail page opens on
    // (work-on 1); -1 / "" when there is none. Valid while printModel is.
    Q_PROPERTY(int     printShotId   READ printShotId   NOTIFY printModelChanged)

public:
    explicit SessionReportModel(QObject *parent = nullptr);
    ~SessionReportModel() override;

    void setStudioProfile(StudioProfile *studio);
    void setAthleteController(AthleteController *athletes);
    // THE SESSION'S DIAGNOSIS (refactor step 3.7-d): the model a make reads for `sessionDir` — the Session's thin adapter
    // for the "report" surface, or the empty adapter when no Session holds it (the header).
    // main.cpp: diagnosticsAdapters.of(app.sessionAt(dir), "report", dir). Never null when set.
    void setDiagnosisDoor(std::function<SessionDiagnosticsModel *(const QString &sessionDir)> door) { m_diagnosisDoor = std::move(door); }
    // THE ATHLETE LEVEL A MAKE READS (refactor 3.7-f): the session's athlete's AthleteDiagnostics — its Tendencies page as
    // of the session (tendenciesAsOf) and the earlier sessions of the day ("Earlier today", their summaries) — or null.
    void setAthleteDoor(std::function<pinpoint::AthleteLevel *(const QString &sessionDir)> door) { m_athleteDoor = std::move(door); }
    // Whether the session at `sessionDir` has no golfer (the Unassigned owner's: information_model.md §5.11). Such a
    // report reads no athlete level and no same-day siblings — they are other golfers' — and says so (step 5.1 review B1).
    void setNoGolferTest(std::function<bool(const QString &sessionDir)> test) { m_noGolfer = std::move(test); }
    // THE SESSION'S SHOTS for the report's temporary carousel (refactor step 5.2; R-QML-5): a view over the Session being
    // reported, made through this (main.cpp: app.sessionAt, then a SessionShotSource) — the launch monitor's figures
    // come off its rows. Without it the carousel lists nothing and the report has no launch monitor figures.
    void setShotSourceFor(std::function<std::unique_ptr<pinpoint::ShotSource>(const QString &sessionDir)> sourceFor)
    { m_shotSourceFor = std::move(sourceFor); }

    bool    busy() const;
    QString busySession() const;
    int     progressPage() const;
    int     progressPages() const;
    QObject *printModel() const;
    int     printShotId() const;

    // {canReport, swings, photos:{included, reason}, missing:[{text, where}], lastNote}
    Q_INVOKABLE QVariantMap preflight(const QString &sessionDir) const;
    // Asynchronous; ends in made(). options: {note}
    Q_INVOKABLE void make(const QString &sessionDir, const QVariantMap &options);
    // The map without the print: asynchronous, ends in prepared(). Stills are made (into the cache) so
    // the map is the one a make would print. options: {note, stills: false to skip them}
    Q_INVOKABLE void prepare(const QString &sessionDir, const QVariantMap &options);
    // Cheap (report_meta.json + fingerprint); never opens the session.
    // {state:"none"|"current"|"stale", pdfPath, preparedMs, sizeBytes, words, lastSend:{ms, method}, sentDiffers}
    Q_INVOKABLE QVariantMap state(const QString &sessionDir) const;
    Q_INVOKABLE void recordSend(const QString &sessionDir, const QString &method);
    // The print host's answer to printRequested().
    Q_INVOKABLE void printFinished(bool ok, const QString &path, int pages, const QString &error);
    Q_INVOKABLE void printProgress(int page, int pages);

    // The fingerprint state() compares against — the work-on record's recipe (rule version, content
    // stamp, each swing document) plus inputsStamp(). Public for the probes and the tests.
    QString fingerprintOf(const QString &sessionDir, bool atMake = false) const;   // atMake: the make's own (R2-B1)
    // fingerprintOf() with its inputs (the studio, the athletes, the content stamp) copied now, for a
    // thread that may not touch this model: the archiver keeps a current report current (S-D6).
    std::function<QString(const QString &sessionDir)> fingerprintFunction() const;

signals:
    void busyChanged();
    void progressChanged();
    void printModelChanged();
    void made(const QString &sessionDir, bool ok, const QString &pdfPath, const QString &error);
    void prepared(const QString &sessionDir, bool ok, const QVariantMap &report, const QString &error);
    void stateChanged(const QString &sessionDir);
    // To PpReportPrintHost: lay out PpSessionReport with this map and print it to `path`.
    void printRequested(const QVariantMap &report, const QString &path);

private:
    struct Job;

    void start(const QString &sessionDir, const QVariantMap &options, bool print);
    void onModelSettled();
    void letModelGo();   // the job's model let go: an adapter deleted (its use of the Session dropped)
    void onDocsRead();
    void onChartsRead();
    void maybeBuild();
    void build();
    void deliver();
    void fail(const QString &error);
    void finishJob();

    // The report's other inputs, as one stamp (the studio's details and the photo rule's), so a
    // studio edit or a change of approval puts the report out of date (§8.2).
    QString inputsStamp(const QString &sessionDir, bool atMake = false) const;
    // The athlete a session belongs to: the athlete whose folder holds it (D4), and only that — never by a
    // name or a swing's recorded uuid (E-02; J-66, step 2.8b). {} when no athlete's folder holds it.
    QVariantMap athleteFor(const QString &sessionDir) const;
    QString contentStamp() const;

    QPointer<StudioProfile>     m_studio;
    QPointer<AthleteController> m_athletes;
    std::function<SessionDiagnosticsModel *(const QString &)> m_diagnosisDoor;
    std::function<pinpoint::AthleteLevel *(const QString &)> m_athleteDoor;
    std::function<bool(const QString &)>                     m_noGolfer;
    std::function<std::unique_ptr<pinpoint::ShotSource>(const QString &)> m_shotSourceFor;
    QPointer<SessionDiagnosticsModel> m_model;          // the job's (the door's answer); null between makes
    QMetaObject::Connection     m_modelBusy;            // its busyChanged, for the job's length
    QMetaObject::Connection     m_modelGone;            // its destroyed: the session went away mid-make (B1)
    ReportStills               *m_stills = nullptr;
    std::unique_ptr<Job>        m_job;
    mutable QString             m_contentStamp;     // the pack + norms a model would grade on, computed once
    bool                        m_printing = false;
    int                         m_printShotId = -1;
    int                         m_progressPage = 0;
    int                         m_progressPages = 0;
};
