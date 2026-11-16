#ifndef HIDSEOS_SEOS_READER_H
#define HIDSEOS_SEOS_READER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "seos_parser.h"
#include "seos_types.h"

class ClientSession;

namespace HidSeos {

/**
 * Raw pieces collected by the reader, kept separate from the assembly step so
 * the model can be built in unit tests without a Proxmark3 client.
 */
struct SeosReadSnapshot
{
    QString uid;
    QString atqa;
    QString sak;
    QString ats;
    QString detectedAt;
    QByteArray fci;
    Parser::ApduResponse balance;
    Parser::ApduResponse pass;
    QVector<QByteArray> trips;
    QVector<QByteArray> financial;
    QVector<QByteArray> topups;
    QVector<QByteArray> readEvents;
    QByteArray sfi13;
    QStringList warnings;
    bool partial = false;
    bool cancelled = false;
};

/**
 * Read-only KS X 6924 reader for ISO14443-A / ISO-DEP T-money and Climate
 * Cards.
 *
 * The reader reuses the existing ClientSession FIFO command queue (no second
 * client process, no serial layer) and only issues ISO 7816-4 read commands:
 *
 *   hf 14a info
 *   hf 14a apdu -s -k -d 00A4040007D4100000030001   SELECT AID
 *   hf 14a apdu -k -d 904C000004                     GET BALANCE
 *   hf 14a apdu -k -d 00B087001B                     SFI 7 period pass
 *   hf 14a apdu -k -d 00B2<rec>1C00                  SFI 3 trip records
 *   hf 14a apdu -k -d 00B2<rec>2400                  SFI 4 records
 *   hf 14a apdu -k -d 00B2<rec>2C00                  SFI 5 top-ups
 *   hf 14a apdu -k -d 00B2<rec>5C00                  SFI 11 read events
 *   hf 14a apdu -k -d 00B08D0010                     SFI 13 auxiliary file
 *
 * No write, authentication, key, or MIFARE Classic command is generated.
 */
class SeosReader : public QObject
{
    Q_OBJECT
public:
    explicit SeosReader(ClientSession *session, QObject *parent = nullptr);

    bool isRunning() const;
    void start();
    void cancel();

    /** Assemble a ReadResult from raw APDU bodies and a status word. */
    static HidSeos::ReadResult buildResult(const SeosReadSnapshot &snapshot);

signals:
    void progress(const QString &message, int percent);
    void finished(const HidSeos::ReadResult &result);
    void failed(const QString &message);

private slots:
    void onCommandFinished(quint64 id, const QString &output, bool success,
                           bool timedOut, const QString &error);

private:
    enum class Step
    {
        Idle,
        Info,
        Select,
        Balance,
        Pass,
        Trips,
        Financial,
        Topups,
        ReadEvents,
        Aux,
        Done,
    };

    void resetState();
    void requestInfo();
    void requestSelect();
    void requestBalance();
    void requestPass(int le = 0x1B, bool retry = false);
    void beginTrips();
    void requestTrip(bool retry = false, int le = 0);
    void beginFinancial();
    void requestFinancial(bool retry = false, int le = 0);
    void beginTopups();
    void requestTopup(bool retry = false, int le = 0);
    void beginReadEvents();
    void requestReadEvent(bool retry = false, int le = 0);
    void requestAux(int le = 0x10, bool retry = false);
    void afterPass();
    void afterTrips();
    void afterFinancial();
    void afterTopups();
    void afterReadEvents();
    void retryInfoOrFail(const QString &reason);
    void retrySelectOrFail(const QString &reason);
    void fail(const QString &message);
    void complete(bool cancelled);
    void addWarning(const QString &warning);
    void markPartial(const QString &warning);

    QString apduCommand(const QString &dataHex, bool select) const;
    QString byteHex(int value) const;

    ClientSession *m_session = nullptr;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_partial = false;
    bool m_linkDown = false;
    Step m_step = Step::Idle;
    quint64 m_pendingId = 0;
    int m_retryCount = 0;
    bool m_retryPending = false;

    QString m_uid;
    QString m_atqa;
    QString m_sak;
    QString m_ats;
    QString m_detectedAt;
    QByteArray m_fci;

    HidSeos::Parser::ApduResponse m_balance;
    HidSeos::Parser::ApduResponse m_pass;

    QVector<QByteArray> m_tripRaw;
    QVector<QByteArray> m_financialRaw;
    QVector<QByteArray> m_topupRaw;
    QVector<QByteArray> m_readEventRaw;
    QByteArray m_sfi13;
    HidSeos::PurseInfo m_purse;
    bool m_leRetried = false;

    int m_record = 1;
    int m_maxRecords = 20;
    QStringList m_warnings;
};

} // namespace HidSeos

#endif // HIDSEOS_SEOS_READER_H
