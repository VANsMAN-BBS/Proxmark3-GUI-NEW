#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTranslator>
#include <QVector>

#include "iso14443apage.h"
#include "iso14443aruntime.h"
#include "iso14443aservice.h"

namespace {

const QByteArray kUid = QByteArray::fromHex("11223344");
const QByteArray kAts = QByteArray::fromHex("097877C1020E513101");

quint16 crc16A(const QByteArray &data)
{
    quint16 crc = 0x6363;
    for (char ch : data)
    {
        quint16 bt = static_cast<quint8>(ch);
        bt ^= (crc & 0xFF);
        bt ^= ((bt << 4) & 0xFF);
        crc = static_cast<quint16>((crc >> 8) ^ ((bt << 8) & 0xFFFF)
                                   ^ ((bt << 3) & 0xFFFF) ^ (bt >> 4));
    }
    return static_cast<quint16>(((crc & 0x00FF) << 8) | ((crc >> 8) & 0x00FF));
}

QByteArray appendCrc(const QByteArray &data)
{
    const quint16 crc = crc16A(data);
    QByteArray result = data;
    result.append(static_cast<char>((crc >> 8) & 0xFF));
    result.append(static_cast<char>(crc & 0xFF));
    return result;
}

int oddParity(char value)
{
    const quint8 byte = static_cast<quint8>(value);
    int ones = 0;
    for (int i = 0; i < 8; ++i)
        ones += (byte >> i) & 1;
    return (ones % 2 == 1) ? 0 : 1;
}

QByteArray bitsToWire(const QVector<int> &bits, int *validBits)
{
    QByteArray out;
    for (int i = 0; i < bits.size(); i += 8)
    {
        int value = 0;
        for (int j = 0; j < 8 && i + j < bits.size(); ++j)
            value |= (bits.at(i + j) & 1) << (7 - j);
        out.append(static_cast<char>(value));
    }
    int valid = bits.size() % 8;
    if (valid == 0 && !bits.isEmpty())
        valid = 8;
    if (valid != 8 && !out.isEmpty())
        out[out.size() - 1] = static_cast<char>(
            static_cast<quint8>(out.at(out.size() - 1)) >> (8 - valid));
    *validBits = valid;
    return out;
}

QByteArray buildPacket(char direction, quint32 timestamp, const QByteArray &data,
                       bool withCrc = true, int partialBits = -1)
{
    const QByteArray wire = withCrc ? appendCrc(data) : data;
    QVector<int> bits;
    if (partialBits > 0)
    {
        for (int k = 0; k < partialBits; ++k)
            bits.append((static_cast<quint8>(wire.at(0)) >> k) & 1);
    }
    else
    {
        for (char ch : wire)
        {
            const quint8 byte = static_cast<quint8>(ch);
            for (int k = 0; k < 8; ++k)
                bits.append((byte >> k) & 1);
            bits.append(oddParity(ch));
        }
    }

    int valid = 8;
    const QByteArray payload = bitsToWire(bits, &valid);
    QByteArray packet;
    packet.append(direction);
    packet.append(static_cast<char>((timestamp >> 24) & 0xFF));
    packet.append(static_cast<char>((timestamp >> 16) & 0xFF));
    packet.append(static_cast<char>((timestamp >> 8) & 0xFF));
    packet.append(static_cast<char>(timestamp & 0xFF));
    packet.append(static_cast<char>((payload.size() >> 8) & 0xFF));
    packet.append(static_cast<char>(payload.size() & 0xFF));
    packet.append(static_cast<char>(valid));
    packet.append(payload);
    return packet;
}

QList<QByteArray> tclPackets()
{
    QByteArray uidBcc = kUid;
    char bcc = 0;
    for (char ch : kUid)
        bcc = static_cast<char>(bcc ^ ch);
    uidBcc.append(bcc);

    QList<QByteArray> packets;
    packets << buildPacket('<', 3100, QByteArray::fromHex("0400"), false);
    packets << buildPacket('>', 3200, QByteArray::fromHex("9320"), false);
    packets << buildPacket('<', 3300, uidBcc, false);
    packets << buildPacket('>', 3400, QByteArray::fromHex("9370") + uidBcc);
    packets << buildPacket('<', 3500, QByteArray::fromHex("20"));
    packets << buildPacket('>', 3600, QByteArray::fromHex("E080"));
    packets << buildPacket('<', 3700, kAts);
    packets << buildPacket('>', 3800, QByteArray::fromHex("0200B0000008"));
    return packets;
}

QList<QByteArray> atqaPackets()
{
    QList<QByteArray> packets;
    packets << buildPacket('<', 5000, QByteArray::fromHex("0400"), false);
    packets << buildPacket('<', 5100, QByteArray::fromHex("08"));
    return packets;
}

class ScopedTempDir
{
public:
    ScopedTempDir()
    {
        const QString base = QDir::currentPath() + QStringLiteral("/iso14443a-tmp-")
            + QString::number(QCoreApplication::applicationPid()) + QLatin1Char('-')
            + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (QDir().mkpath(base))
            path = base;
    }
    ~ScopedTempDir()
    {
        if (!path.isEmpty())
            QDir(path).removeRecursively();
    }
    QString path;
};

QString writeCapture(const QString &path, const QList<QByteArray> &packets)
{
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
        return QString();
    for (const QByteArray &packet : packets)
        file.write(packet.toHex().toUpper() + "\n");
    file.close();
    return path;
}

QJsonObject waitForParse(QSignalSpy &spy, quint64 requestId, int timeoutMs = 40000)
{
    QJsonObject result;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        if (spy.count() > 0)
        {
            const QList<QVariant> args = spy.takeFirst();
            const quint64 id = args.at(0).toULongLong();
            if (id != requestId)
                continue;
            result = args.at(2).value<QJsonObject>();
            return result;
        }
        QTest::qWait(50);
    }
    return result;
}

void installTestRuntime()
{
    Iso14443aRuntime::clearPythonCache();
    Iso14443aRuntime::clearScriptCache();
    qputenv("PM3GUI_ISO14443A_RUNTIME_DIR",
            (QDir::tempPath() + QStringLiteral("/iso14443a-test-runtime")).toUtf8());
}

} // namespace

class Iso14443aTests : public QObject
{
    Q_OBJECT

private slots:
    void pageRegistration();
    void initializeAndParseCapture();
    void consecutiveParsesDoNotShareState();
    void partialLineBuffering();
    void crashRecovery();
};

void Iso14443aTests::pageRegistration()
{
    QTabWidget tabs;
    Iso14443aPage *page = new Iso14443aPage(&tabs);
    const int index = registerIso14443aPage(&tabs, page);
    QCOMPARE(index, 0);
    QCOMPARE(tabs.count(), 1);
    QCOMPARE(tabs.tabText(index), Iso14443aPage::pageTitle());
    QCOMPARE(page->objectName(), QStringLiteral("Iso14443a_page"));
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("Iso14443a_openTraceButton")) != nullptr);
    QVERIFY(page->findChild<QPushButton *>(QStringLiteral("Iso14443a_parseButton")) != nullptr);

    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/zh_CN.qm")));
    QCoreApplication::installTranslator(&translator);
    QCOMPARE(Iso14443aPage::pageTitle(), QString::fromUtf8("14443A 协议解析"));
    QCoreApplication::removeTranslator(&translator);
}

void Iso14443aTests::initializeAndParseCapture()
{
    installTestRuntime();
    QString error;
    if (Iso14443aRuntime::pythonExecutable(nullptr, &error).isEmpty())
        QSKIP("Python 3 not available for the sidecar");

    Iso14443aService service;
    QSignalSpy finished(&service, &Iso14443aService::requestFinished);
    const quint64 initId = service.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(service.isReady(), 30000);
    Q_UNUSED(initId);
    finished.clear();

    ScopedTempDir dir;
    QVERIFY2(!dir.path.isEmpty(), "cannot create a temporary directory");
    const QString cjkDir = dir.path + QStringLiteral("/鍗忚_test");
    QVERIFY(QDir().mkpath(cjkDir));
    const QString capture = writeCapture(cjkDir + QStringLiteral("/capture.txt"), tclPackets());
    QVERIFY(!capture.isEmpty());

    const quint64 id = service.parseCapture(capture);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 40000);
    const QJsonObject data = waitForParse(finished, id);
    QVERIFY(!data.isEmpty());

    const QJsonObject card = data.value(QStringLiteral("card_info")).toObject();
    QCOMPARE(card.value(QStringLiteral("uid")).toString(), QStringLiteral("11223344"));
    QCOMPARE(card.value(QStringLiteral("atqa_hex")).toString(), QStringLiteral("0004"));
    QCOMPARE(card.value(QStringLiteral("sak_hex")).toString(), QStringLiteral("20"));
    QVERIFY(card.value(QStringLiteral("ats")).isObject());

    const QJsonObject statistics = data.value(QStringLiteral("statistics")).toObject();
    const QJsonObject byType = statistics.value(QStringLiteral("by_type")).toObject();
    QVERIFY(byType.contains(QStringLiteral("ats")));
    QVERIFY(byType.contains(QStringLiteral("i_block")));
}

void Iso14443aTests::consecutiveParsesDoNotShareState()
{
    installTestRuntime();
    if (Iso14443aRuntime::pythonExecutable().isEmpty())
        QSKIP("Python 3 not available for the sidecar");

    Iso14443aService service;
    QSignalSpy finished(&service, &Iso14443aService::requestFinished);
    service.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(service.isReady(), 30000);
    finished.clear();

    ScopedTempDir dir;
    QVERIFY2(!dir.path.isEmpty(), "cannot create a temporary directory");
    const QString first = writeCapture(dir.path + QStringLiteral("/first.txt"), tclPackets());
    const QString second = writeCapture(dir.path + QStringLiteral("/second.txt"), atqaPackets());

    quint64 id = service.parseCapture(first);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 40000);
    QJsonObject data = waitForParse(finished, id);
    QVERIFY(data.value(QStringLiteral("card_info")).toObject()
                .value(QStringLiteral("protocol")).toObject()
                .value(QStringLiteral("tcl")).toBool());

    id = service.parseCapture(second);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 40000);
    data = waitForParse(finished, id);
    const QJsonArray frames = data.value(QStringLiteral("frames")).toArray();
    QVERIFY(!frames.isEmpty());
    QCOMPARE(frames.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("atqa"));
    QCOMPARE(data.value(QStringLiteral("card_info")).toObject()
                 .value(QStringLiteral("atqa_hex")).toString(), QStringLiteral("0004"));
}

void Iso14443aTests::partialLineBuffering()
{
    installTestRuntime();
    if (Iso14443aRuntime::pythonExecutable().isEmpty())
        QSKIP("Python 3 not available for the sidecar");

    ScopedTempDir dir;
    QVERIFY2(!dir.path.isEmpty(), "cannot create a temporary directory");
    const QString script = dir.path + QStringLiteral("/fake_sidecar.py");
    QFile file(script);
    QVERIFY(file.open(QFile::WriteOnly | QFile::Truncate));
    file.write(
        "import sys, json, time\n"
        "for line in sys.stdin:\n"
        "    try:\n"
        "        req = json.loads(line)\n"
        "    except Exception:\n"
        "        continue\n"
        "    out = json.dumps({'schema':1,'request_id':req.get('request_id'),"
        "'status':'ok','message':'fake','data':{'fake':True}})\n"
        "    sys.stdout.write(out[:12]); sys.stdout.flush()\n"
        "    time.sleep(0.25)\n"
        "    sys.stdout.write(out[12:] + '\\n'); sys.stdout.flush()\n");
    file.close();

    qputenv("PM3GUI_ISO14443A_SCRIPT", script.toUtf8());
    Iso14443aService service;
    QSignalSpy finished(&service, &Iso14443aService::requestFinished);
    const quint64 id = service.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 20000);
    const QJsonObject data = waitForParse(finished, id);
    QVERIFY(data.value(QStringLiteral("fake")).toBool());
    service.shutdown();
    qunsetenv("PM3GUI_ISO14443A_SCRIPT");
}

void Iso14443aTests::crashRecovery()
{
    installTestRuntime();
    qunsetenv("PM3GUI_ISO14443A_SCRIPT");
    if (Iso14443aRuntime::pythonExecutable().isEmpty())
        QSKIP("Python 3 not available for the sidecar");

    Iso14443aService service;
    service.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(service.isReady(), 30000);
    const qint64 pid = service.processId();
    QVERIFY(pid > 0);

#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill"),
                      QStringList() << QStringLiteral("/PID")
                                    << QString::number(pid)
                                    << QStringLiteral("/F"));
#else
    QProcess::execute(QStringLiteral("kill"),
                      QStringList() << QStringLiteral("-9") << QString::number(pid));
#endif

    QTRY_VERIFY_WITH_TIMEOUT(!service.isReady(), 15000);
    // The service restarts automatically; a new request must succeed.
    QSignalSpy finished(&service, &Iso14443aService::requestFinished);
    service.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(service.isReady(), 30000);
    QVERIFY(service.isRunning());
}

QTEST_MAIN(Iso14443aTests)
#include "tst_iso14443a.moc"



