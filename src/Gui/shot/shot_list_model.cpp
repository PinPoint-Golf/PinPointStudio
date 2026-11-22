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

#include "../Core/club_vocabulary.h"
#include "../Core/pp_debug.h"
#include "../../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S)

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <climits>
#include <utility>

namespace {
// A folder as one key: the same session however its path is spelled.
QString folderKey(const QString &folder)
{
    return folder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
}
// The roles a Shot's event can change, and a rewrite of its document (EVT-22, EVT-02): refreshShot's set, as it was.
const QList<int> kReviewRoles{ ShotListModel::ClubRole, ShotListModel::RatingRole, ShotListModel::NoteRole,
                               ShotListModel::LmExcludedRole, ShotListModel::LmClubOverrideRole };
const QList<int> kRewriteRoles{ ShotListModel::ScoreRole, ShotListModel::MetricsRole, ShotListModel::LmDeviceRole,
                                ShotListModel::DataWarningRole, ShotListModel::DataWarningDetailRole,
                                ShotListModel::LmExcludedRole, ShotListModel::LmClubOverrideRole, ShotListModel::ClubRole,
                                ShotListModel::RatingRole, ShotListModel::NoteRole };
} // namespace

ShotListModel::ShotListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_trashTimer.setSingleShot(true);
    connect(&m_trashTimer, &QTimer::timeout, this, &ShotListModel::attemptPendingTrash);
}

ShotListModel::~ShotListModel() = default;

int ShotListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

const pinpoint::ShotSummary *ShotListModel::factsOf(const Row &row) const
{
    if (row.unsaved || !m_source || row.shot.isNull())
        return nullptr;
    const pinpoint::ShotSummary *f = m_source->summary(row.shot, pinpoint::SummaryRead::Kept);
    return (f && f->ok) ? f : nullptr;
}

QVariant ShotListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    if (role == ShotIdRole)      return r.id;
    if (role == SwingDirRole)    return r.swingDir;
    if (role == TracePointsRole) return r.tracePoints;
    if (r.unsaved) {
        const CapturedShot &c = r.captured;
        switch (role) {
        case OrdinalRole:           return r.ordinal;
        case TimestampLabelRole:    return c.timestampLabel;
        case ClubRole:              return c.club;
        case HasVideoRole:          return c.hasVideo;
        case ThumbnailSourceRole:   return r.thumbnailSource;
        case ScoreRole:             return c.score;
        case RatingRole:            return r.rating;
        case NoteRole:              return r.note;
        case MetricsRole:           return c.metrics;
        case DataWarningRole:       return c.dataWarning;
        case DataWarningDetailRole: return c.dataWarningDetail;
        case LmDeviceRole:          return QString();
        case LmExcludedRole:        return r.lmExcluded;
        case LmClubOverrideRole:    return r.lmClubOverride;
        default:                    return {};
        }
    }
    // A Shot's row: every fact is its Shot's, read now (its kept summary).
    const pinpoint::ShotSummary *f = factsOf(r);
    const pinpoint::ShotSummary none;
    const pinpoint::ShotSummary &s = f ? *f : none;
    switch (role) {
    case OrdinalRole:           return s.ordinal;
    case TimestampLabelRole:    return s.timestampLabel;
    case ClubRole:              return s.club;
    case HasVideoRole:          return s.hasVideo;
    case ThumbnailSourceRole:   return !r.thumbnailSource.isEmpty() ? r.thumbnailSource
                                     : s.thumbnailPath.isEmpty()   ? QUrl() : QUrl::fromLocalFile(s.thumbnailPath);
    case ScoreRole:             return s.score;
    case RatingRole:            return s.rating;
    case NoteRole:              return s.note;
    case MetricsRole:           return s.metrics;
    case DataWarningRole:       return s.dataWarning;
    case DataWarningDetailRole: return s.dataWarningDetail;
    case LmDeviceRole:          return s.lmDevice;
    case LmExcludedRole:        return s.lmExcluded;
    case LmClubOverrideRole:    return s.lmClubOverride;
    default:                    return {};
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
        { SwingDirRole,        "swingDir"        },
        { DataWarningRole,     "dataWarning"     },
        { LmDeviceRole,        "lmDevice"        },
        { DataWarningDetailRole, "dataWarningDetail" },
        { LmExcludedRole,      "lmExcluded"      },
        { LmClubOverrideRole,  "lmClubOverride"  },
    };
}

int ShotListModel::activeCount() const
{
    return m_rows.size();
}

int ShotListModel::rowForId(int id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id)
            return i;
    return -1;
}

int ShotListModel::rowForShot(const pinpoint::ShotRef &shot) const
{
    if (shot.isNull())
        return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).shot == shot)
            return i;
    return -1;
}

int ShotListModel::ordinalForId(int id) const
{
    const int row = rowForId(id);
    return row < 0 ? 0 : data(index(row), OrdinalRole).toInt();
}

// ── the session shown ──────────────────────────────────────────────────────────────────────────────────────────

QString ShotListModel::sessionFolder() const
{
    return m_source ? m_source->folder() : QString();
}

bool ShotListModel::hasSavedShots() const
{
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row &r) { return !r.unsaved; });
}

void ShotListModel::showSession(const QString &folder)
{
    // THE SESSION ALREADY SHOWN: nothing (4-a's B3 by design — End, re-entering a screen, a reload of the same session
    // never reset the rows, so the selection and the replay never see the session go).
    if (m_source && !folder.isEmpty() && folderKey(m_source->folder()) == folderKey(folder))
        return;
    if (folder.isEmpty()) {
        setSource(nullptr);
        return;
    }
    std::unique_ptr<pinpoint::ShotSource> source = m_sourceFor ? m_sourceFor(folder) : nullptr;
    if (!source)
        ppWarn() << "[ShotListModel] no Session holds" << folder << "- its shots are not listed";
    setSource(std::move(source));
}

void ShotListModel::setSource(std::unique_ptr<pinpoint::ShotSource> source)
{
    if (!source && !m_source && m_rows.isEmpty())
        return;
    if (source && m_source && folderKey(source->folder()) == folderKey(m_source->folder()))
        return;   // the same session: keep the rows (and the source that already holds them)
    beginResetModel();
    m_rows.clear();
    m_nextOrdinal = 1;
    m_source = std::move(source);
    if (pinpoint::ShotSource *s = m_source.get()) {
        // Each callback checks it is still this view's source (a source let go of is deleted later: onGone).
        pinpoint::ShotSourceEvents e;
        e.added   = [this, s](const pinpoint::ShotRef &r) { if (m_source.get() == s) onAdded(r); };
        e.changed = [this, s](const pinpoint::ShotRef &r, bool rw) { if (m_source.get() == s) onChanged(r, rw); };
        e.trashed = [this, s](const pinpoint::ShotRef &r) { if (m_source.get() == s) onTrashed(r); };
        e.gone    = [this, s] { if (m_source.get() == s) onGone(); };
        s->watch(std::move(e));
        // Its Shots that have a document, read again (the document is the truth when a session is shown), newest first.
        for (const pinpoint::ShotRef &ref : s->shots()) {
            const pinpoint::ShotSummary *f = s->summary(ref, pinpoint::SummaryRead::Again);
            if (!f || !f->ok)
                continue;
            Row row;
            row.id       = m_nextId++;
            row.shot     = ref;
            row.swingDir = s->swingFolder(ref);
            m_nextOrdinal = std::max(m_nextOrdinal, f->ordinal + 1);
            m_rows.insert(insertPosition(ref), row);
        }
    }
    endResetModel();
    emit activeCountChanged();
}

int ShotListModel::insertPosition(const pinpoint::ShotRef &shot) const
{
    const int n = shot.isNull() ? INT_MAX : shot.swingNumber();
    for (int i = 0; i < m_rows.size(); ++i) {
        const pinpoint::ShotRef &r = m_rows.at(i).shot;
        if ((r.isNull() ? INT_MAX : r.swingNumber()) < n)
            return i;
    }
    return m_rows.size();
}

void ShotListModel::insertRow(const Row &row)
{
    const int at = row.unsaved && row.shot.isNull() ? 0 : insertPosition(row.shot);
    beginInsertRows(QModelIndex(), at, at);
    m_rows.insert(at, row);
    endInsertRows();
    emit activeCountChanged();
}

int ShotListModel::addShotRow(const pinpoint::ShotRef &shot, pinpoint::SummaryRead how)
{
    if (!m_source || shot.isNull())
        return -1;
    const pinpoint::ShotSummary *f = m_source->summary(shot, how);
    // The cheap read finds no summary for a document older than swing.ppsw with no sidecar: read it whole then, once.
    if (how == pinpoint::SummaryRead::AgainCheap && (!f || !f->ok))
        f = m_source->summary(shot, pinpoint::SummaryRead::Again);
    if (!f || !f->ok)
        return -1;
    Row row;
    row.id       = m_nextId++;
    row.shot     = shot;
    row.swingDir = m_source->swingFolder(shot);
    m_nextOrdinal = std::max(m_nextOrdinal, f->ordinal + 1);
    insertRow(row);
    return rowForId(row.id);
}

void ShotListModel::forgetCapture()
{
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (!m_rows.at(i).unsaved)
            continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.removeAt(i);
        endRemoveRows();
        emit activeCountChanged();
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (r.thumbnailSource.isEmpty() && r.tracePoints.isEmpty())
            continue;
        r.thumbnailSource = QUrl();
        r.tracePoints.clear();
        emit dataChanged(index(i), index(i), { ThumbnailSourceRole, TracePointsRole });
    }
}

void ShotListModel::rereadShots()
{
    if (!m_source)
        return;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).unsaved)
            continue;
        m_source->summary(m_rows.at(i).shot, pinpoint::SummaryRead::Again);
        emit dataChanged(index(i), index(i), {});
    }
}

// ── the Session's events ───────────────────────────────────────────────────────────────────────────────────────

// EVT-01: a shot's first document has landed. Its row is made — or, when capture had already given it a card with
// nothing saved (a launch monitor's device-only document filling that shot's folder), that card becomes its Shot's row
// in place: one row for one swing, never two.
void ShotListModel::onAdded(const pinpoint::ShotRef &shot)
{
    const int row = rowForShot(shot);
    if (row < 0) {
        addShotRow(shot, pinpoint::SummaryRead::AgainCheap);   // the summary, never a whole document (J5)
        return;
    }
    Row &r = m_rows[row];
    if (!r.unsaved)
        return;
    const pinpoint::ShotSummary *f = m_source->summary(shot, pinpoint::SummaryRead::Again);   // as attaching read it
    if (!f || !f->ok)
        return;
    r.unsaved  = false;
    r.captured = {};
    r.swingDir = m_source->swingFolder(shot);
    m_nextOrdinal = std::max(m_nextOrdinal, f->ordinal + 1);
    emit dataChanged(index(row), index(row), {});
}

// One of the Shot's events. A rewrite of its document (a late part, a re-analysis) reads its summary again; the
// others' facts are already in the summary the Shot keeps. The row keeps its id, its place and the selection.
void ShotListModel::onChanged(const pinpoint::ShotRef &shot, bool rewritten)
{
    if (!m_editing.isNull() && shot == m_editing)
        return;   // the edit in hand says so itself, once
    const int row = rowForShot(shot);
    if (row < 0 || m_rows.at(row).unsaved)
        return;
    if (rewritten) {
        pinpoint::PostShotTrace::Scope trace("refresh");   // [PostShot]: the summary read again, then the row
        {
            pinpoint::PostShotTrace::Scope t("read");
            m_source->summary(shot, pinpoint::SummaryRead::Again);
        }
        pinpoint::PostShotTrace::Scope traceEmit("emit");   // the carousel's delegates (QML)
        emit dataChanged(index(row), index(row), kRewriteRoles);
        return;
    }
    emit dataChanged(index(row), index(row), kReviewRoles);
}

// EVT-03: the Session trashed the shot (the carousel's delete, or anyone's).
void ShotListModel::onTrashed(const pinpoint::ShotRef &shot)
{
    const int row = rowForShot(shot);
    if (row < 0)
        return;
    const int id = m_rows.at(row).id;
    const QString dir = m_rows.at(row).swingDir;
    m_leftByEvent.insert(id);   // a trash of it still being retried counts it moved (gone counts as gone)
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit activeCountChanged();
    emit shotTrashed(id, dir);
}

// The Session went (released, trashed, moved to another athlete): nothing to show. The source is deleted on the next
// turn — this runs inside its own callback.
void ShotListModel::onGone()
{
    std::shared_ptr<pinpoint::ShotSource> old(m_source.release());
    QTimer::singleShot(0, [old] {});
    beginResetModel();
    m_rows.clear();
    m_nextOrdinal = 1;
    endResetModel();
    emit activeCountChanged();
}

// ── the capture path ───────────────────────────────────────────────────────────────────────────────────────────

int ShotListModel::landShot(const CapturedShot &c, bool documentWritten)
{
    int row = rowForShot(c.shot);
    // EVT-01 made the row as the document landed; if it did not (the summary did not read), the Shot's row now.
    if (row < 0 && documentWritten)
        row = addShotRow(c.shot, pinpoint::SummaryRead::AgainCheap);
    if (row >= 0) {
        Row &r = m_rows[row];
        r.thumbnailSource = c.thumbnailSource;   // presentation: what capture just drew
        r.tracePoints     = c.tracePoints;
        if (r.unsaved)
            r.captured = c;
        emit dataChanged(index(row), index(row), r.unsaved ? QList<int>{} : QList<int>{ ThumbnailSourceRole, TracePointsRole });
        return r.id;
    }
    // The shot happened — it always lands on the carousel, with whatever the pipeline produced. Nothing saved: a card
    // with capture's values and the next number this view has not issued (⚠ NOT max(ordinal) + 1 over the rows: trash
    // the newest shot and hit another, and that recomputed the number that just left).
    Row r;
    r.id              = m_nextId++;
    r.shot            = c.shot;
    r.unsaved         = true;
    r.captured        = c;
    r.thumbnailSource = c.thumbnailSource;
    r.tracePoints     = c.tracePoints;
    r.ordinal         = m_nextOrdinal++;
    insertRow(r);
    return r.id;
}

// ── the golfer's edits ─────────────────────────────────────────────────────────────────────────────────────────

ShotListModel::Edit ShotListModel::forwarded(int row, const char *what,
    const std::function<pinpoint::WriteResult(pinpoint::ShotSource &, const pinpoint::ShotRef &)> &command)
{
    const Row &r = m_rows.at(row);
    if (r.unsaved)
        return Edit::Card;   // nothing saved for this shot: the card is all there is
    QString words = QString::fromLatin1(what);
    if (!words.isEmpty()) words[0] = words[0].toUpper();
    if (!m_source || r.shot.isNull()) {
        const QString why = tr("No golfer's session holds this shot, so nothing was saved.");
        ppWarn() << "[ShotListModel]" << what << "not saved: no Session holds" << r.swingDir;
        emit saveFailed(words, why);
        return Edit::Refused;
    }
    const pinpoint::ShotRef shot = r.shot;
    m_editing = shot;
    const pinpoint::WriteResult w = command(*m_source, shot);
    m_editing = {};
    if (!w.ok()) {
        ppWarn() << "[ShotListModel]" << what << "not saved:" << w.error;
        emit saveFailed(words, w.error);
    }
    return Edit::Shot;
}

void ShotListModel::setRating(int id, int n)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    n = std::clamp(n, 0, 5);
    if (data(index(row), RatingRole).toInt() == n)
        return;
    if (forwarded(row, "the rating", [n](pinpoint::ShotSource &s, const pinpoint::ShotRef &r) { return s.rate(r, n); })
        == Edit::Card)
        m_rows[row].rating = n;
    emit dataChanged(index(row), index(row), { RatingRole });   // what the Shot holds now (or the card)
}

void ShotListModel::setNote(int id, const QString &text)
{
    const int row = rowForId(id);
    if (row < 0 || data(index(row), NoteRole).toString() == text)
        return;
    if (forwarded(row, "the note", [&text](pinpoint::ShotSource &s, const pinpoint::ShotRef &r) { return s.note(r, text); })
        == Edit::Card)
        m_rows[row].note = text;
    emit dataChanged(index(row), index(row), { NoteRole });
}

void ShotListModel::setClub(int id, const QString &club)
{
    const int row = rowForId(id);
    if (row < 0 || data(index(row), ClubRole).toString() == club)
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
    if (forwarded(row, "the declared club", [&club](pinpoint::ShotSource &s, const pinpoint::ShotRef &r) {
            return s.setDeclaredClub(r, club); }) == Edit::Card)
        m_rows[row].captured.club = club;
    emit dataChanged(index(row), index(row), { ClubRole });
}

void ShotListModel::setLmExcluded(int id, bool excluded)
{
    const int row = rowForId(id);
    if (row < 0 || data(index(row), LmExcludedRole).toBool() == excluded)
        return;
    if (forwarded(row, "the reading's exclusion", [excluded](pinpoint::ShotSource &s, const pinpoint::ShotRef &r) {
            return s.excludeReading(r, excluded); }) == Edit::Card)
        m_rows[row].lmExcluded = excluded;
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
    const QString recorded = data(index(row), ClubRole).toString();
    QString stored = c;
    if (!c.isEmpty() && c.compare(recorded.trimmed(), Qt::CaseInsensitive) == 0)
        stored = recorded.trimmed();
    else if (!c.isEmpty() && !clubOptions().contains(c)) {
        ppWarn() << "[ShotListModel] setLmClubOverride refused — not in the bag:" << club;
        return;
    }
    if (data(index(row), LmClubOverrideRole).toString() == stored)
        return;
    if (forwarded(row, "the club correction", [&stored](pinpoint::ShotSource &s, const pinpoint::ShotRef &r) {
            return s.correctClub(r, stored); }) == Edit::Card)
        m_rows[row].lmClubOverride = stored;
    emit dataChanged(index(row), index(row), { LmClubOverrideRole });
}

QStringList ShotListModel::clubOptions()
{
    // The one canonical bag — shared with the Markup Lab picker (see club_vocabulary.h).
    return pinpoint::clubVocabulary();
}

// ── the trash ──────────────────────────────────────────────────────────────────────────────────────────────────

// THE SESSION'S TRASH (Session::trashShot, EVT-03): the move (SwingPaths::trashPath — ⚠ not QFile::moveToTrash: the
// SwingData share has no OS trash) under the session's lock, its number kept as the high-water; the row leaves on the
// event. A swing already gone counts as gone (the store's rule).
bool ShotListModel::trashOne(const QString &dir, const pinpoint::ShotRef &shot)
{
    if (!m_source || shot.isNull()) {
        ppWarn() << "[ShotListModel] not moved to the trash: no Session holds" << dir;
        return false;
    }
    QString where;
    const pinpoint::WriteResult w = m_source->trash(shot, &where);
    if (!w.ok())
        ppWarn() << "[ShotListModel] the shot's session did not trash it:" << dir << "-" << w.error;
    return w.ok();
}

// A card with nothing saved, or a row whose event has not removed it: it leaves here. A Shot's row has already left on
// its EVT-03 (onTrashed said so).
void ShotListModel::removeTrashedRow(int id, const QString &dir)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit activeCountChanged();
    emit shotTrashed(id, dir);
}

bool ShotListModel::moveToTrash(int id)
{
    const int row = rowForId(id);
    if (row < 0)
        return false;
    const QString dir = m_rows.at(row).swingDir;
    if (!m_rows.at(row).unsaved) {
        if (m_releaser && !dir.isEmpty())
            m_releaser(dir);
        if (!trashOne(dir, m_rows.at(row).shot)) {
            ppWarn() << "[ShotListModel] could not move to trash:" << dir;
            return false;             // keep the row — its files are still here
        }
    }
    removeTrashedRow(id, dir);
    return true;
}

void ShotListModel::trashShots(const QList<int> &ids)
{
    for (const int id : ids) {
        const int row = rowForId(id);
        if (row < 0)
            continue;
        if (std::any_of(m_trashPending.cbegin(), m_trashPending.cend(),
                        [id](const PendingTrash &p) { return p.id == id; }))
            continue;                 // already on its way
        ++m_trashRequested;
        const Row &r = m_rows.at(row);
        if (r.unsaved) {              // nothing on disk: the card is all there is
            ++m_trashMoved;
            removeTrashedRow(id, QString());
            continue;
        }
        if (m_busyCheck) {
            const QString why = m_busyCheck(r.swingDir);
            if (!why.isEmpty()) {
                m_trashReasons << tr("Swing #%1 %2").arg(data(index(row), OrdinalRole).toInt()).arg(why);
                continue;
            }
        }
        if (m_releaser)
            m_releaser(r.swingDir);
        m_trashPending.append(PendingTrash{ id, data(index(row), OrdinalRole).toInt(), r.swingDir, 0, r.shot });
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
        // Gone already counts as gone: its row left on an EVT-03 while we waited (someone else trashed it), or the
        // Session's trash says it went.
        if (m_leftByEvent.remove(p.id) || trashOne(p.dir, p.shot)) {
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
    m_leftByEvent.clear();
    const QStringList reasons = std::exchange(m_trashReasons, {});
    m_trashClock.invalidate();
    if (requested == 0)
        return;                       // nothing asked for was in this model
    emit trashFinished(requested, moved, reasons.join(QStringLiteral("; ")));
}

int ShotListModel::moveAllToTrash(const QList<int> &ids)
{
    // Delegate per id — moveToTrash() re-resolves the row each time, so the
    // index shift from each removal can't stale a cached row.
    int moved = 0;
    for (const int id : ids)
        if (moveToTrash(id))
            ++moved;
    return moved;
}

// ── reads for QML ──────────────────────────────────────────────────────────────────────────────────────────────

QStringList ShotListModel::swingDirsForIds(const QList<int> &ids) const
{
    QStringList dirs;
    for (const int id : ids) {
        const int row = rowForId(id);
        if (row < 0)
            continue;
        const QString &dir = m_rows.at(row).swingDir;
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
    const QModelIndex ix = index(row);
    return {
        { QStringLiteral("valid"),             true },
        { QStringLiteral("ordinal"),           data(ix, OrdinalRole) },
        { QStringLiteral("club"),              data(ix, ClubRole) },
        { QStringLiteral("timestampLabel"),    data(ix, TimestampLabelRole) },
        { QStringLiteral("score"),             data(ix, ScoreRole) },
        { QStringLiteral("rating"),            data(ix, RatingRole) },
        { QStringLiteral("note"),              data(ix, NoteRole) },
        { QStringLiteral("hasVideo"),          data(ix, HasVideoRole) },
        { QStringLiteral("swingDir"),          data(ix, SwingDirRole) },
        { QStringLiteral("dataWarning"),       data(ix, DataWarningRole) },
        { QStringLiteral("dataWarningDetail"), data(ix, DataWarningDetailRole) },
    };
}

QString ShotListModel::shotRefOf(int id) const
{
    const int row = rowForId(id);
    return row < 0 ? QString() : m_rows.at(row).shot.toString();
}

int ShotListModel::idOfShotRef(const pinpoint::ShotRef &shot) const
{
    const int row = rowForShot(shot);
    return row < 0 ? -1 : m_rows.at(row).id;
}

int ShotListModel::idOfShot(const QString &shotRef, const QString &swingDir) const
{
    const pinpoint::ShotRef want = pinpoint::ShotRef::fromString(shotRef);
    if (!want.isNull())
        return idOfShotRef(want);
    if (swingDir.isEmpty())
        return -1;
    for (const Row &r : m_rows)
        if (!r.swingDir.isEmpty() && QDir::cleanPath(r.swingDir) == QDir::cleanPath(swingDir))
            return r.id;
    return -1;
}
