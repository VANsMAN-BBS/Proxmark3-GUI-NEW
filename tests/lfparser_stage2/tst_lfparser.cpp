#include <QtTest>
#include <QFile>

#include <limits>

#include "lf/lfparser.h"
#include "lf/lfparsetypes.h"
#include "lf/lftextnormalizer.h"
#include "lf/t55xxparser.h"

namespace {

QString loadFixture(const char *name)
{
    QString path = QFINDTESTDATA(name);
    if (path.isEmpty()) {
        // The canonical fixtures live in tests/fixtures while this source file
        // is in tests/parser_tests.
        QString relative = QString::fromLatin1(name);
        if (relative.startsWith(QStringLiteral("fixtures/"))) {
            relative = QStringLiteral("../") + relative;
            const QByteArray utf8 = relative.toUtf8();
            path = QFINDTESTDATA(utf8.constData());
        }
    }
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

void verifyOptionalU32(const Lf::Optional<quint32> &value, const QString &expected)
{
    if (expected.isEmpty()) {
        QVERIFY(value.isAbsent());
        return;
    }
    QVERIFY(value.isPresent());
    QCOMPARE(value.value(), expected.toUInt());
}

void verifyOptionalU64(const Lf::Optional<quint64> &value, const QString &expected)
{
    if (expected.isEmpty()) {
        QVERIFY(value.isAbsent());
        return;
    }
    QVERIFY(value.isPresent());
    QCOMPARE(value.value(), expected.toULongLong());
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

    // Stage 2: remaining protocols
    void newProtocols_data();
    void newProtocols();
    void newProtocols_missingFields_data();
    void newProtocols_missingFields();
    void newProtocols_invalidValues_data();
    void newProtocols_invalidValues();
    void newProtocols_brandOnlyIsNotIdentified_data();
    void newProtocols_brandOnlyIsNotIdentified();
    void newProtocols_crlf();
    void newProtocols_lowercaseHex();

    void ioprox_facilityCodeHexConversion();
    void fdxb_countryIsNotFacilityCode();
    void keri_variantsAreDistinct();
    void securakey_facilityCodeHexConversion();
    void pyramid_unknownLength();
    void gproxii_unknownLength();
    void idteck_checksumFailureWarning();
    void idteck_winsOverHidLine();
    void nexwatch_88bitAndRaw();
    void motorola_readerOnlyFormat();

    void registry_allProtocolsRegistered();
    void registry_multiProtocolTextIsDeterministic();

    // T55xx (separate from card registry)
    void t55xx_detectPlain();
    void t55xx_detectPassword();
    void t55xx_detectNoChip();
    void t55xx_block0Ask();
    void t55xx_block0Psk2();
    void t55xx_block0ExtendedUnmapped();
    void t55xx_block0Invalid();
    void t55xx_notPartOfCardRegistry();
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

void TestLfParser::newProtocols_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<int>("protocol");
    QTest::addColumn<QString>("formatPart");
    QTest::addColumn<QString>("fc");
    QTest::addColumn<QString>("cn");
    QTest::addColumn<QString>("raw");

    QTest::newRow("ioprox-xsf") << "fixtures/ioprox/xsf.txt"
        << static_cast<int>(Lf::Protocol::IOProx) << "XSF" << "101" << "1337" << "007859603059CDAF";
    QTest::newRow("ioprox-ssf") << "fixtures/ioprox/ssf.txt"
        << static_cast<int>(Lf::Protocol::IOProx) << "SSF" << "101" << "1337" << "";
    QTest::newRow("fdxb-animal-id") << "fixtures/fdxb/animal_id.txt"
        << static_cast<int>(Lf::Protocol::FDXB) << "FDX-B" << "" << "123456789012"
        << "2858997D3B9F0000C0CC000000";
    QTest::newRow("fdxb-animal-code") << "fixtures/fdxb/animal_code.txt"
        << static_cast<int>(Lf::Protocol::FDXB) << "FDX-B" << "" << "123456789012"
        << "2858997D3B9F0000C0CC000000";
    QTest::newRow("paradox") << "fixtures/paradox/standard.txt"
        << static_cast<int>(Lf::Protocol::Paradox) << "Paradox" << "96" << "40426"
        << "0F55555695596A6A9999A59A";
    QTest::newRow("keri-internal") << "fixtures/keri/internal.txt"
        << static_cast<int>(Lf::Protocol::Keri) << "Internal" << "" << "12345" << "E000000080003039";
    QTest::newRow("keri-ms") << "fixtures/keri/ms.txt"
        << static_cast<int>(Lf::Protocol::Keri) << "MS" << "1" << "12544" << "";
    QTest::newRow("keri-nxt") << "fixtures/keri/nxt.txt"
        << static_cast<int>(Lf::Protocol::Keri) << "NXT" << "" << ""
        << "00112233445566778899AABBCCDDEEFF";
    QTest::newRow("viking") << "fixtures/viking/standard.txt"
        << static_cast<int>(Lf::Protocol::Viking) << "Viking" << "" << "" << "F200001A2B3C4D1A";
    QTest::newRow("pyramid-known") << "fixtures/pyramid/known.txt"
        << static_cast<int>(Lf::Protocol::Pyramid) << "Pyramid 26-bit" << "123" << "4567"
        << "00112233445566778899AABBCCDDEEFF";
    QTest::newRow("pyramid-unknown") << "fixtures/pyramid/unknown.txt"
        << static_cast<int>(Lf::Protocol::Pyramid) << "unknown" << "" << "4567"
        << "00112233445566778899AABBCCDDEEFF";
    QTest::newRow("nexwatch") << "fixtures/nexwatch/standard.txt"
        << static_cast<int>(Lf::Protocol::NexWatch) << "NexWatch" << "" << "521512301"
        << "5600000000213C9F8F150C00";
    QTest::newRow("presco") << "fixtures/presco/standard.txt"
        << static_cast<int>(Lf::Protocol::Presco) << "Presco" << "0" << "57470"
        << "10D0000000000000000000000031E07E";
    QTest::newRow("nedap-64") << "fixtures/nedap/64.txt"
        << static_cast<int>(Lf::Protocol::Nedap) << "NEDAP 64b" << "" << "12345"
        << "FF820CA58960F8F3";
    QTest::newRow("gproxii-known") << "fixtures/gproxii/known.txt"
        << static_cast<int>(Lf::Protocol::GProxII) << "G-Prox-II 26-bit" << "123" << "1234"
        << "FAC2A38C2B081AF008EB0AC2";
    QTest::newRow("gproxii-unknown") << "fixtures/gproxii/unknown.txt"
        << static_cast<int>(Lf::Protocol::GProxII) << "unknown" << "" << ""
        << "00112233445566778899AABB";
    QTest::newRow("gallagher") << "fixtures/gallagher/standard.txt"
        << static_cast<int>(Lf::Protocol::Gallagher) << "Gallagher" << "22" << "3333"
        << "7FEAA35854B86B0D1A8CB120";
    QTest::newRow("pac") << "fixtures/pac/standard.txt"
        << static_cast<int>(Lf::Protocol::PAC) << "PAC/Stanley" << "" << ""
        << "FF2049906D8511C593155B56D5B2649F";
    QTest::newRow("noralsy") << "fixtures/noralsy/standard.txt"
        << static_cast<int>(Lf::Protocol::Noralsy) << "Noralsy" << "" << "112233"
        << "BB0214FF0110002233070000";
    QTest::newRow("jablotron") << "fixtures/jablotron/standard.txt"
        << static_cast<int>(Lf::Protocol::Jablotron) << "Jablotron" << "" << ""
        << "FFFF00001122335C";
    QTest::newRow("securakey-long") << "fixtures/securakey/long.txt"
        << static_cast<int>(Lf::Protocol::SecuraKey) << "SecuraKey 26-bit" << "53" << "64169"
        << "7FCB400001ADEA5344300000";
    QTest::newRow("securakey-short") << "fixtures/securakey/short.txt"
        << static_cast<int>(Lf::Protocol::SecuraKey) << "Short" << "" << "1234"
        << "7FCB400001ADEA53";
    QTest::newRow("visa2000") << "fixtures/visa2000/standard.txt"
        << static_cast<int>(Lf::Protocol::Visa2000) << "Visa2000" << "" << "112233"
        << "564953320001B66900000183";
    QTest::newRow("motorola") << "fixtures/motorola/standard.txt"
        << static_cast<int>(Lf::Protocol::Motorola) << "Motorola 26-bit" << "12" << "3456"
        << "A0000000A0002021";
    QTest::newRow("idteck") << "fixtures/idteck/standard.txt"
        << static_cast<int>(Lf::Protocol::IDTECK) << "IDTECK" << "" << "4963871"
        << "4944544B351FBE4B";
}

void TestLfParser::newProtocols()
{
    QFETCH(QString, fixture);
    QFETCH(int, protocol);
    QFETCH(QString, formatPart);
    QFETCH(QString, fc);
    QFETCH(QString, cn);
    QFETCH(QString, raw);

    const QString text = loadFixture(fixture.toUtf8().constData());
    QVERIFY2(!text.isEmpty(), qPrintable(QStringLiteral("missing fixture: ") + fixture));

    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(static_cast<int>(result.protocol), protocol);
    QVERIFY2(result.format.contains(formatPart, Qt::CaseInsensitive),
             qPrintable(QStringLiteral("format '%1' does not contain '%2'").arg(result.format, formatPart)));
    verifyOptionalU32(result.facilityCode, fc);
    verifyOptionalU64(result.cardNumber, cn);
    QCOMPARE(result.rawData, raw);
    QCOMPARE(result.status, Lf::Status::Identified);
}

void TestLfParser::newProtocols_missingFields_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("protocol");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("fc");
    QTest::addColumn<QString>("cn");

    QTest::newRow("ioprox-no-raw") << "[+] IO Prox - XSF(02)0A:00042"
        << static_cast<int>(Lf::Protocol::IOProx) << static_cast<int>(Lf::Status::Identified) << "10" << "42";
    QTest::newRow("noralsy-no-year") << "[+] Noralsy - Card: 5"
        << static_cast<int>(Lf::Protocol::Noralsy) << static_cast<int>(Lf::Status::Identified) << "" << "5";
    QTest::newRow("pyramid-len-only") << "[+] Pyramid - len: 26"
        << static_cast<int>(Lf::Protocol::Pyramid) << static_cast<int>(Lf::Status::Partial) << "" << "";
    QTest::newRow("motorola-fmt-only") << "[+] Motorola - fmt: 26"
        << static_cast<int>(Lf::Protocol::Motorola) << static_cast<int>(Lf::Status::Partial) << "" << "";
    QTest::newRow("visa2000-card-only") << "[+] Visa2000 - Card 7"
        << static_cast<int>(Lf::Protocol::Visa2000) << static_cast<int>(Lf::Status::Identified) << "" << "7";
    QTest::newRow("securakey-no-raw") << "[+] Securakey - len: 26 FC: 0x1 Card: 2"
        << static_cast<int>(Lf::Protocol::SecuraKey) << static_cast<int>(Lf::Status::Identified) << "1" << "2";
    QTest::newRow("nedap-no-raw") << "[+] NEDAP (64b) - ID: 12345 subtype: 1 customer code: 101 / 0x065"
        << static_cast<int>(Lf::Protocol::Nedap) << static_cast<int>(Lf::Status::Identified) << "" << "12345";
    QTest::newRow("gallagher-multiline-no-region")
        << "[+] GALLAGHER\n[+] Facility: 22\n[+] Card No.: 3333"
        << static_cast<int>(Lf::Protocol::Gallagher) << static_cast<int>(Lf::Status::Identified) << "22" << "3333";
    QTest::newRow("pac-card-only") << "[+] PAC/Stanley - Card: CD4F5552"
        << static_cast<int>(Lf::Protocol::PAC) << static_cast<int>(Lf::Status::Identified) << "" << "";
}

void TestLfParser::newProtocols_missingFields()
{
    QFETCH(QString, text);
    QFETCH(int, protocol);
    QFETCH(int, status);
    QFETCH(QString, fc);
    QFETCH(QString, cn);

    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(static_cast<int>(result.protocol), protocol);
    QCOMPARE(static_cast<int>(result.status), status);
    verifyOptionalU32(result.facilityCode, fc);
    verifyOptionalU64(result.cardNumber, cn);
}

void TestLfParser::newProtocols_invalidValues_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("protocol");
    QTest::addColumn<int>("status");

    QTest::newRow("visa-nonnumeric") << "[+] Visa2000 - Card ABC"
        << static_cast<int>(Lf::Protocol::Visa2000) << static_cast<int>(Lf::Status::Partial);
    QTest::newRow("idteck-nonnumeric") << "[+] IDTECK Tag Found: Card ID xyz"
        << static_cast<int>(Lf::Protocol::IDTECK) << static_cast<int>(Lf::Status::Partial);
    QTest::newRow("pyramid-missing-cn") << "[+] Pyramid - len: 26, FC: 123 Card: xyz"
        << static_cast<int>(Lf::Protocol::Pyramid) << static_cast<int>(Lf::Status::Partial);
    QTest::newRow("securakey-bad-fc") << "[+] Securakey - len: 26 FC: 0xZZ Card: 2"
        << static_cast<int>(Lf::Protocol::SecuraKey) << static_cast<int>(Lf::Status::Partial);
    QTest::newRow("visa-overflow") << "[+] Visa2000 - Card 999999999999999999999999999999"
        << static_cast<int>(Lf::Protocol::Visa2000) << static_cast<int>(Lf::Status::Partial);
    QTest::newRow("motorola-huge-fmt") << "[+] Motorola - fmt: 999 FC: 1 Card: 2"
        << static_cast<int>(Lf::Protocol::Motorola) << static_cast<int>(Lf::Status::Identified);
}

void TestLfParser::newProtocols_invalidValues()
{
    QFETCH(QString, text);
    QFETCH(int, protocol);
    QFETCH(int, status);

    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(static_cast<int>(result.protocol), protocol);
    QCOMPARE(static_cast<int>(result.status), status);
    if (result.status == Lf::Status::Partial)
        QVERIFY(!result.hasIdentity());
    else
        QVERIFY(result.hasIdentity());
}

void TestLfParser::newProtocols_brandOnlyIsNotIdentified_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("protocol");

    QTest::newRow("pac-brand") << "Please replace the PAC/Stanley reader."
        << static_cast<int>(Lf::Protocol::PAC);
    QTest::newRow("indala-brand") << "This Indala reader is broken."
        << static_cast<int>(Lf::Protocol::Indala);
    QTest::newRow("em410x-doc") << "EM410x documentation is available."
        << static_cast<int>(Lf::Protocol::EM4100);
    QTest::newRow("awid-guide") << "AWID configuration guide."
        << static_cast<int>(Lf::Protocol::AWID);
    QTest::newRow("nedap-brand") << "NEDAP access control."
        << static_cast<int>(Lf::Protocol::Nedap);
}

void TestLfParser::newProtocols_brandOnlyIsNotIdentified()
{
    QFETCH(QString, text);
    QFETCH(int, protocol);

    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(static_cast<int>(result.protocol), protocol);
    QVERIFY(result.status != Lf::Status::Identified);
    QVERIFY(!result.hasIdentity());
}

void TestLfParser::newProtocols_crlf()
{
    const QStringList fixtures = {
        QStringLiteral("fixtures/ioprox/xsf.txt"),
        QStringLiteral("fixtures/fdxb/animal_id.txt"),
        QStringLiteral("fixtures/paradox/standard.txt"),
        QStringLiteral("fixtures/nedap/64.txt"),
        QStringLiteral("fixtures/gallagher/standard.txt")
    };

    for (const QString &fixture : fixtures) {
        const QString text = withCrlf(loadFixture(fixture.toUtf8().constData()));
        QVERIFY(!text.isEmpty());
        const LFParseResult result = LFParserRegistry::instance().parse(text);
        QVERIFY2(result.status == Lf::Status::Identified,
                 qPrintable(QStringLiteral("CRLF failed for ") + fixture));
    }
}

void TestLfParser::newProtocols_lowercaseHex()
{
    LFParseResult result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] G-Prox-II - Len: 26 FC: 1 Card: 2 xor: 3, Raw: fac2a38c2b081af008eb0ac2"));
    QCOMPARE(result.rawData, QStringLiteral("FAC2A38C2B081AF008EB0AC2"));

    result = LFParserRegistry::instance().parse(
        QStringLiteral("[+] Viking - Card 1a2b3c4d, Raw: f200001a2b3c4d1a"));
    QCOMPARE(result.rawData, QStringLiteral("F200001A2B3C4D1A"));
}

void TestLfParser::ioprox_facilityCodeHexConversion()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/ioprox/xsf.txt"));
    QCOMPARE(result.facilityCode.value(), 101u);
    bool found = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("facilityCodeHex") && field.value == QStringLiteral("65"))
            found = true;
    }
    QVERIFY(found);
}

void TestLfParser::fdxb_countryIsNotFacilityCode()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/fdxb/animal_id.txt"));
    QVERIFY(result.facilityCode.isAbsent());
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(123456789012));
    bool country = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("countryCode") && field.value == QStringLiteral("999"))
            country = true;
    }
    QVERIFY(country);
}

void TestLfParser::keri_variantsAreDistinct()
{
    const LFParseResult internal = LFParserRegistry::instance().parse(
        loadFixture("fixtures/keri/internal.txt"));
    QVERIFY(internal.format.contains(QStringLiteral("Internal")));

    const LFParseResult ms = LFParserRegistry::instance().parse(
        loadFixture("fixtures/keri/ms.txt"));
    QVERIFY(ms.format.contains(QStringLiteral("MS")));

    const LFParseResult nxt = LFParserRegistry::instance().parse(
        loadFixture("fixtures/keri/nxt.txt"));
    QVERIFY(nxt.format.contains(QStringLiteral("NXT")));
    QVERIFY(nxt.cardNumber.isAbsent());
}

void TestLfParser::securakey_facilityCodeHexConversion()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/securakey/long.txt"));
    QCOMPARE(result.facilityCode.value(), 53u); // 0x35
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(64169));
}

void TestLfParser::pyramid_unknownLength()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/pyramid/unknown.txt"));
    QCOMPARE(result.bitLength.value(), 33);
    QVERIFY(result.format.contains(QStringLiteral("unknown")));
    QVERIFY(result.facilityCode.isAbsent());
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(4567));
}

void TestLfParser::gproxii_unknownLength()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/gproxii/unknown.txt"));
    QCOMPARE(result.bitLength.value(), 33);
    QVERIFY(result.format.contains(QStringLiteral("unknown")));
    bool xorFound = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("xor") && field.value == QStringLiteral("141"))
            xorFound = true;
    }
    QVERIFY(xorFound);
}

void TestLfParser::idteck_checksumFailureWarning()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/idteck/standard.txt"));
    QVERIFY(result.warnings.join(QString()).contains(QStringLiteral("checksum")));
    bool state = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("checksumState") && field.value == QStringLiteral("fail"))
            state = true;
    }
    QVERIFY(state);
}

void TestLfParser::idteck_winsOverHidLine()
{
    QString text = loadFixture("fixtures/idteck/standard.txt");
    text += QStringLiteral("[+] [H10301  ] HID H10301 26-bit                FC: 37  CN: 57103  parity ( ok )\n");
    const LFParseResult result = LFParserRegistry::instance().parse(text);
    QCOMPARE(result.protocol, Lf::Protocol::IDTECK);
}

void TestLfParser::nexwatch_88bitAndRaw()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/nexwatch/standard.txt"));
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(521512301));
    QCOMPARE(result.rawData, QStringLiteral("5600000000213C9F8F150C00"));
    bool fingerprint = false;
    for (const LfExtraField &field : result.extraFields) {
        if (field.name == QStringLiteral("fingerprint") && field.value == QStringLiteral("Quadrakey"))
            fingerprint = true;
    }
    QVERIFY(fingerprint);
}

void TestLfParser::motorola_readerOnlyFormat()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/motorola/standard.txt"));
    QCOMPARE(result.bitLength.value(), 26);
    QCOMPARE(result.facilityCode.value(), 12u);
    QCOMPARE(result.cardNumber.value(), Q_UINT64_C(3456));
}

void TestLfParser::registry_allProtocolsRegistered()
{
    const QList<Lf::Protocol> protocols = LFParserRegistry::instance().protocols();
    QCOMPARE(protocols.size(), 22);

    const QList<Lf::Protocol> expected = {
        Lf::Protocol::EM4100, Lf::Protocol::HIDProx, Lf::Protocol::AWID,
        Lf::Protocol::Indala, Lf::Protocol::IOProx, Lf::Protocol::FDXB,
        Lf::Protocol::Paradox, Lf::Protocol::Keri, Lf::Protocol::Viking,
        Lf::Protocol::Pyramid, Lf::Protocol::NexWatch, Lf::Protocol::Presco,
        Lf::Protocol::Nedap, Lf::Protocol::GProxII, Lf::Protocol::Gallagher,
        Lf::Protocol::PAC, Lf::Protocol::Noralsy, Lf::Protocol::Jablotron,
        Lf::Protocol::SecuraKey, Lf::Protocol::Visa2000, Lf::Protocol::Motorola,
        Lf::Protocol::IDTECK
    };
    for (Lf::Protocol protocol : expected)
        QVERIFY(protocols.contains(protocol));
}

void TestLfParser::registry_multiProtocolTextIsDeterministic()
{
    // The most specific protocol in the text must win, twice in a row.
    QString text = loadFixture("fixtures/idteck/standard.txt");
    text += loadFixture("fixtures/viking/standard.txt");

    const LFParseResult first = LFParserRegistry::instance().parse(text);
    const LFParseResult second = LFParserRegistry::instance().parse(text);
    QCOMPARE(first.protocol, Lf::Protocol::IDTECK);
    QCOMPARE(second.protocol, Lf::Protocol::IDTECK);
}

void TestLfParser::t55xx_detectPlain()
{
    const LfT55xx::DetectInfo info = LfT55xx::parseDetect(
        loadFixture("fixtures/t55xx/detect_plain.txt"));

    QVERIFY(info.detected);
    QCOMPARE(info.chipType, QStringLiteral("T55x7"));
    QVERIFY(info.passwordSet.isPresent());
    QCOMPARE(info.passwordSet.value(), false);
    QVERIFY(info.block0.isPresent());
    QCOMPARE(info.block0.value(), static_cast<quint32>(0x00148040));
    QCOMPARE(info.block0Hex, QStringLiteral("00148040"));
    bool modulation = false;
    for (const LfExtraField &field : info.extraFields) {
        if (field.name == QStringLiteral("modulation") && field.value == QStringLiteral("ASK"))
            modulation = true;
    }
    QVERIFY(modulation);
    QCOMPARE(info.bitRate.value(), 64);
    QCOMPARE(info.offset.value(), 0);
    QVERIFY(info.inverted.isPresent());
    QCOMPARE(info.inverted.value(), false);
}

void TestLfParser::t55xx_detectPassword()
{
    const LfT55xx::DetectInfo info = LfT55xx::parseDetect(
        loadFixture("fixtures/t55xx/detect_password.txt"));
    QVERIFY(info.detected);
    QVERIFY(info.passwordSet.isPresent());
    QCOMPARE(info.passwordSet.value(), true);
}

void TestLfParser::t55xx_detectNoChip()
{
    const LfT55xx::DetectInfo info = LfT55xx::parseDetect(QStringLiteral("hello world"));
    QVERIFY(!info.detected);
    QVERIFY(!info.warnings.isEmpty());
}

void TestLfParser::t55xx_block0Ask()
{
    const LfT55xx::Block0 block = LfT55xx::parseBlock0(QStringLiteral("00148040"));
    QVERIFY(block.valid);
    QVERIFY(block.modulation.isPresent());
    QCOMPARE(block.modulation.value(), LfT55xx::Modulation::ASK);
    QCOMPARE(block.bitRateIndex.value(), 5);
    QCOMPARE(block.bitRateDivisor.value(), 64);
    QCOMPARE(block.maxBlock.value(), 2);
    QVERIFY(!block.extend);
    QCOMPARE(block.flagBits, static_cast<quint8>(0));
}

void TestLfParser::t55xx_block0Psk2()
{
    const LfT55xx::Block0 block = LfT55xx::parseBlock0(QStringLiteral("00082040"));
    QVERIFY(block.valid);
    QCOMPARE(block.modulation.value(), LfT55xx::Modulation::PSK2);
    QCOMPARE(block.bitRateDivisor.value(), 32);
}

void TestLfParser::t55xx_block0ExtendedUnmapped()
{
    const LfT55xx::Block0 block = LfT55xx::parseBlock0(QStringLiteral("00265000"));
    QVERIFY(block.valid);
    QVERIFY(block.extend);
    QCOMPARE(block.bitRateIndex.value(), 9);
    QVERIFY(block.bitRateDivisor.isAbsent());
    QVERIFY(!block.warnings.isEmpty());
}

void TestLfParser::t55xx_block0Invalid()
{
    const LfT55xx::Block0 shortValue = LfT55xx::parseBlock0(QStringLiteral("123"));
    QVERIFY(!shortValue.valid);
    QVERIFY(!shortValue.warnings.isEmpty());

    const LfT55xx::Block0 nonHex = LfT55xx::parseBlock0(QStringLiteral("ZZZZZZZZ"));
    QVERIFY(!nonHex.valid);
}

void TestLfParser::t55xx_notPartOfCardRegistry()
{
    const LFParseResult result = LFParserRegistry::instance().parse(
        loadFixture("fixtures/t55xx/detect_plain.txt"));
    QVERIFY(result.status != Lf::Status::Identified);
    QVERIFY(result.protocol == Lf::Protocol::Unknown);
}

QTEST_APPLESS_MAIN(TestLfParser)

#include "tst_lfparser.moc"
