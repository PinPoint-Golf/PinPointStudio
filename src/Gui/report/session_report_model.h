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
// the report sheet drive. It owns a private, panel-less SessionDiagnosticsModel (the pattern
// WorkOnsController uses), builds the one `report` map the register binds to (every string made here,
// none in QML: build/report-build/REPORT_MAP.md is the schema), has the stills made, and asks
// PpReportPrintHost to lay the register out on a hidden A4 sheet and print it. Context property
// `sessionReport`.
//
// ── What a make runs, in order ──────────────────────────────────────────────────────────────────
//   1. the private model is pointed at the session (activateSession, reviewing, the app's grade
//      policy) while the swing documents' small blocks — launch monitor, athlete, clock, cameras — are
//      read on a worker thread, and, on another, the Charts panel's session snapshot of the swings is
//      built (a private SessionSeriesModel, inline) and the Posture presenter's session payload read off
//      it (postureSessionReportPayload) for the Posture page;
//   2. once all three have landed, the work-on list is derived FRESH (sessionWorkOns(), never work_ons.json,
//      which can be stale after a delete), the same-day sessions' records are read for "Earlier today",
//      and the pure reductions (src/Coaching/session_report.h) choose what to say;
//   3. the stills are made off the GUI thread (ReportStills::renderAll) unless photos are off;
//   4. the map is assembled (report_words.h holds every sentence template) and handed to the print host
//      (printRequested); the host prints it and answers printFinished;
//   5. report_meta.json is written beside the PDF (note, options, photos, prepared times, SHA-256, size,
//      the fingerprint), the private model lets the session go, and made() / stateChanged() fire.
// prepare() runs 1–4 without the print, for the probe that dumps the map (tools/session-report-mock/
// dump_report.qml) and for any surface that wants the words without a PDF.
//
// ── What it never does ──────────────────────────────────────────────────────────────────────────
// It never writes into the session's analysis on its own account: no diagnostics.json of its own making,
// no ratchet, no closeSession, no work_ons.json, no fault profile. The ONE write it cannot prevent is the
// diagnostics model's own reconcile: activateSession() regrades rows that went stale (a re-analysis, a
// content change) and persists them, exactly as the panel and the work-ons catch-up do when they open
// the same session. SessionDiagnosticsModel has no read-only switch today; see the .cpp (activate()).
//
// state() and preflight() are cheap and synchronous — report_meta.json, the fingerprint (the work-on
// record's recipe plus the studio's and the photo rule's inputs) and a directory listing — and never
// activate a session, so the session list can ask them for every row.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <memory>

class StudioProfile;
class AthleteController;
class SessionDiagnosticsModel;
class ReportStills;

class SessionReportModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    busy          READ busy          NOTIFY busyChanged)
    Q_PROPERTY(QString busySession   READ busySession   NOTIFY busyChanged)
    Q_PROPERTY(int     progressPage  READ progressPage  NOTIFY progressChanged)
    Q_PROPERTY(int     progressPages READ progressPages NOTIFY progressChanged)
    // The private diagnostics model, exposed for the length of a print ONLY (null otherwise): the print
    // host feeds the technical appendix (PpDiagPrint) from it exactly as diag_print_pdf.qml feeds it from
    // the panel's body — cards, headerInfo, detail, and shotReadout() for the reference swing.
    Q_PROPERTY(QObject *printModel   READ printModel    NOTIFY printModelChanged)
    // The shot the appendix reads (the reference swing) and the condition its detail page opens on
    // (work-on 1); -1 / "" when there is none. Valid while printModel is.
    Q_PROPERTY(int     printShotId   READ printShotId   NOTIFY printModelChanged)

public:
    explicit SessionReportModel(QObject *parent = nullptr);
    ~SessionReportModel() override;

    void setStudioProfile(StudioProfile *studio);
    void setAthleteController(AthleteController *athletes);

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
    QString fingerprintOf(const QString &sessionDir) const;

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
    SessionDiagnosticsModel *model();
    void onModelSettled();
    void onDocsRead();
    void onPostureRead();
    void maybeBuild();
    void build();
    void deliver();
    void fail(const QString &error);
    void finishJob();

    // The report's other inputs, as one stamp (the studio's details and the photo rule's), so a
    // studio edit or a change of approval puts the report out of date (§8.2).
    QString inputsStamp(const QString &sessionDir) const;
    // The athlete a session belongs to: by the first swing's uuid when known, else by the session
    // folder's parent (the library names an athlete's folder after their sanitised name).
    QVariantMap athleteFor(const QString &sessionDir, const QString &uuid = QString(), const QString &name = QString()) const;
    QString contentStamp() const;

    QPointer<StudioProfile>     m_studio;
    QPointer<AthleteController> m_athletes;
    SessionDiagnosticsModel    *m_model = nullptr;
    ReportStills               *m_stills = nullptr;
    std::unique_ptr<Job>        m_job;
    mutable QString             m_contentStamp;     // the private model's pack + norms, computed once
    bool                        m_printing = false;
    int                         m_printShotId = -1;
    int                         m_progressPage = 0;
    int                         m_progressPages = 0;
};
