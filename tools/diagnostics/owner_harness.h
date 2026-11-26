// owner_harness.h — A SESSION'S DIAGNOSIS FROM ITS OWNER, HEADLESS, FOR THE DIAGNOSTICS TOOLS (refactor step 3.7-d;
// build/refactor/step-3/BRIEF.md §4.9's cut-over order, "the report and the three tools").
//
// diag_rank_diff and diag_uncertainty_report read a session's diagnosis from the session's ONE owner, as the app does
// since 3.7-a, instead of making a SessionDiagnosticsModel of their own: an Athlete over the session's athlete folder,
// its Session (Athlete::sessionAt), that Session's SessionDiagnostics run synchronously (§4.4 step 11, as
// regrade_ledger runs it, by default since refactor 5.3), then the Session's thin adapter for the "tool" surface
// (SessionDiagnosticsModel::adapterFor) for the view half a tool reads — the explanation's roots, the links, the rows as
// the reduction read them. regrade_ledger reads each session's athlete from the library's athlete.json, else from
// athletes.json beside the session (the corpus; 5.3), --athletes-ini only as the last fallback, and refuses a session
// none names (3.10 review B1; refactor 4-c, J-61); its `--engine old` went in 5.4b.
//
// NOTHING IS WRITTEN: the owner runs in memory (DiagnosticsDoors::persist off — no diagnostics.json, no work_ons.json,
// no club_baseline.json; the firing reader is the parallel run's, which parses a swing whose sidecar is missing or stale
// and writes no file). So a tool may point it at the library's own folders without touching them.
//
// The athlete: the session file's own athlete id, so the Athlete holds the session (Athlete::hold refuses another
// athlete's), else — a folder with no session file, read under the legacy rule — an id of the athlete folder's name. No
// athlete record: no norm cohort (DEC-171), which resolves exactly as before cohorts existed.

#pragma once

#include "Core/work_pool.h"
#include "DomainModel/athlete.h"
#include "DomainModel/rule_data.h"
#include "DomainModel/session.h"
#include "DomainModel/session_diagnostics.h"
#include "Stores/recording_loader.h"        // SwingStoreRecordingReader
#include "Stores/swing_store_repository.h"
#include "Stores/session_store.h"
#include "Gui/diagnostics/diag_parallel_run.h"
#include "Gui/diagnostics/session_diagnostics_adapter.h"
#include "Gui/diagnostics/session_diagnostics_model.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#include <map>
#include <memory>
#include <optional>

namespace pinpoint::tools {

struct OwnerHarnessOptions {
    // The gate report's measurement-uncertainty switches (DiagnosticsDoors::withSigma, ::softTier); unset = tuned.
    std::optional<bool> withSigma;
    std::optional<bool> softTier;
    // Every shot fired again, the stage replayed from Cold (DiagnosticsDoors::rebuild): the session's kept findings
    // and its ledger options are not read. Off: what diagnostics.json holds is read first (its rows kept while
    // their stamps are current, its ledger options used), as when the app loads the session.
    bool rebuild = false;
};

class OwnerHarness
{
public:
    using Options = OwnerHarnessOptions;

    explicit OwnerHarness(Options o = {}) : m_options(o)
    {
        m_store.setOsTrashForTest(false);   // nothing here trashes; and never into the real Trash
        m_recordings = std::make_unique<SwingStoreRecordingRepository>(m_store);
        m_receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            const SessionId id = SessionId::fromString(a.owner);
            for (auto &it : m_athletes)
                if (Session *s = it.second->loadedSession(id)) return diagnosticsWorkTarget(s, a);
            return diagnosticsWorkTarget(nullptr, a);
        });
    }
    ~OwnerHarness()
    {
        m_athletes.clear();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // The Session of `sessionDir`, its owner settled (every ask run, the diagnosis applied). Null when no Athlete can
    // hold it. RuleData::installed()'s snapshot as it is now: a tool that publishes another between sessions opens the
    // next under it.
    Session *open(const QString &sessionDir)
    {
        const QString dir    = QDir::cleanPath(QFileInfo(sessionDir).absoluteFilePath());
        const QString folder = QFileInfo(dir).absolutePath();
        // The athlete the session file names, read through the store (a folder with none: the legacy rule's, below).
        AthleteId id;
        if (const SessionId sid = m_store.openFolder(dir, AthleteId()); !sid.isNull())
            if (const auto e = m_store.open(sid)) id = e->athlete;
        if (id.isNull())
            id = AthleteId::fromString(QStringLiteral("00000000-0000-4000-8000-%1")
                                           .arg(QString::fromLatin1(QCryptographicHash::hash(
                                                    QFileInfo(folder).fileName().toUtf8(), QCryptographicHash::Sha1).toHex().left(12))));
        const QString key = id.toString() + QLatin1Char('|') + folder;
        auto it = m_athletes.find(key);
        if (it == m_athletes.end()) {
            auto a = std::make_unique<Athlete>(nullptr, &m_store, id);
            AthleteRecord r;
            r.name = QFileInfo(folder).fileName();
            a->setStored(r, folder, SessionOwner{ id, r.name, QFileInfo(folder).absolutePath() }, Athlete::Source::Library, false);
            a->setShotDoors({ m_recordings.get(), std::make_shared<SwingStoreRecordingReader>(m_store) });
            // THE PARALLEL RUN'S DOORS, the one place in-memory doors are put together (review 3.7-d M2): persist off,
            // the reader that parses a miss and writes no sidecar (3.7-0, SidecarPolicy::ParseNoWrite), the attach. A door
            // added there reaches the tools too. Then only what a tool needs on top: no pool (synchronous, §4.4 step 11),
            // no summary, and the tool's options.
            DiagnosticsDoors d = parallel::doors(nullptr, m_receiver.get(), RuleData::installed(), m_store, *m_recordings);
            d.synchronous  = true;     // R-HL-2
            d.writeSummary = false;
            d.rebuild      = m_options.rebuild;
            d.withSigma    = m_options.withSigma;
            d.softTier     = m_options.softTier;
            a->setDiagnosticsDoors(d);
            it = m_athletes.emplace(key, std::move(a)).first;
        }
        Session *s = it->second->sessionAt(dir);
        if (SessionDiagnostics *d = s ? s->diagnostics() : nullptr)
            d->flush();
        return s;
    }

    // The Session's thin adapter for a tool: the owner's applied diagnosis with the view half run (the explanation, the
    // graph), REVIEWING as the old model ran in the tools, posterior ranking as asked. Held until release().
    SessionDiagnosticsModel *adapter(Session *s, bool posteriorRank = tuned::diagUncertainty::kPosteriorRank)
    {
        SessionDiagnosticsModel *m = SessionDiagnosticsModel::adapterFor(s, QStringLiteral("tool"));
        if (!m) return nullptr;
        m->setSynchronous(true);
        m->setReviewing(true);
        // The explanation's ranking is the view half's (ExplainOptions); σ and the soft tier are the owner's (above).
        m->setUncertaintyModes(m_options.withSigma.value_or(tuned::diagUncertainty::kEnabled),
                               m_options.softTier.value_or(tuned::diagUncertainty::kSoftTier), posteriorRank);
        m->addView();
        m->activateSession(s->entry() ? s->entry()->folder : QString());   // the panel's pointing: the diagnosis taken in
        return m;
    }

    // The adapter let go (its use of the Session dropped), then the Session released.
    void release(Session *s, SessionDiagnosticsModel *adapter)
    {
        delete adapter;
        if (s && s->athlete()) s->athlete()->releaseSession(s->id());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

private:
    Options                                            m_options;
    SessionStore                                       m_store;
    std::unique_ptr<SwingStoreRecordingRepository>     m_recordings;
    std::unique_ptr<WorkReceiver>                      m_receiver;
    std::map<QString, std::unique_ptr<Athlete>>        m_athletes;   // destroyed before the receiver
};

} // namespace pinpoint::tools
