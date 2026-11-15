#include "lf/parsers/gallagherparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol GallagherParser::protocol() const { return Lf::Protocol::Gallagher; }
QString GallagherParser::name() const { return QStringLiteral("Gallagher"); }

int GallagherParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression region(
        QStringLiteral("(?i)GALLAGHER\\s*-\\s*Region:"));
    static const QRegularExpression facility(
        QStringLiteral("(?i)GALLAGHER\\b[^\\n]*\\bFacility:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)\\bGALLAGHER\\b"));

    if (region.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Gallagher region line"));
        return 100;
    }
    if (facility.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Gallagher facility line"));
        return 90;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Gallagher keyword"));
        return 55;
    }
    return 0;
}

LFParseResult GallagherParser::parse(const QString &normalizedText,
                                     const QString &originalText) const
{
    static const QRegularExpression single(
        QStringLiteral("GALLAGHER\\s*-\\s*Region:\\s*(\\d+)\\s+Facility:\\s*(\\d+)\\s+Card\\s+No\\.?:\\s*(\\d+)\\s+Issue\\s+Level:\\s*(\\d+)"));
    static const QRegularExpression region(QStringLiteral("Region(?:\\s+Code)?:?\\s*(\\d+)"));
    static const QRegularExpression facility(QStringLiteral("Facility(?:\\s+Code)?:?\\s*(\\d+)"));
    static const QRegularExpression cardNo(QStringLiteral("Card\\s+No\\.?:?\\s*(\\d+)"));
    static const QRegularExpression issue(QStringLiteral("Issue\\s+Level:?\\s*(\\d+)"));
    static const QRegularExpression displayed(QStringLiteral("Displayed:\\s*([A-Za-z]\\d+)"));
    static const QRegularExpression raw(QStringLiteral("Raw:\\s*([0-9A-Fa-f]{16,32})"));
    static const QRegularExpression crc(
        QStringLiteral("CRC:\\s*([0-9A-Fa-f]{2})\\s*-\\s*([0-9A-Fa-f]{2})\\s*\\(\\s*(ok|fail)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Gallagher");
    result.sourceText = originalText;
    result.format = QStringLiteral("Gallagher");

    const QRegularExpressionMatch singleMatch = single.match(normalizedText);
    QString regionValue;
    QString fcValue;
    QString cnValue;
    QString ilValue;

    if (singleMatch.hasMatch()) {
        regionValue = singleMatch.captured(1);
        fcValue = singleMatch.captured(2);
        cnValue = singleMatch.captured(3);
        ilValue = singleMatch.captured(4);
    } else {
        regionValue = LfParse::capture(normalizedText, region);
        fcValue = LfParse::capture(normalizedText, facility);
        cnValue = LfParse::capture(normalizedText, cardNo);
        ilValue = LfParse::capture(normalizedText, issue);
    }

    if (!regionValue.isEmpty())
        result.addExtra(QStringLiteral("regionCode"), regionValue);
    if (!ilValue.isEmpty())
        result.addExtra(QStringLiteral("issueLevel"), ilValue);
    if (!fcValue.isEmpty()) {
        quint32 fc = 0;
        if (LfParse::toU32(fcValue, &fc))
            result.facilityCode.set(fc);
    }
    if (!cnValue.isEmpty()) {
        quint64 cn = 0;
        if (LfParse::toU64(cnValue, &cn))
            result.cardNumber.set(cn);
    }

    const QString displayedValue = LfParse::capture(normalizedText, displayed);
    if (!displayedValue.isEmpty())
        result.addExtra(QStringLiteral("displayed"), displayedValue);

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 16, 32);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    const QRegularExpressionMatch crcMatch = crc.match(normalizedText);
    if (crcMatch.hasMatch()) {
        result.addExtra(QStringLiteral("crc"), crcMatch.captured(1).toUpper());
        result.addExtra(QStringLiteral("crcState"), crcMatch.captured(3).toLower());
        if (crcMatch.captured(1).compare(crcMatch.captured(2), Qt::CaseInsensitive) != 0)
            result.addWarning(QStringLiteral("Gallagher CRC mismatch reported by the client."));
    }

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.facilityCode.isPresent() && result.cardNumber.isPresent())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else if (!result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Gallagher marker found but no identity fields were extracted."));
    }
    return result;
}
