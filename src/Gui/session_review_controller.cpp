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
#include <QFileInfo>
#include <QTime>
#include <QUrl>

#include "app_settings.h"
#include "athlete_controller.h"
#include "session_summary.h"
#include "../Export/swing_doc.h"

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
    // The loaded session's shot count tracks trash/restore in the review model.
    connect(&m_reviewModel, &ShotListModel::activeCountChanged,
            this, &SessionReviewController::activeShotCountChanged);
    refresh();
}

QString SessionReviewController::liveSessionDir() const
{
    if (!m_liveModel)
        return {};
    const int n = m_liveModel->rowCount();
    for (int r = 0; r < n; ++r) {
        const QString sd = m_liveModel->data(m_liveModel->index(r),
                                             ShotListModel::SwingDirRole).toString();
        if (!sd.isEmpty())
            return QFileInfo(sd).absolutePath();
    }
    return {};
}

SessionListModel::Row SessionReviewController::buildLiveRow(qint64 nowMs) const
{
    QVector<pinpoint::ShotSummaryInput> ins;
    if (m_liveModel) {
        const QDate today = QDateTime::fromMSecsSinceEpoch(nowMs).date();
        const int n = m_liveModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QModelIndex idx = m_liveModel->index(r);
            if (m_liveModel->data(idx, ShotListModel::TrashedRole).toBool())
                continue;
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
    return row;
}

SessionListModel::Row SessionReviewController::buildDiskRow(const QString &sessionDir,
                                                            qint64 nowMs) const
{
    QVector<pinpoint::ShotSummaryInput> ins;
    for (const QString &sd : pinpoint::SwingDocReader::findSwingDirs(sessionDir)) {
        const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(sd);
        if (!ps.ok)
            continue;
        ins.append({ ps.score, ps.hasVideo, ps.wallclockMs, ps.club, ps.thumbnailPath });
    }
    const pinpoint::SessionSummary sum = pinpoint::summarizeSession(ins, nowMs);

    SessionListModel::Row row;
    row.sessionId     = QFileInfo(sessionDir).absoluteFilePath();
    row.isLive        = false;
    row.dayLabel      = sum.dayLabel;
    row.timeLabel     = sum.timeLabel;
    row.clubMix       = sum.clubMix;
    row.shotCount     = sum.shotCount;
    row.lengthLabel   = sum.lengthLabel;
    row.avgQuality    = sum.avgQuality;
    row.previewThumbs = toThumbUrls(sum.previewThumbs);
    return row;
}

void SessionReviewController::refresh()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    QVector<SessionListModel::Row> rows;
    rows.append(buildLiveRow(nowMs));        // live session pinned first

    const QString liveDir = liveSessionDir();
    const QString root    = m_settings ? m_settings->athleteLibraryPath() : QString();
    const QString athlete = m_athlete  ? m_athlete->currentName()         : QString();
    for (const QString &sessionDir : pinpoint::SwingDocReader::sessionDirs(root, athlete)) {
        // Skip the live session's own dir — it's already the synthesized row.
        if (!liveDir.isEmpty() && QFileInfo(sessionDir).absoluteFilePath() == liveDir)
            continue;
        rows.append(buildDiskRow(sessionDir, nowMs));
    }
    m_sessionsModel.setRows(rows);
}

void SessionReviewController::loadSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) {            // the live row's sentinel id
        resumeLive();
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_reviewModel.clear();

    QVector<pinpoint::ShotSummaryInput> ins;
    for (const QString &sd : pinpoint::SwingDocReader::findSwingDirs(sessionId)) {
        const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(sd);
        if (!ps.ok)
            continue;
        m_reviewModel.addPersistedShot(
            ps.swingDir, ps.ordinal, ps.timestampLabel, ps.club, ps.hasVideo,
            ps.thumbnailPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(ps.thumbnailPath),
            ps.score, ps.rating, ps.note, ps.metrics, ps.analysisDetail);
        ins.append({ ps.score, ps.hasVideo, ps.wallclockMs, ps.club, ps.thumbnailPath });
    }

    const pinpoint::SessionSummary sum = pinpoint::summarizeSession(ins, nowMs);
    m_activeDayLabel  = sum.dayLabel;
    m_activeTimeLabel = sum.timeLabel;
    m_activeClubMix   = sum.clubMix;

    m_reviewActive = true;
    emit reviewActiveChanged();          // also re-evaluates the activeDay/Time/ClubMix bindings
    emit activeShotCountChanged();
}

void SessionReviewController::resumeLive()
{
    if (!m_reviewActive)
        return;
    m_reviewActive = false;
    m_activeDayLabel.clear();
    m_activeTimeLabel.clear();
    m_activeClubMix.clear();
    m_reviewModel.clear();
    emit reviewActiveChanged();
    emit activeShotCountChanged();
}
