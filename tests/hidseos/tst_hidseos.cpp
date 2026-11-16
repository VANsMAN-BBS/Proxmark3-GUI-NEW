#include <QtTest>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include "seos_codes.h"
#include "seos_exporter.h"
#include "seos_parser.h"
#include "seos_reader.h"
#include "seos_types.h"

namespace {

const quint32 kDayA = 13551; // community epoch 1989-06-14 + 13551 = 2026-07-21
const quint32 kDayB = 13550;

int hexNibble(QChar c)
{
    return QString(c).toInt(nullptr, 16);
}

QByteArray bcdBytes(const QString &digits)
{
    QByteArray out;
    if ((digits.size() % 2) != 0)
        return out;
    out.reserve(digits.size() / 2);
    for (int i = 0; i < digits.size(); i += 2) {
        const int high = hexNibble(digits.at(i));
        const int low = hexNibble(digits.at(i + 1));
        out.append(static_cast<char>((high << 4) | low));
    }
    return out;
}

void setBe16(QByteArray &data, int offset, int value)
{
    data[offset] = static_cast<char>((value >> 8) & 0xFF);
    data[offset + 1] = static_cast<char>(value & 0xFF);
}

void setBe32(QByteArray &data, int offset, qint64 value)
{
    data[offset] = static_cast<char>((value >> 24) & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<char>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<char>(value & 0xFF);
}

/**
 * Synthetic PurseInfo. The CSN is deliberately fake (9999... / 8888...) and
 * the UID used by the tests is A1B2C3D4; no real card identity is stored here.
 */
QByteArray makePurse(int cardType, const QString &csn,
                     const QString &issue = QStringLiteral("20250101"),
                     const QString &expiry = QStringLiteral("20301231"),
                     int idCenter = 0x08, int userCode = 0x01)
{
    QByteArray purse(47, '\0');
    purse[0] = static_cast<char>(cardType);
    purse[1] = 0x10; // 3DES
    purse[2] = 0x01; // key version
    purse[3] = static_cast<char>(idCenter);
    purse.replace(4, 8, bcdBytes(csn));
    purse.replace(12, 5, bcdBytes(QStringLiteral("0000000001")));
    purse.replace(17, 4, bcdBytes(issue));
    purse.replace(21, 4, bcdBytes(expiry));
    purse[25] = static_cast<char>(userCode);
    purse[26] = 0x00;
    setBe32(purse, 27, 500000);
    setBe16(purse, 31, 0x4000);
    return purse;
}

QByteArray makeFci(const QByteArray &purse)
{
    QByteArray fci = QByteArray::fromHex("6F31B02F");
    fci.append(purse);
    return fci;
}

/**
 * Documented sample layout: 2026-07-13 .. 2026-07-15.
 *
 * Field offsets follow the raw APDU log (docs/raw/climate_pass.txt). The prose
 * table in docs/02 shows one extra zero byte before the product field, which
 * would shift product/unknown by one byte; the machine-readable JSON, the raw
 * log and parse_ksx6924.py all agree on the layout used here
 * (product 0x0306, unknown 0E0A4300).
 */
QByteArray makePass()
{
    return QByteArray::fromHex(
        "2607132607150000000000C803060E0A4300000000000000000001");
}

quint32 packedTimestamp(int day, int hour, int minute, int second)
{
    return (static_cast<quint32>(day) << 16)
           | (static_cast<quint32>(hour & 0x1F) << 11)
           | (static_cast<quint32>(minute & 0x3F) << 5)
           | (static_cast<quint32>((second / 2) & 0x1F));
}

QByteArray makeTrip(int event, int transport, int counter, int day,
                    int hour, int minute, int second, int distance,
                    int baseFare, int totalFare)
{
    QByteArray record(52, '\0');
    record[0] = 0x01;
    record[1] = 0x32;
    record[2] = static_cast<char>(event);
    record[3] = static_cast<char>(transport);
    record[4] = static_cast<char>(counter);
    record[6] = 0x00; // synthetic device id
    record[7] = 0x11;
    record[8] = 0x22;

    const quint32 packed = packedTimestamp(day, hour, minute, second);
    record[11] = static_cast<char>((packed >> 24) & 0xFF);
    record[12] = static_cast<char>((packed >> 16) & 0xFF);
    record[13] = static_cast<char>((packed >> 8) & 0xFF);
    record[14] = static_cast<char>(packed & 0xFF);

    setBe16(record, 19, distance);
    setBe16(record, 22, totalFare);
    record[24] = static_cast<char>(0xA0); // synthetic station id
    record[25] = static_cast<char>(0xB0 + counter);
    record[26] = static_cast<char>(0xC0);
    record[27] = static_cast<char>(0xD0);
    setBe16(record, 40, baseFare);
    setBe16(record, 46, totalFare);
    return record;
}

QByteArray makeFinancial(int sfi, int type, int balance, int sequence,
                         int fare, int marker)
{
    QByteArray record(sfi == 11 ? 27 : 46, '\0');
    record[0] = static_cast<char>(type);
    setBe32(record, 2, balance);
    setBe16(record, 8, sequence);
    setBe16(record, 12, fare);
    setBe16(record, 14, marker);
    for (int i = 0; i < 8; ++i)
        record[18 + i] = static_cast<char>(0x10 + i); // synthetic terminal
    return record;
}

HidSeos::Parser::ApduResponse makeResponse(const QByteArray &data, quint16 sw)
{
    HidSeos::Parser::ApduResponse response;
    response.responded = true;
    response.data = data;
    response.sw = sw;
    response.raw = data;
    response.raw.append(static_cast<char>((sw >> 8) & 0xFF));
    response.raw.append(static_cast<char>(sw & 0xFF));
    return response;
}

/** 20 synthetic taps arranged as the 7 journeys of the documented layout. */
QVector<QByteArray> makeClimateTrips()
{
    QVector<QByteArray> trips;
    // counter 14: bus + metro
    trips << makeTrip(0x01, 0x01, 14, kDayA, 20, 15, 30, 2020, 1500, 3000);
    trips << makeTrip(0x00, 0x01, 14, kDayA, 20, 10, 10, 747, 1500, 3000);
    trips << makeTrip(0x01, 0x00, 14, kDayA, 20, 3, 20, 747, 1500, 1500);
    trips << makeTrip(0x00, 0x00, 14, kDayA, 20, 0, 5, 0, 1500, 1500);
    // counter 13: bus + metro
    trips << makeTrip(0x01, 0x01, 13, kDayA, 18, 14, 22, 1828, 1500, 3000);
    trips << makeTrip(0x00, 0x01, 13, kDayA, 18, 10, 12, 1134, 1500, 3000);
    trips << makeTrip(0x01, 0x00, 13, kDayA, 18, 6, 40, 1134, 1500, 1500);
    trips << makeTrip(0x00, 0x00, 13, kDayA, 18, 0, 0, 0, 1500, 1500);
    // counter 12: bus only, distance surcharge
    trips << makeTrip(0x01, 0x00, 12, kDayA, 16, 40, 18, 12200, 1550, 1650);
    trips << makeTrip(0x00, 0x00, 12, kDayA, 16, 5, 40, 0, 1550, 1550);
    // counter 11: bus only
    trips << makeTrip(0x01, 0x00, 11, kDayA, 7, 28, 42, 6600, 1550, 1550);
    trips << makeTrip(0x00, 0x00, 11, kDayA, 7, 10, 12, 0, 1550, 1550);
    // counter 10: bus + metro, previous day
    trips << makeTrip(0x01, 0x01, 10, kDayB, 13, 58, 18, 9100, 1550, 3050);
    trips << makeTrip(0x00, 0x01, 10, kDayB, 13, 45, 12, 4800, 1550, 3050);
    trips << makeTrip(0x01, 0x00, 10, kDayB, 13, 33, 58, 4800, 1500, 1500);
    trips << makeTrip(0x00, 0x00, 10, kDayB, 13, 20, 36, 0, 1500, 1500);
    // counter 9: bus only
    trips << makeTrip(0x01, 0x00, 9, kDayB, 12, 5, 14, 8051, 1500, 1500);
    trips << makeTrip(0x00, 0x00, 9, kDayB, 11, 40, 4, 0, 1500, 1500);
    // counter 8: bus only
    trips << makeTrip(0x01, 0x00, 8, kDayB, 9, 15, 58, 858, 1500, 1500);
    trips << makeTrip(0x00, 0x00, 8, kDayB, 9, 2, 30, 0, 1500, 1500);
    return trips;
}

HidSeos::SeosReadSnapshot makeClimateSnapshot(int cardType = 0xB5,
                                     const QString &csn = QStringLiteral("8888000022223333"))
{
    HidSeos::SeosReadSnapshot snapshot;
    snapshot.uid = QStringLiteral("A1B2C3D4");
    snapshot.atqa = QStringLiteral("0004");
    snapshot.sak = QStringLiteral("20");
    snapshot.ats = QStringLiteral("097877C1020E513101");
    snapshot.detectedAt = QStringLiteral("2026-01-01T00:00:00");
    snapshot.fci = makeFci(makePurse(cardType, csn,
                                     QStringLiteral("20260506"),
                                     QStringLiteral("20310505")));
    snapshot.balance = makeResponse(QByteArray(), 0x6D00);
    snapshot.pass = makeResponse(makePass(), 0x9000);
    snapshot.trips = makeClimateTrips();
    snapshot.readEvents << makeFinancial(11, 1, 0, 0x2E, 0, 0x0720);
    snapshot.readEvents << makeFinancial(11, 1, 0, 0x2D, 0, 0x0720);
    snapshot.sfi13 = QByteArray::fromHex("10000000000000000000000000000000");
    return snapshot;
}

HidSeos::SeosReadSnapshot makeEmptyTMoneySnapshot()
{
    HidSeos::SeosReadSnapshot snapshot;
    snapshot.uid = QStringLiteral("A1B2C3D4");
    snapshot.atqa = QStringLiteral("0004");
    snapshot.sak = QStringLiteral("20");
    snapshot.ats = QStringLiteral("097877C1020E513101");
    snapshot.detectedAt = QStringLiteral("2026-01-01T00:00:00");
    snapshot.fci = makeFci(makePurse(0x00, QStringLiteral("9999000011112222"),
                                     QStringLiteral("20250101"),
                                     QStringLiteral("20301231")));
    snapshot.balance = makeResponse(QByteArray::fromHex("00000000"), 0x9000);
    snapshot.pass = makeResponse(QByteArray(), 0x6A82);
    snapshot.financial << makeFinancial(4, 1, 0, 2, 0, 0x0720);
    snapshot.financial << makeFinancial(4, 1, 0, 1, 0, 0x0720);
    return snapshot;
}

} // namespace

class TestHidSeos : public QObject
{
    Q_OBJECT

private slots:
    void apduOutputParsing();
    void tmoneyEmptyCard();
    void climateIdentityAndPass();
    void climateBalanceIsUnsupported();
    void climateAggregatesAndCalibration();
    void unknownCardTypeIsPreserved();
    void shortAndTruncatedDataIsSafe();
    void duplicateTripsAreIgnored();
    void unknownSfiAndStatusWords();
    void exportJsonSchema();
    void exportCsvBomAndGroupedRows();
    void rawRecordsPreserveBytes();
};

void TestHidSeos::apduOutputParsing()
{
    const QByteArray fci = makeFci(makePurse(0x00, QStringLiteral("9999000011112222")));
    const QByteArray full = fci + QByteArray::fromHex("9000");
    const QString output =
        QStringLiteral("[+] select card, keep field on\n"
                       "[+] >>> 00A4040007D4100000030001\n"
                       "[+] <<< %1 | ascii\n"
                       "[+] <<< status: 90 00 - Command successfully executed (OK).\n")
            .arg(QString::fromLatin1(full.toHex().toUpper()));

    const HidSeos::Parser::ApduResponse ok = HidSeos::Parser::parseApduOutput(output);
    QVERIFY(ok.responded);
    QCOMPARE(static_cast<int>(ok.sw), 0x9000);
    QCOMPARE(ok.data, fci);
    QCOMPARE(ok.raw, full);

    const HidSeos::Parser::ApduResponse unsupported =
        HidSeos::Parser::parseApduOutput(QStringLiteral("[+] <<< 6D00 | m.\n"));
    QVERIFY(unsupported.responded);
    QCOMPARE(static_cast<int>(unsupported.sw), 0x6D00);
    QVERIFY(unsupported.data.isEmpty());

    const HidSeos::Parser::ApduResponse missing =
        HidSeos::Parser::parseApduOutput(QStringLiteral("[!] no response\n"));
    QVERIFY(!missing.responded);
    QVERIFY(!missing.error.isEmpty());

    QString uid;
    QString atqa;
    QString sak;
    QString ats;
    QVERIFY(HidSeos::Parser::parseCardInfo(
        QStringLiteral("[+]  UID: a1 b2 c3 d4\n"
                       "[+] ATQA: 00 04\n"
                       "[+]  SAK: 20 [1]\n"
                       "[+] ATS: 09 78 77 C1 02 0E 51 31 01\n"),
        &uid, &atqa, &sak, &ats));
    QCOMPARE(uid, QStringLiteral("A1B2C3D4"));
    QCOMPARE(atqa, QStringLiteral("0004"));
    QCOMPARE(sak, QStringLiteral("20"));
    QCOMPARE(ats.size(), 18);
}

void TestHidSeos::tmoneyEmptyCard()
{
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeEmptyTMoneySnapshot());

    QVERIFY(result.selectOk);
    QVERIFY(result.iso14443a);
    QCOMPARE(static_cast<int>(result.kind), static_cast<int>(HidSeos::CardKind::TMoney));
    QCOMPARE(result.purse.cardTypeCode, 0);
    QCOMPARE(result.purse.csn, QStringLiteral("9999000011112222"));
    QCOMPARE(result.purse.issueDate, QStringLiteral("2025-01-01"));
    QCOMPARE(result.purse.expiryDate, QStringLiteral("2030-12-31"));

    QVERIFY(result.balance.supported);
    QVERIFY(result.balance.known);
    QCOMPARE(result.balance.amountKrw, static_cast<qint64>(0));
    QVERIFY(result.trips.isEmpty());
    QVERIFY(result.groupedTrips.isEmpty());

    QCOMPARE(result.financial.size(), 2);
    QCOMPARE(result.purchaseCount(), 0);
    for (const HidSeos::FinancialRecord &record : result.financial) {
        QVERIFY(record.valid);
        QCOMPARE(record.fareKrw, static_cast<qint64>(0));
        QCOMPARE(record.marker, 0x0720);
    }
    QVERIFY(!result.pass.present);
}

void TestHidSeos::climateIdentityAndPass()
{
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeClimateSnapshot());

    QCOMPARE(static_cast<int>(result.kind), static_cast<int>(HidSeos::CardKind::Climate));
    QCOMPARE(result.purse.cardTypeCode, 0xB5);
    QCOMPARE(HidSeos::Codes::cardTypeLabel(0xB5),
             QStringLiteral("Climate Card / period-pass (proprietary)"));
    QCOMPARE(result.purse.csn, QStringLiteral("8888000022223333"));
    QCOMPARE(result.purse.issueDate, QStringLiteral("2026-05-06"));
    QCOMPARE(result.purse.expiryDate, QStringLiteral("2031-05-05"));
    QCOMPARE(result.purse.userCode, 0x01);
    QCOMPARE(result.purse.maxBalanceKrw, static_cast<qint64>(500000));

    QVERIFY(result.pass.present);
    QCOMPARE(result.pass.validFrom, QStringLiteral("2026-07-13"));
    QCOMPARE(result.pass.validTo, QStringLiteral("2026-07-15"));
    QCOMPARE(result.pass.days, 3);
    QCOMPARE(result.pass.productCode, 0x0306);
    QCOMPARE(result.pass.status, 0x01);
    QCOMPARE(result.pass.raw.size(), 27);
    QVERIFY(result.rawRecords.size() >= 2);
}

void TestHidSeos::climateBalanceIsUnsupported()
{
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeClimateSnapshot());

    QVERIFY(!result.balance.supported);
    QVERIFY(!result.balance.known);
    QCOMPARE(result.balance.amountKrw, static_cast<qint64>(0)); // not displayed as 0
    QCOMPARE(static_cast<int>(result.balance.sw), 0x6D00);
    QVERIFY(result.balance.reason.contains(QStringLiteral("6D00")));
    QVERIFY(result.balance.reason.contains(QStringLiteral("Climate")));

    const QJsonObject root =
        QJsonDocument::fromJson(HidSeos::Export::toJson(result)).object();
    QVERIFY(root.value(QStringLiteral("balance")).toObject()
                .value(QStringLiteral("amount_krw")).isNull());

    // A B5 card without a valid pass must not be silently treated as Climate.
    HidSeos::SeosReadSnapshot noPass = makeClimateSnapshot();
    noPass.pass = makeResponse(QByteArray(), 0x6A82);
    const HidSeos::ReadResult unknown = HidSeos::SeosReader::buildResult(noPass);
    QCOMPARE(static_cast<int>(unknown.kind), static_cast<int>(HidSeos::CardKind::Unknown));
    QVERIFY(unknown.pass.present == false);
}

void TestHidSeos::climateAggregatesAndCalibration()
{
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeClimateSnapshot());

    QCOMPARE(result.trips.size(), 20);
    QCOMPARE(result.groupedTrips.size(), 7);
    QCOMPARE(result.recordedFareTotalKrw(), static_cast<qint64>(15250));

    QCOMPARE(result.calibration.offsetDays, -6);
    QCOMPARE(result.calibration.observedEpoch,
             QDate(1989, 6, 8));
    QCOMPARE(result.calibration.communityEpoch,
             QDate(1989, 6, 14));
    QVERIFY(result.calibration.verified);
    QVERIFY(result.calibration.source.contains(QStringLiteral("SFI 7")));

    // Raw timestamps stay visible; only the presentation date is calibrated.
    QCOMPARE(result.trips.first().dayCounter, kDayA);
    QVERIFY(result.trips.first().communityDatetime.startsWith(QStringLiteral("2026-07-21")));
    QCOMPARE(result.trips.first().date, QStringLiteral("2026-07-15"));
    QCOMPARE(result.trips.first().time, QStringLiteral("20:15"));

    const HidSeos::GroupedTrip &newest = result.groupedTrips.first();
    QCOMPARE(newest.counter, 14);
    QCOMPARE(newest.journeyFareKrw, static_cast<qint64>(3000));
    QCOMPARE(newest.tapCount, 4);
    QCOMPARE(newest.date, QStringLiteral("2026-07-15"));
    QCOMPARE(newest.startTime, QStringLiteral("20:00"));
    QCOMPARE(newest.endTime, QStringLiteral("20:15"));
    QCOMPARE(newest.legs.size(), 2);

    QHash<int, int> tapsPerCounter;
    for (const HidSeos::GroupedTrip &trip : result.groupedTrips)
        tapsPerCounter.insert(trip.counter, trip.tapCount);
    QCOMPARE(tapsPerCounter.value(14), 4);
    QCOMPARE(tapsPerCounter.value(13), 4);
    QCOMPARE(tapsPerCounter.value(12), 2);
    QCOMPARE(tapsPerCounter.value(11), 2);
    QCOMPARE(tapsPerCounter.value(10), 4);
    QCOMPARE(tapsPerCounter.value(9), 2);
    QCOMPARE(tapsPerCounter.value(8), 2);

    QCOMPARE(result.readEvents.size(), 2);
    QCOMPARE(result.sfi13.size(), 16);
}

void TestHidSeos::unknownCardTypeIsPreserved()
{
    HidSeos::SeosReadSnapshot snapshot = makeEmptyTMoneySnapshot();
    snapshot.fci = makeFci(makePurse(0x7E, QStringLiteral("9999000011112222")));
    snapshot.balance = makeResponse(QByteArray::fromHex("000001F4"), 0x9000);

    const HidSeos::ReadResult result = HidSeos::SeosReader::buildResult(snapshot);
    QCOMPARE(static_cast<int>(result.kind), static_cast<int>(HidSeos::CardKind::Unknown));
    QCOMPARE(result.purse.cardTypeCode, 0x7E);
    QVERIFY(HidSeos::Codes::cardTypeLabel(0x7E).isEmpty());
    bool foundWarning = false;
    for (const QString &warning : result.warnings) {
        if (warning.contains(QStringLiteral("0x7E")))
            foundWarning = true;
    }
    QVERIFY(foundWarning);
    QVERIFY(result.balance.known); // an unknown type may still answer 90 4C
    QCOMPARE(result.balance.amountKrw, static_cast<qint64>(500));

    const QJsonObject root =
        QJsonDocument::fromJson(HidSeos::Export::toJson(result)).object();
    const QJsonObject card = root.value(QStringLiteral("card_info")).toObject();
    QCOMPARE(card.value(QStringLiteral("card_type_code")).toInt(), 0x7E);
    QVERIFY(card.value(QStringLiteral("card_type")).isNull());
}

void TestHidSeos::shortAndTruncatedDataIsSafe()
{
    HidSeos::PurseInfo purse;
    QVERIFY(!HidSeos::Parser::parsePurseInfo(QByteArray(10, '\0'), &purse));
    QVERIFY(!purse.valid);

    HidSeos::PassInfo pass;
    QVERIFY(!HidSeos::Parser::parsePassRecord(QByteArray(12, '\0'), &pass));
    QVERIFY(!pass.present);

    const QByteArray shortTrip(20, '\0');
    HidSeos::TripRecord trip;
    QVERIFY(!HidSeos::Parser::parseTripRecord(shortTrip, 1, &trip));
    QVERIFY(!trip.valid);
    QCOMPARE(trip.raw, shortTrip);
    QVERIFY(!trip.error.isEmpty());

    HidSeos::FinancialRecord financial;
    QVERIFY(!HidSeos::Parser::parseFinancialRecord(QByteArray(10, '\0'), 4, 1, &financial));
    QVERIFY(!financial.valid);

    HidSeos::SeosReadSnapshot snapshot = makeEmptyTMoneySnapshot();
    snapshot.trips << shortTrip;
    const HidSeos::ReadResult result = HidSeos::SeosReader::buildResult(snapshot);
    QVERIFY(result.trips.isEmpty());
    QVERIFY(result.groupedTrips.isEmpty());
    QVERIFY(!result.rawRecords.isEmpty());
    bool shortRecordPreserved = false;
    for (const HidSeos::RawRecord &record : result.rawRecords) {
        if (record.sfi == 3 && record.data == shortTrip) {
            shortRecordPreserved = true;
            QVERIFY(!record.parsed);
        }
    }
    QVERIFY(shortRecordPreserved);

    QVector<QByteArray> mixed;
    mixed << shortTrip << makeTrip(0x00, 0x00, 1, kDayB, 9, 0, 0, 0, 1500, 1500);
    QStringList warnings;
    QCOMPARE(HidSeos::Parser::parseTripSeries(mixed, &warnings).size(), 1);
    QVERIFY(!warnings.isEmpty());
}

void TestHidSeos::duplicateTripsAreIgnored()
{
    QVector<QByteArray> records = makeClimateTrips();
    records.append(records.first());

    QStringList warnings;
    const QVector<HidSeos::TripRecord> trips =
        HidSeos::Parser::parseTripSeries(records, &warnings);
    QCOMPARE(trips.size(), 20);
    bool duplicateWarning = false;
    for (const QString &warning : warnings) {
        if (warning.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive))
            duplicateWarning = true;
    }
    QVERIFY(duplicateWarning);

    HidSeos::SeosReadSnapshot snapshot = makeClimateSnapshot();
    snapshot.trips.append(snapshot.trips.first());
    const HidSeos::ReadResult result = HidSeos::SeosReader::buildResult(snapshot);
    QCOMPARE(result.trips.size(), 20);
    QCOMPARE(result.groupedTrips.size(), 7);
    QCOMPARE(result.recordedFareTotalKrw(), static_cast<qint64>(15250));
}

void TestHidSeos::unknownSfiAndStatusWords()
{
    QVERIFY(HidSeos::Codes::sfiName(3).contains(QStringLiteral("trip")));
    QVERIFY(HidSeos::Codes::sfiName(7).contains(QStringLiteral("pass")));
    QVERIFY(HidSeos::Codes::sfiName(99).isEmpty());

    QVERIFY(HidSeos::Codes::statusWordText(0x9000).contains(QStringLiteral("OK")));
    QVERIFY(HidSeos::Codes::statusWordText(0x6A83).contains(QStringLiteral("record")));
    const QString wrongLe = HidSeos::Codes::statusWordText(0x6C1B);
    QVERIFY(wrongLe.contains(QStringLiteral("27")));
    QVERIFY(HidSeos::Codes::statusWordText(0x1234).isEmpty());

    // An unexpected SFI is parsed without inventing a file meaning.
    HidSeos::FinancialRecord unknownSfi;
    QVERIFY(HidSeos::Parser::parseFinancialRecord(QByteArray(46, '\0'), 99, 1, &unknownSfi));
    QCOMPARE(unknownSfi.sfi, 99);
    QVERIFY(HidSeos::Codes::sfiName(unknownSfi.sfi).isEmpty());
}

void TestHidSeos::exportJsonSchema()
{
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeClimateSnapshot());
    const QByteArray json = HidSeos::Export::toJson(result);
    const QJsonObject root = QJsonDocument::fromJson(json).object();

    const QStringList required = QStringList()
        << QStringLiteral("card_info") << QStringLiteral("balance")
        << QStringLiteral("pass") << QStringLiteral("time_calibration")
        << QStringLiteral("raw_transactions") << QStringLiteral("grouped_trips")
        << QStringLiteral("read_status") << QStringLiteral("warnings");
    for (const QString &key : required)
        QVERIFY2(root.contains(key), qPrintable(key));

    QCOMPARE(root.value(QStringLiteral("card_info")).toObject()
                 .value(QStringLiteral("csn")).toString(),
             QStringLiteral("8888000022223333"));
    QCOMPARE(root.value(QStringLiteral("time_calibration")).toObject()
                 .value(QStringLiteral("offset_days")).toInt(), -6);
    QCOMPARE(root.value(QStringLiteral("pass")).toObject()
                 .value(QStringLiteral("days")).toInt(), 3);
    QCOMPARE(root.value(QStringLiteral("raw_transactions")).toArray().size(), 22);
    QCOMPARE(root.value(QStringLiteral("grouped_trips")).toArray().size(), 7);
    QCOMPARE(root.value(QStringLiteral("records")).toObject()
                 .value(QStringLiteral("recorded_fare_total_krw")).toInt(), 15250);
    QCOMPARE(root.value(QStringLiteral("read_status")).toObject()
                 .value(QStringLiteral("cancelled")).toBool(), false);

    // The empty T-money card exports a real zero, not a missing balance.
    const HidSeos::ReadResult empty =
        HidSeos::SeosReader::buildResult(makeEmptyTMoneySnapshot());
    const QJsonObject emptyRoot =
        QJsonDocument::fromJson(HidSeos::Export::toJson(empty)).object();
    const QJsonValue emptyAmount = emptyRoot.value(QStringLiteral("balance")).toObject()
                                       .value(QStringLiteral("amount_krw"));
    QVERIFY(!emptyAmount.isNull());
    QCOMPARE(emptyAmount.toInt(), 0);
    QCOMPARE(emptyRoot.value(QStringLiteral("raw_transactions")).toArray().size(), 2);
}

void TestHidSeos::exportCsvBomAndGroupedRows()
{
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeClimateSnapshot());

    const QByteArray transactions = HidSeos::Export::transactionsCsv(result);
    QVERIFY(transactions.startsWith(QByteArray("\xEF\xBB\xBF")));
    QVERIFY(transactions.contains("kind,sfi,index"));
    QVERIFY(transactions.contains("trip"));
    QVERIFY(transactions.contains("read_event"));

    const QByteArray trips = HidSeos::Export::tripsCsv(result);
    QVERIFY(trips.startsWith(QByteArray("\xEF\xBB\xBF")));
    QVERIFY(trips.contains("journey_fare_krw"));
    QVERIFY(trips.contains("recorded nominal fare"));
    QCOMPARE(trips.count('\n'), 8); // header + 7 journeys
}

void TestHidSeos::rawRecordsPreserveBytes()
{
    const QVector<QByteArray> rawTrips = makeClimateTrips();
    const HidSeos::ReadResult result =
        HidSeos::SeosReader::buildResult(makeClimateSnapshot());

    int tripRecords = 0;
    for (const HidSeos::RawRecord &record : result.rawRecords) {
        if (record.sfi != 3)
            continue;
        QVERIFY(record.index >= 1 && record.index <= rawTrips.size());
        QCOMPARE(record.data, rawTrips.at(record.index - 1));
        ++tripRecords;
    }
    QCOMPARE(tripRecords, 20);
    QCOMPARE(result.trips.first().raw, rawTrips.first());
    QCOMPARE(result.trips.first().rawTimestamp, QStringLiteral("34EFA1EF"));
}

QTEST_MAIN(TestHidSeos)
#include "tst_hidseos.moc"

