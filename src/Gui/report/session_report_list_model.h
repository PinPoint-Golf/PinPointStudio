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

// ONE GOLFER'S SESSIONS, EACH WITH ITS REPORT STATE (docs/design/session_report_design.md §8.0) — the
// list at the foot of Home and on the golfer page's Sessions tab. Context property `sessionReportList`.
//
// A row is one session folder that holds at least one swing:
//   "Tue 20 Oct 2026 · Session 3"   the SESSION date (the first swing's wall-clock, local time) and the
//                                   n-th session that day, counted in start order
//   clubs · swings · devices        the session drawer's own summary (session_summary.h over each
//                                   swing's cheap summary read — never the fat document parse), and its
//                                   device labels
//   work-on 1                       the session's work_ons.json, under the CURRENT rule only (a record
//                                   from an older rule is about to be re-derived by WorkOnsController,
//                                   whose itemsChanged refreshes this list); its goal is the pack's
//                                   `goal` for that condition, through the resolver main.cpp installs,
//                                   else the record's own name
//   the report state                SessionReportModel::state(dir) — report_meta.json and the
//                                   fingerprint, never the session itself
//
// TWO PASSES. The scan (directory listings, summary reads, work-on records) runs on a pool thread,
// generation-counted so a golfer changed mid-scan drops the stale result — WorkOnsController::scan /
// applyScan's pattern. The report state is asked on the GUI thread afterwards, a few rows per turn of
// the event loop, newest first: state() belongs to an object that lives there (it reads the studio's
// and the golfer's details), and it stats every swing document of the session for the fingerprint, so
// a long library asked in one go would be a visible stall. Home's ten rows land on the first turn.
//
// The state function and the goal resolver are injected rather than linked so the list's test runs
// over a temp library without the report model or the pack behind it (report_list_test).

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>
#include <QVector>

#include <functional>

class SessionReportModel;

class SessionReportListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // True while a scan is running (the first one included). The list says "Reading sessions…" only
    // while it has no rows to show.
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
public:
    enum Role {
        DirRole = Qt::UserRole + 1, DateLabelRole, SessionOfDayRole, ClubsRole, SwingsRole, DevicesRole,
        WorkOn1Role, IsLiveRole, CanReportRole, StateRole, StateWordsRole, PdfPathRole, SizeBytesRole,
        // Beyond the contract's list, additive: the session's start (epoch ms; 0 when no swing says),
        // for the email's date words (§8.4); whether the record said anything (false: no current
        // work-on record yet, so line 2 stays quiet rather than claiming "no habits"); whether the
        // state has been asked yet; the PDF on disk is not the one that last went out.
        SessionMsRole, WorkOnKnownRole, StateKnownRole, SentDiffersRole, PreparedMsRole,
    };
    Q_ENUM(Role)

    // {state, pdfPath, preparedMs, sizeBytes, words, lastSend, sentDiffers} — SessionReportModel::state().
    using StateFn = std::function<QVariantMap(const QString &sessionDir)>;
    // The pack's `goal` for a condition id; empty when it has none.
    using GoalFn  = std::function<QString(const QString &conditionId)>;

    explicit SessionReportListModel(QObject *parent = nullptr);
    ~SessionReportListModel() override;

    // Installs reports->state() as the state function and follows its stateChanged(dir) for that one
    // row. Called through the meta-object, so this file links nothing of the model's.
    void setReportModel(SessionReportModel *reports);
    void setStateFunction(StateFn fn);
    void setGoalResolver(GoalFn fn);
    void setAthleteDir(const QString &dir);
    QString athleteDir() const { return m_athleteDir; }
    // The running session's folder (SessionReviewController::liveSessionDir); empty when none. Its row
    // offers Open session only, and the list re-scans because its swing count is moving.
    void setLiveSessionDir(const QString &dir);
    // Tests: scan on the calling thread and ask every state at once.
    void setSynchronous(bool on) { m_synchronous = on; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    bool scanning() const { return m_scanning; }

    Q_INVOKABLE void refresh();
    // A row as a map (QML's delegates have the roles; the sheet and the probes want one row by dir).
    Q_INVOKABLE QVariantMap rowFor(const QString &sessionDir) const;

    // One row, as the scan builds it. Public for the test.
    struct Row {
        QString     dir;
        qint64      startMs = 0;        // the earliest swing's wall-clock; 0 when no swing says
        qint64      sortMs  = 0;        // startMs, else the folder's date / mtime
        QString     dateLabel;          // "Tue 20 Oct 2026"
        int         sessionOfDay = 0;   // 1-based, in start order within the day
        QString     clubs;              // "7 iron, Driver"
        int         swings = 0;
        QStringList devices;            // display labels, the drawer's order and words
        bool        workOnKnown = false;
        QString     workOn1Id, workOn1;
        bool        isLive = false;
        QString     state;              // "none" | "current" | "stale"; empty until asked
        QString     stateWords;
        QString     pdfPath;
        qint64      sizeBytes  = 0;
        qint64      preparedMs = 0;
        bool        sentDiffers = false;
    };
    // The worker half. Touches no member; pure apart from the file system.
    static QVector<Row> scan(const QString &athleteDir, qint64 nowMs);

public slots:
    // Re-asks one session's report state (a report made, a send recorded) without a re-scan. A slot,
    // not an invokable, because setReportModel() connects stateChanged(QString) to it by name.
    void refreshState(const QString &sessionDir);

signals:
    void countChanged();
    void scanningChanged();
    // A scan has been applied (its rows are in; their states may still be arriving).
    void refreshed();

private:
    void applyScan(int generation, QVector<Row> rows);
    void askStates();
    void applyState(int row);
    static void markLive(QVector<Row> &rows, const QString &liveDir);

    QString        m_athleteDir;
    QString        m_liveDir;
    QVector<Row>   m_rows;
    StateFn        m_stateFn;
    GoalFn         m_goalFn;
    QThreadPool    m_pool;
    int            m_generation = 0;
    int            m_stateCursor = 0;       // next row askStates() asks
    int            m_stateGeneration = 0;   // a re-scan abandons the previous pass
    bool           m_scanning = false;
    bool           m_rescan = false;
    bool           m_synchronous = false;
};
