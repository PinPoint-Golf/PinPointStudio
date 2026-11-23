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

// THE TIMELINE LAYOUTS' TYPED RESULTS (refactor 5.8a, lint A6): what TimelineLabels' stationLayout and
// positionLayout hand QML, one Q_GADGET per entry, each property named as the old map's key (timeline_labels.h
// documents them). Every map carried every key, so a gadget reads the same. moc reaches this header through
// timeline_labels.cpp's #include of its moc file.

#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace pinpoint::timelineui {

// One phase station on the transit line: its dot (`center`) and its distributed label (`label`, a main-axis position).
struct Station
{
    Q_GADGET
    Q_PROPERTY(int     phase    MEMBER phase    FINAL)
    Q_PROPERTY(qint64  tUs      MEMBER tUs      FINAL)
    Q_PROPERTY(double  frac     MEMBER frac     FINAL)
    Q_PROPERTY(double  center   MEMBER center   FINAL)
    Q_PROPERTY(double  label    MEMBER label    FINAL)
    Q_PROPERTY(QString name     MEMBER name     FINAL)
    Q_PROPERTY(bool    isImpact MEMBER isImpact FINAL)
    Q_PROPERTY(bool    elbow    MEMBER elbow    FINAL)
public:
    int     phase = 0;
    qint64  tUs = 0;
    double  frac = 0, center = 0, label = 0;
    QString name;
    bool    isImpact = false, elbow = false;
    friend bool operator==(const Station &, const Station &) = default;
};

// One coaching P-position chip: its solved main-axis position (`center`) and its tag (`label`, "P1" … "P10").
struct PositionMark
{
    Q_GADGET
    Q_PROPERTY(int     p      MEMBER p      FINAL)
    Q_PROPERTY(qint64  tUs    MEMBER tUs    FINAL)
    Q_PROPERTY(double  frac   MEMBER frac   FINAL)
    Q_PROPERTY(double  center MEMBER center FINAL)
    Q_PROPERTY(QString label  MEMBER label  FINAL)
    Q_PROPERTY(int     source MEMBER source FINAL)
    Q_PROPERTY(double  conf   MEMBER conf   FINAL)
    Q_PROPERTY(bool    elbow  MEMBER elbow  FINAL)
public:
    int     p = 0;
    qint64  tUs = 0;
    double  frac = 0, center = 0;
    QString label;
    int     source = 0;
    double  conf = 0;
    bool    elbow = false;
    friend bool operator==(const PositionMark &, const PositionMark &) = default;
};

} // namespace pinpoint::timelineui
