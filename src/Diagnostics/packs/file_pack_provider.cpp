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

#include "pack_provider.h"

#include "../engine/pack_io.h"
#include "../engine/provider_leaf_p.h"
#include "../norms/norm_provider.h"   // kNormSetSuffix, kContextTreeFileName: the sibling registries' names
#include "drill_pack.h"
#include "gate_words_pack.h"
#include "reference_pack.h"
#include "screen_pack.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The user and community pack LAYERS, from bytes. Refactor step 3.2 (Q-02: DEC-170) moved every read
// and write of the pack files to the rule data's repository (src/Export/rule_data_store.cpp), the one
// door to FILE-APP-01 and FILE-APP-04; what stays here is what a pack layer MEANS — its origin by file
// name, which files in the shared directory are packs and in what order, and the regroup's rewrite of
// the user's own file as a pure transform the repository applies.

namespace pinpoint::analysis {

namespace {

// The file name the user's OWN pack is written under. Named here rather than spelled out at each
// use, because three separate decisions key on it: where the repository writes the user's pack,
// which file characteristicPackFileOrder() layers first, and which file is LocalUser rather than
// Community.
const QLatin1String kUserPackName("user.json");

// ONE user pack file. A pack that fails to parse is REPORTED and skipped, never fatal: a single
// malformed community pack must not take the whole library down with it, and — now that a directory
// of several packs becomes several of these — must not take its NEIGHBOURS down either. That
// containment is the reason this is one file per provider rather than one directory: a provider holds
// exactly one pack, so "which file was that fault in" has an answer, and a fault in one file cannot
// decide what another file contributes.
//
// The mechanics — "absent is normal", the warning that names the file, dropping the referential codes
// a layer cannot honestly make, and keying off `parsed` rather than `loaded` — are loadLayer()'s,
// shared with the norm side's file leaf. The argument for each lives at provider_leaf_p.h.
class FilePackProvider final : public detail::PackLeaf {
public:
    FilePackProvider(const RuleFile &file, PackOrigin origin, const ReferenceSet *refs)
    {
        m_origin = origin;
        loadLayer(file, refs);
    }

    // A provider that read NOTHING is not a layer. No user pack is the normal case, and reporting the
    // absent file as an installed pack would put a row in the census that a user cannot act on and
    // did not create — indistinguishable from a real but empty pack they authored. Exactly the rule
    // FileNormProvider::layers() applies, for exactly the same reason; it is stated on the two FILE
    // leaves only, because a memory layer with nothing in it is still the editor's working copy and
    // does own its row.
    std::vector<PackLayerInfo> layers() const override
    {
        if (m_pack.id.isEmpty() && m_pack.conditions.empty() && m_pack.measures.empty()
            && m_pack.signalDefs.empty() && m_pack.edges.empty())
            return {};
        return ICharacteristicPackProvider::layers();
    }
};

} // namespace

QString userPackFileName() { return kUserPackName; }

std::optional<QByteArray> userPackRegrouped(const QByteArray &bytes)
{
    // The swing area regroup, applied once to the user's own pack (characteristic_pack.h). The user
    // layer stores full rows for the conditions they edited, so an old token there would otherwise be
    // read through the alias on every load and rewritten only when they next happened to save. The
    // rewrite edits the JSON directly rather than round-tripping through loadPack/savePack, so a field
    // this build does not know about survives it. The loader's alias already gives the right answer
    // without this, so the repository treats any failure to apply it as costing nothing but a warning.
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) return std::nullopt;     // the loader reports a malformed file
    QJsonObject root = doc.object();
    if (packHasCurrentGroups(root)) return std::nullopt;

    if (root.contains(QStringLiteral("conditions"))) {
        QJsonArray conditions = root.value(QStringLiteral("conditions")).toArray();
        for (qsizetype i = 0; i < conditions.size(); ++i) {
            QJsonObject row = conditions.at(i).toObject();
            if (!row.contains(QStringLiteral("group"))) continue;
            row.insert(QStringLiteral("group"),
                       currentConditionGroupToken(row.value(QStringLiteral("id")).toString(),
                                                  row.value(QStringLiteral("group")).toString(),
                                                  /*fileHasCurrentGroups*/ false));
            conditions.replace(i, row);
        }
        root.insert(QStringLiteral("conditions"), conditions);
    }
    root.insert(QStringLiteral("groupsRevision"), kPackGroupsRevision);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QStringList characteristicPackFileOrder(const QStringList &jsonFileNames)
{
    QStringList user, others;
    QStringList names = jsonFileNames;
    names.sort();   // QDir::Name order, whatever order the caller listed them in
    for (const QString &name : std::as_const(names)) {
        if (!name.endsWith(QLatin1String(".json"))) continue;
        // This directory is shared with every other user registry (norm sets, contexts, screens,
        // drills, gate words, references) — see the note in pack_io.h on why it is one directory
        // rather than several. Left unfiltered, `*.json` picks up their files too: each has no
        // top-level `id`, so it either injects a spurious "Pack has no id" issue into a provider's
        // report, or, worse, if it DOES have a non-empty `id` (unlikely today but not contractually
        // ruled out), name order sorts it ahead of `user.json` and it silently becomes a pack in the
        // assembled library. Skip every sibling registry by name.
        //
        // THE ONE PLACE this list lives, and it is spelled with each registry's own file-name constant
        // (audit B-13), so a registry renamed or added in its own header cannot be loaded as a
        // Community characteristic pack by a list that forgot it.
        if (name.endsWith(QLatin1String(kNormSetSuffix))) continue;
        // The pack editor's reset copies every user layer aside beside it as `<name>-<stamp>.backup.json`
        // (RuleDataRepository::backupUserLayers). A copy is for the author, never a layer: read as a
        // community pack it brought the reset model back at the next read (refactor step 3.9, seen in
        // model_browser_test).
        if (name.endsWith(QLatin1String(kUserLayerBackupSuffix))) continue;
        if (name == QLatin1String(kScreenSetFileName) || name == QLatin1String(kDrillSetFileName)
            || name == QLatin1String(kGateWordSetFileName)
            || name == QLatin1String(kReferenceSetFileName)
            || name == QLatin1String(kContextTreeFileName)) continue;

        (name == kUserPackName ? user : others) << name;
    }

    // LAYERING ORDER: `user.json` first, then the rest in name (alphabetical) order.
    //
    // The user's own pack goes first because it is the only layer that may REPLACE core content, and
    // putting it at the front means a later community pack is merged against a library that already
    // reflects the user's overrides rather than against a library the user has since changed.
    // Alphabetical for the rest is an arbitrary rule and is chosen for being one: it is STABLE and it
    // is VISIBLE, so two machines with the same directory assemble the same library, and an author who
    // needs one community pack to land after another can say so by renaming. Directory order
    // (whatever the filesystem hands back) would have neither property, and file mtime would mean
    // re-downloading a pack silently re-ordered the library.
    //
    // Order matters less here than it does for norms, because community packs are namespaced and lose
    // every collision with core anyway (see PackOrigin) — but it decides which of two community packs
    // is reported as the loser when they collide with each other, and that has to be reproducible or
    // the health list changes between runs.
    return user + others;
}

std::unique_ptr<ICharacteristicPackProvider> makeFilePackProvider(const RuleFile &packFile,
                                                                  PackOrigin origin,
                                                                  const ReferenceSet *refs)
{
    return std::make_unique<FilePackProvider>(packFile, origin, refs);
}

std::vector<std::unique_ptr<ICharacteristicPackProvider>> makeFilePackProviders(
    const std::vector<RuleFile> &files, const ReferenceSet *refs)
{
    std::vector<std::unique_ptr<ICharacteristicPackProvider>> out;
    for (const RuleFile &f : files) {
        // ORIGIN BY FILE NAME, which is the only evidence there is. `user.json` is the file the
        // editor writes, so it is by construction the user's own library and gets LocalUser — the
        // origin that may override shipped characteristics, because refusing the user's own edit
        // would make the editor useless for every shipped row.
        //
        // Everything else in that directory got there by being imported from somebody else, so it is
        // Community: namespaced, and core wins on collision. That is the protection PackOrigin was
        // written for, and until this function existed nothing applied it — a single provider covered
        // the whole directory with one origin, so a downloaded pack that happened to sort first was
        // merged as though the user had authored it.
        out.push_back(makeFilePackProvider(
            f, QFileInfo(f.path).fileName() == kUserPackName ? PackOrigin::LocalUser
                                                             : PackOrigin::Community,
            refs));
    }
    return out;
}

// Spelled through kUserPackName, so the file the repository WRITES and the file the enumeration treats
// as the user's own layer cannot drift apart — they were two independent string literals once, which
// is one rename away from a user pack that saves and never loads. A path, not an access: the editor's
// own reads of its working copy name it until it writes through RuleData (refactor item 3.9).
QString userPackPath() { return diagnosticsFilePath(kUserPackName); }

} // namespace pinpoint::analysis
