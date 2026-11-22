#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ntag/ntagmodel.h"
#include "ntag/ndefbuilder.h"
#include "ntag/ntagmemory.h"

namespace {

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for(int value : values)
        out.append(static_cast<char>(value));
    return out;
}

QByteArray makeDump(const QByteArray &version8, int pages)
{
    QByteArray file(56, '\0');
    file.replace(0, qMin(8, version8.size()), version8.left(8));
    file[0x0B] = static_cast<char>(pages - 1);
    for(int i = 0; i < pages; i++)
    {
        file.append(static_cast<char>(i));
        file.append(static_cast<char>(i + 1));
        file.append(static_cast<char>(i + 2));
        file.append(static_cast<char>(i + 3));
    }
    return file;
}

} // namespace

class TestNtagModel : public QObject
{
    Q_OBJECT
private slots:
    void versionParses213();
    void versionParsesWithoutLeadingZero();
    void versionRejectsShort();
    void identifyExactByStorage();
    void identifyByCCWhenVersionUnavailable();
    void identifyConflictIsProbable();
    void identifyClientTypeFallback();
    void identifyUnknown();
    void capacity213();
    void capacity215();
    void capacity216();
    void ntag213LastPageIs44();
    void parseCCFields();
    void parseCCInvalidMagic();
    void parseDumpPages();
    void parseDumpRejectsShortFile();
    void parseDumpRejectsUnalignedPayload();
    void parseDumpReportsHeaderMismatch();
    void regionsFor213();
    void regionsOutOfRange();
    void ndefText();
    void ndefUri();
    void ndefMime();
    void ndefExternal();
    void ndefEmptyMessage();
    void ndefNoTlv();
    void ndefTruncatedRecord();
    void realisticCloneDump();
    void pageClasses215();
    void pageClassesUnknown();
    void erasePlanTargetsUserPagesOnly();
    void erasePlanRefusesUnknownModel();
    void erasePlanRefusesIncompleteRead();
    void erasePlanDetectsLockBits();
    void verifyWritePassed();
    void verifyWriteReadOnlyIsException();
    void verifyWriteMismatchFails();
    void verifyWriteUnreadIsIncomplete();
    void verifyWriteIgnoresReadOnlyMismatch();
    void ndefBuilderUrlRecord();
    void ndefBuilderPhone();
    void ndefBuilderSmsEncodesBody();
    void ndefBuilderEmail();
    void ndefBuilderWifiSupported();
    void ndefBuilderLongRecord();
    void ndefBuilderRejectsBadUri();
    void ndefBuilderModelCapacity();
    void memoryConfigure216();
    void memoryEditTracksDirty();
    void memoryRejectsReadOnly();
    void memoryRejectsUnread();
    void memoryEditsAcrossPages();
    void memoryRevertRestoresOriginal();
    void parseHexAcceptsFormats();
    void parseHexRejectsInvalid();
    void buildDumpKeepsPages();
    void buildDumpRejectsInvalidPages();
    void readJobStartsAndTracks();
    void readJobRejectsDuplicateStart();
    void readJobCancelAndFinish();
    void readJobFinishIsIdempotent();
    void writeJobStartsAndTracks();
    void writeJobRejectsDuplicateAndCancel();
    void writeJobRetryAndFailureStates();
    void dumpWorkspaceCleansOnlyItsFiles();
};

void TestNtagModel::versionParses213()
{
    const Ntag::Version v = Ntag::parseVersion(
                bytes({0x00, 0x5A, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03}));
    QVERIFY(v.valid);
    QCOMPARE(v.vendor, quint8(0x5A));
    QCOMPARE(v.storageSize, quint8(0x0F));
    QCOMPARE(v.protocol, quint8(0x03));
}

void TestNtagModel::versionParsesWithoutLeadingZero()
{
    const Ntag::Version v = Ntag::parseVersion(
                bytes({0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03}));
    QVERIFY(v.valid);
    QCOMPARE(v.vendor, quint8(0x04));
    QCOMPARE(v.storageSize, quint8(0x11));
}

void TestNtagModel::versionRejectsShort()
{
    QVERIFY(!Ntag::parseVersion(bytes({0x00, 0x04, 0x02})).valid);
}

void TestNtagModel::identifyExactByStorage()
{
    const Ntag::Version v = Ntag::parseVersion(
                bytes({0x00, 0x53, 0x04, 0x02, 0x01, 0x00, 0x13, 0x03}));
    const Ntag::Capability cap = Ntag::identify(v, Ntag::CC(), QString());
    QCOMPARE(cap.model, Ntag::Model::NTAG216);
    QCOMPARE(cap.confidence, Ntag::Confidence::Exact);
    QCOMPARE(cap.pages, 231);
}

void TestNtagModel::identifyByCCWhenVersionUnavailable()
{
    Ntag::CC cc;
    cc.valid = true;
    cc.size = 0x12;
    const Ntag::Capability cap = Ntag::identify(Ntag::Version(), cc, QString());
    QCOMPARE(cap.model, Ntag::Model::NTAG213);
    QCOMPARE(cap.confidence, Ntag::Confidence::Probable);
}

void TestNtagModel::identifyConflictIsProbable()
{
    const Ntag::Version v = Ntag::parseVersion(
                bytes({0x00, 0x53, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03}));
    Ntag::CC cc;
    cc.valid = true;
    cc.size = 0x6D; // 216 CC
    const Ntag::Capability cap = Ntag::identify(v, cc, QString());
    QCOMPARE(cap.model, Ntag::Model::NTAG215); // version wins
    QCOMPARE(cap.confidence, Ntag::Confidence::Probable);
    QVERIFY(cap.basis.contains(QStringLiteral("conflict"), Qt::CaseInsensitive));
}

void TestNtagModel::identifyClientTypeFallback()
{
    const Ntag::Capability cap = Ntag::identify(Ntag::Version(), Ntag::CC(),
                                                QStringLiteral("NTAG 215"));
    QCOMPARE(cap.model, Ntag::Model::NTAG215);
    QCOMPARE(cap.confidence, Ntag::Confidence::Probable);
}

void TestNtagModel::identifyUnknown()
{
    const Ntag::Version v = Ntag::parseVersion(
                bytes({0x00, 0x04, 0x03, 0x01, 0x00, 0x10, 0x03}));
    const Ntag::Capability cap = Ntag::identify(v, Ntag::CC(), QStringLiteral("NTAG UNKNOWN"));
    QCOMPARE(cap.model, Ntag::Model::Unknown);
    QCOMPARE(cap.pages, 0);
    QCOMPARE(cap.confidence, Ntag::Confidence::Unknown);
}

void TestNtagModel::capacity213()
{
    const Ntag::Capability cap = Ntag::capacityFor(Ntag::Model::NTAG213,
                                                   Ntag::Confidence::Exact, QString());
    QCOMPARE(cap.pages, 45);
    QCOMPARE(cap.userLast, 39);
    QCOMPARE(cap.lockPage, 40);
    QCOMPARE(cap.cfg0, 41);
    QCOMPARE(cap.cfg1, 42);
    QCOMPARE(cap.pwd, 43);
    QCOMPARE(cap.pack, 44);
}

void TestNtagModel::capacity215()
{
    const Ntag::Capability cap = Ntag::capacityFor(Ntag::Model::NTAG215,
                                                   Ntag::Confidence::Exact, QString());
    QCOMPARE(cap.pages, 135);
    QCOMPARE(cap.lockPage, 130);
    QCOMPARE(cap.cfg0, 131);
    QCOMPARE(cap.pack, 134);
}

void TestNtagModel::capacity216()
{
    const Ntag::Capability cap = Ntag::capacityFor(Ntag::Model::NTAG216,
                                                   Ntag::Confidence::Exact, QString());
    QCOMPARE(cap.pages, 231);
    QCOMPARE(cap.lockPage, 226);
    QCOMPARE(cap.cfg0, 227);
    QCOMPARE(cap.pack, 230);
}

void TestNtagModel::ntag213LastPageIs44()
{
    // The client's "Max block is 41" default must not become the layout limit.
    const Ntag::Capability cap = Ntag::capacityFor(Ntag::Model::NTAG213,
                                                   Ntag::Confidence::Exact, QString());
    QCOMPARE(cap.pages - 1, 44);
    QVERIFY(cap.pack == 44);
}

void TestNtagModel::parseCCFields()
{
    const Ntag::CC cc = Ntag::parseCC(bytes({0xE1, 0x10, 0x12, 0x00}));
    QVERIFY(cc.valid);
    QCOMPARE(cc.version, quint8(0x10));
    QCOMPARE(cc.size, quint8(0x12));
    QCOMPARE(cc.ndefCapacity, 144);
    QVERIFY(cc.readWrite);
    QVERIFY(!cc.multiBlock);
}

void TestNtagModel::parseCCInvalidMagic()
{
    QVERIFY(!Ntag::parseCC(bytes({0x00, 0x10, 0x12, 0x00})).valid);
}

void TestNtagModel::parseDumpPages()
{
    const QByteArray file = makeDump(bytes({0x00, 0x53, 0x04, 0x02, 0x01, 0x00, 0x13, 0x03}), 231);
    const Ntag::Dump dump = Ntag::parseDump(file);
    QVERIFY(dump.valid);
    QCOMPARE(dump.pages.size(), 231);
    QCOMPARE(dump.maxPage, quint8(230));
    QCOMPARE(dump.pages.at(4).at(0), char(4));
}

void TestNtagModel::parseDumpRejectsShortFile()
{
    const Ntag::Dump dump = Ntag::parseDump(QByteArray(20, '\0'));
    QVERIFY(!dump.valid);
    QVERIFY(!dump.error.isEmpty());
}

void TestNtagModel::parseDumpRejectsUnalignedPayload()
{
    QByteArray file(56 + 6, '\0');
    const Ntag::Dump dump = Ntag::parseDump(file);
    QVERIFY(!dump.valid);
}

void TestNtagModel::parseDumpReportsHeaderMismatch()
{
    QByteArray file = makeDump(bytes({0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03}), 45);
    file[0x0B] = char(50); // claims more pages than the file holds
    const Ntag::Dump dump = Ntag::parseDump(file);
    QVERIFY(dump.valid);
    QVERIFY(!dump.error.isEmpty());
    QCOMPARE(dump.pages.size(), 45);
}

void TestNtagModel::regionsFor213()
{
    const Ntag::Capability cap = Ntag::capacityFor(Ntag::Model::NTAG213,
                                                   Ntag::Confidence::Exact, QString());
    QVERIFY(Ntag::regionName(cap, 0).contains(QStringLiteral("UID")));
    QVERIFY(Ntag::regionName(cap, 3).contains(QStringLiteral("Capability")));
    QCOMPARE(Ntag::regionName(cap, 20), QStringLiteral("User data"));
    QCOMPARE(Ntag::regionName(cap, 40), QStringLiteral("Dynamic lock"));
    QCOMPARE(Ntag::regionName(cap, 41), QStringLiteral("Configuration 0"));
    QCOMPARE(Ntag::regionName(cap, 44), QStringLiteral("PACK"));
}

void TestNtagModel::regionsOutOfRange()
{
    const Ntag::Capability cap = Ntag::capacityFor(Ntag::Model::NTAG213,
                                                   Ntag::Confidence::Exact, QString());
    QCOMPARE(Ntag::regionName(cap, 45), QStringLiteral("Out of range"));
    QCOMPARE(Ntag::regionName(Ntag::Capability(), 2), QStringLiteral("BCC1 / Internal / Lock"));
}

void TestNtagModel::ndefText()
{
    // TLV 03 09; record D1 01 05 'T' | 00 'e' 'n' 'H' 'i'
    const QByteArray area = bytes({0x03, 0x09, 0xD1, 0x01, 0x05, 'T', 0x82, 'e', 'n', 'H', 'i'});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.records.size(), 1);
    QCOMPARE(result.records.at(0).typeName, QStringLiteral("Text"));
    QCOMPARE(result.records.at(0).text, QStringLiteral("Hi"));
}

void TestNtagModel::ndefUri()
{
    // payload = 0x04 + "example.com" (12 bytes); record = 16 bytes.
    const QByteArray area = bytes({0x03, 0x10, 0xD1, 0x01, 0x0C, 'U',
                                   0x04, 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm',
                                   0xFE});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.records.at(0).uri, QStringLiteral("https://example.com"));
}

void TestNtagModel::ndefMime()
{
    // record = D2 03 03 't' 'x' 't' 'a' 'b' 'c' (9 bytes)
    const QByteArray area = bytes({0x03, 0x09, 0xD2, 0x03, 0x03,
                                   't', 'x', 't', 'a', 'b', 'c'});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.records.at(0).typeName, QStringLiteral("MIME"));
    QCOMPARE(result.records.at(0).mime, QStringLiteral("txt"));
}

void TestNtagModel::ndefExternal()
{
    // record = D4 05 02 'e' 'x' '0' '1' '2' 00 01 (10 bytes)
    const QByteArray area = bytes({0x03, 0x0A, 0xD4, 0x05, 0x02,
                                   'e', 'x', '0', '1', '2', 0x00, 0x01});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.records.at(0).typeName, QStringLiteral("External"));
}

void TestNtagModel::ndefEmptyMessage()
{
    const QByteArray area = bytes({0x03, 0x00, 0xFE, 0x00});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("no NDEF")));
}

void TestNtagModel::ndefNoTlv()
{
    const QByteArray area = bytes({0xA5, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void TestNtagModel::ndefTruncatedRecord()
{
    const QByteArray area = bytes({0x03, 0x05, 0xD1, 0x01, 0x20, 'T', 0x00});
    const Ntag::NdefResult result = Ntag::parseNdef(area);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void TestNtagModel::realisticCloneDump()
{
    const QByteArray file = makeDump(bytes({0x00, 0x5A, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03}), 45);
    const Ntag::Dump dump = Ntag::parseDump(file);
    QVERIFY(dump.valid);
    QCOMPARE(dump.pages.size(), 45);
    const Ntag::Version version = Ntag::parseVersion(dump.versionRaw);
    const Ntag::CC cc = Ntag::parseCC(bytes({0xE1, 0x10, 0x12, 0x00}));
    const Ntag::Capability cap = Ntag::identify(version, cc, QStringLiteral("NTAG UNKNOWN ( Gen 1b )"));
    QCOMPARE(cap.model, Ntag::Model::NTAG213);
    QCOMPARE(cap.confidence, Ntag::Confidence::Exact);
    QCOMPARE(Ntag::regionName(cap, 44), QStringLiteral("PACK"));
}

void TestNtagModel::pageClasses215()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QCOMPARE(Ntag::classifyPage(cap, 0), Ntag::PageClass::Manufacturer);
    QCOMPARE(Ntag::classifyPage(cap, 1), Ntag::PageClass::UID);
    QCOMPARE(Ntag::classifyPage(cap, 2), Ntag::PageClass::Lock);
    QCOMPARE(Ntag::classifyPage(cap, 3), Ntag::PageClass::Config);
    QCOMPARE(Ntag::classifyPage(cap, 4), Ntag::PageClass::Writable);
    QCOMPARE(Ntag::classifyPage(cap, 129), Ntag::PageClass::Writable);
    QCOMPARE(Ntag::classifyPage(cap, 130), Ntag::PageClass::Lock);
    QCOMPARE(Ntag::classifyPage(cap, 131), Ntag::PageClass::Config);
    QCOMPARE(Ntag::classifyPage(cap, 132), Ntag::PageClass::Config);
    QCOMPARE(Ntag::classifyPage(cap, 133), Ntag::PageClass::Security);
    QCOMPARE(Ntag::classifyPage(cap, 134), Ntag::PageClass::Security);
    QCOMPARE(Ntag::classifyPage(cap, 135), Ntag::PageClass::NotApplicable);
}

void TestNtagModel::pageClassesUnknown()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::Unknown, Ntag::Confidence::Unknown, QString());
    QCOMPARE(Ntag::classifyPage(cap, 0), Ntag::PageClass::Unknown);
    QCOMPARE(Ntag::classifyPage(cap, 4), Ntag::PageClass::Unknown);
}

void TestNtagModel::erasePlanTargetsUserPagesOnly()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    pages[4] = bytes({0x03, 0x10, 0xD1, 0x01});
    pages[129] = bytes({0xAA, 0xBB, 0xCC, 0xDD});
    pages[130] = bytes({0x01, 0x02, 0x03, 0x00}); // dynamic lock bits

    const Ntag::ErasePlan plan = Ntag::planErase(cap, pages);
    QVERIFY(plan.valid);
    QCOMPARE(plan.clearPages, QList<int>({4, 129}));
    QVERIFY(plan.blankPages.contains(5));
    QVERIFY(!plan.clearPages.contains(130));
    QVERIFY(!plan.clearPages.contains(133));
    QVERIFY(plan.lockBitsPresent);
    QCOMPARE(plan.dynamicLock, bytes({0x01, 0x02, 0x03}));
}

void TestNtagModel::erasePlanRefusesUnknownModel()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::Unknown, Ntag::Confidence::Unknown, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < 135; i++)
        pages.append(QByteArray(4, '\0'));
    const Ntag::ErasePlan plan = Ntag::planErase(cap, pages);
    QVERIFY(!plan.valid);
    QVERIFY(!plan.error.isEmpty());
}

void TestNtagModel::erasePlanRefusesIncompleteRead()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < 100; i++)
        pages.append(QByteArray(4, '\0'));
    const Ntag::ErasePlan plan = Ntag::planErase(cap, pages);
    QVERIFY(!plan.valid);
    QVERIFY(plan.error.contains(QStringLiteral("incomplete")));
}

void TestNtagModel::erasePlanDetectsLockBits()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG213, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    pages[2] = bytes({0x88, 0x00, 0x0F, 0x00}); // static lock bits set
    const Ntag::ErasePlan plan = Ntag::planErase(cap, pages);
    QVERIFY(plan.valid);
    QVERIFY(plan.lockBitsPresent);
    QCOMPARE(plan.staticLock, bytes({0x0F, 0x00}));
}

void TestNtagModel::verifyWritePassed()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    QVector<Ntag::PageState> states;
    states.fill(Ntag::PageState::Read, cap.pages);
    pages[4] = bytes({0x03, 0x08, 0xD1, 0x01});
    pages[5] = bytes({0x55, 0x04, 't', 'e'});

    QMap<int, QByteArray> expected;
    expected.insert(4, pages.at(4));
    expected.insert(5, pages.at(5));
    const Ntag::VerifyReport report = Ntag::verifyWrite(cap, expected, pages, states);
    QCOMPARE(report.status, Ntag::VerifyStatus::Passed);
    QCOMPARE(report.writableTargets, 2);
    QCOMPARE(report.writablePassed, 2);
    QCOMPARE(report.writableFailed, 0);
}

void TestNtagModel::verifyWriteReadOnlyIsException()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    QVector<Ntag::PageState> states;
    states.fill(Ntag::PageState::Read, cap.pages);
    pages[4] = bytes({0x03, 0x08, 0xD1, 0x01});

    QMap<int, QByteArray> expected;
    expected.insert(4, pages.at(4));
    expected.insert(0, bytes({0x04, 0x00, 0x00, 0x00})); // never writable
    const Ntag::VerifyReport report = Ntag::verifyWrite(cap, expected, pages, states);
    QCOMPARE(report.status, Ntag::VerifyStatus::PassedWithExceptions);
    QCOMPARE(report.writableTargets, 1);
    QCOMPARE(report.writablePassed, 1);
}

void TestNtagModel::verifyWriteMismatchFails()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    QVector<Ntag::PageState> states;
    states.fill(Ntag::PageState::Read, cap.pages);
    pages[4] = bytes({0xAA, 0xAA, 0xAA, 0xAA});

    QMap<int, QByteArray> expected;
    expected.insert(4, bytes({0xBB, 0xBB, 0xBB, 0xBB}));
    const Ntag::VerifyReport report = Ntag::verifyWrite(cap, expected, pages, states);
    QCOMPARE(report.status, Ntag::VerifyStatus::Failed);
    QCOMPARE(report.writableFailed, 1);
    QCOMPARE(report.checks.first().actual, bytes({0xAA, 0xAA, 0xAA, 0xAA}));
}

void TestNtagModel::verifyWriteUnreadIsIncomplete()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    QVector<Ntag::PageState> states;
    states.fill(Ntag::PageState::Read, cap.pages);
    states[5] = Ntag::PageState::Failed;

    QMap<int, QByteArray> expected;
    expected.insert(4, QByteArray(4, '\0'));
    expected.insert(5, bytes({0x01, 0x02, 0x03, 0x04}));
    const Ntag::VerifyReport report = Ntag::verifyWrite(cap, expected, pages, states);
    QCOMPARE(report.status, Ntag::VerifyStatus::Incomplete);
    QCOMPARE(report.writablePassed, 1);
    QCOMPARE(report.unchecked, 1);
    QCOMPARE(report.checks.at(1).verdict, Ntag::PageVerdict::Unchecked);
}

void TestNtagModel::verifyWriteIgnoresReadOnlyMismatch()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    QList<QByteArray> pages;
    for(int i = 0; i < cap.pages; i++)
        pages.append(QByteArray(4, '\0'));
    QVector<Ntag::PageState> states;
    states.fill(Ntag::PageState::Read, cap.pages);
    pages[3] = bytes({0xE1, 0x10, 0x3E, 0x00});
    pages[4] = bytes({0x01, 0x02, 0x03, 0x04});

    QMap<int, QByteArray> expected;
    expected.insert(3, bytes({0xFF, 0xFF, 0xFF, 0xFF})); // should never be written
    expected.insert(4, pages.at(4));
    const Ntag::VerifyReport report = Ntag::verifyWrite(cap, expected, pages, states);
    QCOMPARE(report.status, Ntag::VerifyStatus::PassedWithExceptions);
    QCOMPARE(report.writableFailed, 0);
    QCOMPARE(report.checks.first().verdict, Ntag::PageVerdict::Preserved);
}

void TestNtagModel::ndefBuilderUrlRecord()
{
    const Ndef::LaunchPayload payload = Ndef::buildUri(
                QStringLiteral("https://example.com"));
    QVERIFY(payload.ok);
    QCOMPARE(payload.message.left(5), bytes({0xD1, 0x01, 0x0C, 'U', 0x04}));
    QCOMPARE(payload.message.mid(5), QByteArray("example.com"));
    QCOMPARE(payload.requiredBytes, 2 + payload.message.size() + 1);
    QCOMPARE(payload.userArea.size() % 4, 0);
    QCOMPARE(payload.userArea.at(payload.tlv.size()), char(0xFE));
}

void TestNtagModel::ndefBuilderPhone()
{
    Ndef::LaunchRequest request;
    request.type = Ndef::LaunchType::Phone;
    request.primary = QStringLiteral("+1 (555) 0100");
    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    QVERIFY(payload.ok);
    QCOMPARE(payload.uri, QStringLiteral("tel:+15550100"));
    QCOMPARE(static_cast<quint8>(payload.message.at(4)), quint8(0x05));
}

void TestNtagModel::ndefBuilderSmsEncodesBody()
{
    Ndef::LaunchRequest request;
    request.type = Ndef::LaunchType::Sms;
    request.primary = QStringLiteral("+15550100");
    request.body = QStringLiteral("Hello World");
    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    QVERIFY(payload.ok);
    QCOMPARE(payload.uri, QStringLiteral("sms:+15550100?body=Hello%20World"));
    QCOMPARE(static_cast<quint8>(payload.message.at(4)), quint8(0x00));
}

void TestNtagModel::ndefBuilderEmail()
{
    Ndef::LaunchRequest request;
    request.type = Ndef::LaunchType::Email;
    request.primary = QStringLiteral("user@example.com");
    request.subject = QStringLiteral("Hi there");
    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    QVERIFY(payload.ok);
    QVERIFY(payload.uri.startsWith(QStringLiteral("mailto:user@example.com?subject=")));
    QCOMPARE(static_cast<quint8>(payload.message.at(4)), quint8(0x06));
}

void TestNtagModel::ndefBuilderWifiSupported()
{
    Ndef::LaunchRequest request;
    request.type = Ndef::LaunchType::Wifi;
    request.primary = QStringLiteral("SSID");
    request.subject = QStringLiteral("password");
    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    QVERIFY(payload.ok);
    QCOMPARE(static_cast<quint8>(payload.message.at(0)) & 0x07, quint8(0x02));
    QVERIFY(payload.message.contains("application/vnd.wfa.wsc"));
    QVERIFY(!payload.message.isEmpty());
}

void TestNtagModel::ndefBuilderLongRecord()
{
    const QString uri = QStringLiteral("https://") + QString(300, QLatin1Char('a'));
    const Ndef::LaunchPayload payload = Ndef::buildUri(uri);
    QVERIFY(payload.ok);
    // SR=0 for payloads above 255 bytes; 4-byte length field follows.
    QCOMPARE(static_cast<quint8>(payload.message.at(0)) & 0x10, 0);
    QCOMPARE(static_cast<quint8>(payload.message.at(0)) & 0xC0, 0xC0);
    QCOMPARE(payload.requiredBytes, 4 + payload.message.size() + 1);
    QCOMPARE(payload.userArea.size() % 4, 0);
}

void TestNtagModel::ndefBuilderRejectsBadUri()
{
    QVERIFY(!Ndef::buildUri(QString()).ok);
    QVERIFY(!Ndef::buildUri(QStringLiteral("example.com")).ok);
    QVERIFY(!Ndef::buildUri(QStringLiteral("https://exa mple.com")).ok);
}

void TestNtagModel::ndefBuilderModelCapacity()
{
    const Ntag::CC cc213 = Ntag::parseCC(bytes({0xE1, 0x10, 0x12, 0x00}));
    const Ntag::CC cc215 = Ntag::parseCC(bytes({0xE1, 0x10, 0x3E, 0x00}));
    const Ntag::CC cc216 = Ntag::parseCC(bytes({0xE1, 0x10, 0x6D, 0x00}));
    QCOMPARE(cc213.ndefCapacity, 144);
    QCOMPARE(cc215.ndefCapacity, 496);
    QCOMPARE(cc216.ndefCapacity, 872);

    const Ndef::LaunchPayload small = Ndef::buildUri(
                QStringLiteral("https://example.com/reset?id=42"));
    QVERIFY(small.requiredBytes <= cc213.ndefCapacity);

    const Ndef::LaunchPayload medium = Ndef::buildUri(
                QStringLiteral("https://") + QString(200, QLatin1Char('a')));
    QVERIFY(medium.requiredBytes > cc213.ndefCapacity);
    QVERIFY(medium.requiredBytes <= cc215.ndefCapacity);

    const Ndef::LaunchPayload large = Ndef::buildUri(
                QStringLiteral("https://") + QString(600, QLatin1Char('a')));
    QVERIFY(large.requiredBytes > cc215.ndefCapacity);
    QVERIFY(large.requiredBytes <= cc216.ndefCapacity);
}

void TestNtagModel::memoryConfigure216()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG216, Ntag::Confidence::Exact, QString());
    Ntag::MemoryImage mem;
    mem.configure(cap);
    QCOMPARE(mem.pageCount(), 231);
    QVERIFY(mem.valid());
    QCOMPARE(mem.page(0)->pageClass, Ntag::PageClass::Manufacturer);
    QCOMPARE(mem.page(4)->pageClass, Ntag::PageClass::Writable);
    QVERIFY(mem.page(4)->writable);
    QVERIFY(!mem.page(3)->writable);
    QCOMPARE(mem.page(226)->pageClass, Ntag::PageClass::Lock);
    QCOMPARE(mem.page(227)->pageClass, Ntag::PageClass::Config);
    QCOMPARE(mem.page(229)->pageClass, Ntag::PageClass::Security);
}

void TestNtagModel::memoryEditTracksDirty()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG216, Ntag::Confidence::Exact, QString());
    Ntag::MemoryImage mem;
    mem.configure(cap);
    QVERIFY(mem.setPageData(4, bytes({0x01, 0x02, 0x03, 0x04})));
    QVERIFY(!mem.page(4)->dirty());
    QString error;
    QVERIFY(mem.editByte(4, 2, 0xAA, &error));
    QVERIFY(mem.page(4)->dirty());
    QCOMPARE(mem.page(4)->original, bytes({0x01, 0x02, 0x03, 0x04}));
    QCOMPARE(mem.page(4)->edited, bytes({0x01, 0x02, 0xAA, 0x04}));
    QCOMPARE(mem.dirtyPages(), QList<int>({4}));
    QVERIFY(mem.page(4)->writeState == Ntag::WriteState::None);
}

void TestNtagModel::memoryRejectsReadOnly()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG216, Ntag::Confidence::Exact, QString());
    Ntag::MemoryImage mem;
    mem.configure(cap);
    QVERIFY(mem.setPageData(3, bytes({0xE1, 0x10, 0x6D, 0x00})));
    QString error;
    QVERIFY(!mem.editByte(3, 0, 0x00, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(mem.page(3)->edited, bytes({0xE1, 0x10, 0x6D, 0x00}));
    QVERIFY(mem.dirtyPages().isEmpty());
}

void TestNtagModel::memoryRejectsUnread()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG216, Ntag::Confidence::Exact, QString());
    Ntag::MemoryImage mem;
    mem.configure(cap);
    QString error;
    QVERIFY(!mem.editByte(10, 0, 0x01, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!mem.page(10)->readable());
}

void TestNtagModel::memoryEditsAcrossPages()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG215, Ntag::Confidence::Exact, QString());
    Ntag::MemoryImage mem;
    mem.configure(cap);
    QVERIFY(mem.setPageData(4, bytes({0, 0, 0, 0})));
    QVERIFY(mem.setPageData(5, bytes({0, 0, 0, 0})));
    QString error;
    QVERIFY(mem.setBytes(4, 2, bytes({0xAA, 0xBB, 0xCC, 0xDD}), &error));
    QCOMPARE(mem.page(4)->edited, bytes({0, 0, 0xAA, 0xBB}));
    QCOMPARE(mem.page(5)->edited, bytes({0xCC, 0xDD, 0, 0}));
    QCOMPARE(mem.dirtyPages(), QList<int>({4, 5}));

    // A range crossing an unread page must change nothing.
    QVERIFY(!mem.setBytes(5, 3, bytes({1, 2, 3}), &error));
    QCOMPARE(mem.page(5)->edited, bytes({0xCC, 0xDD, 0, 0}));
    QVERIFY(!mem.page(6)->readable());
}

void TestNtagModel::memoryRevertRestoresOriginal()
{
    const Ntag::Capability cap = Ntag::capacityFor(
                Ntag::Model::NTAG216, Ntag::Confidence::Exact, QString());
    Ntag::MemoryImage mem;
    mem.configure(cap);
    QVERIFY(mem.setPageData(4, bytes({1, 2, 3, 4})));
    QVERIFY(mem.editByte(4, 0, 9));
    QVERIFY(mem.page(4)->dirty());
    mem.revertPage(4);
    QVERIFY(!mem.page(4)->dirty());
    QCOMPARE(mem.page(4)->original, bytes({1, 2, 3, 4}));
    QCOMPARE(mem.page(4)->edited, bytes({1, 2, 3, 4}));
}

void TestNtagModel::parseHexAcceptsFormats()
{
    QByteArray bytes;
    QString error;
    QVERIFY(Ntag::parseHexBytes(QStringLiteral("E1 10 3E 00"), &bytes, &error));
    QCOMPARE(bytes, QByteArray::fromHex("E1103E00"));
    QVERIFY(Ntag::parseHexBytes(QStringLiteral("e1103e00"), &bytes, &error));
    QCOMPARE(bytes, QByteArray::fromHex("E1103E00"));
    QVERIFY(Ntag::parseHexBytes(QStringLiteral("E1,10:3E-00"), &bytes, &error));
    QCOMPARE(bytes, QByteArray::fromHex("E1103E00"));
    QVERIFY(Ntag::parseHexBytes(QStringLiteral("0xE1 0x10"), &bytes, &error));
    QCOMPARE(bytes, QByteArray::fromHex("E110"));
}

void TestNtagModel::parseHexRejectsInvalid()
{
    QByteArray bytes;
    QString error;
    QVERIFY(!Ntag::parseHexBytes(QString(), &bytes, &error));
    QVERIFY(!Ntag::parseHexBytes(QStringLiteral("E1 1"), &bytes, &error));
    QVERIFY(!Ntag::parseHexBytes(QStringLiteral("E1 ZZ"), &bytes, &error));
}

void TestNtagModel::buildDumpKeepsPages()
{
    QList<QByteArray> pages;
    for(int i = 0; i < 45; i++)
        pages.append(bytes({i, i + 1, i + 2, i + 3}));
    const QByteArray dump = Ntag::buildDump(
                QByteArray::fromHex("0004040201000F03"), pages);
    QCOMPARE(dump.size(), 56 + 45 * 4);
    QCOMPARE(static_cast<quint8>(dump.at(0x0B)), quint8(44));
    QCOMPARE(dump.mid(56, 8), pages.at(0) + pages.at(1));
}

void TestNtagModel::buildDumpRejectsInvalidPages()
{
    QList<QByteArray> pages;
    pages.append(bytes({1, 2, 3}));
    QVERIFY(Ntag::buildDump(QByteArray(), pages).isEmpty());

    QList<QByteArray> many;
    for(int i = 0; i < 257; i++)
        many.append(QByteArray(4, '\0'));
    QVERIFY(Ntag::buildDump(QByteArray(), many).isEmpty());
}

void TestNtagModel::readJobStartsAndTracks()
{
    Ntag::ReadJobState job;
    QVERIFY(job.start(Ntag::ReadJobMode::DumpChunks, 231));
    QVERIFY(job.busy);
    QCOMPARE(job.total, 231);
    job.success(32);
    job.success(31);
    job.failure(2);
    QCOMPARE(job.done, 63);
    QCOMPARE(job.failures, 2);
    job.success(1000);
    QCOMPARE(job.done, 231);
}

void TestNtagModel::readJobRejectsDuplicateStart()
{
    Ntag::ReadJobState job;
    QVERIFY(job.start(Ntag::ReadJobMode::Pages, 3));
    QVERIFY(!job.start(Ntag::ReadJobMode::DumpChunks, 231));
    QCOMPARE(job.mode, Ntag::ReadJobMode::Pages);
    QCOMPARE(job.total, 3);
}

void TestNtagModel::readJobCancelAndFinish()
{
    Ntag::ReadJobState job;
    QVERIFY(job.start(Ntag::ReadJobMode::DumpChunks, 231));
    job.requestCancel();
    QVERIFY(job.cancelled);
    job.finish();
    QVERIFY(!job.busy);
    QVERIFY(!job.cancelled);
    QCOMPARE(job.done, 0);
    QCOMPARE(job.total, 0);
    QCOMPARE(job.failures, 0);
    QVERIFY(job.start(Ntag::ReadJobMode::Pages, 1));
}

void TestNtagModel::readJobFinishIsIdempotent()
{
    Ntag::ReadJobState job;
    QVERIFY(job.start(Ntag::ReadJobMode::Pages, 2));
    job.success();
    job.finish();
    job.finish();
    QVERIFY(!job.busy);
    QCOMPARE(job.done, 0);
    QVERIFY(job.start(Ntag::ReadJobMode::DumpChunks, 231));
    job.requestCancel();
    job.finish();
    QVERIFY(!job.cancelled);
}

void TestNtagModel::writeJobStartsAndTracks()
{
    Ntag::WriteJobState job;
    QVERIFY(job.start(Ntag::WriteJobMode::WritePages, 3));
    QVERIFY(job.busy);
    QCOMPARE(job.mode, Ntag::WriteJobMode::WritePages);
    QCOMPARE(job.total, 3);
    QCOMPARE(job.phase, Ntag::WriteJobPhase::Idle);
    job.beginPage(4);
    QCOMPARE(job.currentPage, 4);
    job.phase = Ntag::WriteJobPhase::Writing;
    job.success();
    job.success();
    QCOMPARE(job.written, 2);
    job.failure();
    QCOMPARE(job.failures, 1);
    QCOMPARE(job.written, 2);
    job.finish();
    QVERIFY(!job.busy);
    QCOMPARE(job.total, 0);
    QCOMPARE(job.written, 0);
    QCOMPARE(job.failures, 0);
}

void TestNtagModel::writeJobRejectsDuplicateAndCancel()
{
    Ntag::WriteJobState job;
    QVERIFY(!job.start(Ntag::WriteJobMode::None, 1));
    QVERIFY(!job.start(Ntag::WriteJobMode::WritePages, 0));
    QVERIFY(job.start(Ntag::WriteJobMode::Erase, 10));
    QVERIFY(!job.start(Ntag::WriteJobMode::Verify, 2));
    job.requestCancel();
    QVERIFY(job.cancelled);
    job.finish();
    QVERIFY(!job.cancelled);
    QVERIFY(job.start(Ntag::WriteJobMode::Verify, 1));
    QCOMPARE(job.mode, Ntag::WriteJobMode::Verify);
}

void TestNtagModel::writeJobRetryAndFailureStates()
{
    Ntag::WriteJobState job;
    QVERIFY(job.start(Ntag::WriteJobMode::WritePages, 2));
    job.beginPage(4);
    QVERIFY(!job.retryUsed);
    job.retryUsed = true;
    QVERIFY(job.retryUsed);
    // A successful page clears the retry marker before the next page starts.
    job.success();
    QVERIFY(!job.retryUsed);
    // A failed page does not count as written and can be stopped immediately.
    job.failure();
    QCOMPARE(job.written, 1);
    QCOMPARE(job.failures, 1);
    job.finish();
    QCOMPARE(job.phase, Ntag::WriteJobPhase::Idle);
    QCOMPARE(job.currentPage, -1);
}

void TestNtagModel::dumpWorkspaceCleansOnlyItsFiles()
{
    Ntag::DumpWorkspace first;
    Ntag::DumpWorkspace second;
    QVERIFY2(first.isValid(), qPrintable(QDir::tempPath()));
    QVERIFY(second.isValid());
    QVERIFY(first.path() != second.path());

    QFile bin(first.path());
    QVERIFY(bin.open(QIODevice::WriteOnly));
    bin.write("dump");
    bin.close();
    QFile json(first.sidecarPath());
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write("{}");
    json.close();
    QFile suffix(QDir(QFileInfo(first.sidecarPath()).absolutePath())
                 .filePath(QStringLiteral("pm3gui-ntag-dump-001.json")));
    QVERIFY(suffix.open(QIODevice::WriteOnly));
    suffix.write("{}");
    suffix.close();

    const QString userPath = QDir::currentPath() + QStringLiteral("/pm3gui-ntag-user-dump.bin");
    QFile user(userPath);
    QVERIFY(user.open(QIODevice::WriteOnly));
    user.write("user");
    user.close();

    first.clearDump();
    QVERIFY(!QFile::exists(first.path()));
    QVERIFY(!QFile::exists(first.sidecarPath()));
    QVERIFY(!QFile::exists(suffix.fileName()));
    QVERIFY(QFile::exists(userPath));
    QFile::remove(userPath);

    QFile secondBin(second.path());
    QVERIFY(secondBin.open(QIODevice::WriteOnly));
    secondBin.write("dump");
    secondBin.close();
    second.clearDump();
    QVERIFY(!QFile::exists(second.path()));
}

QTEST_GUILESS_MAIN(TestNtagModel)
#include "tst_ntagmodel.moc"



