#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include "ntag/ndefbuilder.h"
#include "ntag/ntagmemory.h"
#include "ntag/ntagmodel.h"

namespace {

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for(int value : values)
        out.append(static_cast<char>(value));
    return out;
}

QString hex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex()).toUpper();
}

Ntag::NdefResult parse(const Ndef::LaunchPayload &payload)
{
    return Ntag::parseNdef(payload.userArea);
}

} // namespace

class TestQuickLaunch : public QObject
{
    Q_OBJECT
private slots:
    void samples_data();
    void samples();

    void uriPrefixCodes();
    void urlRecordExact();
    void textRecordExact();
    void textUtf8AndLanguage();
    void textLanguageValidation();

    void mimeNonAsciiWarning();
    void mimeEmptyPayloadAllowed();
    void mimeValidation();

    void vcardUsesCrlf();
    void vcardRequiresFields();
    void wifiWscExact();
    void wifiValidation();
    void bluetoothOobExact();
    void bluetoothValidation();
    void openPrintTag64Bytes();
    void externalAndAar();
    void externalValidation();

    void multiRecordFlags();
    void shortRecordBoundary();
    void tlvBoundaries();
    void oversizeRejected();
    void invalidUris();
    void unknownPayloadPreserved();

    void buildLaunchMappings();
    void simWorkspaceCreateCleanup();
    void simDumpRoundTrip();
};

void TestQuickLaunch::samples_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<QString>("primary");
    QTest::addColumn<QString>("subject");
    QTest::addColumn<QString>("body");
    QTest::addColumn<QString>("extra");

    const QString openPrintTagPayload = hex(QByteArray(64, '\x5A'));

    QTest::newRow("01 HTTPS") << int(Ndef::LaunchType::Url)
                              << "https://123.com" << "" << "" << "";
    QTest::newRow("02 Text UTF-8") << int(Ndef::LaunchType::Text)
                                   << QString::fromUtf8("ABC123测试测试") << "en" << "" << "";
    QTest::newRow("03 custom Git URI") << int(Ndef::LaunchType::CustomUri)
                                       << QString::fromUtf8("git://123测试个性化") << "" << "" << "";
    QTest::newRow("04 App Store") << int(Ndef::LaunchType::Url)
                                  << "https://appstore.com/abc/abcapp" << "" << "" << "";
    QTest::newRow("05 HTTPS www") << int(Ndef::LaunchType::Url)
                                  << QString::fromUtf8("https://www.facebook.com/测试社交网络") << "" << "" << "";
    QTest::newRow("06 YouTube") << int(Ndef::LaunchType::Url)
                                << "https://youtu.be/VideoIDorURL" << "" << "" << "";
    QTest::newRow("07 CN query") << int(Ndef::LaunchType::Url)
                                 << "https://www.google.com/search?q=%E6%B5%8B%E8%AF%95" << "" << "" << "";
    QTest::newRow("08 file URI") << int(Ndef::LaunchType::CustomUri)
                                 << "file:///Storage/example.txt" << "" << "" << "";
    QTest::newRow("09 mailto") << int(Ndef::LaunchType::Email)
                               << "Test@Test.com" << QString::fromUtf8("标题测试")
                               << QString::fromUtf8("内容测试") << "";
    QTest::newRow("10 vCard") << int(Ndef::LaunchType::VCard)
                              << "name" << "Company" << "123123" << "Test@Test.com";
    QTest::newRow("11 tel") << int(Ndef::LaunchType::Phone)
                            << "123123" << "" << "" << "";
    QTest::newRow("12 sms") << int(Ndef::LaunchType::Sms)
                            << "123123" << "" << QString::fromUtf8("内容测试") << "";
    QTest::newRow("13 facetime") << int(Ndef::LaunchType::CustomUri)
                                 << "facetime:123123oremail" << "" << "" << "";
    QTest::newRow("14 maps") << int(Ndef::LaunchType::CustomUri)
                             << "comgooglemaps://maps.google.com/?q=1,2" << "" << "" << "";
    QTest::newRow("15 paypal") << int(Ndef::LaunchType::Url)
                               << "https://paypal.me/%E8%B4%A6%E6%88%B7/123USD" << "" << "" << "";
    QTest::newRow("16 Bluetooth OOB") << int(Ndef::LaunchType::Bluetooth)
                                      << "26:98:15:06:D8:12" << "" << "" << "";
    QTest::newRow("17 Wi-Fi WSC") << int(Ndef::LaunchType::Wifi)
                                  << "ssidtest" << "Passwordtest" << "WPA2" << "";
    QTest::newRow("18 non-ASCII MIME") << int(Ndef::LaunchType::Mime)
                                       << QString::fromUtf8("文字测试1/文字测试2") << ""
                                       << "" << QString::fromLatin1("e695b0e68daee38383");
    QTest::newRow("19 X-HM") << int(Ndef::LaunchType::CustomUri)
                             << "X-HM://00145PFD9UMHN" << "" << "" << "";
    QTest::newRow("20 OpenPrintTag") << int(Ndef::LaunchType::OpenPrintTag)
                                     << "" << "" << "" << openPrintTagPayload;
    QTest::newRow("21 shortcuts") << int(Ndef::LaunchType::CustomUri)
                                  << "shortcuts://run-shortcut?name=test&input=text&text=abc"
                                  << "" << "" << "";
}

void TestQuickLaunch::samples()
{
    QFETCH(int, type);
    QFETCH(QString, primary);
    QFETCH(QString, subject);
    QFETCH(QString, body);
    QFETCH(QString, extra);

    Ndef::LaunchRequest request;
    request.type = static_cast<Ndef::LaunchType>(type);
    request.primary = primary;
    request.subject = subject;
    request.body = body;
    request.extra = extra;

    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    QVERIFY2(payload.ok, qPrintable(payload.error));
    QVERIFY(!payload.message.isEmpty());
    QCOMPARE(payload.userArea.size() % 4, 0);
    QVERIFY(payload.requiredBytes > 0);
    QCOMPARE(payload.tlv.size() + 1, payload.requiredBytes);

    const Ntag::NdefResult parsed = parse(payload);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.records.size(), 1);

    if(type == int(Ndef::LaunchType::Mime))
        QVERIFY(!payload.warnings.isEmpty());
    else
        QVERIFY(payload.warnings.isEmpty());
}

void TestQuickLaunch::uriPrefixCodes()
{
    struct Case { const char *uri; int prefix; };
    const Case cases[] = {
        { "sms:123123&body=hello", 0x00 },
        { "facetime:123", 0x00 },
        { "https://www.example.com/x", 0x02 },
        { "https://example.com/x", 0x04 },
        { "tel:123123", 0x05 },
        { "mailto:test@example.com", 0x06 },
        { "file:///storage/x", 0x1D }
    };
    for(const Case &item : cases)
    {
        const Ndef::LaunchPayload payload = Ndef::buildUri(QString::fromLatin1(item.uri));
        QVERIFY2(payload.ok, item.uri);
        QVERIFY(payload.message.size() >= 5);
        QCOMPARE(int(static_cast<quint8>(payload.message.at(4))), item.prefix);
    }
}

void TestQuickLaunch::urlRecordExact()
{
    const Ndef::LaunchPayload payload = Ndef::buildUri(QStringLiteral("https://123.com"));
    QVERIFY(payload.ok);
    QCOMPARE(hex(payload.message), QStringLiteral("D1010855043132332E636F6D"));
}

void TestQuickLaunch::textRecordExact()
{
    const Ndef::LaunchPayload payload = Ndef::buildText(QStringLiteral("ABC123"),
                                                        QStringLiteral("en"));
    QVERIFY(payload.ok);
    QCOMPARE(hex(payload.message),
             QStringLiteral("D101095482656E414243313233"));
}

void TestQuickLaunch::textUtf8AndLanguage()
{
    const QString text = QString::fromUtf8("ABC123测试测试");
    const Ndef::LaunchPayload payload = Ndef::buildText(text, QStringLiteral("zh"));
    QVERIFY(payload.ok);
    const Ntag::NdefResult parsed = parse(payload);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.size(), 1);
    QCOMPARE(parsed.records.first().typeName, QStringLiteral("Text"));
    QCOMPARE(parsed.records.first().text, text);
    QCOMPARE(static_cast<quint8>(payload.message.at(4)), quint8(0x82));
}

void TestQuickLaunch::textLanguageValidation()
{
    QVERIFY(!Ndef::buildText(QString(), QStringLiteral("en")).ok);
    QVERIFY(!Ndef::buildText(QStringLiteral("x"), QString::fromUtf8("中文")).ok);
    QVERIFY(!Ndef::buildText(QStringLiteral("x"), QString(64, QLatin1Char('a'))).ok);
}

void TestQuickLaunch::mimeNonAsciiWarning()
{
    const QByteArray body = QString::fromUtf8("数据3").toUtf8();
    const Ndef::LaunchPayload payload = Ndef::buildMime(
                QString::fromUtf8("文字测试1/文字测试2"), body);
    QVERIFY(payload.ok);
    QCOMPARE(payload.warnings.size(), 1);
    QVERIFY(payload.warnings.first().contains(QStringLiteral("non-ASCII")));
    const Ntag::NdefResult parsed = parse(payload);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.first().payload, body);
    QCOMPARE(parsed.records.first().tnf, QStringLiteral("0X02"));
}

void TestQuickLaunch::mimeEmptyPayloadAllowed()
{
    const Ndef::LaunchPayload payload = Ndef::buildMime(
                QStringLiteral("application/octet-stream"), QByteArray());
    QVERIFY(payload.ok);
    QCOMPARE(parse(payload).records.first().payload.size(), 0);
}

void TestQuickLaunch::mimeValidation()
{
    QVERIFY(!Ndef::buildMime(QString(), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildMime(QStringLiteral("text"), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildMime(QStringLiteral("text/"), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildMime(QStringLiteral("/plain"), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildMime(QStringLiteral("text/plain file"), QByteArray("x")).ok);
}

void TestQuickLaunch::vcardUsesCrlf()
{
    const Ndef::LaunchPayload payload = Ndef::buildVCard(
                QStringLiteral("name"), QStringLiteral("Company"),
                QStringLiteral("123123"), QStringLiteral("Test@Test.com"),
                QStringLiteral("https://example.com"),
                QString::fromUtf8("地址，abc"));
    QVERIFY(payload.ok);
    const Ntag::NdefResult parsed = parse(payload);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.first().tnf, QStringLiteral("0X02"));
    QCOMPARE(parsed.records.first().mime, QStringLiteral("text/vcard"));

    const QByteArray vcard = parsed.records.first().payload;
    QVERIFY(vcard.startsWith("BEGIN:VCARD\r\nVERSION:3.0\r\n"));
    QVERIFY(vcard.endsWith("END:VCARD\r\n"));
    QByteArray withoutCrLf = vcard;
    withoutCrLf.replace("\r\n", "");
    QVERIFY(!withoutCrLf.contains('\n'));
    QVERIFY(vcard.contains("FN:name\r\n"));
    QVERIFY(vcard.contains("ORG:Company\r\n"));
    QVERIFY(vcard.contains("TEL:123123\r\n"));
    QVERIFY(vcard.contains("EMAIL:Test@Test.com\r\n"));
}

void TestQuickLaunch::vcardRequiresFields()
{
    QVERIFY(!Ndef::buildVCard(QString(), QString(), QString(), QString(),
                              QString(), QString()).ok);
}

void TestQuickLaunch::wifiWscExact()
{
    const Ndef::LaunchPayload payload = Ndef::buildWifi(
                QStringLiteral("ssidtest"), QStringLiteral("Passwordtest"),
                QStringLiteral("WPA2"));
    QVERIFY(payload.ok);
    QCOMPARE(hex(payload.message),
             QStringLiteral("D2173B6170706C69636174696F6E2F766E642E7766612E777363"
                            "100E00371026000101104500087373696474657374"
                            "100300020022100F0002000C1027000C50617373776F726474657374"
                            "10200006FFFFFFFFFFFF"));
}

void TestQuickLaunch::wifiValidation()
{
    QVERIFY(!Ndef::buildWifi(QString(), QStringLiteral("x")).ok);
    QVERIFY(!Ndef::buildWifi(QString(33, QLatin1Char('a')), QStringLiteral("x")).ok);
    QVERIFY(!Ndef::buildWifi(QStringLiteral("ssid"), QString(64, QLatin1Char('a'))).ok);
    QVERIFY(!Ndef::buildWifi(QStringLiteral("ssid"), QStringLiteral("x"),
                             QStringLiteral("WEP")).ok);
    QVERIFY(Ndef::buildWifi(QStringLiteral("ssid"), QString(), QStringLiteral("OPEN")).ok);
}

void TestQuickLaunch::bluetoothOobExact()
{
    const Ndef::LaunchPayload payload = Ndef::buildBluetooth(
                QStringLiteral("26:98:15:06:D8:12"));
    QVERIFY(payload.ok);
    QCOMPARE(hex(payload.message),
             QStringLiteral("D220086170706C69636174696F6E2F766E642E626C7565746F6F74682E65702E6F6F62"
                            "080012D806159826"));
}

void TestQuickLaunch::bluetoothValidation()
{
    QVERIFY(!Ndef::buildBluetooth(QStringLiteral("AA:BB")).ok);
    QVERIFY(!Ndef::buildBluetooth(QStringLiteral("GG:BB:CC:DD:EE:FF")).ok);
}

void TestQuickLaunch::openPrintTag64Bytes()
{
    QByteArray raw(64, '\0');
    for(int i = 0; i < raw.size(); i++)
        raw[i] = static_cast<char>(i);
    const Ndef::LaunchPayload payload = Ndef::buildOpenPrintTag(raw);
    QVERIFY(payload.ok);
    const Ntag::NdefResult parsed = parse(payload);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.first().mime, QStringLiteral("application/vnd.openprinttag"));
    QCOMPARE(parsed.records.first().payload, raw);
    QVERIFY(!Ndef::buildOpenPrintTag(raw.left(63)).ok);
    QVERIFY(!Ndef::buildOpenPrintTag(raw + QByteArray(1, '\0')).ok);
}

void TestQuickLaunch::externalAndAar()
{
    const Ndef::LaunchPayload external = Ndef::buildExternal(
                QStringLiteral("example.com:test"), QByteArray("abc"));
    QVERIFY(external.ok);
    QCOMPARE(hex(external.message),
             QStringLiteral("D410036578616D706C652E636F6D3A74657374616263"));
    const Ntag::NdefResult parsed = parse(external);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.first().tnf, QStringLiteral("0X04"));
    QCOMPARE(parsed.records.first().typeName, QStringLiteral("External"));
    QCOMPARE(parsed.records.first().payload, QByteArray("abc"));

    const Ndef::LaunchPayload aar = Ndef::buildAar(QStringLiteral("com.example.app"));
    QVERIFY(aar.ok);
    const Ntag::NdefResult aarParsed = parse(aar);
    QVERIFY(aarParsed.ok);
    QCOMPARE(aarParsed.records.first().tnf, QStringLiteral("0X04"));
    QCOMPARE(aarParsed.records.first().type, QStringLiteral("android.com:pkg"));
    QCOMPARE(aarParsed.records.first().payload, QByteArray("com.example.app"));
}

void TestQuickLaunch::externalValidation()
{
    QVERIFY(!Ndef::buildExternal(QString(), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildExternal(QStringLiteral("no-colon"), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildExternal(QStringLiteral("bad domain:type"), QByteArray("x")).ok);
    QVERIFY(!Ndef::buildAar(QString()).ok);
}

void TestQuickLaunch::multiRecordFlags()
{
    Ndef::RecordSpec uri;
    uri.tnf = 0x01;
    uri.type = QByteArrayLiteral("U");
    uri.payload = bytes({0x04}) + QByteArray("example.com");
    Ndef::RecordSpec text;
    text.tnf = 0x01;
    text.type = QByteArrayLiteral("T");
    text.payload = bytes({0x82}) + QByteArray("en") + QByteArray("hello");
    QString error;
    const QByteArray message = Ndef::buildMessage(QList<Ndef::RecordSpec>() << uri << text,
                                                  &error);
    QVERIFY2(!message.isEmpty(), qPrintable(error));
    QCOMPARE(static_cast<quint8>(message.at(0)) & 0x80, 0x80);
    QCOMPARE(static_cast<quint8>(message.at(0)) & 0x40, 0x00);
    const int second = 1 + 1 + 1 + uri.type.size() + uri.payload.size();
    QCOMPARE(static_cast<quint8>(message.at(second)) & 0x80, 0x00);
    QCOMPARE(static_cast<quint8>(message.at(second)) & 0x40, 0x40);

    const Ndef::LaunchPayload wrapped = Ndef::buildUri(QStringLiteral("https://example.com"));
    Q_UNUSED(wrapped);
    const Ntag::NdefResult parsed = Ntag::parseNdef(
                Ndef::wrapTlv(message) + QByteArray(1, static_cast<char>(0xFE)));
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.size(), 2);
}

void TestQuickLaunch::shortRecordBoundary()
{
    const Ndef::LaunchPayload sr = Ndef::buildText(QString(252, QLatin1Char('a')),
                                                   QStringLiteral("en"));
    QVERIFY(sr.ok);
    QCOMPARE(static_cast<quint8>(sr.message.at(0)) & 0x10, 0x10);
    QCOMPARE(static_cast<quint8>(sr.message.at(2)), quint8(255));

    const Ndef::LaunchPayload longRecord = Ndef::buildText(QString(253, QLatin1Char('a')),
                                                           QStringLiteral("en"));
    QVERIFY(longRecord.ok);
    QCOMPARE(static_cast<quint8>(longRecord.message.at(0)) & 0x10, 0x00);
    QCOMPARE(int(static_cast<quint8>(longRecord.message.at(2))), 0);
    QCOMPARE(int(static_cast<quint8>(longRecord.message.at(4))), 1);
    QCOMPARE(int(static_cast<quint8>(longRecord.message.at(5))), 0);
    QCOMPARE(int(static_cast<quint8>(longRecord.message.at(6))), int('T'));
    const Ntag::NdefResult parsed = parse(longRecord);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.first().text, QString(253, QLatin1Char('a')));
}

void TestQuickLaunch::tlvBoundaries()
{
    const QByteArray small = Ndef::wrapTlv(QByteArray(254, 'A'));
    QCOMPARE(int(static_cast<quint8>(small.at(0))), 0x03);
    QCOMPARE(int(static_cast<quint8>(small.at(1))), 254);
    QCOMPARE(small.size(), 2 + 254);

    const QByteArray exact = Ndef::wrapTlv(QByteArray(255, 'A'));
    QCOMPARE(int(static_cast<quint8>(exact.at(1))), 0xFF);
    QCOMPARE(int(static_cast<quint8>(exact.at(2))), 0);
    QCOMPARE(int(static_cast<quint8>(exact.at(3))), 255);
    QCOMPARE(exact.size(), 4 + 255);

    const QByteArray bigger = Ndef::wrapTlv(QByteArray(256, 'A'));
    QCOMPARE(int(static_cast<quint8>(bigger.at(2))), 1);
    QCOMPARE(int(static_cast<quint8>(bigger.at(3))), 0);
    QCOMPARE(bigger.size(), 4 + 256);
    QCOMPARE(Ndef::tlvLength(QByteArray(254, 'A')), 2 + 254 + 1);
    QCOMPARE(Ndef::tlvLength(QByteArray(255, 'A')), 4 + 255 + 1);

    // A valid message just over the short length form still parses.
    const Ndef::LaunchPayload payload = Ndef::buildText(QString(249, QLatin1Char('x')),
                                                        QStringLiteral("en"));
    QVERIFY(payload.ok);
    QCOMPARE(static_cast<quint8>(payload.tlv.at(1)), quint8(0xFF));
    QVERIFY(parse(payload).ok);
}

void TestQuickLaunch::oversizeRejected()
{
    QVERIFY(Ndef::wrapTlv(QByteArray(65536, 'x')).isEmpty());
    const Ndef::LaunchPayload payload = Ndef::buildText(QString(70000, QLatin1Char('x')),
                                                        QStringLiteral("en"));
    QVERIFY(!payload.ok);
    QVERIFY(!payload.error.isEmpty());
}

void TestQuickLaunch::invalidUris()
{
    QVERIFY(!Ndef::buildUri(QString()).ok);
    QVERIFY(!Ndef::buildUri(QStringLiteral("example.com")).ok);
    QVERIFY(!Ndef::buildUri(QStringLiteral("https://example.com/a b")).ok);
    QVERIFY(!Ndef::buildUri(QStringLiteral("https://example.com/a\nb")).ok);
    QVERIFY(!Ndef::buildUri(QStringLiteral("https://example.com/\x01")).ok);
}

void TestQuickLaunch::unknownPayloadPreserved()
{
    Ndef::RecordSpec unknown;
    unknown.tnf = 0x05;
    unknown.type = QByteArrayLiteral("x-unknown");
    unknown.payload = bytes({0x00, 0x7F, 0xFF, 'A'});
    QString error;
    const QByteArray message = Ndef::buildMessage(QList<Ndef::RecordSpec>() << unknown,
                                                  &error);
    QVERIFY2(!message.isEmpty(), qPrintable(error));
    const Ntag::NdefResult parsed = Ntag::parseNdef(
                Ndef::wrapTlv(message) + QByteArray(1, static_cast<char>(0xFE)));
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.records.size(), 1);
    QCOMPARE(parsed.records.first().tnf, QStringLiteral("0X05"));
    QCOMPARE(parsed.records.first().type, QStringLiteral("x-unknown"));
    QCOMPARE(parsed.records.first().payload, unknown.payload);
}

void TestQuickLaunch::buildLaunchMappings()
{
    Ndef::LaunchRequest text;
    text.type = Ndef::LaunchType::Text;
    text.primary = QStringLiteral("hello");
    text.subject = QStringLiteral("zh");
    const Ndef::LaunchPayload textPayload = Ndef::buildLaunch(text);
    QVERIFY(textPayload.ok);
    QCOMPARE(parse(textPayload).records.first().text, QStringLiteral("hello"));

    Ndef::LaunchRequest openPrint;
    openPrint.type = Ndef::LaunchType::OpenPrintTag;
    openPrint.body.clear();
    openPrint.extra = hex(QByteArray(64, '\x01'));
    const Ndef::LaunchPayload openPayload = Ndef::buildLaunch(openPrint);
    QVERIFY(openPayload.ok);
    QCOMPARE(parse(openPayload).records.first().payload.size(), 64);

    Ndef::LaunchRequest external;
    external.type = Ndef::LaunchType::External;
    external.primary = QStringLiteral("android.com:pkg");
    external.body = QStringLiteral("com.example.app");
    const Ndef::LaunchPayload externalPayload = Ndef::buildLaunch(external);
    QVERIFY(externalPayload.ok);
    QCOMPARE(parse(externalPayload).records.first().payload,
             QByteArray("com.example.app"));
}

void TestQuickLaunch::simWorkspaceCreateCleanup()
{
    Ntag::DumpWorkspace workspace;
    QVERIFY(workspace.isValid());
    QVERIFY(workspace.simPath().endsWith(QStringLiteral(".bin")));

    QList<QByteArray> pages;
    for(int i = 0; i < 45; i++)
        pages.append(bytes({0xE1, 0x10, 0x12, 0x00}));
    const QByteArray dump = Ntag::buildDump(bytes({0x00, 0x04, 0x04, 0x02,
                                                   0x01, 0x00, 0x0F, 0x03}),
                                            pages);
    QVERIFY(!dump.isEmpty());
    QString error;
    QVERIFY2(workspace.writeSimDump(dump, &error), qPrintable(error));
    QVERIFY(QFile::exists(workspace.simPath()));
    QCOMPARE(QFileInfo(workspace.simPath()).size(), qint64(dump.size()));

    const Ntag::Dump parsed = Ntag::parseDump(dump);
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.pages.size(), 45);

    workspace.clearDump();
    QVERIFY(!QFile::exists(workspace.simPath()));
    QVERIFY(!workspace.writeSimDump(QByteArray(), &error));
}

void TestQuickLaunch::simDumpRoundTrip()
{
    Ntag::DumpWorkspace workspace;
    QVERIFY(workspace.isValid());
    QList<QByteArray> pages;
    for(int i = 0; i < 231; i++)
        pages.append(bytes({i & 0xFF, (i + 1) & 0xFF, (i + 2) & 0xFF, (i + 3) & 0xFF}));
    const QByteArray dump = Ntag::buildDump(bytes({0x00, 0x53, 0x04, 0x02,
                                                   0x01, 0x00, 0x13, 0x03}),
                                            pages);
    QVERIFY(!dump.isEmpty());
    QVERIFY(workspace.writeSimDump(dump));
    QFile file(workspace.simPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray readBack = file.readAll();
    file.close();
    QCOMPARE(readBack, dump);
    const Ntag::Dump parsed = Ntag::parseDump(readBack);
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.pages.size(), 231);
    QCOMPARE(parsed.pages.at(4), pages.at(4));
}

QTEST_GUILESS_MAIN(TestQuickLaunch)
#include "tst_quicklaunch.moc"
