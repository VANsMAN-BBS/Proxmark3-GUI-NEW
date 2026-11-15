#include "lf/parsers/pyramidparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol PyramidParser::protocol() const { return Lf::Protocol::Pyramid; }
QString PyramidParser::name() const { return QStringLiteral("Pyramid"); }

int PyramidParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression lenLine(
        QStringLiteral("(?i)Pyramid\\s*-\\s*len:"));
    static const QRegularExpression fcLine(
        QStringLiteral("(?i)Pyramid\\b[^\\n]*\\bFC:"));

    if (lenLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Pyramid length line"));
        return 100;
    }
    if (fcLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Pyramid FC line"));
        return 85;
    }
    if (normalizedText.contains(QStringLiteral("Pyramid"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Pyramid keyword"));
        return 50;
    }
    return 0;
}

LFParseResult PyramidParser::parse(const QString &normalizedText,
                                   const QString &originalText) const
{
    static const QRegularExpression known(
        QStringLiteral("Pyramid\\s*-\\s*len:\\s*(\\d+)\\s*,?\\s*FC:\\s*(\\d+)\\s+Card:\\s*(\\d+)[^\\n]*?Raw:\\s*([0-9A-Fa-f]{24,40})"));
    static const QRegularExpression unknown(
        QStringLiteral("Pyramid\\s*-\\s*len:\\s*(\\d+)\\s+-unknown-\\s+Card:\\s*(\\d+)[^\\n]*?Raw:\\s*([0-9A-Fa-f]{24,40})"));
    static const QRegularExpression rawOnly(
        QStringLiteral("Pyramid[^\\n]*?Raw:\\s*([0-9A-Fa-f]{24,40})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Pyramid");
    result.sourceText = originalText;

    int bitLength = 0;
    bool unknownFormat = false;

    QRegularExpressionMatch match = known.match(normalizedText);
    if (match.hasMatch()) {
        bitLength = match.captured(1).toInt();
        quint32 fc = 0;
        if (LfParse::toU32(match.captured(2), &fc))
            result.facilityCode.set(fc);
        quint64 cn = 0;
        if (LfParse::toU64(match.captured(3), &cn))
            result.cardNumber.set(cn);
        result.rawData = LfParse::hex(match.captured(4), 24, 40);
    } else {
        match = unknown.match(normalizedText);
        if (match.hasMatch()) {
            unknownFormat = true;
            bitLength = match.captured(1).toInt();
            quint64 cn = 0;
            if (LfParse::toU64(match.captured(2), &cn))
                result.cardNumber.set(cn);
            result.rawData = LfParse::hex(match.captured(3), 24, 40);
            result.addWarning(QStringLiteral("Pyramid length is not a known format; "
                                             "the value is reported as the client's card number."));
        } else {
            result.rawData = LfParse::hex(LfParse::capture(normalizedText, rawOnly), 24, 40);
        }
    }

    if (bitLength > 0)
        result.bitLength.set(bitLength);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (bitLength > 0)
        result.format = unknownFormat
                ? QStringLiteral("Pyramid %1-bit (unknown)").arg(bitLength)
                : QStringLiteral("Pyramid %1-bit").arg(bitLength);
    else
        result.format = QStringLiteral("Pyramid");

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()
            || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = unknownFormat ? Lf::Confidence::Low
                                          : (result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                                      : Lf::Confidence::High);
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Pyramid marker found but no card fields were extracted."));
    }
    return result;
}
