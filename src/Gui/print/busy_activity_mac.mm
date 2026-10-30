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

#include "busy_activity.h"

#import <Foundation/Foundation.h>

// CFBridgingRetain / CFBridgingRelease are right with or without ARC, so this file needs no flag.

BusyActivity::BusyActivity(const char *reason)
{
    @autoreleasepool {
        NSString *why = [NSString stringWithUTF8String:(reason ? reason : "PinPoint Studio is working")];
        id<NSObject> activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                              reason:why];
        m_token = const_cast<void *>(CFBridgingRetain(activity));
    }
}

BusyActivity::~BusyActivity()
{
    if (!m_token)
        return;
    @autoreleasepool {
        id<NSObject> activity = CFBridgingRelease(m_token);
        [[NSProcessInfo processInfo] endActivity:activity];
    }
}
