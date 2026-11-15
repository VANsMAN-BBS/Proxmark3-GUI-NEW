#include "lf/parsers/visa2000parser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol Visa2000Parser::protocol() const { return Lf::Protocol::Visa2000; }
QString Visa2000Parser::name() const { return QStringLiteral("Visa2000"); }

int Visa2000Parser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression card(
        QStringLiteral("(?i)Visa2000\\s*-\\s*Card"));
    static const QRegularExpression keyword(QStringLiteral("(?i)\\bVisa2000\\b"));

    if (card.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Visa2000 card line"));
        return 100;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Visa2000 keyword"));
        return 55;
    }
    return 0;
}

LFParseResult Visa2000Parser::parse(const QString &normalizedText,
                                    const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Visa2000\\s*-\\s*Card\\s+(\\d+)"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{16,24})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Visa2000");
    result.sourceText = originalText;
    result.format = QStringLiteral("Visa2000");

    const QString card = LfParse::capture(normalizedText, main);
    quint64 value = 0;
    if (!card.isEmpty() && LfParse::toU64(card, &value)) {
        result.cardNumber.set(value);
        result.decimalId.set(value);
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 16, 24);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (result.cardNumber.isPresent() || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.cardNumber.isPresent() ? Lf::Confidence::High
                                                          : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Visa2000 marker found but no card number was extracted."));
    }
    return result;
}
