#include "seos_reader.h"

#include "seos_codes.h"

#include "core/clientsession.h"

#include <QDateTime>
#include <QTimer>

namespace {

const int kInfoTimeoutMs = 10000;
const int kSelectTimeoutMs = 15000;
const int kBalanceTimeoutMs = 10000;
const int kReadTimeoutMs = 10000;
const int kMaxInfoRetries = 2;
const int kMaxSelectRetries = 1;

QString statusText(quint16 sw)
{
    const QString label = HidSeos::Codes::statusWordText(sw);
    const QString hex = QStringLiteral("%1")
                            .arg(sw, 4, 16, QLatin1Char('0'))
                            .toUpper();
    if (label.isEmpty())
        return hex;
    return QStringLiteral("%1 (%2)").arg(hex, label);
}

} // namespace

namespace HidSeos {

SeosReader::SeosReader(ClientSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    if (m_session) {
        connect(m_session, &ClientSession::commandFinished,
                this, &SeosReader::onCommandFinished);
    }
}

bool SeosReader::isRunning() const
{
    return m_running;
}

void SeosReader::resetState()
{
    m_cancelled = false;
    m_partial = false;
    m_linkDown = false;
    m_step = Step::Idle;
    m_pendingId = 0;
    m_retryCount = 0;
    m_retryPending = false;
    m_uid.clear();
    m_atqa.clear();
    m_sak.clear();
    m_ats.clear();
    m_detectedAt.clear();
    m_fci.clear();
    m_balance = Parser::ApduResponse();
    m_pass = Parser::ApduResponse();
    m_tripRaw.clear();
    m_financialRaw.clear();
    m_topupRaw.clear();
    m_readEventRaw.clear();
    m_sfi13.clear();
    m_purse = PurseInfo();
    m_leRetried = false;
    m_record = 1;
    m_maxRecords = 20;
    m_warnings.clear();
}

void SeosReader::start()
{
    if (m_running)
        return;
    if (!m_session || !m_session->isConnected()) {
        emit failed(tr("Proxmark3 client is not connected."));
        return;
    }

    resetState();
    m_running = true;
    requestInfo();
}

void SeosReader::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    complete(true);
}

QString SeosReader::byteHex(int value) const
{
    return QStringLiteral("%1")
        .arg(value & 0xFF, 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString SeosReader::apduCommand(const QString &dataHex, bool select) const
{
    return QStringLiteral("hf 14a apdu %1-k -d %2")
        .arg(select ? QStringLiteral("-s ") : QString(), dataHex);
}

void SeosReader::addWarning(const QString &warning)
{
    if (!m_warnings.contains(warning))
        m_warnings.append(warning);
}

void SeosReader::markPartial(const QString &warning)
{
    m_partial = true;
    addWarning(warning);
}

void SeosReader::requestInfo()
{
    m_step = Step::Info;
    emit progress(tr("Detecting an ISO14443-A card..."), 3);
    m_pendingId = m_session->executeCommand(QStringLiteral("hf 14a info"),
                                            kInfoTimeoutMs, false);
}

void SeosReader::requestSelect()
{
    m_step = Step::Select;
    emit progress(tr("Selecting the T-money application (D4100000030001)..."), 8);
    m_pendingId = m_session->executeCommand(
        apduCommand(QStringLiteral("00A4040007D4100000030001"), true),
        kSelectTimeoutMs, false);
}

void SeosReader::requestBalance()
{
    m_step = Step::Balance;
    emit progress(tr("Reading the stored balance..."), 12);
    m_pendingId = m_session->executeCommand(apduCommand(QStringLiteral("904C000004"), false),
                                            kBalanceTimeoutMs, false);
}

void SeosReader::requestPass(int le, bool retry)
{
    m_step = Step::Pass;
    m_leRetried = retry;
    emit progress(tr("Reading the period-pass record (SFI 7)..."), 16);
    const QString command = QStringLiteral("00B08700") + byteHex(le);
    m_pendingId = m_session->executeCommand(apduCommand(command, false),
                                            kReadTimeoutMs, false);
}

void SeosReader::beginTrips()
{
    m_record = 1;
    m_maxRecords = 20;
    m_leRetried = false;
    requestTrip();
}

void SeosReader::requestTrip(bool retry, int le)
{
    m_step = Step::Trips;
    if (retry)
        m_leRetried = true;
    const int percent = 18 + qMin(32, (m_record - 1) * 32 / qMax(1, m_maxRecords));
    emit progress(tr("Reading trip records (SFI 3, %1/%2)...")
                      .arg(m_record)
                      .arg(m_maxRecords),
                  percent);

    const QString recordHex = QStringLiteral("%1").arg(m_record, 2, 16, QLatin1Char('0')).toUpper();
    const QString leHex = retry ? byteHex(le) : QStringLiteral("00");
    const QString command = QStringLiteral("00B2%1%2%3")
                                .arg(recordHex, QStringLiteral("1C"), leHex);
    m_pendingId = m_session->executeCommand(apduCommand(command, false),
                                            kReadTimeoutMs, false);
}

void SeosReader::beginFinancial()
{
    m_record = 1;
    m_maxRecords = 20;
    m_leRetried = false;
    requestFinancial();
}

void SeosReader::requestFinancial(bool retry, int le)
{
    m_step = Step::Financial;
    if (retry)
        m_leRetried = true;
    const int percent = 54 + qMin(12, (m_record - 1) * 12 / qMax(1, m_maxRecords));
    emit progress(tr("Reading financial records (SFI 4, %1/%2)...")
                      .arg(m_record)
                      .arg(m_maxRecords),
                  percent);

    const QString recordHex = QStringLiteral("%1").arg(m_record, 2, 16, QLatin1Char('0')).toUpper();
    const QString leHex = retry ? byteHex(le) : QStringLiteral("00");
    const QString command = QStringLiteral("00B2%1%2%3")
                                .arg(recordHex, QStringLiteral("24"), leHex);
    m_pendingId = m_session->executeCommand(apduCommand(command, false),
                                            kReadTimeoutMs, false);
}

void SeosReader::beginTopups()
{
    m_record = 1;
    m_maxRecords = 20;
    m_leRetried = false;
    requestTopup();
}

void SeosReader::requestTopup(bool retry, int le)
{
    m_step = Step::Topups;
    if (retry)
        m_leRetried = true;
    emit progress(tr("Reading top-up records (SFI 5, %1/%2)...")
                      .arg(m_record)
                      .arg(m_maxRecords),
                  68);

    const QString recordHex = QStringLiteral("%1").arg(m_record, 2, 16, QLatin1Char('0')).toUpper();
    const QString leHex = retry ? byteHex(le) : QStringLiteral("00");
    const QString command = QStringLiteral("00B2%1%2%3")
                                .arg(recordHex, QStringLiteral("2C"), leHex);
    m_pendingId = m_session->executeCommand(apduCommand(command, false),
                                            kReadTimeoutMs, false);
}

void SeosReader::beginReadEvents()
{
    m_record = 1;
    m_maxRecords = 2;
    m_leRetried = false;
    requestReadEvent();
}

void SeosReader::requestReadEvent(bool retry, int le)
{
    m_step = Step::ReadEvents;
    if (retry)
        m_leRetried = true;
    emit progress(tr("Reading card-read events (SFI 11, %1/%2)...")
                      .arg(m_record)
                      .arg(m_maxRecords),
                  78);

    const QString recordHex = QStringLiteral("%1").arg(m_record, 2, 16, QLatin1Char('0')).toUpper();
    const QString leHex = retry ? byteHex(le) : QStringLiteral("00");
    const QString command = QStringLiteral("00B2%1%2%3")
                                .arg(recordHex, QStringLiteral("5C"), leHex);
    m_pendingId = m_session->executeCommand(apduCommand(command, false),
                                            kReadTimeoutMs, false);
}

void SeosReader::requestAux(int le, bool retry)
{
    m_step = Step::Aux;
    m_leRetried = retry;
    emit progress(tr("Reading the auxiliary file (SFI 13)..."), 92);
    const QString command = QStringLiteral("00B08D00") + byteHex(le);
    m_pendingId = m_session->executeCommand(apduCommand(command, false),
                                            kReadTimeoutMs, false);
}

void SeosReader::retryInfoOrFail(const QString &reason)
{
    if (m_retryCount < kMaxInfoRetries) {
        ++m_retryCount;
        m_retryPending = true;
        emit progress(tr("Card not ready (%1). Retrying %2/%3...")
                          .arg(reason)
                          .arg(m_retryCount)
                          .arg(kMaxInfoRetries),
                      0);
        QTimer::singleShot(300, this, [this]()
        {
            m_retryPending = false;
            if (!m_running)
                return;
            if (m_cancelled) {
                complete(true);
                return;
            }
            requestInfo();
        });
        return;
    }
    fail(tr("No ISO14443-A card detected."));
}

void SeosReader::retrySelectOrFail(const QString &reason)
{
    if (m_retryCount < kMaxSelectRetries) {
        ++m_retryCount;
        m_retryPending = true;
        emit progress(tr("Card did not answer SELECT (%1). Retrying...").arg(reason), 5);
        QTimer::singleShot(300, this, [this]()
        {
            m_retryPending = false;
            if (!m_running)
                return;
            if (m_cancelled) {
                complete(true);
                return;
            }
            requestSelect();
        });
        return;
    }
    fail(tr("The card did not respond to SELECT AID."));
}

void SeosReader::fail(const QString &message)
{
    if (!m_running)
        return;
    m_running = false;
    m_step = Step::Done;
    emit failed(message);
}

void SeosReader::onCommandFinished(quint64 id, const QString &output, bool /*success*/,
                                   bool timedOut, const QString &error)
{
    if (!m_running || id != m_pendingId || m_retryPending)
        return;

    if (m_cancelled) {
        complete(true);
        return;
    }

    if (m_step == Step::Info) {
        QString uid;
        QString atqa;
        QString sak;
        QString ats;
        if (!timedOut && Parser::parseCardInfo(output, &uid, &atqa, &sak, &ats)) {
            m_uid = uid;
            m_atqa = atqa;
            m_sak = sak;
            m_ats = ats;
            m_detectedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
            m_retryCount = 0;
            emit progress(tr("Card detected: %1").arg(m_uid), 6);
            requestSelect();
            return;
        }
        if (timedOut) {
            fail(tr("No ISO14443-A card detected (the detection command timed out)."));
            return;
        }
        retryInfoOrFail(error.isEmpty() ? tr("no UID in the client output") : error);
        return;
    }

    if (m_step == Step::Select) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            if (timedOut) {
                fail(tr("The card did not respond to SELECT AID."));
                return;
            }
            retrySelectOrFail(error.isEmpty() ? tr("no APDU response") : error);
            return;
        }
        if (response.sw != 0x9000) {
            fail(tr("This is not a T-money / KS X 6924 card (SELECT AID returned %1).")
                     .arg(statusText(response.sw)));
            return;
        }
        if (!Parser::parsePurseInfo(response.data, &m_purse, nullptr)) {
            fail(tr("The SELECT response does not contain a valid 47-byte PurseInfo."));
            return;
        }
        m_fci = response.data;
        m_retryCount = 0;
        requestBalance();
        return;
    }

    if (m_step == Step::Balance) {
        m_balance = Parser::parseApduOutput(output);
        if (!m_balance.responded) {
            markPartial(tr("GET BALANCE did not return a response."));
        } else if (m_balance.sw != 0x9000 && m_balance.sw != 0x6D00) {
            markPartial(tr("GET BALANCE returned %1.").arg(statusText(m_balance.sw)));
        }
        requestPass();
        return;
    }

    if (m_step == Step::Pass) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            markPartial(tr("SFI 7 did not return a response."));
            afterPass();
            return;
        }
        if ((response.sw & 0xFF00) == 0x6C00 && !m_leRetried) {
            requestPass(response.sw & 0xFF, true);
            return;
        }
        if (response.sw == 0x9000) {
            m_pass = response;
        } else if (response.sw != 0x6A82 && response.sw != 0x6A83
                   && m_purse.cardTypeCode == 0xB5) {
            markPartial(tr("SFI 7 returned %1; no period pass is shown.")
                            .arg(statusText(response.sw)));
        }
        afterPass();
        return;
    }

    if (m_step == Step::Trips) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            m_linkDown = true;
            markPartial(tr("The card stopped responding during the SFI 3 read."));
            complete(false);
            return;
        }
        if (response.sw == 0x9000) {
            m_tripRaw.append(response.data);
            m_leRetried = false;
            ++m_record;
            if (m_record > m_maxRecords) {
                afterTrips();
                return;
            }
            requestTrip();
            return;
        }
        if ((response.sw & 0xFF00) == 0x6C00 && !m_leRetried) {
            requestTrip(true, response.sw & 0xFF);
            return;
        }
        if (response.sw == 0x6A82 || response.sw == 0x6A83) {
            afterTrips();
            return;
        }
        if (response.sw == 0x6981) {
            markPartial(tr("SFI 3 is not a record file (%1).").arg(statusText(response.sw)));
            afterTrips();
            return;
        }
        markPartial(tr("SFI 3 record %1 returned %2.")
                        .arg(m_record)
                        .arg(statusText(response.sw)));
        afterTrips();
        return;
    }

    if (m_step == Step::Financial) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            m_linkDown = true;
            markPartial(tr("The card stopped responding during the SFI 4 read."));
            complete(false);
            return;
        }
        if (response.sw == 0x9000) {
            m_financialRaw.append(response.data);
            m_leRetried = false;
            ++m_record;
            if (m_record > m_maxRecords) {
                afterFinancial();
                return;
            }
            requestFinancial();
            return;
        }
        if ((response.sw & 0xFF00) == 0x6C00 && !m_leRetried) {
            requestFinancial(true, response.sw & 0xFF);
            return;
        }
        if (response.sw == 0x6A82 || response.sw == 0x6A83 || response.sw == 0x6981) {
            afterFinancial();
            return;
        }
        markPartial(tr("SFI 4 returned %1; the series was stopped.")
                        .arg(statusText(response.sw)));
        afterFinancial();
        return;
    }

    if (m_step == Step::Topups) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            m_linkDown = true;
            markPartial(tr("The card stopped responding during the SFI 5 read."));
            complete(false);
            return;
        }
        if (response.sw == 0x9000) {
            m_topupRaw.append(response.data);
            m_leRetried = false;
            ++m_record;
            if (m_record > m_maxRecords) {
                afterTopups();
                return;
            }
            requestTopup();
            return;
        }
        if ((response.sw & 0xFF00) == 0x6C00 && !m_leRetried) {
            requestTopup(true, response.sw & 0xFF);
            return;
        }
        if (response.sw != 0x6A82 && response.sw != 0x6A83 && response.sw != 0x6981) {
            markPartial(tr("SFI 5 returned %1; the series was stopped.")
                            .arg(statusText(response.sw)));
        }
        afterTopups();
        return;
    }

    if (m_step == Step::ReadEvents) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            m_linkDown = true;
            markPartial(tr("The card stopped responding during the SFI 11 read."));
            complete(false);
            return;
        }
        if (response.sw == 0x9000) {
            m_readEventRaw.append(response.data);
            m_leRetried = false;
            ++m_record;
            if (m_record > m_maxRecords) {
                afterReadEvents();
                return;
            }
            requestReadEvent();
            return;
        }
        if ((response.sw & 0xFF00) == 0x6C00 && !m_leRetried) {
            requestReadEvent(true, response.sw & 0xFF);
            return;
        }
        if (response.sw != 0x6A82 && response.sw != 0x6A83 && response.sw != 0x6981) {
            markPartial(tr("SFI 11 returned %1; the series was stopped.")
                            .arg(statusText(response.sw)));
        }
        afterReadEvents();
        return;
    }

    if (m_step == Step::Aux) {
        const Parser::ApduResponse response = Parser::parseApduOutput(output);
        if (!response.responded) {
            markPartial(tr("SFI 13 did not return a response."));
        } else if ((response.sw & 0xFF00) == 0x6C00 && !m_leRetried) {
            requestAux(response.sw & 0xFF, true);
            return;
        } else if (response.sw == 0x9000) {
            m_sfi13 = response.data;
        } else if (response.sw != 0x6A82 && response.sw != 0x6A83) {
            markPartial(tr("SFI 13 returned %1.").arg(statusText(response.sw)));
        }
        complete(false);
    }
}

void SeosReader::afterPass()
{
    beginTrips();
}

void SeosReader::afterTrips()
{
    beginFinancial();
}

void SeosReader::afterFinancial()
{
    beginTopups();
}

void SeosReader::afterTopups()
{
    if (m_purse.cardTypeCode == 0xB5)
        beginReadEvents();
    else
        complete(false);
}

void SeosReader::afterReadEvents()
{
    requestAux();
}

void SeosReader::complete(bool cancelled)
{
    if (!m_running)
        return;

    m_running = false;
    m_step = Step::Done;

    SeosReadSnapshot snapshot;
    snapshot.uid = m_uid;
    snapshot.atqa = m_atqa;
    snapshot.sak = m_sak;
    snapshot.ats = m_ats;
    snapshot.detectedAt = m_detectedAt;
    snapshot.fci = m_fci;
    snapshot.balance = m_balance;
    snapshot.pass = m_pass;
    snapshot.trips = m_tripRaw;
    snapshot.financial = m_financialRaw;
    snapshot.topups = m_topupRaw;
    snapshot.readEvents = m_readEventRaw;
    snapshot.sfi13 = m_sfi13;
    snapshot.warnings = m_warnings;
    snapshot.partial = cancelled || m_partial || m_linkDown;
    snapshot.cancelled = cancelled;

    const ReadResult result = buildResult(snapshot);
    emit progress(cancelled ? tr("Read cancelled.") : tr("Read complete."), 100);
    emit finished(result);
}

ReadResult SeosReader::buildResult(const SeosReadSnapshot &snapshot)
{
    ReadResult result;
    result.uid = snapshot.uid;
    result.atqa = snapshot.atqa;
    result.sak = snapshot.sak;
    result.ats = snapshot.ats;
    result.detectedAt = snapshot.detectedAt;
    result.iso14443a = !snapshot.uid.isEmpty();
    result.selectOk = !snapshot.fci.isEmpty();
    result.cancelled = snapshot.cancelled;
    result.partial = snapshot.partial || snapshot.cancelled;

    QStringList warnings = snapshot.warnings;

    if (!snapshot.fci.isEmpty()) {
        if (!Parser::parsePurseInfo(snapshot.fci, &result.purse, &warnings)) {
            warnings.append(tr("The SELECT response does not contain a valid 47-byte PurseInfo."));
        }
    }

    if (snapshot.pass.responded && snapshot.pass.sw == 0x9000) {
        if (!Parser::parsePassRecord(snapshot.pass.data, &result.pass, &warnings))
            warnings.append(tr("The SFI 7 record could not be parsed as a 27-byte period pass."));
    }

    if (result.purse.cardTypeCode == 0xB5 && !result.pass.present) {
        warnings.append(tr("Card type 0xB5 was read, but the SFI 7 pass is missing or invalid; "
                           "the card is shown as unknown instead of Climate Card."));
    }

    QVector<TripRecord> trips = Parser::parseTripSeries(snapshot.trips, &warnings);
    result.calibration = Parser::deriveCalibration(result.pass, trips);
    Parser::applyCalibration(result.calibration, &trips);
    result.trips = trips;
    result.groupedTrips = Parser::groupTrips(trips, &warnings);

    for (int i = 0; i < snapshot.financial.size(); ++i) {
        FinancialRecord record;
        Parser::parseFinancialRecord(snapshot.financial.at(i), 4, i + 1, &record, &warnings);
        result.financial.append(record);
    }
    for (int i = 0; i < snapshot.topups.size(); ++i) {
        FinancialRecord record;
        Parser::parseFinancialRecord(snapshot.topups.at(i), 5, i + 1, &record, &warnings);
        result.financial.append(record);
    }
    for (int i = 0; i < snapshot.readEvents.size(); ++i) {
        FinancialRecord record;
        Parser::parseFinancialRecord(snapshot.readEvents.at(i), 11, i + 1, &record, &warnings);
        result.readEvents.append(record);
    }
    result.sfi13 = snapshot.sfi13;

    if (!result.purse.valid) {
        result.kind = CardKind::Unknown;
    } else if (result.purse.cardTypeCode == 0xB5) {
        result.kind = result.pass.present ? CardKind::Climate : CardKind::Unknown;
    } else if (Codes::isKnownCardType(result.purse.cardTypeCode)) {
        result.kind = CardKind::TMoney;
    } else {
        result.kind = CardKind::Unknown;
        const QString code = QStringLiteral("0x%1")
                                 .arg(result.purse.cardTypeCode, 2, 16, QLatin1Char('0'))
                                 .toUpper()
                                 .replace(QStringLiteral("0X"), QStringLiteral("0x"));
        warnings.append(tr("Unknown card type %1; decoded values are shown without an "
                           "interpretation.").arg(code));
    }

    result.balance.sw = snapshot.balance.responded ? snapshot.balance.sw : 0;
    if (result.kind == CardKind::Climate) {
        result.balance.supported = false;
        result.balance.known = false;
        if (!snapshot.balance.responded) {
            result.balance.reason = tr("GET BALANCE did not return a response.");
        } else if (snapshot.balance.sw == 0x6D00) {
            result.balance.reason =
                tr("Climate Card does not support GET BALANCE (90 4C returns 6D00).");
        } else {
            result.balance.reason =
                tr("Balance is not available for this Climate Card (status %1).")
                    .arg(statusText(snapshot.balance.sw));
        }
    } else if (snapshot.balance.responded && snapshot.balance.sw == 0x9000
               && snapshot.balance.data.size() >= 4) {
        result.balance.supported = true;
        result.balance.known = true;
        result.balance.amountKrw =
            (static_cast<qint64>(static_cast<quint8>(snapshot.balance.data.at(0))) << 24)
            | (static_cast<qint64>(static_cast<quint8>(snapshot.balance.data.at(1))) << 16)
            | (static_cast<qint64>(static_cast<quint8>(snapshot.balance.data.at(2))) << 8)
            | static_cast<quint8>(snapshot.balance.data.at(3));
        result.balance.raw = snapshot.balance.data;
    } else if (snapshot.balance.responded && snapshot.balance.sw == 0x6D00) {
        result.balance.supported = false;
        result.balance.known = false;
        result.balance.reason =
            tr("The card does not support GET BALANCE (90 4C returns 6D00).");
    } else if (!snapshot.balance.responded) {
        result.balance.supported = false;
        result.balance.known = false;
        result.balance.reason = tr("GET BALANCE did not return a response.");
    } else {
        result.balance.supported = false;
        result.balance.known = false;
        result.balance.reason =
            tr("GET BALANCE returned %1.").arg(statusText(snapshot.balance.sw));
    }

    for (int i = 0; i < snapshot.trips.size(); ++i) {
        TripRecord parsed;
        const bool valid = Parser::parseTripRecord(snapshot.trips.at(i), i + 1, &parsed, nullptr);
        RawRecord raw;
        raw.sfi = 3;
        raw.index = i + 1;
        raw.data = snapshot.trips.at(i);
        raw.parsed = valid;
        raw.note = Codes::sfiName(3);
        result.rawRecords.append(raw);
    }
    for (const FinancialRecord &record : result.financial) {
        RawRecord raw;
        raw.sfi = record.sfi;
        raw.index = record.index;
        raw.data = record.raw;
        raw.parsed = record.valid;
        raw.note = Codes::sfiName(record.sfi);
        result.rawRecords.append(raw);
    }
    for (const FinancialRecord &record : result.readEvents) {
        RawRecord raw;
        raw.sfi = 11;
        raw.index = record.index;
        raw.data = record.raw;
        raw.parsed = record.valid;
        raw.note = Codes::sfiName(11);
        result.rawRecords.append(raw);
    }
    if (result.pass.present) {
        RawRecord raw;
        raw.sfi = 7;
        raw.index = 0;
        raw.data = result.pass.raw;
        raw.parsed = true;
        raw.note = Codes::sfiName(7);
        result.rawRecords.append(raw);
    }
    if (!result.sfi13.isEmpty()) {
        RawRecord raw;
        raw.sfi = 13;
        raw.index = 0;
        raw.data = result.sfi13;
        raw.parsed = true;
        raw.note = Codes::sfiName(13);
        result.rawRecords.append(raw);
    }

    result.warnings = warnings;
    return result;
}

} // namespace HidSeos
