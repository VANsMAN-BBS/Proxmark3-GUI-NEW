#ifndef HIDSEOS_SEOS_TYPES_H
#define HIDSEOS_SEOS_TYPES_H

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Read-only data model for KS X 6924 T-money / Climate Card (ISO14443-A +
 * ISO-DEP, AID D4100000030001).
 *
 * The model mirrors the field offsets documented for the two observed cards.
 * Unknown values keep their raw bytes and are never mapped to an invented
 * meaning. Monetary values are integer KRW, not scaled minor units.
 */
namespace HidSeos {

enum class CardKind
{
    Unknown,
    TMoney,
    Climate,
};

struct PurseInfo
{
    bool valid = false;
    int cardTypeCode = -1;
    int algorithm = -1;
    int keyVersion = -1;
    int idCenter = -1;
    int userCode = -1;
    int discount = -1;
    QString csn;          // card number, BCD digits
    QString idtr;
    QString issueDate;    // YYYY-MM-DD, empty when the BCD date is invalid
    QString expiryDate;
    qint64 maxBalanceKrw = 0;
    int branchCode = -1;
    qint64 transactionLimit = 0;
    int mobileCarrier = -1;
    int financialInstitution = -1;
    QByteArray reserved;
    QByteArray raw;       // full 47-byte PurseInfo
};

struct PassInfo
{
    bool present = false;
    QString validFrom;    // YYYY-MM-DD
    QString validTo;
    int days = 0;
    int productCode = 0;
    QByteArray unknown;   // offsets 14..17, not publicly documented
    int status = -1;
    QByteArray raw;       // 27-byte SFI 7 record
};

struct TripRecord
{
    int index = 0;          // 1-based READ RECORD slot
    QByteArray raw;         // full record bytes
    bool valid = false;
    QString error;          // parse error, empty when valid

    QString typeHex;        // offsets 0..1, normally "0132"
    int eventCode = -1;     // offset 2: 0x00 boarding, 0x01 alighting
    int transportCode = -1; // offset 3: 0x00 bus, 0x01 metro, 0x02 train
    int counter = -1;       // offset 4: journey counter shared by transfer legs
    QString device;         // offsets 6..8
    quint32 dayCounter = 0;
    QString rawTimestamp;   // offsets 11..14 as hex
    QString communityDatetime; // decoded with the community epoch (1989-06-14)
    QString datetime;       // decoded with the card-level calibration
    QString date;
    QString time;           // HH:MM
    int hour = 0;
    int minute = 0;
    int second = 0;
    int distanceM = 0;      // offsets 19..20
    qint64 fareField22Krw = 0; // offsets 22..23
    qint64 fareBaseKrw = 0; // offsets 40..41 (single leg)
    qint64 fareTotalKrw = 0; // offsets 46..47 (journey cumulative, displayed amount)
    QString stationId;      // offsets 24..27
    qint64 surchargeKrw = 0; // offsets 50..51
    int journeyCounter = -1;
    qint64 journeyFareKrw = 0;
};

struct FinancialRecord
{
    int sfi = 4;            // 4 (amount / read event), 5 (top-up), 11 (read event)
    int index = 0;
    QByteArray raw;
    bool valid = false;
    QString error;

    int type = -1;          // offset 0
    qint64 balanceKrw = 0;  // offsets 2..5
    int sequence = 0;       // offsets 8..9
    qint64 fareKrw = 0;     // offsets 12..13
    int marker = 0;         // offsets 14..15
    QString terminal;       // offsets 18..25
    QString bcdDatetime;    // offsets 26..32 (often zero)
};

struct GroupedTripLeg
{
    int transportCode = -1;
    QString boardTime;      // HH:MM, empty when the leg has no boarding record
    QString alightTime;
    qint64 fareBaseKrw = 0;
    int distanceM = 0;
};

struct GroupedTrip
{
    int counter = -1;
    QString date;
    QString startTime;
    QString endTime;
    qint64 journeyFareKrw = 0; // cumulative recorded fare, not a new charge
    int tapCount = 0;
    QVector<GroupedTripLeg> legs;
};

struct TimeCalibration
{
    QDate communityEpoch;
    QDate observedEpoch;
    int offsetDays = 0;
    bool verified = false;
    QString source;

    TimeCalibration()
        : communityEpoch(1989, 6, 14)
        , observedEpoch(1989, 6, 14)
        , source(QStringLiteral("community epoch 1989-06-14 (T-money/Cashbee default)"))
    {
    }
};

struct BalanceInfo
{
    bool supported = false;
    bool known = false;
    qint64 amountKrw = 0;
    int sw = 0;             // ISO 7816 status word, 0 when no response was parsed
    QString reason;         // user-facing reason when unsupported or unknown
    QByteArray raw;         // response data without the status word
};

struct RawRecord
{
    int sfi = 0;
    int index = 0;
    QByteArray data;
    bool parsed = false;
    QString note;
};

struct ReadResult
{
    QString protocol = QStringLiteral("ISO14443-A / ISO-DEP");
    QString aid = QStringLiteral("D4100000030001");
    QString uid;
    QString atqa;
    QString sak;
    QString ats;
    QString detectedAt;
    bool iso14443a = false;
    bool selectOk = false;

    PurseInfo purse;
    CardKind kind = CardKind::Unknown;
    BalanceInfo balance;
    PassInfo pass;
    TimeCalibration calibration;

    QVector<TripRecord> trips;
    QVector<FinancialRecord> financial;  // SFI 4 and 5
    QVector<FinancialRecord> readEvents; // SFI 11
    QVector<GroupedTrip> groupedTrips;
    QVector<RawRecord> rawRecords;
    QByteArray sfi13;

    QStringList warnings;
    bool partial = false;
    bool cancelled = false;

    /** Sum of the recorded (nominal) grouped journey fares. */
    qint64 recordedFareTotalKrw() const
    {
        qint64 total = 0;
        for (const GroupedTrip &trip : groupedTrips)
            total += trip.journeyFareKrw;
        return total;
    }

    /** Records that look like real charges (marker 0x0720 is a read event). */
    int purchaseCount() const
    {
        int count = 0;
        for (const FinancialRecord &record : financial) {
            if (record.valid && record.fareKrw > 0 && record.marker != 0x0720)
                ++count;
        }
        return count;
    }
};

} // namespace HidSeos

#endif // HIDSEOS_SEOS_TYPES_H
