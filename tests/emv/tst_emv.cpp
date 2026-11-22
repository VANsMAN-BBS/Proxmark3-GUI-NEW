#include <QtTest>

#include <QElapsedTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "emvexport.h"
#include "emvruntime.h"
#include "emvtypes.h"
#include "emvworker.h"

namespace {

struct RunOutcome
{
    Emv::ReadResult result;
    QVector<Emv::Txn> records;
    QStringList statuses;
    QStringList warnings;
    QString startedPort;
    bool finished = false;
};

void configureMock(EmvWorker &worker, const QStringList &environment = QStringList())
{
    worker.setMockMode(true);
    worker.setPort(QStringLiteral("COM14"));
    worker.setTimeout(30.0);
    worker.setIdleAfterCard(0.5);
    worker.setNoCardTimeout(3.0);
    worker.setEnvironmentOverrides(environment);
}

bool waitFor(const bool &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition && timer.elapsed() < timeoutMs)
        QTest::qWait(50);
    return condition;
}

bool runWorker(EmvWorker &worker, RunOutcome *outcome, int timeoutMs = 60000)
{
    QObject::connect(&worker, &EmvWorker::recordReady,
                     [outcome](const Emv::Txn &txn) { outcome->records.append(txn); });
    QObject::connect(&worker, &EmvWorker::statusChanged,
                     [outcome](const QString &message) { outcome->statuses.append(message); });
    QObject::connect(&worker, &EmvWorker::warningAdded,
                     [outcome](const QString &warning) { outcome->warnings.append(warning); });
    QObject::connect(&worker, &EmvWorker::started,
                     [outcome](const QString &port) { outcome->startedPort = port; });
    QObject::connect(&worker, &EmvWorker::finished,
                     [outcome](const Emv::ReadResult &result)
                     {
                         outcome->result = result;
                         outcome->finished = true;
                     });

    worker.start();
    return waitFor(outcome->finished, timeoutMs);
}

QString warningsText(const Emv::ReadResult &result)
{
    return result.warnings.join(QLatin1Char('\n'));
}

} // namespace

class TestEmv : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mockTranscriptHasFourRecords();
    void bcdAmountsAndMerchantField();
    void noCardBranch();
    void layoutMismatchWarning();
    void stderrIsMerged();
    void csvExport();
    void jsonExport();
    void cancelKeepsPartialRecordsAndAllowsRestart();
    void processIsGoneAfterRun();
    void runtimeFallsBackWithoutOverride();
};

void TestEmv::initTestCase()
{
    // The harness may sandbox AppData writes; keep the extracted runtime inside
    // the build directory instead.
    const QString runtimeDir = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("emv-runtime-test"));
    qputenv("PM3GUI_EMV_RUNTIME_DIR", runtimeDir.toUtf8());

    QString error;
    const QString directory = EmvRuntime::scriptDirectory(&error);
    QVERIFY2(!directory.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(EmvRuntime::workerScript(directory)));
    QVERIFY(QFileInfo::exists(directory + QStringLiteral("/pm3_emv_log.py")));
    QVERIFY(QFileInfo::exists(directory + QStringLiteral("/mock_pm3.py")));

    QStringList prefixArgs;
    const QString python = EmvRuntime::pythonExecutable(&prefixArgs, &error);
    QVERIFY2(!python.isEmpty(), qPrintable(error));
    qInfo() << "EMV runtime:" << directory;
    qInfo() << "Python:" << python << prefixArgs;
}

void TestEmv::mockTranscriptHasFourRecords()
{
    EmvWorker worker;
    configureMock(worker);
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");

    QCOMPARE(outcome.result.txns.size(), 4);
    QCOMPARE(outcome.records.size(), 4);
    QVERIFY(outcome.result.cardDetected);
    QCOMPARE(outcome.result.stopReason, QStringLiteral("done"));
    QCOMPARE(outcome.result.returnCode, 0);
    QVERIFY(outcome.result.warnings.isEmpty());
    QCOMPARE(outcome.startedPort, QStringLiteral("COM14"));
    QVERIFY(!outcome.statuses.isEmpty());

    // Incremental records arrive in card order.
    QCOMPARE(outcome.records.at(0).recordNo, 1);
    QCOMPARE(outcome.records.at(3).recordNo, 4);
}

void TestEmv::bcdAmountsAndMerchantField()
{
    EmvWorker worker;
    configureMock(worker);
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");
    QCOMPARE(outcome.result.txns.size(), 4);

    const Emv::Txn &first = outcome.result.txns.at(0);
    QCOMPARE(first.date, QStringLiteral("2026-06-15"));
    QCOMPARE(first.time, QStringLiteral("09:32:41"));
    QCOMPARE(first.merchant, QStringLiteral("PSBC-ATM"));
    QCOMPARE(first.amount, QStringLiteral("500.00"));

    // BCD amount: 000000002350 -> 2350 cents -> 23.50
    QCOMPARE(outcome.result.txns.at(1).amount, QStringLiteral("23.50"));
    QCOMPARE(outcome.result.txns.at(1).merchant, QStringLiteral("12345678"));
    QCOMPARE(outcome.result.txns.at(2).amount, QStringLiteral("1000.00"));
    QCOMPARE(outcome.result.txns.at(3).amount, QStringLiteral("15.80"));
    QCOMPARE(outcome.result.txns.at(3).merchant, QStringLiteral("ATM"));
}

void TestEmv::noCardBranch()
{
    EmvWorker worker;
    configureMock(worker, QStringList() << QStringLiteral("MOCK_PM3_NO_CARD=1"));
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");

    QCOMPARE(outcome.result.txns.size(), 0);
    QVERIFY(!outcome.result.cardDetected);
    QCOMPARE(outcome.result.stopReason, QStringLiteral("no_card"));
    QVERIFY(!outcome.result.warnings.isEmpty());
}

void TestEmv::layoutMismatchWarning()
{
    EmvWorker worker;
    configureMock(worker, QStringList() << QStringLiteral("MOCK_PM3_BAD_LAYOUT=1"));
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");

    QCOMPARE(outcome.result.txns.size(), 0);
    QVERIFY(outcome.result.cardDetected);
    QCOMPARE(outcome.result.stopReason, QStringLiteral("done"));
    const QString warnings = warningsText(outcome.result);
    QVERIFY2(warnings.contains(QStringLiteral("9F4F")),
             qPrintable(QStringLiteral("missing layout warning: ") + warnings));
}

void TestEmv::stderrIsMerged()
{
    // The mock writes the whole second record (request + response) to stderr.
    // If the reader did not merge stderr into stdout only 3 records would be
    // parsed.
    EmvWorker worker;
    configureMock(worker, QStringList() << QStringLiteral("MOCK_PM3_STDERR=1"));
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");

    QCOMPARE(outcome.result.txns.size(), 4);
    QCOMPARE(outcome.result.txns.at(1).amount, QStringLiteral("23.50"));
    QCOMPARE(outcome.result.stopReason, QStringLiteral("done"));
}

void TestEmv::csvExport()
{
    EmvWorker worker;
    configureMock(worker);
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");
    QCOMPARE(outcome.result.txns.size(), 4);

    const QByteArray csv = EmvExport::toCsv(outcome.result.txns);
    QVERIFY(csv.startsWith("\xEF\xBB\xBF"));
    const QString text = QString::fromUtf8(csv.mid(3));
    QVERIFY(text.startsWith(QStringLiteral("date,time,merchant,amount,record_no\n")));
    QCOMPARE(text.count(QLatin1Char('\n')), 5); // header + 4 records
    QVERIFY(text.contains(QStringLiteral("2026-06-15,09:32:41,PSBC-ATM,500.00,1")));
    QVERIFY(text.contains(QStringLiteral("2026-06-10,12:00:59,ATM,15.80,4")));
}

void TestEmv::jsonExport()
{
    EmvWorker worker;
    configureMock(worker);
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");
    QCOMPARE(outcome.result.txns.size(), 4);

    const QByteArray json = EmvExport::toJson(outcome.result, true);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("experimental")).toBool(), true);
    QCOMPARE(root.value(QStringLiteral("read_only")).toBool(), true);

    const QJsonObject status = root.value(QStringLiteral("read_status")).toObject();
    QCOMPARE(status.value(QStringLiteral("stop_reason")).toString(), QStringLiteral("done"));
    QCOMPARE(status.value(QStringLiteral("record_count")).toInt(), 4);

    const QJsonArray transactions = root.value(QStringLiteral("transactions")).toArray();
    QCOMPARE(transactions.size(), 4);
    const QJsonObject first = transactions.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("date")).toString(), QStringLiteral("2026-06-15"));
    QCOMPARE(first.value(QStringLiteral("merchant")).toString(), QStringLiteral("PSBC-ATM"));
    QCOMPARE(first.value(QStringLiteral("amount")).toString(), QStringLiteral("500.00"));
    QCOMPARE(first.value(QStringLiteral("record_no")).toInt(), 1);
    QVERIFY(!first.value(QStringLiteral("raw")).toString().isEmpty());
}

void TestEmv::cancelKeepsPartialRecordsAndAllowsRestart()
{
    // MOCK_PM3_SLOW adds 0.15 s per line so cancel lands between records.
    EmvWorker worker;
    configureMock(worker, QStringList() << QStringLiteral("MOCK_PM3_SLOW=1"));
    worker.setIdleAfterCard(3.0);
    RunOutcome outcome;

    QObject::connect(&worker, &EmvWorker::recordReady,
                     [&outcome](const Emv::Txn &txn) { outcome.records.append(txn); });
    QObject::connect(&worker, &EmvWorker::finished,
                     [&outcome](const Emv::ReadResult &result)
                     {
                         outcome.result = result;
                         outcome.finished = true;
                     });

    worker.start();
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (outcome.records.isEmpty() && waitTimer.elapsed() < 20000)
        QTest::qWait(50);
    QVERIFY2(!outcome.records.isEmpty(), "no record arrived before cancel");
    QVERIFY(worker.isProcessRunning());

    worker.cancel();
    QVERIFY2(waitFor(outcome.finished, 20000), "cancel did not finish the worker");
    QCOMPARE(outcome.result.stopReason, QStringLiteral("cancelled"));
    QVERIFY(!outcome.result.txns.isEmpty());
    QVERIFY(outcome.result.txns.size() <= 4);
    QVERIFY(!worker.isProcessRunning());
    QCOMPARE(worker.processId(), qint64(0));

    // The process and its pipe are gone: a second reader must work.
    EmvWorker second;
    configureMock(second);
    RunOutcome secondOutcome;
    QVERIFY2(runWorker(second, &secondOutcome), "restart after cancel failed");
    QCOMPARE(secondOutcome.result.txns.size(), 4);
    QCOMPARE(secondOutcome.result.stopReason, QStringLiteral("done"));
}

void TestEmv::processIsGoneAfterRun()
{
    EmvWorker worker;
    configureMock(worker);
    RunOutcome outcome;
    QVERIFY2(runWorker(worker, &outcome), "the mock reader did not finish");

    QVERIFY(!worker.isProcessRunning());
    QCOMPARE(worker.processId(), qint64(0));
    QCOMPARE(outcome.result.txns.size(), 4);
}

void TestEmv::runtimeFallsBackWithoutOverride()
{
    // Production does not set PM3GUI_EMV_RUNTIME_DIR. Verify that the normal
    // AppData -> Temp -> application directory chain still produces a runtime
    // (the CI sandbox may deny the first two).
    const QByteArray saved = qgetenv("PM3GUI_EMV_RUNTIME_DIR");
    qunsetenv("PM3GUI_EMV_RUNTIME_DIR");

    QString error;
    const QString directory = EmvRuntime::scriptDirectory(&error);
    qputenv("PM3GUI_EMV_RUNTIME_DIR", saved);

    QVERIFY2(!directory.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(EmvRuntime::workerScript(directory)));
    QVERIFY(QFileInfo::exists(directory + QStringLiteral("/pm3_emv_log.py")));
}

QTEST_GUILESS_MAIN(TestEmv)

#include "tst_emv.moc"
