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

#include "session_series.h"
#include "session_series_extract.h"

#include "../../Coaching/board_annotations.h"
#include "../../Coaching/handedness_gloss.h"   // golferHand: the hand a shot is named in (refactor 6.3b fix round, B-3)
#include "../../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S)
#include "../../Core/pp_debug.h"
#include "../../DomainModel/rule_data.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMetaMethod>

#include <algorithm>
#include <utility>

namespace pinpoint::charts {

namespace pa = pinpoint::analysis;

// One shot as it was read: its document's stamp, its series (without findings) or why it has none, its board's
// annotations, and the shot as last published (with its place and findings) — kept while its series are the same.
struct SessionSeries::Entry {
    pinpoint::DocumentStamp           stamp;
    bool                              read = false;
    std::shared_ptr<const ShotSeries> base;        // as extracted, the board applied; no ordinal or findings
    QString                           skipReason;
    std::shared_ptr<const ShotSeries> published;   // base + ordinal, shotId, findings; null until published
};

namespace {

// ── The job: values only (charts_work.h) ─────────────────────────────────────────────────────────────────────────
struct JobShot {
    pinpoint::ShotRef ref;
    int               swing = 0;
    QString           folder;
    bool              force = false;
};
struct JobIn {
    std::shared_ptr<const pinpoint::SeriesReader>        reader;
    std::shared_ptr<const pinpoint::RuleDataSnapshot>     rules;
    QString                                               sessionDir;
    std::vector<JobShot>                                  shots;   // by swing number: strike order
    QHash<int, std::shared_ptr<const SessionSeries::Entry>> entries;
    std::shared_ptr<const pa::SessionReductionInput>      rows;    // the owner's, or null
    QHash<QString, QString>                               tiers;
    QStringList                                           workOnOrder;
    QString                                               pendingWhy;
    QString                                               athleteHandedness;   // the record's: golferHand's fallback (B-3)
    bool                                                  rulesMoved = false;
    std::shared_ptr<const SessionSnapshot>                last;
    std::vector<SeriesSkip>                               lastSkipped;
};
struct JobOut {
    QHash<int, std::shared_ptr<const SessionSeries::Entry>> entries;
    std::shared_ptr<SessionSnapshot>                         snapshot;   // null: nothing moved
    std::vector<SeriesSkip>                                  skipped;
    SessionSeries::LoadStats                                 stats;
    qint64                                                   wallMs = 0;
};

// THE SNAPSHOT'S READ DOOR (refactor 5.5c; SnapshotShotReader, session_series_types.h): the session's own shots by
// folder, each read through the series' SeriesReader by its ShotRef. A folder that is not one of them reads nothing.
class SeriesShotDoor final : public SnapshotShotReader
{
public:
    SeriesShotDoor(std::shared_ptr<const pinpoint::SeriesReader> reader, QHash<QString, pinpoint::ShotRef> refs)
        : m_reader(std::move(reader)), m_refs(std::move(refs)) {}

    SnapshotShotRead stamp(const QString &swingDir) const override
    {
        SnapshotShotRead out;
        const pinpoint::ShotRef *ref = refOf(swingDir, &out);
        if (!ref) return out;
        const pinpoint::DocumentStamp st = m_reader->stamp(*ref);
        out.exists = st.exists; out.size = st.size; out.mtimeMs = st.mtimeMs;
        return out;
    }
    SnapshotShotRead analysis(const QString &swingDir) const override
    {
        SnapshotShotRead out;
        const pinpoint::ShotRef *ref = refOf(swingDir, &out);
        return ref ? of(m_reader->analysis(*ref)) : out;
    }
    SnapshotShotRead document(const QString &swingDir) const override
    {
        SnapshotShotRead out;
        const pinpoint::ShotRef *ref = refOf(swingDir, &out);
        return ref ? of(m_reader->document(*ref)) : out;
    }

private:
    const pinpoint::ShotRef *refOf(const QString &swingDir, SnapshotShotRead *out) const
    {
        const auto it = m_refs.constFind(QDir::cleanPath(swingDir.trimmed()));
        if (it == m_refs.constEnd() || !m_reader) {
            out->error = QStringLiteral("not one of this session's shots");
            return nullptr;
        }
        return &it.value();
    }
    static SnapshotShotRead of(pinpoint::SeriesRead &&r)
    {
        SnapshotShotRead out;
        out.exists = r.stamp.exists; out.size = r.stamp.size; out.mtimeMs = r.stamp.mtimeMs;
        out.root = std::move(r.root);
        out.error = std::move(r.error);
        return out;
    }

    std::shared_ptr<const pinpoint::SeriesReader> m_reader;
    QHash<QString, pinpoint::ShotRef>             m_refs;   // by cleaned folder
};

bool sameSkips(const std::vector<SeriesSkip> &a, const std::vector<SeriesSkip> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].swingDir != b[i].swingDir || a[i].shotId != b[i].shotId || a[i].reason != b[i].reason) return false;
    return true;
}

JobOut runSeriesJob(const JobIn &in)
{
    QElapsedTimer wall;
    wall.start();
    JobOut out;
    const pa::CharacteristicPack *pack = (in.rules && in.rules->pack) ? &in.rules->pack->pack() : nullptr;

    // The owner's rows, by swing number (a ShotRecord's shotId IS the swing number: ID-SWING-NUMBER).
    QHash<int, const pa::ShotRecord *> rows;
    if (in.rows)
        for (const pa::ShotRecord &r : in.rows->shots) rows.insert(r.shotId, &r);

    auto snap = std::make_shared<SessionSnapshot>();
    snap->sessionDir      = in.sessionDir;
    snap->tiers           = in.tiers;
    snap->workOnOrder     = in.workOnOrder;
    snap->wristPendingWhy = in.pendingWhy;
    snap->shots.reserve(in.shots.size());

    for (const JobShot &s : in.shots) {
        std::shared_ptr<const SessionSeries::Entry> e = in.entries.value(s.swing);
        bool fresh = false;
        if (!e || !e->read || s.force || in.reader == nullptr || in.reader->stamp(s.ref) != e->stamp) {
            // READ (decision 3): its stamp taken before the read, by the reader.
            auto ne = std::make_shared<SessionSeries::Entry>();
            ne->read = true;
            if (!in.reader) {
                ne->skipReason = QStringLiteral("the swing could not be read");
            } else {
                QElapsedTimer t;
                t.start();
                pinpoint::SeriesRead r = in.reader->analysis(s.ref);
                ++out.stats.docReads;
                out.stats.loadNs += t.nsecsElapsed();
                t.restart();
                ne->stamp = r.stamp;
                if (!r.stamp.exists) {
                    ne->skipReason = QStringLiteral("no swing document");
                } else if (r.root.isEmpty()) {
                    ne->skipReason = QStringLiteral("the swing document could not be read");
                    ppWarn() << "[Charts]" << r.error;
                } else {
                    QString why;
                    std::shared_ptr<ShotSeries> shot = extractShot(r.root, s.folder, &why);
                    if (shot) {
                        seriesAnnotateForBoard(*shot, pa::boardAnnotationOfDocument(r.root));
                        ne->base = std::move(shot);
                    } else {
                        ne->skipReason = why;
                    }
                }
                r.root = QJsonObject();   // the document goes NOW (decision 2)
                out.stats.extractNs += t.nsecsElapsed();
            }
            e = ne;
            fresh = true;
        } else {
            ++out.stats.cacheHits;
        }
        if (!e->base) {
            out.skipped.push_back(SeriesSkip{ s.folder, s.swing, e->skipReason });
            out.entries.insert(s.swing, e);
            continue;
        }
        const int ordinal = snap->count() + 1;
        const auto row = rows.constFind(s.swing);
        const ChartShotFindings diag = row != rows.constEnd() ? seriesDiagnosticsOf(**row, pack) : ChartShotFindings();
        std::shared_ptr<const ShotSeries> pub = e->published;
        // The hand its sides are named in: the swing's own, else the athlete's (golferHand; refactor 6.3b fix round, B-3).
        const QString hand = pa::golferHand(e->base->handedness, in.athleteHandedness);
        if (fresh || !pub || pub->ordinal != ordinal || pub->shotId != s.swing
            || !seriesSameDiagnostics(pub->diagnostics, diag) || pub->handedness != hand) {
            // A PUBLISHED SHOT NEVER CHANGES (decision 4): a new one, its series copied once per change.
            auto copy = std::make_shared<ShotSeries>(*e->base);
            copy->ordinal     = ordinal;
            copy->shotId      = s.swing;
            copy->diagnostics = diag;
            copy->handedness  = hand;
            auto ne = std::make_shared<SessionSeries::Entry>(*e);
            ne->published = copy;
            pub = copy;
            e = ne;
        }
        out.entries.insert(s.swing, e);
        snap->shots.push_back(pub);
    }
    seriesLinkConditions(*snap, pack);
    {
        QHash<QString, pinpoint::ShotRef> refs;
        for (const JobShot &s : in.shots) refs.insert(s.folder, s.ref);
        snap->reader = std::make_shared<SeriesShotDoor>(in.reader, std::move(refs));
    }

    const bool same = in.last && in.last->sessionDir == snap->sessionDir && in.last->shots == snap->shots
                   && in.last->tiers == snap->tiers && in.last->workOnOrder == snap->workOnOrder
                   && in.last->wristPendingWhy == snap->wristPendingWhy && !in.rulesMoved && sameSkips(in.lastSkipped, out.skipped);
    if (!same) out.snapshot = std::move(snap);
    out.wallMs = wall.elapsed();
    return out;
}

// THE MEMORY BOUND (decision 6): the series read most recently first.
QList<QPointer<SessionSeries>> &recentlyRead()
{
    static QList<QPointer<SessionSeries>> l;
    return l;
}

} // namespace

// ── The object ────────────────────────────────────────────────────────────────────────────────────────────────────

SessionSeries *SessionSeries::attach(pinpoint::Session *session, const SeriesDoors &doors)
{
    if (!session) return nullptr;
    if (SessionSeries *s = of(session)) return s;
    return new SessionSeries(session, doors);
}

SessionSeries *SessionSeries::of(const QObject *session)
{
    return session ? session->findChild<SessionSeries *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

SessionSeries::SessionSeries(pinpoint::Session *session, const SeriesDoors &doors)
    : SeriesSource(session), m_session(session), m_doors(doors), m_snapshot(std::make_shared<SessionSnapshot>())
{
    setObjectName(QStringLiteral("sessionSeries"));
    if (const auto e = session->entry()) m_sessionDir = QDir::cleanPath(e->folder);
    auto snap = std::make_shared<SessionSnapshot>();
    snap->sessionDir = m_sessionDir;
    m_snapshot = snap;
    // What it hears (the header): connected at once, acted on only once it has been read.
    connect(session, &pinpoint::Session::shotHeld, this, [this](const pinpoint::ShotRef &ref) {
        if (m_session) hearShot(m_session->shot(ref));
    });
    connect(session, &pinpoint::Session::shotAdded, this, [this](const pinpoint::ShotRef &ref) {   // EVT-01
        if (m_session) hearShot(m_session->shot(ref));
        if (m_active) schedule();   // a new swing has no entry: the next job reads it
    });
    connect(session, &pinpoint::Session::shotTrashed, this, [this](const pinpoint::ShotRef &ref) { forget(ref.swingNumber()); });   // EVT-03
    pinpoint::RuleData *rd = m_doors.ruleData ? m_doors.ruleData : pinpoint::RuleData::installed();
    if (rd)
        connect(rd, &pinpoint::RuleData::packChanged, this, [this] {   // EVT-13
            m_rulesMoved = true;
            if (m_active) schedule();
        });
}

SessionSeries::~SessionSeries() = default;   // the lane is cancelled; a running job finishes into nothing

pinpoint::Session *SessionSeries::session() const { return m_session.data(); }

void SessionSeries::hearShot(pinpoint::Shot *shot)
{
    if (!shot || m_heard.contains(shot->ref().swingNumber())) return;
    m_heard.insert(shot->ref().swingNumber());
    // A re-analysis is read whatever its stamp says (the old regradeShot); the others rewrite the document — the board's
    // club or a reading left out, the declared club, a late part — so its stamp moves and the next job reads it.
    connect(shot, &pinpoint::Shot::reanalysed, this, [this](const pinpoint::ShotRef &ref) { reread(ref.swingNumber()); });   // EVT-02
    const auto look = [this](const pinpoint::ShotRef &) { if (m_active) schedule(); };
    connect(shot, &pinpoint::Shot::clubCorrected, this, look);         // EVT-04 (J-74 Q3)
    connect(shot, &pinpoint::Shot::readingExcluded, this, look);       // EVT-05
    connect(shot, &pinpoint::Shot::declaredClubChanged, this, look);   // EVT-21 (J-74 Q3)
    connect(shot, &pinpoint::Shot::captureCompleted, this, look);      // EVT-22
}

void SessionSeries::connectDiagnostics()
{
    if (m_diagConnected || !m_session) return;
    if (pinpoint::SessionDiagnostics *d = m_session->diagnostics()) {
        m_diagConnected = true;
        connect(d, &pinpoint::SessionDiagnostics::diagnosisApplied, this, [this] { if (m_active) schedule(); });
    }
}

std::shared_ptr<const SessionSnapshot> SessionSeries::snapshot() const
{
    if (!m_active) const_cast<SessionSeries *>(this)->activate();
    return m_snapshot;
}

void SessionSeries::activate()
{
    if (m_active || !m_session) return;
    m_active = true;
    connectDiagnostics();
    if (m_session->inProgress()) m_lane.setForeground(true);
    // THE MEMORY BOUND (decision 6): a third series read empties the oldest one no view is listening to.
    QList<QPointer<SessionSeries>> &recent = recentlyRead();
    recent.removeAll(QPointer<SessionSeries>(this));
    recent.removeAll(QPointer<SessionSeries>());
    recent.prepend(this);
    for (int i = int(recent.size()) - 1, kept = int(recent.size()); i >= 2 && kept > 2; --i) {
        SessionSeries *old = recent.at(i);
        if (old && !old->isSignalConnected(QMetaMethod::fromSignal(&SeriesSource::snapshotChanged)) && !old->m_running) {
            old->deactivate();
            recent.removeAt(i);
            --kept;
        }
    }
    schedule();
}

void SessionSeries::deactivate()
{
    m_active = false;
    m_entries.clear();
    m_force.clear();
    auto snap = std::make_shared<SessionSnapshot>();
    snap->sessionDir = m_sessionDir;
    m_snapshot = snap;
    m_skipped.clear();
}

void SessionSeries::reread(int swing)
{
    if (!m_active) return;
    m_force.insert(swing);
    schedule();
}

void SessionSeries::forget(int swing)
{
    m_heard.remove(swing);
    m_force.remove(swing);
    m_entries.remove(swing);   // a later swing of the same number is a different swing: never served this one's series
    if (m_active) schedule();
}

void SessionSeries::schedule()
{
    m_dirty = true;
    if (m_running) return;   // the job in flight runs once more when it lands
    start();
}

void SessionSeries::start()
{
    if (!m_session) return;
    const bool wasBusy = busy();
    m_dirty = false;
    JobIn in;
    in.reader     = m_doors.reader;
    pinpoint::RuleData *rd = m_doors.ruleData ? m_doors.ruleData : nullptr;
    in.rules      = rd ? rd->snapshot() : pinpoint::RuleData::current();
    in.sessionDir = m_sessionDir;
    in.athleteHandedness = m_session->athlete() ? m_session->athlete()->record().handedness : QString();
    for (pinpoint::Shot *shot : m_session->shots()) {
        if (!shot) continue;
        hearShot(shot);
        const int n = shot->ref().swingNumber();
        in.shots.push_back(JobShot{ shot->ref(), n, QDir::cleanPath(shot->folder()), m_force.contains(n) });
    }
    std::sort(in.shots.begin(), in.shots.end(), [](const JobShot &a, const JobShot &b) { return a.swing < b.swing; });
    m_force.clear();
    in.entries = m_entries;
    // THE FINDINGS ARE THE OWNER'S (decision 5).
    const pinpoint::SessionDiagnostics *d = m_session->diagnostics();
    const pa::SessionDiagnosis *dx = d ? d->diagnosis() : nullptr;
    if (d && dx) {
        in.rows = d->diagnosisInput();
        for (const pa::ConditionLedger &l : dx->ledgers)
            in.tiers.insert(l.id, l.tier == pa::Tier::Pattern ? QStringLiteral("pattern")
                                  : l.tier == pa::Tier::Watching ? QStringLiteral("watching") : QStringLiteral("clean"));
        in.workOnOrder = dx->workOnOrder;
    }
    using FS = pinpoint::SessionDiagnostics::FileState;
    const FS fs = d ? d->fileState() : FS::Missing;
    if (fs == FS::Newer)
        in.pendingWhy = QStringLiteral("This session's diagnostics were saved by a newer version of PinPoint Studio, which this version "
                                       "can't add to, so its wrist checkpoints aren't graded here.");
    else if (fs == FS::Unopenable || fs == FS::Unparseable)
        in.pendingWhy = QStringLiteral("This session's diagnostics can't be read just now, so its wrist checkpoints aren't graded.");
    else if (dx)
        in.pendingWhy = QStringLiteral("Diagnostics have not graded the wrist checkpoints yet.");
    else
        in.pendingWhy = QStringLiteral("This session's diagnostics haven't been worked out yet, so its wrist checkpoints aren't graded.");
    in.rulesMoved = std::exchange(m_rulesMoved, false);
    in.last        = m_snapshot;
    in.lastSkipped = m_skipped;

    const auto apply = [this](JobOut &&out) {
        m_running = false;
        m_stats.docReads  += out.stats.docReads;
        m_stats.cacheHits += out.stats.cacheHits;
        m_stats.loadNs    += out.stats.loadNs;
        m_stats.extractNs += out.stats.extractNs;
        if (m_active) {
            m_entries = std::move(out.entries);
            if (out.snapshot) {
                out.snapshot->revision = ++m_revision;
                m_snapshot = std::move(out.snapshot);
                m_skipped  = std::move(out.skipped);
                pinpoint::PostShotTrace::Scope trace("charts");   // [PostShot] (3.7-S): the presenters and QML redraw
                emit snapshotChanged();
            }
            if (out.stats.docReads > 0)
                ppInfo() << qUtf8Printable(QStringLiteral("[Charts] session %1: %2 shots, %3 skipped; %4 read, %5 cached; "
                                                          "%6 ms (load %7 ms, extract %8 ms)")
                                               .arg(m_sessionDir.section(QLatin1Char('/'), -1))
                                               .arg(m_snapshot->count()).arg(m_skipped.size())
                                               .arg(out.stats.docReads).arg(out.stats.cacheHits).arg(out.wallMs)
                                               .arg(out.stats.loadNs / 1000000).arg(out.stats.extractNs / 1000000));
        }
        if (m_dirty && m_active) start();
        else emit busyChanged();
    };
    m_running = true;
    if (!wasBusy) emit busyChanged();
    if (m_doors.synchronous) {
        apply(runSeriesJob(in));
        return;
    }
    m_lane.ask([in = std::move(in)]() { return std::any(std::make_shared<JobOut>(runSeriesJob(in))); },
               [apply](std::any &&v) { apply(std::move(*std::any_cast<std::shared_ptr<JobOut>>(v))); });
}

QString SessionSeries::skipReasonOf(const QString &swingDir) const
{
    const QString dir = QDir::cleanPath(swingDir.trimmed());
    for (const SeriesSkip &k : m_skipped)
        if (QDir::cleanPath(k.swingDir) == dir) return k.reason.isEmpty() ? QStringLiteral("the swing could not be read") : k.reason;
    return {};
}

bool SessionSeries::waitForIdle(int msTimeout)
{
    if (!m_active) (void)snapshot();
    QElapsedTimer t;
    t.start();
    while ((m_running || m_dirty) && (msTimeout < 0 || t.elapsed() < msTimeout))
        m_lane.waitForDone(20);
    return !m_running && !m_dirty;
}

} // namespace pinpoint::charts
