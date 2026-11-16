#ifndef FELICA_READER_H
#define FELICA_READER_H

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QVector>

#include "transit_types.h"

class ClientSession;

/**
 * Asynchronous, read-only FeliCa transit reader.
 *
 * The reader uses the existing ClientSession queue (no second client process)
 * and performs:
 *   1. `hf felica reader`  -> IDm
 *   2. `hf felica info`    -> card / system metadata
 *   3. `hf felica raw -s -c ...` in chunks of at most 15 blocks
 *
 * Read Without Encryption rejects requests with more than 15 blocks
 * (status FF A2), so 0x090F is read as 15 + 5 blocks.
 */
class FelicaReader : public QObject
{
    Q_OBJECT
public:
    explicit FelicaReader(ClientSession *session, QObject *parent = nullptr);

    bool isRunning() const;
    void start();

    void cancel();

signals:
    void progress(const QString &message, int percent);
    void cardDetected(const Transit::CardInfo &card);
    void finished(const Transit::ReadResult &result);
    void failed(const QString &message);

private slots:
    void onCommandFinished(quint64 id, const QString &output, bool success,
                           bool timedOut, const QString &error);

private:
    enum class Step
    {
        Idle,
        Poll,
        Info,
        ReadService,
        Done,
    };

    struct ReadTask
    {
        int service = 0;
        int firstBlock = 0;
        int count = 0;
    };

    void poll();
    void requestInfo();
    void enqueueTasks();
    void enqueueOctopusTask();
    void nextTask();
    void sendReadTask();
    void retryOrFail(const QString &error);
    void complete(bool cancelled);
    void resetState();

    QString buildReadFrame(int service, int firstBlock, int count) const;
    static bool parseIdm(const QString &output, QString *idm);
    static void parseInfo(const QString &output, Transit::CardInfo *card);
    static bool parseRawResponse(const QString &output, int expectedBlocks,
                                 QVector<QByteArray> *blocks, QString *error);

    ClientSession *m_session = nullptr;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_octopus = false;
    bool m_retryPending = false;
    Step m_step = Step::Idle;
    quint64 m_pendingId = 0;
    int m_retryCount = 0;

    Transit::CardInfo m_card;

    QQueue<ReadTask> m_tasks;
    ReadTask m_currentTask;
    QMap<int, QVector<QByteArray> > m_serviceBlocks;
    QMap<int, QString> m_serviceErrors;
    QVector<Transit::ServiceReadStatus> m_statuses;
    int m_totalTasks = 0;
    int m_doneTasks = 0;
};

#endif // FELICA_READER_H
