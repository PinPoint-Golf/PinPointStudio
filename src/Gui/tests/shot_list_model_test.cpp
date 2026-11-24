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

// ShotListModel — the carousel as a VIEW of one Session (refactor step 5.2; R-QML-5), over a test double of its port
// (fake_shot_source.h: the owner's events, no more). The real path, over a real Session, is shot_commands_test's.
//   S   shotSummary — the focused-shot metadata map behind PpShotActionBar: a valid id resolves every identity field;
//       an unknown / -1 id returns { valid:false } (present, false, bindable); a row a proxy would hide still resolves;
//       a card capture saved nothing for reports its flags without crashing.
//   V1  EVT-01 inserts one row, in swing-number order (newest first), no reset
//   V2  a Shot's events change that row only (dataChanged), a rewrite with the board's device role; the row keeps no
//       fact of the Shot's: it shows what the Shot holds now
//   V3  EVT-03 removes the row — the carousel's delete or anyone's — and shotTrashed says so once
//   V4  a rewrite (a re-analysis) keeps the row's id and place
//   V5  the same session shown again (End, re-entering a screen) changes nothing: no reset, the same ids
//   V6  another session: one reset; none: empty
//   V7  the Session gone (released, trashed): empty, and nothing dangles
//   V8  a card capture saved nothing for, then its document lands (a launch monitor's): one row, the same id
//   V9  a save that fails: said so, and the row shows the saved value; the rating sends no event (DEC-70)
//   V10 End lets go of the cards and of the live presentation, without a reset
//   V11 no row struct holds a Shot's facts: QML has no analysisDetail role, and no lmDetail is kept
//   V12 a card's edit the Shot did not save as its document landed stays on the card and is tried again (the Shot's
//       next event for that row, or the next document to land); the edits that saved are not re-sent; a later edit
//       (the row's own, even of the value shown, or another command's) wins over the kept one, a rewrite does not; a
//       second failure keeps it again (#235)

#include "shot/shot_list_model.h"
#include "fake_shot_source.h"

#include <QCoreApplication>
#include <QPointF>
#include <QSignalSpy>
#include <QThread>
#include <QVariantMap>
#include <QUrl>
#include <cstdio>

using pinpoint::ShotRef;
using pinpoint::ShotSummary;
using pinpoint::testdoc::MemoryShotSource;

static int g_fail = 0;

static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-58s got %-5s want %-5s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

static void checkInt(const char *label, long long got, long long want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-58s got %4lld  want %4lld\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}

static void checkStr(const char *label, const QString &got, const QString &want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-58s got \"%s\"  want \"%s\"\n",
                ok ? "PASS" : "FAIL", label, got.toUtf8().constData(), want.toUtf8().constData());
    if (!ok) ++g_fail;
}

static ShotSummary shot(const char *club, int score, const char *time, bool video = true)
{
    ShotSummary s;
    s.club = QString::fromLatin1(club);
    s.score = score;
    s.timestampLabel = QString::fromLatin1(time);
    s.hasVideo = video;
    return s;
}

static int rowOf(const ShotListModel &m, int id)
{
    for (int r = 0; r < m.rowCount(); ++r)
        if (m.data(m.index(r), ShotListModel::ShotIdRole).toInt() == id) return r;
    return -1;
}
static int ordinalAt(const ShotListModel &m, int r) { return m.data(m.index(r), ShotListModel::OrdinalRole).toInt(); }

// A view over a fresh fake session holding swings 1 and 2 (shown through the factory, as the app does).
struct Rig {
    MemoryShotSource *src = nullptr;   // owned by the view
    ShotListModel     model;
    explicit Rig(const QString &folder = QStringLiteral("/mem/s1"))
    {
        auto s = std::make_unique<MemoryShotSource>(folder);
        src = s.get();
        src->put(1, shot("7 IRON", 82, "13:42"));
        src->put(2, shot("DRIVER", 44, "13:45"));
        model.setSource(std::move(s));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("=== ShotListModel: a view of one Session ===\n\n");

    std::printf("-- S: shotSummary --\n");
    {
        Rig rig;
        ShotListModel &model = rig.model;
        const int id1 = model.idOfShotRef(rig.src->refOf(1));
        const int id2 = model.idOfShotRef(rig.src->refOf(2));
        model.setRating(id1, 4);   // the Shot's command: the row shows what the Shot now holds
        const QVariantMap s = model.shotSummary(id1);
        checkBool("valid",                s.value(QStringLiteral("valid")).toBool(), true);
        checkInt ("ordinal",              s.value(QStringLiteral("ordinal")).toInt(), 1);
        checkStr ("club",                 s.value(QStringLiteral("club")).toString(), QStringLiteral("7 IRON"));
        checkStr ("timestampLabel",       s.value(QStringLiteral("timestampLabel")).toString(), QStringLiteral("13:42"));
        checkInt ("score",                s.value(QStringLiteral("score")).toInt(), 82);
        checkInt ("rating (the Shot's, after its command)", s.value(QStringLiteral("rating")).toInt(), 4);
        checkBool("hasVideo",             s.value(QStringLiteral("hasVideo")).toBool(), true);
        checkStr ("swingDir (the Shot's folder)", s.value(QStringLiteral("swingDir")).toString(), QStringLiteral("/mem/s1/swing_0001"));
        checkBool("dataWarning (clean)",  s.value(QStringLiteral("dataWarning")).toBool(), false);
        const QVariantMap none = model.shotSummary(-1);
        checkBool("(-1) valid present & false", none.contains(QStringLiteral("valid")) && !none.value(QStringLiteral("valid")).toBool(), true);
        checkBool("(99999) valid false", model.shotSummary(99999).value(QStringLiteral("valid")).toBool(), false);
        const QVariantMap low = model.shotSummary(id2);   // score 44: a quality≥50 proxy would hide it
        checkStr ("model-scoped: a hidden row still resolves", low.value(QStringLiteral("club")).toString(), QStringLiteral("DRIVER"));
        // A warned shot carries its warning to the swing panel.
        ShotSummary w = shot("7 IRON", 70, "13:50");
        w.dataWarning = true;
        w.dataWarningDetail = { { QStringLiteral("clubRefused"), QStringLiteral("phaseSuspect") } };
        rig.src->land(rig.src->allocate(9), w);
        const QVariantMap ws = model.shotSummary(model.idOfShotRef(rig.src->refOf(9)));
        checkBool("dataWarning",          ws.value(QStringLiteral("dataWarning")).toBool(), true);
        checkStr ("dataWarningDetail.clubRefused",
                  ws.value(QStringLiteral("dataWarningDetail")).toMap().value(QStringLiteral("clubRefused")).toString(),
                  QStringLiteral("phaseSuspect"));
        // An IMU-only shot capture saved nothing for: a card, no folder.
        ShotListModel::CapturedShot c;
        c.timestampLabel = QStringLiteral("13:48"); c.club = QStringLiteral("PW"); c.score = 18;
        const int idC = model.landShot(c, /*documentWritten*/ false);
        const QVariantMap cs = model.shotSummary(idC);
        checkBool("card: valid",             cs.value(QStringLiteral("valid")).toBool(), true);
        checkBool("card: hasVideo == false", cs.value(QStringLiteral("hasVideo")).toBool(), false);
        checkStr ("card: swingDir empty",    cs.value(QStringLiteral("swingDir")).toString(), QString());
        checkInt ("card: score",             cs.value(QStringLiteral("score")).toInt(), 18);
        checkInt ("card: the next number this view has not issued", cs.value(QStringLiteral("ordinal")).toInt(), 10);
    }

    std::printf("\n-- V1: EVT-01 inserts one row, in swing-number order --\n");
    {
        Rig rig;
        QSignalSpy resets(&rig.model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(&rig.model, &QAbstractItemModel::rowsInserted);
        checkInt ("shown: the session's two shots", rig.model.rowCount(), 2);
        checkInt ("newest first", ordinalAt(rig.model, 0), 2);
        const ShotRef s3 = rig.src->allocate(3);
        checkInt ("an allocated shot with no document: no row", rig.model.rowCount(), 2);
        rig.src->land(s3, shot("7 IRON", 60, "13:50"));
        checkInt ("its document lands (EVT-01): one row", rig.model.rowCount(), 3);
        checkInt ("…one insert", inserts.count(), 1);
        checkInt ("…at the top", ordinalAt(rig.model, 0), 3);
        checkInt ("…no reset", resets.count(), 0);
        rig.src->land(s3, shot("7 IRON", 60, "13:50"));
        checkInt ("EVT-01 again for it: still one row", rig.model.rowCount(), 3);
        const int id = rig.model.landShot({ s3 }, true);
        checkInt ("capture's call finds that row (no second card)", id, rig.model.idOfShotRef(s3));
        checkInt ("…and the count is unchanged", rig.model.rowCount(), 3);
    }

    std::printf("\n-- V2: a Shot's event changes its row only; the row shows the Shot now --\n");
    {
        Rig rig;
        QSignalSpy changed(&rig.model, &QAbstractItemModel::dataChanged);
        const ShotRef s1 = rig.src->refOf(1);
        rig.src->setDeclaredClub(s1, QStringLiteral("6 IRON"));   // EVT-21, from elsewhere (a panel)
        checkInt ("one dataChanged", changed.count(), 1);
        const QModelIndex ix = changed.isEmpty() ? QModelIndex() : changed.first().at(0).toModelIndex();
        checkInt ("…of that row only", ix.row(), rowOf(rig.model, rig.model.idOfShotRef(s1)));
        checkStr ("the row shows the Shot's club now",
                  rig.model.data(rig.model.index(rowOf(rig.model, rig.model.idOfShotRef(s1))), ShotListModel::ClubRole).toString(),
                  QStringLiteral("6 IRON"));
        changed.clear();
        rig.src->rewrite(s1, [](ShotSummary &f) { f.lmDevice = QStringLiteral("gcquad"); f.score = 91; });   // EVT-22
        const QList<int> roles = changed.isEmpty() ? QList<int>{} : changed.first().at(2).value<QList<int>>();
        checkInt ("a rewrite: one dataChanged", changed.count(), 1);
        checkBool("…carrying the board's device role (its cache drops)", roles.contains(ShotListModel::LmDeviceRole), true);
        checkInt ("…and the new score shows",
                  rig.model.data(rig.model.index(rowOf(rig.model, rig.model.idOfShotRef(s1))), ShotListModel::ScoreRole).toInt(), 91);
        changed.clear();
        rig.model.setClub(rig.model.idOfShotRef(s1), QStringLiteral("5 IRON"));
        checkInt ("the carousel's own club edit: one dataChanged, not two", changed.count(), 1);
    }

    std::printf("\n-- V3: EVT-03 removes the row; shotTrashed once --\n");
    {
        Rig rig;
        rig.model.setTrashRetry(5, 200);
        QSignalSpy trashed(&rig.model, &ShotListModel::shotTrashed);
        QSignalSpy finished(&rig.model, &ShotListModel::trashFinished);
        const int id2 = rig.model.idOfShotRef(rig.src->refOf(2));
        rig.model.trashShots({ id2 });
        checkInt ("the carousel's delete: the row leaves", rig.model.rowCount(), 1);
        checkInt ("…shotTrashed once", trashed.count(), 1);
        checkInt ("…the request reported", finished.count(), 1);
        rig.src->trashElsewhere(rig.src->refOf(1));
        checkInt ("anyone's trash (EVT-03): the row leaves", rig.model.rowCount(), 0);
        checkInt ("…shotTrashed again, once", trashed.count(), 2);
        // A refused trash keeps the row, and is retried.
        Rig r2;
        r2.model.setTrashRetry(5, 400);
        r2.src->trashRefusals = 1;
        QSignalSpy fin2(&r2.model, &ShotListModel::trashFinished);
        r2.model.trashShots({ r2.model.idOfShotRef(r2.src->refOf(1)) });
        checkInt ("refused at first: the row stays", r2.model.rowCount(), 2);
        for (int i = 0; i < 50 && fin2.isEmpty(); ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10); }
        checkInt ("…and goes on the retry", r2.model.rowCount(), 1);
        checkInt ("…reported moved", fin2.isEmpty() ? -1 : fin2.first().at(1).toInt(), 1);
    }

    std::printf("\n-- V4: a re-analysis keeps the row's id and place --\n");
    {
        Rig rig;
        const ShotRef s1 = rig.src->refOf(1);
        const int id = rig.model.idOfShotRef(s1), row = rowOf(rig.model, id);
        QSignalSpy resets(&rig.model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&rig.model, &QAbstractItemModel::rowsRemoved);
        rig.src->rewrite(s1, [](ShotSummary &f) { f.score = 55; f.metrics = { { QStringLiteral("k"), 1 } }; });   // EVT-02
        checkInt ("the same id", rig.model.idOfShotRef(s1), id);
        checkInt ("the same place", rowOf(rig.model, id), row);
        checkInt ("no reset, no removal", resets.count() + removed.count(), 0);
        checkStr ("the shot's ref is still the selection's edge", rig.model.shotRefOf(id), s1.toString());
    }

    std::printf("\n-- V5/V6/V7: the session shown --\n");
    {
        int made = 0;
        ShotListModel m;
        m.setSourceFactory([&made](const QString &folder) -> std::unique_ptr<pinpoint::ShotSource> {
            ++made;
            auto s = std::make_unique<MemoryShotSource>(folder);
            s->put(1, shot("7 IRON", 80, "10:00"));
            return s;
        });
        QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
        m.showSession(QStringLiteral("/mem/today"));
        const int id = m.idOfShotRef(ShotRef());   // none: -1
        checkInt ("shown: one reset, its shot", resets.count() * 10 + m.rowCount(), 11);
        const int id1 = m.data(m.index(0), ShotListModel::ShotIdRole).toInt();
        m.showSession(QStringLiteral("/mem/today/"));   // End, re-entering the screen: the same session
        checkInt ("V5 the same session again: no reset", resets.count(), 1);
        checkInt ("V5 …no new source made", made, 1);
        checkInt ("V5 …the same id", m.data(m.index(0), ShotListModel::ShotIdRole).toInt(), id1);
        checkStr ("V5 …its folder", m.sessionFolder(), QStringLiteral("/mem/today"));
        m.showSession(QStringLiteral("/mem/other"));
        checkInt ("V6 another session: one reset", resets.count(), 2);
        m.showSession(QString());
        checkInt ("V6 none: empty", m.rowCount(), 0);
        checkBool("V6 …no session", m.sessionFolder().isEmpty() && id < 0, true);

        Rig rig;
        rig.src->goneNow();   // released, trashed (EVT-20) or moved away
        checkInt ("V7 the Session gone: empty", rig.model.rowCount(), 0);
        checkBool("V7 …no session", rig.model.sessionFolder().isEmpty(), true);
        checkBool("V7 …nothing reads it (a stale id)", rig.model.shotSummary(1).value(QStringLiteral("valid")).toBool(), false);
        QCoreApplication::processEvents();   // the old source goes on the next turn
    }

    std::printf("\n-- V8: a card, then its document lands: one row --\n");
    {
        Rig rig;
        const ShotRef s3 = rig.src->allocate(3);
        ShotListModel::CapturedShot c;
        c.shot = s3; c.timestampLabel = QStringLiteral("14:00"); c.club = QStringLiteral("7 IRON");
        const int id = rig.model.landShot(c, /*documentWritten*/ false);
        checkInt ("nothing saved: a card", rig.model.rowCount(), 3);
        checkBool("…not a saved shot's", rig.model.data(rig.model.index(rowOf(rig.model, id)), ShotListModel::SwingDirRole).toString().isEmpty(), true);
        ShotSummary dev = shot("7 IRON", 0, "14:00", false);
        dev.lmDevice = QStringLiteral("gcquad");
        rig.src->land(s3, dev);   // the launch monitor's device-only document: EVT-01
        checkInt ("its document lands: still three rows", rig.model.rowCount(), 3);
        checkInt ("…the same id", rig.model.idOfShotRef(s3), id);
        checkStr ("…now its Shot's", rig.model.data(rig.model.index(rowOf(rig.model, id)), ShotListModel::LmDeviceRole).toString(),
                  QStringLiteral("gcquad"));
        checkBool("…with its folder", !rig.model.data(rig.model.index(rowOf(rig.model, id)), ShotListModel::SwingDirRole).toString().isEmpty(), true);
        // refactor 5.10: only what the golfer edited on a card is handed to the Shot — the capture's own values are not
        // (an untouched card leaves the document's club, rating and note as they are).
        const ShotRef s4 = rig.src->allocate(4);
        ShotListModel::CapturedShot c4;
        c4.shot = s4; c4.club = QStringLiteral("PW");
        const int id4 = rig.model.landShot(c4, false);
        rig.model.setNote(id4, QStringLiteral("on the card"));
        ShotSummary dev4 = shot("8 IRON", 0, "14:05", false);
        dev4.rating = 2;
        rig.src->land(s4, dev4);
        const QModelIndex ix4 = rig.model.index(rowOf(rig.model, id4));
        checkStr ("5.10 the card's note is the Shot's", rig.model.data(ix4, ShotListModel::NoteRole).toString(), QStringLiteral("on the card"));
        checkStr ("5.10 …the capture's club is not handed over", rig.model.data(ix4, ShotListModel::ClubRole).toString(), QStringLiteral("8 IRON"));
        checkInt ("5.10 …nor an untouched rating", rig.model.data(ix4, ShotListModel::RatingRole).toInt(), 2);
    }

    std::printf("\n-- V9: a failed save; the rating's own refresh --\n");
    {
        Rig rig;
        QSignalSpy failed(&rig.model, &ShotListModel::saveFailed);
        QSignalSpy changed(&rig.model, &QAbstractItemModel::dataChanged);
        const int id1 = rig.model.idOfShotRef(rig.src->refOf(1));
        rig.src->failSaves = true;
        rig.model.setNote(id1, QStringLiteral("not saved"));
        checkInt ("said so", failed.count(), 1);
        checkStr ("…as the edit", failed.isEmpty() ? QString() : failed.first().at(0).toString(), QStringLiteral("The note"));
        checkStr ("the row shows the saved value", rig.model.data(rig.model.index(rowOf(rig.model, id1)), ShotListModel::NoteRole).toString(), QString());
        checkInt ("…announced once (the view puts the control back)", changed.count(), 1);
        rig.src->failSaves = false;
        changed.clear();
        rig.model.setRating(id1, 3);
        checkInt ("the rating (no event: DEC-70): the view's own one refresh", changed.count(), 1);
        checkInt ("…showing the Shot's", rig.model.data(rig.model.index(rowOf(rig.model, id1)), ShotListModel::RatingRole).toInt(), 3);
    }

    std::printf("\n-- V12: a card's edit the Shot did not save stays on the card, and is tried again (#235) --\n");
    {
        Rig rig;
        QSignalSpy failed(&rig.model, &ShotListModel::saveFailed);
        auto noteOf = [&rig](const ShotRef &s) {
            return rig.model.data(rig.model.index(rowOf(rig.model, rig.model.idOfShotRef(s))), ShotListModel::NoteRole).toString(); };
        auto clubOf = [&rig](const ShotRef &s) {
            return rig.model.data(rig.model.index(rowOf(rig.model, rig.model.idOfShotRef(s))), ShotListModel::RecordedClubRole).toString(); };
        auto card = [&rig](const ShotRef &s) { ShotListModel::CapturedShot c; c.shot = s; c.club = QStringLiteral("PW");
                                               return rig.model.landShot(c, false); };
        // A card with three edits; its note's save fails as the document lands.
        const ShotRef s5 = rig.src->allocate(5);
        const int id5 = card(s5);
        rig.model.setClub(id5, QStringLiteral("6 IRON"));
        rig.model.setRating(id5, 4);
        rig.model.setNote(id5, QStringLiteral("keep me"));
        rig.src->failing = { QStringLiteral("note") };
        rig.src->land(s5, shot("8 IRON", 0, "14:10", false));
        checkInt ("the failed note: said so, once", failed.count(), 1);
        checkStr ("…as the edit", failed.isEmpty() ? QString() : failed.first().at(0).toString(), QStringLiteral("The note"));
        checkStr ("…the Shot does not hold it", noteOf(s5), QString());
        checkStr ("the club that saved is the Shot's", clubOf(s5), QStringLiteral("6 IRON"));
        checkInt ("…and the rating", rig.model.data(rig.model.index(rowOf(rig.model, id5)), ShotListModel::RatingRole).toInt(), 4);
        // The Shot's next event for that row: the note, kept on the card, is handed over again.
        rig.src->failing.clear();
        rig.src->rewrite(s5, [](ShotSummary &) {});
        checkStr ("the Shot's next event: the kept note is saved", noteOf(s5), QStringLiteral("keep me"));
        checkInt ("…the note sent twice in all", rig.src->calls.value(QStringLiteral("note")), 2);
        checkInt ("…the club that saved is not re-sent", rig.src->calls.value(QStringLiteral("setDeclaredClub")), 1);
        checkInt ("…nor the rating", rig.src->calls.value(QStringLiteral("rate")), 1);
        checkInt ("…no new failure told", failed.count(), 1);
        rig.src->rewrite(s5, [](ShotSummary &) {});
        checkInt ("saved: the card holds it no more (not sent again)", rig.src->calls.value(QStringLiteral("note")), 2);

        // The note sends no event (DEC-70): the next document to land, any shot's, hands it over.
        rig.src->calls.clear();
        const ShotRef s6 = rig.src->allocate(6);
        rig.model.setNote(card(s6), QStringLiteral("six"));
        rig.src->failing = { QStringLiteral("note") };
        rig.src->land(s6, shot("8 IRON", 0, "14:12", false));
        checkStr ("a second card's note fails as it lands", noteOf(s6), QString());
        rig.src->failing.clear();
        const ShotRef s7 = rig.src->allocate(7);
        rig.src->land(s7, shot("8 IRON", 0, "14:14", false));
        checkStr ("another shot's document lands: the kept note is saved", noteOf(s6), QStringLiteral("six"));

        // A later edit wins: the golfer's own, on the carousel …
        rig.src->calls.clear();
        const ShotRef s8 = rig.src->allocate(8);
        rig.model.setNote(card(s8), QStringLiteral("old"));
        rig.src->failing = { QStringLiteral("note") };
        rig.src->land(s8, shot("8 IRON", 0, "14:16", false));
        rig.src->failing.clear();
        rig.model.setNote(rig.model.idOfShotRef(s8), QStringLiteral("new"));
        rig.src->rewrite(s8, [](ShotSummary &) {});
        checkStr ("a later edit of the row wins over the kept one", noteOf(s8), QStringLiteral("new"));
        checkInt ("…the kept one is not sent again", rig.src->calls.value(QStringLiteral("note")), 2);
        // … or one made elsewhere (a panel's club: EVT-21).
        rig.src->calls.clear();
        const ShotRef s9 = rig.src->allocate(9);
        rig.model.setClub(card(s9), QStringLiteral("6 IRON"));
        rig.src->failing = { QStringLiteral("setDeclaredClub") };
        rig.src->land(s9, shot("8 IRON", 0, "14:18", false));
        rig.src->failing.clear();
        rig.src->setDeclaredClub(s9, QStringLiteral("5 IRON"));
        checkStr ("a later edit from elsewhere wins over the kept one", clubOf(s9), QStringLiteral("5 IRON"));
        checkInt ("…the kept one is not sent again", rig.src->calls.value(QStringLiteral("setDeclaredClub")), 2);
        // A rewrite is not an edit: a late part that moves the club (follow-club) does not beat the golfer's kept club.
        rig.src->calls.clear();
        const ShotRef s10 = rig.src->allocate(10);
        rig.model.setClub(card(s10), QStringLiteral("6 IRON"));
        rig.src->failing = { QStringLiteral("setDeclaredClub") };
        rig.src->land(s10, shot("8 IRON", 0, "14:20", false));
        rig.src->failing.clear();
        rig.src->rewrite(s10, [](ShotSummary &f) { f.club = QStringLiteral("7 IRON"); });
        checkStr ("a rewrite that moves the club: the golfer's kept club still wins", clubOf(s10), QStringLiteral("6 IRON"));
        checkInt ("…sent again once", rig.src->calls.value(QStringLiteral("setDeclaredClub")), 2);

        // Re-picking the value shown is a later edit too: the kept one goes, and the shown value stands.
        rig.src->calls.clear();
        const ShotRef s11 = rig.src->allocate(11);
        rig.model.setClub(card(s11), QStringLiteral("6 IRON"));
        rig.src->failing = { QStringLiteral("setDeclaredClub") };
        rig.src->land(s11, shot("8 IRON", 0, "14:22", false));
        rig.src->failing.clear();
        rig.model.setClub(rig.model.idOfShotRef(s11), QStringLiteral("8 IRON"));   // the club shown, picked again
        rig.src->land(rig.src->allocate(12), shot("8 IRON", 0, "14:24", false));
        checkStr ("re-picking the club shown: it stands", clubOf(s11), QStringLiteral("8 IRON"));
        checkInt ("…the kept club is not sent again", rig.src->calls.value(QStringLiteral("setDeclaredClub")), 1);

        // Another field's event tries the kept edit (here the club correction), the compare passing.
        rig.src->calls.clear();
        const ShotRef s13 = rig.src->allocate(13);
        rig.model.setLmClubOverride(card(s13), QStringLiteral("7 IRON"));
        rig.src->failing = { QStringLiteral("correctClub") };
        rig.src->land(s13, shot("8 IRON", 0, "14:26", false));
        rig.src->failing.clear();
        rig.src->excludeReading(s13, true);   // EVT-05, from elsewhere
        checkStr ("another field's event: the kept correction is saved",
                  rig.model.data(rig.model.index(rowOf(rig.model, rig.model.idOfShotRef(s13))), ShotListModel::LmClubOverrideRole).toString(),
                  QStringLiteral("7 IRON"));
        checkInt ("…sent twice in all", rig.src->calls.value(QStringLiteral("correctClub")), 2);

        // A second failure keeps it again, and says so again; the next try saves it.
        rig.src->calls.clear();
        const ShotRef s14 = rig.src->allocate(14);
        rig.model.setNote(card(s14), QStringLiteral("twice"));
        rig.src->failing = { QStringLiteral("note") };
        rig.src->land(s14, shot("8 IRON", 0, "14:28", false));
        const int toldBefore = failed.count();
        rig.src->rewrite(s14, [](ShotSummary &) {});
        checkInt ("a second failure: said so again", failed.count(), toldBefore + 1);
        rig.src->failing.clear();
        rig.src->rewrite(s14, [](ShotSummary &) {});
        checkStr ("…kept, and saved on the next try", noteOf(s14), QStringLiteral("twice"));
        checkInt ("…three tries in all", rig.src->calls.value(QStringLiteral("note")), 3);

        // A later edit of the row that itself fails still supersedes the kept one (told; not kept: C2).
        rig.src->calls.clear();
        const ShotRef s15 = rig.src->allocate(15);
        rig.model.setNote(card(s15), QStringLiteral("old"));
        rig.src->failing = { QStringLiteral("note") };
        rig.src->land(s15, shot("8 IRON", 0, "14:30", false));
        rig.model.setNote(rig.model.idOfShotRef(s15), QStringLiteral("newer"));
        rig.src->failing.clear();
        rig.src->rewrite(s15, [](ShotSummary &) {});
        checkStr ("a later failed edit: the kept older one is not saved over it", noteOf(s15), QString());
        checkInt ("…nor sent again", rig.src->calls.value(QStringLiteral("note")), 2);
    }

    std::printf("\n-- V10: End lets go of cards and live presentation, no reset --\n");
    {
        Rig rig;
        const ShotRef s3 = rig.src->allocate(3);
        rig.src->land(s3, shot("7 IRON", 60, "13:50"));
        ShotListModel::CapturedShot live;
        live.shot = s3;
        live.tracePoints = { QPointF(0, 0.5), QPointF(1, 0.5) };
        live.thumbnailSource = QUrl(QStringLiteral("file:///live/thumb.jpg"));
        rig.model.landShot(live, true);
        ShotListModel::CapturedShot nothing;
        nothing.club = QStringLiteral("PW");
        rig.model.landShot(nothing, false);
        QSignalSpy resets(&rig.model, &QAbstractItemModel::modelReset);
        checkInt ("before End: the session's three and a card", rig.model.rowCount(), 4);
        rig.model.forgetCapture();
        checkInt ("after: the card has gone", rig.model.rowCount(), 3);
        checkInt ("…no reset", resets.count(), 0);
        checkInt ("…the live trace too", rig.model.data(rig.model.index(rowOf(rig.model, rig.model.idOfShotRef(s3))),
                                                        ShotListModel::TracePointsRole).toList().size(), 0);
    }

    std::printf("\n-- V11: the row keeps no Shot facts --\n");
    {
        Rig rig;
        checkBool("QML has no analysisDetail role", rig.model.roleNames().values().contains("analysisDetail"), false);
        const int reads = rig.src->summaryReads;
        for (int r = 0; r < rig.model.rowCount(); ++r)
            (void)rig.model.data(rig.model.index(r), ShotListModel::MetricsRole);
        checkInt ("showing a row reads the Shot's kept summary, never the document", rig.src->summaryReads, reads);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
