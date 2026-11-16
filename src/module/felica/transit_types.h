#ifndef TRANSIT_TYPES_H
#define TRANSIT_TYPES_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Vendor-neutral transit card data model.
 *
 * The model is intentionally independent of PASMO/Suica field layouts so a
 * future Octopus (or other) profile can provide its own parser. Monetary
 * values are integer minor units; `currency` is carried per value instead of
 * being hard-coded to JPY.
 */
namespace Transit {

enum class ParseStatus
{
    Ok,
    Empty,
    Truncated,
    UnknownType,
    Unavailable,
};

enum class TransactionKind
{
    Rail,
    Goods,
    Bus,
    Unknown,
};

struct StationCandidate
{
    QString table;   // "suica-viewer" or "solidx-legacy"
    int area = -1;
    QString company;
    QString line;
    QString station;
    QString note;
};

struct StationInfo
{
    bool resolved = false;
    bool ambiguous = false;
    QString rawCode;   // "E6/33"
    int lineCode = -1;
    int stationCode = -1;
    int area = -1;
    QString primaryName;
    QVector<StationCandidate> candidates;
};

struct CardInfo
{
    QString protocol;
    QString cardType;
    QString cardId;      // IDm
    QString pmm;
    QString icCode;
    QString icName;
    QString manufactureDate;
    QString issuer;
    QString profileId;
    QString detectedAt;
    QStringList systems;
};

struct BalanceInfo
{
    bool valid = false;
    qint64 amount = 0;
    QString currency;          // "JPY"
    QString sourceService;     // "0x008B"
    int confidence = 0;        // 0..100
    QByteArray raw;
};

struct TransactionRecord
{
    int index = 0;
    QByteArray raw;
    QString date;              // YYYY-MM-DD
    QString time;              // HH:MM:SS, empty when unknown
    bool timeKnown = false;
    QString timeSource;        // "record", "gate" (derived) or empty
    TransactionKind kind = TransactionKind::Unknown;

    int terminalCode = 0;
    QString terminalLabel;
    int transactionCode = 0;
    QString transactionLabel;
    int paymentCode = 0;
    QString paymentLabel;
    int gateInstructionCode = 0;
    QString gateInstructionLabel;

    StationInfo entry;
    StationInfo exit;

    bool hasPosId = false;
    int posId = 0;
    QString posIdHex;

    qint64 balance = 0;
    int transactionNumber = 0;
    int regionEntry = 0;
    int regionExit = 0;

    bool deltaKnown = false;
    qint64 delta = 0;          // negative = spent

    ParseStatus status = ParseStatus::Ok;
};

struct GateRecord
{
    int index = 0;
    QByteArray raw;
    int directionCode = 0;
    QString directionLabel;
    StationInfo station;
    QString deviceId;
    QString date;
    QString time;
    bool timeKnown = false;
    qint64 amount = 0;
    bool amountKnown = false;
    qint64 commuterFee = 0;
    ParseStatus status = ParseStatus::Ok;
};

struct ServiceDefinition
{
    int code = 0;          // logical 0x090F
    QString name;
    QString access;        // "public" / "keyed"
    int declaredBlocks = 0;
};

struct ServiceReadStatus
{
    int code = 0;
    QString name;
    bool attempted = false;
    bool success = false;
    int blocksRead = 0;
    int blocksExpected = 0;
    QString error;
};

struct RawBlock
{
    int service = 0;
    int block = 0;
    QByteArray data;
};

struct ReadResult
{
    CardInfo card;
    BalanceInfo balance;
    QVector<TransactionRecord> transactions;
    QVector<GateRecord> gates;
    bool hasLastEntry = false;
    StationInfo lastEntry;
    QVector<ServiceReadStatus> services;
    QVector<RawBlock> rawBlocks;
    QStringList warnings;
    bool partial = false;
    bool cancelled = false;
};

/** Card profile (Suica/PASMO, future Octopus, ...). */
class TransitCardProfile
{
public:
    virtual ~TransitCardProfile() {}

    virtual QString profileId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString currency() const = 0;

    virtual bool matches(const CardInfo &card) const = 0;
    virtual QVector<ServiceDefinition> services() const = 0;

    /**
     * Parse already-read raw blocks into `out`. Implementations must keep
     * unknown services/blocks available in `out.rawBlocks` and must not invent
     * values for encrypted or unavailable services.
     */
    virtual bool parse(const QVector<RawBlock> &blocks, ReadResult &out,
                       QStringList &warnings) const = 0;
};

} // namespace Transit

#endif // TRANSIT_TYPES_H
