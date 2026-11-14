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

// swing_doc_internal.h — NOT a public header. Only the three swing_doc_*.cpp files include it.
//
// The swing document's code is split by job (refactor step 1.0, 14 Nov 2026):
//   swing_doc_writer.cpp    SwingDocWriter, saveDocument, the launch monitor and integrity blocks
//   swing_doc_reader.cpp    SwingDocReader, the summary and its sidecar, the public read helpers
//   swing_doc_analysis.cpp  the `analysis` block, composed from the per-block serialisers (step 1.3a)
// These are the helpers more than one of them calls. They were file-local in swing_doc.cpp; they are
// declared here, in their own namespace, so each is still defined once.

#include "swing_doc.h"

#include <QJsonObject>
#include <QString>

namespace pinpoint::analysis { struct SwingAnalysis; }

namespace pinpoint::swingdoc_detail {

// swing_doc_analysis.cpp — the whole `analysis` block, composed from the per-block serialisers and
// stamped `timeBase: "window"`. A TimeBase::Clock analysis is taken to window time against windowT0
// (the document's clock.t0_us) through windowTimeUs; a TimeBase::Window one is written as it is.
// Called by SwingDocWriter::writeSwingJson.
QJsonObject serializeAnalysis(const analysis::SwingAnalysis &a, qint64 windowT0,
                              const QString &dtlAlias, const QString &dtlFile);

// swing_doc_reader.cpp — THE extractor for the session-picker scalars, from a document root.
// Called by the reader, and by the writer to build the `summary` block it saves with the document.
SwingSummary summaryFromRoot(const QJsonObject &root, const QString &swingDir);

// swing_doc_reader.cpp — the summary fields as JSON: the `summary` block of a swing.ppsw, and the
// body of a JSON-era swing_summary.json.
QJsonObject summaryJson(const SwingSummary &s);

} // namespace pinpoint::swingdoc_detail
