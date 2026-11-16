#include "octopus_parser.h"

const quint16 OctopusBalanceParser::kSystemCode;
const quint16 OctopusBalanceParser::kBalanceService;
const int OctopusBalanceParser::kDepositOffsetDeci;

OctopusBalance OctopusBalanceParser::parse(const QByteArray &block0)
{
    OctopusBalance result;
    if (block0.size() < 4)
    {
        result.error = QStringLiteral("block is shorter than 4 bytes");
        return result;
    }

    result.raw = (static_cast<quint32>(static_cast<quint8>(block0.at(0))) << 24)
               | (static_cast<quint32>(static_cast<quint8>(block0.at(1))) << 16)
               | (static_cast<quint32>(static_cast<quint8>(block0.at(2))) << 8)
               | static_cast<quint32>(static_cast<quint8>(block0.at(3)));

    const qint64 raw = static_cast<qint64>(result.raw);
    if (raw < kDepositOffsetDeci)
    {
        result.error = QStringLiteral("raw balance is below the deposit offset");
        return result;
    }

    result.availableDeci = raw - kDepositOffsetDeci;
    result.depositDeci = kDepositOffsetDeci;
    result.totalDeci = raw;
    result.valid = true;
    return result;
}
