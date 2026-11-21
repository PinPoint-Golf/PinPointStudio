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

// THE CONSISTENCY OF A CONDITION'S MEASURE OVER LEDGER ROWS moved to the Rules ring (refactor step 3.8; J-34):
// src/Coaching/consistency_grade.h, unchanged — the one rule Home's consistency marks (AthleteDiagnostics::askConsistency,
// from the session summaries since refactor 3.7-f, J-52) and the report's work-on page (SessionReportModel) grade by.
// This header forwards, so the report keeps its include.

#include "../../Coaching/consistency_grade.h"
