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

#include "app.h"
#include "../DomainModel/session.h"
#include "../DomainModel/shot.h"

namespace pinpoint {

App::App(std::function<Athlete *()> currentAthlete, QObject *parent)
    : QObject(parent), m_currentAthlete(std::move(currentAthlete))
{
    connect(&m_state, &AppState::currentAthleteChanged, this, &App::athleteChanged);
    connect(&m_state, &AppState::sessionOnScreenChanged, this, &App::sessionChanged);
}

Session *App::sessionAt(const QString &folder) const
{
    Athlete *athlete = m_currentAthlete ? m_currentAthlete() : nullptr;
    return athlete ? athlete->sessionAt(folder) : nullptr;
}

// ── ShotCommandDoor (refactor step 3.4b-i) ──────────────────────────────────────────────────────────────────
//
// The folder translated once, here, through the current Athlete (sessionAt; DEC-90); the command run on the
// Session's own Shot (P10). Not reached (N1): the caller keeps today's path.

Shot *App::shotAt(const QString &sessionFolder, const ShotRef &shot) const
{
    if (shot.isNull())
        return nullptr;
    Session *s = sessionAt(sessionFolder);
    return (s && s->id() == shot.session()) ? s->shot(shot) : nullptr;
}

namespace {
template <typename Command>
ForwardResult forward(Shot *shot, Command &&command)
{
    if (!shot)
        return {};
    return { true, command(*shot) };
}
} // namespace

ForwardResult App::rate(const QString &sessionFolder, const ShotRef &shot, int stars)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.rate(stars); });
}

ForwardResult App::note(const QString &sessionFolder, const ShotRef &shot, const QString &text)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.note(text); });
}

ForwardResult App::setDeclaredClub(const QString &sessionFolder, const ShotRef &shot, const QString &club)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.setDeclaredClub(club); });
}

ForwardResult App::correctClub(const QString &sessionFolder, const ShotRef &shot, const QString &club)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.correctClub(club); });
}

ForwardResult App::excludeReading(const QString &sessionFolder, const ShotRef &shot, bool excluded)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.excludeReading(excluded); });
}

ForwardResult App::completeCapture(const QString &sessionFolder, const ShotRef &shot,
                                   const lm::LaunchMonitorReading &reading, const QJsonObject &followedClub,
                                   bool *clubReplaced)
{
    return forward(shotAt(sessionFolder, shot),
                   [&](Shot &s) { return s.completeCapture(reading, followedClub, clubReplaced); });
}

ForwardResult App::completeCapture(const QString &sessionFolder, const ShotRef &shot, const StreamArrival &arrival)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.completeCapture(arrival); });
}

ForwardResult App::setMarkupTruth(const QString &sessionFolder, const ShotRef &shot, TruthView view,
                                  const QJsonObject &truth)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.setMarkupTruth(view, truth); });
}

bool App::setShotIntent(const QString &sessionFolder, const ShotRef &shot, const QString &intent, bool keepEmpty,
                        const SaveLater &later)
{
    Shot *s = shotAt(sessionFolder, shot);
    if (!s)
        return false;
    s->setShotIntent(intent, keepEmpty, later);
    return true;
}

bool App::correctLabel(const QString &sessionFolder, const ShotRef &shot, const QVector<LabelCorrection> &corrections,
                       const LabelCorrectionNote &note, bool keepEmpty, const SaveLater &later)
{
    Shot *s = shotAt(sessionFolder, shot);
    if (!s)
        return false;
    s->correctLabel(corrections, note, keepEmpty, later);
    return true;
}

ForwardResult App::flagForTuning(const QString &sessionFolder, const ShotRef &shot, const TuningFlagNote &note)
{
    Session *s = sessionAt(sessionFolder);
    if (!s || !s->athlete() || !shotAt(sessionFolder, shot))
        return {};
    return { true, s->athlete()->flagForTuning(shot, note) };
}

ForwardResult App::setSessionIntent(const QString &sessionFolder, const SessionIntentEdit &edit,
                                    const SessionInput &held)
{
    Session *s = sessionAt(sessionFolder);
    if (!s)
        return {};
    return { true, s->setSessionIntent(edit, held) };
}

ForwardResult App::setIntentRun(const QString &sessionFolder, const QVector<IntentRun> &runs, const SessionInput &held)
{
    Session *s = sessionAt(sessionFolder);
    if (!s)
        return {};
    return { true, s->setIntentRun(runs, held) };
}

// ── The rows and the readers (refactor step 3.4b-ii) ─────────────────────────────────────────────────────────

std::optional<ShotSummary> App::shotSummary(const QString &sessionFolder, const ShotRef &shot, SummaryRead how)
{
    Shot *s = shotAt(sessionFolder, shot);
    if (!s)
        return std::nullopt;
    switch (how) {
    case SummaryRead::KeptOnly:   return s->summaryKept() ? s->summary() : ShotSummary{};
    case SummaryRead::Kept:       return s->summary();
    case SummaryRead::Again:      return s->rereadSummary(/*parseIfUnindexed=*/true);
    case SummaryRead::AgainCheap: return s->rereadSummary(/*parseIfUnindexed=*/false);
    }
    return std::nullopt;
}

std::optional<QVector<HeldShot>> App::sessionShots(const QString &sessionFolder)
{
    Session *session = sessionAt(sessionFolder);
    if (!session)
        return std::nullopt;
    QVector<HeldShot> out;
    for (Shot *s : session->shots()) {   // by swing number
        const ShotSummary &sum = s->rereadSummary();
        if (sum.ok)
            out.append(HeldShot{ s->ref(), s->folder(), sum });
    }
    return out;
}

ForwardResult App::trashShot(const QString &sessionFolder, const ShotRef &shot, QString *where)
{
    Session *s = sessionAt(sessionFolder);
    if (!s || !shotAt(sessionFolder, shot))
        return {};
    return { true, s->trashShot(shot, where) };
}

std::optional<RecordingSource> App::recordingSourceOf(const QString &sessionFolder, const ShotRef &shot)
{
    Shot *s = shotAt(sessionFolder, shot);
    if (!s)
        return std::nullopt;
    return s->recordingSource();
}

ForwardResult App::applyAnalysis(const QString &sessionFolder, const ShotRef &shot, const AnalysisOutcome &outcome,
                                 const PersistOptions &options)
{
    return forward(shotAt(sessionFolder, shot), [&](Shot &s) { return s.applyAnalysis(outcome, options); });
}

} // namespace pinpoint
