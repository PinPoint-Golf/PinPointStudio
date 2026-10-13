/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariantMap>

// LmClubArt — the launch monitor panel's artwork, as a QML singleton.
//
// THE ART IS VENDORED, NOT DRAWN. The club heads, the ball and the impact patch are renders
// from the PinPoint Golf Resources repo (MIT), copied into src/Gui/resources/lm/ by
// tools/lm_art/vendor.sh and served at qrc:/lm/…; VENDORED.md there records the commit. This
// class is the panel's only way in: it names the files, and it hands over each render's
// ANCHORS — where the face centre, the leading edge, the shaft and the face frame sit in the
// image — so a figure can place, turn and annotate a head without measuring a pixel.
//
// THE ANCHORS ARE READ, NEVER COPIED. The signed-off mock carried a hand-copied table of every
// anchor; that is exactly the table that goes stale the first time a head is re-rendered. Here
// the vendored JSON is read at run time and its PIXEL fields are rescaled to the size of the
// image actually vendored (the renders are 2048 px; vendor.sh downsizes them), measured from
// the PNG's own header — so a re-vendor at a different size needs no change anywhere.
//
// WHAT IT DOES NOT DO: decide which club a shot was hit with (LmSessionModel's effective club),
// or what colour anything is (theme roles, in QML). It maps a club LABEL to a head through
// lmClubHead() in src/LaunchMonitor/lm_club_evidence.h — the same function the club evidence rule
// checks families with — so the drawing and the rule can never disagree about what "3 WOOD" is.
class LmClubArt : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit LmClubArt(QObject *parent = nullptr);

    // "driver" | "3wood" | "hybrid" | "iron" | "wedge", from a club label: the driver; any
    // wood; any hybrid; irons 2–9 → iron; PW and the wedges → wedge. Unrecognised → iron.
    Q_INVOKABLE QString headFor(const QString &club) const;

    // The render, or an empty url when that head has no such view (`back` exists for the iron
    // and the wedge only). view: top | side | face | back.
    // look: light | dark | line-black | line-white.
    Q_INVOKABLE QUrl image(const QString &head, const QString &view, const QString &look) const;

    // The anchor JSON for one head and view, as a map, with every PIXEL field rescaled to the
    // vendored image: image{width,height}, mm_per_px, world_to_px, face_centre, leading_edge,
    // face_plane, shaft_axis, ground_y, sole_contact, head_bbox, body_bbox, alpha_bbox, pivot,
    // face_frame{origin, x_px_per_mm, y_px_per_mm}. Directions (face_normal), angles and
    // millimetres pass through. Empty when the head has no such view.
    // Field meanings: ~/Projects/Resources/clubheads/README.md, "Anchor JSON".
    Q_INVOKABLE QVariantMap anchors(const QString &head, const QString &view) const;

    // Where Foresight's strike origin sits relative to our render's `face_centre`, UP THE FACE
    // PLANE, in mm (face_frame +y). GC Quad strike heights are measured from Foresight's face
    // centre, so a strike (h, v) is drawn at face_frame (h, v + strikeOriginMm(head)).
    // 7 iron +6.17, wedge −0.63, woods and hybrid 0. Derivation in the .cpp.
    Q_INVOKABLE double strikeOriginMm(const QString &head) const;

    // The plain ball, side view (the parting line level, the pole up — the view the spin axis
    // turns). look: light | dark | line-black | line-white | line-bold-black | line-bold-white.
    Q_INVOKABLE QUrl ballImage(const QString &look) const;

    // ball_side.json with its pixel fields rescaled to the vendored ball, as anchors() does
    // for a head: image, mm_per_px, centre, radius_px, parting_line.polyline.
    Q_INVOKABLE QVariantMap ballAnchors() const;

    // Foresight's club markers (the 5 mm retroreflective fiducial dots) for this head, as
    //   [{ name: "toe" | "heel" | "lo" | "hi" | "one", xMm, yMm }]
    // in face_frame mm measured from OUR face_centre (+x toward the toe, +y up the face plane) —
    // the same frame anchors().face_frame maps to pixels. toe/heel/lo/hi are the four-dot layout
    // (GC Quad, QuadMAX, Falcon, GCHawk); `one` is the single dot (GC3, HMT on its own). The panel
    // picks which to draw from graphics.device.markers. Derivation in the .cpp.
    Q_INVOKABLE QVariantList markers(const QString &head) const;

    // The impact patch, 20, 25 or 30 mm contact, as the tintable SVG (one evenodd path,
    // fill="currentColor"). Empty for any other size.
    Q_INVOKABLE QUrl patchImage(int mm) const;

    // The same patch as its bare path data (the SVG's one `d`), for a Shape that fills it in a
    // theme colour: units 0.01 mm, origin the contact centre, y down — a 32 × 32 mm canvas
    // (viewBox −1600 −1600 3200 3200). An Image cannot recolour an SVG's currentColor; a
    // ShapePath can, which is why this exists beside patchImage().
    Q_INVOKABLE QString patchPath(int mm) const;

    // The patch the rule calls for on this head: 25 mm for the woods and the hybrid (the
    // README's 50 m/s driver strike), 20 mm for the irons and wedges.
    Q_INVOKABLE int patchMm(const QString &head) const;

private:
    // "3wood" is the contract's word; the vendored files are Resources' "fairway".
    static QString dirFor(const QString &head);
    QSize vendoredSize(const QString &qrcPath) const;

    mutable QHash<QString, QVariantMap> m_anchorCache;
    mutable QHash<int, QString>         m_patchPathCache;
};
