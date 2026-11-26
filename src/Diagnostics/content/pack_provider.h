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

#include "characteristic_pack.h"
#include "../model/pack_io.h"   // RuleFile

#include <QString>

#include <memory>
#include <optional>
#include <vector>

// The pack provider seam: providers over content that is already in hand. Nothing here reads a file
// (refactor step 3.2): RuleData (src/DomainModel/rule_data.h) owns the one assembled library, and its
// repository (src/Stores/rule_data_store.cpp) is the one door to the files. No self-registering
// statics, no startup registration, nothing that runs before main().

namespace pinpoint::analysis {

// Where a pack came from. This decides who may override whom, and the distinction is load-bearing:
//
//   Core        — shipped, read-only.
//   LocalUser   — this user's own library. Their deliberate edits, so they OVERRIDE core.
//   Community   — imported from someone else. Namespaced, and core always wins.
//
// The brief's rule is "core wins on collision", aimed at stopping a community pack silently
// redefining a shipped characteristic — a user reading the detail page would have no way to tell
// which definition they were looking at. That rationale does not apply to the user editing their
// own library: refusing their edit would make the editor useless for every shipped characteristic.
// So the protection is kept exactly where it was aimed, and no wider.
enum class PackOrigin { Core, LocalUser, Community };

// One layer in an assembled library, for the census a UI shows. The counterpart of NormSetInfo, and
// deliberately the same idea: a provider that assembles others reports its CHILDREN here, never
// itself, because "merged" is an implementation word and a user reading a list of installed packs
// needs to see the shipped library and their own as separate things — that separation is what the
// override relationship between them means.
//
// WHICH COUNTS, and why not all four. A norm set holds one kind of row, so one number says how big
// it is. A pack holds four (measures, signals, conditions, edges) and reporting all of them would
// answer a question nobody asks of a layer list. Conditions and measures are what a reader judges a
// pack BY — the faults it can name, and the things it can measure to name them. Signals and edges
// are the wiring between those two, so their counts move with them and say nothing on their own.
struct PackLayerInfo {
    QString    id;
    QString    label;
    PackOrigin origin         = PackOrigin::Core;
    int        conditionCount = 0;
    int        measureCount   = 0;
    bool       readOnly       = true;
};

class ICharacteristicPackProvider {
public:
    virtual ~ICharacteristicPackProvider() = default;

    // The assembled library. A provider that fails to load reports it through `report()` and
    // returns whatever it could read — the UI shows a broken community pack rather than an empty
    // library with no explanation.
    virtual const CharacteristicPack &pack() const = 0;

    // Load-time and structural findings. Warnings here ARE the health list.
    virtual const ValidationReport &report() const = 0;

    virtual QString label() const = 0;
    virtual PackOrigin origin() const = 0;

    // The layers behind this provider, shipped first. The default reports THIS provider as its own
    // single layer, which is right for every leaf; only the merged provider overrides it.
    //
    // The pack's OWN id names it, not the provider's label: a leaf labels itself with where it was
    // read from (":/diagnostics/core.json", or a file path), and a file path is not what a pack is
    // called. The label falls back to it only when the pack has no id to give — a pack that failed
    // to parse, where naming the file is the most useful thing left to say.
    //
    // Defined INLINE, where the norm side's identical default sits in resource_norm_provider.cpp.
    // Not a stylistic difference: the pack providers are compiled into test targets one .cpp at a
    // time (core_pack_test links file_pack_provider.cpp and nothing else), so an out-of-line default
    // would put the definition in a translation unit half its callers never see.
    virtual std::vector<PackLayerInfo> layers() const
    {
        const CharacteristicPack &p    = pack();
        const QString             name = p.id.isEmpty() ? label() : p.id;
        return { PackLayerInfo{ name, name, origin(), int(p.conditions.size()),
                                int(p.measures.size()), p.readOnly } };
    }
};

// Where the user's own pack lives, and its file name. A PATH, not an access: the rule data's repository
// (src/Stores/rule_data_store.cpp) reads and writes the file (refactor step 3.2; FILE-APP-01); since item
// 3.9 the editor reads RuleData's snapshot and names no file.
QString userPackPath();
QString userPackFileName();

// The end of the copies the pack editor's reset takes of each user layer, beside it
// (`<name>-<stamp>.backup.json`; RuleDataRepository::backupUserLayers). Never a pack: the enumeration
// below skips it (refactor step 3.9).
inline constexpr const char kUserLayerBackupSuffix[] = ".backup.json";

// The shipped core pack, from the bytes the repository read (the Qt resource, or the file
// PINPOINT_CORE_PACK names). Read-only. `refs` is the bibliography the loader's book gate reads
// (loadPack()); null leaves the gate off.
std::unique_ptr<ICharacteristicPackProvider> makeResourcePackProvider(const RuleFile &core,
                                                                      const ReferenceSet *refs = nullptr);

// Which of the `*.json` files in the diagnostics directory are characteristic packs, in LAYERING
// ORDER: `user.json` first if present, then the rest alphabetically. Every *.json is a pack EXCEPT the
// sibling registries that share the directory (`*.norms.json`, `screens.json`, `drills.json`,
// `gate_words.json`, `references.json`, `contexts.json`) and the reset's copies (`*.backup.json`). Pure: the repository lists the directory and
// asks this which files to read, so the skip list and the order are ONE decision with one home.
QStringList characteristicPackFileOrder(const QStringList &jsonFileNames);

// ONE pack file's bytes. A missing file is not an error — no user pack is the normal case; an
// unreadable or malformed one is reported through report() and yields an empty pack, so a broken
// community pack costs the library that pack and nothing else.
std::unique_ptr<ICharacteristicPackProvider> makeFilePackProvider(
    const RuleFile &packFile, PackOrigin origin = PackOrigin::LocalUser,
    const ReferenceSet *refs = nullptr);

// One provider per pack file, in the order given (characteristicPackFileOrder()), with the origin each
// file's name implies: `user.json` is LocalUser (the editor writes it), everything else is Community
// (it was imported from someone else). Feed the result straight to makeMergedPackProvider().
//
// One provider PER FILE, which is what the merger has always expected: a single directory-wide provider
// can only hold one pack, so every pack after the first was parsed, had its faults reported, and then
// had its content silently dropped.
std::vector<std::unique_ptr<ICharacteristicPackProvider>> makeFilePackProviders(
    const std::vector<RuleFile> &files, const ReferenceSet *refs = nullptr);

// The user's own pack with the swing-area regroup applied (characteristic_pack.h), or nothing when it
// is already current or is not a JSON object. Pure: the repository applies it once, after taking a
// backup, before it reads the user's pack.
std::optional<QByteArray> userPackRegrouped(const QByteArray &bytes);

// Core + user layers, in that order, and any number of user layers. Collision policy depends on
// origin (see PackOrigin):
//   * LocalUser entities keep their ids and REPLACE a core entity with the same id — an override.
//     They are not prefixed, because a prefix would make overriding impossible.
//   * Community entities are prefixed with "<packId>:", and on a surviving collision CORE WINS,
//     with the loser reported as a warning.
std::unique_ptr<ICharacteristicPackProvider> makeMergedPackProvider(
    std::unique_ptr<ICharacteristicPackProvider> core,
    std::vector<std::unique_ptr<ICharacteristicPackProvider>> user);

// The default assembly — shipped core plus whatever the user has installed — is RuleData's snapshot
// (src/DomainModel/rule_data.h; refactor step 3.2, audit B-02): one assembly per change, shared by
// every reader, rather than one per caller.

// A provider over a pack that is already in memory, validated on construction exactly as a loaded
// one is.
//
// This exists for ONE reason and it is worth stating, because a provider that reads nothing looks
// like a test double: an editor holds a WORKING COPY of the user pack that has not been saved yet,
// and every surface showing that edit has to read an assembly containing it. Without this seam the
// only assembly available is the one on disk, so an unsaved edit is invisible until it is written —
// which forces per-field saves, and per-field saves are what make an undo stack impossible to
// promise anything with. Feed this to makeMergedPackProvider() beside a core provider and the
// result is "the library as it would be if you saved now".
std::unique_ptr<ICharacteristicPackProvider> makeMemoryPackProvider(
    const CharacteristicPack &pack, const QString &label = QString(),
    PackOrigin origin = PackOrigin::LocalUser);

} // namespace pinpoint::analysis
