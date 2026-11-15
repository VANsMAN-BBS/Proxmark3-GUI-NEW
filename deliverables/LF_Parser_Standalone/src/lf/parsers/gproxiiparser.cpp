#include "lf/parsers/gproxiiparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol GProxIIParser::protocol() const { return Lf::Protocol::GProxII; }
QString GProxIIParser::name() const { return QStringLiteral("G-Prox-II"); }

int GProxIIParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression lenLine(
        QStringLiteral("(?i)G-?Prox-?II\\s*-\\s*(?:Len|Unknown\\s+len):"));
    static const QRegularExpression xorLine(
        QStringLiteral("(?i)G-?Prox\\b[^\\n]*\\bxor:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)G-?Prox"));

    if (lenLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("G-Prox-II length line"));
        return 100;
    }
    if (xorLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("G-Prox-II xor line"));
        return 85;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("G-Prox-II keyword"));
        return 50;
    }
    return 0;
}

LFParseResult GProxIIParser::parse(const QString &normalizedText,
                                   const QString &originalText) const
{
    static const QRegularExpression known(
        QStringLiteral("G-?Prox-?II\\s*-\\s*Len:\\s*(\\d+)\\s+FC:\\s*(\\d+)\\s+Card:\\s*(\\d+)\\s+xor:\\s*(\\d+)[^\\n]*?Raw:\\s*([0-9A-Fa-f ]{8,40})"));
    static const QRegularExpression unknown(
        QStringLiteral("G-?Prox-?II\\s*-\\s*Unknown\\s+len:\\s*(\\d+)\\s+xor:\\s*(\\d+)[^\\n]*?Raw:\\s*([0-9A-Fa-f ]{8,40})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("G-Prox-II");
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
        result.addExtra(QStringLiteral("xor"), match.captured(4));
        result.rawData = LfParse::hex(match.captured(5), 8, 40);
    } else {
        match = unknown.match(normalizedText);
        if (match.hasMatch()) {
            unknownFormat = true;
            bitLength = match.captured(1).toInt();
            result.addExtra(QStringLiteral("xor"), match.captured(2));
            result.rawData = LfParse::hex(match.captured(3), 8, 40);
            result.addWarning(QStringLiteral("G-Prox-II length is unknown; only the xor key and raw payload are available."));
        }
    }

    if (bitLength > 0)
        result.bitLength.set(bitLength);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (bitLength > 0)
        result.format = unknownFormat
                ? QStringLiteral("G-Prox-II %1-bit (unknown)").arg(bitLength)
                : QStringLiteral("G-Prox-II %1-bit").arg(bitLength);
    else
        result.format = QStringLiteral("G-Prox-II");

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()
            || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = unknownFormat ? Lf::Confidence::Low
                                          : (result.facilityCode.isPresent() && result.cardNumber.isPresent()
                                             ? Lf::Confidence::High : Lf::Confidence::Medium);
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("G-Prox-II marker found but no fields were extracted."));
    }
    return result;
}
