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

// RuleDataStore — RuleDataRepository's adapter: THE ONE READER AND WRITER of the rule data's files
// (refactor step 3.2; D5: DEC-170; docs/developer/information_model.md FILE-APP-01…05, SET-06, SET-15).
// The contract is the interface's (src/DomainModel/rule_data_repository.h); what is here is where each
// file is and how it is read.
//
//   shipped    FILE-APP-05: the Qt resource `:/diagnostics/<name>.json`, or the file its PINPOINT_CORE_*
//              variable names (PACK, NORMS, CONTEXTS, FLIGHT_NORMS, SCREENS, DRILLS, GATE_WORDS,
//              REFERENCES) — the seam a test or an offline tool reaches shipped content through, since
//              the resource exists only inside the app binary. Read here, and only here.
//   the user's FILE-APP-01…04 in `dataDir` (<AppData>/diagnostics by default): `user.json`,
//              `user.norms.json`, `drills.json`, `references.json`, `gate_words.json`, `screens.json`,
//              community `*.json` / `*.norms.json`, the shared `contexts.json`.
//   settings   SET-06 `ui/diagnosticsNormSetsOff` and SET-15 `ui/diagnosticsGradePolicy`, in the one
//              settings file (ppSettings), where they have always been stored (the v1.9 reviewer's
//              condition: no settings key moves).
//
// THE NORM SETS' BYTES are still read by the norm leaves themselves (src/Diagnostics/norms, the A5 lines
// tagged step 7: "norms loaded by repositories"): this store decides which ones and assembles them, and
// writes the user's set. Everything else is read here.
//
// THE USER PACK'S REGROUP (characteristic_pack.h) is applied here, once, before the first read of a
// `user.json` written before the swing areas were regrouped: a backup beside it, then the rewrite.

#include "../DomainModel/rule_data_repository.h"

#include <QString>
#include <QStringList>

namespace pinpoint {

class RuleDataStore final : public RuleDataRepository {
public:
    // `dataDir` empty: <AppData>/diagnostics (diagnosticsDataDir()), resolved at each use.
    explicit RuleDataStore(QString dataDir = QString());

    QString dataDir() const;

    analysis::ContentLayers<analysis::ReferenceSet> loadReferences() override;
    analysis::ContentLayers<analysis::DrillSet>     loadDrills() override;
    analysis::ContentLayers<analysis::ScreenSet>    loadScreens() override;
    analysis::ContentLayers<analysis::GateWordSet>  loadGateWords() override;
    RulePacks                                       loadPacks(const analysis::ReferenceSet *refs) override;
    RuleNorms                                       loadNorms() override;
    analysis::FlightNorms                           loadFlightNorms() override;
    QStringList                                     readDisabledNormSets() override;
    QString                                         readGradePolicy() override;
    QStringList                                     newerUserFiles() const override { return m_newer; }

    bool saveUserPack(const analysis::CharacteristicPack &pack, QString *whyNot) override;
    bool saveUserNorms(const analysis::NormPack &norms, QString *whyNot) override;
    bool saveUserReferences(const analysis::ReferenceSet &set, QString *whyNot) override;
    bool saveUserDrills(const analysis::DrillSet &set, QString *whyNot) override;
    bool saveUserScreens(const analysis::ScreenSet &set, QString *whyNot) override;
    bool saveUserGateWords(const analysis::GateWordSet &set, QString *whyNot) override;
    void writeDisabledNormSets(const QStringList &sets) override;
    void writeGradePolicy(const QString &name) override;
    bool backupUserLayers(const QString &stamp, QStringList *written, QString *whyNot) override;

    // One file's bytes, and whether there were any (RuleFile, pack_io.h).
    static analysis::RuleFile readFile(const QString &path);
    // A shipped file: the one its PINPOINT_CORE_* variable names, else the resource.
    static QString shippedPath(const char *env, const QString &resource);

private:
    QString userFile(const QString &name) const;
    // A user layer's file that declares a schema newer than `known`: noted, and true (B-20).
    bool isNewer(const QString &path, const QByteArray &bytes, int known);
    bool refuseNewer(const QString &path, int known, QString *whyNot) const;
    bool write(const QString &path, int known, const QByteArray &bytes, QString *whyNot) const;

    QString     m_dir;
    QStringList m_newer;
};

} // namespace pinpoint

// ── By path, for the tests and offline tools that build a provider from a file of their own ─────────
//
// The forms the pack factories had before refactor step 3.2 (src/Diagnostics/packs/pack_provider.h now
// takes the bytes): each reads through RuleDataStore and hands the bytes on, with the bibliography the
// store reads for the pack loader's book gate. Not for the app, which reads RuleData's snapshot.
namespace pinpoint::analysis {

std::unique_ptr<ICharacteristicPackProvider> makeResourcePackProvider(
    const QString &resourcePath = QStringLiteral(":/diagnostics/core.json"));
std::unique_ptr<ICharacteristicPackProvider> makeFilePackProvider(
    const QString &packFile, PackOrigin origin = PackOrigin::LocalUser);
std::vector<std::unique_ptr<ICharacteristicPackProvider>> makeFilePackProviders(
    const QString &directory = QString());
QStringList characteristicPackFilesIn(const QString &directory = QString());

} // namespace pinpoint::analysis
