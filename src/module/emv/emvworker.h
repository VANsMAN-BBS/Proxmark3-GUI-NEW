#ifndef EMV_EMVWORKER_H
#define EMV_EMVWORKER_H

#include <QObject>
#include <QProcess>
#include <QStringList>

#include "emvtypes.h"

class QTimer;
class QJsonObject;

/**
 * Worker-thread owner of the Python EMV reader subprocess.
 *
 * The C++ side never parses APDUs. It runs emv_worker.py (a thin JSON-lines
 * bridge around the unchanged pm3_emv_log.EmvLogReader) with `-W ignore`,
 * streams stdout/stderr incrementally and turns JSON events into Qt signals.
 *
 * Serial exclusivity is the caller's job: MainWindow stops ClientSession before
 * calling start() and restarts it after finished(). cancel() asks the Python
 * reader to stop and terminates the process if it does not exit in time.
 */
class EmvWorker : public QObject
{
    Q_OBJECT
public:
    explicit EmvWorker(QObject *parent = nullptr);
    ~EmvWorker() override;

    void setPort(const QString &port);
    void setClientPath(const QString &clientPath);
    void setMockMode(bool mock);
    void setTimeout(double seconds);
    void setIdleAfterCard(double seconds);
    void setNoCardTimeout(double seconds);
    /** Extra environment entries in KEY=VALUE form (used by tests). */
    void setEnvironmentOverrides(const QStringList &values);

    bool isProcessRunning() const;
    qint64 processId() const;

public slots:
    void start();
    void cancel();

signals:
    void started(const QString &port);
    void statusChanged(const QString &message);
    void recordReady(const Emv::Txn &txn);
    void warningAdded(const QString &warning);
    void finished(const Emv::ReadResult &result);

private slots:
    void onStdout();
    void onStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onCancelGrace();
    void onDeadline();

private:
    void handleLine(const QByteArray &line);
    void handleEvent(const QJsonObject &event);
    void finishWithFallback(const QString &reason, const QString &warning = QString());
    void emitFinished();
    void stopProcess(int terminateAfterMs);
    QString readAllRemaining(QProcess *process);

    QString m_port;
    QString m_clientPath;
    bool m_mock = false;
    double m_timeout = 45.0;
    double m_idleAfterCard = 3.0;
    double m_noCardTimeout = 12.0;
    QStringList m_environment;

    QProcess *m_process = nullptr;
    QTimer *m_cancelTimer = nullptr;
    QTimer *m_deadlineTimer = nullptr;

    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QStringList m_stderrLines;

    Emv::ReadResult m_result;
    bool m_finalEvent = false;
    bool m_finished = false;
    bool m_cancelRequested = false;
    bool m_timeoutRequested = false;
};

#endif // EMV_EMVWORKER_H
