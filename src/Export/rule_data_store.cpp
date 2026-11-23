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

#include "rule_data_store.h"
#include "rule_data_install.h"

#include "../Core/atomic_write.h"
#include "../Coaching/strike_from_flight.h"   // FlightNorms
#include "../Core/pp_debug.h"
#include "../Core/pp_settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace pinpoint {

using namespace pinpoint::analysis;

namespace {

// The shipped layer, presented to the merge as its core while the snapshot keeps the same parsed
// provider as "the shipped pack alone" — one parse of core.json serves both (the editor used to make a
// second, private one).
class SharedPackView final : public ICharacteristicPackProvider {
public:
    explicit SharedPackView(std::shared_ptr<const ICharacteristicPackProvider> p) : m_p(std::move(p)) {}
    const CharacteristicPack  &pack() const override { return m_p->pack(); }
    const ValidationReport    &report() const override { return m_p->report(); }
    QString                    label() const override { return m_p->label(); }
    PackOrigin                 origin() const override { return m_p->origin(); }
    std::vector<PackLayerInfo> layers() const override { return m_p->layers(); }

private:
    std::shared_ptr<const ICharacteristicPackProvider> m_p;
};

// The shipped norm set, presented to the norm merge as its core while the snapshot keeps the same parsed
// provider as "the shipped norms alone" (refactor step 3.9) — one parse of norms.json and contexts.json
// serves both, as SharedPackView does for core.json (the corridor editor used to make a second, private
// one). Every virtual is forwarded, so the merge answers exactly as it did over the provider itself.
class SharedNormView final : public INormProvider {
public:
    explicit SharedNormView(std::shared_ptr<const INormProvider> p) : m_p(std::move(p)) {}
    const NormPack         &norms() const override { return m_p->norms(); }
    const ContextTree      &contexts() const override { return m_p->contexts(); }
    const ValidationReport &report() const override { return m_p->report(); }
    QString                 label() const override { return m_p->label(); }
    PackOrigin              origin() const override { return m_p->origin(); }
    const Norm *shippedNorm(const QString &measureId, const QString &contextId, const Cohort &cohort) const override
    {
        return m_p->shippedNorm(measureId, contextId, cohort);
    }
    bool isOverridden(const QString &measureId, const QString &contextId, const Cohort &cohort) const override
    {
        return m_p->isOverridden(measureId, contextId, cohort);
    }
    std::vector<NormSetInfo> layers() const override { return m_p->layers(); }

private:
    std::shared_ptr<const INormProvider> m_p;
};

// The copy of the user's pack taken before its one migration. It must NOT end in `.json`: the pack
// enumeration reads `*.json` in this directory and would load the copy as a second pack.
QString regroupBackupPath(const QString &path)
{
    const QString base      = path + QStringLiteral(".pre-areas-regroup.bak");
    QString       candidate = base;
    for (int n = 2; QFile::exists(candidate); ++n)
        candidate = base + QLatin1Char('.') + QString::number(n);
    return candidate;
}

// The swing area regroup, applied once to the user's own pack. Any failure costs nothing but a
// warning: the loader's alias reads the old tokens correctly, and a backup that cannot be written
// means the file is not touched at all.
void migrateUserPackGroups(const QString &path)
{
    const RuleFile f = RuleDataStore::readFile(path);
    if (f.state != RuleFile::State::Read) return;   // absent is normal; the loader reports the rest
    // A pack from a NEWER build is not this build's to rewrite (B-20): it stays read-only, regroup and all.
    if (declaredSchemaVersion(f.bytes) > kPackSchemaVersion) return;
    const std::optional<QByteArray> regrouped = userPackRegrouped(f.bytes);
    if (!regrouped) return;

    const QString backup = regroupBackupPath(path);
    QString       whyNot;
    if (!pinpoint::atomicWrite(backup, f.bytes, &whyNot)) {
        ppWarn() << "[Diagnostics] user pack left on its old area names: no backup possible:"
                 << qPrintable(whyNot);
        return;
    }
    if (!pinpoint::atomicWrite(path, *regrouped, &whyNot)) {
        ppWarn() << "[Diagnostics] user pack left on its old area names: could not rewrite it:"
                 << qPrintable(whyNot);
        return;
    }
    ppInfo() << "[Diagnostics] user pack moved to the new swing areas; backup at" << qPrintable(backup);
}

} // namespace

RuleDataStore::RuleDataStore(QString dataDir) : m_dir(std::move(dataDir)) {}

// The default directory is resolved at each use, not once: it follows the application's data location,
// which a test (or a tool) may move after the store is made, as every path function here always did.
QString RuleDataStore::dataDir() const { return m_dir.isEmpty() ? diagnosticsDataDir() : m_dir; }

// readFile and shippedPath: rule_data_reads.cpp (refactor step 7.1), beside the norm reads that need them.

QString RuleDataStore::userFile(const QString &name) const
{
    const QString dir = dataDir();
    return dir.isEmpty() ? QString() : dir + QLatin1Char('/') + name;
}

bool RuleDataStore::isNewer(const QString &path, const QByteArray &bytes, int known)
{
    // Each read answers for its own file afresh: a newer file since replaced or removed is not kept.
    if (declaredSchemaVersion(bytes) <= known) { m_newer.removeAll(path); return false; }
    if (!m_newer.contains(path)) m_newer << path;
    ppWarn() << "[RuleData]" << qPrintable(path)
             << "was written by a newer PinPoint Studio; this build keeps it read-only";
    return true;
}

bool RuleDataStore::refuseNewer(const QString &path, int known, QString *whyNot) const
{
    const RuleFile f = readFile(path);
    if (f.state != RuleFile::State::Read || declaredSchemaVersion(f.bytes) <= known) return false;
    if (whyNot)
        *whyNot = QStringLiteral("'%1' was written by a newer version of PinPoint Studio, so this version "
                                 "keeps it read-only rather than overwrite what it cannot read.")
                      .arg(QFileInfo(path).fileName());
    return true;
}

bool RuleDataStore::write(const QString &path, int known, const QByteArray &bytes, QString *whyNot) const
{
    if (refuseNewer(path, known, whyNot)) return false;
    // Temporary-and-rename, so an interrupted save cannot truncate content the author has spent time on.
    return pinpoint::atomicWrite(path, bytes, whyNot);
}

// ── Reads ───────────────────────────────────────────────────────────────────────────────────────

ContentLayers<ReferenceSet> RuleDataStore::loadReferences()
{
    const RuleFile user = readFile(userFile(QLatin1String(kReferenceSetFileName)));
    if (user.state != RuleFile::State::Read) m_newer.removeAll(user.path);
    else isNewer(user.path, user.bytes, kReferenceSchemaVersion);
    return assembleReferenceLayers(
        readFile(shippedPath("PINPOINT_CORE_REFERENCES", QStringLiteral(":/diagnostics/references.json"))), user);
}

ContentLayers<DrillSet> RuleDataStore::loadDrills()
{
    const RuleFile user = readFile(userFile(QLatin1String(kDrillSetFileName)));
    if (user.state != RuleFile::State::Read) m_newer.removeAll(user.path);
    else isNewer(user.path, user.bytes, kDrillSchemaVersion);
    return assembleDrillLayers(
        readFile(shippedPath("PINPOINT_CORE_DRILLS", QStringLiteral(":/diagnostics/drills.json"))), user);
}

ContentLayers<ScreenSet> RuleDataStore::loadScreens()
{
    const RuleFile user = readFile(userFile(QLatin1String(kScreenSetFileName)));
    if (user.state != RuleFile::State::Read) m_newer.removeAll(user.path);
    else isNewer(user.path, user.bytes, kScreenSchemaVersion);
    return assembleScreenLayers(
        readFile(shippedPath("PINPOINT_CORE_SCREENS", QStringLiteral(":/diagnostics/screens.json"))), user);
}

ContentLayers<GateWordSet> RuleDataStore::loadGateWords()
{
    const RuleFile user = readFile(userFile(QLatin1String(kGateWordSetFileName)));
    if (user.state != RuleFile::State::Read) m_newer.removeAll(user.path);
    else isNewer(user.path, user.bytes, kGateWordSchemaVersion);
    return assembleGateWordLayers(
        readFile(shippedPath("PINPOINT_CORE_GATE_WORDS", QStringLiteral(":/diagnostics/gate_words.json"))), user);
}

RulePacks RuleDataStore::loadPacks(const ReferenceSet *refs)
{
    RulePacks out;
    out.shipped = std::shared_ptr<const ICharacteristicPackProvider>(makeResourcePackProvider(
        readFile(shippedPath("PINPOINT_CORE_PACK", QStringLiteral(":/diagnostics/core.json"))), refs).release());

    // The user's own pack and the community packs: one provider per file, in the enumeration's order.
    std::vector<RuleFile> layers;
    const QString         dir          = dataDir();
    bool                  seenUserPack = false;
    if (!dir.isEmpty() && QDir(dir).exists()) {   // no user packs is the normal case, not a problem
        migrateUserPackGroups(userFile(userPackFileName()));
        const QStringList names = QDir(dir).entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
        for (const QString &name : characteristicPackFileOrder(names)) {
            RuleFile f = readFile(userFile(name));
            if (name == userPackFileName() && f.state == RuleFile::State::Read) {
                isNewer(f.path, f.bytes, kPackSchemaVersion);
                // The editor's baseline: PARSED, not loaded (RulePacks says why), from the same bytes.
                PackLoadResult parsed = loadPack(f.bytes, f.path, refs);
                if (parsed.parsed) out.user = std::make_shared<const CharacteristicPack>(std::move(parsed.pack));
            }
            seenUserPack = seenUserPack || name == userPackFileName();
            layers.push_back(std::move(f));
        }
    }
    if (!seenUserPack) m_newer.removeAll(userFile(userPackFileName()));
    out.assembled = std::shared_ptr<const ICharacteristicPackProvider>(makeMergedPackProvider(
        std::make_unique<SharedPackView>(out.shipped), makeFilePackProviders(layers, refs)).release());
    return out;
}

RuleNorms RuleDataStore::loadNorms()
{
    RuleNorms out;
    // The norm set's own file, checked on read like the user's other layers (B-20), and parsed for the
    // corridor editor (RuleNorms says why parsed, not validated).
    const RuleFile user = readFile(userFile(QLatin1String("user") + QLatin1String(kNormSetSuffix)));
    if (user.state != RuleFile::State::Read) {
        m_newer.removeAll(user.path);
    } else {
        isNewer(user.path, user.bytes, kNormPackSchemaVersion);
        NormPackLoadResult parsed = loadNormPack(user.bytes, user.path);
        if (parsed.parsed) out.user = std::make_shared<const NormPack>(std::move(parsed.pack));
    }
    // The shipped set and context tree (PINPOINT_CORE_NORMS / _CONTEXTS) and one provider per norm-set
    // file in the directory, assembled exactly as makeNormProvider() does over the default directory —
    // the shipped layer through a view, so the snapshot keeps that one parse as "the shipped norms alone".
    out.shipped   = std::shared_ptr<const INormProvider>(makeResourceNormProvider().release());
    out.assembled = std::shared_ptr<const INormProvider>(
        makeMergedNormProvider(std::make_unique<SharedNormView>(out.shipped), makeFileNormProviders(dataDir()))
            .release());
    return out;
}

FlightNorms RuleDataStore::loadFlightNorms()
{
    // The table PINPOINT_CORE_FLIGHT_NORMS names (tests, tooling), else the resource beside norms.json; empty
    // (nothing is read from the flight) when it cannot be read. The one read of the file since refactor
    // step 7.1: the rules' own loader (strike_from_flight.h) is gone.
    const RuleFile f = readFile(shippedPath("PINPOINT_CORE_FLIGHT_NORMS", QStringLiteral(":/diagnostics/flight_norms.json")));
    if (f.state != RuleFile::State::Read) return {};
    QJsonParseError     err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return flightNormsFromJson(doc.object());
}

QStringList RuleDataStore::readDisabledNormSets()
{
    return ppSettings().value(QStringLiteral("ui/diagnosticsNormSetsOff"), QStringList()).toStringList();
}

QString RuleDataStore::readGradePolicy()
{
    return ppSettings().value(QStringLiteral("ui/diagnosticsGradePolicy"), QStringLiteral("standard")).toString();
}

// ── Writes ──────────────────────────────────────────────────────────────────────────────────────

bool RuleDataStore::saveUserPack(const CharacteristicPack &pack, QString *whyNot)
{
    return write(userFile(userPackFileName()), kPackSchemaVersion,
                 QJsonDocument(savePack(pack)).toJson(QJsonDocument::Indented), whyNot);
}

bool RuleDataStore::saveUserNorms(const NormPack &norms, QString *whyNot)
{
    return write(userFile(QLatin1String("user") + QLatin1String(kNormSetSuffix)), kNormPackSchemaVersion,
                 QJsonDocument(saveNormPack(norms)).toJson(QJsonDocument::Indented), whyNot);
}

bool RuleDataStore::saveUserReferences(const ReferenceSet &set, QString *whyNot)
{
    return write(userFile(QLatin1String(kReferenceSetFileName)), kReferenceSchemaVersion, saveReferenceSet(set), whyNot);
}

bool RuleDataStore::saveUserDrills(const DrillSet &set, QString *whyNot)
{
    return write(userFile(QLatin1String(kDrillSetFileName)), kDrillSchemaVersion, saveDrillSet(set), whyNot);
}

bool RuleDataStore::saveUserScreens(const ScreenSet &set, QString *whyNot)
{
    return write(userFile(QLatin1String(kScreenSetFileName)), kScreenSchemaVersion, saveScreenSet(set), whyNot);
}

bool RuleDataStore::saveUserGateWords(const GateWordSet &set, QString *whyNot)
{
    return write(userFile(QLatin1String(kGateWordSetFileName)), kGateWordSchemaVersion, saveGateWordSet(set), whyNot);
}

void RuleDataStore::writeDisabledNormSets(const QStringList &sets)
{
    ppSettings().setValue(QStringLiteral("ui/diagnosticsNormSetsOff"), sets);
}

void RuleDataStore::writeGradePolicy(const QString &name)
{
    ppSettings().setValue(QStringLiteral("ui/diagnosticsGradePolicy"), name);
}

// The pack editor's "Reset to the standard model" copies (refactor step 3.9: moved here from
// model_browser.cpp, which copied the files itself). `<name>-<stamp>.backup.<suffix>` beside each layer,
// the name the editor's design gave them (ADDENDUM-02); the pack enumeration skips that suffix
// (characteristicPackFileOrder), so a copy is never loaded as a community pack.
bool RuleDataStore::backupUserLayers(const QString &stamp, QStringList *written, QString *whyNot)
{
    const QStringList names{ userPackFileName(), QLatin1String("user") + QLatin1String(kNormSetSuffix),
                             QLatin1String(kScreenSetFileName), QLatin1String(kDrillSetFileName),
                             QLatin1String(kGateWordSetFileName) };
    for (const QString &name : names) {
        const QString path = userFile(name);
        if (path.isEmpty() || !QFile::exists(path)) continue;   // a layer never edited: nothing to copy

        const QFileInfo info(path);
        const QString   backup = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                               + QStringLiteral("-") + stamp + QStringLiteral(".backup.") + info.suffix();
        // Every layer is a .json file, so the copy ends in kUserLayerBackupSuffix, which the enumeration skips.
        QFile::remove(backup);
        if (!QFile::copy(path, backup)) {
            if (whyNot)
                *whyNot = QObject::tr("Could not write the backup %1, so nothing was reset.").arg(backup);
            return false;
        }
        if (written) written->append(backup);
    }
    return true;
}

// ── How a process gets its one RuleData (rule_data_install.h) ─────────────────────────────────────

std::unique_ptr<RuleDataRepository> makeRuleDataRepository(const QString &dataDir)
{
    return std::make_unique<RuleDataStore>(dataDir);
}

RuleData &installProcessRuleData(std::shared_ptr<const MetricCatalogue> catalogue)
{
    // Never destroyed: it lives until the process exits, past every static that might still read it
    // (the norm slot among them).
    static RuleData *one = [&catalogue] {
        auto *r = new RuleData(makeRuleDataRepository(), std::move(catalogue));
        RuleData::install(r);
        // The norm slot's assembly should RuleData ever be uninstalled (after the source: no reset).
        analysis::installNormFileFactory();
        return r;
    }();
    return *one;
}

} // namespace pinpoint

// ── By path ─────────────────────────────────────────────────────────────────────────────────────
namespace pinpoint::analysis {

namespace {
ReferenceSet bibliographyNow() { return RuleDataStore().loadReferences().merged; }
} // namespace

std::unique_ptr<ICharacteristicPackProvider> makeResourcePackProvider(const QString &resourcePath)
{
    const ReferenceSet refs = bibliographyNow();
    return makeResourcePackProvider(RuleDataStore::readFile(RuleDataStore::shippedPath("PINPOINT_CORE_PACK", resourcePath)), &refs);
}

std::unique_ptr<ICharacteristicPackProvider> makeFilePackProvider(const QString &packFile, PackOrigin origin)
{
    // The user's own file is regrouped before its read, as the store does (the provider did, before 3.2).
    if (origin == PackOrigin::LocalUser && QFileInfo(packFile).fileName() == userPackFileName())
        migrateUserPackGroups(packFile);
    const ReferenceSet refs = bibliographyNow();
    return makeFilePackProvider(RuleDataStore::readFile(packFile), origin, &refs);
}

std::vector<std::unique_ptr<ICharacteristicPackProvider>> makeFilePackProviders(const QString &directory)
{
    const QString dirPath = directory.isEmpty() ? diagnosticsDataDir() : directory;
    if (!dirPath.isEmpty() && QDir(dirPath).exists())
        migrateUserPackGroups(QDir(dirPath).filePath(userPackFileName()));
    const ReferenceSet    refs = bibliographyNow();
    std::vector<RuleFile> files;
    for (const QString &path : characteristicPackFilesIn(directory)) files.push_back(RuleDataStore::readFile(path));
    return makeFilePackProviders(files, &refs);
}

QStringList characteristicPackFilesIn(const QString &directory)
{
    const QString dirPath = directory.isEmpty() ? diagnosticsDataDir() : directory;
    if (dirPath.isEmpty() || !QDir(dirPath).exists()) return {};
    QStringList out;
    for (const QString &name : characteristicPackFileOrder(
             QDir(dirPath).entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)))
        out << QDir(dirPath).filePath(name);
    return out;
}

} // namespace pinpoint::analysis
