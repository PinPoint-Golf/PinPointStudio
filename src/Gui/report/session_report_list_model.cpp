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

#include "session_report_list_model.h"

#include "report_meta.h"
#include "session_report_model.h"
#include "../review/session_summary.h"
#include "../session/session_list_presenter.h"   // the one session list (refactor step 5.7a)

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>

namespace {

// Rows asked for their report state per turn of the event loop. Home shows ten; the first turn
// covers them, and a long library fills in below without a stall.
constexpr int kStatesPerTurn = 12;

// The words for a state the model did not word itself (report_meta.h's, never a second set).
QString wordsFor(const QString &state)
{
    using namespace pinpoint::report;
    if (state == QLatin1String("stale")) return reportStateWords(ReportState::Stale, std::nullopt);
    return reportStateWords(ReportState::None, std::nullopt);
}

// The report cell's words (S-D5): the state in one or two words, a sent report by the day it went. "Sent"
// is the app's own send only (the server took it) and only while the PDF on disk is the one that went;
// a hand-off to Mail is not a send (report_meta.h), so that report reads Ready, its full words in the
// tooltip.
QString shortWordsFor(const QString &state, const QVariantMap &st)
{
    if (state == QLatin1String("stale"))   return QStringLiteral("Out of date");
    if (state != QLatin1String("current")) return QStringLiteral("No report");
    const QVariantMap last = st.value(QStringLiteral("lastSend")).toMap();
    if (last.value(QStringLiteral("method")).toString() == QLatin1String("smtp")
        && !st.value(QStringLiteral("sentDiffers")).toBool()) {
        const qint64 ms = qint64(last.value(QStringLiteral("ms")).toDouble());
        return QStringLiteral("Sent ") + QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(ms).date(),
                                                                 QStringLiteral("d MMM"));
    }
    return QStringLiteral("Ready");
}

// The report filter's name for a state: "ready" covers current, sent or not. Empty until asked.
QString reportBucket(const QString &state)
{
    if (state == QLatin1String("current")) return QStringLiteral("ready");
    return state;   // "none" | "stale" | ""
}

} // namespace

SessionReportListModel::SessionReportListModel(QObject *parent) : DeviceFilterModel(parent)
{
}

SessionReportListModel::~SessionReportListModel() = default;

void SessionReportListModel::setReportModel(SessionReportModel *reports)
{
    // By name through the meta-object, not by member pointer: this file then needs the model's header
    // and none of its code, so the list's test links without it.
    QObject *obj = static_cast<QObject *>(reports);
    if (!obj) { m_stateFn = nullptr; return; }
    m_stateFn = [obj](const QString &dir) {
        QVariantMap out;
        QMetaObject::invokeMethod(obj, "state", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, out),
                                  Q_ARG(QString, dir));
        return out;
    };
    connect(obj, SIGNAL(stateChanged(QString)), this, SLOT(refreshState(QString)));
}

void SessionReportListModel::setStateFunction(StateFn fn) { m_stateFn = std::move(fn); }
void SessionReportListModel::setGoalResolver(GoalFn fn) { m_goalFn = std::move(fn); }

void SessionReportListModel::setSessionList(pinpoint::SessionListPresenter *list)
{
    if (m_list == list) return;
    if (m_list) disconnect(m_list, nullptr, this, nullptr);
    m_list = list;
    if (!m_list) return;
    connect(m_list, &pinpoint::SessionListPresenter::rowsChanged, this, &SessionReportListModel::rowsMoved);
    connect(m_list, &pinpoint::SessionListPresenter::rowChanged, this, &SessionReportListModel::rowMoved);
    connect(m_list, &pinpoint::SessionListPresenter::loadingChanged, this, &SessionReportListModel::scanningChanged);
}

void SessionReportListModel::setAthlete(const pinpoint::AthleteId &athlete)
{
    if (m_athlete == athlete) return;
    m_athlete = athlete;
    // The previous golfer's sessions must not sit under this one's name while the presenter reads.
    beginResetModel();
    m_rows.clear();
    rerank();
    endResetModel();
    emit countChanged();
    emit countsChanged();
    emit availableDevicesChanged();
    emit scanningChanged();
    // Rows the presenter already holds land at once; a group read for the first time lands when its reload does.
    if (m_list && m_list->loaded(m_athlete))
        applyRows(rowsFrom(m_list->rowsOf(m_athlete)), true);
    else if (m_list && m_list->has(m_athlete) && !m_list->loading(m_athlete))
        m_list->reload(m_athlete);
}

bool SessionReportListModel::scanning() const
{
    return m_list && !m_athlete.isNull() && m_list->has(m_athlete) && (m_list->loading(m_athlete) || !m_list->loaded(m_athlete));
}

void SessionReportListModel::setLiveSessionDir(const QString &dir)
{
    const QString abs = dir.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    if (m_liveDir == abs) return;
    m_liveDir = abs;
    // The rows again from the one session list, so a session that has ended goes back to its place (and its report can
    // be made); the known states are kept. Its swings follow the Session's own events.
    if (m_list && !m_athlete.isNull() && m_list->loaded(m_athlete)) {
        applyRows(rowsFrom(m_list->rowsOf(m_athlete)), false);
        return;
    }
    beginResetModel();
    markLive(m_rows, m_liveDir);
    rerank();
    endResetModel();
    emit countsChanged();
    emit availableDevicesChanged();
}

int SessionReportListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int SessionReportListModel::count() const { return int(m_rows.size()); }

int SessionReportListModel::shownCount() const
{
    return int(std::count_if(m_shownRank.cbegin(), m_shownRank.cend(), [](int r) { return r >= 0; }));
}

// The saved sessions the other filters pass: the device chips count over these, like every other label.
QVector<QStringList> SessionReportListModel::savedDeviceSets() const
{
    QVector<QStringList> out;
    for (const Row &r : m_rows)
        if (!r.isLive && (failedFilters(r) & ~DeviceBit) == 0)
            out.append(r.deviceTokens);
    return out;
}

int SessionReportListModel::failedFilters(const Row &r) const
{
    int failed = 0;
    if (!passesDeviceFilter(r.deviceTokens))                                          failed |= DeviceBit;
    if (!m_clubFilter.isEmpty() && !r.clubList.contains(m_clubFilter))                failed |= ClubBit;
    if (!m_reportFilter.isEmpty() && reportBucket(r.state) != m_reportFilter)         failed |= ReportBit;
    if (!m_showArchived && r.archived)                                                failed |= ArchivedBit;
    if (!m_search.isEmpty() && !r.workOn1.contains(m_search, Qt::CaseInsensitive)
        && !r.clubs.contains(m_search, Qt::CaseInsensitive))                          failed |= SearchBit;
    return failed;
}

bool SessionReportListModel::rerank()
{
    const QVector<int> before = m_shownRank;
    m_shownRank.resize(m_rows.size());
    int next = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        m_shownRank[i] = (r.isLive || failedFilters(r) == 0) ? next++ : -1;
    }
    return m_shownRank != before;
}

// In place, never a reset: a delegate under the pointer stays put (applyScan's reason).
void SessionReportListModel::applyDeviceFilter()
{
    rerank();
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(int(m_rows.size()) - 1), { ShownRankRole });
}

void SessionReportListModel::refilter()
{
    applyDeviceFilter();
    emit availableDevicesChanged();
    emit countsChanged();
}

void SessionReportListModel::statesLanded()
{
    // Only the report filter reads the state: the ranks move under it alone, the counts always.
    if (rerank() && !m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), { ShownRankRole });
    if (!m_reportFilter.isEmpty()) emit availableDevicesChanged();
    emit countsChanged();
}

void SessionReportListModel::setClubFilter(const QString &club)
{
    if (m_clubFilter == club) return;
    m_clubFilter = club;
    refilter();
    emit clubFilterChanged();
}

void SessionReportListModel::setReportFilter(const QString &filter)
{
    if (m_reportFilter == filter) return;
    m_reportFilter = filter;
    refilter();
    emit reportFilterChanged();
}

void SessionReportListModel::setShowArchived(bool on)
{
    if (m_showArchived == on) return;
    m_showArchived = on;
    refilter();
    emit showArchivedChanged();
}

void SessionReportListModel::setSearch(const QString &text)
{
    const QString t = text.trimmed();
    if (m_search == t) return;
    m_search = t;
    refilter();
    emit searchChanged();
}

QVariantMap SessionReportListModel::reportCounts() const
{
    int none = 0, stale = 0, ready = 0;
    for (const Row &r : m_rows) {
        if (r.isLive || (failedFilters(r) & ~ReportBit) != 0) continue;
        const QString b = reportBucket(r.state);
        if (b == QLatin1String("none"))       ++none;
        else if (b == QLatin1String("stale")) ++stale;
        else if (b == QLatin1String("ready")) ++ready;
    }
    return { {QStringLiteral("none"), none}, {QStringLiteral("stale"), stale}, {QStringLiteral("ready"), ready} };
}

QVariantList SessionReportListModel::clubCounts() const
{
    QHash<QString, int> n;
    if (!m_clubFilter.isEmpty()) n.insert(m_clubFilter, 0);      // a chosen club can always be switched off
    for (const Row &r : m_rows) {
        if (r.isLive || (failedFilters(r) & ~ClubBit) != 0) continue;
        for (const QString &c : r.clubList) ++n[c];
    }
    QVector<QPair<QString, int>> sorted;
    for (auto it = n.cbegin(); it != n.cend(); ++it) sorted.append({ it.key(), it.value() });
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    QVariantList out;
    for (const auto &p : sorted)
        out.append(QVariantMap{ {QStringLiteral("club"), p.first}, {QStringLiteral("count"), p.second} });
    return out;
}

int SessionReportListModel::archivedCount() const
{
    int n = 0;
    for (const Row &r : m_rows)
        if (!r.isLive && r.archived && (failedFilters(r) & ~ArchivedBit) == 0) ++n;
    return n;
}

int SessionReportListModel::shownSwings() const
{
    int n = 0;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_shownRank.value(i, -1) >= 0) n += m_rows.at(i).swings;
    return n;
}

double SessionReportListModel::shownBytes() const
{
    qint64 n = 0;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_shownRank.value(i, -1) >= 0) n += m_rows.at(i).sessionBytes;
    return double(n);
}

QVariant SessionReportListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case DirRole:          return r.dir;
    case DateLabelRole:    return r.dateLabel;
    case SessionOfDayRole: return r.sessionOfDay;
    case ClubsRole:        return r.clubs;
    case SwingsRole:       return r.swings;
    case DevicesRole:      return r.devices;
    case WorkOn1Role:      return r.workOn1;
    case IsLiveRole:       return r.isLive;
    // Three swings make a report (fewer is the one-page note, not worth sending, §8.0); a session still
    // running would be out of date as soon as it went.
    case CanReportRole:    return r.swings >= 3 && !r.isLive;
    case StateRole:        return r.state;
    case StateWordsRole:   return r.stateWords;
    case PdfPathRole:      return r.pdfPath;
    case SizeBytesRole:    return double(r.sizeBytes);
    case SessionMsRole:    return double(r.startMs);
    case WorkOnKnownRole:  return r.workOnKnown;
    case StateKnownRole:   return !r.state.isEmpty();
    case SentDiffersRole:  return r.sentDiffers;
    case PreparedMsRole:   return double(r.preparedMs);
    case DeviceTokensRole: return r.deviceTokens;
    case ShownRankRole:    return m_shownRank.value(index.row(), -1);
    case SessionBytesRole: return double(r.sessionBytes);
    case ArchivedRole:     return r.archived;
    case StateShortRole:   return r.stateShort;
    default:               return {};
    }
}

QHash<int, QByteArray> SessionReportListModel::roleNames() const
{
    return { {DirRole, "dir"}, {DateLabelRole, "dateLabel"}, {SessionOfDayRole, "sessionOfDay"},
             {ClubsRole, "clubs"}, {SwingsRole, "swings"}, {DevicesRole, "devices"}, {WorkOn1Role, "workOn1"},
             {IsLiveRole, "isLive"}, {CanReportRole, "canReport"}, {StateRole, "state"},
             {StateWordsRole, "stateWords"}, {PdfPathRole, "pdfPath"}, {SizeBytesRole, "sizeBytes"},
             {SessionMsRole, "sessionMs"}, {WorkOnKnownRole, "workOnKnown"}, {StateKnownRole, "stateKnown"},
             {SentDiffersRole, "sentDiffers"}, {PreparedMsRole, "preparedMs"},
             {DeviceTokensRole, "deviceTokens"}, {ShownRankRole, "shownRank"},
             {SessionBytesRole, "sessionBytes"}, {ArchivedRole, "archived"}, {StateShortRole, "stateShort"} };
}

QVariantMap SessionReportListModel::rowFor(const QString &sessionDir) const
{
    const QString abs = QDir::cleanPath(QFileInfo(sessionDir).absoluteFilePath());
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).dir != abs) continue;
        QVariantMap out;
        const QModelIndex ix = index(i);
        const QHash<int, QByteArray> names = roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            out.insert(QString::fromLatin1(it.value()), data(ix, it.key()));
        return out;
    }
    return {};
}

// ── The rows, from the one session list ─────────────────────────────────────────────────────────────

void SessionReportListModel::refresh()
{
    if (m_list && !m_athlete.isNull()) m_list->reload(m_athlete);
}

// THE SUMMARY IS THE DRAWER'S (SessionReviewController): each swing's cheap summary (the presenter's kept read) folded by
// summarizeSession. An un-indexed swing still counts as a swing (the folder lists it), and a session none of whose swings
// say when they were hit is dated by its index entry's identity: its start, else its day.
QVector<SessionReportListModel::Row> SessionReportListModel::rowsFrom(const QVector<pinpoint::SessionListRow> &sessions)
{
    QVector<Row> out;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const pinpoint::SessionListRow &s : sessions) {
        const pinpoint::SessionListFacts &f = s.facts;
        if (!f.read || f.swings == 0) continue;      // not read yet, or nothing was hit: not a session to report on

        QVector<pinpoint::ShotSummaryInput> ins;
        qint64 startMs = 0;
        for (const pinpoint::SessionListSwing &w : f.shots) {
            ins.append({ w.score, w.hasVideo, w.wallclockMs, w.club, w.thumbnailPath, w.devices });
            if (w.wallclockMs > 0 && (startMs == 0 || w.wallclockMs < startMs)) startMs = w.wallclockMs;
        }
        const pinpoint::SessionSummary sum = pinpoint::summarizeSession(ins, nowMs);

        Row r;
        r.dir     = QDir::cleanPath(s.entry.folder);
        r.swings  = f.swings;
        r.startMs = startMs;
        if (startMs > 0) {
            r.sortMs = startMs;
        } else {
            // No swing says when: the session's own start (its session file), else its day (its identity: for a session
            // older than step 2, the day its folder's name records, under every naming pattern, F-06).
            const pinpoint::SessionIdentity &id = s.entry.identity;
            r.sortMs = id.startedAtUtc().isValid() ? id.startedAtUtc().toMSecsSinceEpoch()
                     : id.localDay().isValid()     ? QDateTime(id.localDay(), QTime(0, 0)).toMSecsSinceEpoch()
                                                   : f.modifiedMs;
        }
        r.dateLabel = QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(r.sortMs).date(),
                                            QStringLiteral("ddd d MMM yyyy"));
        // The drawer joins clubs with " · ", which on this line is already the separator between facts.
        r.clubList = sum.clubMix.split(QStringLiteral(" · "), Qt::SkipEmptyParts);
        r.clubs    = r.clubList.join(QStringLiteral(", "));
        r.deviceTokens = sum.devices;
        for (const QString &t : sum.devices) r.devices.append(pinpoint::deviceLabel(t));

        r.workOnKnown = f.workOnKnown;
        r.workOn1Id   = f.workOn1Id;
        r.workOn1     = f.workOn1Name;

        // Every file, an archived session's stubs included: what the folder takes, not what it held.
        r.sessionBytes = f.bytes;
        r.archived     = s.entry.archived;   // the archive marker (FILE-SESS-06), as the index entry reads it
        out.append(r);
    }

    // Newest first. Then the session of the day, counted in start order (the earliest that day is
    // Session 1) — the cover's "session of the day" and this row must agree.
    std::stable_sort(out.begin(), out.end(), [](const Row &a, const Row &b) { return a.sortMs > b.sortMs; });
    QHash<QDate, int> perDay;
    for (int i = int(out.size()) - 1; i >= 0; --i) {
        const QDate d = QDateTime::fromMSecsSinceEpoch(out[i].sortMs).date();
        out[i].sessionOfDay = ++perDay[d];
    }
    return out;
}

void SessionReportListModel::rowsMoved(const pinpoint::AthleteId &athlete)
{
    if (!m_list || athlete != m_athlete) return;
    if (!m_list->loaded(m_athlete)) return;   // its first reload lands with every row
    applyRows(rowsFrom(m_list->rowsOf(m_athlete)), true);
}

void SessionReportListModel::rowMoved(const pinpoint::SessionId &session)
{
    if (!m_list || m_athlete.isNull()) return;
    const std::optional<pinpoint::SessionListRow> r = m_list->row(session);
    if (!r || r->owner != m_athlete) return;
    applyRows(rowsFrom(m_list->rowsOf(m_athlete)), false);
    // That one session's report state is asked again (its swings moved: its fingerprint did).
    refreshState(r->entry.folder);
}

void SessionReportListModel::applyRows(QVector<Row> rows, bool askAll)
{
    // The goal, on this thread: the resolver reads a pack that lives here.
    for (Row &r : rows) {
        if (r.workOn1Id.isEmpty() || !m_goalFn) continue;
        const QString goal = m_goalFn(r.workOn1Id);
        if (!goal.isEmpty()) r.workOn1 = goal;
    }
    // A row whose state was already known keeps it until it is asked again, so new rows do not
    // blink every row's words to nothing and back.
    QHash<QString, Row> previous;
    for (const Row &r : std::as_const(m_rows)) previous.insert(r.dir, r);
    for (Row &r : rows) {
        const auto it = previous.constFind(r.dir);
        if (it == previous.cend()) continue;
        r.state = it->state; r.stateWords = it->stateWords; r.stateShort = it->stateShort; r.pdfPath = it->pdfPath;
        r.sizeBytes = it->sizeBytes; r.preparedMs = it->preparedMs; r.sentDiffers = it->sentDiffers;
    }

    const int before = int(m_rows.size());
    markLive(rows, m_liveDir);
    // The same sessions in the same order (the common case: a work-on record landed, a swing was
    // added to the live session) change rows in place. A reset would rebuild every delegate under the
    // pointer, and a link being pressed would vanish mid-click.
    bool sameDirs = rows.size() == m_rows.size();
    for (int i = 0; sameDirs && i < rows.size(); ++i) sameDirs = rows[i].dir == m_rows[i].dir;
    if (sameDirs) {
        m_rows = std::move(rows);
        rerank();
        if (!m_rows.isEmpty()) emit dataChanged(index(0), index(int(m_rows.size()) - 1));
    } else {
        beginResetModel();
        m_rows = std::move(rows);
        rerank();
        endResetModel();
    }
    if (int(m_rows.size()) != before) emit countChanged();
    emit countsChanged();
    emit availableDevicesChanged();
    emit scanningChanged();
    emit refreshed();

    if (!askAll) {
        // Rows new to the list have no state yet: asked now, the others keep theirs.
        bool any = false;
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows.at(i).state.isEmpty()) { applyState(i); any = true; }
        if (any) statesLanded();
        return;
    }
    m_stateCursor = 0;
    ++m_stateGeneration;
    askStates();
}

// Marks the live row and moves it to the top: it is the session the coach is in.
void SessionReportListModel::markLive(QVector<Row> &rows, const QString &liveDir)
{
    int liveAt = -1;
    for (int i = 0; i < rows.size(); ++i) {
        rows[i].isLive = !liveDir.isEmpty() && rows[i].dir == liveDir;
        if (rows[i].isLive) liveAt = i;
    }
    if (liveAt > 0) rows.move(liveAt, 0);
}

void SessionReportListModel::askStates()
{
    const int gen = m_stateGeneration;
    const int end = m_synchronous ? int(m_rows.size()) : std::min(int(m_rows.size()), m_stateCursor + kStatesPerTurn);
    const int from = m_stateCursor;
    for (; m_stateCursor < end; ++m_stateCursor) applyState(m_stateCursor);
    if (m_stateCursor > from) statesLanded();
    if (m_stateCursor < m_rows.size())
        QTimer::singleShot(0, this, [this, gen] { if (gen == m_stateGeneration) askStates(); });
}

void SessionReportListModel::applyState(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    Row &r = m_rows[row];
    const QVariantMap st = m_stateFn ? m_stateFn(r.dir) : QVariantMap{};
    r.state       = st.value(QStringLiteral("state"), QStringLiteral("none")).toString();
    if (r.state.isEmpty()) r.state = QStringLiteral("none");
    r.stateWords  = st.value(QStringLiteral("words")).toString();
    if (r.stateWords.isEmpty()) r.stateWords = wordsFor(r.state);
    r.stateShort  = shortWordsFor(r.state, st);
    r.pdfPath     = st.value(QStringLiteral("pdfPath")).toString();
    r.sizeBytes   = qint64(st.value(QStringLiteral("sizeBytes")).toDouble());
    r.preparedMs  = qint64(st.value(QStringLiteral("preparedMs")).toDouble());
    r.sentDiffers = st.value(QStringLiteral("sentDiffers")).toBool();
    const QModelIndex ix = index(row);
    emit dataChanged(ix, ix, { StateRole, StateWordsRole, PdfPathRole, SizeBytesRole, StateKnownRole,
                               SentDiffersRole, PreparedMsRole, StateShortRole });
}

void SessionReportListModel::refreshState(const QString &sessionDir)
{
    const QString abs = QDir::cleanPath(QFileInfo(sessionDir).absoluteFilePath());
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).dir == abs) { applyState(i); statesLanded(); return; }
}
