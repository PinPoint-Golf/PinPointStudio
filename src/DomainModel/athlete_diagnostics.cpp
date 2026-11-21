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

#include "athlete_diagnostics.h"
#include "session.h"
#include "session_diagnostics.h"

#include "../Coaching/shot_outcomes_pack.h"
#include "../Coaching/tendencies_pack.h"
#include "../Diagnostics/packs/swing_themes_pack.h"
#include "../Core/pp_debug.h"

#include <algorithm>

namespace pinpoint {

using analysis::SessionPlace;

AthleteDiagnostics *AthleteDiagnostics::of(Athlete *athlete)
{
    if (!athlete)
        return nullptr;
    if (auto *d = athlete->findChild<AthleteDiagnostics *>(QString(), Qt::FindDirectChildrenOnly))
        return d;
    return new AthleteDiagnostics(athlete);
}

AthleteDiagnostics::AthleteDiagnostics(Athlete *athlete)
    : QObject(athlete), m_athlete(athlete)
{
    const auto index = [this](const SessionId &) { markStale(); };
    connect(athlete, &Athlete::sessionStarted, this, index, Qt::DirectConnection);   // EVT-17
    connect(athlete, &Athlete::sessionResumed, this, index, Qt::DirectConnection);   // EVT-23
    connect(athlete, &Athlete::sessionTrashed, this, index, Qt::DirectConnection);   // EVT-20
    connect(athlete, &Athlete::recordChanged, this, [this] { markStale(); }, Qt::DirectConnection);   // EVT-14's interim
    connect(athlete, &Athlete::sessionHeld, this, &AthleteDiagnostics::hearSession, Qt::DirectConnection);
    for (Session *s : athlete->loadedSessions())
        hearSession(s);
}

SessionRepository *AthleteDiagnostics::repository() const
{
    return m_athlete ? m_athlete->m_sessions : nullptr;
}

void AthleteDiagnostics::hearSession(Session *session)
{
    if (!session || m_heard.contains(session->id()))
        return;
    m_heard.insert(session->id());
    const SessionId id = session->id();
    const auto again = [this, id] { markStale(id); };
    connect(session, &Session::sessionEnded, this, again, Qt::DirectConnection);           // EVT-09
    connect(session, &Session::shotTrashed, this, again, Qt::DirectConnection);            // EVT-03
    connect(session, &Session::clubBaselineRewritten, this, again, Qt::DirectConnection);  // EVT-16
    // The summary written (SessionDiagnostics, its one writer: DEC-55) is the session level's result landing.
    if (SessionDiagnostics *d = session->diagnostics()) {
        connect(d, &SessionDiagnostics::written, this, again, Qt::DirectConnection);
        if (d->counts().summaryWrites > 0)
            markStale(id);   // it wrote before this level heard it (a write while it loaded)
    }
    // A released Session leaves the map: a later one of that id is heard afresh.
    connect(session, &QObject::destroyed, this, [this, id] { m_heard.remove(id); });
}

void AthleteDiagnostics::markStale(const SessionId &session)
{
    const bool was = stale();
    if (session.isNull())
        m_indexStale = true;
    else
        m_dirty.insert(session);
    ++m_counts.staleMarks;
    if (!was)
        emit changed();
}

AthleteSessionSummary AthleteDiagnostics::readSummary(const SessionIndexEntry &e)
{
    AthleteSessionSummary out;
    out.id     = e.id();
    out.place  = SessionPlace{ e.identity.folderName(), e.identity.localDay(), e.identity.nOfDay() };
    out.status = e.status;
    SessionRepository *repo = repository();
    if (!repo)
        return out;
    const JsonFileRead f = repo->readWorkOns(e.id());
    ++m_counts.summaryReads;
    if (!f.exists || !f.ok)
        return out;
    bool ok = false;
    analysis::SessionResults r = analysis::sessionResultsFromJson(f.root, analysis::SessionResultsRead::Strict, &ok);
    if (!ok)
        return out;   // an older rule's summary: not this build's answer (the catch-up writes it again)
    out.read    = true;
    out.results = std::move(r);
    return out;
}

void AthleteDiagnostics::ensure()
{
    if (!stale())
        return;
    if (m_indexStale) {
        // The index again: keep what was read of a session still there and not dirty; read the rest.
        QHash<SessionId, AthleteSessionSummary> kept;
        for (AthleteSessionSummary &s : m_summaries)
            if (!m_dirty.contains(s.id)) kept.insert(s.id, std::move(s));
        m_summaries.clear();
        for (const SessionIndexEntry &e : m_athlete->index()) {
            if (e.id().isNull())
                continue;
            if (auto it = kept.find(e.id()); it != kept.end()) {
                it->status = e.status;
                m_summaries.push_back(std::move(*it));
            } else {
                m_summaries.push_back(readSummary(e));
            }
        }
    } else {
        for (AthleteSessionSummary &s : m_summaries)
            if (m_dirty.contains(s.id)) {
                const auto e = repository() ? repository()->open(s.id) : std::nullopt;
                if (e) s = readSummary(*e);
            }
    }
    m_indexStale = false;
    m_dirty.clear();
    std::stable_sort(m_summaries.begin(), m_summaries.end(), [](const AthleteSessionSummary &a, const AthleteSessionSummary &b) {
        return analysis::sessionPlaceBefore(a.place, b.place);
    });

    // The derived results, each a pure rule over what was read.
    std::vector<analysis::SessionWorkOns> sessions;
    QHash<QString, std::vector<analysis::ClubSessionPool>> pools;
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read)
            continue;
        sessions.push_back(s.results.workOns);
        if (!s.place.day.isValid())
            continue;   // nothing can say what comes before or after it (club_distances.h sessionsByDay)
        for (analysis::ClubSessionPool p : s.results.clubWindows) {
            p.folder = s.place.folder;
            p.day    = s.place.day;
            pools[p.club].push_back(std::move(p));
        }
    }
    m_tendenciesCurrent = false;   // computed again when next read
    m_conditions = analysis::aggregateConditions(sessions);
    m_workOns    = analysis::aggregateWorkOns(std::move(sessions));
    m_distances  = analysis::clubDistancesFromPools(pools);
    ++m_counts.recomputes;
}

const std::vector<AthleteSessionSummary> &AthleteDiagnostics::summaries()
{
    ensure();
    return m_summaries;
}

QHash<QString, analysis::SessionWorkOns> AthleteDiagnostics::records()
{
    ensure();
    QHash<QString, analysis::SessionWorkOns> out;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read) out.insert(s.results.workOns.sessionId, s.results.workOns);
    return out;
}

const std::vector<analysis::WorkOnSummary> &AthleteDiagnostics::workOns()
{
    ensure();
    return m_workOns;
}

const std::vector<analysis::WorkOnSummary> &AthleteDiagnostics::conditions()
{
    ensure();
    return m_conditions;
}

const std::vector<analysis::ClubDistanceItem> &AthleteDiagnostics::distances()
{
    ensure();
    return m_distances;
}

std::shared_ptr<const RuleDataSnapshot> AthleteDiagnostics::rules() const
{
    RuleData *rd = m_athlete && m_athlete->diagnosticsDoors().ruleData ? m_athlete->diagnosticsDoors().ruleData : RuleData::installed();
    return rd ? rd->snapshot() : nullptr;
}

// Each summary's compact tendency facts (DEC-192 as Mark amended it, "b"), placed. A summary without them (an older
// file) adds nothing.
std::vector<analysis::TendencySessionShots> AthleteDiagnostics::tendencySessions()
{
    ensure();
    std::vector<analysis::TendencySessionShots> out;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read && s.results.hasFacts)
            out.push_back(analysis::TendencySessionShots{ s.place, s.results.tendencyShots });
    return out;
}

const analysis::TendencySet &AthleteDiagnostics::tendencies()
{
    ensure();
    if (m_tendenciesCurrent)
        return m_tendencies;
    m_tendencies = {};
    const auto snap = rules();
    if (snap && snap->pack && snap->norms) {
        const analysis::ShotOutcomePack outcomes = analysis::shotOutcomePackFor(&snap->pack->pack());
        const analysis::ContextTree &tree = snap->norms->contexts();
        m_tendencies = analysis::reduceTendencyShots(tendencySessions(), outcomes,
                                                     [&tree](const QString &club) { return analysis::tendencyClubGroup(tree, club); });
        // Its distances: the summaries' club windows (D7), the same rows the ledger path pools (checked equal: run P).
        QJsonArray distances;
        for (const analysis::ClubDistanceItem &it : m_distances) distances.append(analysis::clubDistanceItemToJson(it));
        m_tendencies.clubDistances = distances;
    }
    m_tendenciesCurrent = true;
    return m_tendencies;
}

std::vector<analysis::ThemeSessionInput> AthleteDiagnostics::themeInputs()
{
    ensure();
    std::vector<analysis::ThemeSessionInput> out;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read && s.results.hasFacts) {
            analysis::ThemeSessionInput in;
            in.name  = s.place.folder;
            in.facts = s.results.themeShots;
            out.push_back(std::move(in));
        }
    std::stable_sort(out.begin(), out.end(), [](const analysis::ThemeSessionInput &a, const analysis::ThemeSessionInput &b) {
        return a.name < b.name;   // the reduction takes sessions in name order (swing_themes.h)
    });
    return out;
}

analysis::SwingThemes AthleteDiagnostics::themes(const analysis::ThemeParallelFor &parallel, const std::atomic<bool> *cancel)
{
    const auto snap = rules();
    if (!snap || !snap->pack)
        return {};
    const analysis::CharacteristicPack &pack = snap->pack->pack();
    const analysis::DrillSet drills = snap->drills ? snap->drills->merged : analysis::DrillSet{};
    return analysis::reduceSwingThemes(themeInputs(), analysis::themeMeasureInfo(pack), analysis::themeConditionInfo(pack, drills),
                                       analysis::ThemeOptions{}, parallel, cancel);
}

ClubBaselineComputation AthleteDiagnostics::clubBaselineFor(const SessionId &session)
{
    ClubBaselineComputation out;
    SessionRepository *repo = repository();
    const auto me = repo ? repo->open(session) : std::nullopt;
    if (!me) {
        out.why = QStringLiteral("the session is not this athlete's");
        return out;
    }
    out.day = me->identity.localDay();
    if (!out.day.isValid()) {
        // A session with no day has no predecessors: a snapshot now would be empty for good (the old model's rule).
        out.why = QStringLiteral("the session has no day");
        return out;
    }
    // Strictly earlier days, newest first by day and place in the day (SessionIdentity's), never a folder's name.
    std::vector<SessionIndexEntry> earlier;
    for (const SessionIndexEntry &e : m_athlete->index())
        if (e.id() != session && e.identity.localDay().isValid() && e.identity.localDay() < out.day)
            earlier.push_back(e);
    std::sort(earlier.begin(), earlier.end(), [](const SessionIndexEntry &a, const SessionIndexEntry &b) {
        return analysis::sessionPlaceBefore(
            SessionPlace{ b.identity.folderName(), b.identity.localDay(), b.identity.nOfDay() },
            SessionPlace{ a.identity.folderName(), a.identity.localDay(), a.identity.nOfDay() });
    });

    std::vector<analysis::SummaryClubWindows> summaries;
    QStringList notCurrent;
    int taken = 0;
    for (const SessionIndexEntry &e : earlier) {
        if (taken >= analysis::kClubDistanceWindowSessions)
            break;
        const bool hasSwings = !repo->shots(e.id()).isEmpty();
        const AthleteSessionSummary s = readSummary(e);
        if (!s.read || !s.results.hasGrowth) {
            // A session played and not yet summed up by this build (no summary, an older rule's, or one without its
            // windows): the baseline is not computable until it is (the catch-up writes it first).
            if (hasSwings) {
                ++taken;
                notCurrent << s.place.folder;
                out.waitingOn << e.id();
            }
            continue;
        }
        ++taken;
        out.read << s.place.folder;
        summaries.push_back(analysis::SummaryClubWindows{ s.place, s.results.clubWindows });
    }
    if (!notCurrent.isEmpty()) {
        out.why = QStringLiteral("earlier sessions not summed up yet: ") + notCurrent.join(QStringLiteral(", "));
        return out;
    }
    out.clubs      = analysis::clubBaselineFromSummaries(summaries, out.day);
    out.computable = true;
    return out;
}

WriteResult AthleteDiagnostics::persist()
{
    // FILE-ATH-02 and FILE-ATH-03 are written by WorkOnsController in the app until the catch-up's cut-over (3.7): writing
    // them here too would give each file two writers. Nothing of its own to write yet.
    WriteResult w;
    w.status = WriteResult::Status::Unchanged;
    return w;
}

} // namespace pinpoint
