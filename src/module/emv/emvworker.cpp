#include "emvworker.h"

#include "emvruntime.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTimer>

namespace {

QString eventString(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

} // namespace

EmvWorker::EmvWorker(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<Emv::Txn>("Emv::Txn");
    qRegisterMetaType<Emv::ReadResult>("Emv::ReadResult");

    m_cancelTimer = new QTimer(this);
    m_cancelTimer->setSingleShot(true);
    connect(m_cancelTimer, &QTimer::timeout, this, &EmvWorker::onCancelGrace);

    m_deadlineTimer = new QTimer(this);
    m_deadlineTimer->setSingleShot(true);
    connect(m_deadlineTimer, &QTimer::timeout, this, &EmvWorker::onDeadline);
}

EmvWorker::~EmvWorker()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    delete m_process;
    m_process = nullptr;
}

void EmvWorker::setPort(const QString &port)
{
    m_port = port;
}

void EmvWorker::setClientPath(const QString &clientPath)
{
    m_clientPath = clientPath;
}

void EmvWorker::setMockMode(bool mock)
{
    m_mock = mock;
}

void EmvWorker::setTimeout(double seconds)
{
    m_timeout = seconds;
}

void EmvWorker::setIdleAfterCard(double seconds)
{
    m_idleAfterCard = seconds;
}

void EmvWorker::setNoCardTimeout(double seconds)
{
    m_noCardTimeout = seconds;
}

void EmvWorker::setEnvironmentOverrides(const QStringList &values)
{
    m_environment = values;
}

bool EmvWorker::isProcessRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

qint64 EmvWorker::processId() const
{
    return m_process ? m_process->processId() : 0;
}

void EmvWorker::start()
{
    if (m_process || m_finished)
        return;

    m_result = Emv::ReadResult();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_stderrLines.clear();
    m_finalEvent = false;
    m_finished = false;
    m_cancelRequested = false;
    m_timeoutRequested = false;

    QString error;
    const QString directory = EmvRuntime::scriptDirectory(&error);
    if (directory.isEmpty())
    {
        finishWithFallback(QStringLiteral("error"), error);
        return;
    }

    QStringList prefixArgs;
    const QString python = EmvRuntime::pythonExecutable(&prefixArgs, &error);
    if (python.isEmpty())
    {
        finishWithFallback(QStringLiteral("error"), error);
        return;
    }

    const QString script = EmvRuntime::workerScript(directory);
    if (!QFileInfo::exists(script))
    {
        finishWithFallback(QStringLiteral("error"),
                           tr("The EMV worker script is missing: %1").arg(script));
        return;
    }

    QStringList args = prefixArgs;
    args << QStringLiteral("-W") << QStringLiteral("ignore") << QStringLiteral("-u")
         << script
         << QStringLiteral("--timeout") << QString::number(m_timeout, 'f', 1)
         << QStringLiteral("--idle-after-card") << QString::number(m_idleAfterCard, 'f', 1)
         << QStringLiteral("--no-card-timeout") << QString::number(m_noCardTimeout, 'f', 1);
    if (!m_port.isEmpty())
        args << QStringLiteral("--port") << m_port;
    if (m_mock)
    {
        args << QStringLiteral("--mock");
    }
    else if (!m_clientPath.isEmpty() && QFileInfo::exists(m_clientPath))
    {
        args << QStringLiteral("--exe") << QFileInfo(m_clientPath).absoluteFilePath();
    }

    // The proxmark3 client writes its session logs to CWD\.proxmark3. Run the
    // bridge from the client's directory so those logs never land in the GUI
    // working directory (the Python reader also pins the PM3 child to its exe
    // directory).
    QString workdir = directory;
    if (!m_clientPath.isEmpty())
    {
        const QFileInfo clientInfo(m_clientPath);
        if (clientInfo.exists() && clientInfo.isFile())
            workdir = clientInfo.absolutePath();
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    for (const QString &entry : m_environment)
    {
        const int split = entry.indexOf(QLatin1Char('='));
        if (split > 0)
            environment.insert(entry.left(split), entry.mid(split + 1));
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setWorkingDirectory(workdir);
    m_process->setProcessEnvironment(environment);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &EmvWorker::onStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &EmvWorker::onStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &EmvWorker::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &EmvWorker::onProcessError);

    emit statusChanged(tr("Starting EMV reader…"));
    m_process->start(python, args);
    if (!m_process->waitForStarted(6000))
    {
        finishWithFallback(QStringLiteral("error"),
                           tr("Cannot start the EMV reader: %1").arg(m_process->errorString()));
        return;
    }

    const int hardMs = static_cast<int>((m_timeout + 20.0) * 1000.0);
    m_deadlineTimer->start(hardMs);
}

void EmvWorker::cancel()
{
    if (m_finished)
        return;
    m_cancelRequested = true;
    if (!m_process)
    {
        finishWithFallback(QStringLiteral("cancelled"));
        return;
    }
    if (m_process->state() == QProcess::NotRunning)
        return;

    // Ask the Python bridge to call EmvLogReader.cancel(); terminate only if
    // the process does not exit within the grace period.
    m_process->write("cancel\n");
    m_process->waitForBytesWritten(200);
    m_cancelTimer->start(1500);
}

void EmvWorker::onStdout()
{
    if (!m_process)
        return;
    m_stdoutBuffer += m_process->readAllStandardOutput();

    int newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0)
    {
        QByteArray line = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        handleLine(line);
    }
}

void EmvWorker::onStderr()
{
    if (!m_process)
        return;
    m_stderrBuffer += m_process->readAllStandardError();
    int newline = -1;
    while ((newline = m_stderrBuffer.indexOf('\n')) >= 0)
    {
        QByteArray line = m_stderrBuffer.left(newline);
        m_stderrBuffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        const QString text = QString::fromUtf8(line).trimmed();
        if (!text.isEmpty())
            m_stderrLines.append(text);
    }
}

void EmvWorker::handleLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;

    if (!trimmed.startsWith('{'))
    {
        // Non-JSON diagnostics from the bridge. Keep them for the details view
        // instead of treating them as transactions.
        m_stderrLines.append(QString::fromUtf8(trimmed));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_stderrLines.append(QString::fromUtf8(trimmed));
        return;
    }
    handleEvent(document.object());
}

void EmvWorker::handleEvent(const QJsonObject &event)
{
    const QString type = eventString(event, "event");

    if (type == QLatin1String("started"))
    {
        const QString port = eventString(event, "port");
        if (!port.isEmpty())
            m_port = port;
        emit started(m_port);
        emit statusChanged(tr("Reader started."));
        return;
    }

    if (type == QLatin1String("record"))
    {
        const QJsonObject txn = event.value(QStringLiteral("txn")).toObject();
        Emv::Txn record;
        record.date = txn.value(QStringLiteral("date")).toString();
        record.time = txn.value(QStringLiteral("time")).toString();
        record.merchant = txn.value(QStringLiteral("merchant")).toString();
        record.amount = txn.value(QStringLiteral("amount")).toString();
        record.recordNo = txn.value(QStringLiteral("record_no")).toInt(0);
        record.raw = txn.value(QStringLiteral("raw")).toString();
        if (record.date.isEmpty() || record.amount.isEmpty())
            return;
        m_result.txns.append(record);
        emit recordReady(record);
        return;
    }

    if (type == QLatin1String("status"))
    {
        const QString message = eventString(event, "message");
        if (!message.isEmpty())
            emit statusChanged(message);
        return;
    }

    if (type == QLatin1String("warning"))
    {
        const QString message = eventString(event, "message");
        if (!message.isEmpty() && !m_result.warnings.contains(message))
        {
            m_result.warnings.append(message);
            emit warningAdded(message);
        }
        return;
    }

    if (type == QLatin1String("fatal"))
    {
        m_result.stopReason = QStringLiteral("error");
        const QString message = eventString(event, "message");
        if (!message.isEmpty())
        {
            m_result.warnings.append(message);
            emit warningAdded(message);
        }
        m_finalEvent = true;
        return;
    }

    if (type == QLatin1String("result"))
    {
        QString reason = eventString(event, "stop_reason");
        if (!Emv::isKnownStopReason(reason))
        {
            const QString bad = reason;
            reason = QStringLiteral("error");
            const QString warning = tr("The reader reported an unknown stop reason (%1).").arg(bad);
            m_result.warnings.append(warning);
            emit warningAdded(warning);
        }
        m_result.stopReason = reason;
        m_result.cardDetected = event.value(QStringLiteral("card_detected")).toBool(false);
        if (event.contains(QStringLiteral("returncode")))
            m_result.returnCode = event.value(QStringLiteral("returncode")).toInt(-1);

        const QJsonArray warnings = event.value(QStringLiteral("warnings")).toArray();
        for (const QJsonValue &value : warnings)
        {
            const QString warning = value.toString();
            if (!warning.isEmpty() && !m_result.warnings.contains(warning))
            {
                m_result.warnings.append(warning);
                emit warningAdded(warning);
            }
        }
        m_finalEvent = true;
    }
}

void EmvWorker::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_finished)
        return;

    onStdout();
    onStderr();
    if (!m_stdoutBuffer.trimmed().isEmpty())
    {
        handleLine(m_stdoutBuffer);
        m_stdoutBuffer.clear();
    }
    if (!m_stderrBuffer.trimmed().isEmpty())
    {
        m_stderrLines.append(QString::fromUtf8(m_stderrBuffer).trimmed());
        m_stderrBuffer.clear();
    }

    m_cancelTimer->stop();
    m_deadlineTimer->stop();

    if (m_result.returnCode < 0)
        m_result.returnCode = exitCode;

    if (!m_finalEvent)
    {
        if (m_cancelRequested)
        {
            finishWithFallback(QStringLiteral("cancelled"));
        }
        else if (m_timeoutRequested)
        {
            finishWithFallback(QStringLiteral("timeout"),
                               tr("The EMV reader timed out."));
        }
        else if (status == QProcess::CrashExit)
        {
            finishWithFallback(QStringLiteral("error"),
                               tr("The EMV reader process crashed."));
        }
        else
        {
            finishWithFallback(
                QStringLiteral("error"),
                tr("The EMV reader exited without a result (exit code %1).").arg(exitCode));
        }
        return;
    }

    if (m_result.stopReason == QLatin1String("error") && !m_stderrLines.isEmpty())
    {
        const QString detail = m_stderrLines.join(QLatin1Char('\n'));
        if (!detail.isEmpty() && !m_result.warnings.contains(detail))
        {
            m_result.warnings.append(detail);
            emit warningAdded(detail);
        }
    }

    emitFinished();
}

void EmvWorker::onProcessError(QProcess::ProcessError error)
{
    if (m_finished)
        return;
    if (error == QProcess::FailedToStart)
    {
        finishWithFallback(QStringLiteral("error"),
                           tr("Cannot start the EMV reader: %1")
                               .arg(m_process ? m_process->errorString() : QString()));
    }
}

void EmvWorker::onCancelGrace()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_process->terminate();
    m_process->waitForFinished(800);
    if (m_process && m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void EmvWorker::onDeadline()
{
    if (m_finished)
        return;
    m_timeoutRequested = true;
    stopProcess(0);
}

void EmvWorker::stopProcess(int terminateAfterMs)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    if (terminateAfterMs <= 0)
    {
        m_process->kill();
        return;
    }
    m_cancelTimer->start(terminateAfterMs);
}

void EmvWorker::finishWithFallback(const QString &reason, const QString &warning)
{
    if (m_finished)
        return;
    if (!warning.isEmpty() && !m_result.warnings.contains(warning))
    {
        m_result.warnings.append(warning);
        emit warningAdded(warning);
    }
    m_result.stopReason = Emv::isKnownStopReason(reason) ? reason : QStringLiteral("error");
    emitFinished();
}

void EmvWorker::emitFinished()
{
    if (m_finished)
        return;
    m_finished = true;
    m_cancelTimer->stop();
    m_deadlineTimer->stop();
    if (m_process)
    {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    emit finished(m_result);
}
