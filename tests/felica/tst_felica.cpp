#include <QtTest>

#include "cjrc_profile.h"
#include "felica_chunks.h"
#include "felica_exporter.h"
#include "code_tables.h"
#include "octopus_parser.h"
#include "station_table.h"
#include "transit_types.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const int kAttributeService = 0x008B;
const int kHistoryService = 0x090F;
const int kGateService = 0x108F;
const int kLastEntryService = 0x10CB;

quint16 encodeDate(int year, int month, int day)
{
    return static_cast<quint16>(((year - 2000) << 9) | (month << 5) | day);
}

quint16 encodeTime(int hour, int minute, int second)
{
    return static_cast<quint16>(((hour & 0x1F) << 11) | ((minute & 0x3F) << 5)
                                | ((second / 2) & 0x1F));
}

QByteArray makeRecord(int terminal, int transaction, int payment, int gateInstruction,
                      quint16 date, quint8 b6, quint8 b7, quint8 b8, quint8 b9,
                      quint16 balance, quint16 transactionNumber, quint8 regionByte)
{
    QByteArray data(16, '\0');
    data[0] = static_cast<char>(terminal);
    data[1] = static_cast<char>(transaction);
    data[2] = static_cast<char>(payment);
    data[3] = static_cast<char>(gateInstruction);
    data[4] = static_cast<char>((date >> 8) & 0xFF);
    data[5] = static_cast<char>(date & 0xFF);
    data[6] = static_cast<char>(b6);
    data[7] = static_cast<char>(b7);
    data[8] = static_cast<char>(b8);
    data[9] = static_cast<char>(b9);
    data[10] = static_cast<char>(balance & 0xFF);
    data[11] = static_cast<char>((balance >> 8) & 0xFF);
    data[12] = 0;
    data[13] = static_cast<char>((transactionNumber >> 8) & 0xFF);
    data[14] = static_cast<char>(transactionNumber & 0xFF);
    data[15] = static_cast<char>(regionByte);
    return data;
}

QByteArray makeAttribute(quint16 balance, quint16 transactionNumber, quint8 settings)
{
    QByteArray data(16, '\0');
    data[8] = static_cast<char>(settings);
    data[11] = static_cast<char>(balance & 0xFF);
    data[12] = static_cast<char>((balance >> 8) & 0xFF);
    data[14] = static_cast<char>((transactionNumber >> 8) & 0xFF);
    data[15] = static_cast<char>(transactionNumber & 0xFF);
    return data;
}

QByteArray makeGate(int direction, quint8 line, quint8 station, quint16 device,
                    quint16 date, quint8 hour, quint8 minute, quint16 amount)
{
    QByteArray data(16, '\0');
    data[0] = static_cast<char>(direction);
    data[2] = static_cast<char>(line);
    data[3] = static_cast<char>(station);
    data[4] = static_cast<char>((device >> 8) & 0xFF);
    data[5] = static_cast<char>(device & 0xFF);
    data[6] = static_cast<char>((date >> 8) & 0xFF);
    data[7] = static_cast<char>(date & 0xFF);
    data[8] = static_cast<char>(hour);
    data[9] = static_cast<char>(minute);
    data[10] = static_cast<char>(amount & 0xFF);
    data[11] = static_cast<char>((amount >> 8) & 0xFF);
    return data;
}

Transit::RawBlock rawBlock(int service, int block, const QByteArray &data)
{
    Transit::RawBlock item;
    item.service = service;
    item.block = block;
    item.data = data;
    return item;
}

} // namespace

class TestFelica : public QObject
{
    Q_OBJECT

private slots:
    void stationTableLoads();
    void dateAndTimeDecoding();
    void attributeParsing();
    void historyCountAndDeltas();
    void bigEndianTransactionNumber();
    void stationAmbiguityIsPreserved();
    void unknownPosDoesNotInventMerchant();
    void gateHistoryParsing();
    void lastEntryParsing();
    void chunkPlan15Plus5();
    void readFrameLayout();
    void truncatedAndBlankBlocksAreSafe();
    void emptyInputIsSafe();
    void currencyIsNotHardCodedInModel();
    void exportJsonAndCsv();
    void historyBeyondTwentyAndDedupe();
    void localizedDetailLabels();
    void stationNameLocalization();
    void sampleRecordRegression();
    void octopusBalanceParsing();
};

void TestFelica::stationTableLoads()
{
    StationTable &table = StationTable::instance();
    const Transit::StationInfo info = table.resolve(0xE6, 0x33, 0);
    QVERIFY(table.isLoaded());
    QVERIFY(table.entryCount() > 6000);
    QVERIFY(info.resolved);
    QVERIFY(!info.primaryName.isEmpty());
}

void TestFelica::dateAndTimeDecoding()
{
    QCOMPARE(CjrcProfile::decodeDate(0x34BF), QStringLiteral("2026-05-31"));
    QCOMPARE(CjrcProfile::decodeTime(0x76C8), QStringLiteral("14:54:16"));
    QCOMPARE(CjrcProfile::decodeDate(0), QString());
    QCOMPARE(CjrcProfile::decodeTime(0), QString());
    QCOMPARE(CjrcProfile::decodeDate(static_cast<quint16>(0x0001)), QString());
}

void TestFelica::attributeParsing()
{
    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kAttributeService, 0, makeAttribute(157, 136, 0x20)));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    QVERIFY(profile.parse(blocks, result, warnings));

    QVERIFY(result.balance.valid);
    QCOMPARE(result.balance.amount, static_cast<qint64>(157));
    QCOMPARE(result.balance.currency, QStringLiteral("JPY"));
    QCOMPARE(result.balance.sourceService, QStringLiteral("0x008B"));
}

void TestFelica::historyCountAndDeltas()
{
    QVector<Transit::RawBlock> blocks;
    const quint16 date = encodeDate(2026, 5, 31);

    // 20 synthetic rail records; balances decrease by 100 each step.
    for (int i = 0; i < 20; ++i)
    {
        const quint16 balance = static_cast<quint16>(157 + i * 100);
        blocks.append(rawBlock(kHistoryService, i,
                               makeRecord(0x16, 0x01, 0x00, 0x02, date,
                                          0xE6, 0x33, 0xE6, 0x31, balance, 100 + i, 0x00)));
    }

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    QVERIFY(profile.parse(blocks, result, warnings));

    QCOMPARE(result.transactions.size(), 20);
    QVERIFY(result.transactions.at(0).deltaKnown);
    QCOMPARE(result.transactions.at(0).delta, static_cast<qint64>(-100));
    QVERIFY(!result.transactions.last().deltaKnown);
}

void TestFelica::bigEndianTransactionNumber()
{
    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kAttributeService, 0, makeAttribute(500, 0x0102, 0x00)));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    QVERIFY(profile.parse(blocks, result, warnings));

    QVector<Transit::RawBlock> history;
    history.append(rawBlock(kHistoryService, 0,
                            makeRecord(0x16, 0x01, 0x00, 0x02, encodeDate(2026, 5, 31),
                                       0xE6, 0x33, 0xE6, 0x31, 157, 0x0102, 0x00)));
    profile.parse(history, result, warnings);
    QCOMPARE(result.transactions.at(0).transactionNumber, 0x0102);
}

void TestFelica::stationAmbiguityIsPreserved()
{
    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kHistoryService, 0,
                           makeRecord(0x16, 0x01, 0x00, 0x02, encodeDate(2026, 5, 30),
                                      0x2A, 0x0D, 0x03, 0x0D, 805, 0x82, 0x00)));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    profile.parse(blocks, result, warnings);

    QCOMPARE(result.transactions.size(), 1);
    const Transit::StationInfo &entry = result.transactions.at(0).entry;
    QVERIFY(entry.ambiguous);

    QStringList names;
    QStringList tables;
    for (const Transit::StationCandidate &candidate : entry.candidates)
    {
        names.append(candidate.station);
        if (!tables.contains(candidate.table))
            tables.append(candidate.table);
    }
    QVERIFY(names.contains(QStringLiteral("西大井")));
    QVERIFY(names.contains(QStringLiteral("武蔵小杉")));
    QVERIFY(names.contains(QStringLiteral("新川崎")));
    QVERIFY(tables.contains(QStringLiteral("suica-viewer")));
    QVERIFY(tables.contains(QStringLiteral("solidx-legacy")));
}

void TestFelica::unknownPosDoesNotInventMerchant()
{
    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kHistoryService, 0,
                           makeRecord(0xC9, 0x46, 0x00, 0x00, encodeDate(2026, 5, 30),
                                      static_cast<quint8>((encodeTime(17, 33, 0) >> 8) & 0xFF),
                                      static_cast<quint8>(encodeTime(17, 33, 0) & 0xFF),
                                      0x34, 0x12, 1058, 0x90, 0x00)));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    profile.parse(blocks, result, warnings);

    QCOMPARE(result.transactions.size(), 1);
    const Transit::TransactionRecord &record = result.transactions.at(0);
    QVERIFY(record.kind == Transit::TransactionKind::Goods);
    QVERIFY(record.terminalLabel.contains(QStringLiteral("0xC9")));
    QVERIFY(record.terminalLabel.contains(QStringLiteral("規範外")));
    QVERIFY(record.hasPosId);
    QCOMPARE(record.posIdHex, QStringLiteral("0x1234"));
    QVERIFY(record.timeKnown);
}

void TestFelica::gateHistoryParsing()
{
    QVector<Transit::RawBlock> blocks;
    const quint16 date = encodeDate(2026, 5, 31);
    blocks.append(rawBlock(kGateService, 0, makeGate(0x20, 0xE6, 0x31, 0x2015, date, 0x16, 0x21, 178)));
    blocks.append(rawBlock(kGateService, 1, makeGate(0xA0, 0xE6, 0x33, 0x1009, date, 0x16, 0x14, 0)));
    blocks.append(rawBlock(kGateService, 2, makeGate(0x20, 0xE0, 0x07, 0x3003,
                                                     encodeDate(2026, 5, 30), 0x17, 0x56, 167)));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    profile.parse(blocks, result, warnings);

    QCOMPARE(result.gates.size(), 3);
    QCOMPARE(result.gates.at(0).time, QStringLiteral("16:21"));
    QCOMPARE(result.gates.at(1).time, QStringLiteral("16:14"));
    QCOMPARE(result.gates.at(2).time, QStringLiteral("17:56"));
    QCOMPARE(result.gates.at(0).amount, static_cast<qint64>(178));
    QCOMPARE(result.gates.at(1).deviceId, QStringLiteral("1009"));
}

void TestFelica::lastEntryParsing()
{
    QByteArray data(16, '\0');
    data[0] = static_cast<char>(0xE6);
    data[1] = static_cast<char>(0x33);

    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kLastEntryService, 0, data));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    profile.parse(blocks, result, warnings);

    QVERIFY(result.hasLastEntry);
    QVERIFY(result.lastEntry.resolved);
    QVERIFY(!result.lastEntry.primaryName.isEmpty());
}

void TestFelica::chunkPlan15Plus5()
{
    const QVector<QPair<int, int> > plan = FelicaChunks::plan(20);
    QCOMPARE(plan.size(), 2);
    QCOMPARE(plan.at(0).first, 0);
    QCOMPARE(plan.at(0).second, 15);
    QCOMPARE(plan.at(1).first, 15);
    QCOMPARE(plan.at(1).second, 5);

    const QVector<QPair<int, int> > single = FelicaChunks::plan(3);
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.first().second, 3);

    QVERIFY(FelicaChunks::plan(0).isEmpty());
}

void TestFelica::readFrameLayout()
{
    const QByteArray single = FelicaChunks::readFrame(QStringLiteral("0123456789ABCDEF"),
                                                     0x008B, 0, 1);
    QCOMPARE(QString::fromLatin1(single.toHex().toUpper()),
             QStringLiteral("060123456789ABCDEF018B00018000"));

    const QByteArray history = FelicaChunks::readFrame(QStringLiteral("0123456789ABCDEF"),
                                                       0x090F, 15, 5);
    QCOMPARE(history.size(), 23);
    QCOMPARE(QString::fromLatin1(history.toHex().toUpper()),
             QStringLiteral("060123456789ABCDEF010F0905800F8010801180128013"));

    QVERIFY(FelicaChunks::readFrame(QStringLiteral("bad"), 0x008B, 0, 1).isEmpty());
}

void TestFelica::truncatedAndBlankBlocksAreSafe()
{
    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kHistoryService, 0, QByteArray(8, '\0')));
    blocks.append(rawBlock(kHistoryService, 1, QByteArray(16, '\0')));
    blocks.append(rawBlock(kGateService, 0, QByteArray(4, '\0')));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    profile.parse(blocks, result, warnings);

    QCOMPARE(result.transactions.size(), 0);
    QCOMPARE(result.gates.size(), 0);
    QCOMPARE(result.rawBlocks.size(), 3); // raw data is preserved
    QVERIFY(!result.balance.valid);
}

void TestFelica::emptyInputIsSafe()
{
    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    QVERIFY(!profile.parse(QVector<Transit::RawBlock>(), result, warnings));
    QCOMPARE(result.transactions.size(), 0);
    QVERIFY(!warnings.isEmpty());
}

void TestFelica::currencyIsNotHardCodedInModel()
{
    CjrcProfile profile;
    QCOMPARE(profile.currency(), QStringLiteral("JPY"));
    QCOMPARE(profile.services().size(), 4);

    bool historyFound = false;
    for (const Transit::ServiceDefinition &service : profile.services())
    {
        if (service.code == kHistoryService)
        {
            historyFound = true;
            QCOMPARE(service.declaredBlocks, 20);
        }
    }
    QVERIFY(historyFound);
}

QTEST_MAIN(TestFelica)
#include "tst_felica.moc"

void TestFelica::historyBeyondTwentyAndDedupe()
{
    QVector<Transit::RawBlock> blocks;
    const quint16 date = encodeDate(2026, 6, 1);
    for (int i = 0; i < 22; ++i)
    {
        blocks.append(rawBlock(kHistoryService, i,
                               makeRecord(0x16, 0x01, 0x00, 0x02, date,
                                          0xE6, 0x33, 0xE6, 0x31,
                                          static_cast<quint16>(1000 + i * 10), 200 + i, 0x00)));
    }
    // Exact duplicate of block 0 must not produce a second transaction.
    blocks.append(rawBlock(kHistoryService, 22, blocks.first().data));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    QVERIFY(profile.parse(blocks, result, warnings));

    QCOMPARE(result.transactions.size(), 22);
    QCOMPARE(result.rawBlocks.size(), 23);
    QCOMPARE(result.transactions.first().raw, blocks.first().data);
}

void TestFelica::localizedDetailLabels()
{
    QCOMPARE(FelicaCodes::equipmentLabel(0x16, Transit::LabelLanguage::Chinese),
             QStringLiteral("自动检票机"));
    QCOMPARE(FelicaCodes::equipmentLabel(0x16, Transit::LabelLanguage::English),
             QStringLiteral("Automatic ticket gate"));
    QCOMPARE(FelicaCodes::transactionLabel(0x01, Transit::LabelLanguage::Chinese),
             QStringLiteral("自动检票机出场（支付车费）"));
    QCOMPARE(FelicaCodes::transactionLabel(0x01, Transit::LabelLanguage::English),
             QStringLiteral("Automatic gate exit (fare payment)"));
    QCOMPARE(FelicaCodes::paymentLabel(0x00, Transit::LabelLanguage::Chinese),
             QStringLiteral("现金 / 无"));
    QCOMPARE(FelicaCodes::paymentLabel(0x00, Transit::LabelLanguage::English),
             QStringLiteral("Cash / None"));
    QCOMPARE(FelicaCodes::gateInstructionLabel(0x02, Transit::LabelLanguage::Chinese),
             QStringLiteral("入场 / 出场"));
    QCOMPARE(FelicaCodes::gateInstructionLabel(0x02, Transit::LabelLanguage::English),
             QStringLiteral("Entry / Exit"));

    const QString zhUnknown = FelicaCodes::equipmentLabel(0xEE, Transit::LabelLanguage::Chinese);
    const QString enUnknown = FelicaCodes::equipmentLabel(0xEE, Transit::LabelLanguage::English);
    QVERIFY(zhUnknown.startsWith(QStringLiteral("未知终端")));
    QVERIFY(enUnknown.startsWith(QStringLiteral("Unknown terminal")));
    QVERIFY(!zhUnknown.contains(QStringLiteral("不明")));
    QVERIFY(!enUnknown.contains(QStringLiteral("不明")));
}

void TestFelica::stationNameLocalization()
{
    StationNameMap &map = StationNameMap::instance();
    QCOMPARE(map.localized(QStringLiteral("鐘ケ淵"), Transit::LabelLanguage::Chinese),
             QStringLiteral("鐘ケ淵（钟渊）"));
    QCOMPARE(map.localized(QStringLiteral("鐘ケ淵"), Transit::LabelLanguage::English),
             QStringLiteral("鐘ケ淵 (Kanegafuchi)"));
    QCOMPARE(map.localized(QStringLiteral("存在しない駅"), Transit::LabelLanguage::Chinese),
             QStringLiteral("存在しない駅"));
    QCOMPARE(map.localized(QStringLiteral("存在しない駅"), Transit::LabelLanguage::English),
             QStringLiteral("存在しない駅"));
}

void TestFelica::sampleRecordRegression()
{
    const QByteArray raw = QByteArray::fromHex("1601000234C19D059D02FB0700003000");
    QVector<Transit::RawBlock> blocks;
    blocks.append(rawBlock(kHistoryService, 0, raw));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    QVERIFY(profile.parse(blocks, result, warnings));
    QCOMPARE(result.transactions.size(), 1);

    const Transit::TransactionRecord &record = result.transactions.at(0);
    QCOMPARE(record.raw, raw);
    QCOMPARE(record.terminalCode, 0x16);
    QCOMPARE(record.transactionCode, 0x01);
    QCOMPARE(record.paymentCode, 0x00);
    QCOMPARE(record.gateInstructionCode, 0x02);

    QStringList entryNames;
    for (const Transit::StationCandidate &candidate : record.entry.candidates)
        entryNames.append(candidate.station);
    QVERIFY(entryNames.contains(QStringLiteral("鐘ケ淵")));

    QStringList exitNames;
    QStringList exitTables;
    for (const Transit::StationCandidate &candidate : record.exit.candidates)
    {
        exitNames.append(candidate.station);
        if (!exitTables.contains(candidate.table))
            exitTables.append(candidate.table);
    }
    QVERIFY(exitNames.contains(QStringLiteral("とうきょうスカイツリー")));
    QVERIFY(exitNames.contains(QStringLiteral("業平橋")));
    QVERIFY(exitTables.contains(QStringLiteral("suica-viewer")));
    QVERIFY(exitTables.contains(QStringLiteral("solidx-legacy")));

    QCOMPARE(StationNameMap::instance().localized(QStringLiteral("鐘ケ淵"),
                                                  Transit::LabelLanguage::Chinese),
             QStringLiteral("鐘ケ淵（钟渊）"));
    QCOMPARE(StationNameMap::instance().localized(QStringLiteral("鐘ケ淵"),
                                                  Transit::LabelLanguage::English),
             QStringLiteral("鐘ケ淵 (Kanegafuchi)"));
}

void TestFelica::octopusBalanceParsing()
{
    const QByteArray sample = QByteArray::fromHex("000002A300000000000000000000002B");
    const OctopusBalance balance = OctopusBalanceParser::parse(sample);
    QVERIFY(balance.valid);
    QCOMPARE(balance.raw, static_cast<quint32>(675));
    QCOMPARE(balance.availableDeci, static_cast<qint64>(175));
    QCOMPARE(balance.depositDeci, static_cast<qint64>(500));
    QCOMPARE(balance.totalDeci, static_cast<qint64>(675));

    QVERIFY(!OctopusBalanceParser::parse(QByteArray::fromHex("000001F3")).valid);
    QVERIFY(!OctopusBalanceParser::parse(QByteArray::fromHex("0000")).valid);
    QVERIFY(!OctopusBalanceParser::parse(QByteArray(16, '\0')).valid);
    QVERIFY(OctopusBalanceParser::parse(QByteArray::fromHex("000001F4")).valid);
    QCOMPARE(OctopusBalanceParser::kSystemCode, static_cast<quint16>(0x8008));
    QCOMPARE(OctopusBalanceParser::kBalanceService, static_cast<quint16>(0x0117));
}

void TestFelica::exportJsonAndCsv()
{
    QVector<Transit::RawBlock> blocks;
    const quint16 date = encodeDate(2026, 5, 31);
    blocks.append(rawBlock(kAttributeService, 0, makeAttribute(157, 136, 0x20)));
    blocks.append(rawBlock(kHistoryService, 0,
                           makeRecord(0x16, 0x01, 0x00, 0x02, date,
                                      0x2A, 0x0D, 0xE6, 0x31, 157, 136, 0x00)));
    blocks.append(rawBlock(kHistoryService, 1,
                           makeRecord(0xC9, 0x46, 0x00, 0x00, date,
                                      static_cast<quint8>((encodeTime(17, 33, 0) >> 8) & 0xFF),
                                      static_cast<quint8>(encodeTime(17, 33, 0) & 0xFF),
                                      0x34, 0x12, 335, 134, 0x00)));
    blocks.append(rawBlock(kGateService, 0, makeGate(0x20, 0xE6, 0x31, 0x2015, date, 0x16, 0x21, 178)));

    CjrcProfile profile;
    Transit::ReadResult result;
    QStringList warnings;
    profile.parse(blocks, result, warnings);

    const QByteArray json = FelicaExport::toJson(result, false);
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(error.errorString()));
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("schema_version")).toString(), QStringLiteral("1.0"));
    QCOMPARE(root.value(QStringLiteral("balance")).toObject()
                 .value(QStringLiteral("amount")).toInt(), 157);
    QCOMPARE(root.value(QStringLiteral("transactions")).toArray().size(), 2);
    QCOMPARE(root.value(QStringLiteral("gate_history")).toArray().size(), 1);
    QVERIFY(!root.contains(QStringLiteral("raw_blocks")));

    const QJsonObject first = root.value(QStringLiteral("transactions")).toArray().at(0).toObject();
    QVERIFY(first.value(QStringLiteral("entry")).toObject()
                .value(QStringLiteral("ambiguous")).toBool());
    QCOMPARE(first.value(QStringLiteral("raw")).toString(),
             QStringLiteral("16 01 00 02 34 BF 2A 0D E6 31 9D 00 00 00 88 00"));

    const QJsonObject goods = root.value(QStringLiteral("transactions")).toArray().at(1).toObject();
    QCOMPARE(goods.value(QStringLiteral("pos_id")).toString(), QStringLiteral("0x1234"));
    QVERIFY(!goods.contains(QStringLiteral("merchant")));

    const QByteArray jsonRaw = FelicaExport::toJson(result, true);
    const QJsonDocument rawDocument = QJsonDocument::fromJson(jsonRaw);
    QVERIFY(rawDocument.object().contains(QStringLiteral("raw_blocks")));

    const QByteArray history = FelicaExport::historyCsv(result);
    QVERIFY(history.startsWith("\xEF\xBB\xBF"));
    const QString historyText = QString::fromUtf8(history.mid(3));
    const QStringList historyLines = historyText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(historyLines.size(), 3); // header + 2 records
    QVERIFY(historyLines.first().startsWith(QStringLiteral("index,date,time,time_source")));
    QVERIFY(!historyText.contains(QStringLiteral("merchant")));

    const QByteArray gates = FelicaExport::gateCsv(result);
    QVERIFY(gates.startsWith("\xEF\xBB\xBF"));
    const QString gateText = QString::fromUtf8(gates.mid(3));
    const QStringList gateLines = gateText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(gateLines.size(), 2);
    QVERIFY(gateLines.at(1).contains(QStringLiteral("16:21")));
}
