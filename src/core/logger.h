#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QMutex>
#include <QFile>
#include <QtGlobal>

/**
 * Lightweight, thread-safe file logger.
 *
 * Location: %LOCALAPPDATA%/Proxmark3GUI/logs/Proxmark3GUI.log
 * Rotation: size based (default 2 MiB, 5 files).
 *
 * Sensitive values (MRZ, BAC keys, APDU payloads, full Amiibo files) must not
 * be passed to this logger. ClientSession only logs a short command summary.
 */
class Logger
{
public:
    enum Level
    {
        Debug = 0,
        Info,
        Warning,
        Error,
        Fatal
    };

    static void init();
    static void shutdown();

    static void log(Level level, const QString &module, const QString &message);
    static void debug(const QString &module, const QString &message);
    static void info(const QString &module, const QString &message);
    static void warning(const QString &module, const QString &message);
    static void error(const QString &module, const QString &message);
    static void fatal(const QString &module, const QString &message);

    /** Short command summary: first tokens only, no keys/APDU payload. */
    static QString commandSummary(const QString &command);
    static QString logFilePath();

private:
    static void rotateIfNeeded(qint64 incomingBytes);
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context,
                                 const QString &message);
    static QString levelName(Level level);

    static QMutex mutex;
    static QFile file;
    static QString path;
    static qint64 maxBytes;
    static int maxFiles;
    static bool initialized;
};

#endif // LOGGER_H
