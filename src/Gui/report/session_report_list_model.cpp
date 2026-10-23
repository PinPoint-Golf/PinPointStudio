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
#include "../../Coaching/work_ons.h"
#include "../../Export/swing_doc.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
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

// The record's ordering rule is the only one this list may quote: a record from an older rule is
// about to be re-derived, and its work-on 1 may not be the new rule's.
struct WorkOnLine { bool known = false; QString id, name; };
WorkOnLine readWorkOn1(const QString &sessionDir)
{
    WorkOnLine out;
    QFile f(QDir(sessionDir).filePath(QStringLiteral("work_ons.json")));
    if (!f.open(QIODevice::ReadOnly)) return out;
    bool ok = false;
    const pinpoint::analysis::SessionWorkOns rec =
        pinpoint::analysis::sessionWorkOnsFromJson(QJsonDocument::fromJson(f.readAll()).object(), &ok);
    if (!ok) return out;
    out.known = true;
    for (const pinpoint::analysis::WorkOnEntry &e : rec.entries) {
        if (e.rank == 1 || out.id.isEmpty()) { out.id = e.id; out.name = e.name; }
        if (e.rank == 1) break;
    }
    return out;
}

} // namespace

SessionReportListModel::SessionReportListModel(QObject *parent) : DeviceFilterModel(parent)
{
    m_pool.setMaxThreadCount(1);
}

SessionReportListModel::~SessionReportListModel()
{
    // A scan in flight posts its result to `this`; it must not outlive us.
    ++m_generation;
    m_pool.waitForDone();
}

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

void SessionReportListModel::setAthleteDir(const QString &dir)
{
    if (m_athleteDir == dir) return;
    m_athleteDir = dir;
    // The previous golfer's sessions must not sit under this one's name while the scan runs.
    beginResetModel();
    m_rows.clear();
    rerank();
    endResetModel();
    emit countChanged();
    emit countsChanged();
    emit availableDevicesChanged();
    refresh();
}

void SessionReportListModel::setLiveSessionDir(const QString &dir)
{
    const QString abs = dir.isEmpty() ? QString() : QFileInfo(dir).absoluteFilePath();
    if (m_liveDir == abs) return;
    m_liveDir = abs;
    beginResetModel();
    markLive(m_rows, m_liveDir);
    rerank();
    endResetModel();
    emit countsChanged();
    emit availableDevicesChanged();
    refresh();       // a live session's swing count is moving; an ended one's report can now be made
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

QVector<QStringList> SessionReportListModel::savedDeviceSets() const
{
    QVector<QStringList> out;
    for (const Row &r : m_rows)
        if (!r.isLive)
            out.append(r.deviceTokens);
    return out;
}

void SessionReportListModel::rerank()
{
    m_shownRank.resize(m_rows.size());
    int next = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        m_shownRank[i] = (r.isLive || passesDeviceFilter(r.deviceTokens)) ? next++ : -1;
    }
}

// In place, never a reset: a delegate under the pointer stays put (applyScan's reason).
void SessionReportListModel::applyDeviceFilter()
{
    rerank();
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(int(m_rows.size()) - 1), { ShownRankRole });
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
             {DeviceTokensRole, "deviceTokens"}, {ShownRankRole, "shownRank"} };
}

QVariantMap SessionReportListModel::rowFor(const QString &sessionDir) const
{
    const QString abs = QFileInfo(sessionDir).absoluteFilePath();
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

// ── Scan ────────────────────────────────────────────────────────────────────────────────

void SessionReportListModel::refresh()
{
    if (m_scanning) { m_rescan = true; return; }
    if (m_athleteDir.isEmpty()) return;

    m_scanning = true;
    emit scanningChanged();
    const int generation = ++m_generation;
    const QString dir = m_athleteDir;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (m_synchronous) {
        applyScan(generation, scan(dir, nowMs));
        return;
    }
    m_pool.start([this, generation, dir, nowMs]() {
        QVector<Row> rows = scan(dir, nowMs);
        QMetaObject::invokeMethod(this, [this, generation, rows = std::move(rows)]() mutable {
            applyScan(generation, std::move(rows));
        }, Qt::QueuedConnection);
    });
}

// Worker thread. Touches no member.
//
// THE SUMMARY IS THE DRAWER'S (SessionReviewController::buildDiskRow): each swing's cheap summary read
// with writeSidecar=false — a list read never fat-parses and never writes — folded by summarizeSession.
// An un-indexed swing still counts as a swing (the directory listing knows it), and a session none of
// whose swings say when they were hit is dated from its folder name, else its mtime.
QVector<SessionReportListModel::Row> SessionReportListModel::scan(const QString &athleteDir, qint64 nowMs)
{
    QVector<Row> out;
    const QFileInfoList sessions = QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : sessions) {
        const QString sessionDir = fi.absoluteFilePath();
        const QStringList swingDirs = pinpoint::SwingDocReader::findSwingDirs(sessionDir);
        if (swingDirs.isEmpty()) continue;           // nothing was hit; not a session to report on

        QVector<pinpoint::ShotSummaryInput> ins;
        qint64 startMs = 0;
        for (const QString &sd : swingDirs) {
            const pinpoint::SwingSummary s = pinpoint::SwingDocReader::readSwingSummary(sd, /*writeSidecar=*/false);
            if (!s.ok) continue;
            ins.append({ s.score, s.hasVideo, s.wallclockMs, s.club, s.thumbnailPath, s.devices });
            if (s.wallclockMs > 0 && (startMs == 0 || s.wallclockMs < startMs)) startMs = s.wallclockMs;
        }
        const pinpoint::SessionSummary sum = pinpoint::summarizeSession(ins, nowMs);

        Row r;
        r.dir     = sessionDir;
        r.swings  = int(swingDirs.size());
        r.startMs = startMs;
        if (startMs > 0) {
            r.sortMs = startMs;
        } else {
            const QDate named = QDate::fromString(fi.fileName().left(10), QStringLiteral("yyyy-MM-dd"));
            r.sortMs = named.isValid() ? QDateTime(named, QTime(0, 0)).toMSecsSinceEpoch()
                                       : fi.lastModified().toMSecsSinceEpoch();
        }
        r.dateLabel = QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(r.sortMs).date(),
                                            QStringLiteral("ddd d MMM yyyy"));
        // The drawer joins clubs with " · ", which on this line is already the separator between facts.
        r.clubs = sum.clubMix;
        r.clubs.replace(QStringLiteral(" · "), QStringLiteral(", "));
        r.deviceTokens = sum.devices;
        for (const QString &t : sum.devices) r.devices.append(pinpoint::deviceLabel(t));

        const WorkOnLine w = readWorkOn1(sessionDir);
        r.workOnKnown = w.known;
        r.workOn1Id   = w.id;
        r.workOn1     = w.name;
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

void SessionReportListModel::applyScan(int generation, QVector<Row> rows)
{
    m_scanning = false;
    if (generation != m_generation) { emit scanningChanged(); return; }   // superseded; the newer one lands
    if (m_rescan) {                                                         // asked again while scanning
        m_rescan = false;
        emit scanningChanged();
        refresh();
        return;
    }

    // The goal, on this thread: the resolver reads a pack that lives here.
    for (Row &r : rows) {
        if (r.workOn1Id.isEmpty() || !m_goalFn) continue;
        const QString goal = m_goalFn(r.workOn1Id);
        if (!goal.isEmpty()) r.workOn1 = goal;
    }
    // A row whose state was already known keeps it until it is asked again, so a re-scan does not
    // blink every row's words to nothing and back.
    QHash<QString, Row> previous;
    for (const Row &r : std::as_const(m_rows)) previous.insert(r.dir, r);
    for (Row &r : rows) {
        const auto it = previous.constFind(r.dir);
        if (it == previous.cend()) continue;
        r.state = it->state; r.stateWords = it->stateWords; r.pdfPath = it->pdfPath;
        r.sizeBytes = it->sizeBytes; r.preparedMs = it->preparedMs; r.sentDiffers = it->sentDiffers;
    }

    const int before = int(m_rows.size());
    markLive(rows, m_liveDir);
    // The same sessions in the same order (the common re-scan: a work-on record landed, a swing was
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
    for (; m_stateCursor < end; ++m_stateCursor) applyState(m_stateCursor);
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
    r.pdfPath     = st.value(QStringLiteral("pdfPath")).toString();
    r.sizeBytes   = qint64(st.value(QStringLiteral("sizeBytes")).toDouble());
    r.preparedMs  = qint64(st.value(QStringLiteral("preparedMs")).toDouble());
    r.sentDiffers = st.value(QStringLiteral("sentDiffers")).toBool();
    const QModelIndex ix = index(row);
    emit dataChanged(ix, ix, { StateRole, StateWordsRole, PdfPathRole, SizeBytesRole, StateKnownRole,
                               SentDiffersRole, PreparedMsRole });
}

void SessionReportListModel::refreshState(const QString &sessionDir)
{
    const QString abs = QFileInfo(sessionDir).absoluteFilePath();
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).dir == abs) { applyState(i); return; }
}
