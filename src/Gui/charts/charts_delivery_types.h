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

// THE CLUB DELIVERY PRESENTER'S TYPED RESULTS (refactor 5.5b, lint A6): what ChartsDeliveryPresenter hands QML —
// choices, thisShot, session, plotLayers, standfirst — and the picture strip delivery_picture.h builds, as Q_GADGET
// values. Each property is named as the old map's key, so the QML reads them as it read the maps; the shapes are the
// ones charts_delivery_presenter.h documents (OUTPUT SHAPES).
//
// THE DIFFERENCES A GADGET MAKES, and how each keeps the page as it was:
//   · a missing number was null: it is a QVariant holding null (never an invalid QVariant, which reads `undefined`);
//   · an object that was null (scatter.picked, scatter.line, a frame's still) is a QVariant holding the gadget or null;
//   · a per-shot list of numbers with gaps (band.positions[].values) is a list of doubles, NaN for an unread shot —
//     the one reader (PpChBoxCell.valueOf) takes null and NaN alike;
//   · a field the map left out (an unavailable thisShot or session, a box of no shots) is present at its default;
//     every QML reader tests `available` (or the box's n) first.
//
// The rules the QML used to apply now live here, decided by the presenter: Scatter.xSigned (the x axis is a signed
// quantity: the hands' steepening, by which series it is — never by its label's words) and ShapeMark.hoverText (the
// grid's hover line, "Shot 7  Pull, slight fade"). Sentence.clause is the sentence's first clause
// (words::firstClauseOf), which a folded section's head keeps.
//
// The types are in their own namespace: session_delivery.h owns the core's Delivery* structs.
// moc reaches this header through charts_delivery_presenter.cpp's #include of its moc file.

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace pinpoint::charts::deliveryui {

inline QVariant nullValue() { return QVariant::fromValue(nullptr); }

// ── words ──────────────────────────────────────────────────────────────────────────────────────

// A run of a sentence: words, or a figure set in the figure face.
struct Run
{
    Q_GADGET
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(bool    fig  MEMBER fig  FINAL)
public:
    QString text;
    bool    fig = false;
    friend bool operator==(const Run &, const Run &) = default;
};

// words::Sentence as QML takes it: its text, its runs and its first clause.
struct Sentence
{
    Q_GADGET
    Q_PROPERTY(QString    text   MEMBER text   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::Run> runs MEMBER runs FINAL)
    Q_PROPERTY(QString    clause MEMBER clause FINAL)
public:
    QString    text;
    QList<Run> runs;
    QString    clause;
    friend bool operator==(const Sentence &, const Sentence &) = default;
};

// ── choices ────────────────────────────────────────────────────────────────────────────────────

struct ScopeChoice
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    QString id, label;
    bool    current = false;
    friend bool operator==(const ScopeChoice &, const ScopeChoice &) = default;
};

struct Choices
{
    Q_GADGET
    Q_PROPERTY(QString     scope      MEMBER scope      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::ScopeChoice> scopes MEMBER scopes FINAL)
    Q_PROPERTY(QStringList onShowKeys MEMBER onShowKeys FINAL)
public:
    QString            scope;
    QList<ScopeChoice> scopes;
    QStringList        onShowKeys;
    friend bool operator==(const Choices &, const Choices &) = default;
};

// ── the plane story and the slot ───────────────────────────────────────────────────────────────

// POS: the plane (and the hands' plane) at one P-position.
struct Position
{
    Q_GADGET
    Q_PROPERTY(int      index     MEMBER index     FINAL)
    Q_PROPERTY(QString  tag       MEMBER tag       FINAL)
    Q_PROPERTY(int      phase     MEMBER phase     FINAL)
    Q_PROPERTY(QString  name      MEMBER name      FINAL)
    Q_PROPERTY(QVariant value     MEMBER value     FINAL)
    Q_PROPERTY(QString  text      MEMBER text      FINAL)
    Q_PROPERTY(QVariant hands     MEMBER hands     FINAL)
    Q_PROPERTY(QString  handsText MEMBER handsText FINAL)
    Q_PROPERTY(qint64   tUs       MEMBER tUs       FINAL)
    Q_PROPERTY(qint64   seekUs    MEMBER seekUs    FINAL)
public:
    int      index = 0;
    QString  tag;
    int      phase = -1;
    QString  name;
    QVariant value = nullValue();
    QString  text;
    QVariant hands = nullValue();
    QString  handsText;
    qint64   tUs = -1;
    qint64   seekUs = -1;
    friend bool operator==(const Position &, const Position &) = default;
};

struct Story
{
    Q_GADGET
    Q_PROPERTY(bool        available MEMBER available FINAL)
    Q_PROPERTY(QString     text      MEMBER text      FINAL)
    Q_PROPERTY(QStringList parts     MEMBER parts     FINAL)
    Q_PROPERTY(QString     set       MEMBER set       FINAL)
    Q_PROPERTY(QString     back      MEMBER back      FINAL)
    Q_PROPERTY(QString     top       MEMBER top       FINAL)
    Q_PROPERTY(QString     delivered MEMBER delivered FINAL)
    Q_PROPERTY(QString     hands     MEMBER hands     FINAL)
    Q_PROPERTY(bool        shapeRead MEMBER shapeRead FINAL)
    Q_PROPERTY(QString     shapeWord MEMBER shapeWord FINAL)
    Q_PROPERTY(QString     shapeLine MEMBER shapeLine FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::Position> positions MEMBER positions FINAL)
public:
    bool            available = false;
    QString         text;
    QStringList     parts;
    QString         set, back, top, delivered, hands;
    bool            shapeRead = false;
    QString         shapeWord, shapeLine;
    QList<Position> positions;
    friend bool operator==(const Story &, const Story &) = default;
};

struct SlotMark
{
    Q_GADGET
    Q_PROPERTY(int     index MEMBER index FINAL)
    Q_PROPERTY(QString tag   MEMBER tag   FINAL)
    Q_PROPERTY(QString side  MEMBER side  FINAL)
    Q_PROPERTY(QString words MEMBER words FINAL)
public:
    int     index = 0;
    QString tag, side, words;
    friend bool operator==(const SlotMark &, const SlotMark &) = default;
};

struct Slot
{
    Q_GADGET
    Q_PROPERTY(bool     available     MEMBER available     FINAL)
    Q_PROPERTY(QVariant address       MEMBER address       FINAL)
    Q_PROPERTY(QVariant shoulders     MEMBER shoulders     FINAL)
    Q_PROPERTY(QString  addressText   MEMBER addressText   FINAL)
    Q_PROPERTY(QString  shouldersText MEMBER shouldersText FINAL)
    Q_PROPERTY(QString  widthText     MEMBER widthText     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::SlotMark> outside MEMBER outside FINAL)
    Q_PROPERTY(QString  text          MEMBER text          FINAL)
public:
    bool            available = false;
    QVariant        address = nullValue(), shoulders = nullValue();
    QString         addressText, shouldersText, widthText;
    QList<SlotMark> outside;
    QString         text;
    friend bool operator==(const Slot &, const Slot &) = default;
};

// ── the ball flight ────────────────────────────────────────────────────────────────────────────

struct Fit
{
    Q_GADGET
    Q_PROPERTY(QString  key       MEMBER key       FINAL)
    Q_PROPERTY(QString  x         MEMBER x         FINAL)
    Q_PROPERTY(QString  y         MEMBER y         FINAL)
    Q_PROPERTY(QString  xLabel    MEMBER xLabel    FINAL)
    Q_PROPERTY(QString  yLabel    MEMBER yLabel    FINAL)
    Q_PROPERTY(int      n         MEMBER n         FINAL)
    Q_PROPERTY(QVariant slope     MEMBER slope     FINAL)
    Q_PROPERTY(QVariant intercept MEMBER intercept FINAL)
    Q_PROPERTY(QVariant r         MEMBER r         FINAL)
    Q_PROPERTY(QVariant sd        MEMBER sd        FINAL)
    Q_PROPERTY(QString  slopeText MEMBER slopeText FINAL)
    Q_PROPERTY(QString  rText     MEMBER rText     FINAL)
    Q_PROPERTY(QString  sdText    MEMBER sdText    FINAL)
    Q_PROPERTY(bool     holds     MEMBER holds     FINAL)
    Q_PROPERTY(QString  verdict   MEMBER verdict   FINAL)
    Q_PROPERTY(QString  why       MEMBER why       FINAL)
public:
    QString  key, x, y, xLabel, yLabel;
    int      n = 0;
    QVariant slope = nullValue(), intercept = nullValue(), r = nullValue(), sd = nullValue();
    QString  slopeText, rText, sdText;
    bool     holds = false;
    QString  verdict, why;
    friend bool operator==(const Fit &, const Fit &) = default;
};

struct Fits
{
    Q_GADGET
    Q_PROPERTY(pinpoint::charts::deliveryui::Fit pathP6    MEMBER pathP6    FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Fit pathSteep MEMBER pathSteep FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Fit attackP6  MEMBER attackP6  FINAL)
public:
    Fit pathP6, pathSteep, attackP6;
    friend bool operator==(const Fits &, const Fits &) = default;
};

struct Place
{
    Q_GADGET
    Q_PROPERTY(bool    known MEMBER known FINAL)
    Q_PROPERTY(int     third MEMBER third FINAL)
    Q_PROPERTY(QString words MEMBER words FINAL)
public:
    bool    known = false;
    int     third = 0;
    QString words;
    friend bool operator==(const Place &, const Place &) = default;
};

struct Pool
{
    Q_GADGET
    Q_PROPERTY(QString date       MEMBER date       FINAL)
    Q_PROPERTY(int     sessions   MEMBER sessions   FINAL)
    Q_PROPERTY(int     shots      MEMBER shots      FINAL)
    Q_PROPERTY(int     otherShots MEMBER otherShots FINAL)
    Q_PROPERTY(QString words      MEMBER words      FINAL)
    Q_PROPERTY(int     staleGrids MEMBER staleGrids FINAL)
public:
    QString date;
    int     sessions = 0, shots = 0, otherShots = 0;
    QString words;
    int     staleGrids = 0;
    friend bool operator==(const Pool &, const Pool &) = default;
};

// A dot of the scatter: a shot of the day with both readings.
struct ScatterPoint
{
    Q_GADGET
    Q_PROPERTY(int     ordinal      MEMBER ordinal      FINAL)
    Q_PROPERTY(QString swingDir     MEMBER swingDir     FINAL)
    Q_PROPERTY(int     shotNo       MEMBER shotNo       FINAL)
    Q_PROPERTY(double  x            MEMBER x            FINAL)
    Q_PROPERTY(double  y            MEMBER y            FINAL)
    Q_PROPERTY(bool    thisSession  MEMBER thisSession  FINAL)
    Q_PROPERTY(QString session      MEMBER session      FINAL)
    Q_PROPERTY(QString sessionLabel MEMBER sessionLabel FINAL)
    Q_PROPERTY(bool    picked       MEMBER picked       FINAL)
public:
    int     ordinal = 0;
    QString swingDir;
    int     shotNo = 0;
    double  x = 0, y = 0;
    bool    thisSession = false;
    QString session, sessionLabel;
    bool    picked = false;
    friend bool operator==(const ScatterPoint &, const ScatterPoint &) = default;
};

// The picked shot on the scatter: x always, y null with no monitor on it.
struct ScatterPicked
{
    Q_GADGET
    Q_PROPERTY(double   x MEMBER x FINAL)
    Q_PROPERTY(QVariant y MEMBER y FINAL)
public:
    double   x = 0;
    QVariant y = nullValue();
    friend bool operator==(const ScatterPicked &, const ScatterPicked &) = default;
};

struct ScatterLine
{
    Q_GADGET
    Q_PROPERTY(double x0 MEMBER x0 FINAL)
    Q_PROPERTY(double y0 MEMBER y0 FINAL)
    Q_PROPERTY(double x1 MEMBER x1 FINAL)
    Q_PROPERTY(double y1 MEMBER y1 FINAL)
public:
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    friend bool operator==(const ScatterLine &, const ScatterLine &) = default;
};

struct Scatter
{
    Q_GADGET
    Q_PROPERTY(QString  xLabel  MEMBER xLabel  FINAL)
    Q_PROPERTY(QString  yLabel  MEMBER yLabel  FINAL)
    Q_PROPERTY(QString  unit    MEMBER unit    FINAL)
    Q_PROPERTY(bool     holds   MEMBER holds   FINAL)
    // the x axis reads a signed quantity (the hands' steepening): its figures carry their sign (rule C3)
    Q_PROPERTY(bool     xSigned MEMBER xSigned FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::ScatterPoint> points MEMBER points FINAL)
    Q_PROPERTY(QVariant picked  MEMBER picked  FINAL)   // ScatterPicked | null
    Q_PROPERTY(QVariant line    MEMBER line    FINAL)   // ScatterLine | null (only when the fit holds)
    Q_PROPERTY(QVariant xLo     MEMBER xLo     FINAL)
    Q_PROPERTY(QVariant xHi     MEMBER xHi     FINAL)
    Q_PROPERTY(QVariant yLo     MEMBER yLo     FINAL)
    Q_PROPERTY(QVariant yHi     MEMBER yHi     FINAL)
public:
    QString             xLabel, yLabel, unit;
    bool                holds = false;
    bool                xSigned = false;
    QList<ScatterPoint> points;
    QVariant            picked = nullValue(), line = nullValue();
    QVariant            xLo = nullValue(), xHi = nullValue(), yLo = nullValue(), yHi = nullValue();
    friend bool operator==(const Scatter &, const Scatter &) = default;
};

// FLIGHT
struct Flight
{
    Q_GADGET
    Q_PROPERTY(bool    available  MEMBER available  FINAL)
    Q_PROPERTY(QString text       MEMBER text       FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Pool pool MEMBER pool FINAL)
    Q_PROPERTY(QString attackText MEMBER attackText FINAL)
    Q_PROPERTY(QString thisLine   MEMBER thisLine   FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Fit fit MEMBER fit FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Fit attack MEMBER attack FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Fits fits MEMBER fits FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Place place MEMBER place FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Scatter scatter MEMBER scatter FINAL)
public:
    bool    available = false;
    QString text;
    Pool    pool;
    QString attackText, thisLine;
    Fit     fit, attack;
    Fits    fits;
    Place   place;
    Scatter scatter;
    friend bool operator==(const Flight &, const Flight &) = default;
};

// ── at impact, the shot shape, the trust lines ─────────────────────────────────────────────────

struct ImpactLine
{
    Q_GADGET
    Q_PROPERTY(QString  key        MEMBER key        FINAL)
    Q_PROPERTY(QString  text       MEMBER text       FINAL)
    Q_PROPERTY(QString  source     MEMBER source     FINAL)
    Q_PROPERTY(QVariant value      MEMBER value      FINAL)
    Q_PROPERTY(QVariant face       MEMBER face       FINAL)
    Q_PROPERTY(QVariant loft       MEMBER loft       FINAL)
    Q_PROPERTY(QString  loftSource MEMBER loftSource FINAL)
public:
    QString  key, text, source;
    QVariant value = nullValue(), face = nullValue(), loft = nullValue();
    QString  loftSource;
    friend bool operator==(const ImpactLine &, const ImpactLine &) = default;
};

struct Impact
{
    Q_GADGET
    Q_PROPERTY(bool available MEMBER available FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::ImpactLine> lines MEMBER lines FINAL)
public:
    bool              available = false;
    QList<ImpactLine> lines;
    friend bool operator==(const Impact &, const Impact &) = default;
};

// SHAPE
struct Shape
{
    Q_GADGET
    Q_PROPERTY(bool     available      MEMBER available      FINAL)
    Q_PROPERTY(QVariant face           MEMBER face           FINAL)
    Q_PROPERTY(QVariant faceToPath     MEMBER faceToPath     FINAL)
    Q_PROPERTY(QString  faceText       MEMBER faceText       FINAL)
    Q_PROPERTY(QString  faceToPathText MEMBER faceToPathText FINAL)
    Q_PROPERTY(bool     fromBall       MEMBER fromBall       FINAL)
    Q_PROPERTY(QString  launchDirText  MEMBER launchDirText  FINAL)
    Q_PROPERTY(QString  spinAxisText   MEMBER spinAxisText   FINAL)
    Q_PROPERTY(int      start          MEMBER start          FINAL)
    Q_PROPERTY(int      curve          MEMBER curve          FINAL)
    Q_PROPERTY(bool     slight         MEMBER slight         FINAL)
    Q_PROPERTY(QString  startWord      MEMBER startWord      FINAL)
    Q_PROPERTY(QString  curveWord      MEMBER curveWord      FINAL)
    Q_PROPERTY(QString  word           MEMBER word           FINAL)
public:
    bool     available = false;
    QVariant face = nullValue(), faceToPath = nullValue();
    QString  faceText, faceToPathText;
    bool     fromBall = false;
    QString  launchDirText, spinAxisText;
    int      start = 0, curve = 0;
    bool     slight = false;
    QString  startWord, curveWord, word;
    friend bool operator==(const Shape &, const Shape &) = default;
};

struct Trust
{
    Q_GADGET
    Q_PROPERTY(QStringList lines MEMBER lines FINAL)
public:
    QStringList lines;
    friend bool operator==(const Trust &, const Trust &) = default;
};

// Replay's glance (§4.8).
struct Replay
{
    Q_GADGET
    Q_PROPERTY(QString head  MEMBER head  FINAL)
    Q_PROPERTY(QString story MEMBER story FINAL)
    Q_PROPERTY(QString shape MEMBER shape FINAL)
public:
    QString head, story, shape;
    friend bool operator==(const Replay &, const Replay &) = default;
};

// ── the picture strip (delivery_picture.h: picture, FRAME, MARK) ───────────────────────────────

struct PictureCrop
{
    Q_GADGET
    Q_PROPERTY(int x MEMBER x FINAL)
    Q_PROPERTY(int y MEMBER y FINAL)
    Q_PROPERTY(int w MEMBER w FINAL)
    Q_PROPERTY(int h MEMBER h FINAL)
public:
    int x = 0, y = 0, w = 0, h = 0;
    friend bool operator==(const PictureCrop &, const PictureCrop &) = default;
};

// What the still maker is asked for (main.cpp's maker reads it as a map: chartsGadgetMap at the hand-off).
struct PictureStill
{
    Q_GADGET
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(QString camera   MEMBER camera   FINAL)
    Q_PROPERTY(QString phase    MEMBER phase    FINAL)
    Q_PROPERTY(int     x        MEMBER x        FINAL)
    Q_PROPERTY(int     y        MEMBER y        FINAL)
    Q_PROPERTY(int     w        MEMBER w        FINAL)
    Q_PROPERTY(int     h        MEMBER h        FINAL)
    Q_PROPERTY(int     maxPx    MEMBER maxPx    FINAL)
    Q_PROPERTY(QString outPath  MEMBER outPath  FINAL)
public:
    QString swingDir, camera, phase;
    int     x = 0, y = 0, w = 0, h = 0, maxPx = 0;
    QString outPath;
    friend bool operator==(const PictureStill &, const PictureStill &) = default;
};

// MARK: pts are [x, y] pairs in the frame's own crop-local pixels (PpChPostureFrame reads pts[i][0], pts[i][1]).
struct PictureMark
{
    Q_GADGET
    Q_PROPERTY(QString kind   MEMBER kind   FINAL)
    Q_PROPERTY(QString role   MEMBER role   FINAL)
    Q_PROPERTY(QString set    MEMBER set    FINAL)
    Q_PROPERTY(QString label  MEMBER label  FINAL)
    Q_PROPERTY(QString dash   MEMBER dash   FINAL)
    Q_PROPERTY(QString hueKey MEMBER hueKey FINAL)
    Q_PROPERTY(QList<QList<double>> pts MEMBER pts FINAL)
public:
    QString kind, role, set, label, dash, hueKey;
    QList<QList<double>> pts;
    friend bool operator==(const PictureMark &, const PictureMark &) = default;
};

// FRAME
struct PictureFrame
{
    Q_GADGET
    Q_PROPERTY(QString  key         MEMBER key         FINAL)
    Q_PROPERTY(QString  camera      MEMBER camera      FINAL)
    Q_PROPERTY(QString  tag         MEMBER tag         FINAL)
    Q_PROPERTY(int      pos         MEMBER pos         FINAL)
    Q_PROPERTY(QString  when        MEMBER when        FINAL)
    Q_PROPERTY(QString  caption     MEMBER caption     FINAL)
    Q_PROPERTY(QString  note        MEMBER note        FINAL)
    Q_PROPERTY(qint64   tUs         MEMBER tUs         FINAL)
    Q_PROPERTY(qint64   seekUs      MEMBER seekUs      FINAL)
    Q_PROPERTY(qint64   frameUs     MEMBER frameUs     FINAL)
    Q_PROPERTY(int      frameIndex  MEMBER frameIndex  FINAL)
    Q_PROPERTY(int      frameWidth  MEMBER frameWidth  FINAL)
    Q_PROPERTY(int      frameHeight MEMBER frameHeight FINAL)
    Q_PROPERTY(int      width       MEMBER width       FINAL)
    Q_PROPERTY(int      height      MEMBER height      FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::PictureCrop crop MEMBER crop FINAL)
    Q_PROPERTY(QString  src         MEMBER src         FINAL)
    Q_PROPERTY(QString  imageWhy    MEMBER imageWhy    FINAL)
    Q_PROPERTY(QVariant still       MEMBER still       FINAL)   // PictureStill | null
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::PictureMark> marks MEMBER marks FINAL)
public:
    QString     key, camera, tag;
    int         pos = 0;
    QString     when, caption, note;
    qint64      tUs = -1, seekUs = -1, frameUs = -1;
    int         frameIndex = -1, frameWidth = 0, frameHeight = 0, width = 0, height = 0;
    PictureCrop crop;
    QString     src, imageWhy;
    QVariant    still = nullValue();
    QList<PictureMark> marks;
    friend bool operator==(const PictureFrame &, const PictureFrame &) = default;
};

struct PictureGroup
{
    Q_GADGET
    Q_PROPERTY(QString camera MEMBER camera FINAL)
    Q_PROPERTY(QString label  MEMBER label  FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::PictureFrame> frames MEMBER frames FINAL)
public:
    QString camera, label;
    QList<PictureFrame> frames;
    friend bool operator==(const PictureGroup &, const PictureGroup &) = default;
};

// The coach sets the strip draws whatever the reader's toggles are (PpChPostureFrame: sets[mark.set] === true).
struct PictureSets
{
    Q_GADGET
    Q_PROPERTY(bool plane MEMBER plane FINAL)
public:
    bool plane = true;
    friend bool operator==(const PictureSets &, const PictureSets &) = default;
};

struct PictureLines
{
    Q_GADGET
    Q_PROPERTY(bool address   MEMBER address   FINAL)
    Q_PROPERTY(bool shoulders MEMBER shoulders FINAL)
    Q_PROPERTY(bool slot      MEMBER slot      FINAL)
public:
    bool address = false, shoulders = false, slot = false;
    friend bool operator==(const PictureLines &, const PictureLines &) = default;
};

// PICTURE
struct Picture
{
    Q_GADGET
    Q_PROPERTY(bool        available MEMBER available FINAL)
    Q_PROPERTY(QString     swingDir  MEMBER swingDir  FINAL)
    Q_PROPERTY(int         ordinal   MEMBER ordinal   FINAL)
    Q_PROPERTY(QString     legend    MEMBER legend    FINAL)
    Q_PROPERTY(QStringList why       MEMBER why       FINAL)
    Q_PROPERTY(bool        pending   MEMBER pending   FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::PictureSets defaultSets MEMBER defaultSets FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::PictureLines lines MEMBER lines FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::PictureGroup> groups MEMBER groups FINAL)
public:
    bool                available = false;
    QString             swingDir;
    int                 ordinal = 0;
    QString             legend;
    QStringList         why;
    bool                pending = false;
    PictureSets         defaultSets;
    PictureLines        lines;
    QList<PictureGroup> groups;
    friend bool operator==(const Picture &, const Picture &) = default;
};

// ── This shot ──────────────────────────────────────────────────────────────────────────────────

struct ThisShot
{
    Q_GADGET
    Q_PROPERTY(bool     available  MEMBER available  FINAL)
    Q_PROPERTY(QString  empty      MEMBER empty      FINAL)
    Q_PROPERTY(int      ordinal    MEMBER ordinal    FINAL)
    Q_PROPERTY(int      shotId     MEMBER shotId     FINAL)
    Q_PROPERTY(QString  swingDir   MEMBER swingDir   FINAL)
    Q_PROPERTY(bool     inSession  MEMBER inSession  FINAL)
    Q_PROPERTY(QString  kicker     MEMBER kicker     FINAL)
    Q_PROPERTY(QString  mode       MEMBER mode       FINAL)
    Q_PROPERTY(bool     hasPlane   MEMBER hasPlane   FINAL)
    Q_PROPERTY(bool     hasMonitor MEMBER hasMonitor FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(QString  headText   MEMBER headText   FINAL)
    Q_PROPERTY(int      headRule   MEMBER headRule   FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Story story MEMBER story FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Slot slot MEMBER slot FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Flight flight MEMBER flight FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Impact impact MEMBER impact FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Shape shape MEMBER shape FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Trust trust MEMBER trust FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Replay replay MEMBER replay FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::Sentence> standfirsts MEMBER standfirsts FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Picture picture MEMBER picture FINAL)
public:
    bool     available = false;
    QString  empty;
    int      ordinal = 0;
    int      shotId = -1;
    QString  swingDir;
    bool     inSession = false;
    QString  kicker, mode;
    bool     hasPlane = false, hasMonitor = false;
    Sentence head;
    QString  headText;
    int      headRule = 0;
    Story    story;
    Slot     slot;
    Flight   flight;
    Impact   impact;
    Shape    shape;
    Trust    trust;
    Replay   replay;
    QList<Sentence> standfirsts;
    Picture  picture;
    friend bool operator==(const ThisShot &, const ThisShot &) = default;
};

// ── Session ────────────────────────────────────────────────────────────────────────────────────

// box_map.h's BOX, typed: { n, min, max, q1, med, q3, lo, hi, outliers: [ordinal], texts }.
struct BoxTexts
{
    Q_GADGET
    Q_PROPERTY(QString lo  MEMBER lo  FINAL)
    Q_PROPERTY(QString q1  MEMBER q1  FINAL)
    Q_PROPERTY(QString med MEMBER med FINAL)
    Q_PROPERTY(QString q3  MEMBER q3  FINAL)
    Q_PROPERTY(QString hi  MEMBER hi  FINAL)
public:
    QString lo, q1, med, q3, hi;
    friend bool operator==(const BoxTexts &, const BoxTexts &) = default;
};

struct Box
{
    Q_GADGET
    Q_PROPERTY(int      n        MEMBER n        FINAL)
    Q_PROPERTY(QVariant min      MEMBER min      FINAL)
    Q_PROPERTY(QVariant max      MEMBER max      FINAL)
    Q_PROPERTY(QVariant q1       MEMBER q1       FINAL)
    Q_PROPERTY(QVariant med      MEMBER med      FINAL)
    Q_PROPERTY(QVariant q3       MEMBER q3       FINAL)
    Q_PROPERTY(QVariant lo       MEMBER lo       FINAL)
    Q_PROPERTY(QVariant hi       MEMBER hi       FINAL)
    Q_PROPERTY(QList<int> outliers MEMBER outliers FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::BoxTexts texts MEMBER texts FINAL)
public:
    int        n = 0;
    QVariant   min = nullValue(), max = nullValue(), q1 = nullValue(), med = nullValue(), q3 = nullValue();
    QVariant   lo = nullValue(), hi = nullValue();
    QList<int> outliers;
    BoxTexts   texts;
    friend bool operator==(const Box &, const Box &) = default;
};

struct BandPosition
{
    Q_GADGET
    Q_PROPERTY(int      index      MEMBER index      FINAL)
    Q_PROPERTY(QString  tag        MEMBER tag        FINAL)
    Q_PROPERTY(QString  name       MEMBER name       FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Box box MEMBER box FINAL)
    Q_PROPERTY(QVariant median     MEMBER median     FINAL)
    Q_PROPERTY(QString  medianText MEMBER medianText FINAL)
    Q_PROPERTY(QVariant wide       MEMBER wide       FINAL)
    Q_PROPERTY(QVariant half       MEMBER half       FINAL)
    Q_PROPERTY(QString  halfText   MEMBER halfText   FINAL)
    Q_PROPERTY(QVariant picked     MEMBER picked     FINAL)
    Q_PROPERTY(QList<double> values MEMBER values    FINAL)   // per shot; NaN unread
public:
    int           index = 0;
    QString       tag, name;
    Box           box;
    QVariant      median = nullValue();
    QString       medianText;
    QVariant      wide = nullValue(), half = nullValue();
    QString       halfText;
    QVariant      picked = nullValue();
    QList<double> values;
    // NaN != NaN, so `values` compares by its bits: two results with the same unread shots are equal.
    friend bool operator==(const BandPosition &a, const BandPosition &b)
    {
        if (a.values.size() != b.values.size()) return false;
        for (qsizetype i = 0; i < a.values.size(); ++i)
            if (!(a.values[i] == b.values[i] || (a.values[i] != a.values[i] && b.values[i] != b.values[i]))) return false;
        return a.index == b.index && a.tag == b.tag && a.name == b.name && a.box == b.box && a.median == b.median
               && a.medianText == b.medianText && a.wide == b.wide && a.half == b.half && a.halfText == b.halfText
               && a.picked == b.picked;
    }
};

struct Band
{
    Q_GADGET
    Q_PROPERTY(bool shown MEMBER shown FINAL)
    Q_PROPERTY(int  n     MEMBER n     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::BandPosition> positions MEMBER positions FINAL)
public:
    bool                shown = false;
    int                 n = 0;
    QList<BandPosition> positions;
    friend bool operator==(const Band &, const Band &) = default;
};

struct Varies
{
    Q_GADGET
    Q_PROPERTY(bool     has       MEMBER has       FINAL)
    Q_PROPERTY(QString  tightest  MEMBER tightest  FINAL)
    Q_PROPERTY(QString  loosest   MEMBER loosest   FINAL)
    Q_PROPERTY(QVariant tightHalf MEMBER tightHalf FINAL)
    Q_PROPERTY(QVariant looseHalf MEMBER looseHalf FINAL)
    Q_PROPERTY(QString  words     MEMBER words     FINAL)
public:
    bool     has = false;
    QString  tightest, loosest;
    QVariant tightHalf = nullValue(), looseHalf = nullValue();
    QString  words;
    friend bool operator==(const Varies &, const Varies &) = default;
};

struct Usual
{
    Q_GADGET
    Q_PROPERTY(QVariant address        MEMBER address        FINAL)
    Q_PROPERTY(QVariant shoulders      MEMBER shoulders      FINAL)
    Q_PROPERTY(QVariant steepening     MEMBER steepening     FINAL)
    Q_PROPERTY(QString  addressText    MEMBER addressText    FINAL)
    Q_PROPERTY(QString  shouldersText  MEMBER shouldersText  FINAL)
    Q_PROPERTY(QString  steepeningText MEMBER steepeningText FINAL)
public:
    QVariant address = nullValue(), shoulders = nullValue(), steepening = nullValue();
    QString  addressText, shouldersText, steepeningText;
    friend bool operator==(const Usual &, const Usual &) = default;
};

// A session shot on the shot-shape grid.
struct ShapeMark
{
    Q_GADGET
    Q_PROPERTY(int     ordinal   MEMBER ordinal   FINAL)
    Q_PROPERTY(QString swingDir  MEMBER swingDir  FINAL)
    Q_PROPERTY(int     shotNo    MEMBER shotNo    FINAL)
    Q_PROPERTY(bool    available MEMBER available FINAL)
    Q_PROPERTY(int     start     MEMBER start     FINAL)
    Q_PROPERTY(int     curve     MEMBER curve     FINAL)
    Q_PROPERTY(QString word      MEMBER word      FINAL)
    Q_PROPERTY(bool    fromBall  MEMBER fromBall  FINAL)
    Q_PROPERTY(bool    picked    MEMBER picked    FINAL)
    // the grid's words while this dot is hovered: "Shot 7  Pull, slight fade" (rule C6, the swing number)
    Q_PROPERTY(QString hoverText MEMBER hoverText FINAL)
public:
    int     ordinal = 0;
    QString swingDir;
    int     shotNo = 0;
    bool    available = false;
    int     start = 0, curve = 0;
    QString word;
    bool    fromBall = false, picked = false;
    QString hoverText;
    friend bool operator==(const ShapeMark &, const ShapeMark &) = default;
};

struct Shapes
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::ShapeMark> marks MEMBER marks FINAL)
    Q_PROPERTY(QString line MEMBER line FINAL)
public:
    QList<ShapeMark> marks;
    QString          line;
    friend bool operator==(const Shapes &, const Shapes &) = default;
};

struct Session
{
    Q_GADGET
    Q_PROPERTY(bool     available    MEMBER available    FINAL)
    Q_PROPERTY(int      n            MEMBER n            FINAL)
    Q_PROPERTY(int      shots        MEMBER shots        FINAL)
    Q_PROPERTY(int      planeShots   MEMBER planeShots   FINAL)
    Q_PROPERTY(int      monitorShots MEMBER monitorShots FINAL)
    Q_PROPERTY(bool     small        MEMBER isSmall      FINAL)   // the member is not `small`: a Windows header defines it
    Q_PROPERTY(QString  kicker       MEMBER kicker       FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(QString  headText     MEMBER headText     FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Band band MEMBER band FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Varies varies MEMBER varies FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Usual usual MEMBER usual FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Flight flight MEMBER flight FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Shapes shapes MEMBER shapes FINAL)
    Q_PROPERTY(pinpoint::charts::deliveryui::Trust trust MEMBER trust FINAL)
public:
    bool     available = false;
    int      n = 0, shots = 0, planeShots = 0, monitorShots = 0;
    bool     isSmall = false;
    QString  kicker;
    Sentence head;
    QString  headText;
    Band     band;
    Varies   varies;
    Usual    usual;
    Flight   flight;
    Shapes   shapes;
    Trust    trust;
    friend bool operator==(const Session &, const Session &) = default;
};

// ── the plot's lines ───────────────────────────────────────────────────────────────────────────

// LAYER, in PpChPlot's own shapes; one type for the three kinds, each reading only its own fields:
//   hline: v, text · band: t, lo, hi · span: t0, t1, labelled. role and label on all.
struct PlotLayer
{
    Q_GADGET
    Q_PROPERTY(QString       k        MEMBER k        FINAL)
    Q_PROPERTY(double        v        MEMBER v        FINAL)
    Q_PROPERTY(QString       role     MEMBER role     FINAL)
    Q_PROPERTY(QString       label    MEMBER label    FINAL)
    Q_PROPERTY(QString       text     MEMBER text     FINAL)
    Q_PROPERTY(QList<double> t        MEMBER t        FINAL)
    Q_PROPERTY(QList<double> lo       MEMBER lo       FINAL)
    Q_PROPERTY(QList<double> hi       MEMBER hi       FINAL)
    Q_PROPERTY(double        t0       MEMBER t0       FINAL)
    Q_PROPERTY(double        t1       MEMBER t1       FINAL)
    Q_PROPERTY(bool          labelled MEMBER labelled FINAL)
public:
    QString       k;
    double        v = 0;
    QString       role, label, text;
    QList<double> t, lo, hi;
    double        t0 = 0, t1 = 0;
    bool          labelled = false;
    friend bool operator==(const PlotLayer &, const PlotLayer &) = default;
};

// By the metric row they are drawn over (PpTypeset.listAt(layers, rowKey)): only the swing plane curve's.
struct PlotLayers
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::deliveryui::PlotLayer> swingPlaneCurve MEMBER swingPlaneCurve FINAL)
public:
    QList<PlotLayer> swingPlaneCurve;
    friend bool operator==(const PlotLayers &, const PlotLayers &) = default;
};

} // namespace pinpoint::charts::deliveryui
