#include "felica_reader.h"

#include "cjrc_profile.h"
#include "code_tables.h"
#include "felica_chunks.h"

#include "core/clientsession.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

namespace {

const int kReadTimeoutMs = 15000;
const int kInfoTimeoutMs = 10000;
const int kPollTimeoutMs = 10000;
const int kMaxRetries = 2;
const int kMaxBlocksPerRead = 15;

QString nibbleHex(int value)
{
    return QStringLiteral("%1").arg(value & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace


namespace {

/** Fill missing rail times from the matching gate record (same date, fare and
 *  exit station). The UI marks such values as derived. */
void enrichRailTimesFromGates(Transit::ReadResult &result)
{
    for (Transit::TransactionRecord &transaction : result.transactions)
    {
        if (transaction.timeKnown || transaction.kind != Transit::TransactionKind::Rail
            || !transaction.deltaKnown || transaction.delta >= 0)
        {
            continue;
        }

        for (const Transit::GateRecord &gate : result.gates)
        {
            if (!gate.timeKnown || gate.date != transaction.date)
                continue;
            if (gate.amount != -transaction.delta)
                continue;
            if (!gate.station.primaryName.isEmpty()
                && gate.station.primaryName != transaction.exit.primaryName)
            {
                continue;
            }

            transaction.time = gate.time;
            transaction.timeKnown = true;
            transaction.timeSource = QStringLiteral("gate");
            break;
        }
    }
}

} // namespace

FelicaReader::FelicaReader(ClientSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    if (m_session)
    {
        connect(m_session, &ClientSession::commandFinished,
                this, &FelicaReader::onCommandFinished);
    }
}

bool FelicaReader::isRunning() const
{
    return m_running;
}

void FelicaReader::resetState()
{
    m_cancelled = false;
    m_retryPending = false;
    m_step = Step::Idle;
    m_pendingId = 0;
    m_retryCount = 0;
    m_card = Transit::CardInfo();
    m_tasks.clear();
    m_currentTask = ReadTask();
    m_serviceBlocks.clear();
    m_serviceErrors.clear();
    m_statuses.clear();
    m_totalTasks = 0;
    m_doneTasks = 0;
}

void FelicaReader::start()
{
    if (m_running)
        return;
    if (!m_session || !m_session->isConnected())
    {
        emit failed(tr("Proxmark3 client is not connected."));
        return;
    }

    resetState();
    m_running = true;
    poll();
}

void FelicaReader::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    m_tasks.clear();
    complete(true);
}

void FelicaReader::poll()
{
    m_step = Step::Poll;
    m_retryCount = 0;
    emit progress(tr("Polling for a FeliCa card..."), 3);
    m_pendingId = m_session->executeCommand(QStringLiteral("hf felica reader"),
                                            kPollTimeoutMs, false);
}

void FelicaReader::requestInfo()
{
    m_step = Step::Info;
    m_retryCount = 0;
    emit progress(tr("Reading card information..."), 8);
    m_pendingId = m_session->executeCommand(QStringLiteral("hf felica info"),
                                            kInfoTimeoutMs, false);
}

void FelicaReader::enqueueTasks()
{
    CjrcProfile profile;
    const QVector<Transit::ServiceDefinition> services = profile.services();

    m_statuses.clear();
    for (const Transit::ServiceDefinition &service : services)
    {
        Transit::ServiceReadStatus status;
        status.code = service.code;
        status.name = service.name;
        status.blocksExpected = service.declaredBlocks;
        m_statuses.append(status);

        const QVector<QPair<int, int> > plan = FelicaChunks::plan(service.declaredBlocks);
        for (const QPair<int, int> &chunk : plan)
        {
            ReadTask task;
            task.service = service.code;
            task.firstBlock = chunk.first;
            task.count = chunk.second;
            m_tasks.enqueue(task);
        }
    }

    m_totalTasks = m_tasks.size();
    m_doneTasks = 0;

    if (m_tasks.isEmpty())
    {
        complete(false);
        return;
    }
    nextTask();
}

void FelicaReader::nextTask()
{
    if (m_cancelled)
    {
        complete(true);
        return;
    }
    if (m_tasks.isEmpty())
    {
        complete(false);
        return;
    }

    m_currentTask = m_tasks.dequeue();
    m_retryCount = 0;
    sendReadTask();
}

void FelicaReader::sendReadTask()
{
    m_step = Step::ReadService;
    const int percent = 12 + (m_totalTasks > 0 ? (m_doneTasks * 70) / m_totalTasks : 0);
    emit progress(tr("Reading service 0x%1, blocks %2-%3...")
                      .arg(m_currentTask.service, 4, 16, QLatin1Char('0'))
                      .arg(m_currentTask.firstBlock)
                      .arg(m_currentTask.firstBlock + m_currentTask.count - 1)
                      .toUpper()
                      .replace(QStringLiteral("0X"), QStringLiteral("0x")),
                  percent);

    const QString command = QStringLiteral("hf felica raw -s -c %1")
                                .arg(buildReadFrame(m_currentTask.service,
                                                    m_currentTask.firstBlock,
                                                    m_currentTask.count));
    m_pendingId = m_session->executeCommand(command, kReadTimeoutMs, false);
}

QString FelicaReader::buildReadFrame(int service, int firstBlock, int count) const
{
    const QByteArray frame = FelicaChunks::readFrame(m_card.cardId, service, firstBlock, count);
    return QString::fromLatin1(frame.toHex().toUpper());
}

bool FelicaReader::parseIdm(const QString &output, QString *idm)
{
    static const QRegularExpression re(QStringLiteral("IDm[:.]*\\s*([0-9A-Fa-f]{16})"));
    const QRegularExpressionMatch match = re.match(output);
    if (!match.hasMatch())
        return false;
    *idm = match.captured(1).toUpper();
    return true;
}

void FelicaReader::parseInfo(const QString &output, Transit::CardInfo *card)
{
    static const QRegularExpression pmmRe(QStringLiteral("PMM[. ]*\\s*([0-9A-Fa-f]{16})"));
    static const QRegularExpression icRe(QStringLiteral("IC code[. ]*\\s*([0-9A-Fa-f]{4})\\s*\\(([^)]*)\\)"));
    static const QRegularExpression mfgRe(QStringLiteral("Mfg date[. ]*\\s*([0-9]{4}-[0-9]{2}-[0-9]{2})"));
    static const QRegularExpression sysRe(QStringLiteral("System Code[. ]*\\s*([0-9A-Fa-f]{4})"));

    const QRegularExpressionMatch pmm = pmmRe.match(output);
    if (pmm.hasMatch())
        card->pmm = pmm.captured(1).toUpper();

    const QRegularExpressionMatch ic = icRe.match(output);
    if (ic.hasMatch())
    {
        card->icCode = ic.captured(1).toUpper();
        card->icName = ic.captured(2).trimmed();
    }

    const QRegularExpressionMatch mfg = mfgRe.match(output);
    if (mfg.hasMatch())
        card->manufactureDate = mfg.captured(1);

    card->systems.clear();
    QRegularExpressionMatchIterator it = sysRe.globalMatch(output);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const QString system = match.captured(1).toUpper();
        if (!card->systems.contains(system))
            card->systems.append(system);
    }
}

bool FelicaReader::parseRawResponse(const QString &output, int expectedBlocks,
                                    QVector<QByteArray> *blocks, QString *error)
{
    blocks->clear();

    static const QRegularExpression startRe(QStringLiteral("\\bB2\\s+4D\\s+([0-9A-Fa-f]{2})"));
    const QRegularExpressionMatch start = startRe.match(output);
    if (!start.hasMatch())
    {
        if (output.contains(QStringLiteral("FeliCa command failed"))
            || output.contains(QStringLiteral("No response")))

        {
            *error = tr("Card did not respond.");
        }
        else
        {
            *error = tr("Unexpected response from the client.");
        }
        return false;
    }

    const int length = start.captured(1).toInt(nullptr, 16);
    const int total = 2 + 1 + length;

    const QString tail = output.mid(start.capturedStart());
    static const QRegularExpression byteRe(QStringLiteral("([0-9A-Fa-f]{2})"));
    QVector<int> bytes;
    QRegularExpressionMatchIterator it = byteRe.globalMatch(tail);
    while (it.hasNext() && bytes.size() < total)
    {
        const QRegularExpressionMatch match = it.next();
        bytes.append(match.captured(1).toInt(nullptr, 16));
    }

    if (bytes.size() < total || total < 15)
    {
        *error = tr("Truncated response from the client.");
        return false;
    }

    if (bytes.at(0) != 0xB2 || bytes.at(1) != 0x4D || bytes.at(3) != 0x07)
    {
        *error = tr("Unexpected FeliCa frame.");
        return false;
    }

    const int status1 = bytes.at(12);
    const int status2 = bytes.at(13);
    const int count = bytes.at(14);
    if (status1 != 0 || status2 != 0)
    {
        *error = tr("Card rejected the read (status %1 %2).")
                     .arg(nibbleHex(status1), nibbleHex(status2));
        return false;
    }
    if (count != expectedBlocks)
    {
        *error = tr("Card returned %1 blocks, expected %2.")
                     .arg(count)
                     .arg(expectedBlocks);
        return false;
    }
    if (bytes.size() < 15 + expectedBlocks * 16)
    {
        *error = tr("Incomplete block data.");
        return false;
    }

    for (int i = 0; i < expectedBlocks; ++i)
    {
        QByteArray data;
        data.reserve(16);
        const int offset = 15 + i * 16;
        for (int j = 0; j < 16; ++j)
            data.append(static_cast<char>(bytes.at(offset + j)));
        blocks->append(data);
    }
    return true;
}

void FelicaReader::retryOrFail(const QString &error)
{
    if (m_retryCount < kMaxRetries)
    {
        ++m_retryCount;
        m_retryPending = true;
        emit progress(tr("Read failed (%1). Retrying %2/%3...")
                          .arg(error)
                          .arg(m_retryCount)
                          .arg(kMaxRetries),
                      0);
        QTimer::singleShot(300, this, [this]()
        {
            m_retryPending = false;
            if (m_cancelled)
            {
                complete(true);
                return;
            }
            if (m_step == Step::Poll)
                poll();
            else if (m_step == Step::Info)
                requestInfo();
            else if (m_step == Step::ReadService)
                sendReadTask();
        });
        return;
    }

    if (m_step == Step::Poll)
    {
        m_running = false;
        emit failed(tr("No FeliCa card detected."));
        return;
    }

    if (m_step == Step::Info)
    {
        // Card metadata is optional; continue with the IDm.
        m_doneTasks = 0;
        enqueueTasks();
        return;
    }

    m_serviceErrors[m_currentTask.service] = error;
    for (Transit::ServiceReadStatus &status : m_statuses)
    {
        if (status.code == m_currentTask.service)
        {
            status.attempted = true;
            status.success = false;
            status.error = error;
            break;
        }
    }
    ++m_doneTasks;
    nextTask();
}

void FelicaReader::onCommandFinished(quint64 id, const QString &output, bool success,
                                     bool timedOut, const QString &error)
{
    if (!m_running || id != m_pendingId || m_retryPending)
        return;

    if (m_cancelled)
    {
        complete(true);
        return;
    }

    if (timedOut)
    {
        retryOrFail(tr("command timed out"));
        return;
    }

    if (m_step == Step::Poll)
    {
        QString idm;
        if (!success || !parseIdm(output, &idm))
        {
            retryOrFail(error.isEmpty() ? tr("no IDm in the client output") : error);
            return;
        }

        m_card = Transit::CardInfo();
        m_card.protocol = QStringLiteral("FeliCa");
        m_card.cardId = idm;
        m_card.detectedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        CjrcProfile profile;
        m_card.profileId = profile.profileId();
        m_card.issuer = profile.displayName();
        emit cardDetected(m_card);
        requestInfo();
        return;
    }

    if (m_step == Step::Info)
    {
        Transit::CardInfo card = m_card;
        if (success)
            parseInfo(output, &card);

        CjrcProfile profile;
        if (!card.systems.isEmpty() && !profile.matches(card))
        {
            m_running = false;
            emit failed(tr("This card is not a CJRC transit card (system 0x0003 was not found)."));
            return;
        }

        card.cardType = card.icName.isEmpty() ? tr("FeliCa Standard") : card.icName;
        card.profileId = profile.profileId();
        card.issuer = profile.displayName();
        m_card = card;
        emit cardDetected(m_card);
        m_doneTasks = 0;
        enqueueTasks();
        return;
    }

    if (m_step == Step::ReadService)
    {
        QVector<QByteArray> blocks;
        QString parseError;
        if (!success || !parseRawResponse(output, m_currentTask.count, &blocks, &parseError))
        {
            retryOrFail(parseError.isEmpty() ? (error.isEmpty() ? tr("read failed") : error)
                                             : parseError);
            return;
        }

        QVector<QByteArray> &serviceData = m_serviceBlocks[m_currentTask.service];
        serviceData.append(blocks);

        for (Transit::ServiceReadStatus &status : m_statuses)
        {
            if (status.code == m_currentTask.service)
            {
                status.attempted = true;
                status.blocksRead = serviceData.size();
                status.success = true;
                status.error.clear();
                break;
            }
        }

        ++m_doneTasks;
        nextTask();
    }
}

void FelicaReader::complete(bool cancelled)
{
    if (!m_running)
        return;

    m_running = false;
    m_step = Step::Done;

    QVector<Transit::RawBlock> rawBlocks;
    for (auto it = m_serviceBlocks.constBegin(); it != m_serviceBlocks.constEnd(); ++it)
    {
        const int service = it.key();
        const QVector<QByteArray> &data = it.value();
        for (int i = 0; i < data.size(); ++i)
        {
            Transit::RawBlock block;
            block.service = service;
            block.block = i;
            block.data = data.at(i);
            rawBlocks.append(block);
        }
    }

    CjrcProfile profile;
    Transit::ReadResult result;
    result.card = m_card;
    result.cancelled = cancelled;
    result.services = m_statuses;

    QStringList warnings;
    if (!rawBlocks.isEmpty())
        profile.parse(rawBlocks, result, warnings);
    enrichRailTimesFromGates(result);
    result.warnings = warnings;

    bool anyFailure = cancelled;
    for (const Transit::ServiceReadStatus &status : result.services)
    {
        if (status.attempted && !status.success)
            anyFailure = true;
    }
    result.partial = anyFailure;

    if (result.card.profileId.isEmpty())
        result.card.profileId = profile.profileId();

    emit progress(cancelled ? tr("Read cancelled.") : tr("Read complete."), 100);
    emit finished(result);
}
