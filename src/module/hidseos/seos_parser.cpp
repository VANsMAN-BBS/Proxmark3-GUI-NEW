#include "seos_parser.h"

#include "seos_codes.h"

#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace HidSeos {
namespace Parser {

namespace {

const QDate kCommunityEpoch(1989, 6, 14);

bool isHexDigit(QChar c)
{
    const ushort value = c.unicode();
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')
           || (value >= 'A' && value <= 'F');
}

quint16 readBe16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

qint64 readBe32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return (static_cast<qint64>(static_cast<quint8>(data.at(offset))) << 24)
           | (static_cast<qint64>(static_cast<quint8>(data.at(offset + 1))) << 16)
           | (static_cast<qint64>(static_cast<quint8>(data.at(offset + 2))) << 8)
           | static_cast<quint8>(data.at(offset + 3));
}

QString bcdDigits(const QByteArray &data)
{
    QString digits;
    digits.reserve(data.size() * 2);
    for (int i = 0; i < data.size(); ++i) {
        const quint8 value = static_cast<quint8>(data.at(i));
        digits += QString::number(value >> 4, 16).toUpper();
        digits += QString::number(value & 0x0F, 16).toUpper();
    }
    return digits;
}

QString bcdDate(const QByteArray &data)
{
    if (data.size() != 4)
        return QString();

    const QString digits = bcdDigits(data);
    if (digits.size() != 8)
        return QString();

    bool ok = false;
    const int year = digits.left(4).toInt(&ok);
    if (!ok)
        return QString();
    const int month = digits.mid(4, 2).toInt(&ok);
    if (!ok)
        return QString();
    const int day = digits.mid(6, 2).toInt(&ok);
    if (!ok)
        return QString();

    const QDate date(year, month, day);
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

QString hexOf(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex().toUpper());
}

void setTripTimestamp(TripRecord *trip, quint32 packed, const QDate &epoch)
{
    const int day = static_cast<int>(packed >> 16);
    const int hour = static_cast<int>((packed >> 11) & 0x1F);
    const int minute = static_cast<int>((packed >> 5) & 0x3F);
    const int second = static_cast<int>((packed & 0x1F) * 2);

    trip->dayCounter = static_cast<quint32>(day);
    trip->hour = hour;
    trip->minute = minute;
    trip->second = second;

    const QDate date = epoch.addDays(day);
    trip->date = date.toString(Qt::ISODate);
    trip->time = QStringLiteral("%1:%2")
                     .arg(hour, 2, 10, QLatin1Char('0'))
                     .arg(minute, 2, 10, QLatin1Char('0'));
    trip->datetime = QStringLiteral("%1 %2:%3")
                         .arg(trip->date, trip->time)
                         .arg(second, 2, 10, QLatin1Char('0'));
}

} // namespace

ApduResponse parseApduOutput(const QString &output)
{
    ApduResponse response;
    const QStringList lines = output.split(QLatin1Char('\n'));
    QString hex;

    for (const QString &line : lines) {
        const int marker = line.indexOf(QStringLiteral("<<<"));
        if (marker < 0)
            continue;

        QString rest = line.mid(marker + 3).trimmed();
        if (rest.startsWith(QStringLiteral("status:"), Qt::CaseInsensitive))
            continue;

        int length = 0;
        while (length < rest.size() && isHexDigit(rest.at(length)))
            ++length;
        if (length >= 4 && (length % 2) == 0) {
            hex = rest.left(length);
            break;
        }
    }

    if (hex.isEmpty()) {
        response.error = QStringLiteral("No APDU response was found in the client output.");
        return response;
    }

    const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
    if (bytes.size() < 2) {
        response.error = QStringLiteral("The APDU response is too short.");
        return response;
    }

    response.responded = true;
    response.raw = bytes;
    response.data = bytes.left(bytes.size() - 2);
    response.sw = static_cast<quint16>((static_cast<quint8>(bytes.at(bytes.size() - 2)) << 8)
                                       | static_cast<quint8>(bytes.at(bytes.size() - 1)));
    return response;
}

bool parseCardInfo(const QString &output, QString *uid, QString *atqa,
                   QString *sak, QString *ats)
{
    static const QRegularExpression uidRe(
        QStringLiteral("UID[:\\s]*([0-9A-Fa-f][0-9A-Fa-f ]{7,})"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression atqaRe(
        QStringLiteral("ATQA[:\\s]*([0-9A-Fa-f][0-9A-Fa-f ]{3,})"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sakRe(
        QStringLiteral("SAK[:\\s]*([0-9A-Fa-f]{2})"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression atsRe(
        QStringLiteral("ATS[:\\s]*([0-9A-Fa-f][0-9A-Fa-f ]{1,})"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch uidMatch = uidRe.match(output);
    if (!uidMatch.hasMatch())
        return false;

    QString uidValue = uidMatch.captured(1).toUpper();
    uidValue.remove(QLatin1Char(' '));
    if (uidValue.size() < 8 || (uidValue.size() % 2) != 0)
        return false;
    if (uid)
        *uid = uidValue;

    const QRegularExpressionMatch atqaMatch = atqaRe.match(output);
    if (atqaMatch.hasMatch()) {
        QString value = atqaMatch.captured(1).toUpper();
        value.remove(QLatin1Char(' '));
        if (atqa)
            *atqa = value;
    }

    const QRegularExpressionMatch sakMatch = sakRe.match(output);
    if (sakMatch.hasMatch() && sak)
        *sak = sakMatch.captured(1).toUpper();

    const QRegularExpressionMatch atsMatch = atsRe.match(output);
    if (atsMatch.hasMatch()) {
        QString value = atsMatch.captured(1).toUpper();
        value.remove(QLatin1Char(' '));
        if (ats)
            *ats = value;
    }
    return true;
}

bool parsePurseInfo(const QByteArray &fci, PurseInfo *purse, QStringList *warnings)
{
    if (!purse)
        return false;

    *purse = PurseInfo();

    QByteArray body;
    if (fci.size() >= 4 && static_cast<quint8>(fci.at(0)) == 0x6F) {
        const int outerLength = static_cast<quint8>(fci.at(1));
        if (outerLength < 2 || 2 + outerLength > fci.size())
            return false;
        const QByteArray inner = fci.mid(2, outerLength);
        if (inner.size() < 2 || static_cast<quint8>(inner.at(0)) != 0xB0)
            return false;
        const int purseLength = static_cast<quint8>(inner.at(1));
        if (purseLength < 47 || 2 + purseLength > inner.size())
            return false;
        body = inner.mid(2, purseLength);
    } else if (fci.size() == 47) {
        // Convenience for tests and for callers that already stripped the FCI.
        body = fci;
    } else {
        return false;
    }

    if (body.size() < 47)
        return false;

    const QByteArray raw = body.left(47);
    purse->raw = raw;
    purse->cardTypeCode = static_cast<quint8>(raw.at(0));
    purse->algorithm = static_cast<quint8>(raw.at(1));
    purse->keyVersion = static_cast<quint8>(raw.at(2));
    purse->idCenter = static_cast<quint8>(raw.at(3));
    purse->csn = bcdDigits(raw.mid(4, 8));
    purse->idtr = bcdDigits(raw.mid(12, 5));
    purse->issueDate = bcdDate(raw.mid(17, 4));
    purse->expiryDate = bcdDate(raw.mid(21, 4));
    purse->userCode = static_cast<quint8>(raw.at(25));
    purse->discount = static_cast<quint8>(raw.at(26));
    purse->maxBalanceKrw = readBe32(raw, 27);
    purse->branchCode = readBe16(raw, 31);
    purse->transactionLimit = readBe32(raw, 33);
    purse->mobileCarrier = static_cast<quint8>(raw.at(37));
    purse->financialInstitution = static_cast<quint8>(raw.at(38));
    purse->reserved = raw.mid(39, 8);
    purse->valid = true;

    if (warnings && purse->issueDate.isEmpty())
        warnings->append(QStringLiteral("PurseInfo issue date is not a valid BCD date."));
    if (warnings && purse->expiryDate.isEmpty())
        warnings->append(QStringLiteral("PurseInfo expiry date is not a valid BCD date."));
    return true;
}

bool parsePassRecord(const QByteArray &data, PassInfo *pass, QStringList *warnings)
{
    if (!pass)
        return false;

    *pass = PassInfo();
    if (data.size() < 27) {
        if (warnings) {
            warnings->append(QStringLiteral("SFI 7 record is %1 bytes, expected 27.")
                                 .arg(data.size()));
        }
        return false;
    }
    if (data.size() > 27 && warnings)
        warnings->append(QStringLiteral("SFI 7 record has %1 bytes; only the first 27 are decoded.")
                             .arg(data.size()));

    const QByteArray raw = data.left(27);

    auto decodeBcdDate = [](const QByteArray &value, QDate *date) -> bool {
        if (value.size() != 3)
            return false;
        const QString digits = bcdDigits(value);
        if (digits.size() != 6)
            return false;
        bool ok = false;
        const int year = 2000 + digits.left(2).toInt(&ok);
        if (!ok)
            return false;
        const int month = digits.mid(2, 2).toInt(&ok);
        if (!ok)
            return false;
        const int day = digits.mid(4, 2).toInt(&ok);
        if (!ok)
            return false;
        *date = QDate(year, month, day);
        return date->isValid();
    };

    QDate from;
    QDate to;
    if (!decodeBcdDate(raw.mid(0, 3), &from) || !decodeBcdDate(raw.mid(3, 3), &to)) {
        if (warnings)
            warnings->append(QStringLiteral("SFI 7 pass dates are not valid BCD dates."));
        return false;
    }
    if (to < from) {
        if (warnings)
            warnings->append(QStringLiteral("SFI 7 pass end date precedes its start date."));
        return false;
    }

    pass->present = true;
    pass->validFrom = from.toString(Qt::ISODate);
    pass->validTo = to.toString(Qt::ISODate);
    pass->days = static_cast<int>(from.daysTo(to)) + 1;
    pass->productCode = readBe16(raw, 12);
    pass->unknown = raw.mid(14, 4);
    pass->status = static_cast<quint8>(raw.at(26));
    pass->raw = raw;
    return true;
}

bool parseTripRecord(const QByteArray &data, int index, TripRecord *trip,
                     QStringList *warnings)
{
    if (!trip)
        return false;

    *trip = TripRecord();
    trip->index = index;
    trip->raw = data;

    if (data.size() < 52) {
        trip->error = QStringLiteral("SFI 3 record %1 is %2 bytes, expected 52.")
                          .arg(index)
                          .arg(data.size());
        if (warnings)
            warnings->append(trip->error);
        return false;
    }

    const QByteArray raw = data.left(52);
    trip->typeHex = hexOf(raw.mid(0, 2));
    trip->eventCode = static_cast<quint8>(raw.at(2));
    trip->transportCode = static_cast<quint8>(raw.at(3));
    trip->counter = static_cast<quint8>(raw.at(4));
    trip->device = hexOf(raw.mid(6, 3));
    trip->rawTimestamp = hexOf(raw.mid(11, 4));

    const quint32 packed = static_cast<quint32>(
        (static_cast<quint32>(static_cast<quint8>(raw.at(11))) << 24)
        | (static_cast<quint32>(static_cast<quint8>(raw.at(12))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(raw.at(13))) << 8)
        | static_cast<quint32>(static_cast<quint8>(raw.at(14))));

    setTripTimestamp(trip, packed, kCommunityEpoch);
    trip->communityDatetime = trip->datetime;

    trip->distanceM = readBe16(raw, 19);
    trip->fareField22Krw = readBe16(raw, 22);
    trip->stationId = hexOf(raw.mid(24, 4));
    trip->fareBaseKrw = readBe16(raw, 40);
    trip->fareTotalKrw = readBe16(raw, 46);
    trip->surchargeKrw = readBe16(raw, 50);
    trip->journeyCounter = trip->counter;
    trip->journeyFareKrw = trip->fareTotalKrw;
    trip->valid = true;

    if (warnings && Codes::eventLabel(trip->eventCode).isEmpty()) {
        const QString code = QStringLiteral("0x%1")
                                 .arg(trip->eventCode, 2, 16, QLatin1Char('0'))
                                 .toUpper()
                                 .replace(QStringLiteral("0X"), QStringLiteral("0x"));
        warnings->append(QStringLiteral("SFI 3 record %1 has an unknown event code %2.")
                             .arg(index)
                             .arg(code));
    }
    return true;
}

QVector<TripRecord> parseTripSeries(const QVector<QByteArray> &records,
                                    QStringList *warnings)
{
    QVector<TripRecord> trips;
    QSet<QByteArray> seen;
    int duplicates = 0;

    for (int i = 0; i < records.size(); ++i) {
        TripRecord trip;
        if (!parseTripRecord(records.at(i), i + 1, &trip, warnings))
            continue;
        if (seen.contains(trip.raw)) {
            ++duplicates;
            continue;
        }
        seen.insert(trip.raw);
        trips.append(trip);
    }

    if (duplicates > 0 && warnings) {
        warnings->append(QStringLiteral("Ignored %1 duplicate SFI 3 record(s).")
                             .arg(duplicates));
    }
    return trips;
}

bool parseFinancialRecord(const QByteArray &data, int sfi, int index,
                          FinancialRecord *record, QStringList *warnings)
{
    if (!record)
        return false;

    *record = FinancialRecord();
    record->sfi = sfi;
    record->index = index;
    record->raw = data;

    const int minimum = (sfi == 11) ? 26 : 46;
    if (data.size() < minimum) {
        record->error = QStringLiteral("SFI %1 record %2 is %3 bytes, expected at least %4.")
                            .arg(sfi)
                            .arg(index)
                            .arg(data.size())
                            .arg(minimum);
        if (warnings)
            warnings->append(record->error);
        return false;
    }

    record->type = static_cast<quint8>(data.at(0));
    record->balanceKrw = readBe32(data, 2);
    record->sequence = readBe16(data, 8);
    record->fareKrw = readBe16(data, 12);
    record->marker = readBe16(data, 14);
    record->terminal = hexOf(data.mid(18, 8));
    const int bcdLength = qMin(7, data.size() - 26);
    if (bcdLength > 0)
        record->bcdDatetime = hexOf(data.mid(26, bcdLength));
    record->valid = true;

    if (warnings && sfi == 11 && data.size() != 27) {
        warnings->append(QStringLiteral("SFI 11 record %1 is %2 bytes; the documented length is 27.")
                             .arg(index)
                             .arg(data.size()));
    }
    return true;
}

QDateTime decodeTimestamp(quint32 packed, const QDate &epoch)
{
    const int day = static_cast<int>(packed >> 16);
    const int hour = static_cast<int>((packed >> 11) & 0x1F);
    const int minute = static_cast<int>((packed >> 5) & 0x3F);
    const int second = static_cast<int>((packed & 0x1F) * 2);
    if (hour > 23 || minute > 59 || second > 59)
        return QDateTime();
    return QDateTime(epoch.addDays(day), QTime(hour, minute, second));
}

QString packedTimestampHex(const QByteArray &record)
{
    if (record.size() < 15)
        return QString();
    return hexOf(record.mid(11, 4));
}

TimeCalibration deriveCalibration(const PassInfo &pass,
                                  const QVector<TripRecord> &communityTrips)
{
    TimeCalibration calibration;
    calibration.communityEpoch = kCommunityEpoch;
    calibration.observedEpoch = kCommunityEpoch;
    calibration.offsetDays = 0;
    calibration.verified = false;

    if (!pass.present || communityTrips.isEmpty()) {
        calibration.source =
            QStringLiteral("community epoch 1989-06-14 (T-money/Cashbee default)");
        return calibration;
    }

    const QDate passTo = QDate::fromString(pass.validTo, Qt::ISODate);
    QDate newest;
    for (const TripRecord &trip : communityTrips) {
        if (!trip.valid)
            continue;
        const QDate date = QDate::fromString(trip.communityDatetime.left(10), Qt::ISODate);
        if (date.isValid() && (!newest.isValid() || date > newest))
            newest = date;
    }

    if (!passTo.isValid() || !newest.isValid() || newest <= passTo) {
        calibration.source = QStringLiteral(
            "community epoch 1989-06-14; the SFI 7 pass does not indicate an offset");
        return calibration;
    }

    const int difference = static_cast<int>(passTo.daysTo(newest));
    if (difference > 31) {
        calibration.source = QStringLiteral(
            "community epoch 1989-06-14; the SFI 7 pass and SFI 3 day counters differ by %1 days, "
            "so no automatic offset was applied").arg(difference);
        return calibration;
    }

    calibration.offsetDays = -difference;
    calibration.observedEpoch = kCommunityEpoch.addDays(calibration.offsetDays);
    calibration.verified = true;
    calibration.source = QStringLiteral(
        "SFI 7 pass %1..%2 vs SFI 3 day counters: offset %3 day(s)")
        .arg(pass.validFrom, pass.validTo)
        .arg(calibration.offsetDays);
    return calibration;
}

void applyCalibration(const TimeCalibration &calibration, QVector<TripRecord> *trips)
{
    if (!trips)
        return;

    for (TripRecord &trip : *trips) {
        if (!trip.valid)
            continue;
        setTripTimestamp(&trip, (trip.dayCounter << 16)
                                    | (static_cast<quint32>(trip.hour & 0x1F) << 11)
                                    | (static_cast<quint32>(trip.minute & 0x3F) << 5)
                                    | (static_cast<quint32>(trip.second / 2) & 0x1F),
                         calibration.observedEpoch);
    }
}

QVector<GroupedTrip> groupTrips(const QVector<TripRecord> &trips, QStringList *warnings)
{
    QMap<int, QVector<int> > groups;
    for (int i = 0; i < trips.size(); ++i) {
        if (!trips.at(i).valid)
            continue;
        groups[trips.at(i).counter].append(i);
    }

    QVector<GroupedTrip> journeys;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QVector<int> &indices = it.value();
        if (indices.isEmpty())
            continue;

        GroupedTrip journey;
        journey.counter = it.key();
        journey.tapCount = indices.size();
        journey.date = trips.at(indices.first()).date;

        QSet<int> transports;
        for (int index : indices)
            transports.insert(trips.at(index).transportCode);

        QVector<int> sortedTransports;
        for (int transport : transports)
            sortedTransports.append(transport);
        std::sort(sortedTransports.begin(), sortedTransports.end());

        for (int transport : sortedTransports) {
            const TripRecord *boarding = nullptr;
            const TripRecord *alighting = nullptr;
            for (int index : indices) {
                const TripRecord &record = trips.at(index);
                if (record.transportCode != transport)
                    continue;
                if (record.eventCode == 0x00 && !boarding)
                    boarding = &record;
                if (record.eventCode == 0x01 && !alighting)
                    alighting = &record;
            }
            if (!boarding && !alighting)
                continue;

            GroupedTripLeg leg;
            leg.transportCode = transport;
            leg.boardTime = boarding ? boarding->time : QString();
            leg.alightTime = alighting ? alighting->time : QString();
            const TripRecord *reference = alighting ? alighting : boarding;
            leg.fareBaseKrw = reference->fareBaseKrw;
            leg.distanceM = reference->distanceM;
            journey.legs.append(leg);
        }

        std::sort(journey.legs.begin(), journey.legs.end(),
                  [](const GroupedTripLeg &left, const GroupedTripLeg &right)
                  {
                      if (left.boardTime.isEmpty() != right.boardTime.isEmpty())
                          return !left.boardTime.isEmpty();
                      return left.boardTime < right.boardTime;
                  });

        journey.startTime = journey.legs.isEmpty() ? QString()
                                                   : journey.legs.first().boardTime;
        journey.endTime = journey.legs.isEmpty() ? QString()
                                                 : journey.legs.last().alightTime;
        if (journey.startTime.isEmpty()) {
            for (const GroupedTripLeg &leg : journey.legs) {
                if (!leg.boardTime.isEmpty()) {
                    journey.startTime = leg.boardTime;
                    break;
                }
            }
        }
        if (journey.endTime.isEmpty()) {
            for (int i = journey.legs.size() - 1; i >= 0; --i) {
                if (!journey.legs.at(i).alightTime.isEmpty()) {
                    journey.endTime = journey.legs.at(i).alightTime;
                    break;
                }
            }
        }

        for (int index : indices)
            journey.journeyFareKrw = qMax(journey.journeyFareKrw, trips.at(index).fareTotalKrw);

        if (journey.legs.isEmpty() && warnings) {
            warnings->append(QStringLiteral(
                "Journey counter %1 has taps but no boarding/alighting pair.").arg(journey.counter));
        }
        journeys.append(journey);
    }

    std::sort(journeys.begin(), journeys.end(),
              [](const GroupedTrip &left, const GroupedTrip &right)
              {
                  if (left.date != right.date)
                      return left.date > right.date;
                  if (left.endTime != right.endTime)
                      return left.endTime > right.endTime;
                  return left.counter > right.counter;
              });
    return journeys;
}

} // namespace Parser
} // namespace HidSeos
