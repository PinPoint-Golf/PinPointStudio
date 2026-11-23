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

// day_readings_dump — THE DAY-LEVEL READS, OLD AGAINST NEW (refactor 5.5d; DEC-74; deliverable 6). For each session folder
// named — A SCRATCH COPY whose athlete folder the app's catch-up has summed up again (every work_ons.json of version 4) —
// the report's Club delivery and Posture pages, two ways, over the same snapshot (FolderSeries, the old loader):
//   OLD  the readers the Charts presenters used before 5.5d, kept here VERBATIM so the app has none of them: the day off
//        the day's other sessions' swing_phasegrid.json (daySiblings, readDay), the "At address" history off every
//        earlier session's diagnostics.json and first swing document (camerasOf, ledgerReading, readAddressHistory);
//   NEW  the same pages from the other sessions' SUMMARIES (their `dayReadings`), placed by SessionIdentity as the athlete
//        level places them: deliveryDayFrom, postureHistoryFrom (charts_delivery_presenter.h, charts_posture_presenter.h).
// Every leaf of both pages is compared; each difference is printed. Listed before the run (D-a): the old day's count of
// sidecars cached before their swing last changed (`staleGrids`), whose trust clause has gone; printed per session.
// Exit 0 when nothing differs, 1 when something does, 2 on a usage error. Reads only.
//
//   day_readings_dump [--out <json>] <session dir>...

#include "charts/charts_delivery_presenter.h"
#include "charts/charts_posture_presenter.h"
#include "charts/charts_words.h"
#include "charts/session_delivery.h"
#include "charts/session_neighbours.h"
#include "charts/session_posture.h"
#include "charts/session_series_extract.h"
#include "tests/folder_series.h"
#include "Coaching/diagnostic_ledger.h"
#include "Coaching/session_day_readings_rules.h"
#include "Coaching/session_results.h"
#include "Export/rule_data_install.h"
#include "Export/session_diagnostics_files.h"
#include "Export/session_files.h"
#include "Export/swing_paths.h"
#include "Export/swing_store.h"
#include "Core/pp_debug.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <cmath>
#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::charts;
namespace pa = pinpoint::analysis;

namespace {

using words::fin;
QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

// ════ OLD: the readers before refactor 5.5d, verbatim (charts_delivery_presenter.cpp, charts_posture_presenter.cpp at 97673b8b) ════

// ── the golfer's day ────────────────────────────────────────────────────────────────────────

// The session folder the snapshot is (its own, else its first shot's parent, else the picked shot's).
QString sessionDirOf(const SessionSnapshot &snap, const QString &picked)
{
    if (!snap.sessionDir.trimmed().isEmpty()) return cleanDir(snap.sessionDir);
    if (!snap.shots.empty() && !snap.shots.front()->swingDir.isEmpty()) return cleanDir(QFileInfo(snap.shots.front()->swingDir).absolutePath());
    return picked.isEmpty() ? QString() : cleanDir(QFileInfo(picked).absolutePath());
}

// The day's other sessions: the golfer's sessions whose local day is this one's — SessionIdentity's day, by start
// time (or, for a session older than step 2, the day its folder's name records, under every naming pattern: F-06) —
// never a folder name's first ten characters, which `name-date-type` never starts with (step 2.7b). Each with its
// place in the day (n-of-day), in that order; `date` is the day ("yyyy-MM-dd"), "" when this session has none.
struct DaySibling { QString dir; int nOfDay = 0; };
std::vector<DaySibling> daySiblings(const QString &sessionDir, QString *date)
{
    if (date) date->clear();
    const QString athleteDir = QFileInfo(sessionDir).absolutePath();
    const QList<pinpoint::SessionIndexEntry> index = pinpoint::SessionDiagnosticsFiles::sessionPlaces(athleteDir);
    const pinpoint::analysis::SessionPlace self = pinpoint::SessionDiagnosticsFiles::placeOf(index, sessionDir);
    if (!self.day.isValid()) return {};
    if (date) *date = self.day.toString(QStringLiteral("yyyy-MM-dd"));
    std::vector<DaySibling> out;
    const QString me = cleanDir(QFileInfo(sessionDir).absoluteFilePath());
    for (const pinpoint::SessionIndexEntry &e : index) {
        if (e.identity.localDay() != self.day) continue;
        const QString dir = cleanDir(QFileInfo(e.folder).absoluteFilePath());
        if (dir == me) continue;
        out.push_back(DaySibling{ dir, e.identity.nOfDay() });
    }
    std::stable_sort(out.begin(), out.end(), [](const DaySibling &a, const DaySibling &b) { return a.nOfDay < b.nOfDay; });
    return out;
}

// What the day's reading was made from: each sibling folder, and in each of its swing folders the phase grid and the
// swing document (size and mtime, a stat each — never a read). A folder's own mtime moves only when a swing folder comes
// or goes; a grid rebuilt by a regrade, or a swing re-analysed, changes only the files inside a swing folder, so those
// are stamped too (issue #174: the day used to keep a count of stale grids the regrade had since rebuilt).
QString dayStamp(const std::vector<DaySibling> &siblings)
{
    QStringList parts;
    const auto stat = [](const QFileInfo &f) { return f.exists() ? QStringLiteral("%1:%2").arg(f.size()).arg(f.lastModified().toMSecsSinceEpoch())
                                                                 : QStringLiteral("-"); };
    for (const DaySibling &sib : siblings) {
        const QString &s = sib.dir;
        parts << QStringLiteral("%1@%2#%3").arg(QFileInfo(s).fileName()).arg(QFileInfo(s).lastModified().toMSecsSinceEpoch()).arg(sib.nOfDay);
        const QDir dir(s);
        for (const QString &sw : pinpoint::SwingPaths::swingFolders(s)) {
            const QString swingDir = dir.filePath(sw);
            const SwingStore::DocInfo doc = SwingStore::info(swingDir);
            parts << QStringLiteral("%1 %2 %3:%4").arg(sw, stat(QFileInfo(QDir(swingDir).filePath(QStringLiteral("swing_phasegrid.json")))))
                                                  .arg(doc.size).arg(doc.mtimeMs);
        }
    }
    return parts.join(QLatin1Char('|'));
}

// Read the siblings' sidecars — never a swing document (a 28 MB parse froze the GUI once): each swing's
// swing_phasegrid.json, its staleness judged by a stat of the document beside it, by THE stamp rule the grid's own
// reader uses (SwingStore::sameDocumentStamp), so a grid the reader would serve is never counted as stale and a grid
// it would rebuild always is.
DeliveryDay readDay(const std::vector<DaySibling> &siblings, const QString &date, int &staleGrids)
{
    staleGrids = 0;
    DeliveryDay day;
    day.date = date;
    day.sessions = 0;
    QElapsedTimer t;
    t.start();
    int files = 0, unread = 0;
    for (const DaySibling &sib : siblings) {
        const QString &sd = sib.dir;
        const QDir dir(sd);
        bool any = false;
        for (const QString &sw : pinpoint::SwingPaths::swingFolders(sd)) {
            const QString swingDir = dir.filePath(sw);
            QFile f(QDir(swingDir).filePath(QStringLiteral("swing_phasegrid.json")));
            if (!f.open(QIODevice::ReadOnly)) continue;
            ++files;
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            bool ok = false;
            DeliveryShotInput in = deliveryInputFromPhaseGrid(root, swingDir, &ok);
            if (!ok) { ++unread; continue; }
            if (!in.hasPlane() && !in.hasMonitor()) continue;
            in.session = QFileInfo(sd).fileName();
            in.sessionOfDay = sib.nOfDay;
            in.thisSession = false;
            if (in.ordinal <= 0) in.ordinal = folderShotId(swingDir);
            in.shotId = folderShotId(swingDir);
            const QJsonObject src = root.value(QStringLiteral("source")).toObject();
            const SwingStore::DocInfo doc = SwingStore::info(swingDir);   // swing.ppsw, else swing.json; none: nothing to be stale against
            if (doc.exists()
                && !SwingStore::sameDocumentStamp(src.value(QStringLiteral("size")).toVariant().toLongLong(),
                                                  src.value(QStringLiteral("mtime_ms")).toVariant().toLongLong(), doc.size, doc.mtimeMs))
                ++staleGrids;
            day.others.push_back(std::move(in));
            any = true;
        }
        if (any) ++day.sessions;
    }
    ppInfo() << "[Charts] delivery day" << date << ":" << siblings.size() << "other sessions," << files << "phase grids,"
             << day.others.size() << "shots read," << unread << "of another schema," << staleGrids << "older than their swing,"
             << t.elapsed() << "ms";
    return day;
}



// ── the golfer's earlier sessions from the same set-up (§7.4.2) ──

// A session's cameras, from its first swing document's streams: the view each stands on (setup.perspectiveName,
// else the alias) and its serial (source.serial). `known` false when no document was read.
struct SessionCameras { bool known = false; std::array<bool, 2> present{}; std::array<QString, 2> serial; };
SessionCameras camerasOf(const QString &sessionDir)
{
    SessionCameras c;
    const QDir sd(sessionDir);
    const QStringList swings = pinpoint::SwingPaths::swingFolders(sessionDir);   // THE one listing (step 2.7b)
    for (const QString &sw : swings) {
        const QString dir = sd.filePath(sw);
        if (!pinpoint::SwingStore::hasDocument(dir)) continue;
        const QJsonObject root = pinpoint::SwingStore::loadAnalysis(dir);
        if (root.isEmpty()) continue;
        c.known = true;
        for (const QJsonValue &sv : root.value(QStringLiteral("streams")).toArray()) {
            const QJsonObject st = sv.toObject();
            if (st.value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
            const QString persp = st.value(QStringLiteral("setup")).toObject().value(QStringLiteral("perspectiveName")).toString();
            const QString alias = st.value(QStringLiteral("alias")).toString();
            int cam = -1;
            if (persp == QLatin1String("DownTheLine") || (persp.isEmpty() && alias.compare(QLatin1String("DTL"), Qt::CaseInsensitive) == 0))
                cam = int(PostureCamera::Dtl);
            else if (persp == QLatin1String("FaceOn") || (persp.isEmpty() && alias.startsWith(QLatin1String("Face"), Qt::CaseInsensitive)))
                cam = int(PostureCamera::FaceOn);
            if (cam < 0) continue;
            c.present[size_t(cam)] = true;
            c.serial[size_t(cam)] = st.value(QStringLiteral("source")).toObject().value(QStringLiteral("serial")).toVariant().toString();
        }
        break;
    }
    return c;
}

// A measure's graded reading on one ledger shot: a row driven or read by it that the engine could assess, else a
// composite's part read by it. NaN when the shot carries none.
// A shot with a DATA WARNING (fireFindings) has every row turned NotAssessable after it was read,
// its value kept: such a row counts when the corridor was applied to it (corridorHi > corridorLo) — it was measured
// and then withheld, unlike a row the engine never assessed, whose 0 nobody measured.
double ledgerReading(const pa::ShotRecord &shot, const QString &measureId)
{
    for (const pa::ConditionRow &r : shot.rows) {
        if (r.state == pa::ShotState::NotAssessable && !(shot.dataWarning && r.corridorHi > r.corridorLo)) continue;
        for (const pa::MeasureRow &m : pa::rowReadings(r))
            if (m.measureId == measureId && fin(m.value)) return m.value;
    }
    for (const pa::ConditionRow &r : shot.rows) {
        for (const pa::CompositePartRow &p : r.composite.parts)
            if (p.measureId == measureId && fin(p.value)) return p.value;
        for (const pa::MeasureRow &m : r.readings)
            for (const pa::CompositePartRow &p : m.composite.parts)
                if (p.measureId == measureId && fin(p.value)) return p.value;
    }
    return kNaN;
}

// The stamp of what the history reads: the session folder and every sibling ledger's name, size and time.
QString addressHistoryStamp(const QString &sessionDir)
{
    const QFileInfo fi(cleanDir(sessionDir));
    if (sessionDir.trimmed().isEmpty() || !fi.isDir()) return cleanDir(sessionDir) + QStringLiteral("|none");
    QStringList parts{ fi.absoluteFilePath() };
    const QDir athlete = fi.dir();
    for (const QString &name : athlete.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QFileInfo d(pinpoint::SessionFiles::diagnosticsFile(athlete.filePath(name)));
        if (d.exists()) parts << QStringLiteral("%1:%2:%3").arg(name).arg(d.size()).arg(d.lastModified().toMSecsSinceEpoch());
    }
    return parts.join(QLatin1Char('|'));
}

// THE HISTORY: every EARLIER session in the athlete folder — earlier by SessionIdentity's place (an earlier local day,
// or the same day and a lower n-of-day, by start time: SessionDiagnosticsFiles::sessionPlaces, step 2.7b), never by
// folder-name order, which `name-date-type` and a renamed athlete's folders break (F-06); a session with no day has
// no history — whose diagnostics ledger reads the measure, kept per view when its camera on
// that view is this session's (by serial; a serial not recorded on either side keeps it, counted as unrecorded).
// One reading per swing: the ledger's graded value — the measure engine's own number, as Session diagnostics graded it.
PostureAddressHistory readAddressHistory(const QString &sessionDir, const PostureEnv &env)
{
    PostureAddressHistory h;
    const QFileInfo fi(cleanDir(sessionDir));
    if (sessionDir.trimmed().isEmpty() || !fi.isDir()) return h;
    const QDir athlete = fi.dir();
    if (!athlete.exists()) return h;
    h.looked = true;
    const SessionCameras mine = camerasOf(fi.absoluteFilePath());
    for (int c = 0; c < 2; ++c) {
        h.views[size_t(c)].known = mine.known;
        h.views[size_t(c)].present = mine.present[size_t(c)];
        h.views[size_t(c)].serial = mine.serial[size_t(c)];
    }
    const QList<pinpoint::SessionIndexEntry> places = pinpoint::SessionDiagnosticsFiles::sessionPlaces(athlete.absolutePath());
    const pa::SessionPlace me = pinpoint::SessionDiagnosticsFiles::placeOf(places, fi.absoluteFilePath());
    std::vector<std::pair<pa::SessionPlace, QString>> earlier;   // oldest first
    if (me.day.isValid())
        for (const pinpoint::SessionIndexEntry &e : places) {
            const pa::SessionPlace p{ e.identity.folderName(), e.identity.localDay(), e.identity.nOfDay() };
            if (p.day.isValid() && pa::sessionPlaceBefore(p, me)) earlier.emplace_back(p, e.folder);
        }
    std::stable_sort(earlier.begin(), earlier.end(), [](const auto &a, const auto &b) { return pa::sessionPlaceBefore(a.first, b.first); });
    for (const auto &[place, dir] : earlier) {
        Q_UNUSED(place)
        // Through the envelope's one reader (step 2.5).
        const pinpoint::DiagnosticsRead read = pinpoint::SessionDiagnosticsFiles::readDiagnostics(dir);
        if (!read.parsed()) continue;
        const std::vector<pa::ShotRecord> shots = pa::fromJson(read.envelope.ledger);
        QHash<QString, std::vector<double>> got;
        std::array<int, 2> swingsWith{};
        // A swing with a data warning COUNTS, as it does in this session's box (the session's shots are boxed
        // whatever their warning): the two medians side by side are one rule. How many are in is said once
        // (addressLines). One swing read on both views is one swing.
        std::vector<std::array<bool, 2>> warnedWith;
        for (const pa::ShotRecord &shot : shots) {
            std::array<bool, 2> any{};
            for (const PostureAbsoluteSpec &sp : env.absolutes) {
                const double v = ledgerReading(shot, sp.measureId);
                if (!fin(v)) continue;
                got[sp.measureId].push_back(v);
                any[size_t(int(sp.camera))] = true;
            }
            for (int c = 0; c < 2; ++c) if (any[size_t(c)]) ++swingsWith[size_t(c)];
            if (shot.dataWarning && (any[0] || any[1])) warnedWith.push_back(any);
        }
        if (got.isEmpty()) continue;
        const SessionCameras theirs = camerasOf(dir);
        std::array<bool, 2> keptView{};
        for (int c = 0; c < 2; ++c) {
            if (swingsWith[size_t(c)] == 0) continue;
            PostureAddressHistory::View &v = h.views[size_t(c)];
            const QString &a = mine.serial[size_t(c)], &b = theirs.serial[size_t(c)];
            if (!a.isEmpty() && !b.isEmpty() && a != b) { ++v.otherCamera; continue; }
            keptView[size_t(c)] = true;
            if (a.isEmpty() || b.isEmpty()) ++v.unrecorded;
            ++v.kept;
            v.swings += swingsWith[size_t(c)];
            for (const PostureAbsoluteSpec &sp : env.absolutes) {
                if (int(sp.camera) != c) continue;
                const auto it = got.constFind(sp.measureId);
                if (it == got.constEnd() || it->empty()) continue;
                std::vector<double> &dst = h.values[sp.measureId];
                dst.insert(dst.end(), it->begin(), it->end());
                ++h.sessions[sp.measureId];
            }
        }
        for (const std::array<bool, 2> &w : warnedWith)
            if ((w[0] && keptView[0]) || (w[1] && keptView[1])) ++h.dataWarned;
    }
    return h;
}



// ════ NEW: the other sessions as their summaries hold them, placed as the athlete level places them ════════════════════
// The athlete level's own rule (AthleteDiagnostics' summaryFrom, session_neighbours.h neighboursFrom) over the files a
// tool may read: each session of the athlete folder by SessionIdentity's place, its work_ons.json read strictly.
SessionNeighbours neighboursOfFolder(const QString &sessionDir)
{
    SessionNeighbours n;
    const QString me = QFileInfo(sessionDir).fileName();
    const QList<SessionIndexEntry> index = SessionDiagnosticsFiles::sessionPlaces(QFileInfo(sessionDir).absolutePath());
    for (const SessionIndexEntry &e : index) {
        const pa::SessionPlace place{ e.identity.folderName(), e.identity.localDay(), e.identity.nOfDay() };
        if (QFileInfo(e.folder).fileName() == me) { n.known = true; n.self = place; continue; }
        NeighbourSession o;
        o.place = place;
        const JsonFileRead f = SessionDiagnosticsFiles::readWorkOns(e.folder);
        bool ok = false;
        const pa::SessionResults r = f.exists && f.ok ? pa::sessionResultsFromJson(f.root, pa::SessionResultsRead::Strict, &ok) : pa::SessionResults{};
        if (f.exists && f.ok && pa::sessionResultsFromNewerBuild(f.root)) o.state = NeighbourSession::State::Newer;
        else if (ok && r.dayReadings.has) { o.state = NeighbourSession::State::Summed; o.readings = r.dayReadings; }
        else if (!SwingPaths::swingFolders(e.folder).isEmpty()) o.state = NeighbourSession::State::NotYet;
        else continue;
        n.others.push_back(std::move(o));
    }
    std::stable_sort(n.others.begin(), n.others.end(),
                     [](const NeighbourSession &a, const NeighbourSession &b) { return pa::sessionPlaceBefore(a.place, b.place); });
    return n;
}

pa::SessionCameras newCamerasOf(const QString &sessionDir)
{
    for (const QString &sw : SwingPaths::swingFolders(sessionDir)) {
        const QString dir = QDir(sessionDir).filePath(sw);
        if (!SwingStore::hasDocument(dir)) continue;
        const pa::SessionCameras c = pa::sessionCamerasOf(SwingStore::loadAnalysis(dir));
        if (c.known) return c;
    }
    return {};
}

// Every leaf that differs, by path (charts_series_dump's).
void differences(const QJsonValue &a, const QJsonValue &b, const QString &path, QStringList &out)
{
    if (a.isObject() && b.isObject()) {
        const QJsonObject x = a.toObject(), y = b.toObject();
        QStringList keys = x.keys();
        for (const QString &k : y.keys()) if (!keys.contains(k)) keys << k;
        keys.sort();
        for (const QString &k : keys) differences(x.value(k), y.value(k), path + QLatin1Char('.') + k, out);
        return;
    }
    if (a.isArray() && b.isArray() && a.toArray().size() == b.toArray().size()) {
        const QJsonArray x = a.toArray(), y = b.toArray();
        for (qsizetype i = 0; i < x.size(); ++i) differences(x.at(i), y.at(i), path + QStringLiteral("[%1]").arg(i), out);
        return;
    }
    if (a != b)
        out << QStringLiteral("%1: old %2 / new %3").arg(path,
                    QString::fromUtf8(QJsonDocument(QJsonArray{ a }).toJson(QJsonDocument::Compact)).left(160),
                    QString::fromUtf8(QJsonDocument(QJsonArray{ b }).toJson(QJsonDocument::Compact)).left(160));
}

QJsonValue jsonOf(const QVariantMap &m) { return QJsonValue(QJsonObject::fromVariantMap(m)); }

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);   // the user's packs stay out
    QStringList args = app.arguments().mid(1);
    QString outPath;
    if (args.size() >= 2 && args.first() == QLatin1String("--out")) { outPath = args.at(1); args = args.mid(2); }
    if (args.isEmpty()) { std::fprintf(stderr, "usage: day_readings_dump [--out <json>] <session dir>...\n"); return 2; }
    installProcessRuleData();
    const std::shared_ptr<const PostureEnv> env = postureSharedEnv();

    QJsonObject all;
    int differing = 0;
    for (const QString &arg : args) {
        const QString dir = QDir::cleanPath(QFileInfo(arg).absoluteFilePath());
        const QString name = QFileInfo(dir).fileName();
        FolderSeries series;
        series.setSynchronous(true);
        series.activateSession(dir);
        const std::shared_ptr<const SessionSnapshot> snap = series.snapshot();
        if (!snap) { std::printf("%s: no snapshot\n", qPrintable(name)); ++differing; continue; }

        // OLD
        QString date;
        const std::vector<DaySibling> siblings = daySiblings(dir, &date);
        int staleGrids = 0;
        DeliveryDay oldDay;
        oldDay.sessions = 0;
        if (!siblings.empty()) oldDay = readDay(siblings, date, staleGrids);
        oldDay.date = date;
        oldDay.sessions += 1;
        const PostureAddressHistory oldHist = readAddressHistory(dir, *env);
        // NEW
        const SessionNeighbours n = neighboursOfFolder(dir);
        const DeliveryDay newDay = deliveryDayFrom(n, dir);
        const PostureAddressHistory newHist = postureHistoryFrom(n, newCamerasOf(dir), *env);

        const QJsonValue dOld = jsonOf(reportPayloadOf(deliverySessionReportOfDay(*snap, QStringLiteral("swings"), oldDay)));
        const QJsonValue dNew = jsonOf(reportPayloadOf(deliverySessionReportOfDay(*snap, QStringLiteral("swings"), newDay)));
        const QJsonValue pOld = jsonOf(reportPayloadOf(postureSessionReportOfHistory(*snap, -1, QStringLiteral("swings"), oldHist)));
        const QJsonValue pNew = jsonOf(reportPayloadOf(postureSessionReportOfHistory(*snap, -1, QStringLiteral("swings"), newHist)));
        QStringList diffs;
        differences(dOld, dNew, QStringLiteral("delivery"), diffs);
        differences(pOld, pNew, QStringLiteral("posture"), diffs);
        int notYet = 0, newer = 0, summed = 0;
        for (const NeighbourSession &o : n.others)
            (o.state == NeighbourSession::State::NotYet ? notYet : o.state == NeighbourSession::State::Newer ? newer : summed) += 1;
        int histValues = 0;
        for (auto it = newHist.values.constBegin(); it != newHist.values.constEnd(); ++it) histValues += int(it->size());
        std::printf("%s: %lld shots; day %s: %lld other shots old, %lld new (%d sessions); history: %d readings; "
                    "neighbours %d summed, %d not yet, %d newer; D-a staleGrids %d; %lld differences\n",
                    qPrintable(name), (long long)snap->shots.size(), qPrintable(date), (long long)oldDay.others.size(),
                    (long long)newDay.others.size(), newDay.sessions, histValues, summed, notYet, newer, staleGrids,
                    (long long)diffs.size());
        for (const QString &x : diffs) std::printf("  DIFF %s\n", qPrintable(x));
        if (!diffs.isEmpty()) ++differing;
        all.insert(name, QJsonObject{ { "delivery", dNew }, { "posture", pNew }, { "staleGrids", staleGrids },
                                      { "differences", QJsonArray::fromStringList(diffs) } });
    }
    std::printf("\nTOTAL: %d of %lld sessions differ\n", differing, (long long)args.size());
    if (!outPath.isEmpty()) {
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
    }
    return differing == 0 ? 0 : 1;
}
