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

#include "session_review_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "../../Export/session_archiver.h"
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentMap>

#include <optional>

#include "app_settings.h"
#include "athlete_controller.h"
#include "session_summary.h"
#include "../session/session_list_presenter.h"   // the one session list (refactor step 5.7a)
#include "../Core/pp_debug.h"
#include "../Export/swing_doc.h"
#include "../DomainModel/athlete.h"
#include "../DomainModel/session.h"    // the Session its Athlete holds (the drawer's delete, refactor step 3.4b-ii)
#include "../DomainModel/recording_repository.h"
#include "../DomainModel/session_repository.h"

namespace {

// thumbnail absolute paths → file:// URL strings for QML Image sources.
QStringList toThumbUrls(const QStringList &paths)
{
    QStringList urls;
    urls.reserve(paths.size());
    for (const QString &p : paths)
        urls.append(p.isEmpty() ? QString() : QUrl::fromLocalFile(p).toString());
    return urls;
}

} // namespace

SessionReviewController::SessionReviewController(ShotListModel     *liveModel,
                                                 AppSettings       *settings,
                                                 AthleteController *athlete,
                                                 QObject           *parent)
    : QObject(parent)
    , m_liveModel(liveModel)
    , m_settings(settings)
    , m_athlete(athlete)
{
    // The loaded session's shot count tracks trashing in the review model.
    connect(&m_reviewModel, &ShotListModel::activeCountChanged,
            this, &SessionReviewController::activeShotCountChanged);
    // liveSessionDir follows the live view: rows arriving, leaving, the view shown another session (a reset), and a
    // card becoming its Shot's row (a late device-only document: dataChanged).
    if (m_liveModel) {
        connect(m_liveModel, &QAbstractItemModel::rowsInserted, this, &SessionReviewController::updateLiveSessionDir);
        connect(m_liveModel, &QAbstractItemModel::rowsRemoved,  this, &SessionReviewController::updateLiveSessionDir);
        connect(m_liveModel, &QAbstractItemModel::modelReset,   this, &SessionReviewController::updateLiveSessionDir);
        connect(m_liveModel, &QAbstractItemModel::dataChanged,  this, &SessionReviewController::updateLiveSessionDir);
    }
    // The header's clubs follow the review's rows (a club changed in review, a shot trashed).
    for (const auto sig : { &QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved })
        connect(&m_reviewModel, sig, this, &SessionReviewController::activeClubMixChanged);
    connect(&m_reviewModel, &QAbstractItemModel::modelReset, this, &SessionReviewController::activeClubMixChanged);
    connect(&m_reviewModel, &QAbstractItemModel::dataChanged, this, &SessionReviewController::activeClubMixChanged);
    m_liveSessionDir = liveSessionDir();
    // The board follows the shots on screen (refactor 6.1b): the loaded session's while reviewing, the live list's else.
    connect(this, &SessionReviewController::reviewActiveChanged, this, &SessionReviewController::followLmBoard);
    refresh();
}

LmSessionModel *SessionReviewController::lmBoard()
{
    if (!m_lmBoard)
        m_lmBoard = new LmSessionModel(this);
    return m_lmBoard;
}

void SessionReviewController::holdLmBoard(QObject *view)
{
    if (!view || m_lmBoardViews.contains(view))
        return;
    m_lmBoardViews.insert(view);
    // A view lets go when it is destroyed (the panel's teardown, a Loader switching it out), whether or not it said so.
    connect(view, &QObject::destroyed, this, [this, view] {
        if (m_lmBoardViews.remove(view))
            followLmBoard();
    });
    followLmBoard();
}

void SessionReviewController::releaseLmBoard(QObject *view)
{
    if (!m_lmBoardViews.remove(view))
        return;
    disconnect(view, &QObject::destroyed, this, nullptr);
    followLmBoard();
}

void SessionReviewController::followLmBoard()
{
    if (m_lmBoardViews.isEmpty()) {
        if (m_lmBoard)
            m_lmBoard->setShotModel(nullptr);   // boards nothing, and forgets its live grades (setShotModel clears them)
        return;
    }
    // The list first, then the register: one reduction for each, as the panel's two bindings were.
    LmSessionModel *board = lmBoard();
    board->setShotModel(activeShots());
    board->setReviewing(m_reviewActive);
}

SessionReviewController::~SessionReviewController()
{
    // The board first, while the lists it boards are still here; and no view's teardown reaches back in after.
    for (QObject *view : std::as_const(m_lmBoardViews))
        disconnect(view, &QObject::destroyed, this, nullptr);
    m_lmBoardViews.clear();
    delete m_lmBoard;
    m_lmBoard = nullptr;
}

void SessionReviewController::updateLiveSessionDir()
{
    const QString now = liveSessionDir();
    if (now == m_liveSessionDir) return;
    m_liveSessionDir = now;
    emit liveSessionDirChanged();
}

QString SessionReviewController::liveSessionDir() const
{
    // The session the live view shows, while it shows a saved shot of it (as the folder of its first saved row was).
    if (!m_liveModel || !m_liveModel->hasSavedShots())
        return {};
    const QString folder = m_liveModel->sessionFolder();
    return folder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
}

QString SessionReviewController::activeClubMix() const
{
    if (!m_reviewActive)
        return {};
    // The view's rows, oldest first (the order the session was hit, as the load's documents were read).
    QVector<pinpoint::ShotSummaryInput> ins;
    for (int r = m_reviewModel.rowCount() - 1; r >= 0; --r) {
        pinpoint::ShotSummaryInput in;
        in.club = m_reviewModel.data(m_reviewModel.index(r), ShotListModel::ClubRole).toString();
        ins.append(in);
    }
    return ins.isEmpty() ? QString() : pinpoint::summarizeSession(ins, QDateTime::currentMSecsSinceEpoch()).clubMix;
}

SessionListModel::Row SessionReviewController::buildLiveRow(qint64 nowMs) const
{
    QVector<pinpoint::ShotSummaryInput> ins;
    if (m_liveModel) {
        const QDate today = QDateTime::fromMSecsSinceEpoch(nowMs).date();
        const int n = m_liveModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QModelIndex idx = m_liveModel->index(r);
            pinpoint::ShotSummaryInput in;
            in.score    = m_liveModel->data(idx, ShotListModel::ScoreRole).toInt();
            in.hasVideo = m_liveModel->data(idx, ShotListModel::HasVideoRole).toBool();
            in.club     = m_liveModel->data(idx, ShotListModel::ClubRole).toString();
            const QUrl thumb = m_liveModel->data(idx, ShotListModel::ThumbnailSourceRole).toUrl();
            in.thumbnailPath = thumb.isLocalFile() ? thumb.toLocalFile() : thumb.toString();
            // Live shots carry only hh:mm:ss — combine with today for an instant.
            const QTime t = QTime::fromString(
                m_liveModel->data(idx, ShotListModel::TimestampLabelRole).toString(),
                QStringLiteral("hh:mm:ss"));
            if (t.isValid())
                in.wallclockMs = QDateTime(today, t).toMSecsSinceEpoch();
            ins.append(in);
        }
    }
    const pinpoint::SessionSummary sum = pinpoint::summarizeSession(ins, nowMs);
    // What recorded it: the live model does not hold this. The live session's row of the one session list does (its entry's
    // swing roll-up, read again on each shot's event: refactor 5.7a, 5.7a-P1), so it is not read again here for every swing.
    QStringList devices;
    if (const QString live = liveSessionDir(); m_list && !live.isEmpty())
        if (const std::optional<pinpoint::SessionListRow> r = m_list->rowAt(live))
            devices = r->entry.summary.swings.devices;

    SessionListModel::Row row;
    row.sessionId     = QString();           // sentinel — selecting the live row resumes live
    row.isLive        = true;
    row.dayLabel      = QStringLiteral("Today");
    row.timeLabel     = sum.timeLabel;
    row.clubMix       = sum.clubMix;
    row.shotCount     = sum.shotCount;
    row.lengthLabel   = sum.lengthLabel;
    row.avgQuality    = sum.avgQuality;
    row.previewThumbs = toThumbUrls(sum.previewThumbs);
    row.devices       = devices;
    return row;
}

SessionListModel::Row SessionReviewController::buildDiskRow(const pinpoint::SessionListRow &session,
                                                            qint64 nowMs) const
{
    // The entry's swing roll-up (P1: the swings' summary sidecars ONLY, never the fat swing.json parse, which runs to hundreds
    // of MB behind a session and used to freeze the app long enough for the compositor to offer to kill it).
    const pinpoint::SessionListFacts &f = session.facts;
    const pinpoint::SessionSwingRollUp &sw = session.entry.summary.swings;
    const bool indexed = session.entry.summary.read && sw.indexed();
    const pinpoint::SessionSummary sum = pinpoint::summaryOf(sw, nowMs);

    SessionListModel::Row row;
    row.sessionId     = QDir::cleanPath(session.entry.folder);
    row.devices       = sum.devices;    // of the indexed shots — a partly indexed session may list fewer
    row.isLive        = false;
    row.indexed       = indexed;
    row.dayLabel      = sum.dayLabel;
    row.timeLabel     = sum.timeLabel;
    row.clubMix       = sum.clubMix;
    row.shotCount     = sum.shotCount;
    row.lengthLabel   = sum.lengthLabel;
    row.avgQuality    = sum.avgQuality;
    row.previewThumbs = toThumbUrls(sum.previewThumbs);

    if (!indexed) {
        // Nothing (or not everything) indexed yet. Fall back to the facts the folder listing and the index entry supply,
        // so the row stays recognisable and clickable; opening it indexes the session and its row is read again.
        row.shotCount = sw.swings;
        if (row.dayLabel.isEmpty()) {
            // The session's own start time, else its day: its index entry's identity (step 2.7b, F-06) — the session
            // file's start, or for a session older than step 2 the day its folder's name records, under every naming
            // pattern. The folder's mtime is its LAST write (the final swing), while an indexed row's dayLabel comes from
            // the EARLIEST shot, so it is used only when neither is known.
            const pinpoint::SessionIdentity &id = session.entry.identity;
            const qint64 anchorMs =
                id.startedAtUtc().isValid() ? id.startedAtUtc().toMSecsSinceEpoch()
                : id.localDay().isValid()   ? QDateTime(id.localDay(), QTime(0, 0)).toMSecsSinceEpoch()
                                            : f.modifiedMs;
            row.dayLabel = pinpoint::relativeDayLabel(anchorMs, nowMs);
            // Leave timeLabel blank: the drawer renders "day · time" only when time is
            // non-empty, so an unknown fact reads honestly instead of showing a dir mtime
            // that is not the session's start.
        }
    }
    return row;
}

void SessionReviewController::setSessionList(pinpoint::SessionListPresenter *list)
{
    if (m_list == list) return;
    if (m_list) disconnect(m_list, nullptr, this, nullptr);
    m_list = list;
    if (m_list) {
        connect(m_list, &pinpoint::SessionListPresenter::rowsChanged, this, &SessionReviewController::refreshSoon);
        connect(m_list, &pinpoint::SessionListPresenter::rowChanged, this, &SessionReviewController::refreshSoon);
    }
    refresh();
}

void SessionReviewController::refreshSoon()
{
    // On the next turn of the event loop, once: the rebuild resets the sessions model, which destroys the drawer's
    // delegates, and a change can arrive from one of their click handlers (loadSession, trashSession).
    if (m_refreshArmed) return;
    m_refreshArmed = true;
    QTimer::singleShot(0, this, [this] { m_refreshArmed = false; refresh(); });
}

void SessionReviewController::refresh()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    QVector<SessionListModel::Row> rows;
    rows.append(buildLiveRow(nowMs));        // live session pinned first

    const QString liveDir = liveSessionDir();
    // The current athlete's sessions, as the one session list holds them (its SessionIndex; refactor 5.7a), in the
    // drawer's order: the folder last written first (the naming pattern means a plain name sort does not track recency).
    pinpoint::Athlete *athlete = m_athlete ? m_athlete->currentAthlete() : nullptr;
    QVector<pinpoint::SessionListRow> sessions =
        (m_list && athlete) ? m_list->rowsOf(athlete->id()) : QVector<pinpoint::SessionListRow>{};
    std::stable_sort(sessions.begin(), sessions.end(), [](const pinpoint::SessionListRow &a, const pinpoint::SessionListRow &b) {
        return a.facts.modifiedMs > b.facts.modifiedMs;
    });
    for (const pinpoint::SessionListRow &session : std::as_const(sessions)) {
        // Skip the live session's own folder — it's already the synthesized row.
        if (!liveDir.isEmpty() && QDir::cleanPath(session.entry.folder) == liveDir)
            continue;
        rows.append(buildDiskRow(session, nowMs));
    }
    m_sessionsModel.setRows(rows);
}

void SessionReviewController::loadSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) {            // the live row's sentinel id
        resumeLive();
        return;
    }

    if (m_reasonFilterArmed) m_reasonFilterArmed = false;   // this is the load it was set for
    else                     clearReasonFilter();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString dir = QFileInfo(sessionId).absoluteFilePath();

    // THE SESSION'S INDEX, made in parallel before its rows are read (#173; refactor step 5.2). The review's carousel is
    // a view of the Session (R-QML-5): each row reads its Shot's summary, which for a JSON-era swing is its summary
    // sidecar. So, one swing per pool thread: the summary read alone (never a write); a swing with none is parsed whole
    // and indexed through the swing door (RecordingRepository::writeSummarySidecar), as this load always did — the
    // sidecar also keeps the Sessions list cheap from now on. One after another on the GUI thread the whole parse was
    // ~1.5 s of a 3.2 s freeze on opening a 32-shot session. The wallclocks give the header's day and time.
    const QStringList swingDirs = pinpoint::SwingDocReader::findSwingDirs(sessionId);
    pinpoint::RecordingRepository *repo = m_repo;
    // The edge (DEC-90): each swing folder to its ShotRef HERE, on the GUI thread; the pool threads write by the ShotRef.
    QList<QPair<QString, pinpoint::ShotRef>> swings;
    for (const QString &sd : swingDirs)
        swings.append({ sd, repo ? repo->shotOfFolder(sd) : pinpoint::ShotRef() });
    const QList<std::optional<pinpoint::ShotSummaryInput>> read = QtConcurrent::blockingMapped(
        swings, [repo](const QPair<QString, pinpoint::ShotRef> &sw) -> std::optional<pinpoint::ShotSummaryInput> {
            const pinpoint::SwingSummary ss = pinpoint::SwingDocReader::readSwingSummary(sw.first, /*writeSidecar=*/false);
            if (ss.ok)
                return pinpoint::ShotSummaryInput{ ss.score, ss.hasVideo, ss.wallclockMs, ss.club, ss.thumbnailPath, ss.devices };
            const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(sw.first);
            if (!ps.ok)
                return std::nullopt;
            if (repo)
                repo->writeSummarySidecar(sw.second, ps);
            return pinpoint::ShotSummaryInput{ ps.score, ps.hasVideo, ps.wallclockMs, ps.club, ps.thumbnailPath, ps.devices };
        });
    QVector<pinpoint::ShotSummaryInput> ins;
    for (const std::optional<pinpoint::ShotSummaryInput> &in : read)
        if (in)
            ins.append(*in);

    // The same session again (an archived one restored under the review): its rows read their Shots again, in place.
    if (!m_loadedSessionDir.isEmpty() && QDir::cleanPath(m_loadedSessionDir) == QDir::cleanPath(dir)
        && QDir::cleanPath(QFileInfo(m_reviewModel.sessionFolder()).absoluteFilePath()) == QDir::cleanPath(dir))
        m_reviewModel.rereadShots();
    else
        m_reviewModel.showSession(dir);

    const pinpoint::SessionSummary sum = pinpoint::summarizeSession(ins, nowMs);
    m_activeDayLabel   = sum.dayLabel;
    m_activeTimeLabel  = sum.timeLabel;
    m_loadedSessionDir = dir;

    m_reviewActive = true;
    emit reviewActiveChanged();          // also re-evaluates the activeDay/Time bindings
    emit activeShotCountChanged();
    emit activeClubMixChanged();

    // The loop above just indexed this session, so its row can now show full detail instead of the shot count alone:
    // the one session list reads it again (its sidecars only), and the drawer follows on the next turn (refreshSoon:
    // loadSession() is called FROM one of the drawer's delegates' click handlers, PpSessionDrawer.qml).
    const std::optional<pinpoint::SessionListRow> listed = m_list ? m_list->rowAt(m_loadedSessionDir) : std::nullopt;
    if (listed)
        m_list->reread(listed->entry.id());
    refreshSoon();

    // Archived: the index entry's mark (FILE-SESS-06); a folder the list does not hold, the marker itself.
    if (listed ? listed->entry.archived : pinpoint::SessionArchiver::isArchived(m_loadedSessionDir))
        emit archivedSessionOpened(m_loadedSessionDir);
}

void SessionReviewController::onAthleteChanged()
{
    if (m_reviewActive)
        resumeLive();                    // the loaded session belongs to the previous athlete
    refresh();                           // sidecar-only, so this is safe to run inline
}

void SessionReviewController::setReasonFilter(const QString &conditionId)
{
    m_reasonFilterArmed = !conditionId.isEmpty();
    if (m_reasonFilter == conditionId) return;
    m_reasonFilter = conditionId;
    emit reasonFilterChanged();
}

void SessionReviewController::clearReasonFilter()
{
    m_reasonFilterArmed = false;
    if (m_reasonFilter.isEmpty()) return;
    m_reasonFilter.clear();
    emit reasonFilterChanged();
}

void SessionReviewController::resumeLive()
{
    clearReasonFilter();
    if (!m_reviewActive)
        return;
    m_reviewActive = false;
    m_activeDayLabel.clear();
    m_activeTimeLabel.clear();
    m_loadedSessionDir.clear();
    m_reviewModel.showSession(QString());
    emit reviewActiveChanged();
    emit activeShotCountChanged();
    emit activeClubMixChanged();
}

QVariantList SessionReviewController::swingDirsForSession(const QString &sessionId) const
{
    QVariantList dirs;
    if (sessionId.isEmpty())             // the live row has no on-disk session
        return dirs;
    for (const QString &sd : pinpoint::SwingDocReader::findSwingDirs(sessionId))
        dirs.append(sd);
    return dirs;
}

bool SessionReviewController::trashSession(const QString &sessionId)
{
    if (sessionId.isEmpty())             // the live row has no on-disk session
        return false;
    // The live session's own folder, named by its path: its shots are on the carousel and a session
    // may still be writing into it. Refused here, not only hidden in the views.
    const QString live = liveSessionDir();
    if (!live.isEmpty() && QDir::cleanPath(QFileInfo(sessionId).absoluteFilePath()) == QDir::cleanPath(live)) {
        ppWarn() << "[SessionReviewController] refused: the live session cannot be trashed:" << sessionId;
        return false;
    }
    // THROUGH THE SESSION REPOSITORY, BY SessionId (refactor step 2.6; brief §4.4): the row's folder is
    // translated once, here at the edge (DEC-90) — its session file's id, or, for a session older than
    // step 2, D3's from the athlete whose sessions the drawer lists (the id the athlete's index holds it
    // under, J-39). The repository refuses a session its session file says is in progress (here, on the
    // other computer, or left open by a crash) and one another writer holds locked; it moves the whole
    // folder to the OS trash, else the library's own (a share has none), recoverable either way. On a
    // refusal or a failure everything stays in place.
    pinpoint::SessionRepository *sessions = m_athlete ? m_athlete->sessionRepository() : nullptr;
    pinpoint::Athlete           *athlete  = m_athlete ? m_athlete->currentAthlete() : nullptr;
    const QString                folder   = QDir::cleanPath(QFileInfo(sessionId).absoluteFilePath());
    // A SESSION ITS ATHLETE HOLDS IS TRASHED BY ITS ATHLETE (refactor step 3.4b-ii; DEC-71: Athlete::trashSession,
    // EVT-20): the same refusals and the same move, through the same repository, and the Athlete lets its Session
    // (and its Shots) go. A session no Athlete holds — another golfer's folder — keeps the path below (N1).
    if (pinpoint::Session *held = athlete ? athlete->sessionAt(folder) : nullptr) {
        QString where;
        const pinpoint::WriteResult t = athlete->trashSession(held->id(), &where);
        if (!t.ok()) {
            ppWarn() << "[SessionReviewController] could not move session to trash:" << sessionId << "-" << t.error;
            return false;
        }
        if (m_reviewActive && QFileInfo(sessionId).absoluteFilePath() == m_loadedSessionDir)
            resumeLive();
        return true;   // its row goes with EVT-20 (the one session list follows it)
    }
    if (!sessions) {
        ppWarn() << "[SessionReviewController] no session repository: cannot trash" << sessionId;
        return false;
    }
    const pinpoint::SessionId id = sessions->openFolder(folder, athlete ? athlete->id() : pinpoint::AthleteId());
    const pinpoint::WriteResult r = id.isNull()
        ? pinpoint::WriteResult{ pinpoint::WriteResult::Status::Failed,
                                 QStringLiteral("the session cannot be named (no session file, and no athlete)") }
        : sessions->trash(id);
    if (!r.ok()) {
        ppWarn() << "[SessionReviewController] could not move session to trash:" << sessionId << "-" << r.error;
        return false;
    }
    // If we just trashed the session being reviewed, drop back to live so the
    // carousel isn't left pointing at files that are no longer there.
    if (m_reviewActive && QFileInfo(sessionId).absoluteFilePath() == m_loadedSessionDir)
        resumeLive();
    // No Athlete held it, so no event: the list reads the index again and the trashed row drops out of every list.
    if (m_list && athlete)
        m_list->entriesChanged(athlete->id());
    refresh();
    return true;
}
