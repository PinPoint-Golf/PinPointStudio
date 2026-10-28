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

#include "navigation_controller.h"

NavigationController::NavigationController(QObject *parent)
    : QObject(parent)
{
}

int  NavigationController::currentIndex() const { return m_current; }
bool NavigationController::canGoBack()    const { return !m_back.isEmpty(); }
bool NavigationController::canGoForward() const { return !m_forward.isEmpty(); }

void NavigationController::navigate(int index) { push(index); }

void NavigationController::navigateRail(int index)
{
    // Home rail button is a hard reset — clears history rather than extending it
    if (index == AppScreen::Home) { navigateHome(); return; }
    push(index);
}

void NavigationController::navigateHome()
{
    const bool hadHistory = !m_back.isEmpty() || !m_forward.isEmpty();
    m_back.clear();
    m_forward.clear();
    // Already on Home, setCurrentIndex() announces nothing — but the cleared
    // history still changes canGoBack/canGoForward.
    if (m_current == AppScreen::Home) {
        if (hadHistory) emit historyChanged();
        return;
    }
    setCurrentIndex(AppScreen::Home);
}

void NavigationController::back()
{
    if (m_back.isEmpty()) return;
    m_forward.append(m_current);
    setCurrentIndex(m_back.takeLast());
}

void NavigationController::forward()
{
    if (m_forward.isEmpty()) return;
    m_back.append(m_current);
    setCurrentIndex(m_forward.takeLast());
}

void NavigationController::push(int index)
{
    if (index == m_current) return;

    if (m_back.size() >= kMaxHistory)
        m_back.removeFirst();

    m_back.append(m_current);
    m_forward.clear();
    setCurrentIndex(index);
}

void NavigationController::setCurrentIndex(int index)
{
    if (m_current == index) return;
    m_current = index;
    emit currentIndexChanged();
    emit historyChanged();
}
