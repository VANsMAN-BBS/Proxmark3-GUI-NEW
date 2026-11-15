#include "lf/parsers/nedapparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol NedapParser::protocol() const { return Lf::Protocol::Nedap; }
QString NedapParser::name() const { return QStringLiteral("NEDAP"); }

int NedapParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression full(
        QStringLiteral("(?i)NEDAP\\s*\\([^)]*\\)\\s*-\\s*ID:"));
    static const QRegularExpression subtype(
        QStringLiteral("(?i)NEDAP\\b[^\\n]*\\bsubtype:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)\\bNEDAP\\b"));

    if (full.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NEDAP identified line"));
        return 100;
    }
    if (subtype.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NEDAP subtype line"));
        return 90;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NEDAP keyword"));
        return 50;
    }
    return 0;
}

LFParseResult NedapParser::parse(const QString &normalizedText,
                                 const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("NEDAP\\s*\\(([^)]*)\\)\\s*-\\s*ID:\\s*(\\d+)\\s+subtype:\\s*(\\d+)\\s+customer\\s+code:\\s*(\\d+)\\s*/\\s*0x([0-9A-Fa-f]+)"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f ]{8,40})"));
    static const QRegularExpression secondCard(
        QStringLiteral("Second\\s+Card:\\s*(\\d+)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("NEDAP");
    result.sourceText = originalText;

    const QRegularExpressionMatch match = main.match(normalizedText);
    if (match.hasMatch()) {
        const QString variant = match.captured(1).trimmed();
        quint64 id = 0;
        if (LfParse::toU64(match.captured(2), &id)) {
            result.cardNumber.set(id);
            result.decimalId.set(id);
        }
        result.addExtra(QStringLiteral("variant"), variant);
        result.addExtra(QStringLiteral("subtype"), match.captured(3));
        result.addExtra(QStringLiteral("customerCode"), match.captured(4));
        result.addExtra(QStringLiteral("customerCodeHex"), match.captured(5).toUpper());
        result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 8, 40);
        result.hexId = result.rawData;
        result.format = QStringLiteral("NEDAP %1").arg(variant);
    } else {
        result.format = QStringLiteral("NEDAP");
    }

    const QString second = LfParse::capture(normalizedText, secondCard);
    if (!second.isEmpty())
        result.addExtra(QStringLiteral("secondCard"), second);

    if (result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                     : Lf::Confidence::High;
    } else if (!result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("NEDAP marker found but no badge ID was extracted."));
    }
    return result;
}
