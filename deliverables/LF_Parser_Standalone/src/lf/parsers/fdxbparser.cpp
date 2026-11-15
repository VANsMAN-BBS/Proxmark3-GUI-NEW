#include "lf/parsers/fdxbparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol FDXBParser::protocol() const { return Lf::Protocol::FDXB; }
QString FDXBParser::name() const { return QStringLiteral("FDX-B"); }

int FDXBParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression animalId(
        QStringLiteral("(?i)Animal\\s+ID\\s*[:.]"));
    static const QRegularExpression animalCode(
        QStringLiteral("(?i)Animal\\s+code\\s*\\.{3,}"));
    static const QRegularExpression rawHex(
        QStringLiteral("(?i)Raw\\s+ID\\s+Hex:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)FDX-?B"));

    const bool hasNational = normalizedText.contains(QStringLiteral("National Code"), Qt::CaseInsensitive)
            || normalizedText.contains(QStringLiteral("National ID code"), Qt::CaseInsensitive);

    if ((animalId.match(normalizedText).hasMatch()
         || animalCode.match(normalizedText).hasMatch()) && hasNational) {
        if (evidence) evidence->append(QStringLiteral("FDX-B animal identity block"));
        return 100;
    }
    if ((animalId.match(normalizedText).hasMatch() || animalCode.match(normalizedText).hasMatch())) {
        if (evidence) evidence->append(QStringLiteral("FDX-B animal code line"));
        return 90;
    }
    if (rawHex.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("FDX-B raw id line"));
        return 85;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("FDX-B keyword"));
        return 60;
    }
    return 0;
}

LFParseResult FDXBParser::parse(const QString &normalizedText,
                                const QString &originalText) const
{
    static const QRegularExpression animalId(
        QStringLiteral("Animal\\s+ID\\s*[:.]\\s*(\\d{3,4})\\s*-\\s*(\\d{1,12})"));
    static const QRegularExpression animalCode(
        QStringLiteral("Animal\\s+code\\s*\\.{3,}\\s*(\\d{3})\\s+(\\d{1,12})"));
    static const QRegularExpression country4(
        QStringLiteral("CountryCode:\\s*(\\d{1,4})"));
    static const QRegularExpression national(
        QStringLiteral("National\\s+Code:\\s*(\\d{1,12})"));
    static const QRegularExpression country3(
        QStringLiteral("Country\\s+code\\s*\\.{3,}\\s*(\\d{3})"));
    static const QRegularExpression nationalId(
        QStringLiteral("National\\s+ID\\s+code\\s*\\.{3,}\\s*(\\d{1,12})"));
    static const QRegularExpression rawHex(
        QStringLiteral("Raw\\s+ID\\s+Hex:\\s*([0-9A-Fa-f ]{8,})"));
    static const QRegularExpression rawDots(
        QStringLiteral("Raw\\s*\\.{3,}\\s*([0-9A-Fa-f ]{8,})"));
    static const QRegularExpression animalBit(
        QStringLiteral("Animal\\s+(?:Tag|bit)\\s*\\.{0,}\\s*[:.]?\\s*(True|False)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression dataBlock(
        QStringLiteral("(?:Has\\s+extended\\s+data|Data\\s+block\\s+flag)\\s*\\.{0,}\\s*[:.]?\\s*(True|False)\\s*\\[?\\s*0x?([0-9A-Fa-f]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression crcLine(
        QStringLiteral("CRC(?:-16)?\\s*\\.{0,}\\s*[:.]?\\s*0x?([0-9A-Fa-f]{4})[^\\n]*?(ok|fail)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rudi(
        QStringLiteral("RUDI-bit\\s*\\.{3,}\\s*(True|False)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reserved(
        QStringLiteral("Reserved(?:/RFU|\\s+field)?\\s*\\.{0,}\\s*[:.]?\\s*(\\d{1,3})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("FDX-B");
    result.sourceText = originalText;

    QString country;
    QString nationalValue;

    QRegularExpressionMatch match = animalId.match(normalizedText);
    if (!match.hasMatch())
        match = animalCode.match(normalizedText);
    if (match.hasMatch()) {
        country = match.captured(1);
        nationalValue = match.captured(2);
    } else {
        country = LfParse::capture(normalizedText, country4);
        if (country.isEmpty())
            country = LfParse::capture(normalizedText, country3);
        nationalValue = LfParse::capture(normalizedText, national);
        if (nationalValue.isEmpty())
            nationalValue = LfParse::capture(normalizedText, nationalId);
    }

    if (!country.isEmpty())
        result.addExtra(QStringLiteral("countryCode"), country);

    quint64 nationalNumber = 0;
    if (!nationalValue.isEmpty() && LfParse::toU64(nationalValue, &nationalNumber)) {
        result.cardNumber.set(nationalNumber);
        result.decimalId.set(nationalNumber);
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, rawHex), 8, 64);
    if (result.rawData.isEmpty())
        result.rawData = LfParse::hex(LfParse::capture(normalizedText, rawDots), 8, 64);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    match = animalBit.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("animalBit"), match.captured(1).toLower());
    match = dataBlock.match(normalizedText);
    if (match.hasMatch()) {
        result.addExtra(QStringLiteral("extendedData"), match.captured(1).toLower());
        if (!match.captured(2).isEmpty())
            result.addExtra(QStringLiteral("extendedValue"), match.captured(2));
    }
    match = crcLine.match(normalizedText);
    if (match.hasMatch()) {
        result.addExtra(QStringLiteral("crc"), match.captured(1));
        result.addExtra(QStringLiteral("crcState"), match.captured(2).toLower());
        if (match.captured(2).compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0)
            result.addWarning(QStringLiteral("FDX-B CRC check failed on the client side."));
    }
    match = rudi.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("rudiBit"), match.captured(1).toLower());
    match = reserved.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("reserved"), match.captured(1));

    result.format = QStringLiteral("FDX-B");

    if (result.cardNumber.isPresent() || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.cardNumber.isPresent() && !result.rawData.isEmpty())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("FDX-B marker found but no animal identity was extracted."));
    }
    return result;
}
