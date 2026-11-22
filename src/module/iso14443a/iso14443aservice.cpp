#include "iso14443aservice.h"

#include "iso14443aruntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

namespace {

const int kMaxStdoutLine = 4 * 1024 * 1024;
const int kInitializeTimeoutMs = 15000;
const int kParseTimeoutMs = 60000;
const int kResetTimeoutMs = 10000;

} // namespace

Iso14443aService::Iso14443aService(QObject *parent)
    : QObject(parent)
{
    m_crashWindow.start();
}

Iso14443aService::~Iso14443aService()
{
    shutdown();
}

bool Iso14443aService::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool Iso14443aService::isReady() const
{
    return m_ready;
}

qint64 Iso14443aService::processId() const
{
    return m_process ? m_process->processId() : 0;
}

QString Iso14443aService::lastError() const
{
    return m_lastError;
}

void Iso14443aService::ensureStarted()
{
    if (m_fatal)
        return;
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;
    if (m_starting)
        return;
    startProcess();
}

void Iso14443aService::startProcess()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;
    if (m_process)
    {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_ready = false;
    m_lastError.clear();

    QString error;
    const QString directory = Iso14443aRuntime::scriptDirectory(&error);
    if (directory.isEmpty())
    {
        m_fatal = true;
        m_lastError = error;
        emit logLine(error);
        failAll(QStringLiteral("runtime_error"), error);
        return;
    }

    QStringList prefixArgs;
    const QString python = Iso14443aRuntime::pythonExecutable(&prefixArgs, &error);
    if (python.isEmpty())
    {
        m_fatal = true;
        m_lastError = error;
        emit logLine(error);
        failAll(QStringLiteral("python_not_found"), error);
        return;
    }

    const QString script = Iso14443aRuntime::workerScript(directory);
    if (!QFileInfo::exists(script))
    {
        m_fatal = true;
        m_lastError = tr("The ISO 14443A sidecar script is missing: %1").arg(script);
        emit logLine(m_lastError);
        failAll(QStringLiteral("runtime_error"), m_lastError);
        return;
    }

    QStringList args = prefixArgs;
    args << QStringLiteral("-W") << QStringLiteral("ignore") << QStringLiteral("-u")
         << script;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setWorkingDirectory(directory);
    m_process->setProcessEnvironment(environment);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &Iso14443aService::onStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &Iso14443aService::onStderr);
    connect(m_process, &QProcess::started,
            this, &Iso14443aService::onProcessStarted);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Iso14443aService::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &Iso14443aService::onProcessError);

    m_starting = true;
    emit logLine(tr("Starting ISO 14443A parser sidecar…"));
    m_process->start(python, args);
}

void Iso14443aService::onProcessStarted()
{
    m_starting = false;
    m_ready = false;
    emit logLine(tr("ISO 14443A parser sidecar started."));
    flushQueue();
}

quint64 Iso14443aService::sendRequest(const QString &action,
                                      const QJsonObject &payload,
                                      int timeoutMs)
{
    Queued request;
    request.id = m_nextId++;
    request.action = action;
    request.timeoutMs = timeoutMs;

    QJsonObject root = payload;
    root.insert(QStringLiteral("request_id"), static_cast<double>(request.id));
    root.insert(QStringLiteral("action"), action);
    request.line = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';

    m_queue.enqueue(request);
    emit requestStarted(request.id, action);
    ensureStarted();
    flushQueue();
    return request.id;
}

void Iso14443aService::flushQueue()
{
    if (!m_process || m_process->state() != QProcess::Running || m_starting)
        return;

    while (!m_queue.isEmpty())
    {
        const Queued request = m_queue.dequeue();

        QTimer *timer = new QTimer(this);
        timer->setSingleShot(true);
        const quint64 id = request.id;
        connect(timer, &QTimer::timeout, this, [this, id]()
        {
            handleTimeout(id);
        });

        Pending pending;
        pending.action = request.action;
        pending.timer = timer;
        m_pending.insert(request.id, pending);

        m_process->write(request.line);
        timer->start(request.timeoutMs);
        setBusy(true);
    }
}

void Iso14443aService::onStdout()
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

    if (m_stdoutBuffer.size() > kMaxStdoutLine)
    {
        m_stdoutBuffer.clear();
        const QString message = tr("The ISO 14443A sidecar produced an oversized line.");
        emit logLine(message);
        failAll(QStringLiteral("line_too_long"), message);
        stopProcess(0);
    }
}

void Iso14443aService::onStderr()
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
        {
            m_stderrLines.append(text);
            if (m_stderrLines.size() > 200)
                m_stderrLines.removeFirst();
            emit logLine(text);
        }
    }
}

void Iso14443aService::handleLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;

    if (!trimmed.startsWith('{'))
    {
        emit logLine(QString::fromUtf8(trimmed));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        emit logLine(QString::fromUtf8(trimmed));
        return;
    }

    const QJsonObject root = document.object();
    const quint64 id = static_cast<quint64>(root.value(QStringLiteral("request_id"))
                                                .toDouble(-1));
    if (id == 0 || !m_pending.contains(id))
    {
        emit logLine(tr("Ignored sidecar response with an unknown request id."));
        return;
    }

    const Pending pending = m_pending.take(id);
    if (pending.timer)
    {
        pending.timer->stop();
        pending.timer->deleteLater();
    }

    const QString status = root.value(QStringLiteral("status")).toString();
    const QString message = root.value(QStringLiteral("message")).toString();
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();

    if (status == QLatin1String("ok"))
    {
        if (pending.action == QLatin1String("initialize"))
        {
            m_ready = true;
            m_crashCount = 0;
            emit ready();
        }
        emit requestFinished(id, pending.action, data);
    }
    else
    {
        QString code = data.value(QStringLiteral("code")).toString();
        if (code.isEmpty())
            code = QStringLiteral("sidecar_error");
        m_lastError = message;
        emit requestFailed(id, pending.action, code, message);
    }

    setBusy(!m_pending.isEmpty());
}

void Iso14443aService::handleTimeout(quint64 requestId)
{
    if (!m_pending.contains(requestId))
        return;

    const Pending pending = m_pending.take(requestId);
    if (pending.timer)
    {
        pending.timer->stop();
        pending.timer->deleteLater();
    }

    const QString message = tr("The ISO 14443A parser request timed out.");
    m_lastError = message;
    emit logLine(message);
    emit requestFailed(requestId, pending.action, QStringLiteral("timeout"), message);
    setBusy(!m_pending.isEmpty());

    // A stuck parse cannot be cancelled safely at the protocol level; restart
    // the one persistent process so the next request starts clean.
    if (pending.action.startsWith(QLatin1String("parse")))
        stopProcess(0);
}

void Iso14443aService::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_process)
    {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_starting = false;
    m_ready = false;

    if (m_stopping)
    {
        failAll(QStringLiteral("shutdown"), tr("The ISO 14443A sidecar was stopped."));
        return;
    }

    if (m_fatal)
    {
        failAll(QStringLiteral("sidecar_error"), m_lastError);
        return;
    }

    const QString message = status == QProcess::CrashExit
        ? tr("The ISO 14443A sidecar crashed.")
        : tr("The ISO 14443A sidecar exited unexpectedly (code %1).").arg(exitCode);
    m_lastError = message;
    emit logLine(message);
    failAll(QStringLiteral("sidecar_crashed"), message);
    scheduleRestart();
}

void Iso14443aService::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart)
    {
        m_starting = false;
        m_fatal = true;
        const QString message = tr("Cannot start the ISO 14443A sidecar: %1")
            .arg(m_process ? m_process->errorString() : QString());
        m_lastError = message;
        emit logLine(message);
        failAll(QStringLiteral("python_not_found"), message);
    }
    else if (error != QProcess::UnknownError)
    {
        emit logLine(tr("ISO 14443A sidecar process error: %1")
                         .arg(m_process ? m_process->errorString() : QString()));
    }
}

void Iso14443aService::scheduleRestart()
{
    if (!m_crashWindow.isValid() || m_crashWindow.elapsed() > 60000)
    {
        m_crashWindow.restart();
        m_crashCount = 0;
    }
    ++m_crashCount;
    if (m_crashCount > 3)
    {
        m_fatal = true;
        const QString message = tr("The ISO 14443A sidecar keeps crashing; automatic restart was disabled.");
        m_lastError = message;
        emit logLine(message);
        return;
    }

    emit logLine(tr("Restarting the ISO 14443A parser sidecar…"));
    QTimer::singleShot(250, this, [this]()
    {
        if (m_fatal || m_stopping)
            return;
        // A request may have already started the replacement process.
        if (m_starting || (m_process && m_process->state() != QProcess::NotRunning))
            return;
        startProcess();
        emit processRestarted();
    });
}

void Iso14443aService::failRequest(quint64 id, const QString &code,
                                   const QString &message)
{
    if (!m_pending.contains(id))
        return;
    const Pending pending = m_pending.take(id);
    if (pending.timer)
    {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
    emit requestFailed(id, pending.action, code, message);
    setBusy(!m_pending.isEmpty());
}

void Iso14443aService::failAll(const QString &code, const QString &message)
{
    const QList<quint64> ids = m_pending.keys();
    for (quint64 id : ids)
        failRequest(id, code, message);

    while (!m_queue.isEmpty())
    {
        const Queued request = m_queue.dequeue();
        emit requestFailed(request.id, request.action, code, message);
    }
    setBusy(false);
}

void Iso14443aService::stopProcess(int gracefulMs)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    if (gracefulMs > 0)
    {
        m_process->terminate();
        if (m_process->waitForFinished(gracefulMs))
            return;
    }
    m_process->kill();
}

void Iso14443aService::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

quint64 Iso14443aService::initialize()
{
    return sendRequest(QStringLiteral("initialize"), QJsonObject(),
                       kInitializeTimeoutMs);
}

quint64 Iso14443aService::parseTrace(const QString &path)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("path"), path);
    return sendRequest(QStringLiteral("parse_trace"), payload, kParseTimeoutMs);
}

quint64 Iso14443aService::parseCapture(const QString &path)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("path"), path);
    return sendRequest(QStringLiteral("parse_capture"), payload, kParseTimeoutMs);
}

quint64 Iso14443aService::parseCaptureLines(const QStringList &lines)
{
    QJsonArray array;
    for (const QString &line : lines)
        array.append(line);
    QJsonObject payload;
    payload.insert(QStringLiteral("lines"), array);
    return sendRequest(QStringLiteral("parse_capture"), payload, kParseTimeoutMs);
}

quint64 Iso14443aService::parseCaptureText(const QString &text)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    return sendRequest(QStringLiteral("parse_capture"), payload, kParseTimeoutMs);
}

quint64 Iso14443aService::reset()
{
    return sendRequest(QStringLiteral("reset"), QJsonObject(), kResetTimeoutMs);
}

void Iso14443aService::shutdown()
{
    m_stopping = true;
    m_queue.clear();
    failAll(QStringLiteral("shutdown"), tr("The ISO 14443A sidecar was stopped."));

    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        m_process->write("{\"request_id\":0,\"action\":\"shutdown\"}\n");
        m_process->waitForBytesWritten(150);
        if (!m_process->waitForFinished(900))
        {
            m_process->terminate();
            if (!m_process->waitForFinished(300))
            {
                m_process->kill();
                m_process->waitForFinished(300);
            }
        }
    }

    if (m_process)
    {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_starting = false;
    m_ready = false;
    m_stopping = false;
}
