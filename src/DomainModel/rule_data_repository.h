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

// RuleDataRepository — THE ONE DOOR TO THE PACKS AND NORMS (refactor step 3.2; Q-02 closed by D5: DEC-170,
// IM-S3-1; governance/artefacts/information_model.md OBJ-PACKS-NORMS, CLS-PACKS-NORMS, FILE-APP-01…05, SET-06,
// SET-15). RuleDataStore (src/Export/rule_data_store.h) implements it; RuleData (rule_data.h), the owner,
// is the only caller. The interface sits here, beside its owner (P4; D17: the owning object includes its
// repository).
//
//   read      the shipped content (FILE-APP-05: the Qt resource, or the files PINPOINT_CORE_* name), the
//             user's own layers (FILE-APP-01…03) and community sets (FILE-APP-04) in <AppData>/diagnostics/,
//             and SET-06 / SET-15 where they are stored today (ui/diagnosticsNormSetsOff,
//             ui/diagnosticsGradePolicy). Each read hands back VALUES the rules compute from; no caller
//             sees a file.
//   write     the user's own layers, each whole and atomically, and the two settings keys in place.
//   schema    checked on read (audit B-20): a user file that declares a schema newer than this build's
//             is read as before (its loader refuses what it cannot read) and kept READ-ONLY — the write
//             of that layer is refused, so this build cannot overwrite content it does not understand.
//
// Every write returns whether it landed, with the reason in the user's words when not (DEC-126).

#include "../Diagnostics/packs/drill_pack.h"
#include "../Diagnostics/packs/gate_words_pack.h"
#include "../Diagnostics/packs/layered_set.h"
#include "../Diagnostics/packs/pack_provider.h"
#include "../Diagnostics/packs/reference_pack.h"
#include "../Diagnostics/packs/screen_pack.h"
#include "../Diagnostics/norms/norm_provider.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace pinpoint::analysis {
struct FlightNorms;   // Diagnostics/derived/strike_from_flight.h — not included here: it brings the whole ledger with it
}

namespace pinpoint {

// The assembled characteristic library, its shipped layer alone (the editor's "does this ship?") and the
// user's own pack as PARSED (FILE-APP-01; refactor step 3.9): the pack editor's baseline. Parsed, not
// loaded: an overlay routinely fails standalone referential validation (its edges name core conditions it
// does not hold), so a validated read would hand the editor an empty pack and its first save would erase
// every override. Empty (a default pack) when the file is absent or cannot be parsed.
struct RulePacks {
    std::shared_ptr<const analysis::ICharacteristicPackProvider> assembled;
    std::shared_ptr<const analysis::ICharacteristicPackProvider> shipped;
    std::shared_ptr<const analysis::CharacteristicPack>          user;
};

// The assembled norm sets, the shipped set and context tree alone (the corridor editor's "does this
// ship?", one parse serving both) and the user's own norm set as parsed (FILE-APP-02; step 3.9), for the
// same reason as the user pack: parsed, not validated. Empty when absent or unparseable.
struct RuleNorms {
    std::shared_ptr<const analysis::INormProvider> assembled;
    std::shared_ptr<const analysis::INormProvider> shipped;
    std::shared_ptr<const analysis::NormPack>      user;
};

class RuleDataRepository {
public:
    virtual ~RuleDataRepository() = default;

    // ── Reads: values, each a fresh read of what is on disk now ──────────────────────────────
    virtual analysis::ContentLayers<analysis::ReferenceSet> loadReferences() = 0;
    virtual analysis::ContentLayers<analysis::DrillSet>     loadDrills()     = 0;
    virtual analysis::ContentLayers<analysis::ScreenSet>    loadScreens()    = 0;
    virtual analysis::ContentLayers<analysis::GateWordSet>  loadGateWords()  = 0;
    // `refs` is the bibliography the pack loader's book gate reads (loadPack()).
    virtual RulePacks                                       loadPacks(const analysis::ReferenceSet *refs) = 0;
    virtual RuleNorms                                       loadNorms()       = 0;
    virtual analysis::FlightNorms                           loadFlightNorms() = 0;
    virtual QStringList                                     readDisabledNormSets() = 0;   // SET-06
    virtual QString                                         readGradePolicy()      = 0;   // SET-15
    // The user files found newer than this build at their last read (B-20): their layers are read-only.
    virtual QStringList                                     newerUserFiles() const = 0;

    // ── Writes: the user's own layers and the two settings keys ──────────────────────────────
    virtual bool saveUserPack(const analysis::CharacteristicPack &pack, QString *whyNot) = 0;
    virtual bool saveUserNorms(const analysis::NormPack &norms, QString *whyNot)         = 0;
    virtual bool saveUserReferences(const analysis::ReferenceSet &set, QString *whyNot)  = 0;
    virtual bool saveUserDrills(const analysis::DrillSet &set, QString *whyNot)          = 0;
    virtual bool saveUserScreens(const analysis::ScreenSet &set, QString *whyNot)        = 0;
    virtual bool saveUserGateWords(const analysis::GateWordSet &set, QString *whyNot)    = 0;
    virtual void writeDisabledNormSets(const QStringList &sets) = 0;
    virtual void writeGradePolicy(const QString &name)          = 0;

    // A copy of each of the user's own layers that exists (user.json, user.norms.json, screens.json,
    // drills.json, gate_words.json), taken beside it before the pack editor's "Reset to the standard
    // model" replaces them (refactor step 3.9: the editor used to copy them itself). COPIED, never moved:
    // the save that follows writes a temporary and renames it over the original, and a moved original
    // would leave a moment with no file at all. A missing layer is a success with nothing to do. `stamp`
    // names the copies; `written` gets each copy's path. False, with the reason, at the first copy that
    // cannot be written, and the reset does not go ahead. Changes no content, so it publishes nothing.
    virtual bool backupUserLayers(const QString &stamp, QStringList *written, QString *whyNot) = 0;
};

} // namespace pinpoint
