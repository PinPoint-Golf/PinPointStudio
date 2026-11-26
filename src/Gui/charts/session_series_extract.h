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

// THE SESSION SERIES' PURE HALF (refactor 5.5a): what the session's series (session_series.h) does to one swing's
// analysis and one ledger row, as functions, so the series, the presenters' reference read and the tests share one copy.
// Moved, not rewritten, from the Charts panel's old loader (SessionSeriesModel). Pure: no file, no QObject.
//
//   extractShot          one swing document's analysis → one ShotSeries (ordinal, shotId and diagnostics left for the
//                        join); null when it carries no analysis metrics, `whyNot` then the sentence `skipped` carries.
//                        Every time goes through windowTimeUs against the document's analysis origin (the replay's clock).
//   seriesRelUs          that one rule for one time.
//   folderShotId         a swing folder's swing number (swing_0007 → 7), −1 for none: SwingPaths::swingNumberOf.
//   approxShotBytes      the resident heap one shot holds, estimated (for sizing and the log line).
//   seriesAnnotateForBoard  the launch monitor board on a series (J-45): the board's club; a reading left out read as one
//                        the monitor never made (its "lm." series not read).
//   seriesDiagnosticsOf  one ledger row → the Charts' ChartShotFindings (each fired and clean condition's graded value
//                        and its driving reading, through condition_reading.h; the sequence checks; the wrist profile).
//   seriesLinkConditions every condition that fired on some shot of the snapshot, named, grouped and linked to the
//                        reading it grades (design §6.4).

#include "../../Diagnostics/derived/series/session_series_types.h"
#include "../../Diagnostics/derived/board_annotations.h"
#include "../../Diagnostics/derived/diagnostic_ledger.h"   // ShotRecord

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <memory>

namespace pinpoint::analysis { struct CharacteristicPack; }

namespace pinpoint::charts {

std::shared_ptr<ShotSeries> extractShot(const QJsonObject &root, const QString &swingDir, QString *whyNot = nullptr);
qint64 seriesRelUs(const QJsonValue &v, qint64 originUs);
int    folderShotId(const QString &swingDir);
qint64 approxShotBytes(const ShotSeries &shot);
void   seriesAnnotateForBoard(ShotSeries &shot, const pinpoint::analysis::BoardAnnotation &a);
ChartShotFindings seriesDiagnosticsOf(const pinpoint::analysis::ShotRecord &row, const pinpoint::analysis::CharacteristicPack *pack);
bool   seriesSameDiagnostics(const ChartShotFindings &a, const ChartShotFindings &b);
void   seriesLinkConditions(SessionSnapshot &snapshot, const pinpoint::analysis::CharacteristicPack *pack);

} // namespace pinpoint::charts
