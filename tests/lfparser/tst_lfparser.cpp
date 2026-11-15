#include <QtTest>
#include <QFile>

#include <limits>

#include "lfparser.h"
#include "lfparsetypes.h"
#include "lftextnormalizer.h"

namespace {

QString loadFixture(const char *name)
{
    const QString path = QFINDTESTDATA(name);
    if (path.isEmpty())
        return QString();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    return QString::fromUtf8(file.readAll());
}

QString withCrlf(const QString &text)
{
    QString result = text;
    result.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    return result;
}

} // namespace

class TestLfParser : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Normalizer
    void normalizer_stripsAnsi();
    void normalizer_normalizesLineEndings();
    void normalizer_preservesHexSpacing();
    void normalizer_normalizeHex();
    void normalizer_normalizeHexRejectsInvalid();

    // EM4100
    void em4100_standardFixture();
    void em4100_descrambleOnlyFixture();
    void em4100_lowercaseId();
    void em4100_crlfInput();
    void em4100_xlFrame();
    void em4100_electra();
    void em4100_markerOnlyIsPartial();
    void em4100_decimalOnlyFromClient();
    void em4100_doesNotInventFacilityCode();
    void em4100_fieldOrderIndependent();
    void em4100_invalidPayloadDoesNotCrash();

    // HID Prox
    void hid_h10301Fixture();
    void hid_h10301_crlf();
    void hid_multiIdenticalCandidates();
    void hid_conflictingCandidates();
    void hid_37bit();
    void hid_34bit();
    void hid_corporate1000();
    void hid_unknownFormatDoesNotDefault();
    void hid_rawOnly();
    void hid_parityFail();
    void hid_facilityOnlyKeepsCardNumberMissing();
    void hid_lowercaseRaw();
    void hid_indalaKeywordDoesNotStealHid();
    void hid_invalidNumericValuesDoNotCrash();

    // AWID
    void awid_26Fixture();
    void awid_50Fixture();
    void awid_unknownLength();
    void awid_missingRaw();
    void awid_keywordOnlyIsPartial();
    void awid_doesNotCrossParseHid();
    void awid_invalidRawDoesNotCrash();

    // Indala
    void indala_withRawFixture();
    void indala_withoutRawFixture();
    void indala_fieldOrderReversed();
    void indala_224bit();
    void indala_markerOnlyIsPartial();
    void indala_lowercaseRaw();
    void indala_rawWithoutDecodedFields();
    void indala_fmtWithoutValuesIsPartial();

    // Registry / dispatch
    void registry_emptyInput();
    void registry_whitespaceInput();
    void registry_noCard();
    void registry_firmwareUnsupported();
    void registry_unrelatedText();
    void registry_prefersStrongerSignature();
    void registry_preservesSourceText();
    void registry_parseAsMismatchedProtocol();
    void registry_registeredProtocols();
    void registry_largeInputDoesNotCrash();

    // Model semantics
    void model_missingIsNotZero();
    void model_summaryIsStable();
};

void TestLfParser::initTestCase()
{
    QVERIFY2(!loadFixture("fixtures/em4100_standard.txt").isEmpty(),
             "fixtures directory is not reachable from the test binary");
}

void TestLfParser::normalizer_stripsAnsi()
{
    const QString input = QStringLiteral("\x1b[32m[+] EM 410x ID 0F00112233\x1b[0m");
    const QString normalized = LFTextNormalizer::normalize(input);
    QVERIFY(!normalized.contains(QChar(0x1b)));
    QVERIFY(normalized.contains(QStringLiteral("EM 410x ID 0F00112233")));
}

void TestLfParser::normalizer_normalizesLineEndings()
{
    const QString normalized = LFTextNormalizer::normalize(QStringLiteral("a\r\nb\rc"));
    QCOMPARE(normalized, QStringLiteral("a\nb\nc"));
}

void TestLfParser::normalizer_preservesHexSpacing()
{
    const QString line = QStringLiteral("[+] Raw............... 28 58 99 7D 3B 9F");
    const QString normalized = LFTextNormalizer::normalize(line);
    QCOMPARE(normalized, line);
}

void TestLfParser::normalizer_normalizeHex()
{
    QCOMPARE(LFTextNormalizer::normalizeHex(QStringLiteral("a0:00-00 00")),
             QStringLiteral("A0000000"));
    QCOMPARE(LFTextNormalizer::normalizeHex(QStringLiteral("0x200078be5e1e")),
             QStringLiteral("200078BE5E1E"));
}

void TestLfParser::normalizer_normalizeHexRejectsInvalid()
{
    QCOMPARE(LFTextNormalizer::normalizeHex(QStringLiteral("ZZ11")), QString());
    QCOMPARE(LFTextNormalizer::normalizeHex(QStringLiteral("")), QString());
}

void TestLfParser::em4100_standardFixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/em4100_standard.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.status, Lf::Status::Identified);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
    QCOMPARE(result.rawData, QStringLiteral("0F00112233"));
    QVERIFY(result.decimalId.isPresent());
    QCOMPARE(result.decimalId.value(), Q_UINT64_C(4276803383));
    QVERIFY(result.facilityCode.isAbsent());
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("de-scramble")));
    QCOMPARE(result.confidence, Lf::Confidence::High);
}

void TestLfParser::em4100_descrambleOnlyFixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/em4100_descramble_only.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.status, Lf::Status::Identified);
    QCOMPARE(result.hexId, QStringLiteral("1122334455"));
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("de-scramble")));
    QVERIFY(result.facilityCode.isAbsent());
}

void TestLfParser::em4100_lowercaseId()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] EM 410x ID 0f00112233"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
}

void TestLfParser::em4100_crlfInput()
{
    const QString text = withCrlf(loadFixture("fixtures/em4100_standard.txt"));
    const LFParseResult result = LFParserRegistry::instance().parse(text);

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
}

void TestLfParser::em4100_xlFrame()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("EM 410x XL ID 0F0011200078BE5E1E1A2B"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F0011200078BE5E1E1A2B"));
    QCOMPARE(result.format, QStringLiteral("EM4100 XL"));
}

void TestLfParser::em4100_electra()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("EM 410x ID 0F00112233 Electra 123"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
    bool found = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("electra") && field.value == QStringLiteral("123"))
            found = true;
    }
    QVERIFY(found);
}

void TestLfParser::em4100_markerOnlyIsPartial()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] EM410x ( RF/64 )"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.status, Lf::Status::Partial);
    QVERIFY(result.hexId.isEmpty());
    QVERIFY(!result.warnings.isEmpty());
}

void TestLfParser::em4100_decimalOnlyFromClient()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("EM 410x ID 0F00112233\n"
                       "    DEZ 10         : 12345"));

    QCOMPARE(result.decimalId.value(), Q_UINT64_C(12345));
}

void TestLfParser::em4100_doesNotInventFacilityCode()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/em4100_standard.txt"));

    QVERIFY(result.facilityCode.isAbsent());
    QCOMPARE(result.facilityCode.valueOr(9999u), 9999u);
}

void TestLfParser::em4100_fieldOrderIndependent()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("    DEZ 10         : 12345\n"
                       "[+] EM 410x ID 0F00112233"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
    QCOMPARE(result.decimalId.value(), Q_UINT64_C(12345));
}

void TestLfParser::em4100_invalidPayloadDoesNotCrash()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("EM 410x ID ZZZZZZZZZZ"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.status, Lf::Status::Partial);
    QVERIFY(result.hexId.isEmpty());
}

void TestLfParser::hid_h10301Fixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_h10301.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.status, Lf::Status::Identified);
    QCOMPARE(result.format, QStringLiteral("H10301 (26-bit)"));
    QCOMPARE(result.facilityCode.value(), 65u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(29334));
    QCOMPARE(result.bitLength.value(), 26);
    QCOMPARE(result.rawData, QStringLiteral("200078BE5E1E"));
    QCOMPARE(result.confidence, Lf::Confidence::High);
}

void TestLfParser::hid_h10301_crlf()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        withCrlf(loadFixture("fixtures/hid_h10301.txt")));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.facilityCode.value(), 65u);
}

void TestLfParser::hid_multiIdenticalCandidates()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_multi_identical.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.status, Lf::Status::Identified);
    QCOMPARE(result.facilityCode.value(), 65u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(29334));
    QCOMPARE(result.formatCandidates.size(), 2);
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("identical")));
}

void TestLfParser::hid_conflictingCandidates()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_conflict.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.status, Lf::Status::Partial);
    QCOMPARE(result.format, QStringLiteral("Ambiguous"));
    QVERIFY(result.facilityCode.isAbsent());
    QVERIFY(result.cardNumber.isAbsent());
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("disagree")));
}

void TestLfParser::hid_37bit()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_37bit.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.bitLength.value(), 37);
    QCOMPARE(result.format, QStringLiteral("HID 37-bit (H10304)"));
    QCOMPARE(result.facilityCode.value(), 118u);
}

void TestLfParser::hid_34bit()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10306  ] HID H10306 34-bit                FC: 200  CN: 60000  parity ( ok )"));

    QCOMPARE(result.bitLength.value(), 34);
    QCOMPARE(result.format, QStringLiteral("HID 34-bit (H10306)"));
}

void TestLfParser::hid_corporate1000()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_corp1000.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.format, QStringLiteral("Corporate 1000 (35-bit)"));
    QCOMPARE(result.facilityCode.value(), 123u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(45678));
}

void TestLfParser::hid_unknownFormatDoesNotDefault()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_unknown_format.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.status, Lf::Status::Partial);
    QCOMPARE(result.format, QStringLiteral("Unknown HID format"));
    QCOMPARE(result.facilityCode.value(), 65u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(29334));
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("H10301")));
    QCOMPARE(result.confidence, Lf::Confidence::Low);
}

void TestLfParser::hid_rawOnly()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("HID Prox ID found!\nraw: 200078be5e1e"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.status, Lf::Status::Partial);
    QCOMPARE(result.rawData, QStringLiteral("200078BE5E1E"));
    QVERIFY(result.facilityCode.isAbsent());
}

void TestLfParser::hid_parityFail()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10301  ] HID H10301 26-bit                FC: 65  CN: 29334  parity ( fail )"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("parity")));
    QCOMPARE(result.confidence, Lf::Confidence::Medium);
}

void TestLfParser::hid_facilityOnlyKeepsCardNumberMissing()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10302  ] HID H10302 37-bit huge ID          FC: 12345  parity ( ok )"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QVERIFY(result.facilityCode.isPresent());
    QVERIFY(result.cardNumber.isAbsent());
    QCOMPARE(result.cardNumber.valueOr(std::numeric_limits<quint64>::max()),
             std::numeric_limits<quint64>::max());
}

void TestLfParser::hid_lowercaseRaw()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10301  ] HID H10301 26-bit                FC: 1  CN: 2  parity ( ok )\n"
                       "raw: 01f0760643c3"));

    QCOMPARE(result.rawData, QStringLiteral("01F0760643C3"));
}

void TestLfParser::hid_indalaKeywordDoesNotStealHid()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10301  ] HID H10301 26-bit                FC: 1  CN: 2  parity ( ok )\n"
                       "[+] Valid Indala ID found!"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QCOMPARE(result.facilityCode.value(), 1u);
}

void TestLfParser::hid_invalidNumericValuesDoNotCrash()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10301  ] HID H10301 26-bit                FC: 999999999999999999999  CN: 2  parity ( ok )"));

    QCOMPARE(result.protocol, Lf::Protocol::HIDProx);
    QVERIFY(result.facilityCode.isAbsent());
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(2));
    QCOMPARE(result.status, Lf::Status::Identified);
}

void TestLfParser::awid_26Fixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/awid_26.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QCOMPARE(result.status, Lf::Status::Identified);
    QCOMPARE(result.bitLength.value(), 26);
    QCOMPARE(result.facilityCode.value(), 50u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(1234));
    QCOMPARE(result.rawData, QStringLiteral("011DB2881474411111111111"));
    QCOMPARE(result.format, QStringLiteral("AWID 26-bit"));
    QCOMPARE(result.confidence, Lf::Confidence::High);
}

void TestLfParser::awid_50Fixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/awid_50.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QCOMPARE(result.bitLength.value(), 50);
    QCOMPARE(result.facilityCode.value(), 4000u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(2248975));
    bool foundRbh = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("rbhCn"))
            foundRbh = true;
    }
    QVERIFY(foundRbh);
}

void TestLfParser::awid_unknownLength()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("AWID - len: 33 -unknown- (1234) - Wiegand: 1a2b, Raw: 00112233445566778899aabb"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QCOMPARE(result.bitLength.value(), 33);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(1234));
    QCOMPARE(result.format, QStringLiteral("AWID 33-bit (unknown)"));
    QCOMPARE(result.confidence, Lf::Confidence::Low);
    QVERIFY(result.facilityCode.isAbsent());
}

void TestLfParser::awid_missingRaw()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("AWID - len: 26 FC: 50 Card: 1234"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QCOMPARE(result.status, Lf::Status::Identified);
    QVERIFY(result.rawData.isEmpty());
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("raw")));
}

void TestLfParser::awid_keywordOnlyIsPartial()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] Valid AWID ID found!"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QCOMPARE(result.status, Lf::Status::Partial);
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("no card fields")));
}

void TestLfParser::awid_doesNotCrossParseHid()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/awid_26.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QVERIFY(result.formatCandidates.isEmpty());
}

void TestLfParser::awid_invalidRawDoesNotCrash()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("AWID - len: 26 FC: 50 Card: 1234 - Wiegand: x, Raw: ZZZZ"));

    QCOMPARE(result.protocol, Lf::Protocol::AWID);
    QCOMPARE(result.status, Lf::Status::Identified);
    QVERIFY(result.rawData.isEmpty());
    QCOMPARE(result.facilityCode.value(), 50u);
}

void TestLfParser::indala_withRawFixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/indala_64_raw.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.status, Lf::Status::Identified);
    QCOMPARE(result.bitLength.value(), 64);
    QCOMPARE(result.rawData, QStringLiteral("A0000000A0002021"));
    QCOMPARE(result.facilityCode.value(), 123u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(1337));
    QVERIFY(!result.extraFields.isEmpty());
}

void TestLfParser::indala_withoutRawFixture()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/indala_64_no_raw.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.status, Lf::Status::Identified);
    QVERIFY(result.rawData.isEmpty());
    QCOMPARE(result.facilityCode.value(), 123u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(1337));
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("raw")));
    QCOMPARE(result.confidence, Lf::Confidence::Medium);
}

void TestLfParser::indala_fieldOrderReversed()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("Fmt 26 FC: 9 Card: 4242 Parity: 01\n"
                       "Indala (len 64)  Raw: a0000000a0002021"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.facilityCode.value(), 9u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(4242));
    QCOMPARE(result.rawData, QStringLiteral("A0000000A0002021"));
}

void TestLfParser::indala_224bit()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("Indala (len 224)  Raw: 80000001b23523a6c2e31eba3cbee4af"
                       "b3c6ad1fcf649393928c14e5"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.bitLength.value(), 224);
    QCOMPARE(result.rawData.size(), 56);
    QCOMPARE(result.format, QStringLiteral("Indala 224-bit"));
    QCOMPARE(result.status, Lf::Status::Identified);
}

void TestLfParser::indala_markerOnlyIsPartial()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] Valid Indala ID found!"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.status, Lf::Status::Partial);
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("no identity")));
}

void TestLfParser::indala_lowercaseRaw()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("Indala (len 64)  Raw: a0000000a0002021"));

    QCOMPARE(result.rawData, QStringLiteral("A0000000A0002021"));
}

void TestLfParser::indala_rawWithoutDecodedFields()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("Indala (len 64)  Raw: a0000000a0002021"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.status, Lf::Status::Identified);
    QVERIFY(result.facilityCode.isAbsent());
    QVERIFY(result.cardNumber.isAbsent());
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("FC/CN")));
}

void TestLfParser::indala_fmtWithoutValuesIsPartial()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] Indala ID found!\nFmt 26"));

    QCOMPARE(result.protocol, Lf::Protocol::Indala);
    QCOMPARE(result.status, Lf::Status::Partial);
    QVERIFY(result.facilityCode.isAbsent());
}

void TestLfParser::registry_emptyInput()
{
    const LFParseResult result = LFParserRegistry::instance().parse(QString());
    QCOMPARE(result.status, Lf::Status::InvalidInput);
    QCOMPARE(result.protocol, Lf::Protocol::Unknown);
}

void TestLfParser::registry_whitespaceInput()
{
    const LFParseResult result = LFParserRegistry::instance().parse(QStringLiteral("  \n\t \n"));
    QCOMPARE(result.status, Lf::Status::InvalidInput);
}

void TestLfParser::registry_noCard()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/no_card.txt"));

    QCOMPARE(result.status, Lf::Status::Unidentified);
    QVERIFY(!result.warnings.isEmpty());
}

void TestLfParser::registry_firmwareUnsupported()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/firmware_unsupported.txt"));

    QCOMPARE(result.status, Lf::Status::Unidentified);
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("does not look like")));
}

void TestLfParser::registry_unrelatedText()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("hello world, this is not proxmark output"));

    QCOMPARE(result.status, Lf::Status::Unidentified);
}

void TestLfParser::registry_prefersStrongerSignature()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/mixed_protocols.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
}

void TestLfParser::registry_preservesSourceText()
{
    const QString text = loadFixture("fixtures/em4100_standard.txt");
    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(result.sourceText, text);
}

void TestLfParser::registry_parseAsMismatchedProtocol()
{
    const LFParseResult result = LFParserRegistry::instance().parseAs(
        Lf::Protocol::EM4100, loadFixture("fixtures/hid_h10301.txt"));

    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.status, Lf::Status::Partial);
    QVERIFY(result.hexId.isEmpty());
}

void TestLfParser::registry_registeredProtocols()
{
    const QList<Lf::Protocol> protocols = LFParserRegistry::instance().protocols();
    QVERIFY(protocols.contains(Lf::Protocol::EM4100));
    QVERIFY(protocols.contains(Lf::Protocol::HIDProx));
    QVERIFY(protocols.contains(Lf::Protocol::AWID));
    QVERIFY(protocols.contains(Lf::Protocol::Indala));
}

void TestLfParser::registry_largeInputDoesNotCrash()
{
    QString text;
    text.reserve(200000);
    for (int i = 0; i < 5000; ++i)
        text += QStringLiteral("filler line with no protocol markers\n");
    text += QStringLiteral("[+] EM 410x ID 0F00112233\n");

    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(result.protocol, Lf::Protocol::EM4100);
    QCOMPARE(result.hexId, QStringLiteral("0F00112233"));
}

void TestLfParser::model_missingIsNotZero()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[H10301  ] HID H10301 26-bit                FC: 0  CN: 0  parity ( ok )"));

    QVERIFY(result.facilityCode.isPresent());
    QVERIFY(result.cardNumber.isPresent());
    QCOMPARE(result.facilityCode.value(), 0u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(0));

    const LFParseResult missing = LFParserRegistry::instance().parse(
        QStringLiteral("[H10302  ] HID H10302 37-bit huge ID          FC: 7  parity ( ok )"));
    QVERIFY(missing.cardNumber.isAbsent());
}

void TestLfParser::model_summaryIsStable()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/hid_h10301.txt"));

    QVERIFY(result.summary().contains(QStringLiteral("HID Prox")));
    QVERIFY(result.summary().contains(QStringLiteral("FC=65")));
    QVERIFY(result.summary().contains(QStringLiteral("CN=29334")));
}

QTEST_APPLESS_MAIN(TestLfParser)

#include "tst_lfparser.moc"
