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

// PPCHDELIVERYPICTURE — THE CLUB DELIVERY PICTURE STRIP OF THIS SHOT (docs/design/club_delivery_chart_design.md §9 stage 6;
// the twin of PpChPosturePicture): the golfer's own down-the-line frames at address, the top and P6, with the address line
// (solid, from the ball up the shaft), the shoulder plane (dashed, from the ball through the trail shoulder) and the slot
// between them (a faint wash), all FROZEN at address and drawn the same on every frame. ChartsDeliveryPresenter's
// thisShot.picture, placed (PpChPostureFrame per frame, its "poly" mark the slot); a press on a frame seeks the replay
// there (Analyse; the glance draws it with no affordance). The lines are the section's own reading — the plot's two
// hairlines — so they are drawn whatever the reader's Coach lines toggles are (the picture's defaultSets). Nothing at
// all with no down-the-line camera (no groups, nothing said). The strip is PpChPictureStrip (shared with Posture).
// objectNames: chDeliveryPicture, chDeliveryPictureFrame (×3: tag, seekUs, coachDrawn, polyDrawn, hasImage),
// chDeliveryPictureLegend, chDeliveryPictureWhy.
PpChPictureStrip {
    prefix: "chDelivery"
    title: "Address, the top and P6"
    coachAlpha: 0.9
}
