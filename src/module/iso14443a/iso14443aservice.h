#ifndef ISO14443A_SERVICE_H
#define ISO14443A_SERVICE_H

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QStringList>

class QTimer;

/**
 * Persistent Qt front-end for the Python ISO 14443A parser sidecar.
 *
 * One QProcess is started lazily and reused for every request.  stdout is
 * parsed incrementally: a JSON object may arrive split across several
 * readyRead() calls, and several objects may arrive in one call.  Requests
 * carry a monotonically increasing id and a per-request timeout.  A crashed
 * process fails its pending requests and is restarted automatically (with a
 * small crash-loop guard).
 *
 * No serial port is opened here: the sidecar only reads trace/capture files.
 */
class Iso14443aService : public QObject
{
    Q_OBJECT
public:
    explicit Iso14443aService(QObject *parent = nullptr);
    ~Iso14443aService() override;

    bool isRunning() const;
    bool isReady() const;
    qint64 processId() const;
    QString lastError() const;

    /** Start the sidecar if needed (non-blocking). */
    void ensureStarted();

    /** Requests. Each returns the request id used in the signals. */
    quint64 initialize();
    quint64 parseTrace(const QString &path);
    quint64 parseCapture(const QString &path);
    quint64 parseCaptureLines(const QStringList &lines);
    quint64 parseCaptureText(const QString &text);
    quint64 reset();

    /** Best-effort graceful shutdown; safe to call from a destructor. */
    void shutdown();

signals:
    void ready();
    void requestStarted(quint64 requestId, const QString &action);
    void requestFinished(quint64 requestId, const QString &action, const QJsonObject &data);
    void requestFailed(quint64 requestId, const QString &action,
                       const QString &code, const QString &message);
    void logLine(const QString &line);
    void processRestarted();
    void busyChanged(bool busy);

private slots:
    void onStdout();
    void onStderr();
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void handleTimeout(quint64 requestId);

private:
    struct Queued
    {
        quint64 id = 0;
        QString action;
        QByteArray line;
        int timeoutMs = 30000;
    };

    struct Pending
    {
        QString action;
        QTimer *timer = nullptr;
    };

    void startProcess();
    quint64 sendRequest(const QString &action, const QJsonObject &payload,
                        int timeoutMs);
    void writeWhenReady(const Queued &request);
    void flushQueue();
    void handleLine(const QByteArray &line);
    void failAll(const QString &code, const QString &message);
    void failRequest(quint64 id, const QString &code, const QString &message);
    void stopProcess(int gracefulMs);
    void scheduleRestart();
    void setBusy(bool busy);

    QProcess *m_process = nullptr;
    bool m_starting = false;
    bool m_stopping = false;
    bool m_ready = false;
    bool m_busy = false;
    bool m_fatal = false;
    QString m_lastError;

    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QStringList m_stderrLines;

    quint64 m_nextId = 1;
    QQueue<Queued> m_queue;
    QHash<quint64, Pending> m_pending;

    QElapsedTimer m_crashWindow;
    int m_crashCount = 0;
};

#endif // ISO14443A_SERVICE_H
