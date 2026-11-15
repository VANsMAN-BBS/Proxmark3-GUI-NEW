#include "lf/parsers/pacparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol PACParser::protocol() const { return Lf::Protocol::PAC; }
QString PACParser::name() const { return QStringLiteral("PAC/Stanley"); }

int PACParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression card(
        QStringLiteral("(?i)PAC/Stanley\\s*-\\s*Card:"));
    static const QRegularExpression keyword(
        QStringLiteral("(?i)PAC/Stanley"));

    if (card.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("PAC/Stanley card line"));
        return 100;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("PAC/Stanley keyword"));
        return 60;
    }
    return 0;
}

LFParseResult PACParser::parse(const QString &normalizedText,
                               const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("PAC/Stanley\\s*-\\s*Card:\\s*([0-9A-Fa-f]{4,16})"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{24,40})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("PAC/Stanley");
    result.sourceText = originalText;
    result.format = QStringLiteral("PAC/Stanley");

    const QString card = LfParse::capture(normalizedText, main);
    if (!card.isEmpty()) {
        result.hexId = card.toUpper();
        result.addExtra(QStringLiteral("pacCardHex"), result.hexId);
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 24, 40);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (!result.hexId.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                     : Lf::Confidence::High;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("PAC/Stanley marker found but no card value was extracted."));
    }
    return result;
}
