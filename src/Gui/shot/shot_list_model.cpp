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

#include "shot_list_model.h"

#include "../Export/swing_doc.h"
#include "../DomainModel/recording_repository.h"
#include "../../App/shot_command_door.h"
#include "../Core/club_vocabulary.h"
#include "../Core/pp_debug.h"
#include "../../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S)

#include <QFile>
#include <QFileInfo>
#include "../../Export/swing_paths.h"
#include <QStringList>

#include <algorithm>
#include <utility>

ShotListModel::ShotListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_trashTimer.setSingleShot(true);
    connect(&m_trashTimer, &QTimer::timeout, this, &ShotListModel::attemptPendingTrash);
}

int ShotListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shots.size();
}

QVariant ShotListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shots.size())
        return {};
    const Shot &s = m_shots.at(index.row());
    switch (role) {
    case ShotIdRole:          return s.id;
    case OrdinalRole:         return s.ordinal;
    case TimestampLabelRole:  return s.timestampLabel;
    case ClubRole:            return s.club;
    case HasVideoRole:        return s.hasVideo;
    case ThumbnailSourceRole: return s.thumbnailSource;
    case TracePointsRole:     return s.tracePoints;
    case ScoreRole:           return s.score;
    case RatingRole:          return s.rating;
    case NoteRole:            return s.note;
    case MetricsRole:         return s.metrics;
    // QML only. By reference — qml_payload.h. C++ reads analysisDetailMap(): this converts the
    // whole detail into the JS heap (#173).
    case AnalysisDetailRole:  return QVariant::fromValue(s.analysisDetail.js());
    case SwingDirRole:        return s.swingDir;
    case DataWarningRole:     return s.dataWarning;
    case DataWarningDetailRole: return s.dataWarningDetail;
    case LmDeviceRole:    return s.lmDevice;
    case LmExcludedRole:      return s.lmExcluded;
    case LmClubOverrideRole:  return s.lmClubOverride;
    default:                  return {};
    }
}

QHash<int, QByteArray> ShotListModel::roleNames() const
{
    return {
        { ShotIdRole,          "shotId"          },
        { OrdinalRole,         "ordinal"         },
        { TimestampLabelRole,  "timestampLabel"  },
        { ClubRole,            "club"            },
        { HasVideoRole,        "hasVideo"        },
        { ThumbnailSourceRole, "thumbnailSource" },
        { TracePointsRole,     "tracePoints"     },
        { ScoreRole,           "score"           },
        { RatingRole,          "rating"          },
        { NoteRole,            "note"            },
        { MetricsRole,         "metrics"         },
        { AnalysisDetailRole,  "analysisDetail"  },
        { SwingDirRole,        "swingDir"        },
        { DataWarningRole,     "dataWarning"     },
        { LmDeviceRole,    "lmDevice"        },
        { DataWarningDetailRole, "dataWarningDetail" },
        { LmExcludedRole,      "lmExcluded"      },
        { LmClubOverrideRole,  "lmClubOverride"  },
    };
}

int ShotListModel::activeCount() const
{
    return m_shots.size();
}

QVariantMap ShotListModel::analysisDetailMap(int row) const
{
    if (row < 0 || row >= m_shots.size())
        return {};
    return m_shots.at(row).analysisDetail.map();
}

int ShotListModel::rowForId(int id) const
{
    for (int i = 0; i < m_shots.size(); ++i)
        if (m_shots.at(i).id == id)
            return i;
    return -1;
}

int ShotListModel::ordinalForId(int id) const
{
    const int row = rowForId(id);
    return row < 0 ? 0 : m_shots[row].ordinal;
}

int ShotListModel::addShot(const QString &swingDir, const QString &timestampLabel,
                           const QString &club, bool hasVideo, const QUrl &thumbnailSource,
                           const QVariantList &tracePoints, int score,
                           const QVariantMap &metrics,
                           const QVariantMap &analysisDetail, bool dataWarning,
                           const QVariantMap &dataWarningDetail)
{
    // ⚠ THE NUMBER COMES FROM THE DOCUMENT, NOT FROM COUNTING THE ROWS.
    //
    // Two things number a swing: SessionRepository::allocateSwing, which reads
    // the swing_* folders and the high-water and probes upward, and this, which used to take
    // max(ordinal) + 1 over the rows. They agree until a swing is deleted from
    // the middle of a session, and then they do not: a carousel reloaded from a
    // folder holding swing_0001 and swing_0005 called the next shot "Shot 6"
    // while its swing.json recorded index 3 — so the same swing was Shot 6 this
    // afternoon and Shot 3 after a restart, and "Shot 6" in a toast named a row
    // nobody could find tomorrow. The document is the one that survives, so the
    // document wins. The summary read is the cheap path (no analysisDetail, no
    // pose track) and the file was written moments ago by the join.
    //
    // A SHOT A SESSION HOLDS (refactor step 3.4b-ii): the number, and the review facts, are its Shot's — its summary,
    // read by the Shot through its repository (the same cheap read). Otherwise (N1) the carousel reads it, as today.
    int ordinal = 0;
    pinpoint::ShotRef     ref;
    pinpoint::ShotSummary facts;
    const bool held = !swingDir.isEmpty() && shotFacts(swingDir, ref, pinpoint::SummaryRead::AgainCheap, facts);
    if (held) {
        if (facts.ok)
            ordinal = facts.ordinal;
    } else if (!swingDir.isEmpty()) {
        const pinpoint::SwingSummary sum =
            pinpoint::SwingDocReader::readSwingSummary(swingDir, /*writeSidecar=*/false);
        if (sum.ok)
            ordinal = sum.ordinal;
    }
    // A shot with no document — export and analysis both produced nothing — has
    // no number of its own, so it takes the next one this model has not issued.
    // ⚠ NOT max(ordinal) + 1 OVER THE ROWS: trash the newest shot and hit
    // another, and that recomputed the number that just left, so two shots in
    // one session were both announced as "Shot 4".
    if (ordinal <= 0)
        ordinal = m_nextOrdinal;
    m_nextOrdinal = std::max(m_nextOrdinal, ordinal + 1);

    Shot shot;
    shot.id              = m_nextId++;
    shot.ordinal         = ordinal;
    shot.swingDir        = swingDir;
    shot.timestampLabel  = timestampLabel;
    shot.club            = club;
    shot.hasVideo        = hasVideo;
    shot.thumbnailSource = thumbnailSource;
    shot.tracePoints     = tracePoints;
    shot.score           = score;
    shot.metrics         = metrics;
    shot.analysisDetail  = analysisDetail;
    shot.dataWarning     = dataWarning;
    shot.dataWarningDetail = dataWarningDetail;
    shot.shot            = ref;
    shot.held            = held;
    // The analysis just run stays the row's until its first refresh (it is what the Shot's document now holds);
    // the facts the Shot's commands own are the Shot's.
    if (held && facts.ok)
        showShotReview(shot, facts);

    // Newest first, matching the mockup's right-of-cap ordering.
    beginInsertRows(QModelIndex(), 0, 0);
    m_shots.prepend(shot);
    endInsertRows();
    emit activeCountChanged();
    return shot.id;
}

void ShotListModel::addPersistedShot(const QString &swingDir, int ordinal,
                                     const QString &timestampLabel, const QString &club,
                                     bool hasVideo, const QUrl &thumbnailSource, int score,
                                     int rating, const QString &note,
                                     const QVariantMap &metrics, const QVariantMap &analysisDetail,
                                     bool dataWarning, const QString &lmDevice,
                                     const QVariantMap &dataWarningDetail,
                                     bool lmExcluded, const QString &lmClubOverride,
                                     const pinpoint::ShotRef &ref)
{
    // Reloaded numbers come from the documents, so move the allocator past them:
    // the first live shot after a reload must not be handed a number the session
    // has already used.
    m_nextOrdinal = std::max(m_nextOrdinal, ordinal + 1);

    Shot shot;
    shot.id              = m_nextId++;
    shot.ordinal         = ordinal;          // preserve the on-disk swing index
    shot.swingDir        = swingDir;
    shot.timestampLabel  = timestampLabel;
    shot.club            = club;
    shot.hasVideo        = hasVideo;
    shot.thumbnailSource = thumbnailSource;
    shot.score           = score;
    shot.rating          = rating;           // restored from the "review" block
    shot.note            = note;
    shot.metrics         = metrics;
    shot.analysisDetail  = analysisDetail;
    shot.dataWarning     = dataWarning;
    shot.dataWarningDetail = dataWarningDetail;
    shot.lmDevice        = lmDevice;
    shot.lmExcluded      = lmExcluded;       // the launch monitor board's annotations
    shot.lmClubOverride  = lmClubOverride;
    // A shot a Session holds (refactor step 3.4b-ii; the session under review, whose rows SessionReviewController
    // fills from its own parallel parse of the documents: #173): the review facts are the Shot's when it keeps a
    // summary; one it has not read yet is NOT read here, one row after another on the GUI thread (review 1, CN-2) —
    // the row shows what the caller has just parsed from the same document, and the Shot reads its own at the first
    // edit. Nothing is ever the row's own: every edit goes to the Shot and the row then shows the Shot's.
    shot.shot = ref;
    if (pinpoint::ShotSummary f;
        !swingDir.isEmpty() && shotFacts(swingDir, shot.shot, pinpoint::SummaryRead::KeptOnly, f)) {
        shot.held = true;
        if (f.ok)
            showShotReview(shot, f);
    }

    // Prepend (newest first); callers reload ascending so the highest ordinal lands first.
    beginInsertRows(QModelIndex(), 0, 0);
    m_shots.prepend(shot);
    endInsertRows();
    emit activeCountChanged();
}

void ShotListModel::loadSessionDir(const QString &dir)
{
    // Point the live carousel at a specific session folder — today's, on wrist
    // entry and at session start/end. An empty dir just clears (empty carousel).
    // Never called during live capture; clears then re-seeds from disk truth.
    clear();
    if (dir.isEmpty())
        return;
    // THE ROWS OVER THE SESSION'S SHOTS (refactor step 3.4b-ii): a session an Athlete holds is listed by its Session,
    // each row made from its Shot's summary — the same cheap read, made by the Shot through its repository, read
    // again here because the document is the truth at a load. The swingDir is today's text (this folder + the
    // swing's folder name), so every caller that matches rows by it finds the same row.
    if (m_commands) {
        if (const std::optional<QVector<pinpoint::HeldShot>> held = m_commands->sessionShots(dir)) {
            for (const pinpoint::HeldShot &h : *held)
                addHeldRow(dir + QStringLiteral("/") + QFileInfo(h.swingDir).fileName(), h.ref, h.summary);
            return;
        }
    }
    // A session no Athlete holds (N1): the carousel reads it itself, as today.
    // ⚠ THE CHEAP READ, and it has to be (2026-09-16). This ran the FULL parse on every swing in
    // the session — ~28 MB of document each, whose analysis.pose2d alone is ~13 MB, deep-converted
    // to QVariant — on the GUI thread, over a network share, in a debug build. Ending a session
    // froze the UI for ~10 s and grew with every shot taken. The summary sidecar carries everything
    // a row shows; the only thing it omits is analysisDetail, whose single consumer
    // (analysisDetailForSwingDir) parses on demand.
    for (const QString &sd : pinpoint::SwingDocReader::findSwingDirs(dir)) {
        const pinpoint::SwingSummary s = pinpoint::SwingDocReader::readSwingSummary(sd);
        if (!s.ok)
            continue;
        addPersistedShot(s.swingDir, s.ordinal, s.timestampLabel, s.club, s.hasVideo,
                         s.thumbnailPath.isEmpty() ? QUrl()
                                                   : QUrl::fromLocalFile(s.thumbnailPath),
                         s.score, s.rating, s.note, s.metrics, QVariantMap{},
                         s.dataWarning, s.lmDevice, s.dataWarningDetail,
                         s.excluded, s.clubOverride);
    }
}

void ShotListModel::refreshShot(const QString &swingDir)
{
    if (swingDir.isEmpty())
        return;
    int row = -1;
    for (int i = 0; i < m_shots.size(); ++i) {
        if (m_shots.at(i).swingDir == swingDir) { row = i; break; }
    }
    if (row < 0)
        return;
    pinpoint::PostShotTrace::Scope trace("refresh");   // [PostShot]: the whole document read again, then the row

    pinpoint::PersistedShot p;
    {
        pinpoint::PostShotTrace::Scope t("read");
        p = pinpoint::SwingDocReader::readSwingJson(swingDir);
    }
    if (!p.ok) {
        ppWarn() << "[ShotListModel] refreshShot: could not read" << swingDir;
        return;
    }

    Shot &s = m_shots[row];
    s.score          = p.score;
    s.metrics        = p.metrics;
    s.analysisDetail = p.analysisDetail;
    // Re-read alongside the readings it belongs to: this is the path a live row takes when
    // a monitor's numbers are folded in, so it is where the row learns which device sent them.
    s.lmDevice       = p.lmDevice;
    // A re-analysis re-reaches (or withdraws) the integrity verdicts, so the ⚠ must
    // follow the document here — before this it survived on screen until reload.
    s.dataWarning       = p.dataWarning;
    s.dataWarningDetail = p.dataWarningDetail;
    // The board's annotations follow the document too: they are the golfer's, and the document
    // is where they live — a row must never show a shot as left out that the file does not.
    s.lmExcluded        = p.excluded;
    s.lmClubOverride    = p.clubOverride;
    // And the club: following Premier's club can correct it in the same rewrite (launchmonitor/
    // premierFollowClub). An empty read keeps the row's club rather than blanking it.
    if (!p.club.isEmpty())
        s.club = p.club;
    // A shot a Session holds: those golfer's facts are its Shot's, read again (refactor step 3.4b-ii).
    {
        pinpoint::PostShotTrace::Scope t("review");
        takeShotReview(row, pinpoint::SummaryRead::Again);
    }
    pinpoint::PostShotTrace::Scope traceEmit("emit");   // the carousel's delegates (QML)
    emit dataChanged(index(row), index(row),
                     { ScoreRole, MetricsRole, AnalysisDetailRole, LmDeviceRole,
                       DataWarningRole, DataWarningDetailRole,
                       LmExcludedRole, LmClubOverrideRole, ClubRole, RatingRole, NoteRole });
}

void ShotListModel::attachSwingDir(int id, const QString &swingDir)
{
    if (swingDir.isEmpty())
        return;
    int row = -1;
    for (int i = 0; i < m_shots.size(); ++i)
        if (m_shots.at(i).id == id) { row = i; break; }
    if (row < 0)
        return;

    Shot &s = m_shots[row];
    if (!s.swingDir.isEmpty() && s.swingDir != swingDir) {
        ppWarn() << "[ShotListModel] attachSwingDir: row" << id
                 << "already points at" << s.swingDir << "— refusing to repoint it at" << swingDir;
        return;
    }

    const pinpoint::PersistedShot p = pinpoint::SwingDocReader::readSwingJson(swingDir);
    if (!p.ok) {
        ppWarn() << "[ShotListModel] attachSwingDir: could not read" << swingDir;
        return;
    }

    s.swingDir       = swingDir;
    s.shot           = {};   // translated afresh from the new folder (shotOf)
    s.score          = p.score;
    s.metrics        = p.metrics;
    s.analysisDetail = p.analysisDetail;
    s.club           = p.club;
    s.timestampLabel = p.timestampLabel;
    s.lmDevice       = p.lmDevice;
    s.dataWarning       = p.dataWarning;
    s.dataWarningDetail = p.dataWarningDetail;
    s.lmExcluded        = p.excluded;
    s.lmClubOverride    = p.clubOverride;
    // A shot a Session holds: the golfer's facts are its Shot's (refactor step 3.4b-ii).
    takeShotReview(row, pinpoint::SummaryRead::Again);
    emit dataChanged(index(row), index(row),
                     { SwingDirRole, ScoreRole, MetricsRole, AnalysisDetailRole,
                       ClubRole, TimestampLabelRole, LmDeviceRole,
                       DataWarningRole, DataWarningDetailRole,
                       LmExcludedRole, LmClubOverrideRole, RatingRole, NoteRole });
}

void ShotListModel::clear()
{
    // The allocator belongs to whatever the model is holding, so it resets even
    // when there are no rows to drop — loadSessionDir() clears an empty model
    // before seeding it from a different session's documents.
    m_nextOrdinal = 1;
    if (m_shots.isEmpty())
        return;
    beginResetModel();
    m_shots.clear();
    endResetModel();
    emit activeCountChanged();
}

// Persist the row's review (rating + note + club) to its swing.json, if it has
// one on disk. A row with no swingDir (export produced no directory) is in-memory
// only and silently skips the write-through.
// The edge (DEC-90): a row's folder translated to its ShotRef the first time the row writes, then kept
// (a row's folder never changes once set, except by attachSwingDir, which clears it).
pinpoint::ShotRef ShotListModel::shotOf(int row)
{
    Shot &s = m_shots[row];
    if (s.shot.isNull() && m_repo && !s.swingDir.isEmpty())
        s.shot = m_repo->shotOfFolder(s.swingDir);
    return s.shot;
}

void ShotListModel::persistReview(int row)
{
    const Shot &s = m_shots.at(row);
    if (s.swingDir.isEmpty())
        return;
    if (!m_repo) {
        ppWarn() << "[ShotListModel] review write-through skipped — no recording repository:" << s.swingDir;
        return;
    }
    pinpoint::ReviewEdit edit;
    edit.rating = s.rating;
    edit.note   = s.note;
    edit.club   = s.club;
    const pinpoint::WriteResult w = m_repo->editReview(shotOf(row), edit);
    if (!w.ok())
        ppWarn() << "[ShotListModel] review write-through failed:" << w.error;
}

ShotListModel::Edit ShotListModel::forwarded(int row, const char *what, const Forward &command)
{
    Shot &s = m_shots[row];
    if (s.swingDir.isEmpty())
        return Edit::InMemory;   // in memory only, as today: nothing to save, and no Shot to tell
    if (!m_commands)
        return Edit::NotHeld;
    const pinpoint::ShotRef ref = shotOf(row);
    if (ref.isNull())
        return Edit::NotHeld;
    const pinpoint::ForwardResult f = command(*m_commands, pinpoint::sessionFolderOf(s.swingDir), ref);
    s.held = f.reached;
    if (!f.reached)
        return Edit::NotHeld;   // no Session holds it (N1): today's write-through
    if (!f.result.ok())
        ppWarn() << "[ShotListModel]" << what << "not saved:" << f.result.error;
    return Edit::Shot;
}

// ── The rows over the Shots (refactor step 3.4b-ii) ──────────────────────────────────────────────────────────

bool ShotListModel::shotFacts(const QString &swingDir, pinpoint::ShotRef &ref, pinpoint::SummaryRead how,
                              pinpoint::ShotSummary &out)
{
    if (!m_commands || swingDir.isEmpty())
        return false;
    if (ref.isNull() && m_repo)
        ref = m_repo->shotOfFolder(swingDir);   // the edge (DEC-90), once per row
    if (ref.isNull())
        return false;
    const std::optional<pinpoint::ShotSummary> f =
        m_commands->shotSummary(pinpoint::sessionFolderOf(swingDir), ref, how);
    if (!f)
        return false;   // no Session holds it (N1)
    out = *f;
    if (!out.ok && (how == pinpoint::SummaryRead::Kept || how == pinpoint::SummaryRead::Again))
        ppWarn() << "[ShotListModel] the shot's summary does not read now; the row keeps what it shows:" << swingDir;
    return true;
}

void ShotListModel::showShotReview(Shot &s, const pinpoint::ShotSummary &f)
{
    // The facts the Shot's commands own (Shot::rate, note, setDeclaredClub, excludeReading, correctClub), as its
    // summary holds them. An empty club keeps the row's rather than blanking it (refreshShot's rule).
    s.rating         = f.rating;
    s.note           = f.note;
    if (!f.club.isEmpty())
        s.club = f.club;
    s.lmExcluded     = f.lmExcluded;
    s.lmClubOverride = f.lmClubOverride;
}

void ShotListModel::takeShotReview(int row, pinpoint::SummaryRead how)
{
    Shot &s = m_shots[row];
    pinpoint::ShotSummary f;
    s.held = shotFacts(s.swingDir, s.shot, how, f);
    if (s.held && f.ok)
        showShotReview(s, f);
}

void ShotListModel::addHeldRow(const QString &swingDir, const pinpoint::ShotRef &ref, const pinpoint::ShotSummary &f)
{
    // Reloaded numbers come from the documents, so move the allocator past them (addPersistedShot's rule).
    m_nextOrdinal = std::max(m_nextOrdinal, f.ordinal + 1);

    Shot shot;
    shot.id                = m_nextId++;
    shot.ordinal           = f.ordinal;
    shot.swingDir          = swingDir;
    shot.shot              = ref;
    shot.held              = true;
    shot.timestampLabel    = f.timestampLabel;
    shot.hasVideo          = f.hasVideo;
    shot.thumbnailSource   = f.thumbnailPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(f.thumbnailPath);
    shot.score             = f.score;
    shot.metrics           = f.metrics;
    shot.dataWarning       = f.dataWarning;
    shot.dataWarningDetail = f.dataWarningDetail;
    shot.lmDevice          = f.lmDevice;
    shot.club              = f.club;
    showShotReview(shot, f);
    // No analysisDetail: as the cheap load always was (its one consumer parses on demand).

    beginInsertRows(QModelIndex(), 0, 0);
    m_shots.prepend(shot);
    endInsertRows();
    emit activeCountChanged();
}

void ShotListModel::setRating(int id, int n)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    n = std::clamp(n, 0, 5);
    if (m_shots[row].rating == n)
        return;
    switch (forwarded(row, "the rating", [n](pinpoint::ShotCommandDoor &d, const QString &f, const pinpoint::ShotRef &r) {
                return d.rate(f, r, n); })) {
    case Edit::Shot:     takeShotReview(row, pinpoint::SummaryRead::Kept); break;   // the row shows what the Shot holds
    case Edit::NotHeld:  m_shots[row].rating = n; persistReview(row); break;
    case Edit::InMemory: m_shots[row].rating = n; break;
    }
    emit dataChanged(index(row), index(row), { RatingRole });
}

void ShotListModel::setNote(int id, const QString &text)
{
    const int row = rowForId(id);
    if (row < 0 || m_shots.at(row).note == text)
        return;
    switch (forwarded(row, "the note", [&text](pinpoint::ShotCommandDoor &d, const QString &f, const pinpoint::ShotRef &r) {
                return d.note(f, r, text); })) {
    case Edit::Shot:     takeShotReview(row, pinpoint::SummaryRead::Kept); break;
    case Edit::NotHeld:  m_shots[row].note = text; persistReview(row); break;
    case Edit::InMemory: m_shots[row].note = text; break;
    }
    emit dataChanged(index(row), index(row), { NoteRole });
}

void ShotListModel::setClub(int id, const QString &club)
{
    const int row = rowForId(id);
    if (row < 0 || m_shots.at(row).club == club)
        return;
    // ⚠ ONE OF THE CLUBS IN THE BAG, AND NOTHING ELSE. The picker only offers
    // clubOptions(), but this is Q_INVOKABLE and took whatever it was given —
    // and the value goes into swing.json's review block, where the session
    // ledger groups by it and the club-length prior is keyed on it. A club from
    // outside the vocabulary is a club nothing downstream can read.
    if (!clubOptions().contains(club)) {
        ppWarn() << "[ShotListModel] setClub refused — not in the bag:" << club;
        return;
    }
    switch (forwarded(row, "the declared club", [&club](pinpoint::ShotCommandDoor &d, const QString &f,
                                                        const pinpoint::ShotRef &r) { return d.setDeclaredClub(f, r, club); })) {
    case Edit::Shot:     takeShotReview(row, pinpoint::SummaryRead::Kept); break;
    case Edit::NotHeld:  m_shots[row].club = club; persistReview(row); break;
    case Edit::InMemory: m_shots[row].club = club; break;
    }
    emit dataChanged(index(row), index(row), { ClubRole });
}

// Persist the row's launch monitor annotations. Same rule as persistReview: no swingDir, no
// write, no error — the row simply keeps them for the life of the session.
void ShotListModel::persistLmAnnotations(int row)
{
    const Shot &s = m_shots.at(row);
    if (s.swingDir.isEmpty())
        return;
    if (!m_repo) {
        ppWarn() << "[ShotListModel] launch monitor annotation write-through skipped — no recording repository:"
                 << s.swingDir;
        return;
    }
    pinpoint::ReviewEdit edit;
    edit.excluded     = s.lmExcluded;
    edit.clubOverride = s.lmClubOverride;
    const pinpoint::WriteResult w = m_repo->editReview(shotOf(row), edit);
    if (!w.ok())
        ppWarn() << "[ShotListModel] launch monitor annotation write-through failed:" << w.error;
}

void ShotListModel::setLmExcluded(int id, bool excluded)
{
    const int row = rowForId(id);
    if (row < 0 || m_shots.at(row).lmExcluded == excluded)
        return;
    switch (forwarded(row, "the reading's exclusion", [excluded](pinpoint::ShotCommandDoor &d, const QString &f,
                                                                 const pinpoint::ShotRef &r) {
                return d.excludeReading(f, r, excluded); })) {
    case Edit::Shot:     takeShotReview(row, pinpoint::SummaryRead::Kept); break;
    case Edit::NotHeld:  m_shots[row].lmExcluded = excluded; persistLmAnnotations(row); break;
    case Edit::InMemory: m_shots[row].lmExcluded = excluded; break;
    }
    emit dataChanged(index(row), index(row), { LmExcludedRole });
}

void ShotListModel::setLmClubOverride(int id, const QString &club)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    const QString c = club.trimmed();
    // ⚠ THE SAME VOCABULARY RULE AS setClub, plus the shot's OWN recorded club: choosing it is
    // how the golfer declines the evidence's relabel, and an older document may spell it in a
    // form the bag no longer lists ("7-iron") — refusing that would make the decline impossible.
    const QString recorded = m_shots.at(row).club;
    QString stored = c;
    if (!c.isEmpty() && c.compare(recorded.trimmed(), Qt::CaseInsensitive) == 0)
        stored = recorded.trimmed();
    else if (!c.isEmpty() && !clubOptions().contains(c)) {
        ppWarn() << "[ShotListModel] setLmClubOverride refused — not in the bag:" << club;
        return;
    }
    if (m_shots.at(row).lmClubOverride == stored)
        return;
    switch (forwarded(row, "the club correction", [&stored](pinpoint::ShotCommandDoor &d, const QString &f,
                                                            const pinpoint::ShotRef &r) {
                return d.correctClub(f, r, stored); })) {
    case Edit::Shot:     takeShotReview(row, pinpoint::SummaryRead::Kept); break;
    case Edit::NotHeld:  m_shots[row].lmClubOverride = stored; persistLmAnnotations(row); break;
    case Edit::InMemory: m_shots[row].lmClubOverride = stored; break;
    }
    emit dataChanged(index(row), index(row), { LmClubOverrideRole });
}

QStringList ShotListModel::clubOptions()
{
    // The one canonical bag — shared with the Markup Lab picker (see club_vocabulary.h).
    return pinpoint::clubVocabulary();
}

// ⚠ SwingPaths::trashPath, not QFile::moveToTrash: the SwingData share has no OS
// trash, and every delete there failed silently until 2 Sept 2026.
//
// A SHOT A SESSION HOLDS IS TRASHED BY ITS SESSION (refactor step 3.4b-ii; BRIEF §3.3: Session::trashShot, EVT-03):
// the same move (SessionRepository::trashShot → SwingPaths::trashPath, its number kept as the high-water) under the
// session's lock, and the Session's collection lets the Shot go. Otherwise (N1) the move itself, as today.
bool ShotListModel::trashFolder(const QString &dir, const pinpoint::ShotRef &shot) const
{
    if (m_commands && !shot.isNull()) {
        QString where;
        const pinpoint::ForwardResult f = m_commands->trashShot(pinpoint::sessionFolderOf(dir), shot, &where);
        if (f.reached) {
            if (!f.result.ok())
                ppWarn() << "[ShotListModel] the shot's session did not trash it:" << dir << "-" << f.result.error;
            return f.result.ok();
        }
    }
    return m_trashFn ? m_trashFn(dir) : pinpoint::SwingPaths::trashPath(dir);
}

void ShotListModel::removeTrashedRow(int id, const QString &dir)
{
    // The row may already be gone: the model was re-pointed at another session
    // while its folder was still being retried. The folder went all the same.
    const int row = rowForId(id);
    if (row >= 0) {
        beginRemoveRows(QModelIndex(), row, row);
        m_shots.removeAt(row);
        endRemoveRows();
        emit activeCountChanged();
    }
    emit shotTrashed(id, dir);
}

bool ShotListModel::moveToTrash(int id)
{
    const int row = rowForId(id);
    if (row < 0)
        return false;
    // Move the on-disk folder to the trash (recoverable there). An
    // analysis-only shot has no folder and simply drops from the model.
    const QString dir = m_shots.at(row).swingDir;
    if (!dir.isEmpty()) {
        if (m_releaser)
            m_releaser(dir);
        if (!trashFolder(dir, shotOf(row))) {
            ppWarn() << "[ShotListModel] could not move to trash:" << dir;
            return false;             // keep the row — its files are still here
        }
    }
    removeTrashedRow(id, dir);
    return true;
}

void ShotListModel::trashShots(const QVariantList &ids)
{
    for (const QVariant &v : ids) {
        const int id  = v.toInt();
        const int row = rowForId(id);
        if (row < 0)
            continue;
        if (std::any_of(m_trashPending.cbegin(), m_trashPending.cend(),
                        [id](const PendingTrash &p) { return p.id == id; }))
            continue;                 // already on its way
        ++m_trashRequested;
        const Shot &s = m_shots.at(row);
        if (s.swingDir.isEmpty()) {   // nothing on disk: the row is all there is
            ++m_trashMoved;
            removeTrashedRow(id, QString());
            continue;
        }
        if (m_busyCheck) {
            const QString why = m_busyCheck(s.swingDir);
            if (!why.isEmpty()) {
                m_trashReasons << tr("Swing #%1 %2").arg(s.ordinal).arg(why);
                continue;
            }
        }
        if (m_releaser)
            m_releaser(s.swingDir);
        const QString dir = s.swingDir;
        const int     ordinal = s.ordinal;
        m_trashPending.append(PendingTrash{ id, ordinal, dir, 0, shotOf(row) });
    }

    if (m_trashTimer.isActive())
        return;                       // a retry is already due; the new folders go with it
    if (!m_trashClock.isValid()) {
        m_trashClock.start();
        m_trashNextRetryMs = m_trashFirstRetryMs;
    }
    attemptPendingTrash();
}

void ShotListModel::attemptPendingTrash()
{
    for (int i = 0; i < m_trashPending.size(); ) {
        PendingTrash &p = m_trashPending[i];
        ++p.attempts;
        // Gone already counts as gone: someone else moved it while we waited.
        if (trashFolder(p.dir, p.shot) || !QFileInfo::exists(p.dir)) {
            if (p.attempts > 1)
                ppInfo() << "[ShotListModel] moved to trash on attempt" << p.attempts
                         << "after" << m_trashClock.elapsed() << "ms:" << p.dir;
            const PendingTrash done = p;
            m_trashPending.removeAt(i);
            ++m_trashMoved;
            removeTrashedRow(done.id, done.dir);
            continue;
        }
        ++i;
    }

    if (m_trashPending.isEmpty()) {
        finishTrash();
        return;
    }
    if (m_trashClock.elapsed() + m_trashNextRetryMs > m_trashGiveUpMs) {
        for (const PendingTrash &p : std::as_const(m_trashPending)) {
            ppWarn() << "[ShotListModel] could not move to trash after" << p.attempts
                     << "attempts over" << m_trashClock.elapsed() << "ms — a file in it is"
                     << "probably still open:" << p.dir;
            m_trashReasons << tr("Swing #%1: its files are still in use").arg(p.ordinal);
        }
        m_trashPending.clear();
        finishTrash();
        return;
    }
    m_trashTimer.start(m_trashNextRetryMs);
    m_trashNextRetryMs *= 2;
}

void ShotListModel::finishTrash()
{
    const int requested = std::exchange(m_trashRequested, 0);
    const int moved     = std::exchange(m_trashMoved, 0);
    const QStringList reasons = std::exchange(m_trashReasons, {});
    m_trashClock.invalidate();
    if (requested == 0)
        return;                       // nothing asked for was in this model
    emit trashFinished(requested, moved, reasons.join(QStringLiteral("; ")));
}

int ShotListModel::moveAllToTrash(const QVariantList &ids)
{
    // Delegate per id — moveToTrash() re-resolves the row each time, so the
    // index shift from each removeAt() can't stale a cached row.
    int moved = 0;
    for (const QVariant &v : ids)
        if (moveToTrash(v.toInt()))
            ++moved;
    return moved;
}

QVariantList ShotListModel::swingDirsForIds(const QVariantList &ids) const
{
    QVariantList dirs;
    for (const QVariant &v : ids) {
        const int row = rowForId(v.toInt());
        if (row < 0)
            continue;
        const QString &dir = m_shots.at(row).swingDir;
        if (!dir.isEmpty())
            dirs.append(dir);
    }
    return dirs;
}

QVariantMap ShotListModel::shotSummary(int id) const
{
    const int row = rowForId(id);
    if (row < 0)
        return { { QStringLiteral("valid"), false } };
    const Shot &s = m_shots.at(row);
    return {
        { QStringLiteral("valid"),          true },
        { QStringLiteral("ordinal"),        s.ordinal },
        { QStringLiteral("club"),           s.club },
        { QStringLiteral("timestampLabel"), s.timestampLabel },
        { QStringLiteral("score"),          s.score },
        { QStringLiteral("rating"),         s.rating },
        { QStringLiteral("note"),           s.note },
        { QStringLiteral("hasVideo"),       s.hasVideo },
        { QStringLiteral("swingDir"),       s.swingDir },
        { QStringLiteral("dataWarning"),       s.dataWarning },
        { QStringLiteral("dataWarningDetail"), s.dataWarningDetail },
    };
}

QVariantMap ShotListModel::previousAnalysisDetail(const QString &swingDir) const
{
    if (swingDir.isEmpty())
        return {};
    for (int i = 0; i < m_shots.size(); ++i) {
        if (m_shots.at(i).swingDir != swingDir)
            continue;
        const int older = i + 1;               // newest-first → the next row is the previous swing
        return (older < m_shots.size()) ? m_shots.at(older).analysisDetail.map() : QVariantMap{};
    }
    return {};
}

QVariantMap ShotListModel::analysisDetailForSwingDir(const QString &swingDir) const
{
    if (swingDir.isEmpty())
        return {};
    for (const Shot &s : m_shots)
        if (s.swingDir == swingDir)
            return s.analysisDetail.map();
    // Not in this list is not "does not exist" — see the header. The row is the fast path
    // (it is already parsed); the document is the truth.
    const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(swingDir);
    return ps.ok ? ps.analysisDetail : QVariantMap{};
}

