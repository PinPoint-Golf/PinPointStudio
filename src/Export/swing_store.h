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

// swing_store.h — THE door to a swing's document on disk.
//
// A swing directory holds its document in one of two formats:
//   swing.ppsw  the binary format (libppswing, docs/implementation/swing_storage_impl.md),
//               written by every writer since the Phase 2 switch (Sept 2026);
//   swing.json  the legacy text format, read indefinitely and never written again.
//
// Every reader and writer goes through here, so "is there a document", "how old is it" and
// "read it" have one answer whichever format the swing is in. A directory holding both is
// mid-conversion or was rewritten by this code, and swing.ppsw wins: it is always the newer.
//
// ⚠ THIS HEADER NAMES NO libppswing TYPE. It is included by translation units that compile
// into dozens of test binaries; the ppsw headers stay inside swing_store.cpp and ppsw_qt.cpp,
// the two files of the pinpoint_swingstore library every one of those targets links.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace pinpoint::SwingStore {

enum class Format { None, Ppsw, Json };

struct DocInfo {
    Format  format  = Format::None;
    QString path;          // the file that will be read; empty when there is none
    qint64  size    = 0;
    qint64  mtimeMs = 0;
    bool exists() const { return format != Format::None; }
};

QString ppswPath(const QString &swingDir);          // <dir>/swing.ppsw
QString jsonPath(const QString &swingDir);          // <dir>/swing.json
QString legacySummaryPath(const QString &swingDir); // <dir>/swing_summary.json (JSON-era sidecar)

DocInfo info(const QString &swingDir);
bool    hasDocument(const QString &swingDir);

// The whole document. Empty on no document or any read/parse failure; *error says which.
QJsonObject load(const QString &swingDir, QString *error = nullptr);

// Only the `summary` block, read from the root chunk of a swing.ppsw without decoding the rest.
// Empty for a JSON document, a .ppsw written without one, or a read failure.
QJsonObject loadSummaryBlock(const QString &swingDir);

// Write the document as swing.ppsw (atomic: temp file + rename), then remove any swing.json and
// swing_summary.json beside it — the rewrite supersedes both. Removing them is what migrates a
// JSON-era swing the first time anything rewrites it.
bool save(const QString &swingDir, const QJsonObject &root, QString *error = nullptr);

// The same encoding save() uses, to an arbitrary path (no sidecar cleanup). For tools that write
// a document somewhere other than a swing directory.
bool writeFile(const QString &path, const QJsonObject &root, QString *error = nullptr);

// Convert one JSON-era swing, proving the result before touching the original:
//   parse swing.json with libppswing's own parser (exact int/double, the path proven 170/170 on
//   the corpus) → add `summary` from summaryFn → write swing.ppsw → reopen it with a fresh
//   reader → semantic equality of (everything but /summary) against the parse.
// Only on EQUAL is swing.json (and swing_summary.json) deleted, and only when deleteJson is set.
// A failed verify removes the .ppsw it wrote and leaves the JSON exactly as it was.
struct ConvertResult {
    enum class Outcome { Converted, AlreadyPpsw, NoDocument, Failed };
    Outcome outcome   = Outcome::Failed;
    qint64  jsonBytes = 0;
    qint64  ppswBytes = 0;
    QString error;     // Failed: what failed, including the first differing JSON Pointer
};
using SummaryFn = std::function<QJsonObject(const QJsonObject &root, const QString &swingDir)>;
ConvertResult convertDir(const QString &swingDir, const SummaryFn &summaryFn, bool deleteJson);

// Every directory under root (root itself included) that holds a swing document, sorted.
// Dot-directories (.pinpoint-trash) are not entered: a trashed swing is the user's to restore.
QStringList swingDirsUnder(const QString &root);

// The document as compact JSON text — the interchange form (zip export, `ppsw dump`).
QByteArray toJsonText(const QJsonObject &root);

} // namespace pinpoint::SwingStore
