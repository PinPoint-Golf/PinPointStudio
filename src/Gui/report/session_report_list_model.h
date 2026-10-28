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
// list on the rail's Sessions screen (ScreenSessions). Context property `sessionReportList`.
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
//
// THE DEVICE FILTER is the session drawer's (DeviceFilterModel): the chips over the list keep the saved
// sessions that used every chosen device, the live one always. The rows stay where they are — the report
// states are asked and answered by row — and each says where it falls among those shown (shownRank, -1
// when filtered out), so Home's "first ten" are the first ten that pass.
//
// THE SESSIONS SCREEN'S FILTERS (sessions_screen_redesign.md §2.1) narrow the same way, beside the device
// filter and ANDed with it: a club, a report state, archived sessions hidden unless asked for, and a search
// over work-on 1 and the clubs. Each filter's label counts (reportCounts, clubCounts, archivedCount, and the
// device chips) are taken over the saved sessions the OTHER filters pass, so a label is what clicking it
// would show. The live row is never filtered out and never counted.

#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>
#include <QVector>

#include <functional>

#include "../review/device_filter_model.h"

class SessionReportModel;

class SessionReportListModel : public DeviceFilterModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Rows that pass the device filter, the live one included: what the list shows.
    Q_PROPERTY(int shownCount READ shownCount NOTIFY countsChanged)
    // True while a scan is running (the first one included). The list says "Reading sessions…" only
    // while it has no rows to show.
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // A club's display label ("7 iron"), "" for all: sessions that hit it.
    Q_PROPERTY(QString clubFilter READ clubFilter WRITE setClubFilter NOTIFY clubFilterChanged)
    // "" | "none" | "stale" | "ready" (current, sent or not). A row whose state is not asked yet passes "" only.
    Q_PROPERTY(QString reportFilter READ reportFilter WRITE setReportFilter NOTIFY reportFilterChanged)
    // Archived sessions (SessionArchiver's marker) are hidden unless this is on.
    Q_PROPERTY(bool showArchived READ showArchived WRITE setShowArchived NOTIFY showArchivedChanged)
    // Case-insensitive, over work-on 1 as shown (the goal) and the clubs; "" for all. No notes yet (S-D2).
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    // The filter labels. { none, stale, ready }: saved sessions per report state.
    Q_PROPERTY(QVariantMap reportCounts READ reportCounts NOTIFY countsChanged)
    // [{ club, count }], most sessions first, then by name; the chosen club always listed.
    Q_PROPERTY(QVariantList clubCounts READ clubCounts NOTIFY countsChanged)
    // Archived sessions that showArchived would add.
    Q_PROPERTY(int archivedCount READ archivedCount NOTIFY countsChanged)
    // Over the rows shown, the live one included: the header's "10 sessions · 202 swings · 14.6 GB".
    Q_PROPERTY(int shownSwings READ shownSwings NOTIFY countsChanged)
    Q_PROPERTY(double shownBytes READ shownBytes NOTIFY countsChanged)
public:
    enum Role {
        DirRole = Qt::UserRole + 1, DateLabelRole, SessionOfDayRole, ClubsRole, SwingsRole, DevicesRole,
        WorkOn1Role, IsLiveRole, CanReportRole, StateRole, StateWordsRole, PdfPathRole, SizeBytesRole,
        // Beyond the contract's list, additive: the session's start (epoch ms; 0 when no swing says),
        // for the email's date words (§8.4); whether the record said anything (false: no current
        // work-on record yet, so line 2 stays quiet rather than claiming "no habits"); whether the
        // state has been asked yet; the PDF on disk is not the one that last went out.
        SessionMsRole, WorkOnKnownRole, StateKnownRole, SentDiffersRole, PreparedMsRole,
        // The device tokens behind DevicesRole's labels, same order (the filter's ids); and the row's
        // place among those the device filter shows, -1 when it is filtered out.
        DeviceTokensRole, ShownRankRole,
        // The Sessions screen (sessions_screen_redesign.md): the folder's size on disk; whether it is
        // archived; the report cell's short words — "No report", "Out of date", "Ready", "Sent 21 Oct"
        // (sent by the app and still the PDF on disk) — empty until the state is asked.
        SessionBytesRole, ArchivedRole, StateShortRole,
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
    int shownCount() const;
    bool scanning() const { return m_scanning; }

    QString clubFilter() const { return m_clubFilter; }
    void    setClubFilter(const QString &club);
    QString reportFilter() const { return m_reportFilter; }
    void    setReportFilter(const QString &filter);
    bool    showArchived() const { return m_showArchived; }
    void    setShowArchived(bool on);
    QString search() const { return m_search; }
    void    setSearch(const QString &text);
    QVariantMap  reportCounts() const;
    QVariantList clubCounts() const;
    int     archivedCount() const;
    int     shownSwings() const;
    double  shownBytes() const;

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
        QStringList clubList;           // the same labels, one each
        int         swings = 0;
        QStringList devices;            // display labels, the drawer's order and words
        QStringList deviceTokens;       // their session_summary.h tokens, same order
        bool        workOnKnown = false;
        QString     workOn1Id, workOn1;
        bool        isLive = false;
        qint64      sessionBytes = 0;   // the folder on disk, every file
        bool        archived = false;   // SessionArchiver's marker
        QString     state;              // "none" | "current" | "stale"; empty until asked
        QString     stateWords;
        QString     stateShort;         // StateShortRole's words
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
    void clubFilterChanged();
    void reportFilterChanged();
    void showArchivedChanged();
    void searchChanged();
    // A scan has been applied (its rows are in; their states may still be arriving).
    void refreshed();

protected:
    QVector<QStringList> savedDeviceSets() const override;
    void applyDeviceFilter() override;

private:
    // The filters a saved row fails, one bit each, so a count can leave its own filter out.
    enum FilterBit { DeviceBit = 1, ClubBit = 2, ReportBit = 4, ArchivedBit = 8, SearchBit = 16 };
    int  failedFilters(const Row &r) const;
    bool rerank();                          // m_shownRank from m_rows and the filters; true when a rank moved
    void refilter();                        // a filter changed: re-rank in place and say so
    void statesLanded();                    // report states arrived: the report filter and counts move
    void applyScan(int generation, QVector<Row> rows);
    void askStates();
    void applyState(int row);
    static void markLive(QVector<Row> &rows, const QString &liveDir);

    QString        m_athleteDir;
    QString        m_liveDir;
    QString        m_clubFilter;
    QString        m_reportFilter;
    QString        m_search;
    bool           m_showArchived = false;
    QVector<Row>   m_rows;
    QVector<int>   m_shownRank;             // per row: its place among those shown, -1 when filtered out
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
