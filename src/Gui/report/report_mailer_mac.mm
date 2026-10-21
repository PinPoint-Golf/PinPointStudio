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

#include "report_mailer.h"

// THE MAC HAND-OFF: NSSharingService's "compose email" — the service the Share menu's Mail item is,
// answered by whichever mail app the coach has set as their default (Mail, Outlook, Spark…). It opens a
// new message with the recipients, the subject, the body and the PDF attached; the coach reads it and
// presses Send from their own account, which is the point of option A (§8.3): it goes from the coach's
// own address, sits in their Sent folder, and replies come back to them.
//
// REPLY-TO CANNOT BE SET through NSSharingService — it has recipients and a subject and nothing else. It
// does not need to be: the sender IS the coach's own mail account, so a reply already goes to them. The
// argument is accepted for the platforms that can use it.
//
// AppKit, so the main thread (QML calls compose() from the GUI thread, which on macOS is the main one).
// ARC (CMakeLists.txt sets -fobjc-arc on this file).

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <QThread>
#include <QCoreApplication>

namespace pinpoint::report {

QString platformCompose(const QStringList &to, const QString &replyTo, const QString &subject,
                        const QString &body, const QString &filePath)
{
    Q_UNUSED(replyTo);
    if (QThread::currentThread() != QCoreApplication::instance()->thread())
        return QStringLiteral("The mail app can only be opened from the app's main window.");

    @autoreleasepool {
        NSSharingService *service = [NSSharingService sharingServiceNamed:NSSharingServiceNameComposeEmail];
        if (!service)
            return QStringLiteral("No mail app is set up on this Mac. Use Copy address and Show in Finder "
                                  "to attach the report by hand.");

        NSMutableArray<NSString *> *recipients = [NSMutableArray arrayWithCapacity:NSUInteger(to.size())];
        for (const QString &a : to) [recipients addObject:a.toNSString()];
        service.recipients = recipients;
        service.subject    = subject.toNSString();

        NSURL *file = [NSURL fileURLWithPath:filePath.toNSString()];
        NSArray *items = @[ body.toNSString(), file ];
        // False when no account is set up in the default mail app, or there is no default mail app.
        if (![service canPerformWithItems:items])
            return QStringLiteral("This Mac's mail app has no account set up, so it cannot open a new "
                                  "message. Use Copy address and Show in Finder to attach the report by hand.");

        // The service is asynchronous: the compose window opens after this returns. Held until the next
        // compose replaces it so ARC does not free it while Mail is still being asked.
        static NSSharingService *s_last = nil;
        s_last = service;
        [service performWithItems:items];
    }
    return {};
}

} // namespace pinpoint::report
