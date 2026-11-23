/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "shot.h"
#include "session.h"
#include "recording_repository.h"

#include "../Core/pp_debug.h"

#include <QCoreApplication>

#include <algorithm>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace pinpoint {

namespace {
WriteResult noDoor()
{
    return { WriteResult::Status::Failed, QStringLiteral("no recording repository") };
}
} // namespace

Shot::Shot(const ShotRef &ref, std::optional<recording::ShotKind> kind, const ShotDoors &doors, Session *parent)
    : QObject(parent), m_ref(ref), m_doors(doors), m_kind(kind)
{
}

bool Shot::documentIsDeviceOnly() const
{
    return m_doors.recordings && m_doors.recordings->isDeviceOnly(m_ref);
}

recording::ShotKind Shot::kind() const
{
    // Read once, then kept: the kind never changes (CLS-SHOT). Without a door it is not known to be
    // device-only, which is what the classification answers for a shot it cannot find.
    if (!m_kind)
        m_kind = (m_doors.recordings && m_doors.recordings->isDeviceOnly(m_ref)) ? recording::ShotKind::DeviceOnly
                                                                                : recording::ShotKind::Captured;
    return *m_kind;
}

// ── Its diagnostics (refactor 4-b): a view over its Session's owner, never a copy ─────────────────────────────

ShotDiagnostics Shot::diagnostics() const
{
    // Its parent is its Session (P10: Session is the one friend that makes it). No owner: no value ("-").
    const auto *session = static_cast<const Session *>(parent());
    return ShotDiagnostics(session ? session->diagnostics() : nullptr, swingNumber());
}

// ── The summary (refactor step 3.4b-ii: the carousel's rows over the Shots) ─────────────────────────────────

const ShotSummary &Shot::summary()
{
    return m_summaryRead ? m_summary : rereadSummary();
}

const ShotSummary &Shot::rereadSummary(bool parseIfUnindexed)
{
    // The cheap read, through the repository (never the multi-MB analysis). A read that finds no document (capture
    // still writing it, or gone) is not kept: the next ask reads again.
    m_summary     = m_doors.recordings ? m_doors.recordings->readSummary(m_ref, parseIfUnindexed) : ShotSummary{};
    m_summaryRead = m_summary.ok;
    return m_summary;
}

void Shot::keepInSummary(const ReviewEdit &edit)
{
    // The summary follows the saves (shot.h): the field the save wrote, as readSummary would now read it.
    if (edit.rating)       m_summary.rating         = std::clamp(*edit.rating, 0, 5);
    if (edit.note)         m_summary.note           = *edit.note;
    if (edit.club)         m_summary.club           = *edit.club;
    if (edit.excluded)     m_summary.lmExcluded     = *edit.excluded;
    if (edit.clubOverride) m_summary.lmClubOverride = *edit.clubOverride;
}

// ── The commands for its own facts (refactor step 3.4b-i) ────────────────────────────────────────────────
//
// Each: one save through the repository by ShotRef (the session's lock taken for that write: step 2.8b), then
// the event once, on this thread, when the save changed the fact (DEC-69, DEC-103). A failed save emits nothing;
// its WriteResult goes back to the caller and the log says so (DEC-126).

void Shot::emitEvent(Event event)
{
    switch (event) {
    case Event::None:             return;
    case Event::DeclaredClub:     emit declaredClubChanged(m_ref); return;   // EVT-21
    case Event::ClubCorrected:    emit clubCorrected(m_ref);       return;   // EVT-04
    case Event::ReadingExcluded:  emit readingExcluded(m_ref);     return;   // EVT-05
    case Event::LabelCorrected:   emit labelCorrected(m_ref);      return;   // EVT-06
    case Event::ShotIntent:       emit shotIntentChanged(m_ref);   return;   // EVT-07
    case Event::CaptureCompleted: emit captureCompleted(m_ref);    return;   // EVT-22
    case Event::Reanalysed:       emit reanalysed(m_ref);          return;   // EVT-02
    }
}

WriteResult Shot::finish(const WriteResult &result, Event event, const char *command)
{
    if (!result.ok()) {
        ppWarn() << "[Shot]" << command << "not saved for" << m_ref.toString() << "-" << result.error;
        return result;   // nothing changed: no event
    }
    // A late part or a re-analysis rewrote what the summary block holds: read it again when next asked.
    if (result.changed() && (event == Event::CaptureCompleted || event == Event::Reanalysed))
        m_summaryRead = false;
    if (result.changed())
        emitEvent(event);
    return result;
}

WriteResult Shot::saveReview(const ReviewEdit &edit, Event event, const char *command)
{
    const WriteResult w = m_doors.recordings ? m_doors.recordings->editReview(m_ref, edit) : noDoor();
    if (w.ok() && m_summaryRead)
        keepInSummary(edit);   // before the event, so a listener reads the saved value
    return finish(w, event, command);
}

void Shot::saveReviewLater(const ReviewEdit &edit, Event event, const char *command, const SaveLater &later,
                           const std::optional<LabelCorrectionNote> &note)
{
    if (!later.pool) {
        if (note)
            m_lastCorrection = *note;
        const WriteResult w = saveReview(edit, event, command);
        if (later.done)
            later.done(w);
        return;
    }
    // The rewrite on the caller's worker (today's thread for this edit: D25, DEC-183), the event and `done` back
    // on this thread once it has landed. The repository's editReview is safe from a worker (it resolves the
    // folder from the session repository's published folders). A Shot trashed meanwhile emits nothing; `done`
    // is still told.
    RecordingRepository *repo = m_doors.recordings;
    const ShotRef        ref  = m_ref;
    const QPointer<Shot> self(this);
    const auto           done = later.done;
    later.pool->start([repo, ref, edit, event, command, self, done, note]() {
        const WriteResult w = repo ? repo->editReview(ref, edit) : noDoor();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, w, edit, event, command, done, note, ref]() {
            if (self) {
                if (note && w.ok())
                    self->m_lastCorrection = *note;
                if (w.ok() && self->m_summaryRead)
                    self->keepInSummary(edit);
                self->finish(w, event, command);
            } else if (!w.ok()) {
                ppWarn() << "[Shot]" << command << "not saved for" << ref.toString() << "-" << w.error;
            }
            if (done)
                done(w);
        }, Qt::QueuedConnection);
    });
}

WriteResult Shot::rate(int stars)
{
    ReviewEdit e;
    e.rating = stars;
    return saveReview(e, Event::None, "rate");          // no event (DEC-70)
}

WriteResult Shot::note(const QString &text)
{
    ReviewEdit e;
    e.note = text;
    return saveReview(e, Event::None, "note");          // no event (DEC-70)
}

WriteResult Shot::setDeclaredClub(const QString &club)
{
    ReviewEdit e;
    e.club = club;
    return saveReview(e, Event::DeclaredClub, "setDeclaredClub");
}

WriteResult Shot::correctClub(const QString &club)
{
    ReviewEdit e;
    e.clubOverride = club;
    return saveReview(e, Event::ClubCorrected, "correctClub");
}

WriteResult Shot::excludeReading(bool excluded)
{
    ReviewEdit e;
    e.excluded = excluded;
    return saveReview(e, Event::ReadingExcluded, "excludeReading");
}

WriteResult Shot::setShotIntent(const QString &intent, bool keepEmpty)
{
    ReviewEdit e;
    e.shotIntent           = intent;
    e.keepEmptyGolferInput = keepEmpty;
    return saveReview(e, Event::ShotIntent, "setShotIntent");
}

void Shot::setShotIntent(const QString &intent, bool keepEmpty, const SaveLater &later)
{
    ReviewEdit e;
    e.shotIntent           = intent;
    e.keepEmptyGolferInput = keepEmpty;
    saveReviewLater(e, Event::ShotIntent, "setShotIntent", later, std::nullopt);
}

WriteResult Shot::correctLabel(const QVector<LabelCorrection> &corrections, const LabelCorrectionNote &note,
                               bool keepEmpty)
{
    ReviewEdit e;
    e.labelCorrections     = corrections;
    e.keepEmptyGolferInput = keepEmpty;
    const LabelCorrectionNote before = m_lastCorrection;
    m_lastCorrection = note;   // read by the Athlete while EVT-06 is emitted (DEC-43)
    const WriteResult w = saveReview(e, Event::LabelCorrected, "correctLabel");
    if (!w.ok())
        m_lastCorrection = before;
    return w;
}

void Shot::correctLabel(const QVector<LabelCorrection> &corrections, const LabelCorrectionNote &note,
                        bool keepEmpty, const SaveLater &later)
{
    if (!later.pool) {
        const WriteResult w = correctLabel(corrections, note, keepEmpty);
        if (later.done)
            later.done(w);
        return;
    }
    ReviewEdit e;
    e.labelCorrections     = corrections;
    e.keepEmptyGolferInput = keepEmpty;
    saveReviewLater(e, Event::LabelCorrected, "correctLabel", later, note);
}

WriteResult Shot::completeCapture(const lm::LaunchMonitorReading &reading, const QJsonObject &followedClub,
                                  bool *clubReplaced)
{
    if (clubReplaced)
        *clubReplaced = false;
    return finish(m_doors.recordings ? m_doors.recordings->completeCapture(m_ref, reading, followedClub, clubReplaced)
                                     : noDoor(),
                  Event::CaptureCompleted, "completeCapture");
}

WriteResult Shot::completeCapture(const StreamArrival &arrival)
{
    return finish(m_doors.recordings ? m_doors.recordings->completeCapture(m_ref, arrival) : noDoor(),
                  Event::CaptureCompleted, "completeCapture");
}

WriteResult Shot::setMarkupTruth(TruthView view, const QJsonObject &truth)
{
    return finish(m_doors.recordings ? m_doors.recordings->writeTruth(m_ref, view, truth) : noDoor(),
                  Event::None, "setMarkupTruth");   // no event (DEC-187: IM-S3-9)
}

QJsonObject Shot::markupTruth(TruthView view) const
{
    return m_doors.recordings ? m_doors.recordings->truth(m_ref, view) : QJsonObject();
}

WriteResult Shot::applyAnalysis(const AnalysisOutcome &outcome, const PersistOptions &options)
{
    return finish(m_doors.recordings ? m_doors.recordings->writeAnalysis(m_ref, outcome, options) : noDoor(),
                  Event::Reanalysed, "applyAnalysis");   // EVT-02 (DEC-103)
}

} // namespace pinpoint
