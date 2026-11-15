#include "lf/parsers/idteckparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol IDTECKParser::protocol() const { return Lf::Protocol::IDTECK; }
QString IDTECKParser::name() const { return QStringLiteral("IDTECK"); }

int IDTECKParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression tag(
        QStringLiteral("(?i)IDTECK\\s+Tag\\s+Found"));
    static const QRegularExpression cardId(
        QStringLiteral("(?i)IDTECK\\b[^\\n]*\\bCard\\s+ID"));

    if (tag.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("IDTECK tag found line"));
        return 100;
    }
    if (cardId.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("IDTECK card id line"));
        return 85;
    }
    if (normalizedText.contains(QStringLiteral("IDTECK"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("IDTECK keyword"));
        return 55;
    }
    return 0;
}

LFParseResult IDTECKParser::parse(const QString &normalizedText,
                                  const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("IDTECK\\s+Tag\\s+Found:\\s*Card\\s+ID\\s+(\\d+)\\s*\\(\\s*0x([0-9A-Fa-f]+)\\s*\\)"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{12,20})"));
    static const QRegularExpression checksum(
        QStringLiteral("chksum\\s+0x([0-9A-Fa-f]{2})\\s*\\(\\s*(ok|fail)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("IDTECK");
    result.sourceText = originalText;
    result.format = QStringLiteral("IDTECK");

    const QRegularExpressionMatch match = main.match(normalizedText);
    if (match.hasMatch()) {
        quint64 id = 0;
        if (LfParse::toU64(match.captured(1), &id)) {
            result.cardNumber.set(id);
            result.decimalId.set(id);
        }
        result.addExtra(QStringLiteral("cardIdHex"), match.captured(2).toUpper());
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 12, 20);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    const QRegularExpressionMatch checksumMatch = checksum.match(normalizedText);
    if (checksumMatch.hasMatch()) {
        result.addExtra(QStringLiteral("checksum"), checksumMatch.captured(1).toUpper());
        result.addExtra(QStringLiteral("checksumState"), checksumMatch.captured(2).toLower());
        if (checksumMatch.captured(2).compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0)
            result.addWarning(QStringLiteral("IDTECK checksum failed on the client side."));
    }

    if (result.cardNumber.isPresent() || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.cardNumber.isPresent() ? Lf::Confidence::High
                                                          : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("IDTECK marker found but no card ID was extracted."));
    }
    return result;
}
