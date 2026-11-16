#include "seos_codes.h"

namespace HidSeos {
namespace Codes {

QString cardTypeLabel(int code)
{
    switch (code) {
    case 0x00: return QStringLiteral("prepaid");
    case 0x10: return QStringLiteral("postpay");
    case 0x15: return QStringLiteral("mobile postpay");
    case 0xB5: return QStringLiteral("Climate Card / period-pass (proprietary)");
    default: break;
    }
    return QString();
}

QString issuerLabel(int code)
{
    switch (code) {
    case 0x01: return QStringLiteral("KFTC");
    case 0x03: return QStringLiteral("MYbi");
    case 0x06: return QStringLiteral("Mondex");
    case 0x07: return QStringLiteral("KEC");
    case 0x08: return QStringLiteral("T-money");
    case 0x09: return QStringLiteral("KORAIL");
    case 0x0B: return QStringLiteral("Cashbee (eB)");
    case 0x0C: return QStringLiteral("Seoul Bus");
    case 0x0D: return QStringLiteral("Cardnet");
    default: break;
    }
    return QString();
}

QString userTypeLabel(int code)
{
    switch (code) {
    case 0x01: return QStringLiteral("adult (regular)");
    case 0x02: return QStringLiteral("child");
    case 0x03: return QStringLiteral("youth");
    case 0x04: return QStringLiteral("senior");
    case 0x05: return QStringLiteral("disabled");
    case 0x11: return QStringLiteral("bus");
    case 0x12: return QStringLiteral("lorry");
    case 0xFF: return QStringLiteral("inactive");
    default: break;
    }
    return QString();
}

QString transportLabel(int code)
{
    switch (code) {
    case 0x00: return QStringLiteral("bus");
    case 0x01: return QStringLiteral("metro");
    case 0x02: return QStringLiteral("train");
    default: break;
    }
    return QString();
}

QString eventLabel(int code)
{
    switch (code) {
    case 0x00: return QStringLiteral("boarding");
    case 0x01: return QStringLiteral("alighting");
    default: break;
    }
    return QString();
}

QString markerLabel(int code)
{
    switch (code) {
    case 0x4913: return QStringLiteral("standard transit charge");
    case 0x4923: return QStringLiteral("transfer surcharge");
    case 0x0720: return QStringLiteral("card-read / sub-charge event");
    case 0x0000: return QStringLiteral("top-up");
    default: break;
    }
    return QString();
}

QString financialTypeLabel(int code)
{
    switch (code) {
    case 0x01: return QStringLiteral("transit");
    case 0x02: return QStringLiteral("top-up");
    default: break;
    }
    return QString();
}

QString sfiName(int sfi)
{
    switch (sfi) {
    case 2: return QStringLiteral("FCI");
    case 3: return QStringLiteral("trip records");
    case 4: return QStringLiteral("amount and card-read records");
    case 5: return QStringLiteral("top-up records");
    case 7: return QStringLiteral("period pass (Climate)");
    case 11: return QStringLiteral("card-read events (Climate)");
    case 13: return QStringLiteral("auxiliary file");
    default: break;
    }
    return QString();
}

QString statusWordText(int sw)
{
    switch (sw) {
    case 0x9000: return QStringLiteral("OK");
    case 0x6A82: return QStringLiteral("file or application not found");
    case 0x6A83: return QStringLiteral("record not found");
    case 0x6981: return QStringLiteral("command incompatible with file structure");
    case 0x6D00: return QStringLiteral("instruction not supported");
    case 0x6A86: return QStringLiteral("incorrect P1/P2");
    case 0x6986: return QStringLiteral("no current EF");
    case 0x6A88: return QStringLiteral("referenced data not found");
    default: break;
    }
    if ((sw & 0xFF00) == 0x6C00)
        return QStringLiteral("wrong Le (card expects %1 bytes)").arg(sw & 0xFF);
    return QString();
}

bool isKnownCardType(int code)
{
    return code == 0x00 || code == 0x10 || code == 0x15 || code == 0xB5;
}

bool isClimateCardType(int code)
{
    return code == 0xB5;
}

} // namespace Codes
} // namespace HidSeos
