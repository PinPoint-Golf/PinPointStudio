// Foresight Premier's session files → PremierShot / LaunchMonitorReading.
//
// Every fixture is a real file Premier wrote on the capture PC on 2026-11-02 (the watcher capture
// in docs/design/foresight_premier_connector.md §2.6), sanitised: backend ids nulled, the user id
// gone from the names. Byte-for-byte otherwise, BOM included.
//
// SIGNS ARE THE POINT OF HALF THIS FILE, as in the CSV parser's test. The sign shots of §2.6.5
// (pull, fade, draw, toe, heel) were hit on purpose and read off Premier's own screen; each is
// asserted here against what PinPoint's convention says it must be.

#include "premier_club.h"
#include "premier_session_parser.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QTimeZone>

#include <cmath>

using namespace pinpoint::lm;

namespace {

constexpr double kMph = 2.2369362920544;

QByteArray fixture(const char *rel)
{
    QFile f(QStringLiteral(PP_PREMIER_FIXTURES "/") + QString::fromLatin1(rel));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// The shot whose LocalShotId starts with `prefix`.
const PremierShot *find(const PremierFile &f, const char *prefix)
{
    for (const PremierShot &s : f.shots)
        if (s.localShotId.startsWith(QLatin1String(prefix)))
            return &s;
    return nullptr;
}

const DroppedField *dropFor(const LaunchMonitorReading &r, const char *key)
{
    for (const DroppedField &d : r.dropped)
        if (d.key == QLatin1String(key))
            return &d;
    return nullptr;
}

// The ended range session with its club data flushed: every shot of session 2, full stickers.
PremierFile session2Queue()
{
    const auto f = parsePremierSessionFile(fixture("s2_no_stickers_ghost_delete/16_queue.json"));
    return f ? *f : PremierFile{};
}

} // namespace

// ════════════════════════════════════════════════════════════════ shape ════════

TEST(PremierParser, ReadsALiveSnapshotWithItsBom)
{
    const QByteArray bytes = fixture("s1_full_then_one_sticker/05_snapshot.json");
    ASSERT_FALSE(bytes.isEmpty());
    ASSERT_TRUE(bytes.startsWith("\xEF\xBB\xBF"));     // the BOM is real, and must be survived

    QString err;
    const auto f = parsePremierSessionFile(bytes, &err);
    ASSERT_TRUE(f) << err.toStdString();
    EXPECT_EQ(f->sessionCount, 1);
    ASSERT_EQ(f->shots.size(), 5u);
    // Club data lands a shot late: the four older shots have it, the newest does not.
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(f->shots[i].hasClubData) << i;
    EXPECT_FALSE(f->shots[4].hasClubData);
    EXPECT_TRUE(f->shots[4].reading.dropped.empty())
        << "a shot whose club data is merely pending must not say it was not measured";
}

TEST(PremierParser, TheSameFileWithoutABomReadsTheSame)
{
    QByteArray bytes = fixture("s1_full_then_one_sticker/05_snapshot.json");
    bytes.remove(0, 3);
    const auto f = parsePremierSessionFile(bytes);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->shots.size(), 5u);
}

TEST(PremierParser, ReadsAQueueOfSeveralSessions)
{
    // The stuck 2026-10-23 session (91 shots, rejected by the server for good), session 1
    // (10 shots) and session 2 (14) — every shot, in file order.
    const PremierFile f = session2Queue();
    EXPECT_EQ(f.sessionCount, 3);
    EXPECT_EQ(f.shots.size(), 91u + 10u + 14u);
}

TEST(PremierParser, CreatedAtUtcIsReadToTheMillisecond)
{
    // "2026-11-02T15:17:52.3053…Z" — seven fractional digits, more than ISO-with-ms takes.
    const PremierFile f = session2Queue();
    const PremierShot *s = find(f, "773c37ee");
    ASSERT_TRUE(s);
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(s->createdUtcMs, QTimeZone::UTC);
    EXPECT_EQ(t.toString(Qt::ISODateWithMs), QStringLiteral("2026-11-02T15:17:52.305Z"));
    // Contact is ~0.58 s before Premier stamps the shot (§2.6.6).
    ASSERT_TRUE(s->reading.contactAtMs);
    EXPECT_EQ(*s->reading.contactAtMs, s->createdUtcMs - kPremierCreatedAfterContactMs);
}

// ═══════════════════════════════════════════════════════ a full sticker set ═══

TEST(PremierParser, FullClubDataIsConvertedIntoCatalogueUnits)
{
    // The draw from the sign block, gap wedge with a full sticker set.
    const PremierFile f = session2Queue();
    const PremierShot *s = find(f, "773c37ee");
    ASSERT_TRUE(s);
    const LaunchMonitorReading &r = s->reading;

    EXPECT_EQ(r.deviceShotId, s->localShotId);
    EXPECT_EQ(r.deviceClub, QStringLiteral("Gw"));
    EXPECT_TRUE(s->hasClubData);
    EXPECT_TRUE(r.dropped.empty());

    ASSERT_TRUE(r.ballSpeed);      EXPECT_NEAR(*r.ballSpeed, 34.69 * kMph, 5e-3);
    ASSERT_TRUE(r.clubheadSpeed);  EXPECT_NEAR(*r.clubheadSpeed, 31.024576 * kMph, 1e-3);
    ASSERT_TRUE(r.launchAngle);
    ASSERT_TRUE(r.spinRate);
    ASSERT_TRUE(r.smashFactor);    EXPECT_NEAR(*r.smashFactor, 1.12, 0.02);   // Premier's own Efficiency
    ASSERT_TRUE(r.dynamicLoft);    EXPECT_NEAR(*r.dynamicLoft, 44.1, 1e-4);
    ASSERT_TRUE(r.attackAngle);
    ASSERT_TRUE(r.spinLoft);       EXPECT_NEAR(*r.spinLoft, *r.dynamicLoft - *r.attackAngle, 1e-9);
    ASSERT_TRUE(r.lieAngle);
    ASSERT_TRUE(r.closureRate);
    ASSERT_TRUE(r.strikeHeight);   EXPECT_NEAR(*r.strikeHeight, -4.4, 1e-4);
}

// ════════════════════════════════════════════════════════════ the sign shots ═══

TEST(PremierParser, SignsFollowPinPointsConvention)
{
    const PremierFile f = session2Queue();

    // Pull: starts left, path left.
    const PremierShot *pull = find(f, "0fbce15f");
    ASSERT_TRUE(pull);
    EXPECT_NEAR(*pull->reading.launchDirection, -10.1, 1e-4);
    EXPECT_NEAR(*pull->reading.clubPath, -8.7, 1e-4);

    // Fade: curves right — side spin and spin axis right-positive, as-is.
    const PremierShot *fade = find(f, "ace56c94");
    ASSERT_TRUE(fade);
    EXPECT_NEAR(*fade->reading.sideSpin, 1887.0, 1e-9);
    EXPECT_NEAR(*fade->reading.spinAxis, 13.2, 1e-4);   // FAxisDeg, not atan2
    EXPECT_NEAR(*fade->reading.faceToPath, 0.2 - -12.1, 1e-4);

    // Draw: curves left, face closed (Premier's screen: "4.1 closed").
    const PremierShot *draw = find(f, "773c37ee");
    ASSERT_TRUE(draw);
    EXPECT_NEAR(*draw->reading.sideSpin, -1042.0, 1e-9);
    EXPECT_NEAR(*draw->reading.spinAxis, -8.2, 1e-4);
    EXPECT_NEAR(*draw->reading.faceAngle, -4.1, 1e-4);
    EXPECT_NEAR(*draw->reading.faceToPath, -4.1 - 2.8, 1e-4);

    // Toe and heel: Premier is HEEL-positive (screen "low toe" read −24.0), PinPoint toe-positive.
    const PremierShot *toe = find(f, "48ef027d");
    ASSERT_TRUE(toe);
    EXPECT_NEAR(*toe->reading.strikeLocation, +24.0, 1e-4);
    EXPECT_NEAR(*toe->reading.strikeHeight, -5.1, 1e-4);
    const PremierShot *heel = find(f, "ad509163");
    ASSERT_TRUE(heel);
    EXPECT_NEAR(*heel->reading.strikeLocation, -6.5, 1e-4);
}

// ═══════════════════════════════════════════════════ one sticker, no stickers ═══

TEST(PremierParser, OneStickerGivesSpeedAttackAndPathAndDropsTheRest)
{
    // The 5-wood with a single sticker: Premier writes the eight other fields as JSON null.
    const auto f = parsePremierSessionFile(fixture("s1_full_then_one_sticker/11_queue.json"));
    ASSERT_TRUE(f);
    const PremierShot *s = find(*f, "9597b7ba");
    ASSERT_TRUE(s);
    const LaunchMonitorReading &r = s->reading;
    EXPECT_TRUE(s->hasClubData);
    EXPECT_EQ(r.deviceClub, QStringLiteral("5w"));

    EXPECT_TRUE(r.clubheadSpeed);
    EXPECT_TRUE(r.smashFactor);
    EXPECT_TRUE(r.attackAngle);
    EXPECT_TRUE(r.clubPath);
    for (const char *k : { "lm.faceAngle", "lm.lieAngle", "lm.dynamicLoft", "lm.strikeLocation",
                           "lm.strikeHeight", "lm.closureRate" }) {
        const DroppedField *d = dropFor(r, k);
        ASSERT_TRUE(d) << k;
        EXPECT_EQ(d->code, QStringLiteral("noData")) << k;
    }
    EXPECT_EQ(r.dropped.size(), 6u);
    // Spin axis has a fallback (the spins), so a null FAxisDeg is not a drop.
    EXPECT_FALSE(dropFor(r, "lm.spinAxis"));
    ASSERT_TRUE(r.spinAxis);
    EXPECT_NEAR(*r.spinAxis, std::atan2(*r.sideSpin, *r.backSpin) * 180.0 / 3.14159265358979323846, 1e-9);
    // Derived from a missing input: absent, and covered by lm_gates' derivations, not dropped.
    EXPECT_FALSE(r.faceToPath);
    EXPECT_FALSE(dropFor(r, "lm.faceToPath"));
}

TEST(PremierParser, NoStickersIsANullClubDataAndDropsNothingUntilTold)
{
    // The 4-iron with no stickers: ClubData stays null for good. The parser cannot tell that
    // from "pending"; the monitor can, a shot later, and says so with addClubNotMeasuredDrops.
    const PremierFile f = session2Queue();
    const PremierShot *s = find(f, "570c090f");
    ASSERT_TRUE(s);
    EXPECT_FALSE(s->hasClubData);
    EXPECT_TRUE(s->reading.dropped.empty());
    EXPECT_TRUE(s->reading.ballSpeed);
    EXPECT_FALSE(s->reading.clubheadSpeed);

    LaunchMonitorReading r = s->reading;
    addClubNotMeasuredDrops(r);
    EXPECT_EQ(r.dropped.size(), 10u);   // the 11 club fields less FAxisDeg, which has a fallback
    for (const DroppedField &d : r.dropped)
        EXPECT_EQ(d.code, QStringLiteral("noData")) << d.key.toStdString();
    EXPECT_TRUE(dropFor(r, "lm.clubheadSpeed"));
    EXPECT_TRUE(dropFor(r, "lm.faceAngle"));
    EXPECT_FALSE(dropFor(r, "lm.spinAxis"));
    // Idempotent: a second call adds nothing.
    addClubNotMeasuredDrops(r);
    EXPECT_EQ(r.dropped.size(), 10u);
}

// ═══════════════════════════════════════════════════════════ what goes wrong ═══

TEST(PremierParser, ATornReadIsNotAFormatChange)
{
    const QByteArray whole = fixture("s1_full_then_one_sticker/05_snapshot.json");
    bool changed = true;
    QString err;
    EXPECT_FALSE(parsePremierSessionFile(whole.left(whole.size() / 2), &err, &changed));
    EXPECT_FALSE(changed) << "half a file is a read to retry, not a new format";
    EXPECT_FALSE(parsePremierSessionFile(QByteArray(), &err, &changed));
    EXPECT_FALSE(changed);
}

TEST(PremierParser, AWholeFileOfAnotherShapeIsAFormatChange)
{
    bool changed = false;
    EXPECT_FALSE(parsePremierSessionFile("{\"Something\": 1}", nullptr, &changed));
    EXPECT_TRUE(changed);

    changed = false;
    EXPECT_FALSE(parsePremierSessionFile("{\"Sessions\": [ { \"Groups\": [] } ]}", nullptr, &changed));
    EXPECT_TRUE(changed);

    // A shot that lost its ball speed (a renamed field) is a format change, not a quiet gap.
    changed = false;
    QByteArray bytes = fixture("s1_full_then_one_sticker/01_snapshot.json");
    bytes.replace("BallSpeedMs", "BallSpeedMps");
    EXPECT_FALSE(parsePremierSessionFile(bytes, nullptr, &changed));
    EXPECT_TRUE(changed);
}

TEST(PremierParser, AnImpossibleValueIsDroppedAsOutOfRange)
{
    QByteArray bytes = fixture("s1_full_then_one_sticker/02_snapshot.json");
    // The first shot's club data: put its smash factor out of any golf.
    const int at = bytes.indexOf("\"Efficiency\":");
    ASSERT_GT(at, 0);
    const int end = bytes.indexOf(',', at);
    bytes.replace(at, end - at, "\"Efficiency\": 99.0");
    const auto f = parsePremierSessionFile(bytes);
    ASSERT_TRUE(f);
    const LaunchMonitorReading &r = f->shots[0].reading;
    // The derivation refills smash from the two speeds, so the drop is reconciled away and
    // the value is the honest ball/club ratio rather than 99.
    ASSERT_TRUE(r.smashFactor);
    EXPECT_LT(*r.smashFactor, 2.0);
    EXPECT_FALSE(dropFor(r, "lm.smashFactor"));

    // An out-of-range field with no derivation stays dropped, with its evidence.
    QByteArray b2 = fixture("s1_full_then_one_sticker/02_snapshot.json");
    const int at2 = b2.indexOf("\"LoftDeg\":");
    const int end2 = b2.indexOf(',', at2);
    b2.replace(at2, end2 - at2, "\"LoftDeg\": 400.0");
    const auto f2 = parsePremierSessionFile(b2);
    ASSERT_TRUE(f2);
    const DroppedField *d = dropFor(f2->shots[0].reading, "lm.dynamicLoft");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->code, QStringLiteral("outOfRange"));
    ASSERT_TRUE(d->raw);
    EXPECT_DOUBLE_EQ(*d->raw, 400.0);
    EXPECT_FALSE(f2->shots[0].reading.dynamicLoft);
}

// ── Premier's club → PinPoint's (launchmonitor/premierFollowClub) ─────────────────────────────
// Every code seen in the 2 Nov capture, the driver and putter spellings Foresight uses elsewhere,
// and the clubs PinPoint has no name for, which must come back empty rather than as a guess.
TEST(PremierClub, MapsEveryCodeSeenToTheVocabulary)
{
    using pinpoint::lm::premierClubToVocabulary;
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("4h")), QStringLiteral("4 HYBRID"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("9i")), QStringLiteral("9 IRON"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("4i")), QStringLiteral("4 IRON"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("5w")), QStringLiteral("5 WOOD"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("Gw")), QStringLiteral("GAP WEDGE"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("Pw")), QStringLiteral("PITCHING WEDGE"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("Sw")), QStringLiteral("SAND WEDGE"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("Lw")), QStringLiteral("LOB WEDGE"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("1w")), QStringLiteral("DRIVER"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("Dr")), QStringLiteral("DRIVER"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral("Pt")), QStringLiteral("PUTTER"));
    EXPECT_EQ(premierClubToVocabulary(QStringLiteral(" 3W ")), QStringLiteral("3 WOOD"));
}

TEST(PremierClub, AClubPinPointDoesNotNameIsLeftAlone)
{
    using pinpoint::lm::premierClubToVocabulary;
    for (const char *code : { "7w", "2i", "2h", "60", "Irn", "Wdg", "Fw", "", "w", "10i" })
        EXPECT_TRUE(premierClubToVocabulary(QString::fromLatin1(code)).isEmpty()) << code;
}

// ═══════════════════════════════════════════ history.json: Premier's carry (#148) ═══
//
// history_20261102.json is ShotReplay\history.json as it stood at the end of the 2 Nov capture,
// cut to the 30 records 14:50–15:30 that the session fixtures overlap. Not otherwise edited.

namespace {

std::vector<PremierHistoryRecord> history()
{
    const auto h = parsePremierHistoryFile(fixture("history_20261102.json"));
    return h ? *h : std::vector<PremierHistoryRecord>{};
}

} // namespace

TEST(PremierHistory, ReadsEveryRecordWithItsCarryInYards)
{
    const auto h = history();
    ASSERT_EQ(h.size(), 30u);
    // Newest first, as Premier writes it.
    EXPECT_GT(h.front().createdUtcMs, h.back().createdUtcMs);
    for (const PremierHistoryRecord &r : h)
        EXPECT_GT(r.ballSpeedMph, 0.0);
}

TEST(PremierHistory, EachSessionShotFindsItsOwnRecord)
{
    // The 5-wood at 15:00:29.895, 55.88 m/s: Premier's carry 169.1 m.
    const auto f = parsePremierSessionFile(fixture("s1_full_then_one_sticker/11_queue.json"));
    ASSERT_TRUE(f);
    const auto h = history();
    int paired = 0;
    for (const PremierShot &s : f->shots) {
        if (s.createdUtcMs < h.back().createdUtcMs)
            continue;                                  // older than the cut: not in the fixture
        const auto c = premierCarryFor(h, s.createdUtcMs, *s.reading.ballSpeed);
        ASSERT_TRUE(c) << s.localShotId.toStdString();
        ++paired;
        if (s.localShotId.startsWith(QLatin1String("ce9eade0")))
            EXPECT_NEAR(*c, 169.1 / 0.9144, 1e-6);
    }
    EXPECT_EQ(paired, 9) << "the nine shots of the 14:53 session this queue version holds, every one paired";
}

TEST(PremierHistory, TheJoinNeedsTheTimeAndTheBallSpeed)
{
    const auto h = history();
    const qint64 t = QDateTime::fromString(QStringLiteral("2026-11-02T15:00:29.895Z"), Qt::ISODateWithMs)
                         .toMSecsSinceEpoch();
    const double mph = 55.88 * kMph;
    EXPECT_TRUE(premierCarryFor(h, t, mph));
    EXPECT_FALSE(premierCarryFor(h, t, mph + 1.0)) << "another shot's ball speed";
    EXPECT_FALSE(premierCarryFor(h, t + 1500, mph)) << "the record is before the shot";
    EXPECT_FALSE(premierCarryFor(h, t - kPremierHistoryWindowMs, mph)) << "too long after";
    EXPECT_FALSE(premierCarryFor({}, t, mph));
}

TEST(PremierHistory, AKnockPremierNeverFlewHasNoCarry)
{
    // The 2.1 mph knock at 15:11:40.284: carryDistanceMeters 0.0, which is not a carry of 0.
    const auto h = history();
    const qint64 t = QDateTime::fromString(QStringLiteral("2026-11-02T15:11:40.284Z"), Qt::ISODateWithMs)
                         .toMSecsSinceEpoch();
    bool sawRecord = false;
    for (const PremierHistoryRecord &r : h)
        if (r.createdUtcMs > t && r.createdUtcMs - t < 1000) {
            sawRecord = true;
            EXPECT_FALSE(r.carryYd);
        }
    EXPECT_TRUE(sawRecord);
    EXPECT_FALSE(premierCarryFor(h, t, 2.1));
}

TEST(PremierHistory, ATornReadIsNotAFormatChangeButAnotherShapeIs)
{
    const QByteArray whole = fixture("history_20261102.json");
    bool changed = true;
    EXPECT_FALSE(parsePremierHistoryFile(whole.left(whole.size() / 2), nullptr, &changed));
    EXPECT_FALSE(changed);
    EXPECT_FALSE(parsePremierHistoryFile("{\"version\":2,\"shots\":[]}", nullptr, &changed));
    EXPECT_TRUE(changed);
    EXPECT_FALSE(parsePremierHistoryFile("{\"records\":[{\"carryDistanceMeters\":10}]}", nullptr, &changed));
    EXPECT_TRUE(changed) << "a record with no time or ball speed";
}
