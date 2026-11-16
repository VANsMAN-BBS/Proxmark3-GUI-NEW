#include "cjrc_profile.h"

#include "code_tables.h"
#include "station_table.h"

#include <QMap>

#include <algorithm>

namespace {

const int kAttributeService = 0x008B;
const int kHistoryService = 0x090F;
const int kGateService = 0x108F;
const int kLastEntryService = 0x10CB;

quint16 be16(const QByteArray &data, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

quint16 le16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(data.at(offset))
                                | (static_cast<quint8>(data.at(offset + 1)) << 8));
}

Transit::RawBlock findBlock(const QVector<Transit::RawBlock> &blocks, int service, int block)
{
    for (const Transit::RawBlock &item : blocks)
    {
        if (item.service == service && item.block == block)
            return item;
    }
    return Transit::RawBlock();
}

QVector<Transit::RawBlock> serviceBlocks(const QVector<Transit::RawBlock> &blocks, int service)
{
    QVector<Transit::RawBlock> result;
    for (const Transit::RawBlock &item : blocks)
    {
        if (item.service == service)
            result.append(item);
    }
    std::sort(result.begin(), result.end(), [](const Transit::RawBlock &a, const Transit::RawBlock &b) {
        return a.block < b.block;
    });
    return result;
}

bool isBlank(const QByteArray &data)
{
    for (char c : data)
    {
        if (c != 0)
            return false;
    }
    return true;
}

Transit::TransactionRecord parseHistoryBlock(const QByteArray &raw, int index,
                                             QStringList &warnings)
{
    Transit::TransactionRecord record;
    record.index = index;
    record.raw = raw;

    if (raw.size() != 16)
    {
        record.status = Transit::ParseStatus::Truncated;
        warnings.append(QStringLiteral("history block %1 is truncated").arg(index));
        return record;
    }

    record.terminalCode = static_cast<quint8>(raw.at(0));
    record.terminalLabel = FelicaCodes::equipmentLabel(record.terminalCode);

    record.transactionCode = static_cast<quint8>(raw.at(1)) & 0x7F;
    record.transactionLabel = FelicaCodes::transactionLabel(record.transactionCode);
    record.kind = FelicaCodes::kindForTransaction(record.transactionCode);

    record.paymentCode = static_cast<quint8>(raw.at(2));
    record.paymentLabel = FelicaCodes::paymentLabel(record.paymentCode);

    record.gateInstructionCode = static_cast<quint8>(raw.at(3));
    record.gateInstructionLabel = FelicaCodes::gateInstructionLabel(record.gateInstructionCode);

    record.date = CjrcProfile::decodeDate(be16(raw, 4));
    record.balance = le16(raw, 10);
    record.transactionNumber = be16(raw, 13);
    record.regionEntry = (static_cast<quint8>(raw.at(15)) >> 6) & 0x03;
    record.regionExit = (static_cast<quint8>(raw.at(15)) >> 4) & 0x03;

    if (record.kind == Transit::TransactionKind::Goods)
    {
        const QString time = CjrcProfile::decodeTime(be16(raw, 6));
        record.time = time;
        record.timeKnown = !time.isEmpty();
        if (record.timeKnown)
            record.timeSource = QStringLiteral("record");
        record.hasPosId = true;
        record.posId = le16(raw, 8);
        record.posIdHex = QStringLiteral("0x%1")
                              .arg(record.posId, 4, 16, QLatin1Char('0'))
                              .toUpper()
                              .replace(QStringLiteral("0X"), QStringLiteral("0x"));
    }
    else if (record.kind == Transit::TransactionKind::Bus)
    {
        // Bus company/stop codes are kept in the raw record only; there is no
        // public bus-stop name table in this build.
    }
    else if (record.kind == Transit::TransactionKind::Rail)
    {
        record.entry = CjrcProfile::resolveStation(static_cast<quint8>(raw.at(6)),
                                                   static_cast<quint8>(raw.at(7)),
                                                   record.regionEntry);
        record.exit = CjrcProfile::resolveStation(static_cast<quint8>(raw.at(8)),
                                                  static_cast<quint8>(raw.at(9)),
                                                  record.regionExit);
    }

    if (record.date.isEmpty())
        record.status = Transit::ParseStatus::UnknownType;

    return record;
}

Transit::GateRecord parseGateBlock(const QByteArray &raw, int index, QStringList &warnings)
{
    Transit::GateRecord record;
    record.index = index;
    record.raw = raw;

    if (raw.size() != 16)
    {
        record.status = Transit::ParseStatus::Truncated;
        warnings.append(QStringLiteral("gate block %1 is truncated").arg(index));
        return record;
    }

    record.directionCode = static_cast<quint8>(raw.at(0));
    record.directionLabel = FelicaCodes::gateInOutLabel(record.directionCode);
    record.station = CjrcProfile::resolveStation(static_cast<quint8>(raw.at(2)),
                                                 static_cast<quint8>(raw.at(3)));
    record.deviceId = QStringLiteral("%1%2")
                          .arg(static_cast<quint8>(raw.at(4)), 2, 16, QLatin1Char('0'))
                          .arg(static_cast<quint8>(raw.at(5)), 2, 16, QLatin1Char('0'))
                          .toUpper();
    record.date = CjrcProfile::decodeDate(be16(raw, 6));

    // 0x108F stores HH:MM as two hexadecimal literals, not the packed bitfield
    // used by goods records.
    // 0x108F stores HH:MM as two hexadecimal literals (0x16 0x21 -> "16:21"),
    // not as decimal values and not as the goods-record bitfield.
    const int hourByte = static_cast<quint8>(raw.at(8));
    const int minuteByte = static_cast<quint8>(raw.at(9));
    const int hour = ((hourByte >> 4) & 0x0F) * 10 + (hourByte & 0x0F);
    const int minute = ((minuteByte >> 4) & 0x0F) * 10 + (minuteByte & 0x0F);
    if (hour < 24 && minute < 60)
    {
        record.time = QStringLiteral("%1:%2")
                          .arg(hourByte, 2, 16, QLatin1Char('0'))
                          .arg(minuteByte, 2, 16, QLatin1Char('0'))
                          .toUpper();
        record.timeKnown = true;
    }

    record.amount = le16(raw, 10);
    record.amountKnown = true;
    record.commuterFee = le16(raw, 12);
    return record;
}

} // namespace

QString CjrcProfile::profileId() const
{
    return QStringLiteral("cjrc");
}

QString CjrcProfile::displayName() const
{
    return QStringLiteral("Suica / PASMO (CJRC)");
}

QString CjrcProfile::currency() const
{
    return QStringLiteral("JPY");
}

bool CjrcProfile::matches(const Transit::CardInfo &card) const
{
    for (const QString &system : card.systems)
    {
        if (system.contains(QStringLiteral("0003"), Qt::CaseInsensitive))
            return true;
    }
    return card.icCode.startsWith(QStringLiteral("05"));
}

QVector<Transit::ServiceDefinition> CjrcProfile::services() const
{
    QVector<Transit::ServiceDefinition> result;

    Transit::ServiceDefinition attribute;
    attribute.code = kAttributeService;
    attribute.name = FelicaCodes::serviceName(kAttributeService);
    attribute.access = QStringLiteral("public");
    attribute.declaredBlocks = 1;
    result.append(attribute);

    Transit::ServiceDefinition history;
    history.code = kHistoryService;
    history.name = FelicaCodes::serviceName(kHistoryService);
    history.access = QStringLiteral("public");
    history.declaredBlocks = 20;
    result.append(history);

    Transit::ServiceDefinition gates;
    gates.code = kGateService;
    gates.name = FelicaCodes::serviceName(kGateService);
    gates.access = QStringLiteral("public");
    gates.declaredBlocks = 3;
    result.append(gates);

    Transit::ServiceDefinition lastEntry;
    lastEntry.code = kLastEntryService;
    lastEntry.name = FelicaCodes::serviceName(kLastEntryService);
    lastEntry.access = QStringLiteral("public");
    lastEntry.declaredBlocks = 2;
    result.append(lastEntry);

    return result;
}

QString CjrcProfile::decodeDate(quint16 value)
{
    if (value == 0)
        return QString();

    const int year = 2000 + ((value >> 9) & 0x7F);
    const int month = (value >> 5) & 0x0F;
    const int day = value & 0x1F;
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return QString();

    return QStringLiteral("%1-%2-%3")
        .arg(year, 4, 10, QLatin1Char('0'))
        .arg(month, 2, 10, QLatin1Char('0'))
        .arg(day, 2, 10, QLatin1Char('0'));
}

QString CjrcProfile::decodeTime(quint16 value)
{
    if (value == 0)
        return QString();

    const int hour = (value >> 11) & 0x1F;
    const int minute = (value >> 5) & 0x3F;
    const int second = (value & 0x1F) * 2;
    if (hour > 23 || minute > 59 || second > 61)
        return QString();

    return QStringLiteral("%1:%2:%3")
        .arg(hour, 2, 10, QLatin1Char('0'))
        .arg(minute, 2, 10, QLatin1Char('0'))
        .arg(second, 2, 10, QLatin1Char('0'));
}

Transit::StationInfo CjrcProfile::resolveStation(int line, int station, int area)
{
    return StationTable::instance().resolve(line, station, area);
}

bool CjrcProfile::parse(const QVector<Transit::RawBlock> &blocks,
                        Transit::ReadResult &out,
                        QStringList &warnings) const
{
    out.rawBlocks = blocks;

    const Transit::RawBlock attribute = findBlock(blocks, kAttributeService, 0);
    if (attribute.data.size() == 16)
    {
        const QByteArray &raw = attribute.data;
        const int settings = static_cast<quint8>(raw.at(8));
        out.balance.valid = true;
        out.balance.amount = le16(raw, 11);
        out.balance.currency = currency();
        out.balance.sourceService = QStringLiteral("0x008B");
        out.balance.confidence = 100;
        out.balance.raw = raw;
        Q_UNUSED(settings);
    }
    else
    {
        warnings.append(QStringLiteral("attribute service 0x008B was not read; balance is unavailable"));
    }

    const QVector<Transit::RawBlock> historyBlocks = serviceBlocks(blocks, kHistoryService);
    for (const Transit::RawBlock &block : historyBlocks)
    {
        if (block.data.size() != 16 || isBlank(block.data))
            continue;
        out.transactions.append(parseHistoryBlock(block.data, block.block, warnings));
        if (out.transactions.size() >= 20)
            break;
    }

    for (int i = 0; i + 1 < out.transactions.size(); ++i)
    {
        const qint64 current = out.transactions.at(i).balance;
        const qint64 previous = out.transactions.at(i + 1).balance;
        out.transactions[i].delta = current - previous;
        out.transactions[i].deltaKnown = true;
    }
    if (!out.transactions.isEmpty())
    {
        out.transactions.last().delta = 0;
        out.transactions.last().deltaKnown = false;
    }

    const QVector<Transit::RawBlock> gateBlocks = serviceBlocks(blocks, kGateService);
    for (const Transit::RawBlock &block : gateBlocks)
    {
        if (block.data.size() != 16 || isBlank(block.data))
            continue;
        out.gates.append(parseGateBlock(block.data, block.block, warnings));
    }

    const Transit::RawBlock lastEntry = findBlock(blocks, kLastEntryService, 0);
    if (lastEntry.data.size() == 16 && !isBlank(lastEntry.data))
    {
        out.hasLastEntry = true;
        out.lastEntry = resolveStation(static_cast<quint8>(lastEntry.data.at(0)),
                                       static_cast<quint8>(lastEntry.data.at(1)));
    }

    if (out.transactions.isEmpty())
        warnings.append(QStringLiteral("no transaction history was read"));
    if (out.gates.isEmpty())
        warnings.append(QStringLiteral("no gate history was read"));

    return out.balance.valid || !out.transactions.isEmpty();
}
