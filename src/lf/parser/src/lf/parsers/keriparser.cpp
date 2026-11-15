#include "lf/parsers/keriparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol KeriParser::protocol() const { return Lf::Protocol::Keri; }
QString KeriParser::name() const { return QStringLiteral("Keri"); }

int KeriParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression internal(
        QStringLiteral("(?i)KERI\\s*-\\s*Internal\\s+ID"));
    static const QRegularExpression nxt(
        QStringLiteral("(?i)KERI\\s+NXT\\s*-\\s*Raw"));
    static const QRegularExpression ms(
        QStringLiteral("(?i)(?:Descrambled|Scrambled)\\s+MS\\s*-\\s*FC:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)\\bKERI\\b"));

    if (internal.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Keri internal ID line"));
        return 100;
    }
    if (nxt.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Keri NXT line"));
        return 100;
    }
    if (ms.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Keri MS line"));
        return 95;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Keri keyword"));
        return 60;
    }
    return 0;
}

LFParseResult KeriParser::parse(const QString &normalizedText,
                                const QString &originalText) const
{
    static const QRegularExpression internal(
        QStringLiteral("KERI\\s*-\\s*Internal\\s+ID:\\s*(\\d+)[^\\n]*?Raw:\\s*([0-9A-Fa-f]{12,20})"));
    static const QRegularExpression nxt(
        QStringLiteral("KERI\\s+NXT\\s*-\\s*Raw:\\s*([0-9A-Fa-f]{24,32})"));
    static const QRegularExpression scrambled(
        QStringLiteral("Scrambled\\s+MS\\s*-\\s*FC:\\s*(\\d+)\\s*Card:\\s*(\\d+)[^\\n]*?Raw:\\s*(?:E0000000)?([0-9A-Fa-f]{8,20})"));
    static const QRegularExpression descrambled(
        QStringLiteral("Descrambled\\s+MS\\s*-\\s*FC:\\s*(\\d+)\\s*Card:\\s*(\\d+)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Keri");
    result.sourceText = originalText;

    QRegularExpressionMatch match = internal.match(normalizedText);
    if (match.hasMatch()) {
        result.format = QStringLiteral("Keri Internal");
        result.addExtra(QStringLiteral("keriType"), QStringLiteral("internal"));
        quint64 id = 0;
        if (LfParse::toU64(match.captured(1), &id)) {
            result.cardNumber.set(id);
            result.decimalId.set(id);
        }
        result.rawData = LfParse::hex(match.captured(2), 12, 20);
        result.hexId = result.rawData;
    } else {
        match = nxt.match(normalizedText);
        if (match.hasMatch()) {
            result.format = QStringLiteral("Keri NXT");
            result.addExtra(QStringLiteral("keriType"), QStringLiteral("nxt"));
            result.rawData = LfParse::hex(match.captured(1), 24, 32);
            result.hexId = result.rawData;
        } else {
            match = scrambled.match(normalizedText);
            if (match.hasMatch()) {
                result.format = QStringLiteral("Keri MS (scrambled)");
                result.addExtra(QStringLiteral("keriType"), QStringLiteral("ms-scrambled"));
                quint32 fc = 0;
                if (LfParse::toU32(match.captured(1), &fc))
                    result.facilityCode.set(fc);
                quint64 cn = 0;
                if (LfParse::toU64(match.captured(2), &cn))
                    result.cardNumber.set(cn);
                result.rawData = LfParse::hex(match.captured(3), 8, 20);
                result.hexId = result.rawData;
                result.addWarning(QStringLiteral("Keri scrambled values are reported as printed by the client."));
            } else {
                match = descrambled.match(normalizedText);
                if (match.hasMatch()) {
                    result.format = QStringLiteral("Keri MS");
                    result.addExtra(QStringLiteral("keriType"), QStringLiteral("ms"));
                    quint32 fc = 0;
                    if (LfParse::toU32(match.captured(1), &fc))
                        result.facilityCode.set(fc);
                    quint64 cn = 0;
                    if (LfParse::toU64(match.captured(2), &cn))
                        result.cardNumber.set(cn);
                }
            }
        }
    }

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()
            || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                     : Lf::Confidence::High;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Keri marker found but no identity was extracted."));
    }
    return result;
}
