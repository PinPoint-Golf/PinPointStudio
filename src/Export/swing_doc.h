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

#include <QJsonObject>
#include <QString>

namespace pinpoint::analysis { struct SwingAnalysis; }

namespace pinpoint {

// The single, unified per-shot document. Raw capture manifest and derived analysis
// live in ONE swing.json — no separate analysis.json. Written once, on the GUI thread,
// at the analyzer∥exporter join (ShotProcessor::maybeJoin), so the two concurrent
// workers never write the same file.
class SwingDocWriter {
public:
    // Compose `rawManifest` (the exporter's returned pinpoint.swing tree) with the
    // analyzer's `analysis` (inline, additive "analysis" object) and write atomically
    // to <swingDir>/swing.json. Bumps schema to pinpoint.swing/2. analysis==nullptr →
    // raw only (no "analysis"). Returns false (and sets *error) on write failure.
    static bool writeSwingJson(const QString &swingDir, const QJsonObject &rawManifest,
                               const analysis::SwingAnalysis *analysis,
                               QString *error = nullptr);
};

} // namespace pinpoint
