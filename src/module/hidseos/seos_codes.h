#ifndef HIDSEOS_SEOS_CODES_H
#define HIDSEOS_SEOS_CODES_H

#include <QString>

/**
 * KS X 6924 code tables.
 *
 * Only values that are documented in the source bundle are mapped. Everything
 * else returns an empty string so the caller can show the raw value instead of
 * an invented label.
 */
namespace HidSeos {
namespace Codes {

QString cardTypeLabel(int code);
QString issuerLabel(int code);
QString userTypeLabel(int code);
QString transportLabel(int code);
QString eventLabel(int code);
QString markerLabel(int code);
QString financialTypeLabel(int code);
QString sfiName(int sfi);
QString statusWordText(int sw);

bool isKnownCardType(int code);
bool isClimateCardType(int code);

} // namespace Codes
} // namespace HidSeos

#endif // HIDSEOS_SEOS_CODES_H
