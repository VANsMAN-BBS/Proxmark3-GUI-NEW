#ifndef EMV_EMVTYPES_H
#define EMV_EMVTYPES_H

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Emv {

/** One transaction record decoded by the Python pm3_emv_log parser. */
struct Txn
{
    QString date;      // "2026-06-15"
    QString time;      // "09:32:41"
    QString merchant;  // 9F4E text, "-" when empty
    QString amount;    // decimal string, e.g. "12.50"
    int recordNo = 0;  // READ RECORD P1
    QString raw;       // record body hex (diagnostics only)
};

/** Outcome of one reader run. */
struct ReadResult
{
    QVector<Txn> txns;
    bool cardDetected = false;
    /**
     * One of the documented pm3_emv_log stop reasons:
     * "done", "no_card", "cancelled", "timeout", "error".
     * Empty until the reader reports a result.
     */
    QString stopReason;
    int returnCode = -1;
    QStringList warnings;
};

/** The only stop_reason values documented by emv_log_reader/README.md §5.2. */
inline bool isKnownStopReason(const QString &reason)
{
    return reason == QLatin1String("done")
        || reason == QLatin1String("no_card")
        || reason == QLatin1String("cancelled")
        || reason == QLatin1String("timeout")
        || reason == QLatin1String("error");
}

} // namespace Emv

Q_DECLARE_METATYPE(Emv::Txn)
Q_DECLARE_METATYPE(Emv::ReadResult)

#endif // EMV_EMVTYPES_H
