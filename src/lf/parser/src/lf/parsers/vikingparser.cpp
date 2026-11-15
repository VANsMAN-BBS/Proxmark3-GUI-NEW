#include "lf/parsers/vikingparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol VikingParser::protocol() const { return Lf::Protocol::Viking; }
QString VikingParser::name() const { return QStringLiteral("Viking"); }

int VikingParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression card(
        QStringLiteral("(?i)Viking\\s*-\\s*Card"));
    static const QRegularExpression withRaw(
        QStringLiteral("(?i)Viking\\b[^\\n]*\\bRaw:"));

    if (card.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Viking card line"));
        return 100;
    }
    if (withRaw.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Viking raw line"));
        return 80;
    }
    if (normalizedText.contains(QStringLiteral("Viking"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Viking keyword"));
        return 50;
    }
    return 0;
}

LFParseResult VikingParser::parse(const QString &normalizedText,
                                  const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Viking\\s*-\\s*Card\\s+([0-9A-Fa-f]{6,10})"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{12,20})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Viking");
    result.sourceText = originalText;
    result.format = QStringLiteral("Viking");

    const QString card = LfParse::capture(normalizedText, main);
    if (!card.isEmpty()) {
        result.hexId = card.toUpper();
        result.addExtra(QStringLiteral("cardHex"), result.hexId);
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 12, 20);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (!result.hexId.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = !result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                      : Lf::Confidence::Low;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Viking marker found but no card ID was extracted."));
    }
    return result;
}
