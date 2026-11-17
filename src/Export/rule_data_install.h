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

// How a process gets its one RuleData (refactor step 3.2; D5: DEC-170). Not a repository: what it hands
// back is the OWNER, RuleData, made over the one store (src/Export/rule_data_store.h), so a composition
// root never names the store and no view reaches it (P11).
//
//   makeRuleDataRepository   the store, for a caller that constructs a RuleData of its own over another
//                            directory (rule_data_test)
//   installProcessRuleData   the process's one: the app's composition root (main.cpp), an offline tool,
//                            a test's main. Makes RuleData over the default store, installs it
//                            (RuleData::install) and returns it. Call it on the main thread, before any
//                            reader, and after the PINPOINT_CORE_* seams are set. A second call returns
//                            the same one. It is NEVER DESTROYED: a worker still holding a snapshot's
//                            reference after main returns (the global pool drains in ~QCoreApplication)
//                            must not read freed memory, and the norm slot it feeds outlives main's locals.
//
// A FACTORY HEADER THE LINT DOES NOT COUNT AS A REPOSITORY INCLUDE (architecture_lint_test's
// isRepositoryHeader lists rule_data_repository.h and rule_data_store.h, not this): any src/ file could
// include it and call makeRuleDataRepository() with no A5 finding. Only main.cpp does. Recorded for Mark
// (3.2 NOTES, J10) rather than decided here.

#include "../DomainModel/rule_data.h"

#include <memory>

namespace pinpoint {

std::unique_ptr<RuleDataRepository> makeRuleDataRepository(const QString &dataDir = QString());
RuleData &installProcessRuleData(std::shared_ptr<const analysis::MetricCatalogue> catalogue = {});

} // namespace pinpoint
