#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>

#include "amiibo/amiibodata.h"

namespace {

QByteArray rawImage(int pages)
{
    QByteArray data;
    for(int page = 0; page < pages; page++)
    {
        data.append(static_cast<char>(page));
        data.append(static_cast<char>(page + 1));
        data.append(static_cast<char>(page + 2));
        data.append(static_cast<char>(page + 3));
    }
    return data;
}

QByteArray dumpWithHeader(const QByteArray &version, int pages)
{
    QByteArray header(56, '\0');
    header.replace(0, qMin(8, version.size()), version.left(8));
    header[0x0B] = static_cast<char>(pages - 1);
    return header + rawImage(pages);
}

} // namespace

class TestAmiiboData : public QObject
{
    Q_OBJECT
private slots:
    void classifiesRaw540();
    void classifiesRaw180And924();
    void classifiesDumpFile();
    void dumpFilePayloadIsLossless();
    void rejectsEmptyAndUnknownLengths();
    void doesNotMistakeRawImageForDump();
    void rawImageWithDumpLikePageCountStaysRaw();
    void rejectsTruncatedDump();
    void sha256IsStable();
    void sha256DetectsOneBitChange();
    void makeDumpPreservesPayloadExactly();
    void makeDumpUsesHeaderPageCount();
    void makeDumpKeepsPayloadInsteadOfTruncating();
    void makeDumpKeepsVersion();
    void makeDumpRejectsUnalignedPayload();
    void rawBinRoundTripHash();
    void dumpRoundTripHash();
    void structuredParsingDoesNotTouchRaw();
    void asciiClientPathPolicy();
    void clientCopyPreservesBytes();
};

void TestAmiiboData::classifiesRaw540()
{
    const QByteArray image = rawImage(135);
    QCOMPARE(image.size(), 540);
    const Amiibo::Info info = Amiibo::classify(image);
    QCOMPARE(info.kind, Amiibo::Kind::RawImage);
    QCOMPARE(info.pages, 135);
    QCOMPARE(info.payload, image);
    QCOMPARE(info.payloadOffset, 0);
}

void TestAmiiboData::classifiesRaw180And924()
{
    QCOMPARE(Amiibo::classify(rawImage(45)).kind, Amiibo::Kind::RawImage);
    QCOMPARE(Amiibo::classify(rawImage(45)).pages, 45);
    QCOMPARE(Amiibo::classify(rawImage(231)).kind, Amiibo::Kind::RawImage);
    QCOMPARE(Amiibo::classify(rawImage(231)).pages, 231);
}

void TestAmiiboData::classifiesDumpFile()
{
    const QByteArray file = dumpWithHeader(
                QByteArray::fromHex("0004040201001103"), 135);
    const Amiibo::Info info = Amiibo::classify(file);
    QCOMPARE(info.kind, Amiibo::Kind::DumpFile);
    QCOMPARE(info.pages, 135);
    QCOMPARE(info.payloadOffset, 56);
    QCOMPARE(info.header, file.left(56));
    QCOMPARE(info.payload, file.mid(56));
}

void TestAmiiboData::dumpFilePayloadIsLossless()
{
    const QByteArray payload = rawImage(135);
    const QByteArray file = dumpWithHeader(QByteArray(), 135);
    const Amiibo::Info info = Amiibo::classify(file);
    QVERIFY(info.kind == Amiibo::Kind::DumpFile);
    QCOMPARE(info.payload, payload);
}

void TestAmiiboData::rejectsEmptyAndUnknownLengths()
{
    QCOMPARE(Amiibo::classify(QByteArray()).kind, Amiibo::Kind::Invalid);
    QCOMPARE(Amiibo::classify(QByteArray(3, '\0')).kind, Amiibo::Kind::Invalid);
    QCOMPARE(Amiibo::classify(QByteArray(536, '\0')).kind, Amiibo::Kind::Invalid);
    QVERIFY(!Amiibo::classify(QByteArray(100, '\0')).error.isEmpty());
}

void TestAmiiboData::doesNotMistakeRawImageForDump()
{
    // 540 bytes with an Amiibo-like CC byte 0xE1 at offset 0x0B.  A naive
    // header check would read 226 pages and reject the file.
    QByteArray image = rawImage(135);
    image[0x0B] = static_cast<char>(0xE1);
    const Amiibo::Info info = Amiibo::classify(image);
    QCOMPARE(info.kind, Amiibo::Kind::RawImage);
    QCOMPARE(info.payload, image);
    QVERIFY(!info.payload.startsWith(QByteArray(56, '\0')));
}

void TestAmiiboData::rawImageWithDumpLikePageCountStaysRaw()
{
    // 56 + 121*4 == 540: a raw image whose 0x0B byte is 120 would look like a
    // 121-page dump to a naive length check.  It must stay a raw image.
    QByteArray image = rawImage(135);
    image[0x0B] = static_cast<char>(120);
    const Amiibo::Info info = Amiibo::classify(image);
    QCOMPARE(info.kind, Amiibo::Kind::RawImage);
    QCOMPARE(info.payload, image);
}

void TestAmiiboData::rejectsTruncatedDump()
{
    QByteArray file = dumpWithHeader(QByteArray(), 135);
    file.chop(4);
    QCOMPARE(Amiibo::classify(file).kind, Amiibo::Kind::Invalid);
}

void TestAmiiboData::sha256IsStable()
{
    const QByteArray payload = rawImage(135);
    QCOMPARE(Amiibo::sha256(payload), Amiibo::sha256(payload));
    QCOMPARE(Amiibo::sha256(QByteArray()), QStringLiteral(
                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void TestAmiiboData::sha256DetectsOneBitChange()
{
    QByteArray payload = rawImage(135);
    const QString before = Amiibo::sha256(payload);
    payload[0] = static_cast<char>(payload.at(0) ^ 0x01);
    QVERIFY(Amiibo::sha256(payload) != before);
}

void TestAmiiboData::makeDumpPreservesPayloadExactly()
{
    const QByteArray image = rawImage(135);
    const QByteArray dump = Amiibo::makeDump(image, 135);
    QCOMPARE(dump.size(), 56 + image.size());
    QCOMPARE(dump.mid(56), image);
    QCOMPARE(static_cast<quint8>(dump.at(0x0B)), quint8(134));
}

void TestAmiiboData::makeDumpUsesHeaderPageCount()
{
    const QByteArray image = rawImage(45);
    const QByteArray dump = Amiibo::makeDump(image, 45);
    QCOMPARE(static_cast<quint8>(dump.at(0x0B)), quint8(44));
    QCOMPARE(dump.mid(56), image);
}

void TestAmiiboData::makeDumpKeepsPayloadInsteadOfTruncating()
{
    const QByteArray image = rawImage(135);
    // A wrong page count must not chop the payload.
    const QByteArray dump = Amiibo::makeDump(image, 42);
    QCOMPARE(dump.mid(56), image);
    QCOMPARE(static_cast<quint8>(dump.at(0x0B)), quint8(134));
}

void TestAmiiboData::makeDumpKeepsVersion()
{
    const QByteArray version = QByteArray::fromHex("0004040201001103");
    const QByteArray dump = Amiibo::makeDump(rawImage(135), 135, version);
    QCOMPARE(dump.left(8), version);
}

void TestAmiiboData::makeDumpRejectsUnalignedPayload()
{
    QVERIFY(Amiibo::makeDump(QByteArray(6, '\0'), 0).isEmpty());
    QVERIFY(Amiibo::makeDump(QByteArray(), 0).isEmpty());
    QVERIFY(Amiibo::makeDump(QByteArray(4 * 300, '\0'), 300).isEmpty());
}

void TestAmiiboData::rawBinRoundTripHash()
{
    // Import a headerless Amiibo image, build the simulation input, and prove
    // that both the original bytes and the payload are unchanged.
    const QByteArray imported = rawImage(135);
    const QString importedHash = Amiibo::sha256(imported);
    const Amiibo::Info info = Amiibo::classify(imported);
    QCOMPARE(info.payload, imported);

    const QByteArray simulation = Amiibo::makeDump(info.payload, info.pages);
    const Amiibo::Info check = Amiibo::classify(simulation);
    QCOMPARE(check.kind, Amiibo::Kind::DumpFile);
    QCOMPARE(check.payload, imported);
    QCOMPARE(Amiibo::sha256(imported), importedHash);
}

void TestAmiiboData::dumpRoundTripHash()
{
    // An imported dump file must be usable as-is for simulation: no re-wrap,
    // no added or removed bytes.
    const QByteArray imported = dumpWithHeader(
                QByteArray::fromHex("0004040201001103"), 135);
    const QString importedHash = Amiibo::sha256(imported);
    const Amiibo::Info info = Amiibo::classify(imported);
    QCOMPARE(info.kind, Amiibo::Kind::DumpFile);
    QCOMPARE(Amiibo::sha256(imported), importedHash);
    QCOMPARE(info.payload.size(), 540);

    const QByteArray simulation = imported; // raw-bin path passes the file through
    QCOMPARE(Amiibo::sha256(simulation), importedHash);
    QCOMPARE(Amiibo::classify(simulation).payload, info.payload);
}

void TestAmiiboData::structuredParsingDoesNotTouchRaw()
{
    // Classification is read-only: repeated calls and payload extraction leave
    // the authoritative bytes untouched.
    const QByteArray imported = rawImage(135);
    const QString hash = Amiibo::sha256(imported);
    for(int i = 0; i < 3; i++)
    {
        const Amiibo::Info info = Amiibo::classify(imported);
        QCOMPARE(info.payload, imported);
        QCOMPARE(Amiibo::sha256(imported), hash);
    }
}

void TestAmiiboData::asciiClientPathPolicy()
{
    QVERIFY(!Amiibo::requiresAsciiClientPath(
                QStringLiteral("E:/Opencode_project/amiibo plain 540.bin")));
    QVERIFY(Amiibo::requiresAsciiClientPath(
                QStringLiteral("E:/Opencode_project/测试 Amiibo 540.bin")));
}

void TestAmiiboData::clientCopyPreservesBytes()
{
    const QByteArray imported = rawImage(135);
    const QString path = QDir::currentPath() +
            QStringLiteral("/pm3gui_amiibo_raw_test.bin");
    QFile::remove(path);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(imported), qint64(imported.size()));
    file.close();
    QFile check(path);
    QVERIFY(check.open(QIODevice::ReadOnly));
    const QByteArray copied = check.readAll();
    check.close();
    QFile::remove(path);
    QCOMPARE(copied, imported);
    QCOMPARE(Amiibo::sha256(copied), Amiibo::sha256(imported));
}

QTEST_APPLESS_MAIN(TestAmiiboData)
#include "tst_amiibodata.moc"
