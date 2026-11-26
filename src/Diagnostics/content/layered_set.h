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

#include "../model/pack_io.h"

#include <QFileInfo>

#include <algorithm>
#include <utility>
#include <vector>

// What the four flat content registries (drills, screens, references, gate words) do the same way:
// a shipped layer and the user's own, the user's rows replacing core's by id. Audit B-13: each of them
// held its own copy of the load / assemble / cache machinery and dropped its own load errors; refactor
// step 3.2 gives them this one copy, pure (the bytes come from the rule data's repository, RuleFile in
// pack_io.h), with the load report KEPT, and no cache: the one assembly per change is RuleData's
// snapshot (src/DomainModel/rule_data.h).

namespace pinpoint::analysis {

template <typename Set>
struct ContentLayers {
    Set              core;     // shipped only, read-only
    Set              user;     // the user's own layer alone; empty when there is no file
    Set              merged;   // core with the user's rows laid over it by id
    ValidationReport report;   // every load fault of both layers, which the old caches dropped
};

namespace detail {

// The SHIPPED layer. Its absence is an error (the build or the env override is wrong), reported in
// the words the pack and norm leaves use; whatever parsed is kept, as it always was.
template <typename Set>
Set shippedContent(const RuleFile &f, LoadResult<Set> (*load)(const QByteArray &, const QString &),
                   const QString &what, ValidationReport &report)
{
    Set out;
    if (f.state == RuleFile::State::Read) {
        LoadResult<Set> res = load(f.bytes, f.path);
        out                 = std::move(res.pack);
        for (ValidationIssue &i : res.report.issues) report.issues.push_back(std::move(i));
    } else {
        report.issues.push_back(ValidationIssue{
            IssueSeverity::Error, QStringLiteral("parse"), f.path,
            QStringLiteral("Could not open the core %1 at '%2'.").arg(what, f.path) });
    }
    out.readOnly = true;
    return out;
}

// The USER layer. Absent is normal and silent. `parsed`, NOT `loaded`: a user layer routinely fails
// STANDALONE validation (it holds two screens, not the shipped thirteen), and keying off `loaded`
// would silently drop every override the author had, then let the next save write the empty result
// over them.
template <typename Set>
Set userContent(const RuleFile &f, LoadResult<Set> (*load)(const QByteArray &, const QString &),
                const QString &what, ValidationReport &report)
{
    if (f.state == RuleFile::State::Unreadable)
        report.issues.push_back(ValidationIssue{
            IssueSeverity::Warning, QStringLiteral("parse"), f.path,
            QStringLiteral("Could not read %1 '%2'.").arg(what, QFileInfo(f.path).fileName()) });
    if (f.state != RuleFile::State::Read) return {};
    LoadResult<Set> res = load(f.bytes, f.path);
    for (ValidationIssue &i : res.report.issues) report.issues.push_back(std::move(i));
    return res.parsed ? std::move(res.pack) : Set{};
}

} // namespace detail

// Each user row REPLACES the shipped one with its id rather than merging field by field (a
// half-overridden protocol, or half of one paper's metadata carrying half of another's, is worse than
// either), and a row core does not carry is appended.
template <typename Row>
void overlayById(std::vector<Row> &into, const std::vector<Row> &from)
{
    for (const Row &row : from) {
        auto it = std::find_if(into.begin(), into.end(), [&](const Row &x) { return x.id == row.id; });
        if (it != into.end()) *it = row;
        else                  into.push_back(row);
    }
}

} // namespace pinpoint::analysis
