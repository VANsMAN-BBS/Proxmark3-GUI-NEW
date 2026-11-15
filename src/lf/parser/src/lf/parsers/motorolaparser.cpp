#include "lf/parsers/motorolaparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol MotorolaParser::protocol() const { return Lf::Protocol::Motorola; }
QString MotorolaParser::name() const { return QStringLiteral("Motorola"); }

int MotorolaParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression fmtLine(
        QStringLiteral("(?i)Motorola\\s*-\\s*fmt:"));
    static const QRegularExpression fcLine(
        QStringLiteral("(?i)Motorola\\b[^\\n]*\\bFC:"));

    if (fmtLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Motorola format line"));
        return 100;
    }
    if (fcLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Motorola FC line"));
        return 85;
    }
    if (normalizedText.contains(QStringLiteral("Motorola"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Motorola keyword"));
        return 50;
    }
    return 0;
}

LFParseResult MotorolaParser::parse(const QString &normalizedText,
                                    const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Motorola\\s*-\\s*fmt:\\s*(\\d+)\\s+FC:\\s*(\\d+)\\s+Card:\\s*(\\d+)"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{12,20})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Motorola");
    result.sourceText = originalText;

    const QRegularExpressionMatch match = main.match(normalizedText);
    if (match.hasMatch()) {
        const int bits = match.captured(1).toInt();
        result.bitLength.set(bits);
        quint32 fc = 0;
        if (LfParse::toU32(match.captured(2), &fc))
            result.facilityCode.set(fc);
        quint64 cn = 0;
        if (LfParse::toU64(match.captured(3), &cn))
            result.cardNumber.set(cn);
        result.format = QStringLiteral("Motorola %1-bit").arg(bits);
    } else {
        result.format = QStringLiteral("Motorola");
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 12, 20);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.facilityCode.isPresent() && result.cardNumber.isPresent())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Motorola marker found but no card fields were extracted."));
    }
    return result;
}
