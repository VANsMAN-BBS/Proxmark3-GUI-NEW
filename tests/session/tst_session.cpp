#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <iostream>
#include <string>

#include "core/clientsession.h"
#include "module/mifare/mfpm3log.h"

namespace
{

/**
 * Minimal stand-in for the Proxmark3 client.  The test binary re-executes
 * itself with --fake-client so the real ClientSession/QProcess path is used
 * without requiring hardware or a second build target.
 */
int runFakeClient(const QString &logPath)
{
    QFile log(logPath);
    log.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream logStream(&log);
    logStream.setCodec("UTF-8");

    std::string line;
    while (std::getline(std::cin, line))
    {
        const QString command = QString::fromUtf8(line.c_str());
        if (command.startsWith(QStringLiteral("rem ")))
        {
            std::cout << "remark: " << command.mid(4).toUtf8().constData() << "\n";
            std::cout.flush();
            continue;
        }
        if (command == QStringLiteral("hw version"))
        {
            std::cout << "OS................. PM3 GENERIC\n";
            std::cout.flush();
            continue;
        }
        if (command == QStringLiteral("quit"))
            break;

        logStream << command << "\n";
        logStream.flush();
        if (command.startsWith(QStringLiteral("hf 14a info")))
        {
            std::cout << "[+] UID: A1 B2 C3 D4\n";
            std::cout << "[+] ATQA: 00 04\n";
            std::cout << "[+] SAK: 08 [1]\n";
        }
        std::cout << "[+] cmd: " << command.toUtf8().constData() << "\n";
        std::cout << "[+] Reading block 3 (25%)\n";
        std::cout << "25%\n";
        std::cout << "[+] done\n";
        std::cout.flush();
    }
    return 0;
}

QString gather(const QSignalSpy &spy, int firstArgument = 0)
{
    QString text;
    for (const QList<QVariant> &args : spy)
    {
        if (args.size() > firstArgument)
            text += args.at(firstArgument).toString();
    }
    return text;
}

} // namespace

class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void handshakeAndAttribution();
    void sentinelNeverReachesCommandOutput();
    void oneExecutionPerCommand();
    void terminalKeepsRawProgressWhileDetailLogCompacts();
    void cancelAndTaskIsolation();
    void chunkBoundariesAndNoise();
    void keepsBriefedSignals();
    void capKeepsCompletion();
    void scrollPolicyFollowsTail();

private:
    QTemporaryDir *m_dir = nullptr;
    ClientSession *m_session = nullptr;
    QString m_logPath;
};

void TestSession::init()
{
    m_dir = new QTemporaryDir(QDir::currentPath() + QStringLiteral("/sessiontmp-XXXXXX"));
    QVERIFY(m_dir->isValid());
    m_logPath = m_dir->filePath(QStringLiteral("fake-client-commands.txt"));
    m_session = new ClientSession;
    m_session->startClient(QCoreApplication::applicationFilePath(),
                           QStringList() << QStringLiteral("--fake-client") << m_logPath);
    QTRY_VERIFY_WITH_TIMEOUT(m_session->isConnected(), 8000);
}

void TestSession::cleanup()
{
    if (m_session)
    {
        m_session->stopClient();
        delete m_session;
        m_session = nullptr;
    }
    delete m_dir;
    m_dir = nullptr;
}

void TestSession::handshakeAndAttribution()
{
    QSignalSpy newOutput(m_session, &ClientSession::newOutput);
    QSignalSpy commandOutput(m_session, &ClientSession::commandOutput);
    QSignalSpy finished(m_session, &ClientSession::commandFinished);

    const quint64 id = m_session->executeCommand(QStringLiteral("say alpha"), 5000, true);
    QVERIFY(finished.wait(8000));

    QCOMPARE(finished.at(0).at(0).toULongLong(), id);
    QVERIFY(finished.at(0).at(2).toBool());

    const QString terminal = gather(newOutput, 0);
    QVERIFY2(terminal.contains(QStringLiteral("say alpha")), qPrintable(terminal));
    QVERIFY2(terminal.contains(QStringLiteral("[+] Reading block 3 (25%)")), qPrintable(terminal));

    bool sawCommandChunk = false;
    for (const QList<QVariant> &args : commandOutput)
    {
        if (args.at(0).toULongLong() == id)
        {
            sawCommandChunk = true;
            break;
        }
    }
    QVERIFY(sawCommandChunk);
}

void TestSession::sentinelNeverReachesCommandOutput()
{
    QSignalSpy commandOutput(m_session, &ClientSession::commandOutput);
    QSignalSpy finished(m_session, &ClientSession::commandFinished);
    m_session->executeCommand(QStringLiteral("say beta"), 5000, true);
    QVERIFY(finished.wait(8000));

    const QString detail = gather(commandOutput, 1);
    QVERIFY(!detail.contains(QStringLiteral("remark: PM3GUI_")));
    QVERIFY(detail.contains(QStringLiteral("say beta")));
}

void TestSession::oneExecutionPerCommand()
{
    QSignalSpy finished(m_session, &ClientSession::commandFinished);
    m_session->executeCommand(QStringLiteral("say first"), 5000, true);
    QVERIFY(finished.wait(8000));
    m_session->executeCommand(QStringLiteral("say second"), 5000, true);
    QVERIFY(finished.wait(8000));

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'),
                                                                    QString::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QStringLiteral("say first"));
    QCOMPARE(lines.at(1), QStringLiteral("say second"));
}

void TestSession::terminalKeepsRawProgressWhileDetailLogCompacts()
{
    QSignalSpy newOutput(m_session, &ClientSession::newOutput);
    QSignalSpy finished(m_session, &ClientSession::commandFinished);
    m_session->executeCommand(QStringLiteral("say gamma"), 5000, true);
    QVERIFY(finished.wait(8000));

    const QString terminal = gather(newOutput, 0);
    QVERIFY2(terminal.contains(QStringLiteral("[+] Reading block 3 (25%)")), qPrintable(terminal));

    mfgen3::Pm3LogFilter filter;
    filter.beginTask(7);
    const QStringList compacted = filter.feed(
        7, QStringLiteral("[+] cmd: say gamma\n[+] Reading block 3 (25%)\n25%\n[+] done\n"));
    QVERIFY(!compacted.contains(QStringLiteral("25%")));
    QVERIFY(compacted.contains(QStringLiteral("[+] Reading block 3 (25%)")));
    QVERIFY(compacted.contains(QStringLiteral("[+] done")));

    // The terminal copy is unchanged by the compactor.
    QVERIFY(terminal.contains(QStringLiteral("[+] Reading block 3 (25%)")));
}

void TestSession::cancelAndTaskIsolation()
{
    mfgen3::Pm3LogFilter filter;
    filter.beginTask(1);
    QCOMPARE(filter.feed(1, QStringLiteral("[+] Keys: 4/32\n")),
             QStringList() << QStringLiteral("[+] Keys: 4/32"));
    // Repeating a status line is compacted.
    QVERIFY(filter.feed(1, QStringLiteral("[+] Keys: 4/32\n")).isEmpty());

    // Cancel freezes the task immediately; late chunks are ignored.
    QVERIFY(filter.feed(1, QStringLiteral("[+] late block 5\n"), true).isEmpty());
    QVERIFY(filter.feed(1, QStringLiteral("[+] later block 6\n")).isEmpty());

    // Next task starts clean and may repeat the same text again.
    filter.beginTask(2);
    QCOMPARE(filter.feed(2, QStringLiteral("[+] Keys: 4/32\n")),
             QStringList() << QStringLiteral("[+] Keys: 4/32"));
    // A stale serial cannot write into the new task.
    QVERIFY(filter.feed(1, QStringLiteral("[+] stale block 9\n")).isEmpty());
    QCOMPARE(filter.serial(), quint64(2));
}

void TestSession::chunkBoundariesAndNoise()
{
    mfgen3::Pm3LogFilter filter;
    filter.beginTask(11);

    // A line split across chunks is emitted exactly once, when complete.
    QVERIFY(filter.feed(11, QStringLiteral("[+] Reading bl")).isEmpty());
    QCOMPARE(filter.feed(11, QStringLiteral("ock 4\n")),
             QStringList() << QStringLiteral("[+] Reading block 4"));

    // Percentage-only and separator lines are noise.
    QVERIFY(filter.feed(11, QStringLiteral("10%\n20%\n[+] ----+----\n")).isEmpty());

    // The same status line is not repeated.
    QCOMPARE(filter.feed(11, QStringLiteral("[+] Waiting for card\n")),
             QStringList() << QStringLiteral("[+] Waiting for card"));
    QVERIFY(filter.feed(11, QStringLiteral("[+] Waiting for card\n")).isEmpty());
}

void TestSession::keepsBriefedSignals()
{
    mfgen3::Pm3LogFilter filter;
    filter.beginTask(12);
    const QString chunk =
        QStringLiteral("[+] UID: A1 B2 C3 D4\n"
                       "[+] backdoor key candidate A31667A8CEC1\n"
                       "[+] nonce 11223344 -> 55667788\n"
                       "[+] verified 12/32 keys\n"
                       "[+] current block 7\n"
                       "[+] block 7: AABBCCDDEEFF00112233445566778899\n"
                       "[-] read failed: NACK\n"
                       "[+] task complete\n");
    const QStringList lines = filter.feed(12, chunk);
    QCOMPARE(lines.size(), 8);
    QVERIFY(lines.at(0).contains(QStringLiteral("UID")));
    QVERIFY(lines.at(1).contains(QStringLiteral("backdoor")));
    QVERIFY(lines.at(2).contains(QStringLiteral("nonce")));
    QVERIFY(lines.at(3).contains(QStringLiteral("verified")));
    QVERIFY(lines.at(4).contains(QStringLiteral("block 7")));
    QVERIFY(lines.at(5).contains(QStringLiteral("block 7:")));
    QVERIFY(lines.at(6).contains(QStringLiteral("failed")));
    QVERIFY(lines.at(7).contains(QStringLiteral("complete")));
}

void TestSession::capKeepsCompletion()
{
    mfgen3::Pm3LogFilter filter;
    filter.beginTask(13);
    QString chunk;
    for (int i = 0; i < 460; ++i)
        chunk += QStringLiteral("[+] filler block %1\n").arg(i);

    const QStringList capped = filter.feed(13, chunk);
    QVERIFY(capped.size() <= 402);
    QVERIFY(capped.join(QLatin1Char('\n')).contains(
        QStringLiteral("further PM3 output suppressed")));

    // Completion is important even after the cap.
    const QStringList tail = filter.feed(13, QStringLiteral("[+] task complete\n"));
    QCOMPARE(tail, QStringList() << QStringLiteral("[+] task complete"));
}

void TestSession::scrollPolicyFollowsTail()
{
    QVERIFY(mfgen3::Pm3LogFilter::shouldAutoScroll(100, 100));
    QVERIFY(mfgen3::Pm3LogFilter::shouldAutoScroll(98, 100));
    QVERIFY(!mfgen3::Pm3LogFilter::shouldAutoScroll(0, 100));
    QVERIFY(!mfgen3::Pm3LogFilter::shouldAutoScroll(95, 100));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--fake-client")))
    {
        const QStringList args = app.arguments();
        const int index = args.indexOf(QStringLiteral("--fake-client"));
        const QString logPath = index + 1 < args.size() ? args.at(index + 1) : QString();
        return runFakeClient(logPath);
    }

    TestSession test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_session.moc"
