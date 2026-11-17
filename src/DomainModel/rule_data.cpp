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

#include "rule_data.h"

#include "../Coaching/strike_from_flight.h"   // FlightNorms
#include "../Core/pp_debug.h"

#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>

#include <atomic>

namespace pinpoint {

using namespace pinpoint::analysis;

namespace {

std::atomic<RuleData *> g_installed{ nullptr };

// The snapshot an accessor answers from when no RuleData is installed: every part present and empty,
// so a reader gets "no content", never a null to trip on.
std::shared_ptr<const RuleDataSnapshot> emptySnapshot()
{
    static const std::shared_ptr<const RuleDataSnapshot> s = [] {
        auto e         = std::make_shared<RuleDataSnapshot>();
        e->pack        = std::shared_ptr<const ICharacteristicPackProvider>(
            makeMemoryPackProvider(CharacteristicPack{}, QStringLiteral("(no rule data)"), PackOrigin::Core).release());
        e->shippedPack = e->pack;
        e->norms       = std::shared_ptr<const INormProvider>(
            makeMemoryNormProvider(NormPack{}, ContextTree{}, QStringLiteral("(no rule data)"), PackOrigin::Core).release());
        e->shippedNorms = e->norms;
        e->userPack     = std::make_shared<const CharacteristicPack>();
        e->userNorms    = std::make_shared<const NormPack>();
        e->references  = std::make_shared<const ContentLayers<ReferenceSet>>();
        e->drills      = std::make_shared<const ContentLayers<DrillSet>>();
        e->screens     = std::make_shared<const ContentLayers<ScreenSet>>();
        e->gateWords   = std::make_shared<const ContentLayers<GateWordSet>>();
        e->flightNorms = std::make_shared<const FlightNorms>();
        e->gradePolicy = QStringLiteral("standard");
        return std::shared_ptr<const RuleDataSnapshot>(std::move(e));
    }();
    return s;
}

// A load fault in a content set used to be dropped (audit B-13). It is logged now, once per read; the
// set still loads whatever it could, exactly as before.
template <typename Set>
void logFaults(const ContentLayers<Set> &layers, const char *what)
{
    for (const ValidationIssue &i : layers.report.issues)
        if (i.severity == IssueSeverity::Error)
            ppWarn() << "[RuleData]" << what << qPrintable(i.code) << qPrintable(i.message);
}

// A provider that answers from a provider a snapshot holds — what the interim pack accessors hand out,
// so a reader holding one keeps that snapshot's library alive without a copy or a parse of its own.
class SnapshotPackView final : public ICharacteristicPackProvider {
public:
    explicit SnapshotPackView(std::shared_ptr<const ICharacteristicPackProvider> p) : m_p(std::move(p)) {}
    const CharacteristicPack  &pack() const override { return m_p->pack(); }
    const ValidationReport    &report() const override { return m_p->report(); }
    QString                    label() const override { return m_p->label(); }
    PackOrigin                 origin() const override { return m_p->origin(); }
    std::vector<PackLayerInfo> layers() const override { return m_p->layers(); }

private:
    std::shared_ptr<const ICharacteristicPackProvider> m_p;
};

} // namespace

RuleData::RuleData(std::unique_ptr<RuleDataRepository> repository,
                   std::shared_ptr<const MetricCatalogue> catalogue, QObject *parent)
    : QObject(parent), m_repository(std::move(repository)), m_catalogue(std::move(catalogue))
{
    std::shared_ptr<RuleDataSnapshot> first = assemble(Everything);
    QMutexLocker lk(&m_lock);
    first->serial = ++m_serial;
    m_current     = first;
    m_published.push_back(m_current);
}

RuleData::~RuleData()
{
    if (g_installed.load() == this) install(nullptr);
}

std::shared_ptr<const RuleDataSnapshot> RuleData::snapshot() const
{
    QMutexLocker lk(&m_lock);
    return m_current;
}

// Builds the next snapshot: the parts named are read afresh through the repository, the rest are the
// current snapshot's, shared (an edit to the drills does not re-parse the pack). The bibliography feeds
// the pack loader's book gate, so a re-read of it re-reads the packs too.
std::shared_ptr<RuleDataSnapshot> RuleData::assemble(unsigned parts) const
{
    std::shared_ptr<const RuleDataSnapshot> cur = snapshot();
    auto next = cur ? std::make_shared<RuleDataSnapshot>(*cur) : std::make_shared<RuleDataSnapshot>();
    if (!cur) parts = Everything;
    if (parts & References) parts |= Packs;

    if (parts & References) {
        auto l = std::make_shared<ContentLayers<ReferenceSet>>(m_repository->loadReferences());
        logFaults(*l, "the bibliography:");
        next->references = std::move(l);
    }
    if (parts & Drills) {
        auto l = std::make_shared<ContentLayers<DrillSet>>(m_repository->loadDrills());
        logFaults(*l, "the drills:");
        next->drills = std::move(l);
    }
    if (parts & Screens) {
        auto l = std::make_shared<ContentLayers<ScreenSet>>(m_repository->loadScreens());
        logFaults(*l, "the screens:");
        next->screens = std::move(l);
    }
    if (parts & GateWords) {
        auto l = std::make_shared<ContentLayers<GateWordSet>>(m_repository->loadGateWords());
        logFaults(*l, "the gate words:");
        next->gateWords = std::move(l);
    }
    if (parts & Packs) {
        RulePacks p       = m_repository->loadPacks(&next->references->merged);
        next->pack        = std::move(p.assembled);
        next->shippedPack = std::move(p.shipped);
        next->userPack    = p.user ? std::move(p.user) : std::make_shared<const CharacteristicPack>();
    }
    if (parts & Norms) {
        RuleNorms n        = m_repository->loadNorms();
        next->norms        = std::move(n.assembled);
        next->shippedNorms = std::move(n.shipped);
        next->userNorms    = n.user ? std::move(n.user) : std::make_shared<const NormPack>();
    }
    if (parts & Flight) next->flightNorms = std::make_shared<const FlightNorms>(m_repository->loadFlightNorms());
    if (parts & Settings) {
        next->disabledNormSets = m_repository->readDisabledNormSets();
        next->gradePolicy      = m_repository->readGradePolicy();
    }
    next->newerUserFiles = m_repository->newerUserFiles();
    next->catalogue      = m_catalogue;
    return next;
}

void RuleData::publish(std::shared_ptr<RuleDataSnapshot> next, unsigned parts, const QString &id)
{
    {
        QMutexLocker lk(&m_lock);
        next->serial = ++m_serial;
        m_current    = next;
        m_published.push_back(m_current);
    }
    // Outside the lock: the norm slot's source takes it (installSharedNormSource, below).
    if (g_installed.load() == this && (parts & Norms)) resetSharedNormProvider();

    if (parts & (Norms | Settings)) emit normsChanged(id);
    if (parts & (Packs | References | Drills | Screens | GateWords | Flight)) emit packChanged(id);
}

bool RuleData::saveUserPack(const CharacteristicPack &pack, QString *whyNot)
{
    if (!m_repository->saveUserPack(pack, whyNot)) return false;
    publish(assemble(Packs), Packs, userPackFileName());
    return true;
}

bool RuleData::saveUserNorms(const NormPack &norms, QString *whyNot)
{
    if (!m_repository->saveUserNorms(norms, whyNot)) return false;
    publish(assemble(Norms), Norms, norms.id.isEmpty() ? QStringLiteral("user") : norms.id);
    return true;
}

bool RuleData::saveUserReferences(const ReferenceSet &set, QString *whyNot)
{
    if (!m_repository->saveUserReferences(set, whyNot)) return false;
    publish(assemble(References), References | Packs, QLatin1String(kReferenceSetFileName));
    return true;
}

bool RuleData::saveUserDrills(const DrillSet &set, QString *whyNot)
{
    if (!m_repository->saveUserDrills(set, whyNot)) return false;
    publish(assemble(Drills), Drills, QLatin1String(kDrillSetFileName));
    return true;
}

bool RuleData::saveUserScreens(const ScreenSet &set, QString *whyNot)
{
    if (!m_repository->saveUserScreens(set, whyNot)) return false;
    publish(assemble(Screens), Screens, QLatin1String(kScreenSetFileName));
    return true;
}

bool RuleData::saveUserGateWords(const GateWordSet &set, QString *whyNot)
{
    if (!m_repository->saveUserGateWords(set, whyNot)) return false;
    publish(assemble(GateWords), GateWords, QLatin1String(kGateWordSetFileName));
    return true;
}

void RuleData::setDisabledNormSets(const QStringList &sets)
{
    if (snapshot()->disabledNormSets == sets) return;
    m_repository->writeDisabledNormSets(sets);
    publish(assemble(Settings), Settings, QString());
}

void RuleData::setGradePolicy(const QString &name)
{
    if (snapshot()->gradePolicy == name) return;
    m_repository->writeGradePolicy(name);
    publish(assemble(Settings), Settings, QString());
}

bool RuleData::backupUserLayers(const QString &stamp, QStringList *written, QString *whyNot)
{
    return m_repository->backupUserLayers(stamp, written, whyNot);
}

void RuleData::reload(unsigned parts)
{
    if (parts & References) parts |= Packs;
    publish(assemble(parts), parts, QString());
}

void RuleData::install(RuleData *ruleData)
{
    RuleData *was = g_installed.exchange(ruleData);
    if (was == ruleData) return;
    if (ruleData)
        installSharedNormSource([] { return RuleData::current()->norms; });
    else
        installSharedNormSource({});
}

RuleData *RuleData::installed() { return g_installed.load(); }

std::shared_ptr<const RuleDataSnapshot> RuleData::current()
{
    if (RuleData *r = g_installed.load()) return r->snapshot();
    // Said once, so a process that forgot installProcessRuleData() is never silently empty: every pack,
    // norm and content accessor answers "nothing" from here on.
    static std::atomic<bool> warned{ false };
    if (!warned.exchange(true))
        ppWarn() << "[RuleData] the packs and norms were read with no RuleData installed; answering from an "
                    "empty snapshot (a composition root, tool or test calls installProcessRuleData())";
    return emptySnapshot();
}

} // namespace pinpoint

// ── The interim accessors ───────────────────────────────────────────────────────────────────────────
namespace pinpoint::analysis {

namespace {

// The snapshot a reference is taken from. It is one RuleData keeps for the process's life (its
// m_published list), or the static empty one, so the reference outlives the shared_ptr copy here.
const RuleDataSnapshot &now() { return *RuleData::current(); }

void reloadPart(unsigned part)
{
    if (RuleData *r = RuleData::installed()) r->reload(part);
}

bool noRuleData(QString *whyNot)
{
    if (whyNot) *whyNot = QStringLiteral("No rule data is open.");
    return false;
}

} // namespace

std::unique_ptr<ICharacteristicPackProvider> makeCharacteristicPackProvider()
{
    return std::make_unique<SnapshotPackView>(RuleData::current()->pack);
}

std::unique_ptr<ICharacteristicPackProvider> shippedPackProvider()
{
    return std::make_unique<SnapshotPackView>(RuleData::current()->shippedPack);
}

const ReferenceSet &sharedReferenceSet() { return now().references->merged; }
const ReferenceSet &coreReferenceSet() { return now().references->core; }
ReferenceSet        loadUserReferenceSet() { return now().references->user; }
void                resetSharedReferenceSet() { reloadPart(RuleData::References); }
const DrillSet     &sharedDrillSet() { return now().drills->merged; }
const DrillSet     &coreDrillSet() { return now().drills->core; }
DrillSet            loadUserDrillSet() { return now().drills->user; }
void                resetSharedDrillSet() { reloadPart(RuleData::Drills); }
const ScreenSet    &sharedScreenSet() { return now().screens->merged; }
const ScreenSet    &coreScreenSet() { return now().screens->core; }
ScreenSet           loadUserScreenSet() { return now().screens->user; }
void                resetSharedScreenSet() { reloadPart(RuleData::Screens); }
const GateWordSet  &sharedGateWordSet() { return now().gateWords->merged; }
const GateWordSet  &coreGateWordSet() { return now().gateWords->core; }
GateWordSet         loadUserGateWordSet() { return now().gateWords->user; }
void                resetSharedGateWordSet() { reloadPart(RuleData::GateWords); }

bool saveUserPack(const CharacteristicPack &pack, QString *whyNot)
{
    RuleData *r = RuleData::installed();
    return r ? r->saveUserPack(pack, whyNot) : noRuleData(whyNot);
}
bool saveUserNormPack(const NormPack &pack, QString *whyNot)
{
    RuleData *r = RuleData::installed();
    return r ? r->saveUserNorms(pack, whyNot) : noRuleData(whyNot);
}
bool saveUserReferenceSet(const ReferenceSet &set, QString *whyNot)
{
    RuleData *r = RuleData::installed();
    return r ? r->saveUserReferences(set, whyNot) : noRuleData(whyNot);
}
bool saveUserDrillSet(const DrillSet &set, QString *whyNot)
{
    RuleData *r = RuleData::installed();
    return r ? r->saveUserDrills(set, whyNot) : noRuleData(whyNot);
}
bool saveUserScreenSet(const ScreenSet &set, QString *whyNot)
{
    RuleData *r = RuleData::installed();
    return r ? r->saveUserScreens(set, whyNot) : noRuleData(whyNot);
}
bool saveUserGateWordSet(const GateWordSet &set, QString *whyNot)
{
    RuleData *r = RuleData::installed();
    return r ? r->saveUserGateWords(set, whyNot) : noRuleData(whyNot);
}

QString citationLabel(const QString &citation) { return citationLabel(citation, sharedReferenceSet()); }

} // namespace pinpoint::analysis
