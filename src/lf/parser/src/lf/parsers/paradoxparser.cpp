#include "lf/parsers/paradoxparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol ParadoxParser::protocol() const { return Lf::Protocol::Paradox; }
QString ParadoxParser::name() const { return QStringLiteral("Paradox"); }

int ParadoxParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression idLine(
        QStringLiteral("(?i)Paradox\\s*-\\s*ID:"));
    static const QRegularExpression fcLine(
        QStringLiteral("(?i)Paradox\\b[^\\n]*\\bFC:"));

    if (idLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Paradox ID line"));
        return 100;
    }
    if (fcLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Paradox FC line"));
        return 85;
    }
    if (normalizedText.contains(QStringLiteral("Paradox"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Paradox keyword"));
        return 50;
    }
    return 0;
}

LFParseResult ParadoxParser::parse(const QString &normalizedText,
                                   const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Paradox\\s*-\\s*ID:\\s*([0-9A-Fa-f]{8,16})\\s*FC:\\s*(\\d+)\\s*Card:\\s*(\\d+)"));
    static const QRegularExpression checksum(
        QStringLiteral("Checksum:\\s*([0-9A-Fa-f]{2})"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{16,24})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Paradox");
    result.sourceText = originalText;

    const QRegularExpressionMatch match = main.match(normalizedText);
    if (match.hasMatch()) {
        result.hexId = match.captured(1).toUpper();
        quint32 fc = 0;
        if (LfParse::toU32(match.captured(2), &fc))
            result.facilityCode.set(fc);
        quint64 cn = 0;
        if (LfParse::toU64(match.captured(3), &cn))
            result.cardNumber.set(cn);

        const QString checksumValue = LfParse::capture(normalizedText, checksum);
        if (!checksumValue.isEmpty())
            result.addExtra(QStringLiteral("checksum"), checksumValue.toUpper());

        result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 16, 24);
        if (!result.rawData.isEmpty())
            result.addExtra(QStringLiteral("rawPayload"), result.rawData);
    }

    result.format = QStringLiteral("Paradox");

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()
            || !result.hexId.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.facilityCode.isPresent() && result.cardNumber.isPresent())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Paradox marker found but no card fields were extracted."));
    }
    return result;
}
