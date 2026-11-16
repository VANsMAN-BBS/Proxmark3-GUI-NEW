#ifndef HIDSEOS_SEOS_EXPORTER_H
#define HIDSEOS_SEOS_EXPORTER_H

#include <QByteArray>

#include "seos_types.h"

/**
 * Stable JSON/CSV serialization for the HID SEOS (T-money / Climate Card)
 * read-only page.
 *
 * The JSON schema keeps the documented field names and always includes
 * card_info, balance, pass, time_calibration, raw_transactions, grouped_trips,
 * read_status, and warnings. Raw record bytes are preserved on every export.
 */
namespace HidSeos {
namespace Export {

QByteArray toJson(const ReadResult &result);

/** Raw transaction rows (SFI 3 taps, SFI 4/5 financial records, SFI 11 read events). */
QByteArray transactionsCsv(const ReadResult &result);

/** One row per grouped journey (transfer legs merged by journey counter). */
QByteArray tripsCsv(const ReadResult &result);

} // namespace Export
} // namespace HidSeos

#endif // HIDSEOS_SEOS_EXPORTER_H
