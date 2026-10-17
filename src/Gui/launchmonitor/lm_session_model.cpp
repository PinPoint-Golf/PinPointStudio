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

#include "lm_session_model.h"

#include "../../LaunchMonitor/launch_monitor_base.h"
#include "../../LaunchMonitor/launch_monitor_factory.h"

#include "../../LaunchMonitor/lm_club_evidence.h"
#include "../../LaunchMonitor/lm_enclosing_ellipse.h"
#include "../../LaunchMonitor/lm_flight_path.h"
#include "../../LaunchMonitor/lm_inferred_reads.h"
#include "../../LaunchMonitor/lm_session_reductions.h"
#include "../../Analysis/swing_analysis.h"
#include "../trust/trust_readings.h"

#include "../../Diagnostics/norms/context_tree.h"
#include "../../Diagnostics/norms/metric_corridor.h"
#include "../../Diagnostics/norms/norm_provider.h"
#include "../../Diagnostics/packs/pack_provider.h"

#include <QPointF>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <limits>
#include <optional>

using namespace pinpoint::analysis;

namespace {

// One shot's launch monitor readings, out of the row's analysisDetail.
//
// The `series` entries are the swing.json metric objects verbatim; a launch monitor
// entry is an EMPTY curve carrying a single phaseSample at Impact (see lmMetricEntries
// — a device reports one number per shot, and inventing a curve for it would be a lie
// the charts would draw). So the value is looked up by PHASE, exactly as the writer
// tagged it, rather than by taking whatever sample happens to be first.
LmShotValues readingsFor(const QVariantMap &analysisDetail)
{
    LmShotValues out;
    const QVariantList series = analysisDetail.value(QStringLiteral("series")).toList();
    for (const QVariant &sv : series) {
        const QVariantMap m = sv.toMap();
        const QString key = m.value(QStringLiteral("key")).toString();
        if (!key.startsWith(QStringLiteral("lm.")))
            continue;
        for (const QVariant &pv : m.value(QStringLiteral("phaseSamples")).toList()) {
            const QVariantMap ps = pv.toMap();
            if (ps.value(QStringLiteral("phase")).toInt() != int(Phase::Impact))
                continue;
            bool ok = false;
            const double v = ps.value(QStringLiteral("value")).toDouble(&ok);
            if (ok) out.insert(key, v);
            break;
        }
    }
    return out;
}

// PinPoint's OWN optical low point for one shot — the bare `lowPointAhead` our cameras
// estimate (club_delivery.cpp), read the same way as a reading because it reaches Impact
// the same way: a PointInTime scalar on an otherwise empty curve.
//
// READ SEPARATELY, AND DELIBERATELY SO. It must not join the LmShotValues map: that map
// is "what the device measured", and rebuild() tests it for emptiness to decide whether
// the monitor has said anything at all and which device to name. An optical estimate
// dropped into it would make a camera-only session claim a monitor reported it. It is
// used for exactly one thing — the IMPACT card's low point falling back to an estimate,
// badged as one — and nothing else may reach for it.
std::optional<double> opticalLowPointFor(const QVariantMap &analysisDetail)
{
    const QVariantList series = analysisDetail.value(QStringLiteral("series")).toList();
    for (const QVariant &sv : series) {
        const QVariantMap m = sv.toMap();
        if (m.value(QStringLiteral("key")).toString() != QLatin1String("lowPointAhead"))
            continue;
        for (const QVariant &pv : m.value(QStringLiteral("phaseSamples")).toList()) {
            const QVariantMap ps = pv.toMap();
            if (ps.value(QStringLiteral("phase")).toInt() != int(Phase::Impact))
                continue;
            bool ok = false;
            const double v = ps.value(QStringLiteral("value")).toDouble(&ok);
            if (ok) return v;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

QVariantMap tileFor(const LmFieldStats &st, const QString &grade)
{
    return QVariantMap{
        { QStringLiteral("key"),       st.key },
        // "" | "ideal" | "good" | "watch" | "action". The panel paints only the last two —
        // see gradesFor() for why an empty string is the answer to three different
        // questions and why the tile is right to draw all three the same way.
        { QStringLiteral("grade"),     grade },
        { QStringLiteral("label"),     QString::fromUtf8(st.def->label) },
        { QStringLiteral("abbrev"),    QString::fromUtf8(st.def->abbrev) },
        { QStringLiteral("unit"),      QString::fromUtf8(st.def->unit) },
        { QStringLiteral("group"),     QString::fromUtf8(st.def->group) },
        // Formatted here and not in QML, so this panel and any future export cannot
        // disagree about what a reading looks like.
        { QStringLiteral("latest"),    lmValueText(st) },
        { QStringLiteral("mean"),      lmMeanText(st) },
        { QStringLiteral("sd"),        lmSdText(st) },
        { QStringLiteral("n"),         st.n },
        { QStringLiteral("hasLatest"), st.hasLatest },
        { QStringLiteral("hasSpread"), st.hasSpread },
        { QStringLiteral("z"),         st.z },
        // The strip's tick position, decided here for the same reason as the numbers.
        { QStringLiteral("tickPct"),   lmTickPercent(st.z) },
    };
}

// ── graphics mode ────────────────────────────────────────────────────────────
//
// The schematics need two things the tiles board never did: the RAW value (a formatted
// string cannot be rotated by), and the band's INDEX (the annotation is coloured by the
// band its metric belongs to, not by the card it sits on — the launch ray inside the
// IMPACT card is Launch teal). Both come off the same LmFieldStats the tiles came from.

int bandIndexOf(const char *group)
{
    const QString g = QString::fromLatin1(group ? group : "");
    const auto &groups = pinpoint::lm::fieldGroups();
    for (size_t i = 0; i < groups.size(); ++i)
        if (QString::fromLatin1(groups[i]) == g) return int(i);
    return 0;
}

// One field, as an annotation needs it.
QVariantMap annotationFor(const LmFieldStats &st, const QString &grade)
{
    return QVariantMap{
        { QStringLiteral("value"),     st.latest },       // RAW — geometry rotates by this
        // The SAME string the tile carries, from the same map. A schematic and a tile
        // showing one reading in two colours would be the panel arguing with itself.
        { QStringLiteral("grade"),     grade },
        { QStringLiteral("text"),      lmValueText(st) }, // formatted — the card prints this
        { QStringLiteral("mean"),      lmMeanText(st) },
        { QStringLiteral("sd"),        lmSdText(st) },
        // The spread as NUMBERS as well as words. The tiles board only ever printed
        // these; the schematics shade a region from them, and a region cannot be built
        // out of the string "±5.2".
        { QStringLiteral("meanValue"), st.mean },
        { QStringLiteral("sdValue"),   st.sd },
        { QStringLiteral("hasSpread"), st.hasSpread },
        { QStringLiteral("n"),         st.n },
        { QStringLiteral("unit"),      QString::fromUtf8(st.def->unit) },
        { QStringLiteral("abbrev"),    QString::fromUtf8(st.def->abbrev) },
        { QStringLiteral("label"),     QString::fromUtf8(st.def->label) },
        { QStringLiteral("bandIndex"), bandIndexOf(st.def->group) },
        { QStringLiteral("has"),       st.hasLatest },
    };
}

// A field's focused-shot value, or nothing. THE `std::optional` IS THE POINT: it is what
// carries "the monitor did not report this" all the way into the inferred reads, which
// answer with no read rather than with a default one.
std::optional<double> valueOf(const std::vector<LmFieldStats> &stats, const char *key)
{
    const QString k = QString::fromLatin1(key);
    for (const LmFieldStats &st : stats)
        if (st.key == k && st.hasLatest) return st.latest;
    return std::nullopt;
}

std::optional<double> meanOf(const std::vector<LmFieldStats> &stats, const char *key)
{
    const QString k = QString::fromLatin1(key);
    for (const LmFieldStats &st : stats)
        if (st.key == k && st.n > 0) return st.mean;
    return std::nullopt;
}

// The flight polyline, thinned to what a 428 px card can resolve.
//
// The integration produces thousands of points at half a millisecond each; drawing every
// one would hand QML a list two orders of magnitude longer than the card has pixels for.
// Thinned HERE rather than in the view because it is a decision about the data, and
// because a Shape rebuilt from 5,000 elements on every focus change is a stutter the
// panel does not need to have.
QVariantList thin(const std::vector<LmPoint> &pts, bool lateral, int want = 120)
{
    QVariantList out;
    if (pts.empty()) return out;
    const int n = int(pts.size());
    const int step = std::max(1, n / std::max(2, want));
    out.reserve(n / step + 2);
    for (int i = 0; i < n; i += step)
        out.append(QPointF(pts[size_t(i)].x, lateral ? pts[size_t(i)].z : pts[size_t(i)].y));
    // The last point always survives the thinning: it is the landing, and it is the one
    // point on the curve the card labels.
    const LmPoint &last = pts.back();
    out.append(QPointF(last.x, lateral ? last.z : last.y));
    return out;
}

QVariantMap pointMap(const LmPoint &p)
{
    return QVariantMap{ { QStringLiteral("x"), p.x },
                        { QStringLiteral("y"), p.y },
                        { QStringLiteral("z"), p.z } };
}

// ── the FSX session half: shots, effective clubs, evidence, ellipses ────────────
//
// One row of the session as rebuild() needs it, in the shot list's own (newest-first) order.
// Gathered ONCE per rebuild so the board, the scope, the evidence and the session figures all
// read the same facts about the same shot.
struct LmShotRow {
    int                   row = -1;
    int                   id  = -1;
    int                   ordinal = 0;
    QString               recorded;       // the club the shot was recorded under (ClubRole)
    QString               clubOverride;   // the golfer's own choice on the panel, or ""
    bool                  excluded = false;
    QString               kind;           // launchMonitor.kind, "" when no device measured it
    LmShotValues          values;         // the raw readings
    std::optional<double> optical;        // our own low point estimate — see opticalLowPointFor
    // The gate behind each reading the shot lacks (lmGateMap), keyed by metric key: {} on a shot
    // analysed before records existed, or one the monitor reported in full.
    QVariantMap           gates;
    QString               swingDir;       // where the records are (the Trust panel opens the reading there)
    // Resolved by resolveClubs():
    QString               club;           // the EFFECTIVE club — the one everything scopes by
    QString               source;         // "recorded" | "evidence" | "user"
    LmClubEvidence        evidence;
    bool                  declined = false;   // the golfer chose the recorded club over the evidence
};

bool sameClub(const QString &a, const QString &b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

// WHY A READING IS MISSING (trust stage 6 session 2; docs/design/trust_stage6_plan.md "Session 2"). Of one
// shot's records (analysisDetail.gates), the gate behind each launch monitor field it lacks — the
// import's drop list as lm_gates.h turned it into records ("not measured", "impossible value") — in
// trust::gateFor's shape, keyed by metric key. A tile prints the words where it printed "-". Empty when
// the shot carries no `lm.` record: an old swing, or a reading the monitor sent in full, keeps its "-".
QVariantMap lmGateMap(const QVariantList &gates)
{
    QVariantMap out;
    bool any = false;
    for (const QVariant &v : gates)
        if (v.toMap().value(QStringLiteral("gate")).toString().startsWith(QLatin1String("lm."))) { any = true; break; }
    if (!any)
        return out;
    for (const pinpoint::lm::FieldDef &f : pinpoint::lm::fieldDefs()) {
        const QString key = QString::fromLatin1(f.key);
        const QVariantMap gm = pinpoint::trust::gateFor(gates, key);
        if (!gm.isEmpty())
            out.insert(key, gm);
    }
    return out;
}

std::optional<double> readingOf(const LmShotValues &v, const char *key)
{
    const auto it = v.constFind(QString::fromLatin1(key));
    if (it == v.constEnd() || !std::isfinite(*it))
        return std::nullopt;
    return *it;
}

// THE EFFECTIVE CLUB, per shot, in one place.
//
//   1. The golfer's own choice (clubOverride) wins, always. If it IS the recorded club, the
//      source is "recorded" — that is what it is — and the evidence is marked declined.
//   2. Else a SPECIFIC evidence suggestion (lm_club_evidence.h) relabels the shot: "evidence".
//      A generic one ("an iron") never does; it only flags.
//   3. Else the recorded club.
//
// The evidence is computed over the whole session in SHOT ORDER and from the RECORDED clubs
// only, so it can never feed on its own relabels, and the same session suggests the same club
// however its rows happen to be ordered on screen.
void resolveClubs(std::vector<LmShotRow> &rows)
{
    std::vector<size_t> order(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&rows](size_t a, size_t b) { return rows[a].ordinal < rows[b].ordinal; });

    std::vector<LmEvidenceShot> in;
    in.reserve(rows.size());
    for (size_t i : order) {
        const LmShotRow &r = rows[i];
        in.push_back({ r.recorded,
                       readingOf(r.values, "lm.dynamicLoft"),
                       readingOf(r.values, "lm.smashFactor"),
                       readingOf(r.values, "lm.spinRate") });
    }
    const std::vector<LmClubEvidence> ev = lmClubEvidence(in);

    for (size_t k = 0; k < order.size(); ++k) {
        LmShotRow &r = rows[order[k]];
        r.evidence = ev[k];
        if (!r.clubOverride.isEmpty()) {
            const bool isRecorded = sameClub(r.clubOverride, r.recorded);
            r.club     = r.clubOverride;
            r.source   = isRecorded ? QStringLiteral("recorded") : QStringLiteral("user");
            r.declined = isRecorded && r.evidence.mismatch;
        } else if (r.evidence.mismatch && r.evidence.specific) {
            r.club   = r.evidence.suggest;
            r.source = QStringLiteral("evidence");
        } else {
            r.club   = r.recorded;
            r.source = QStringLiteral("recorded");
        }
    }
}

// The board's statistics with the FOCUSED shot's readings as `latest`, for the one case the
// statistics cannot carry it themselves: the focused shot is LEFT OUT. It stays "this shot" —
// the tiles and the figures must still show what that swing did — but it is not in the scope
// the means and spreads were computed over. A field only the focused shot carries comes back
// with n = 0, which every formatter already prints as "-" for the mean and spread.
std::vector<LmFieldStats> withFocus(const std::vector<LmFieldStats> &stats, const LmShotValues &focus)
{
    std::vector<LmFieldStats> out;
    out.reserve(pinpoint::lm::fieldDefs().size());
    for (const pinpoint::lm::FieldDef &f : pinpoint::lm::fieldDefs()) {
        const QString key = QString::fromLatin1(f.key);
        LmFieldStats st;
        bool found = false;
        for (const LmFieldStats &s : stats)
            if (s.key == key) { st = s; found = true; break; }
        if (!found) { st.key = key; st.def = &f; }
        st.hasLatest = false;
        st.latest    = 0.0;
        st.z         = 0.0;
        const auto it = focus.constFind(key);
        if (it != focus.constEnd() && std::isfinite(*it)) {
            st.hasLatest = true;
            st.latest    = *it;
            if (st.hasSpread) st.z = (st.latest - st.mean) / st.sd;
        }
        if (found || st.hasLatest)
            out.push_back(st);
    }
    return out;
}

QVariantMap ellipseMap(const LmEllipse &e)
{
    return QVariantMap{
        { QStringLiteral("cx"),       e.cx },
        { QStringLiteral("cy"),       e.cy },
        { QStringLiteral("a"),        e.a },
        { QStringLiteral("b"),        e.b },
        { QStringLiteral("angleDeg"), e.angleDeg },
        { QStringLiteral("n"),        e.n },
    };
}

// One figure's enclosing ellipse over a club's included shots: the shots that carry BOTH
// readings, and nothing under three (lm_enclosing_ellipse.h). Absent rather than empty when
// there is none, so `ellipses.strike` is simply undefined in QML — the contract's "absent
// when n < 3".
void addEllipse(QVariantMap &into, const char *name, const std::vector<const LmShotRow *> &shots,
                const char *keyX, const char *keyY)
{
    std::vector<std::array<double, 2>> pts;
    pts.reserve(shots.size());
    for (const LmShotRow *r : shots) {
        const auto x = readingOf(r->values, keyX), y = readingOf(r->values, keyY);
        if (x && y) pts.push_back({ *x, *y });
    }
    const LmEllipse e = lmEnclosingEllipse(pts);
    if (e.has)
        into.insert(QString::fromLatin1(name), ellipseMap(e));
}

// graphics.shots, .clubs, .mismatch and .device — the FSX session half's data (lm_panel_fsx.md,
// "LmSessionModel additions"). `rows` is the whole session in list order; `focusRow` indexes
// it or is −1.
QVariantMap sessionGraphics(const std::vector<LmShotRow> &rows, int focusRow,
                            const QString &liveDeviceName, bool reviewing)
{
    QVariantMap g;

    // Shot order, oldest first — the order the strips and the notes number them in.
    std::vector<const LmShotRow *> byOrdinal;
    byOrdinal.reserve(rows.size());
    for (const LmShotRow &r : rows) byOrdinal.push_back(&r);
    std::stable_sort(byOrdinal.begin(), byOrdinal.end(),
                     [](const LmShotRow *a, const LmShotRow *b) { return a->ordinal < b->ordinal; });

    // ── shots ───────────────────────────────────────────────────────────────
    QVariantList shots;
    shots.reserve(int(byOrdinal.size()));
    for (const LmShotRow *r : byOrdinal) {
        // RAW doubles, so a scatter point sits where the device put it rather than stacking on
        // a display string rounded to a whole degree (ShotListModel's MetricsRole).
        QVariantMap values;
        for (auto it = r->values.constBegin(); it != r->values.constEnd(); ++it)
            if (std::isfinite(it.value())) values.insert(it.key(), it.value());
        // The gate's words for a reading this shot lacks (the print table prints them for "-").
        QVariantMap gateWords;
        for (auto it = r->gates.constBegin(); it != r->gates.constEnd(); ++it)
            if (!values.contains(it.key()))
                gateWords.insert(it.key(), it.value().toMap().value(QStringLiteral("words")));
        shots.append(QVariantMap{
            { QStringLiteral("id"),           r->id },
            { QStringLiteral("number"),       r->ordinal },
            { QStringLiteral("club"),         r->club },
            { QStringLiteral("recordedClub"), r->recorded },
            { QStringLiteral("clubSource"),   r->source },
            { QStringLiteral("excluded"),     r->excluded },
            { QStringLiteral("values"),       values },
            { QStringLiteral("gateWords"),    gateWords },
            { QStringLiteral("evidence"), QVariantMap{
                // The club the readings point at when they contradict the recorded one — a
                // club the session holds, or "an iron" / "a wood" — else "".
                { QStringLiteral("suggest"),  r->evidence.suggest },
                { QStringLiteral("votes"),    r->evidence.votes },
                { QStringLiteral("of"),       r->evidence.of },
                // Beyond the contract, for the card's words: which family two votes decided
                // ("wood" | "iron" | ""), and whether the golfer declined the suggestion.
                { QStringLiteral("reads"),    lmClubFamilyName(r->evidence.reads) },
                { QStringLiteral("declined"), r->declined },
            } },
        });
    }
    g.insert(QStringLiteral("shots"), shots);

    // ── clubs ───────────────────────────────────────────────────────────────
    // One per EFFECTIVE club among the INCLUDED shots that carry readings, in bag order. A
    // left-out shot is drawn (hollow) but is in none of these numbers; a shot with no readings
    // says nothing about what the monitor measured and is in none of them either.
    struct Group { QString club; std::vector<const LmShotRow *> shots; };
    std::vector<Group> groups;
    for (const LmShotRow *r : byOrdinal) {
        if (r->excluded || r->values.isEmpty()) continue;
        auto it = std::find_if(groups.begin(), groups.end(),
                               [r](const Group &gr) { return sameClub(gr.club, r->club); });
        if (it == groups.end()) groups.push_back({ r->club, { r } });
        else                    it->shots.push_back(r);
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) {
        const int ra = lmClubBagRank(a.club), rb = lmClubBagRank(b.club);
        return ra != rb ? ra < rb : a.club.compare(b.club, Qt::CaseInsensitive) < 0;
    });
    QVariantList clubs;
    for (const Group &gr : groups) {
        std::vector<LmShotValues> vals;
        vals.reserve(gr.shots.size());
        for (const LmShotRow *r : gr.shots) vals.push_back(r->values);
        QVariantMap stats;
        for (const LmFieldStats &st : lmSessionStats(vals, -1)) {
            stats.insert(st.key, QVariantMap{
                { QStringLiteral("mean"),      st.mean },
                // The n−1 sample SD, 0 under two values. `hasSpread` is the board's own rule
                // (n ≥ 3 and sd > 0): below it the panel prints "-", never "±0".
                { QStringLiteral("sd"),        st.sd },
                { QStringLiteral("n"),         st.n },
                { QStringLiteral("hasSpread"), st.hasSpread },
                // The extremes over the same included shots (lm_session_reductions.h) — the
                // Session tiles' range and spray are max − min of these.
                { QStringLiteral("min"),       st.min },
                { QStringLiteral("max"),       st.max },
            });
        }
        QVariantMap ellipses;
        addEllipse(ellipses, "dispersion", gr.shots, "lm.carryDistance", "lm.offline");   // yd, yd
        addEllipse(ellipses, "launchSpin", gr.shots, "lm.launchAngle",   "lm.spinRate");  // °, rpm
        // Face mm, measured from the FORESIGHT origin — the readings as the device reported them.
        // LmClubArt::strikeOriginMm() places that origin on our render.
        addEllipse(ellipses, "strike",     gr.shots, "lm.strikeLocation", "lm.strikeHeight");
        addEllipse(ellipses, "pathFace",   gr.shots, "lm.clubPath",      "lm.faceAngle"); // °, °
        clubs.append(QVariantMap{
            { QStringLiteral("club"),     gr.club },
            { QStringLiteral("head"),     lmClubHead(gr.club) },
            { QStringLiteral("n"),        int(gr.shots.size()) },
            { QStringLiteral("stats"),    stats },
            { QStringLiteral("ellipses"), ellipses },
        });
    }
    g.insert(QStringLiteral("clubs"), clubs);

    // ── mismatch ────────────────────────────────────────────────────────────
    // The note. The largest group of shots the evidence RELABELLED, by (recorded, suggested);
    // failing that, the largest group it could only FLAG (a generic suggestion, nothing
    // relabelled). Every other shot's verdict is still on its own `evidence`; the note names
    // one group because it is one line.
    struct MisGroup { QString recorded, suggested; QVariantList ids; };
    auto largest = [&byOrdinal](bool relabelled) {
        std::vector<MisGroup> mg;
        for (const LmShotRow *r : byOrdinal) {
            const bool want = relabelled
                ? r->source == QLatin1String("evidence")
                : (r->evidence.mismatch && !r->evidence.specific && r->clubOverride.isEmpty());
            if (!want) continue;
            auto it = std::find_if(mg.begin(), mg.end(), [r](const MisGroup &m) {
                return sameClub(m.recorded, r->recorded) && sameClub(m.suggested, r->evidence.suggest);
            });
            if (it == mg.end()) mg.push_back({ r->recorded, r->evidence.suggest, { r->id } });
            else                it->ids.append(r->id);
        }
        const MisGroup *best = nullptr;
        for (const MisGroup &m : mg)
            if (!best || m.ids.size() > best->ids.size()) best = &m;
        return best ? std::optional<MisGroup>(*best) : std::nullopt;
    };
    QVariantMap mismatch;
    std::optional<MisGroup> mg = largest(true);
    const bool relabelled = mg.has_value();
    if (!mg) mg = largest(false);
    if (mg) {
        mismatch = QVariantMap{
            { QStringLiteral("count"),      int(mg->ids.size()) },
            { QStringLiteral("recorded"),   mg->recorded },
            { QStringLiteral("suggested"),  mg->suggested },
            { QStringLiteral("shotIds"),    mg->ids },
            // Beyond the contract: true when these shots ARE treated as `suggested` (the note
            // offers Undo), false when the suggestion is generic and they are only flagged.
            { QStringLiteral("relabelled"), relabelled },
        };
    }
    g.insert(QStringLiteral("mismatch"), mismatch);

    // ── device ──────────────────────────────────────────────────────────────
    // FROM THE SHOTS, NOT ONLY THE CABLE. A session reviewed next week was measured by whatever
    // its documents say; the live connector is only the fallback for a live session whose shots
    // carry no kind yet. The focused shot's device if it has one, else the session's commonest.
    QString kind;
    if (focusRow >= 0 && !rows[size_t(focusRow)].values.isEmpty())
        kind = rows[size_t(focusRow)].kind;
    if (kind.isEmpty()) {
        QHash<QString, int> n;
        for (const LmShotRow &r : rows)
            if (!r.values.isEmpty() && !r.kind.isEmpty()) ++n[r.kind];
        int best = 0;
        for (auto it = n.constBegin(); it != n.constEnd(); ++it)
            if (it.value() > best || (it.value() == best && it.key() < kind)) { best = it.value(); kind = it.key(); }
            // (ties go to the alphabetically first kind, so QHash order never picks the caption)
    }
    QString name = pinpoint::lm::kindShortLabel(pinpoint::lm::kindFromKey(kind));
    if (name.isEmpty() && !reviewing)
        name = liveDeviceName;
    // Foresight's club-marker layouts: four dots on the GC Quad, QuadMAX, Falcon and GCHawk;
    // one on the GC3 and an HMT on its own. Anything else carries none.
    static const QRegularExpression reFour(QStringLiteral("quad|falcon|hawk"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reOne(QStringLiteral("gc\\s*3|hmt"),
                                          QRegularExpression::CaseInsensitiveOption);
    const QString markers = reFour.match(name).hasMatch() ? QStringLiteral("four")
                          : reOne.match(name).hasMatch()  ? QStringLiteral("one")
                                                          : QString();
    const QString maker = (kind == QLatin1String("gcquad") || !markers.isEmpty())
                              ? QStringLiteral("Foresight") : QString();
    g.insert(QStringLiteral("device"), QVariantMap{
        { QStringLiteral("name"),    name },
        { QStringLiteral("maker"),   maker },
        { QStringLiteral("markers"), markers },
    });

    return g;
}

} // namespace

LmSessionModel::LmSessionModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_pack(makeCharacteristicPackProvider())
    , m_norms(sharedNormProvider())
    , m_policyName(QStringLiteral("standard"))
{
}

// Out of line because the header holds a unique_ptr to an incomplete type.
LmSessionModel::~LmSessionModel() = default;

int LmSessionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_bands.size());
}

QVariant LmSessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_bands.size())
        return {};
    const Band &b = m_bands.at(index.row());
    switch (role) {
    case BandRole:  return b.name;
    case CountRole: return int(b.tiles.size());
    case TilesRole: return b.tiles;
    }
    return {};
}

QHash<int, QByteArray> LmSessionModel::roleNames() const
{
    return { { BandRole, "band" }, { CountRole, "count" }, { TilesRole, "tiles" } };
}

void LmSessionModel::setShotModel(ShotListModel *m)
{
    if (m_shots == m)
        return;
    if (m_shots)
        m_shots->disconnect(this);
    m_shots = m;
    connectShotModel();
    emit shotModelChanged();
    rebuild();
}

void LmSessionModel::connectShotModel()
{
    if (!m_shots)
        return;
    // Everything that can change what the board says, and nothing that cannot. A club
    // edit writes through as dataChanged on the row, a reading arriving does the same
    // via refreshShot, and loadSessionDir is a model reset.
    connect(m_shots, &QAbstractItemModel::rowsInserted,   this, &LmSessionModel::rebuild);
    connect(m_shots, &QAbstractItemModel::rowsRemoved,    this, &LmSessionModel::rebuild);
    connect(m_shots, &QAbstractItemModel::modelReset,     this, &LmSessionModel::rebuild);
    connect(m_shots, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                if (m_batching)
                    return;                       // undoMismatch() rebuilds once at the end
                if (roles.isEmpty()
                    || roles.contains(int(ShotListModel::AnalysisDetailRole))
                    || roles.contains(int(ShotListModel::MetricsRole))
                    || roles.contains(int(ShotListModel::ClubRole))
                    || roles.contains(int(ShotListModel::LmDeviceKindRole))
                    || roles.contains(int(ShotListModel::LmExcludedRole))
                    || roles.contains(int(ShotListModel::LmClubOverrideRole)))
                    rebuild();
            });
    connect(m_shots, &QObject::destroyed, this, [this]() { rebuild(); });
}

void LmSessionModel::setFocusedShotId(int id)
{
    if (m_focusedShotId == id)
        return;
    m_focusedShotId = id;
    emit focusedShotIdChanged();
    rebuild();
}

void LmSessionModel::setConnected(bool on)
{
    if (m_connected == on) return;
    m_connected = on;
    emit stateTextChanged();
}

void LmSessionModel::setReviewing(bool on)
{
    if (m_reviewing == on) return;
    m_reviewing = on;
    rebuild();               // the scope line drops the live device's name
    emit stateTextChanged();
}

void LmSessionModel::setDeviceName(const QString &name)
{
    if (m_deviceName == name) return;
    m_deviceName = name;
    rebuild();               // the scope line leads with it
    emit headerChanged();
}

void LmSessionModel::setLeftHanded(bool on)
{
    if (m_leftHanded == on) return;
    m_leftHanded = on;
    rebuild();               // both inferred reads are worded from it
}

void LmSessionModel::setGradePolicy(const QString &name)
{
    // Resolved through the shared table rather than stored as handed in — the same rule
    // MetricCatalog and NormModel follow: an unknown name must grade against the default
    // AND read back as the default, or a typo in a binding would leave the property
    // claiming a policy no corridor on screen was actually graded under.
    const QString resolved = QString::fromLatin1(gradePolicyPresetFor(name).name);
    if (resolved == m_policyName) return;
    m_policyName = resolved;
    emit gradePolicyChanged();
    rebuild();               // every corridor edge moves with the policy
}

// The (metric, phase) → measure → norm join, once per rebuild, for the focused shot.
//
// PHASE::IMPACT AND ONLY IMPACT, because that is what a launch monitor reading IS: every
// one of the 25 reading measures anchors at p7, and the swing.json writer tags them there
// too (see readingsFor()). This is not a simplification of a phase walk — there is no
// second phase for these metrics to be read at.
//
// It grades through the measure the CORRIDOR resolved on, not through a second lookup of
// its own. corridorForMetricAtPhase() walks the measures in preference order and reports
// which one answered; grading against a different one would draw a corridor from one norm
// and colour it from another.
QHash<QString, QString> LmSessionModel::gradesFor(const std::vector<LmFieldStats> &stats,
                                                  const QString &contextId)
{
    QHash<QString, QString> out;
    if (!m_pack || !m_norms)
        return out;

    const CharacteristicPack &pack   = m_pack->pack();
    const GradePolicy         policy = gradePolicyByName(m_policyName);

    for (const LmFieldStats &st : stats) {
        // No reading, no grade. A session mean is not what a corridor judges — the panel
        // colours THIS shot, and a mean that happens to sit outside a corridor says
        // something about the session that a tile's frame is the wrong place to say.
        if (!st.hasLatest)
            continue;

        const std::optional<MetricCorridor> c =
            corridorForMetricAtPhase(pack, *m_norms, st.key, Phase::Impact, contextId, policy);
        if (!c)
            continue;                     // no measure reads it, or no norm anywhere on the chain

        const NormResolution res = m_norms->resolve(c->measureId, contextId);
        if (!res.found())
            continue;                     // cannot happen — the corridor resolved on it — but a
                                          // dereference guarded by "cannot happen" is a crash
        const Grade g = res.grade(st.latest, c->shape, policy);
        // NotMeasured means the norm calls the value implausible: a device glitch, not a
        // fault in the swing. It is left uncoloured rather than shown as an Action, which
        // is the one grade that would be a lie about the golfer rather than about the kit.
        if (g == Grade::NotMeasured)
            continue;
        out.insert(st.key, gradeName(g));
    }
    return out;
}

QVariantList LmSessionModel::bandCounts() const
{
    QVariantList out;
    out.reserve(m_bands.size());
    for (const Band &b : m_bands)
        out.append(int(b.tiles.size()));
    return out;
}

QString LmSessionModel::emptyText() const
{
    // A LOADED SESSION IS JUDGED ONLY ON WHAT IS IN IT. Whether a monitor is plugged in
    // now, and whether the next shot would be stored, are facts about capturing — they
    // cannot unwrite readings already on disk, and "Settings → Launch Monitor" is not a
    // fix for a session recorded last week. So both live gates are skipped, and the two
    // remaining lines are worded in the past tense the reader is actually in.
    if (m_reviewing) {
        if (!m_anyReadings)
            return tr("No launch monitor readings in this session");
        if (m_bands.isEmpty())
            return tr("No readings for this club in this session");
        return {};
    }

    // Ordered by what the user can do about it. "No monitor" outranks "nothing yet",
    // because only the second is a matter of hitting another ball.
    if (!m_connected)
        return tr("No launch monitor connected — Settings → Launch Monitor");
    if (!m_anyReadings)
        return tr("No readings yet this session");
    // Readings exist, but not for the club the focused shot was hit with. Saying "no
    // readings this session" there would be false, and drawing an empty board would
    // look like a fault — the scope is the answer, so the scope is what it names.
    if (m_bands.isEmpty())
        return tr("No readings yet for this club");
    return {};
}

void LmSessionModel::rebuild()
{
    beginResetModel();
    m_bands.clear();
    m_graphics.clear();
    m_scopeText.clear();
    m_corridorScope.clear();
    m_valueLabel = tr("latest");
    m_shotCount = 0;
    m_anyReadings = false;

    if (!m_shots) {
        endResetModel();
        emit headerChanged();
        emit stateTextChanged();
        emit graphicsChanged();
        return;
    }

    const int nrows = m_shots->rowCount();

    // Every row, read once: identity, the recorded club, the golfer's two annotations, the
    // device, the raw readings and our own low point estimate. Then the effective club of each
    // (resolveClubs) — and from here on NOTHING scopes by the recorded club. It used to, which
    // is why fixture shot 8 read "Heel" against the Driver scope: a 7-iron strike graded and
    // averaged as a drive because Driver was left selected on the device.
    std::vector<LmShotRow> rows;
    rows.reserve(size_t(nrows));
    for (int r = 0; r < nrows; ++r) {
        const QModelIndex ix = m_shots->index(r);
        const QVariantMap detail = m_shots->data(ix, ShotListModel::AnalysisDetailRole).toMap();
        LmShotRow row;
        row.row          = r;
        row.id           = m_shots->data(ix, ShotListModel::ShotIdRole).toInt();
        row.ordinal      = m_shots->data(ix, ShotListModel::OrdinalRole).toInt();
        row.recorded     = m_shots->data(ix, ShotListModel::ClubRole).toString().trimmed();
        row.clubOverride = m_shots->data(ix, ShotListModel::LmClubOverrideRole).toString().trimmed();
        row.excluded     = m_shots->data(ix, ShotListModel::LmExcludedRole).toBool();
        row.kind         = m_shots->data(ix, ShotListModel::LmDeviceKindRole).toString();
        row.values       = readingsFor(detail);
        row.optical      = opticalLowPointFor(detail);
        row.gates        = lmGateMap(detail.value(QStringLiteral("gates")).toList());
        row.swingDir     = m_shots->data(ix, ShotListModel::SwingDirRole).toString();
        // Session-wide, deliberately: it separates "the monitor has given us nothing"
        // from "this club has nothing yet", which are different sentences to read.
        if (!row.values.isEmpty())
            m_anyReadings = true;
        rows.push_back(std::move(row));
    }
    resolveClubs(rows);

    // The focused row, and the club that scopes everything — its EFFECTIVE club. Rows are
    // newest-first, so the first row of the scope is also the newest shot in it.
    int focusedRow = -1;
    for (size_t i = 0; i < rows.size() && m_focusedShotId >= 0; ++i)
        if (rows[i].id == m_focusedShotId) { focusedRow = int(i); break; }

    // Aggregating across a mixed bag is meaningless — a driver and a 9-iron in one
    // carry mean is a bug, not a feature. An unknown club on the focused shot falls
    // back to the whole session and SAYS SO in the header; it never silently mixes.
    const QString club = focusedRow >= 0 ? rows[size_t(focusedRow)].club.trimmed() : QString();

    // A LEFT-OUT focused shot is still "this shot": its readings headline the tiles and the
    // figures. It is only absent from the scope the statistics are computed over.
    const bool focusExcluded = focusedRow >= 0 && rows[size_t(focusedRow)].excluded;

    std::vector<LmShotValues> scoped;
    scoped.reserve(rows.size());
    // Kept in step with `scoped`, index for index, so the focused shot's estimate is
    // found by the same latestIndex the statistics use rather than by a second search
    // that could disagree with it about which shot is on screen.
    std::vector<std::optional<double>> opticalLowPoint;
    opticalLowPoint.reserve(rows.size());
    int latestIndex = -1;
    // `scoped` index → `rows` index, so the headline shot's records are found by the same latestIndex.
    std::vector<int> scopedRow;
    scopedRow.reserve(rows.size());
    // The devices that actually MEASURED the shots in scope, in the order they first
    // appear. Normally one; more than one is a session hit across a device change, and
    // naming both is the only honest caption for it.
    QStringList devices;
    for (size_t i = 0; i < rows.size(); ++i) {
        const LmShotRow &row = rows[i];
        if (!club.isEmpty() && !sameClub(row.club, club))
            continue;
        // Left out: in no mean, SD, grade, corridor, count or tile. Drawn hollow by the panel
        // from graphics.shots, which is the only place it still appears.
        if (row.excluded)
            continue;
        // Only a shot that produced readings names a device: an unmeasured swing sitting
        // in the same session says nothing about what measured the ones beside it.
        if (!row.values.isEmpty()) {
            const QString label = pinpoint::lm::kindShortLabel(pinpoint::lm::kindFromKey(row.kind));
            if (!label.isEmpty() && !devices.contains(label))
                devices << label;
        }
        if (int(i) == focusedRow)
            latestIndex = int(scoped.size());
        scoped.push_back(row.values);
        opticalLowPoint.push_back(row.optical);
        scopedRow.push_back(int(i));
    }
    m_shotCount = int(scoped.size());

    // Nothing focused: the newest shot in scope is the one to headline, which is row 0
    // because the list is newest-first. Then the header's word really is "latest".
    const bool focusIsNewest = !focusExcluded && latestIndex <= 0;
    if (latestIndex < 0 && !focusExcluded && !scoped.empty())
        latestIndex = 0;
    m_valueLabel = focusIsNewest ? tr("latest") : tr("this shot");

    QStringList bits;
    // WHICH DEVICE, ASKED OF THE RIGHT PLACE. Live, that is the connector currently
    // attached — it is the thing producing the numbers. In a loaded session it is
    // whatever each shot's own document recorded, because a session may have been hit on
    // another device entirely and captioning it with whatever is plugged in today would
    // be a claim we cannot support. A document written before the reader carried the kind
    // simply names no device, which is the truth about what we know of it.
    if (m_reviewing) {
        if (!devices.isEmpty()) bits << devices.join(QStringLiteral(" + "));
    } else if (!m_deviceName.isEmpty()) {
        bits << m_deviceName;
    }
    bits << (club.isEmpty() ? tr("all clubs") : club);
    // Spelled out rather than tr()'s %n plural form: with no translation catalogue
    // loaded, %n falls back to the SOURCE string, and the header read "1 shot(s)".
    bits << (m_shotCount == 1 ? tr("1 shot") : tr("%1 shots").arg(m_shotCount));
    m_scopeText = bits.join(QStringLiteral(" · "));

    // The statistics, then the same vector split into the board's bands. Walking
    // fieldGroups() outside and the stats inside keeps both orders where they are
    // declared — bands in display order, fields in fieldDefs() order within a band.
    const std::vector<LmFieldStats> stats =
        focusExcluded ? withFocus(lmSessionStats(scoped, -1), rows[size_t(focusedRow)].values)
                      : lmSessionStats(scoped, latestIndex);

    // WHICH CORRIDORS, AND ONLY WHEN THE SCOPE HAS A CLUB. An unknown club already puts
    // the board on the whole session and says "all clubs" in the header; grading a mixed
    // bag against the full-swing default would be the same error the mean refuses to make,
    // wearing a colour. Every corridor that matters here is club-shaped — the driver spin
    // rate row and the wedge one are 3,000 rpm apart — so with no club there is nothing
    // honest to resolve against and the board stays a readout.
    const QString contextId = club.isEmpty() ? QString() : contextIdForClub(club);
    const QHash<QString, QString> grades =
        contextId.isEmpty() ? QHash<QString, QString>() : gradesFor(stats, contextId);
    // Named only when a corridor actually resolved: a legend over a board showing no
    // colour is a promise the board is not keeping.
    if (!grades.isEmpty() && m_norms) {
        if (const ContextNode *cn = m_norms->contexts().node(contextId))
            m_corridorScope = cn->label;
        else
            m_corridorScope = club;
    }

    for (const char *g : pinpoint::lm::fieldGroups()) {
        const QString name = QString::fromLatin1(g);
        Band band;
        band.name = name;
        for (const LmFieldStats &st : stats)
            if (st.def && QString::fromLatin1(st.def->group) == name)
                band.tiles.append(tileFor(st, grades.value(st.key)));
        // A band no shot produced a reading for is not drawn at all — an empty gutter
        // rule with a zero beside it says the device measured nothing, which is a
        // different claim from "the device does not report spin".
        if (!band.tiles.isEmpty())
            m_bands.push_back(band);
    }

    buildGraphics(stats, scoped, grades,
                  focusExcluded ? rows[size_t(focusedRow)].optical
                  : (latestIndex >= 0 && size_t(latestIndex) < opticalLowPoint.size())
                      ? opticalLowPoint[size_t(latestIndex)] : std::nullopt);

    // The FSX session half: every shot, every effective club with its ellipses, the note and
    // the device. Computed from the SAME rows the board just scoped, so a shot cannot be left
    // out of one and counted in the other.
    const QVariantMap session = sessionGraphics(rows, focusedRow, m_deviceName, m_reviewing);
    for (auto it = session.constBegin(); it != session.constEnd(); ++it)
        m_graphics.insert(it.key(), it.value());

    // THE HEADLINE SHOT'S GATES (trust stage 6 session 2): for each reading it lacks, the gate that
    // dropped it ({ subject, gate, words, … }), and the swing the Trust panel opens it on. The tiles
    // print the words in place of "-". Only for a reading the shot does not carry: a value stands.
    {
        const int hr = focusExcluded ? focusedRow
                     : (latestIndex >= 0 && size_t(latestIndex) < scopedRow.size()) ? scopedRow[size_t(latestIndex)] : -1;
        QVariantMap gates;
        QString gateDir;
        if (hr >= 0 && size_t(hr) < rows.size()) {
            const LmShotRow &h = rows[size_t(hr)];
            for (auto it = h.gates.constBegin(); it != h.gates.constEnd(); ++it)
                if (!h.values.contains(it.key()))
                    gates.insert(it.key(), it.value());
            gateDir = h.swingDir;
        }
        m_graphics.insert(QStringLiteral("gates"), gates);
        m_graphics.insert(QStringLiteral("gateSwingDir"), gateDir);
    }

    endResetModel();
    emit headerChanged();
    emit stateTextChanged();
    emit graphicsChanged();
}

// The second projection of the same statistics: what the five schematics draw.
//
// ONE MAP RATHER THAN A SECOND MODEL. Both modes answer questions about one shot out of
// one scope, and the numbers behind them are the vector this is handed. A second model
// would recompute them, and two computations of one mean is two chances to print
// different numbers on two tabs of the same panel.
//
// EVERY DERIVED NUMBER IS DECIDED HERE, not in a binding: the flight polyline, both
// inferred reads, their evidence strings, the drawn-metric count. QML positions and
// paints (analysis pipeline guide §6.2).
void LmSessionModel::buildGraphics(const std::vector<LmFieldStats> &stats,
                                   const std::vector<LmShotValues> &scoped,
                                   const QHash<QString, QString> &grades,
                                   const std::optional<double> &opticalLowPoint)
{
    QVariantMap g;
    g.insert(QStringLiteral("leftHanded"), m_leftHanded);

    // Every reported field, keyed, with its raw value and its band. Keyed rather than
    // ordered because the schematics reach for a specific metric at a specific anchor —
    // the ordered walk is the tiles board's need, not this one's.
    QVariantMap values;
    int reported = 0;
    for (const LmFieldStats &st : stats) {
        if (!st.def) continue;
        values.insert(st.key, annotationFor(st, grades.value(st.key)));
        if (st.hasLatest) ++reported;
    }
    g.insert(QStringLiteral("values"), values);
    g.insert(QStringLiteral("has"), reported > 0);

    // The headline strip: the six figures a golfer reads first, in the design's order.
    // Named explicitly because this IS an editorial choice about what leads — unlike the
    // board below it, which never hand-lists a key.
    static const char *kHeadline[] = {
        "lm.ballSpeed", "lm.clubheadSpeed", "lm.smashFactor",
        "lm.carryDistance", "lm.totalDistance", "lm.spinRate",
    };
    QVariantList headline;
    for (const char *k : kHeadline) {
        const QString key = QString::fromLatin1(k);
        if (values.contains(key)) headline.append(values.value(key));
    }
    g.insert(QStringLiteral("headline"), headline);

    // ── the flight ──────────────────────────────────────────────────────────
    const std::optional<double> ballSpeed = valueOf(stats, "lm.ballSpeed");
    const std::optional<double> launchAng = valueOf(stats, "lm.launchAngle");
    const std::optional<double> startDir  = valueOf(stats, "lm.launchDirection");
    const std::optional<double> spinRate  = valueOf(stats, "lm.spinRate");
    const std::optional<double> spinAxis  = valueOf(stats, "lm.spinAxis");
    const std::optional<double> carry     = valueOf(stats, "lm.carryDistance");
    const std::optional<double> total     = valueOf(stats, "lm.totalDistance");
    const std::optional<double> offline   = valueOf(stats, "lm.offline");
    const std::optional<double> peak      = valueOf(stats, "lm.peakHeight");
    const std::optional<double> descent   = valueOf(stats, "lm.descentAngle");

    QVariantMap flight{ { QStringLiteral("has"), false } };
    if (ballSpeed && launchAng && spinRate) {
        const LmLaunch launch{ *ballSpeed, *launchAng, startDir.value_or(0.0),
                               *spinRate,  spinAxis.value_or(0.0) };
        const LmFlightIntegration raw = lmIntegrateFlight(launch);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const LmFlightPath p = lmNormalisedPath(raw,
                                                carry.value_or(nan),
                                                peak.value_or(nan),
                                                offline.value_or(nan),
                                                total.value_or(nan),
                                                *launchAng,
                                                descent.value_or(nan));
        if (p.has) {
            flight = QVariantMap{
                { QStringLiteral("has"),            true },
                { QStringLiteral("profile"),        thin(p.points, false) },
                { QStringLiteral("track"),          thin(p.points, true) },
                { QStringLiteral("landing"),        pointMap(p.landing) },
                { QStringLiteral("finish"),         pointMap(p.finish) },
                { QStringLiteral("launchTangent"),  pointMap(p.launchTangent) },
                { QStringLiteral("landingTangent"), pointMap(p.landingTangent) },
                { QStringLiteral("carryFraction"),  p.carryFraction },
                { QStringLiteral("apexAtX"),        p.apexAtX },
                { QStringLiteral("lateralExtentYd"), p.lateralExtentYd },
                { QStringLiteral("carryYd"),        p.carryYd },
                { QStringLiteral("totalYd"),        p.totalYd },
                { QStringLiteral("apexFt"),         p.apexFt },
                { QStringLiteral("offlineYd"),      p.offlineYd },
                // Carried out of the model but not drawn yet: the design brief's open
                // question 1 wants shots in known conditions before the card says
                // whether a large residual is wind or a soft spin-axis reading.
                { QStringLiteral("residualOfflineYd"), p.residualOfflineYd },
            };
        }
    }
    g.insert(QStringLiteral("flight"), flight);

    // ── the two inferred reads ──────────────────────────────────────────────
    // The spin axis leads and carry comes along, because severity is decided from how
    // far the ball actually bent off its own start line — see lm_inferred_reads.h.
    const LmFlightShape shape = lmFlightShape(startDir, spinAxis,
                                              valueOf(stats, "lm.faceToPath"),
                                              carry, offline, m_leftHanded);
    g.insert(QStringLiteral("shape"), QVariantMap{
        { QStringLiteral("has"),       shape.has },
        { QStringLiteral("name"),      shape.name },
        { QStringLiteral("evidence"),  shape.evidence },
        { QStringLiteral("windowIdx"), shape.windowIdx },
        { QStringLiteral("curveIdx"),  shape.curveIdx },
    });

    // The session's strike PATTERN, as an ellipse. Not two independent spreads: a golfer
    // who thins it off the heel misses on a diagonal, and that diagonal is the thing
    // worth showing them. Legitimate here because both axes are millimetres AND the face
    // is drawn at one scale in both directions — see lmPairStats() for why the same
    // treatment is wrong for the landing pattern, whose two axes are drawn at 1.8 and
    // 4.5 px/yd and which the FLIGHT card therefore shades axis-aligned.
    const LmPairStats face = lmPairStats(scoped, QStringLiteral("lm.strikeLocation"),
                                                 QStringLiteral("lm.strikeHeight"));
    g.insert(QStringLiteral("strikeEllipse"), QVariantMap{
        { QStringLiteral("has"),     face.has },
        { QStringLiteral("n"),       face.n },
        { QStringLiteral("meanX"),   face.meanX },
        { QStringLiteral("meanY"),   face.meanY },
        { QStringLiteral("majorSd"), face.majorSd },
        { QStringLiteral("minorSd"), face.minorSd },
        { QStringLiteral("tiltDeg"), face.tiltDeg },
    });

    // ── low point ───────────────────────────────────────────────────────────
    // THE ONE READING ON THIS PANEL THAT MAY COME FROM US. Everywhere else the rule holds
    // absolutely — the board shows what the device measured and never our estimate beside
    // it — and it holds here too for the tiles board, which sees `lm.lowPointAhead` and
    // nothing else. The IMPACT card is allowed the fallback because the drawing is a
    // GEOMETRIC one: it already shows attack angle and dynamic loft meeting at the ball,
    // and where the arc bottomed out is the third side of that figure. A side view with
    // the low point missing is an incomplete drawing, not a shorter list.
    //
    // WHICH ONE IT IS TRAVELS WITH IT. `source` is not decoration: an estimate printed as
    // a measurement is the failure this whole `lm.` namespace exists to prevent, and the
    // card badges it from this field rather than inferring it from anything else.
    const std::optional<double> measuredLowPoint = valueOf(stats, "lm.lowPointAhead");
    const std::optional<double> lowPoint = measuredLowPoint ? measuredLowPoint : opticalLowPoint;
    g.insert(QStringLiteral("lowPoint"), QVariantMap{
        { QStringLiteral("has"),    lowPoint.has_value() },
        { QStringLiteral("value"),  lowPoint.value_or(0.0) },
        { QStringLiteral("text"),   lowPoint ? lmFormat(*lowPoint, lmDecimals(QStringLiteral("in")))
                                             : lmAbsent() },
        { QStringLiteral("unit"),   QStringLiteral("in") },
        { QStringLiteral("source"), measuredLowPoint ? QStringLiteral("measured")
                                                     : QStringLiteral("inferred") },
    });

    const LmStrikeRead strike = lmStrikeQuality(valueOf(stats, "lm.strikeLocation"),
                                                valueOf(stats, "lm.strikeHeight"),
                                                valueOf(stats, "lm.smashFactor"),
                                                meanOf(stats, "lm.smashFactor"));
    g.insert(QStringLiteral("strike"), QVariantMap{
        { QStringLiteral("has"),      strike.has },
        { QStringLiteral("name"),     strike.name },
        { QStringLiteral("evidence"), strike.evidence },
    });

    m_graphics = g;
}

// ── The panel's actions ────────────────────────────────────────────────────────
//
// Thin on purpose: the shot list owns the rows and their write-through, and its dataChanged
// is what rebuilds this board. Doing the rebuild here as well would be a second path to the
// same numbers, and the first one to drift would be the one nobody tested.

void LmSessionModel::setShotExcluded(int shotId, bool excluded)
{
    if (m_shots)
        m_shots->setLmExcluded(shotId, excluded);
}

void LmSessionModel::setShotClub(int shotId, const QString &club)
{
    if (m_shots)
        m_shots->setLmClubOverride(shotId, club);
}

void LmSessionModel::undoMismatch()
{
    if (!m_shots)
        return;
    const QVariantMap mm = m_graphics.value(QStringLiteral("mismatch")).toMap();
    // Only a note that RELABELLED anything has something to undo; a flag-only note (a generic
    // "reads like an iron") changed no club.
    if (!mm.value(QStringLiteral("relabelled")).toBool())
        return;
    const QVariantList ids = mm.value(QStringLiteral("shotIds")).toList();
    m_batching = true;
    for (const QVariant &v : ids) {
        const int id = v.toInt();
        for (int r = 0; r < m_shots->rowCount(); ++r) {
            const QModelIndex ix = m_shots->index(r);
            if (m_shots->data(ix, ShotListModel::ShotIdRole).toInt() != id)
                continue;
            // The recorded club, EXPLICITLY — the stored decline that keeps the evidence from
            // re-applying on the next load. "" would only restore the default, which is the
            // evidence's relabel again.
            m_shots->setLmClubOverride(id, m_shots->data(ix, ShotListModel::ClubRole).toString());
            break;
        }
    }
    m_batching = false;
    rebuild();
}
