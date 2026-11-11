#include "logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

QMutex Logger::mutex;
QFile Logger::file;
QString Logger::path;
qint64 Logger::maxBytes = 2 * 1024 * 1024;
int Logger::maxFiles = 5;
bool Logger::initialized = false;

static QtMessageHandler previousHandler = nullptr;

QString Logger::levelName(Level level)
{
    switch(level)
    {
    case Debug: return QStringLiteral("DEBUG");
    case Info: return QStringLiteral("INFO ");
    case Warning: return QStringLiteral("WARN ");
    case Error: return QStringLiteral("ERROR");
    case Fatal: return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO ");
}

void Logger::init()
{
    QMutexLocker locker(&mutex);
    if(initialized)
        return;

    bool fromEnv = false;
    maxBytes = qEnvironmentVariableIntValue("PM3GUI_LOG_MAX_BYTES", &fromEnv);
    if(!fromEnv || maxBytes < 1024)
        maxBytes = 2 * 1024 * 1024;
    maxFiles = qEnvironmentVariableIntValue("PM3GUI_LOG_MAX_FILES");
    if(maxFiles < 1)
        maxFiles = 5;

    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if(base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    // Try each candidate and keep the first one we can actually open. Some
    // deployments (or sandboxed sessions) forbid writes to the user profile,
    // so never trust mkpath() alone.
    QStringList candidates;
    if(!base.isEmpty())
        candidates.append(base + QStringLiteral("/Proxmark3GUI/logs"));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(!appData.isEmpty() && !candidates.contains(appData + QStringLiteral("/logs")))
        candidates.append(appData + QStringLiteral("/logs"));
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates.append(appDir + QStringLiteral("/logs"));
    candidates.append(appDir); // last resort: next to the executable

    path.clear();
    for(int i = 0; i < candidates.size(); i++)
    {
        const QString dirPath = candidates.at(i);
        QDir().mkpath(dirPath);
        const QString candidate = QDir(dirPath).filePath(QStringLiteral("Proxmark3GUI.log"));
        file.setFileName(candidate);
        if(file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            path = candidate;
            break;
        }
    }

    initialized = true;
    if(path.isEmpty())
        return;

    previousHandler = qInstallMessageHandler(&Logger::qtMessageHandler);
}

void Logger::shutdown()
{
    QMutexLocker locker(&mutex);
    if(!initialized)
        return;
    if(file.isOpen())
    {
        file.flush();
        file.close();
    }
    if(previousHandler)
    {
        qInstallMessageHandler(previousHandler);
        previousHandler = nullptr;
    }
    initialized = false;
}

void Logger::rotateIfNeeded(qint64 incomingBytes)
{
    if(!file.isOpen())
        return;
    if(file.size() + incomingBytes <= maxBytes)
        return;

    file.close();
    const QString base = path;
    QFile::remove(base + QStringLiteral(".%1").arg(maxFiles));
    for(int i = maxFiles - 1; i >= 1; i--)
    {
        const QString from = base + QStringLiteral(".%1").arg(i);
        const QString to = base + QStringLiteral(".%1").arg(i + 1);
        if(QFile::exists(from))
        {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    if(QFile::exists(base))
    {
        QFile::remove(base + QStringLiteral(".1"));
        QFile::rename(base, base + QStringLiteral(".1"));
    }
    file.setFileName(base);
    file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Logger::log(Level level, const QString &module, const QString &message)
{
    QMutexLocker locker(&mutex);
    if(!initialized)
        return;

    QString safeModule = module;
    safeModule.remove(QRegularExpression(QStringLiteral("[\\r\\n\\t]")));
    QString safeMessage = message;
    safeMessage.replace(QStringLiteral("\r\n"), QStringLiteral(" | "));
    safeMessage.replace(QLatin1Char('\n'), QLatin1Char(' '));
    safeMessage.replace(QLatin1Char('\r'), QLatin1Char(' '));

    const QString line = QStringLiteral("%1 [%2] [%3] %4\n")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
            .arg(levelName(level))
            .arg(safeModule)
            .arg(safeMessage);
    const QByteArray bytes = line.toUtf8();
    rotateIfNeeded(bytes.size());
    if(file.isOpen())
    {
        file.write(bytes);
        if(level >= Warning)
            file.flush();
    }
}

void Logger::debug(const QString &module, const QString &message) { log(Debug, module, message); }
void Logger::info(const QString &module, const QString &message) { log(Info, module, message); }
void Logger::warning(const QString &module, const QString &message) { log(Warning, module, message); }
void Logger::error(const QString &module, const QString &message) { log(Error, module, message); }
void Logger::fatal(const QString &module, const QString &message) { log(Fatal, module, message); }

QString Logger::commandSummary(const QString &command)
{
    QStringList tokens = command.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList kept;
    const int limit = qMin(3, tokens.size());
    for(int i = 0; i < limit; i++)
    {
        QString token = tokens.at(i);
        // Do not put long hex payloads, keys, APDUs or file contents in logs.
        if(token.size() >= 16 && QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$")).match(token).hasMatch())
            token = QStringLiteral("<hex>");
        kept.append(token);
    }
    QString result = kept.join(QLatin1Char(' '));
    if(tokens.size() > limit)
        result += QStringLiteral(" ...");
    return result;
}

QString Logger::logFilePath()
{
    QMutexLocker locker(&mutex);
    return path;
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Level level = Debug;
    switch(type)
    {
    case QtDebugMsg: level = Debug; break;
    case QtInfoMsg: level = Info; break;
    case QtWarningMsg: level = Warning; break;
    case QtCriticalMsg: level = Error; break;
    case QtFatalMsg: level = Fatal; break;
    }

    QString module = QStringLiteral("Qt");
    if(context.file)
    {
        module = QFileInfo(QString::fromUtf8(context.file)).fileName();
        if(context.line > 0)
            module += QStringLiteral(":%1").arg(context.line);
    }
    Logger::log(level, module, message);

    // Keep a console/debugger fallback; GUI release builds have no console.
    fprintf(stderr, "[%s] %s\n", levelName(level).toUtf8().constData(), message.toUtf8().constData());
    fflush(stderr);

    if(type == QtFatalMsg)
        abort();
}
