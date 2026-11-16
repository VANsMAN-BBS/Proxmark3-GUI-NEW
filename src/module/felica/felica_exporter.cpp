#include "felica_exporter.h"

#include "code_tables.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace {

QString hexBytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ').toUpper());
}

QString kindName(Transit::TransactionKind kind)
{
    switch (kind)
    {
    case Transit::TransactionKind::Rail: return QStringLiteral("rail");
    case Transit::TransactionKind::Goods: return QStringLiteral("goods");
    case Transit::TransactionKind::Bus: return QStringLiteral("bus");
    default: break;
    }
    return QStringLiteral("unknown");
}

QJsonObject stationJson(const Transit::StationInfo &station)
{
    QJsonObject object;
    object.insert(QStringLiteral("raw_code"), station.rawCode);
    object.insert(QStringLiteral("resolved"), station.resolved);
    object.insert(QStringLiteral("ambiguous"), station.ambiguous);
    object.insert(QStringLiteral("primary"), station.primaryName);
    QJsonArray candidates;
    for (const Transit::StationCandidate &candidate : station.candidates)
    {
        QJsonObject item;
        item.insert(QStringLiteral("table"), candidate.table);
        item.insert(QStringLiteral("area"), candidate.area);
        item.insert(QStringLiteral("company"), candidate.company);
        item.insert(QStringLiteral("line"), candidate.line);
        item.insert(QStringLiteral("station"), candidate.station);
        item.insert(QStringLiteral("note"), candidate.note);
        candidates.append(item);
    }
    object.insert(QStringLiteral("candidates"), candidates);
    return object;
}

QString stationText(const Transit::StationInfo &station)
{
    if (!station.resolved)
        return QString();
    QString text = station.primaryName;
    if (station.ambiguous)
    {
        QStringList others;
        for (const Transit::StationCandidate &candidate : station.candidates)
        {
            if (!candidate.station.isEmpty() && candidate.station != station.primaryName
                && !others.contains(candidate.station))
            {
                others.append(candidate.station);
            }
        }
        if (!others.isEmpty())
            text += QStringLiteral(" (+%1)").arg(others.size());
    }
    return text;
}

QString candidateText(const Transit::StationInfo &station)
{
    QStringList lines;
    for (const Transit::StationCandidate &candidate : station.candidates)
    {
        lines.append(QStringLiteral("%1 / %2 / %3 [%4]")
                         .arg(candidate.company, candidate.line, candidate.station,
                              candidate.table));
    }
    return lines.join(QLatin1String(" | "));
}

QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r')))
    {
        escaped = QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return escaped;
}

QString csvLine(const QStringList &fields)
{
    QStringList escaped;
    for (const QString &field : fields)
        escaped.append(csvField(field));
    return escaped.join(QLatin1Char(',')) + QLatin1Char('\n');
}

} // namespace

namespace FelicaExport {

QByteArray toJson(const Transit::ReadResult &result, bool includeRawBlocks)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    root.insert(QStringLiteral("generated_at"),
                QDateTime::currentDateTime().toString(Qt::ISODate));

    QJsonObject source;
    source.insert(QStringLiteral("application"), QStringLiteral("Proxmark3GUI"));
    source.insert(QStringLiteral("profile"), result.card.profileId);
    source.insert(QStringLiteral("read_only"), true);
    root.insert(QStringLiteral("source"), source);

    QJsonObject card;
    card.insert(QStringLiteral("protocol"), result.card.protocol);
    card.insert(QStringLiteral("card_type"), result.card.cardType);
    card.insert(QStringLiteral("card_id"), result.card.cardId);
    card.insert(QStringLiteral("pmm"), result.card.pmm);
    card.insert(QStringLiteral("ic_code"), result.card.icCode);
    card.insert(QStringLiteral("ic_name"), result.card.icName);
    card.insert(QStringLiteral("manufacture_date"), result.card.manufactureDate);
    card.insert(QStringLiteral("issuer"), result.card.issuer);
    card.insert(QStringLiteral("profile_id"), result.card.profileId);
    card.insert(QStringLiteral("detected_at"), result.card.detectedAt);
    QJsonArray systems;
    for (const QString &system : result.card.systems)
        systems.append(system);
    card.insert(QStringLiteral("systems"), systems);
    root.insert(QStringLiteral("card"), card);

    QJsonObject balance;
    balance.insert(QStringLiteral("valid"), result.balance.valid);
    balance.insert(QStringLiteral("amount"), static_cast<double>(result.balance.amount));
    balance.insert(QStringLiteral("currency"), result.balance.currency);
    balance.insert(QStringLiteral("source_service"), result.balance.sourceService);
    balance.insert(QStringLiteral("confidence"), result.balance.confidence);
    root.insert(QStringLiteral("balance"), balance);

    QJsonArray transactions;
    for (const Transit::TransactionRecord &record : result.transactions)
    {
        QJsonObject item;
        item.insert(QStringLiteral("index"), record.index);
        item.insert(QStringLiteral("date"), record.date);
        item.insert(QStringLiteral("time"), record.timeKnown ? QJsonValue(record.time) : QJsonValue());
        item.insert(QStringLiteral("time_source"), record.timeSource);
        item.insert(QStringLiteral("kind"), kindName(record.kind));
        item.insert(QStringLiteral("terminal_code"), record.terminalCode);
        item.insert(QStringLiteral("terminal"), record.terminalLabel);
        item.insert(QStringLiteral("transaction_code"), record.transactionCode);
        item.insert(QStringLiteral("transaction"), record.transactionLabel);
        item.insert(QStringLiteral("payment_code"), record.paymentCode);
        item.insert(QStringLiteral("payment"), record.paymentLabel);
        item.insert(QStringLiteral("gate_instruction_code"), record.gateInstructionCode);
        item.insert(QStringLiteral("gate_instruction"), record.gateInstructionLabel);
        item.insert(QStringLiteral("entry"), stationJson(record.entry));
        item.insert(QStringLiteral("exit"), stationJson(record.exit));
        if (record.hasPosId)
            item.insert(QStringLiteral("pos_id"), record.posIdHex);
        item.insert(QStringLiteral("balance"), static_cast<double>(record.balance));
        item.insert(QStringLiteral("transaction_number"), record.transactionNumber);
        item.insert(QStringLiteral("delta"), record.deltaKnown
                    ? QJsonValue(static_cast<double>(record.delta)) : QJsonValue());
        item.insert(QStringLiteral("raw"), hexBytes(record.raw));
        transactions.append(item);
    }
    root.insert(QStringLiteral("transactions"), transactions);

    QJsonArray gates;
    for (const Transit::GateRecord &record : result.gates)
    {
        QJsonObject item;
        item.insert(QStringLiteral("index"), record.index);
        item.insert(QStringLiteral("date"), record.date);
        item.insert(QStringLiteral("time"), record.timeKnown ? QJsonValue(record.time) : QJsonValue());
        item.insert(QStringLiteral("direction_code"), record.directionCode);
        item.insert(QStringLiteral("direction"), record.directionLabel);
        item.insert(QStringLiteral("station"), stationJson(record.station));
        item.insert(QStringLiteral("device_id"), record.deviceId);
        item.insert(QStringLiteral("amount"), static_cast<double>(record.amount));
        item.insert(QStringLiteral("raw"), hexBytes(record.raw));
        gates.append(item);
    }
    root.insert(QStringLiteral("gate_history"), gates);

    QJsonArray services;
    for (const Transit::ServiceReadStatus &status : result.services)
    {
        QJsonObject item;
        item.insert(QStringLiteral("service"), QStringLiteral("0x%1")
                    .arg(status.code, 4, 16, QLatin1Char('0')).toUpper());
        item.insert(QStringLiteral("name"), status.name);
        item.insert(QStringLiteral("access"), FelicaCodes::serviceAccess(status.code));
        item.insert(QStringLiteral("blocks_expected"), status.blocksExpected);
        item.insert(QStringLiteral("blocks_read"), status.blocksRead);
        item.insert(QStringLiteral("success"), status.success);
        item.insert(QStringLiteral("error"), status.error);
        services.append(item);
    }
    root.insert(QStringLiteral("services"), services);

    QJsonObject readStatus;
    readStatus.insert(QStringLiteral("partial"), result.partial);
    readStatus.insert(QStringLiteral("cancelled"), result.cancelled);
    QJsonArray warnings;
    for (const QString &warning : result.warnings)
        warnings.append(warning);
    readStatus.insert(QStringLiteral("warnings"), warnings);
    root.insert(QStringLiteral("read_status"), readStatus);

    if (includeRawBlocks)
    {
        QJsonArray raw;
        for (const Transit::RawBlock &block : result.rawBlocks)
        {
            QJsonObject item;
            item.insert(QStringLiteral("service"), QStringLiteral("0x%1")
                        .arg(block.service, 4, 16, QLatin1Char('0')).toUpper());
            item.insert(QStringLiteral("block"), block.block);
            item.insert(QStringLiteral("data"), hexBytes(block.data));
            raw.append(item);
        }
        root.insert(QStringLiteral("raw_blocks"), raw);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray historyCsv(const Transit::ReadResult &result)
{
    QString out;
    out += QStringLiteral("index,date,time,time_source,kind,terminal,transaction,payment,")
           + QStringLiteral("entry_station,entry_candidates,exit_station,exit_candidates,")
           + QStringLiteral("amount,balance,transaction_number,raw\n");
    for (const Transit::TransactionRecord &record : result.transactions)
    {
        out += csvLine(QStringList()
                       << QString::number(record.index)
                       << record.date
                       << (record.timeKnown ? record.time : QString())
                       << record.timeSource
                       << kindName(record.kind)
                       << record.terminalLabel
                       << record.transactionLabel
                       << record.paymentLabel
                       << stationText(record.entry)
                       << candidateText(record.entry)
                       << stationText(record.exit)
                       << candidateText(record.exit)
                       << (record.deltaKnown ? QString::number(record.delta) : QStringLiteral("?"))
                       << QString::number(record.balance)
                       << QString::number(record.transactionNumber)
                       << hexBytes(record.raw));
    }

    QByteArray data = out.toUtf8();
    data.prepend("\xEF\xBB\xBF");
    return data;
}

QByteArray gateCsv(const Transit::ReadResult &result)
{
    QString out;
    out += QStringLiteral("index,date,time,direction,station,station_candidates,amount,device,raw\n");
    for (const Transit::GateRecord &record : result.gates)
    {
        out += csvLine(QStringList()
                       << QString::number(record.index)
                       << record.date
                       << (record.timeKnown ? record.time : QString())
                       << record.directionLabel
                       << stationText(record.station)
                       << candidateText(record.station)
                       << QString::number(record.amount)
                       << record.deviceId
                       << hexBytes(record.raw));
    }

    QByteArray data = out.toUtf8();
    data.prepend("\xEF\xBB\xBF");
    return data;
}

} // namespace FelicaExport
