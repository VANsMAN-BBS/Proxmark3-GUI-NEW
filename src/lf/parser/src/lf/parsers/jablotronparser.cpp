#include "lf/parsers/jablotronparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol JablotronParser::protocol() const { return Lf::Protocol::Jablotron; }
QString JablotronParser::name() const { return QStringLiteral("Jablotron"); }

int JablotronParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression card(
        QStringLiteral("(?i)Jablotron\\s*-\\s*Card:"));

    if (card.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Jablotron card line"));
        return 100;
    }
    if (normalizedText.contains(QStringLiteral("Jablotron"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Jablotron keyword"));
        return 50;
    }
    return 0;
}

LFParseResult JablotronParser::parse(const QString &normalizedText,
                                     const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Jablotron\\s*-\\s*Card:\\s*([0-9A-Fa-f]{3,16})"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{12,20})"));
    static const QRegularExpression printed(
        QStringLiteral("Printed:\\s*(1410-[0-9A-Fa-f-]+)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Jablotron");
    result.sourceText = originalText;
    result.format = QStringLiteral("Jablotron");

    const QString card = LfParse::capture(normalizedText, main);
    if (!card.isEmpty()) {
        result.hexId = card.toUpper();
        result.addExtra(QStringLiteral("cardHex"), result.hexId);
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 12, 20);
    if (!result.rawData.isEmpty())
        result.addExtra(QStringLiteral("rawPayload"), result.rawData);

    const QString printedValue = LfParse::capture(normalizedText, printed);
    if (!printedValue.isEmpty())
        result.addExtra(QStringLiteral("printed"), printedValue);

    if (!result.hexId.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                     : Lf::Confidence::High;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Jablotron marker found but no card value was extracted."));
    }
    return result;
}
