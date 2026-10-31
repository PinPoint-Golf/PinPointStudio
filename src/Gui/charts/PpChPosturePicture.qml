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
import QtQuick
import PinPointStudio

// PPCHPOSTUREPICTURE — THE PICTURE STRIP OF THIS SHOT (docs/design/posture_chart_design.md §5.4, stage 5): the golfer's
// own frames at address, the top and impact, down the line then face-on (each camera only where the swing has it), the
// address coach lines frozen on all three, the good-player bands at impact drawn as offsets of the golfer's own lines,
// and the golfer's own butt line at impact in ink. ChartsPosturePresenter's thisShot.picture, placed (PpChPostureFrame per
// frame); a press on a frame seeks the replay there (Analyse; the glance draws it with no affordance). The coach sets are
// the reader's Coach lines toggles when any is on, else the picture's own default sets (posture and head-and-hips):
// the strip is about posture, so it never goes blank because the replay's coach lines are off. The strip is
// PpChPictureStrip (shared with Club delivery).
// objectNames: chPosturePicture, chPosturePictureGroup (+ camera), chPosturePictureFrame (×n), chPosturePictureLegend,
// chPosturePictureWhy.
PpChPictureStrip {
    prefix: "chPosture"
    title: "Address, the top and impact"
    sets: ViewLayout.coachLinesOn(SessionMode.mode) ? ViewLayout.coachSetsFor(SessionMode.mode)
                                                    : (on && picture.defaultSets ? picture.defaultSets : ({}))
}
