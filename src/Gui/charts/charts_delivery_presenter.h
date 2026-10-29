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

#include "session_series_types.h"

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>

// THE CHARTS PANEL'S CLUB DELIVERY PRESENTER — the Club delivery section of This shot and Session
// (docs/design/club_delivery_chart_design.md §4, stage 2), built the way ChartsPosturePresenter is. The core
// (session_delivery.h) decides every number and word; this serialises it, adds the plot's lines, and the QML
// only places what it is given.
//
// ── INPUTS ───────────────────────────────────────────────────────────────────────────────────
// model (SessionSeriesModel), pickedSwingDir + pickedDetail (shotReplay.analysisDetail: a shot not yet in the
// snapshot still reads), register ("analyse" | "replay"), playheadUs (the picked shot's window-relative clock),
// scope ("shot" | "session", following body.shownView).
//
// ── THREADING (the posture presenter's, exactly) ─────────────────────────────────────────────
// Any input or snapshot change schedules ONE coalesced job on a private single-thread pool; results are swapped
// in by a queued invocation and `resultsChanged` fires once per delivered job. The playhead is a GUI-thread
// lookup into the delivered P times: `selected` / `standfirst` change (selectedChanged) only when it crosses
// into another position. setSynchronous(true) runs the job inline when an output is next read (tests). Each
// shot's reading (deliveryInputFromShot) is cached by the shot's shared_ptr.
//
// ── OUTPUT SHAPES ────────────────────────────────────────────────────────────────────────────
// A Sentence is words::Sentence::toVariant(): { text, runs: [{ text, fig }] }. A missing number is null and its
// text "-", never 0. Plane angles whole degrees; the monitor's one decimal (session_delivery.h decision 9).
// A BOX is box_map.h's { n, min, max, q1, med, q3, lo, hi, outliers: [ordinal], texts }.
//
//  available: bool — some shot of the session (or the picked shot's own detail) reads the plane or a monitor
//             delivery reading
//
//  choices: { scope, scopes: [{ id: "shot"|"session", label, current }],
//             onShowKeys: [metric key] (the plane keys and the lm.* delivery keys: a curve on show among them adds the section) }
//
//  thisShot: {
//    available, empty ("Select a swing to review. …" | "No swing plane or launch monitor reading for this swing. …" | ""),
//    ordinal, shotId, swingDir, inSession, kicker ("Shot 7 of 31  ·  gap wedge  ·  Club delivery"),
//    mode ("full" | "noDtl" (§4.3 rule 4: no plane; the grid, the impact lines and the trust) | "noMonitor"),
//    hasPlane, hasMonitor,
//    head: Sentence, headText, headRule (1..4, §4.3),
//    story: STORY, slot: SLOT, flight: FLIGHT, impact: { available, lines: [IMPACT] }, shape: SHAPE, trust: TRUST,
//    replay: { head (= headText), story (= story.text | ""), shape (= shape.word | "") }   (§4.8: Replay's glance)
//    standfirsts: [7 × Sentence] (P1..P7; the live one is `standfirst`) }
//
//  STORY  = { available, text ("set on 59° · 13° steeper than address at P3 · peaked at the top · delivered at 61°, 2° above where you set
//             it. Hands 12° steeper coming down."), parts: [string], set, back, top, delivered, hands, shapeRead, shapeWord,
//             shapeLine (the published shape, Analyse only), positions: [POS] }
//  POS    = { index (0..6), tag ("P4"), phase (Phase id), name ("the top"), value (| null), text ("65°" | "-"),
//             hands (the hand plane | null), handsText, tUs (−1 none), seekUs (= tUs) }
//  SLOT   = { available, address, shoulders (| null), addressText, shouldersText, widthText ("10° wide" | ""),
//             outside: [{ index, tag, side ("above"|"under"), words }], text ("inside the slot" | "above the shoulder plane
//             at P3 — a steep backswing") }
//  FLIGHT = { available, text (the fit's sentence: shown, refused or too few), attackText ("" unless its fit holds),
//             thisLine ("This one: 65° at P6, your steepest third → path −4.3°." | the fit's prediction, said as one),
//             fit: FIT (the stronger path fit, decision 7), attack: FIT, fits: { pathP6: FIT, pathSteep: FIT, attackP6: FIT },
//             place: { known, third (−1|0|1), words }, scatter: SCATTER,
//             pool: { date ("2026-10-20" | ""), sessions (this one included), shots, otherShots, words ("on 20 Oct (3 sessions)" |
//                     "" for a day of one session), staleGrids (other sessions' sidecars older than their swing) } }
//           The fits, the place and the scatter are over THE GOLFER'S DAY (session_delivery.h decision 13): this session's
//           shots and the golfer's other sessions on the same date — sibling folders under the golfer folder whose names
//           start with the same yyyy-MM-dd — read from each swing's swing_phasegrid.json on the worker (never a sibling's
//           swing document), cached per session folder until the day's folders change.
//  FIT    = { key ("path~planeP6" | "path~steepening" | "attack~planeP6"), x ("planeP6"|"steepening"), y ("path"|"attack"),
//             xLabel ("Swing plane at P6" | "Hand plane steepening"), yLabel ("Club path (monitor)" | "Attack angle (monitor)"),
//             n, slope, intercept, r, sd (| null), slopeText ("−0.92"), rText ("−0.80"), sdText ("±1.4°" | ""), holds,
//             verdict ("shown"|"weak"|"wrongSign"|"tooFew"|"none"), why }
//  SCATTER = { xLabel, yLabel, unit ("°"), holds, points: [{ ordinal, swingDir, x, y, thisSession (false: another session of
//             the day), session (its folder name), picked }], picked: { x, y } | null
//             (This shot, even with no monitor: x only, y null), line: { x0, y0, x1, y1 } | null (only when the fit holds),
//             xLo, xHi, yLo, yHi (| null with no point) }
//  IMPACT = { key ("lie"|"lean"|"attack"), text, source ("monitor"|"camera"|"both"), value (| null), face (| null; lie: °,
//             + = left for a right-hander), loft (| null), loftSource ("monitor"|"club"|"") }
//  SHAPE  = { available, face, faceToPath (| null), faceText, faceToPathText, start (−1 left | 0 | +1 right), curve (−1 | 0 | +1),
//             slight, startWord ("pull"|"straight"|"push"), curveWord ("slight fade"|…), word ("Pull, slight fade") }
//  TRUST  = { lines: [string] } (said once, in order)
//
//  session: {
//    available, n (shots in the snapshot), shots (with the plane or a monitor delivery reading), planeShots, monitorShots,
//    small (shots < 10), kicker ("31 shots  ·  gap wedge  ·  Club delivery"), head: Sentence, headText,
//    band: { shown (≥ 5 shots with the curve), n, positions: [{ index, tag, name, box: BOX, median (| null), medianText,
//            wide (q3 − q1 | null), half, halfText ("±1.5°" | ""), picked (this shot's value | null), values: [per shot, null unread] }] },
//    varies: { has, tightest, loosest (tags | ""), tightHalf, looseHalf (| null), words },
//    usual: { address, shoulders, steepening (| null), addressText, shouldersText, steepeningText },
//    flight: FLIGHT (the day's pool, as This shot's; the picked one ringed; thisLine ""),
//    shapes: { marks: [{ ordinal, swingDir, available, start, curve, word, fromBall, picked }], line ("Pull, slight fade on 5 of 8 shots" | "") },
//    trust: TRUST }
//
//  plotLayers: { "swingPlaneCurve": [LAYER] } — the lines the trace figure draws over the swing plane curve's row, in
//    PpChPlot's own layer shapes plus `role` and `label` for the overlay (no colour: the figure picks it by role).
//    Scope "shot": the picked shot's own address line, shoulder plane, slot and the hands' stretches; "session": the
//    session's medians, no stretches. Empty with no plane. t is ms from the shot's impact (the shot figure's axis).
//    LAYER = { k: "hline", v, role: "address", label: "address", text: "59°" }
//          | { k: "hline", v, role: "shoulders", label: "shoulders", text: "68°" }
//          | { k: "band", t: [t0, t1], lo: [address, address], hi: [shoulders, shoulders], role: "slot", label: "the slot" }
//          | { k: "span", t0, t1, role: "hands", label: "hands carry the plane here", labelled (the first only) }
//
//  selected: int (the playhead's position, 0..6), standfirst: Sentence (thisShot.standfirsts[selected]).
namespace pinpoint::charts {

// THE SESSION REPORT'S CLUB DELIVERY PAGE (§4.8, stage 4): the `session` map above for a snapshot, with no picked
// shot (every `picked` null) and no QObject, built on the caller's thread. `noun`: what its sentences count, plural —
// "shots" as on Charts, "swings" for the report.
QVariantMap deliverySessionReportPayload(const SessionSnapshot &snapshot, const QString &noun = QStringLiteral("shots"));

class ChartsDeliveryPresenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── inputs ──
    Q_PROPERTY(QObject *model READ model WRITE setModel NOTIFY inputsChanged)
    Q_PROPERTY(QString pickedSwingDir READ pickedSwingDir WRITE setPickedSwingDir NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap pickedDetail READ pickedDetail WRITE setPickedDetail NOTIFY inputsChanged)
    Q_PROPERTY(QString register READ registerName WRITE setRegisterName NOTIFY inputsChanged)
    Q_PROPERTY(qint64 playheadUs READ playheadUs WRITE setPlayheadUs NOTIFY playheadChanged)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY inputsChanged)

    // ── outputs ──
    Q_PROPERTY(bool available READ available NOTIFY resultsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap choices READ choices NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap thisShot READ thisShot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap session READ session NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap plotLayers READ plotLayers NOTIFY resultsChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap standfirst READ standfirst NOTIFY selectedChanged)

public:
    explicit ChartsDeliveryPresenter(QObject *parent = nullptr);
    ~ChartsDeliveryPresenter() override;

    QObject *model() const;
    void setModel(QObject *m);
    QString pickedSwingDir() const { return m_in.pickedSwingDir; }
    void setPickedSwingDir(const QString &d);
    QVariantMap pickedDetail() const { return m_in.pickedDetail; }
    void setPickedDetail(const QVariantMap &d);
    QString registerName() const { return m_in.reg; }
    void setRegisterName(const QString &r);
    qint64 playheadUs() const { return m_playheadUs; }
    void setPlayheadUs(qint64 us);
    QString scope() const { return m_in.scope; }
    void setScope(const QString &s);

    bool available();
    bool busy() const { return m_running; }
    QVariantMap choices();
    QVariantMap thisShot();
    QVariantMap session();
    QVariantMap plotLayers();
    int selected() const { return m_selected; }
    QVariantMap standfirst() const;

    // The picked shot's window-relative µs of position `tag` ("P1" … "P7"), for shotReplay.seekToUs; −1 when the
    // swing has no such position. A lookup into the delivered result.
    Q_INVOKABLE qint64 seekUsFor(const QString &tag) const;

    // ── test seams ──
    void setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot);   // used while no model is set
    Q_INVOKABLE void setSynchronous(bool on);
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);

    struct Inputs {
        std::shared_ptr<const SessionSnapshot> snapshot;
        QString pickedSwingDir;
        QVariantMap pickedDetail;
        QString reg = QStringLiteral("analyse");
        QString scope = QStringLiteral("shot");
    };
    struct Result;
    struct Worker;

signals:
    void inputsChanged();
    void busyChanged();
    void resultsChanged();
    void playheadChanged();
    void selectedChanged();

private:
    void schedule();
    void startJob();
    void apply(std::shared_ptr<Result> r);
    void flushIfSynchronous();
    void updateSelected(bool force);
    std::shared_ptr<const SessionSnapshot> currentSnapshot() const;

    Inputs m_in;
    QPointer<QObject> m_model;
    std::shared_ptr<const SessionSnapshot> m_directSnapshot;
    qint64 m_playheadUs = -1;
    int m_selected = 0;

    bool m_synchronous = false;
    bool m_dirty = true;
    bool m_scheduled = false;
    bool m_running = false;

    std::shared_ptr<Result> m_result;
    std::shared_ptr<Worker> m_worker;
    QThreadPool m_pool;
};

} // namespace pinpoint::charts
