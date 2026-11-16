#ifndef HIDSEOS_SEOS_PARSER_H
#define HIDSEOS_SEOS_PARSER_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "seos_types.h"

/**
 * KS X 6924 parsing helpers.
 *
 * All functions are pure and side-effect free so they can be covered by the
 * QtTest suite without a Proxmark3 client. Unknown or truncated data keeps the
 * original bytes and reports an error instead of fabricating values.
 */
namespace HidSeos {
namespace Parser {

struct ApduResponse
{
    bool responded = false;
    quint16 sw = 0;
    QByteArray data;   // response body without SW1/SW2
    QByteArray raw;    // full response including SW1/SW2
    QString error;
};

/** Extract the `<<< ...` response line produced by `hf 14a apdu`. */
ApduResponse parseApduOutput(const QString &output);

/** Parse `hf 14a info` output. Returns false when no UID is present. */
bool parseCardInfo(const QString &output, QString *uid, QString *atqa,
                   QString *sak, QString *ats);

/** Parse the 47-byte PurseInfo inside the SELECT FCI (`6F 31 B0 2F ...`). */
bool parsePurseInfo(const QByteArray &fci, PurseInfo *purse,
                    QStringList *warnings = nullptr);

/** Parse the 27-byte SFI 7 period-pass record. */
bool parsePassRecord(const QByteArray &data, PassInfo *pass,
                     QStringList *warnings = nullptr);

/** Parse one 52-byte SFI 3 trip record. */
bool parseTripRecord(const QByteArray &data, int index, TripRecord *trip,
                     QStringList *warnings = nullptr);

/** Parse a sequence of SFI 3 responses; exact duplicates are ignored. */
QVector<TripRecord> parseTripSeries(const QVector<QByteArray> &records,
                                    QStringList *warnings = nullptr);

/** Parse one SFI 4/5 (46 bytes) or SFI 11 (27 bytes) financial record. */
bool parseFinancialRecord(const QByteArray &data, int sfi, int index,
                          FinancialRecord *record, QStringList *warnings = nullptr);

/** Decode the packed 4-byte timestamp with a given epoch. */
QDateTime decodeTimestamp(quint32 packed, const QDate &epoch);

/** Hex of the packed timestamp field at offsets 11..14. */
QString packedTimestampHex(const QByteArray &record);

/**
 * Derive the card-level epoch offset.
 *
 * The community default (1989-06-14) is never modified globally. When an SFI 7
 * pass is present and the community-decoded newest SFI 3 timestamp lies after
 * the pass end date, the difference is used as a per-card offset.
 */
TimeCalibration deriveCalibration(const PassInfo &pass,
                                  const QVector<TripRecord> &communityTrips);

/** Re-decode trip date/time with the observed epoch; raw values are kept. */
void applyCalibration(const TimeCalibration &calibration,
                      QVector<TripRecord> *trips);

/** Group taps that share a journey counter into one trip row. */
QVector<GroupedTrip> groupTrips(const QVector<TripRecord> &trips,
                                QStringList *warnings = nullptr);

} // namespace Parser
} // namespace HidSeos

#endif // HIDSEOS_SEOS_PARSER_H
