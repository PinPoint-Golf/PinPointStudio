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

// TYPED COACHING PLAN VALUES FOR QML (lint A6): Q_GADGET structs exposed by WorkOnsController::coachingPlan
// for Home's Coaching tab (HmSwingSummary.qml, HmFocus.qml). moc reaches this header through
// work_ons_controller.cpp's #include "moc_coaching_plan_types.cpp".

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace pinpoint::coachingui {

struct CoachingFaultItem
{
    Q_GADGET
    Q_PROPERTY(QString      conditionId     MEMBER conditionId     FINAL)
    Q_PROPERTY(QString      text            MEMBER text            FINAL)
    Q_PROPERTY(double       share           MEMBER share           FINAL)
    Q_PROPERTY(double       score           MEMBER score           FINAL)
    Q_PROPERTY(QString      frequency       MEMBER frequency       FINAL)
    Q_PROPERTY(int          trend           MEMBER trend           FINAL)
    Q_PROPERTY(QList<bool>  sessions        MEMBER sessions        FINAL)
    Q_PROPERTY(bool         isFocus         MEMBER isFocus         FINAL)
    Q_PROPERTY(QString      upstreamNote    MEMBER upstreamNote    FINAL)
    Q_PROPERTY(QString      onTheBall       MEMBER onTheBall       FINAL)
    Q_PROPERTY(QString      outcomeContrast MEMBER outcomeContrast FINAL)
public:
    QString     conditionId;
    QString     text;
    double      share = 0.0;
    double      score = 0.0;
    QString     frequency;
    int         trend = 0;
    QList<bool> sessions;
    bool        isFocus = false;
    QString     upstreamNote;
    QString     onTheBall;
    QString     outcomeContrast;
    friend bool operator==(const CoachingFaultItem &, const CoachingFaultItem &) = default;
};

struct CoachingFocusView
{
    Q_GADGET
    Q_PROPERTY(bool        present          MEMBER present          FINAL)
    Q_PROPERTY(QString     title            MEMBER title            FINAL)
    Q_PROPERTY(QString     stageKey         MEMBER stageKey         FINAL)
    Q_PROPERTY(QString     stageLabel       MEMBER stageLabel       FINAL)
    Q_PROPERTY(QStringList aimFor           MEMBER aimFor           FINAL)
    Q_PROPERTY(QList<pinpoint::coachingui::CoachingFaultItem> rightNow MEMBER rightNow FINAL)
    Q_PROPERTY(QString     why              MEMBER why              FINAL)
    Q_PROPERTY(QString     practiseLabel    MEMBER practiseLabel    FINAL)
    Q_PROPERTY(QString     practise         MEMBER practise         FINAL)
    Q_PROPERTY(QString     reasonShort      MEMBER reasonShort      FINAL)
    Q_PROPERTY(QString     focusConditionId MEMBER focusConditionId FINAL)
    Q_PROPERTY(QString     onTheBall        MEMBER onTheBall        FINAL)
    Q_PROPERTY(QString     outcomeContrast  MEMBER outcomeContrast  FINAL)
public:
    bool                     present = false;
    QString                  title;
    QString                  stageKey;
    QString                  stageLabel;
    QStringList              aimFor;
    QList<CoachingFaultItem> rightNow;
    QString                  why;
    QString                  practiseLabel;
    QString                  practise;
    QString                  reasonShort;
    QString                  focusConditionId;
    QString                  onTheBall;
    QString                  outcomeContrast;
    friend bool operator==(const CoachingFocusView &, const CoachingFocusView &) = default;
};

struct CoachingStageView
{
    Q_GADGET
    Q_PROPERTY(QString key        MEMBER key        FINAL)
    Q_PROPERTY(QString label      MEMBER label      FINAL)
    Q_PROPERTY(int     habitCount MEMBER habitCount FINAL)
    Q_PROPERTY(QList<pinpoint::coachingui::CoachingFaultItem> items MEMBER items FINAL)
public:
    QString                  key;
    QString                  label;
    int                      habitCount = 0;
    QList<CoachingFaultItem> items;
    friend bool operator==(const CoachingStageView &, const CoachingStageView &) = default;
};

struct CoachingPlanView
{
    Q_GADGET
    Q_PROPERTY(bool ready MEMBER ready FINAL)
    Q_PROPERTY(QList<pinpoint::coachingui::CoachingFaultItem> setupChecks MEMBER setupChecks FINAL)
    Q_PROPERTY(pinpoint::coachingui::CoachingFocusView        focus       MEMBER focus       FINAL)
    Q_PROPERTY(QList<pinpoint::coachingui::CoachingStageView> stages      MEMBER stages      FINAL)
public:
    bool                     ready = false;
    QList<CoachingFaultItem> setupChecks;
    CoachingFocusView        focus;
    QList<CoachingStageView> stages;
    friend bool operator==(const CoachingPlanView &, const CoachingPlanView &) = default;
};

} // namespace pinpoint::coachingui
