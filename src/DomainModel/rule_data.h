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

#pragma once

// RuleData — THE OWNER OF THE PACKS AND NORMS, one per machine (refactor step 3.2; Q-02 closed by D5:
// DEC-170, IM-S3-1; governance/artefacts/information_model.md OBJ-PACKS-NORMS, CLS-PACKS-NORMS, EVT-12, EVT-13,
// R-CON-4).
//
// What it owns, at the APPLICATION level (the user's edits are per machine, in <AppData>/diagnostics/,
// not per athlete or session): the shipped core pack and norms, the user's pack, corridor and content
// edits, community sets, the flight norms, the metric catalogue, the set of disabled norm sets (SET-06)
// and the grade policy (SET-15). Its one door is RuleDataRepository (rule_data_repository.h); nothing
// else reads or writes those files (P11).
//
// ONE IMMUTABLE SNAPSHOT PER CHANGE (R-CON-4). snapshot() hands out a shared_ptr<const> to everything
// above, assembled once. A change — a command below, or reload() — builds the next snapshot and
// PUBLISHES it, replacing the current one whole; a holder of the old one keeps reading it, unchanged, for
// as long as it holds it (one grading pass grades against one snapshot, start to finish). Publishing
// emits EVT-12 NormsChanged (the norm sets, SET-06, SET-15) or EVT-13 PackChanged (the characteristic
// pack and the content sets), carrying the id of the set that changed ("" for a reload of everything).
//
// THREADS. Made, commanded and published on the GUI thread, at launch, before any reader — so nothing is
// ever first built on a worker (audit B-03). snapshot() and current() are safe from any thread.
//
// THE PROCESS'S ONE, until each reader is handed it (refactor steps 3.5–3.9, 5, 7). The composition root
// (main.cpp, a tool's or a test's main) makes and installs it through installProcessRuleData()
// (src/Export/rule_data_install.h), which never destroys it. The readers not yet handed the snapshot reach it through current() and the
// interim accessors at the end of this header, which replace the lazy registries and the per-caller
// assemblies they used to build (audit B-02, B-03, B-13). Installing also makes RuleData the source of
// the process-wide norm slot (sharedNormProvider(), norm_provider.h), so the analysis passes that read
// it grade against the same norm set.

#include "rule_data_repository.h"

#include "../Vocabulary/Metrics/metric_catalogue.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace pinpoint {

struct RuleDataSnapshot {
    quint64 serial = 0;   // 1, 2, 3 … per published snapshot of one RuleData; 0 is the empty one

    std::shared_ptr<const analysis::ICharacteristicPackProvider> pack;       // core + user + community
    std::shared_ptr<const analysis::ICharacteristicPackProvider> shippedPack;
    std::shared_ptr<const analysis::INormProvider>               norms;      // core + user + community
    // The editor's layers (refactor step 3.9): the shipped norm set and context tree alone, and the user's
    // own pack and norm set AS PARSED (RulePacks / RuleNorms say why parsed, not loaded). The pack and
    // corridor editor reads these, never the files.
    std::shared_ptr<const analysis::INormProvider>               shippedNorms;
    std::shared_ptr<const analysis::CharacteristicPack>          userPack;
    std::shared_ptr<const analysis::NormPack>                    userNorms;
    std::shared_ptr<const analysis::ContentLayers<analysis::ReferenceSet>> references;
    std::shared_ptr<const analysis::ContentLayers<analysis::DrillSet>>     drills;
    std::shared_ptr<const analysis::ContentLayers<analysis::ScreenSet>>    screens;
    std::shared_ptr<const analysis::ContentLayers<analysis::GateWordSet>>  gateWords;
    std::shared_ptr<const analysis::FlightNorms>                           flightNorms;
    std::shared_ptr<const analysis::MetricCatalogue>                       catalogue;   // null when none was given

    // SET-06: the norm sets the user turned off, as stored. RECORDED, NOT APPLIED: nothing in the app
    // applies it today (no grader passes it to the norm merge), and applying it here would change what
    // grades. The findings stamp records it (R-STAMP-1, item 3.3).
    QStringList disabledNormSets;
    QString     gradePolicy;          // SET-15: the preset's name, as stored ("standard" when unset)
    QStringList newerUserFiles;       // B-20: user layers this build keeps read-only
};

class RuleData : public QObject
{
    Q_OBJECT

public:
    // Reads everything through `repository` and publishes the first snapshot. `catalogue` is the metric
    // catalogue the composition root built once (audit A-19); it rides in every snapshot unchanged.
    explicit RuleData(std::unique_ptr<RuleDataRepository> repository,
                      std::shared_ptr<const analysis::MetricCatalogue> catalogue = {},
                      QObject *parent = nullptr);
    ~RuleData() override;

    std::shared_ptr<const RuleDataSnapshot> snapshot() const;   // any thread

    // ── Commands (GUI thread). Each writes through the repository and, when the write lands, publishes
    //    one new snapshot and its event. A failed write publishes nothing and says why. ──────────────
    bool saveUserPack(const analysis::CharacteristicPack &pack, QString *whyNot = nullptr);    // EVT-13
    bool saveUserNorms(const analysis::NormPack &norms, QString *whyNot = nullptr);            // EVT-12
    bool saveUserReferences(const analysis::ReferenceSet &set, QString *whyNot = nullptr);     // EVT-13
    bool saveUserDrills(const analysis::DrillSet &set, QString *whyNot = nullptr);             // EVT-13
    bool saveUserScreens(const analysis::ScreenSet &set, QString *whyNot = nullptr);           // EVT-13
    bool saveUserGateWords(const analysis::GateWordSet &set, QString *whyNot = nullptr);       // EVT-13
    void setDisabledNormSets(const QStringList &sets);                                          // EVT-12
    void setGradePolicy(const QString &name);                                                   // EVT-12
    // Copies the user's own layers aside before the editor's reset replaces them (the repository's
    // backupUserLayers says how). Changes no content, so it publishes nothing.
    bool backupUserLayers(const QString &stamp, QStringList *written, QString *whyNot = nullptr);

    // Re-read parts from the repository (a file changed outside the commands: a tool pointed the
    // PINPOINT_CORE_* seams elsewhere, an interim reader asked for a fresh read) and publish one snapshot.
    enum Part : unsigned { Packs = 1u, Norms = 2u, References = 4u, Drills = 8u, Screens = 16u,
                           GateWords = 32u, Flight = 64u, Settings = 128u, Everything = 255u };
    void reload(unsigned parts = Everything);

    // The process's one (see the header comment).
    static void                                    install(RuleData *ruleData);   // null uninstalls
    static RuleData                               *installed();
    static std::shared_ptr<const RuleDataSnapshot> current();   // installed()'s snapshot, or an empty one

signals:
    void normsChanged(const QString &setId);   // EVT-12 NormsChanged
    void packChanged(const QString &packId);   // EVT-13 PackChanged

private:
    std::shared_ptr<RuleDataSnapshot> assemble(unsigned parts) const;
    void                              publish(std::shared_ptr<RuleDataSnapshot> next, unsigned parts,
                                              const QString &id);

    std::unique_ptr<RuleDataRepository>              m_repository;
    std::shared_ptr<const analysis::MetricCatalogue> m_catalogue;
    mutable QMutex                                   m_lock;      // guards the three below
    std::shared_ptr<const RuleDataSnapshot>          m_current;
    // Every snapshot this RuleData published, kept for ITS life — the process's, for the process's one,
    // which installProcessRuleData() never destroys (the app's included): the interim accessors below
    // hand out references into a snapshot, as the registries they replace did, and an edit must not
    // free one a reader still holds. Bounded by the PUBLISHES: one per command that lands (a save in
    // the Diagnostic Model, a SET-06 / SET-15 change) and one per reload() — which only a tool switching
    // its PINPOINT_CORE_* seams and the tests ask for (the editor's post-save resets went in step 3.9: its
    // saves are the commands, and they publish). Nothing publishes per report, per shot or per session,
    // so in the app the list grows with edits only.
    std::vector<std::shared_ptr<const RuleDataSnapshot>> m_published;
    quint64                                          m_serial = 0;
};

} // namespace pinpoint

// ── The interim accessors (refactor step 3.2) ───────────────────────────────────────────────────────
//
// For the readers not yet handed the snapshot (refactor items 3.5–3.9, steps 5 and 7 move them). Each
// reads RuleData::current() — the one assembly, never a private one (B-02) — under the names the lazy
// registries had, so no reader changed what it calls. A reference is into a snapshot RuleData keeps for
// the process's life. The writes are RuleData's commands; the resets ask it to re-read that part (since
// step 3.9 no app code calls a reset: the pack and corridor editor reads RuleData's snapshot and writes
// through its commands; the tests and tools that switch seams still do).
// With no RuleData installed, every accessor answers from an EMPTY snapshot.
namespace pinpoint::analysis {

std::unique_ptr<ICharacteristicPackProvider> makeCharacteristicPackProvider();   // the assembled library
std::unique_ptr<ICharacteristicPackProvider> shippedPackProvider();             // the shipped layer alone

const ReferenceSet &sharedReferenceSet();
const ReferenceSet &coreReferenceSet();
ReferenceSet        loadUserReferenceSet();
void                resetSharedReferenceSet();   // re-reads the bibliography and, with it, the packs
const DrillSet     &sharedDrillSet();
const DrillSet     &coreDrillSet();
DrillSet            loadUserDrillSet();
void                resetSharedDrillSet();
const ScreenSet    &sharedScreenSet();
const ScreenSet    &coreScreenSet();
ScreenSet           loadUserScreenSet();
void                resetSharedScreenSet();
const GateWordSet  &sharedGateWordSet();
const GateWordSet  &coreGateWordSet();
GateWordSet         loadUserGateWordSet();
void                resetSharedGateWordSet();

bool saveUserPack(const CharacteristicPack &pack, QString *whyNot = nullptr);
bool saveUserNormPack(const NormPack &pack, QString *whyNot = nullptr);
bool saveUserReferenceSet(const ReferenceSet &set, QString *whyNot = nullptr);
bool saveUserDrillSet(const DrillSet &set, QString *whyNot = nullptr);
bool saveUserScreenSet(const ScreenSet &set, QString *whyNot = nullptr);
bool saveUserGateWordSet(const GateWordSet &set, QString *whyNot = nullptr);

// citationLabel() against the snapshot's bibliography (reference_pack.h has the form that is handed one).
QString citationLabel(const QString &citation);

} // namespace pinpoint::analysis
