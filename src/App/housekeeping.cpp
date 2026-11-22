/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "housekeeping.h"

#include "../Core/pp_debug.h"

#include <QDir>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace pinpoint {

Housekeeping::Housekeeping(HousekeepingDoors doors) : m_doors(std::move(doors)) {}

TrashPurge Housekeeping::purge(const QString &libraryRoot, const QDateTime &cutoff) const
{
    if (libraryRoot.isEmpty())
        return {};
    if (!m_doors.purgeTrash) {
        ppWarn() << "[Housekeeping] no purge door: the trash was not emptied";
        return {};
    }
    const TrashPurge p = m_doors.purgeTrash(libraryRoot, cutoff);
    const QString line = QStringLiteral("[Housekeeping] purged %1 trash entries %2%3")
                             .arg(p.removed)
                             .arg(cutoff.isValid() ? QStringLiteral("trashed before ") + cutoff.toString(Qt::ISODate)
                                                   : QStringLiteral("(all)"))
                             .arg(p.failed ? QStringLiteral(" - %1 could not be removed").arg(p.failed) : QString());
    ppInfo() << qPrintable(line);
    return p;
}

qint64 Housekeeping::libraryBytes(const QString &libraryRoot, const std::atomic_bool *abort) const
{
    if (libraryRoot.isEmpty() || !m_doors.libraryBytes)
        return -1;
    return m_doors.libraryBytes(libraryRoot, abort);
}

namespace {

// THE HAND-OVER TO THE OWNER'S THREAD. Qt's event queue is the transport; the visibility of what each side wrote is made
// explicit with standard mutexes (which ThreadSanitizer sees, unlike Qt's queue and semaphores): the poster holds
// g_post while it builds and posts the step, and the step takes g_post first, so everything the poster wrote is visible
// to it; the step's own writes reach the waiter through the hand-over's mutex.
std::mutex g_post;

struct Handoff {
    std::mutex              m;
    std::condition_variable cv;
    bool                    started = false;   // the owner's thread is running the step: it is never abandoned then
    bool                    done    = false;
};

// `fn` posted to `owner`'s thread, without waiting.
void postToOwner(QObject *owner, std::function<void()> fn)
{
    const std::lock_guard<std::mutex> posting(g_post);
    QMetaObject::invokeMethod(owner, [fn = std::move(fn)] {
        { const std::lock_guard<std::mutex> after(g_post); }   // the poster has let go: its writes are visible
        fn();
    }, Qt::QueuedConnection);
}

// `fn` on `owner`'s thread, waited for; false when `abort` was set before the step STARTED. A step that has started is
// always waited for to its end (review 2, A2: abandoning one mid-way would leave what it did — a use added, a Session
// loaded — with nobody to undo it); the owner's thread is running it, so the wait cannot deadlock. A step that has not
// started when the pass gives up finds `gaveUp` set and does nothing (a teardown never meets a step the pass let go of).
bool onOwner(QObject *owner, const std::function<void()> &fn, const std::atomic_bool *abort,
             const std::shared_ptr<std::atomic_bool> &gaveUp)
{
    if (!owner || QThread::currentThread() == owner->thread()) {
        fn();
        return true;
    }
    const auto h = std::make_shared<Handoff>();
    postToOwner(owner, [fn, h, gaveUp] {
        {
            const std::lock_guard<std::mutex> l(h->m);
            if (gaveUp->load()) {
                h->done = true;
                h->cv.notify_one();
                return;
            }
            h->started = true;
        }
        fn();
        {
            const std::lock_guard<std::mutex> l(h->m);
            h->done = true;
        }
        h->cv.notify_one();
    });
    std::unique_lock<std::mutex> l(h->m);
    while (!h->done) {
        h->cv.wait_for(l, std::chrono::milliseconds(50));
        if (!h->done && !h->started && abort && abort->load()) {
            gaveUp->store(true);   // under h->m: the step sees it before it can start
            return false;
        }
    }
    return true;
}

// What the owner's thread found for one folder.
struct Prepared {
    enum class Kind { Unheld, Held, InProgress, Refused } kind = Kind::Unheld;
    SessionArchiveJob  job;
    QPointer<Session>  session;
    QPointer<Athlete>  athlete;
    bool               wasLoaded = false;   // its Athlete held it before the pass asked
};

// The pass's view of the athletes, made on the owner's thread on first need: the app's own instances (DEC-52, DEC-190).
struct Athletes {
    bool                    listed = false;
    QList<QPointer<Athlete>> all;
};

} // namespace

Housekeeping::Report Housekeeping::run(const Plan &plan, const LibraryWalk &walk, QObject *owner,
                                       const std::atomic_bool *abort, const ArchiveWrap &wrap) const
{
    Report rep;
    if (!plan.anythingOn())
        return rep;
    const auto stopped = [abort] { return abort && abort->load(); };
    const QDate     today = plan.today.isValid() ? plan.today : QDate::currentDate();
    const QDateTime now   = plan.now.isValid() ? plan.now : QDateTime::currentDateTime();

    // 1. The trash, past its retention (SET-10): the one purge.
    if (plan.trashRetentionDays > 0) {
        const TrashPurge p = purge(plan.libraryRoot, now.addDays(-plan.trashRetentionDays));
        rep.trashRemoved = p.removed;
        rep.trashFailed  = p.failed;
    }
    if (!plan.canArchive() || stopped())
        return rep;
    if (!walk.scan) {
        ppWarn() << "[Housekeeping] no walk of the library: nothing archived";
        return rep;
    }

    // Today's walk: every session folder with a swing, newest first (then folder, descending), its day as the tab lists it.
    QList<LibraryWalk::Folder> sessions = walk.scan(plan.libraryRoot);
    const QString live      = plan.liveFolder.isEmpty() ? QString() : QDir::cleanPath(plan.liveFolder);
    const qint64  todayMs   = today.startOfDay().toMSecsSinceEpoch();
    const auto    gaveUp    = std::make_shared<std::atomic_bool>(false);
    const auto    athletes  = std::make_shared<Athletes>();
    const AthleteCallables callables = m_doors.athletes;
    const SessionArchiveOptions options{ plan.archiveLocation, plan.keepRaw };
    // Today's sessions are never touched (one may still be recording), nor the live one, nor one already archived.
    const auto leftAlone = [&](const LibraryWalk::Folder &f) {
        return f.archived || f.dateMs >= todayMs || (!live.isEmpty() && f.folder == live);
    };

    // One folder: its Session's archive (the app's instance) when an Athlete holds it (the Unassigned owner's for a no-golfer
    // session: §5.11), else today's path for a folder no Athlete holds. False: not archived.
    const auto archiveOne = [&](LibraryWalk::Folder &f) -> bool {
        auto prep = std::make_shared<Prepared>();
        const QString folder = f.folder;
        const bool reached = onOwner(owner, [prep, athletes, callables, folder, options] {
            if (!athletes->listed) {
                athletes->listed = true;
                if (callables.listAthletes && callables.loadAthlete)
                    for (const AthleteId &id : callables.listAthletes())
                        if (Athlete *a = callables.loadAthlete(id))
                            athletes->all.append(a);
            }
            for (const QPointer<Athlete> &a : std::as_const(athletes->all)) {
                // Only a folder in this athlete's folder can be one of its sessions (path text: nothing read) — or, for the
                // Unassigned owner, in its other folder (the no-golfer `unknown/` an older build left: §5.11, J-66).
                if (!a)
                    continue;
                const QString parent = QDir::cleanPath(folder + QStringLiteral("/.."));
                const QStringList others = a->otherFolders();
                if (parent != QDir::cleanPath(a->folder())
                    && std::none_of(others.cbegin(), others.cend(),
                                    [&parent](const QString &o) { return QDir::cleanPath(o) == parent; }))
                    continue;
                const auto loaded = a->loadedSessions();
                prep->wasLoaded = std::any_of(loaded.cbegin(), loaded.cend(), [&folder](Session *s) {
                    const auto e = s->entry();
                    return e && QDir::cleanPath(e->folder) == folder;
                });
                Session *s = a->sessionAt(folder);
                if (!s)
                    continue;
                prep->session = s;
                prep->athlete = a.data();
                if (s->inProgress()) {
                    prep->kind = Prepared::Kind::InProgress;
                    return;
                }
                prep->job  = s->archiveJob(options);
                prep->kind = prep->job ? Prepared::Kind::Held : Prepared::Kind::Refused;
                if (prep->job)
                    s->addUse();   // held while its job runs (Q-18: not released under it)
                return;
            }
        }, abort, gaveUp);
        if (!reached) {
            rep.stopped = true;
            return false;
        }

        switch (prep->kind) {
        case Prepared::Kind::InProgress:
            ++rep.inProgress;
            ppInfo() << "[Housekeeping] left alone: in progress:" << folder;
            return false;
        case Prepared::Kind::Refused:
            ++rep.archiveFailed;
            rep.why << QStringLiteral("%1: %2").arg(folder, prep->job.refused.error);
            return false;
        case Prepared::Kind::Held: {
            // The job here, on this thread: values only. Then the result back to the Session, on its own thread.
            const SessionArchiveJob job = prep->job;
            const auto act = [&job] { return job.run({}); };
            const SessionArchiveResult r = wrap ? wrap(folder, act) : act();
            const auto land = [prep, r] {
                Session *s = prep->session;
                if (!s)
                    return;   // let go meanwhile: its next load reads the marker
                s->archiveLanded(r);   // EVT-18 on success
                s->dropUse();
                if (!prep->wasLoaded && prep->athlete && s->uses() == 0)
                    prep->athlete->releaseSession(s->id());   // the pass loaded it: let it go again
            };
            if (!owner || QThread::currentThread() == owner->thread())
                land();
            else
                postToOwner(owner, land);
            if (!r.ok) {
                ++rep.archiveFailed;
                rep.why << QStringLiteral("%1: %2").arg(folder, r.error);
                return false;
            }
            f.archived = true;
            ++rep.archived;
            return true;
        }
        case Prepared::Kind::Unheld: {
            if (!walk.archiveUnheld)
                return false;
            const auto act = [&walk, &folder, &options] { return walk.archiveUnheld(folder, options); };
            const SessionArchiveResult r = wrap ? wrap(folder, act) : act();
            if (!r.ok) {
                ++rep.archiveFailed;
                rep.why << QStringLiteral("%1: %2").arg(folder, r.error);
                return false;
            }
            f.archived = true;
            ++rep.archived;
            ++rep.archivedUnheld;
            return true;
        }
        }
        return false;
    };

    // 2. By age (SET-11).
    if (plan.archiveAfterDays > 0) {
        const qint64 cutoff = today.addDays(-plan.archiveAfterDays).startOfDay().toMSecsSinceEpoch();
        for (LibraryWalk::Folder &f : sessions) {
            if (stopped() || rep.stopped)
                break;
            if (leftAlone(f) || f.dateMs >= cutoff)
                continue;
            archiveOne(f);
        }
    }
    // 3. Below the floor (SET-12): the oldest first, until there is room again or nothing is left.
    if (plan.archiveFloorGb > 0) {
        const qint64 floor = qint64(plan.archiveFloorGb) * 1024 * 1024 * 1024;
        for (auto it = sessions.rbegin(); it != sessions.rend(); ++it) {
            if (stopped() || rep.stopped)
                break;
            if (plan.freeBytes && plan.freeBytes() >= floor)
                break;
            if (leftAlone(*it))
                continue;
            archiveOne(*it);
        }
    }
    if (stopped())
        rep.stopped = true;
    const QString line = QStringLiteral("[Housekeeping] pass: %1 trash entries purged, %2 sessions archived (%3 no Athlete holds)%4")
                             .arg(rep.trashRemoved).arg(rep.archived).arg(rep.archivedUnheld)
                             .arg(rep.archiveFailed ? QStringLiteral(" - %1 not archived").arg(rep.archiveFailed) : QString());
    ppInfo() << qPrintable(line);
    return rep;
}

} // namespace pinpoint
