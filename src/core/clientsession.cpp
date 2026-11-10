#include "clientsession.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>

ClientSession::ClientSession(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ClientInfo>("ClientInfo");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    m_handshakeTimer = new QTimer(this);
    m_handshakeTimer->setSingleShot(true);
    connect(m_handshakeTimer, &QTimer::timeout, this, &ClientSession::onHandshakeTimeout);

    m_commandTimer = new QTimer(this);
    m_commandTimer->setSingleShot(true);
    connect(m_commandTimer, &QTimer::timeout, this, &ClientSession::onCommandTimeout);

    connect(m_process, &QProcess::started, this, &ClientSession::onStarted);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ClientSession::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &ClientSession::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ClientSession::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ClientSession::onErrorOccurred);
}

ClientSession::~ClientSession()
{
    m_handshakeTimer->stop();
    m_commandTimer->stop();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1500);
    }
}

bool ClientSession::isConnected() const
{
    return m_info.connected && m_process->state() == QProcess::Running;
}

bool ClientSession::isBusy() const
{
    return m_hasActive || !m_queue.isEmpty();
}

ClientInfo ClientSession::clientInfo() const
{
    return m_info;
}

QString ClientSession::lastError() const
{
    return m_lastError;
}

QString ClientSession::clientPath() const
{
    return m_clientPath;
}

QStringList ClientSession::clientArguments() const
{
    return m_clientArgs;
}

QStringList ClientSession::ensureFlushArgument(const QStringList &args)
{
    QStringList result = args;
    bool hasFlush = false;
    for (const QString &arg : result) {
        if (arg == "-f" || arg == "--flush") {
            hasFlush = true;
            break;
        }
    }
    if (!hasFlush) {
        // The GUI consumes stdout incrementally; without -f the client may
        // buffer output and every command would look like a timeout.
        result << "-f";
    }
    return result;
}

QString ClientSession::makeToken(quint64 id)
{
    return QString("PM3GUI_%1_%2")
            .arg(id)
            .arg(QDateTime::currentMSecsSinceEpoch());
}

void ClientSession::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
    if (m_process) {
        m_process->setWorkingDirectory(dir);
    }
}

void ClientSession::setEnvironment(const QStringList &env)
{
    m_environment = env;
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    for (const QString &entry : env) {
        const int pos = entry.indexOf('=');
        if (pos > 0) {
            pe.insert(entry.left(pos), entry.mid(pos + 1));
        }
    }
    m_process->setProcessEnvironment(pe);
}

void ClientSession::startClient(const QString &path, const QStringList &args)
{
    if (path.isEmpty()) {
        failConnection(tr("The client path is empty."));
        return;
    }

    // A second Connect press while the old process exists must not create a
    // second writer on the same pipe. Stop the old process synchronously first.
    if (m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process->kill();
        m_process->waitForFinished(1500);
        m_stopping = false;
    }

    m_clientPath = path;
    m_clientArgs = ensureFlushArgument(args);
    m_lastError.clear();
    m_info = ClientInfo();
    m_connecting = true;
    m_stopping = false;
    m_handshakeBuffer.clear();
    m_utf8Carry.clear();
    m_handshakeToken = makeToken(0);
    m_queue.clear();
    m_hasActive = false;
    m_active = Request();
    m_commandTimer->stop();

    if (!m_workingDir.isEmpty()) {
        m_process->setWorkingDirectory(m_workingDir);
    }
    setEnvironment(m_environment);

    m_handshakeTimer->start(20000);
    emit connecting();
    m_process->start(path, m_clientArgs, QIODevice::ReadWrite);
}

void ClientSession::onStarted()
{
    if (!m_connecting) {
        return;
    }

    // The sentinel is a client-local command. It is queued after hw version,
    // so it can only appear after the version output has been produced.
    const QByteArray payload = QByteArray("hw version\nrem ") + m_handshakeToken.toUtf8() + "\n";
    m_process->write(payload);
    m_process->waitForBytesWritten(100);
}

void ClientSession::onHandshakeTimeout()
{
    if (m_connecting) {
        failConnection(tr("Timed out waiting for the Proxmark3 client to report its version."));
    }
}

void ClientSession::failConnection(const QString &message)
{
    m_lastError = message;
    m_connecting = false;
    m_handshakeTimer->stop();
    m_info.connected = false;
    emit stateChanged(false, message);
    emit clientError(message);

    if (m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process->kill();
        m_process->waitForFinished(1000);
        m_stopping = false;
    }
}

QString ClientSession::decodeUtf8(const QByteArray &data)
{
    m_utf8Carry.append(data);
    QString result;
    int i = 0;
    while (i < m_utf8Carry.size()) {
        const unsigned char c = static_cast<unsigned char>(m_utf8Carry.at(i));
        int length = 0;
        if (c < 0x80) {
            length = 1;
        } else if ((c & 0xE0) == 0xC0) {
            length = 2;
        } else if ((c & 0xF0) == 0xE0) {
            length = 3;
        } else if ((c & 0xF8) == 0xF0) {
            length = 4;
        } else {
            result += QChar(0xFFFD);
            ++i;
            continue;
        }

        if (i + length > m_utf8Carry.size()) {
            // Incomplete sequence: keep it for the next readyRead chunk.
            break;
        }

        bool valid = true;
        for (int j = 1; j < length; ++j) {
            if ((static_cast<unsigned char>(m_utf8Carry.at(i + j)) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            result += QChar(0xFFFD);
            ++i;
            continue;
        }

        result += QString::fromUtf8(m_utf8Carry.constData() + i, length);
        i += length;
    }
    m_utf8Carry.remove(0, i);
    return result;
}

QString ClientSession::cleanText(const QString &text)
{
    QString result = text;

    // CSI / OSC ANSI escape sequences. The client normally emits none in the
    // GUI pipe, but color/progress extensions must not reach the parser.
    static const QRegularExpression ansi(
        QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1B\\\\))"));
    result.remove(ansi);

    // Terminal progress output uses CR. A single CR is treated as a line
    // break rather than silently overwriting the previous text.
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return result;
}

QString ClientSession::stripSentinel(const QString &text, const QString &token, bool *found)
{
    if (token.isEmpty()) {
        return text;
    }
    const QString marker = QStringLiteral("remark: ") + token;
    QStringList lines = text.split(QLatin1Char('\n'));
    QStringList kept;
    bool removed = false;
    for (const QString &line : lines) {
        if (line.contains(marker)) {
            removed = true;
            continue;
        }
        kept.append(line);
    }
    if (found) {
        *found = removed;
    }
    return kept.join(QLatin1Char('\n'));
}

void ClientSession::processDecoded(const QString &text, bool fromStderr)
{
    if (text.isEmpty()) {
        return;
    }

    const QString displayToken = m_connecting ? m_handshakeToken : m_activeToken;
    const QString clean = cleanText(text);

    if (m_connecting) {
        m_handshakeBuffer += clean;
        bool sentinelFound = false;
        m_handshakeBuffer = stripSentinel(m_handshakeBuffer, m_handshakeToken, &sentinelFound);
        if (sentinelFound) {
            m_connecting = false;
            m_handshakeTimer->stop();
            parseVersionOutput(m_handshakeBuffer);
            if (!m_info.hasFirmwareVersion()) {
                failConnection(tr("The client is running, but no firmware version was reported. "
                                  "Check that the device is connected and the client is Iceman v4.23346."));
            } else {
                m_info.connected = true;
                m_lastError.clear();
                QString display = m_info.firmwareVersion;
                if (m_info.hasModel()) {
                    display += QStringLiteral(" / ") + m_info.model;
                }
                emit changeClientType(m_info.isIceman);
                emit versionUpdated(m_info);
                emit stateChanged(true, display);
            }
        }
    }

    if (m_hasActive) {
        m_active.output += clean;
        bool sentinelFound = false;
        QString withoutSentinel = stripSentinel(m_active.output, m_activeToken, &sentinelFound);
        if (sentinelFound) {
            m_active.output = withoutSentinel;
            if (!m_active.timeoutEmitted) {
                finishActive(true, false, QString());
            } else {
                // The caller has already been told about the timeout. Drain
                // the late sentinel and move on without a duplicate result.
                m_hasActive = false;
                m_active = Request();
                m_activeToken.clear();
                m_commandTimer->stop();
                emit busyChanged(false);
                processQueue();
            }
        }
    }

    QString display = clean;
    // Do not show the internal completion sentinel in the Raw output.
    if (!displayToken.isEmpty()) {
        QStringList lines = display.split(QLatin1Char('\n'));
        QStringList kept;
        const QString marker = QStringLiteral("remark: ") + displayToken;
        const QString commandMarker = QStringLiteral("rem ") + displayToken;
        for (const QString &line : lines) {
            if (!line.contains(marker) && !line.contains(commandMarker)) {
                kept.append(line);
            }
        }
        display = kept.join(QLatin1Char('\n'));
    }
    if (!display.isEmpty()) {
        emit newOutput(display);
    }
    if (fromStderr) {
        // Keep stderr as diagnostic context; it is not by itself a fatal error.
        m_lastError = clean.trimmed();
    }
}

void ClientSession::onReadyReadStdout()
{
    const QByteArray data = m_process->readAllStandardOutput();
    if (data.isEmpty()) {
        return;
    }
    emit rawOutput(data);
    processDecoded(decodeUtf8(data), false);
}

void ClientSession::onReadyReadStderr()
{
    const QByteArray data = m_process->readAllStandardError();
    if (data.isEmpty()) {
        return;
    }
    emit rawOutput(data);
    processDecoded(decodeUtf8(data), true);
}

void ClientSession::parseVersionOutput(const QString &text)
{
    m_info.rawVersionOutput = text;
    m_info.isIceman = text.contains(QStringLiteral("Iceman/")) || text.contains(QStringLiteral("[=]"));

    QRegularExpressionMatch match;

    match = QRegularExpression(QStringLiteral("Iceman/[^\\s]+")).match(text);
    if (match.hasMatch()) {
        m_info.clientVersion = match.captured(0);
    }

    match = QRegularExpression(QStringLiteral("OS\\.{2,}\\s*([^\\r\\n]+)")).match(text);
    if (match.hasMatch()) {
        m_info.firmwareVersion = match.captured(1).trimmed();
    }

    match = QRegularExpression(QStringLiteral("Bootrom\\.{2,}\\s*([^\\r\\n]+)")).match(text);
    if (match.hasMatch()) {
        m_info.bootromVersion = match.captured(1).trimmed();
    }

    // The [ Model ] section prints "Firmware.................. PM3 GENERIC".
    match = QRegularExpression(QStringLiteral("Firmware\\.{2,}\\s*([^\\r\\n]+)")).match(text);
    if (match.hasMatch()) {
        m_info.model = match.captured(1).trimmed();
    }

    match = QRegularExpression(QStringLiteral("uC:\\s*([^\\r\\n]+)")).match(text);
    if (match.hasMatch()) {
        m_info.hardware = match.captured(1).trimmed();
    }
}

void ClientSession::onFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool wasStopping = m_stopping;

    m_connecting = false;
    m_handshakeTimer->stop();
    m_commandTimer->stop();

    if (m_hasActive) {
        Request req = m_active;
        m_hasActive = false;
        m_active = Request();
        m_activeToken.clear();
        if (!req.timeoutEmitted) {
            emit commandFinished(req.id, req.output, false, false,
                                 tr("The client process exited before the command completed."));
        }
        emit busyChanged(false);
    }

    m_info.connected = false;

    if (!wasStopping) {
        QString message;
        if (status == QProcess::CrashExit) {
            message = tr("The Proxmark3 client crashed (exit code %1).").arg(exitCode);
        } else {
            message = tr("The Proxmark3 client exited (exit code %1).").arg(exitCode);
        }
        m_lastError = message;
        emit stateChanged(false, message);
        emit clientError(message);
    } else {
        emit stateChanged(false, QString());
    }
}

void ClientSession::onErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        failConnection(tr("Failed to start the client: %1").arg(m_process->errorString()));
        return;
    }

    if (error == QProcess::Crashed) {
        // finished() will also be emitted; keep the detailed error here.
        m_lastError = tr("The Proxmark3 client crashed: %1").arg(m_process->errorString());
        return;
    }

    if (m_connecting) {
        failConnection(tr("Client process error: %1").arg(m_process->errorString()));
    } else if (error == QProcess::WriteError || error == QProcess::ReadError) {
        m_lastError = tr("Client I/O error: %1").arg(m_process->errorString());
        emit clientError(m_lastError);
    }
}

quint64 ClientSession::executeCommand(const QString &cmd, int timeoutMs, bool rawOutput)
{
    const quint64 id = m_nextId++;
    if (!isConnected()) {
        // Report asynchronously so a caller that starts a local event loop
        // immediately still receives the result.
        QTimer::singleShot(0, this, [this, id]() {
            emit commandFinished(id, QString(), false, false, tr("The client is not connected."));
        });
        return id;
    }

    Request req;
    req.id = id;
    req.cmd = cmd;
    req.timeoutMs = qMax(1000, timeoutMs);
    req.rawOutput = rawOutput;
    m_queue.enqueue(req);
    processQueue();
    return id;
}

void ClientSession::processQueue()
{
    if (!isConnected() || m_hasActive || m_queue.isEmpty()) {
        return;
    }

    m_active = m_queue.dequeue();
    m_hasActive = true;
    m_active.output.clear();
    m_active.timeoutEmitted = false;
    m_activeToken = makeToken(m_active.id);

    QString cmd = m_active.cmd;
    cmd.replace(QLatin1Char('\r'), QLatin1Char(' '));
    cmd.replace(QLatin1Char('\n'), QLatin1Char(' '));

    const QByteArray payload = cmd.toUtf8() + '\n' +
            QByteArray("rem ") + m_activeToken.toUtf8() + '\n';

    m_commandTimer->start(m_active.timeoutMs);
    emit busyChanged(true);

    if (m_process->write(payload) < 0) {
        finishActive(false, false, tr("Failed to write the command to the client."));
        return;
    }
    m_process->waitForBytesWritten(100);
}

void ClientSession::finishActive(bool success, bool timedOut, const QString &error)
{
    if (!m_hasActive) {
        return;
    }

    Request req = m_active;
    const bool alreadyReported = req.timeoutEmitted;
    m_hasActive = false;
    m_active = Request();
    m_activeToken.clear();
    m_commandTimer->stop();

    if (!alreadyReported) {
        emit commandFinished(req.id, req.output, success, timedOut, error);
    }
    emit busyChanged(false);
    processQueue();
}

void ClientSession::onCommandTimeout()
{
    if (!m_hasActive || m_active.timeoutEmitted) {
        return;
    }

    m_active.timeoutEmitted = true;
    m_commandTimer->stop();
    emit commandFinished(m_active.id, m_active.output, false, true,
                         tr("The command did not complete within %1 ms. "
                            "The client is still draining; press Stop if it does not return.")
                         .arg(m_active.timeoutMs));
    // Keep m_hasActive=true so a late sentinel can drain cleanly and no next
    // command is written while the previous one may still be running.
}

void ClientSession::sendRawCommand(const QString &cmd)
{
    if (!isConnected()) {
        emit clientError(tr("Cannot send command: the client is not connected."));
        return;
    }
    QByteArray payload = cmd.toUtf8();
    if (!payload.endsWith('\n')) {
        payload.append('\n');
    }
    if (m_process->write(payload) < 0) {
        emit clientError(tr("Failed to write to the client: %1").arg(m_process->errorString()));
    }
}

void ClientSession::stopClient()
{
    m_connecting = false;
    m_handshakeTimer->stop();
    m_commandTimer->stop();

    if (m_hasActive) {
        Request req = m_active;
        m_hasActive = false;
        m_active = Request();
        m_activeToken.clear();
        if (!req.timeoutEmitted) {
            emit commandFinished(req.id, req.output, false, false, tr("The client was stopped."));
        }
        emit busyChanged(false);
    }
    m_queue.clear();

    if (m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process->write("quit\n");
        if (!m_process->waitForFinished(800)) {
            m_process->kill();
            m_process->waitForFinished(1200);
        }
        m_stopping = false;
    }

    const bool wasConnected = m_info.connected;
    m_info.connected = false;
    if (wasConnected) {
        emit stateChanged(false, QString());
    }
}

void ClientSession::restartClient()
{
    const QString path = m_clientPath;
    const QStringList args = m_clientArgs;
    stopClient();
    startClient(path, args);
}
