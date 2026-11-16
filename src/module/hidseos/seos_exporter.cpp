#include "seos_exporter.h"

#include "seos_codes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace HidSeos {
namespace Export {

namespace {

QString compactHex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex().toUpper());
}

QString hex16(int value, int width = 2)
{
    return QStringLiteral("0x%1")
        .arg(value, width, 16, QLatin1Char('0'))
        .toUpper()
        .replace(QStringLiteral("0X"), QStringLiteral("0x"));
}

QString kindName(CardKind kind)
{
    switch (kind) {
    case CardKind::TMoney: return QStringLiteral("T-money");
    case CardKind::Climate: return QStringLiteral("Climate Card");
    case CardKind::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QString eventName(int code)
{
    return Codes::eventLabel(code);
}

QString transportName(int code)
{
    return Codes::transportLabel(code);
}

QJsonObject legJson(const GroupedTripLeg &leg)
{
    QJsonObject object;
    object.insert(QStringLiteral("transport"), transportName(leg.transportCode));
    object.insert(QStringLiteral("transport_code"), leg.transportCode);
    object.insert(QStringLiteral("board_time"),
                  leg.boardTime.isEmpty() ? QJsonValue() : QJsonValue(leg.boardTime));
    object.insert(QStringLiteral("alight_time"),
                  leg.alightTime.isEmpty() ? QJsonValue() : QJsonValue(leg.alightTime));
    object.insert(QStringLiteral("fare_base_krw"), static_cast<double>(leg.fareBaseKrw));
    object.insert(QStringLiteral("distance_m"), leg.distanceM);
    return object;
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

QByteArray withBom(const QString &text)
{
    QByteArray data = text.toUtf8();
    data.prepend("\xEF\xBB\xBF");
    return data;
}

} // namespace

QByteArray toJson(const ReadResult &result)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    root.insert(QStringLiteral("generated_at"),
                QDateTime::currentDateTime().toString(Qt::ISODate));

    QJsonObject source;
    source.insert(QStringLiteral("application"), QStringLiteral("Proxmark3GUI"));
    source.insert(QStringLiteral("profile"), QStringLiteral("ks-x-6924-tmoney"));
    source.insert(QStringLiteral("protocol"), result.protocol);
    source.insert(QStringLiteral("aid"), result.aid);
    source.insert(QStringLiteral("read_only"), true);
    root.insert(QStringLiteral("source"), source);

    QJsonObject card;
    card.insert(QStringLiteral("protocol"), result.protocol);
    card.insert(QStringLiteral("uid"), result.uid);
    card.insert(QStringLiteral("atqa"), result.atqa);
    card.insert(QStringLiteral("sak"), result.sak);
    card.insert(QStringLiteral("ats"), result.ats);
    card.insert(QStringLiteral("detected_at"), result.detectedAt);
    card.insert(QStringLiteral("iso14443a"), result.iso14443a);
    card.insert(QStringLiteral("select_ok"), result.selectOk);
    card.insert(QStringLiteral("card_kind"), kindName(result.kind));
    card.insert(QStringLiteral("card_type_code"),
                result.purse.cardTypeCode >= 0 ? QJsonValue(result.purse.cardTypeCode)
                                               : QJsonValue());
    const QString cardTypeLabel = Codes::cardTypeLabel(result.purse.cardTypeCode);
    card.insert(QStringLiteral("card_type"),
                cardTypeLabel.isEmpty() ? QJsonValue() : QJsonValue(cardTypeLabel));
    card.insert(QStringLiteral("algorithm"),
                result.purse.algorithm >= 0 ? QJsonValue(hex16(result.purse.algorithm))
                                            : QJsonValue());
    card.insert(QStringLiteral("key_version"),
                result.purse.keyVersion >= 0 ? QJsonValue(result.purse.keyVersion)
                                             : QJsonValue());
    card.insert(QStringLiteral("id_center"),
                result.purse.idCenter >= 0 ? QJsonValue(result.purse.idCenter)
                                           : QJsonValue());
    const QString issuer = Codes::issuerLabel(result.purse.idCenter);
    card.insert(QStringLiteral("issuer"), issuer.isEmpty() ? QJsonValue() : QJsonValue(issuer));
    card.insert(QStringLiteral("csn"), result.purse.csn);
    card.insert(QStringLiteral("idtr"), result.purse.idtr);
    card.insert(QStringLiteral("issue_date"),
                result.purse.issueDate.isEmpty() ? QJsonValue() : QJsonValue(result.purse.issueDate));
    card.insert(QStringLiteral("expiry_date"),
                result.purse.expiryDate.isEmpty() ? QJsonValue() : QJsonValue(result.purse.expiryDate));
    card.insert(QStringLiteral("user_code"),
                result.purse.userCode >= 0 ? QJsonValue(result.purse.userCode) : QJsonValue());
    const QString userType = Codes::userTypeLabel(result.purse.userCode);
    card.insert(QStringLiteral("user_type"),
                userType.isEmpty() ? QJsonValue() : QJsonValue(userType));
    card.insert(QStringLiteral("discount"),
                result.purse.discount >= 0 ? QJsonValue(result.purse.discount) : QJsonValue());
    card.insert(QStringLiteral("max_balance_krw"),
                static_cast<double>(result.purse.maxBalanceKrw));
    card.insert(QStringLiteral("branch_code"),
                result.purse.branchCode >= 0 ? QJsonValue(hex16(result.purse.branchCode, 4))
                                             : QJsonValue());
    card.insert(QStringLiteral("transaction_limit"),
                static_cast<double>(result.purse.transactionLimit));
    card.insert(QStringLiteral("mobile_carrier"),
                result.purse.mobileCarrier >= 0 ? QJsonValue(result.purse.mobileCarrier)
                                                : QJsonValue());
    card.insert(QStringLiteral("financial_institution"),
                result.purse.financialInstitution >= 0
                    ? QJsonValue(result.purse.financialInstitution) : QJsonValue());
    card.insert(QStringLiteral("reserved"), compactHex(result.purse.reserved));
    card.insert(QStringLiteral("raw"), compactHex(result.purse.raw));
    root.insert(QStringLiteral("card_info"), card);

    QJsonObject balance;
    balance.insert(QStringLiteral("supported"), result.balance.supported);
    balance.insert(QStringLiteral("known"), result.balance.known);
    balance.insert(QStringLiteral("amount_krw"),
                   result.balance.known
                       ? QJsonValue(static_cast<double>(result.balance.amountKrw))
                       : QJsonValue());
    balance.insert(QStringLiteral("status_word"),
                   result.balance.sw != 0 ? QJsonValue(hex16(result.balance.sw, 4))
                                          : QJsonValue());
    balance.insert(QStringLiteral("reason"),
                   result.balance.reason.isEmpty() ? QJsonValue()
                                                   : QJsonValue(result.balance.reason));
    balance.insert(QStringLiteral("raw"), compactHex(result.balance.raw));
    root.insert(QStringLiteral("balance"), balance);

    QJsonObject pass;
    pass.insert(QStringLiteral("present"), result.pass.present);
    pass.insert(QStringLiteral("valid_from"),
                result.pass.present ? QJsonValue(result.pass.validFrom) : QJsonValue());
    pass.insert(QStringLiteral("valid_to"),
                result.pass.present ? QJsonValue(result.pass.validTo) : QJsonValue());
    pass.insert(QStringLiteral("days"), result.pass.present ? result.pass.days : 0);
    pass.insert(QStringLiteral("product_code"),
                result.pass.present ? QJsonValue(hex16(result.pass.productCode, 4))
                                    : QJsonValue());
    pass.insert(QStringLiteral("unknown_14_17"), compactHex(result.pass.unknown));
    pass.insert(QStringLiteral("status"),
                result.pass.present ? QJsonValue(result.pass.status) : QJsonValue());
    pass.insert(QStringLiteral("raw"), compactHex(result.pass.raw));
    root.insert(QStringLiteral("pass"), pass);

    QJsonObject calibration;
    calibration.insert(QStringLiteral("community_epoch"),
                       result.calibration.communityEpoch.toString(Qt::ISODate));
    calibration.insert(QStringLiteral("observed_epoch"),
                       result.calibration.observedEpoch.toString(Qt::ISODate));
    calibration.insert(QStringLiteral("offset_days"), result.calibration.offsetDays);
    calibration.insert(QStringLiteral("verified"), result.calibration.verified);
    calibration.insert(QStringLiteral("source"), result.calibration.source);
    root.insert(QStringLiteral("time_calibration"), calibration);

    QJsonArray transactions;
    for (const TripRecord &trip : result.trips) {
        QJsonObject item;
        item.insert(QStringLiteral("kind"), QStringLiteral("trip"));
        item.insert(QStringLiteral("sfi"), 3);
        item.insert(QStringLiteral("index"), trip.index);
        item.insert(QStringLiteral("date"), trip.date);
        item.insert(QStringLiteral("time"), trip.time);
        item.insert(QStringLiteral("datetime"), trip.datetime);
        item.insert(QStringLiteral("community_datetime"), trip.communityDatetime);
        item.insert(QStringLiteral("raw_timestamp"), trip.rawTimestamp);
        item.insert(QStringLiteral("event"), eventName(trip.eventCode));
        item.insert(QStringLiteral("event_code"), trip.eventCode);
        item.insert(QStringLiteral("transport"), transportName(trip.transportCode));
        item.insert(QStringLiteral("transport_code"), trip.transportCode);
        item.insert(QStringLiteral("counter"), trip.counter);
        item.insert(QStringLiteral("journey_counter"), trip.journeyCounter);
        item.insert(QStringLiteral("journey_fare_krw"),
                    static_cast<double>(trip.journeyFareKrw));
        item.insert(QStringLiteral("fare_total_krw"),
                    static_cast<double>(trip.fareTotalKrw));
        item.insert(QStringLiteral("fare_base_krw"),
                    static_cast<double>(trip.fareBaseKrw));
        item.insert(QStringLiteral("distance_m"), trip.distanceM);
        item.insert(QStringLiteral("station_id"), trip.stationId);
        item.insert(QStringLiteral("device"), trip.device);
        item.insert(QStringLiteral("day_counter"), static_cast<double>(trip.dayCounter));
        item.insert(QStringLiteral("raw"), compactHex(trip.raw));
        transactions.append(item);
    }
    for (const FinancialRecord &record : result.financial) {
        QJsonObject item;
        item.insert(QStringLiteral("kind"), QStringLiteral("financial"));
        item.insert(QStringLiteral("sfi"), record.sfi);
        item.insert(QStringLiteral("index"), record.index);
        item.insert(QStringLiteral("type"), Codes::financialTypeLabel(record.type));
        item.insert(QStringLiteral("type_code"), record.type);
        item.insert(QStringLiteral("balance_krw"), static_cast<double>(record.balanceKrw));
        item.insert(QStringLiteral("sequence"), record.sequence);
        item.insert(QStringLiteral("fare_krw"), static_cast<double>(record.fareKrw));
        item.insert(QStringLiteral("marker"), hex16(record.marker, 4));
        item.insert(QStringLiteral("marker_meaning"), Codes::markerLabel(record.marker));
        item.insert(QStringLiteral("terminal"), record.terminal);
        item.insert(QStringLiteral("bcd_datetime"), record.bcdDatetime);
        item.insert(QStringLiteral("raw"), compactHex(record.raw));
        transactions.append(item);
    }
    for (const FinancialRecord &record : result.readEvents) {
        QJsonObject item;
        item.insert(QStringLiteral("kind"), QStringLiteral("read_event"));
        item.insert(QStringLiteral("sfi"), record.sfi);
        item.insert(QStringLiteral("index"), record.index);
        item.insert(QStringLiteral("balance_krw"), static_cast<double>(record.balanceKrw));
        item.insert(QStringLiteral("sequence"), record.sequence);
        item.insert(QStringLiteral("fare_krw"), static_cast<double>(record.fareKrw));
        item.insert(QStringLiteral("marker"), hex16(record.marker, 4));
        item.insert(QStringLiteral("marker_meaning"), Codes::markerLabel(record.marker));
        item.insert(QStringLiteral("terminal"), record.terminal);
        item.insert(QStringLiteral("bcd_datetime"), record.bcdDatetime);
        item.insert(QStringLiteral("raw"), compactHex(record.raw));
        transactions.append(item);
    }
    root.insert(QStringLiteral("raw_transactions"), transactions);

    QJsonArray grouped;
    for (const GroupedTrip &trip : result.groupedTrips) {
        QJsonObject item;
        item.insert(QStringLiteral("counter"), trip.counter);
        item.insert(QStringLiteral("date"), trip.date);
        item.insert(QStringLiteral("start_time"),
                    trip.startTime.isEmpty() ? QJsonValue() : QJsonValue(trip.startTime));
        item.insert(QStringLiteral("end_time"),
                    trip.endTime.isEmpty() ? QJsonValue() : QJsonValue(trip.endTime));
        item.insert(QStringLiteral("journey_fare_krw"),
                    static_cast<double>(trip.journeyFareKrw));
        item.insert(QStringLiteral("tap_count"), trip.tapCount);
        QJsonArray legs;
        for (const GroupedTripLeg &leg : trip.legs)
            legs.append(legJson(leg));
        item.insert(QStringLiteral("legs"), legs);
        grouped.append(item);
    }
    root.insert(QStringLiteral("grouped_trips"), grouped);

    QJsonObject records;
    records.insert(QStringLiteral("sfi3_found"), result.trips.size());
    int sfi4 = 0;
    int sfi5 = 0;
    for (const FinancialRecord &record : result.financial) {
        if (record.sfi == 4)
            ++sfi4;
        else if (record.sfi == 5)
            ++sfi5;
    }
    records.insert(QStringLiteral("sfi4_found"), sfi4);
    records.insert(QStringLiteral("sfi5_found"), sfi5);
    records.insert(QStringLiteral("sfi11_found"), result.readEvents.size());
    records.insert(QStringLiteral("sfi13"), compactHex(result.sfi13));
    records.insert(QStringLiteral("recorded_fare_total_krw"),
                   static_cast<double>(result.recordedFareTotalKrw()));
    root.insert(QStringLiteral("records"), records);

    QJsonObject readStatus;
    readStatus.insert(QStringLiteral("partial"), result.partial);
    readStatus.insert(QStringLiteral("cancelled"), result.cancelled);
    readStatus.insert(QStringLiteral("raw_record_count"), result.rawRecords.size());
    readStatus.insert(QStringLiteral("warning_count"), result.warnings.size());
    root.insert(QStringLiteral("read_status"), readStatus);

    QJsonArray warnings;
    for (const QString &warning : result.warnings)
        warnings.append(warning);
    root.insert(QStringLiteral("warnings"), warnings);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray transactionsCsv(const ReadResult &result)
{
    QString out;
    out += QStringLiteral("kind,sfi,index,date,time,event,transport,counter,journey_counter,")
           + QStringLiteral("journey_fare_krw,fare_total_krw,fare_base_krw,balance_krw,fare_krw,")
           + QStringLiteral("marker,terminal,distance_m,station_id,day_counter,community_datetime,")
           + QStringLiteral("raw\n");

    for (const TripRecord &trip : result.trips) {
        out += csvLine(QStringList()
                       << QStringLiteral("trip")
                       << QStringLiteral("3")
                       << QString::number(trip.index)
                       << trip.date
                       << trip.time
                       << eventName(trip.eventCode)
                       << transportName(trip.transportCode)
                       << QString::number(trip.counter)
                       << QString::number(trip.journeyCounter)
                       << QString::number(trip.journeyFareKrw)
                       << QString::number(trip.fareTotalKrw)
                       << QString::number(trip.fareBaseKrw)
                       << QString()
                       << QString()
                       << QString()
                       << trip.device
                       << QString::number(trip.distanceM)
                       << trip.stationId
                       << QString::number(trip.dayCounter)
                       << trip.communityDatetime
                       << compactHex(trip.raw));
    }

    for (const FinancialRecord &record : result.financial) {
        out += csvLine(QStringList()
                       << QStringLiteral("financial")
                       << QString::number(record.sfi)
                       << QString::number(record.index)
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString::number(record.balanceKrw)
                       << QString::number(record.fareKrw)
                       << hex16(record.marker, 4)
                       << record.terminal
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << compactHex(record.raw));
    }

    for (const FinancialRecord &record : result.readEvents) {
        out += csvLine(QStringList()
                       << QStringLiteral("read_event")
                       << QStringLiteral("11")
                       << QString::number(record.index)
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << QString::number(record.balanceKrw)
                       << QString::number(record.fareKrw)
                       << hex16(record.marker, 4)
                       << record.terminal
                       << QString()
                       << QString()
                       << QString()
                       << QString()
                       << compactHex(record.raw));
    }

    return withBom(out);
}

QByteArray tripsCsv(const ReadResult &result)
{
    QString out;
    out += QStringLiteral("counter,date,start_time,end_time,journey_fare_krw,tap_count,legs,raw_note\n");

    for (const GroupedTrip &trip : result.groupedTrips) {
        QStringList legs;
        for (const GroupedTripLeg &leg : trip.legs) {
            QString description = transportName(leg.transportCode);
            if (!leg.boardTime.isEmpty() || !leg.alightTime.isEmpty()) {
                description += QLatin1Char(' ')
                               + (leg.boardTime.isEmpty() ? QStringLiteral("--") : leg.boardTime)
                               + QLatin1Char('-')
                               + (leg.alightTime.isEmpty() ? QStringLiteral("--") : leg.alightTime);
            }
            description += QStringLiteral(" (%1)").arg(leg.fareBaseKrw);
            legs.append(description);
        }

        out += csvLine(QStringList()
                       << QString::number(trip.counter)
                       << trip.date
                       << trip.startTime
                       << trip.endTime
                       << QString::number(trip.journeyFareKrw)
                       << QString::number(trip.tapCount)
                       << legs.join(QStringLiteral(" + "))
                       << QStringLiteral("recorded nominal fare; the pass may cover it"));
    }

    return withBom(out);
}

} // namespace Export
} // namespace HidSeos
