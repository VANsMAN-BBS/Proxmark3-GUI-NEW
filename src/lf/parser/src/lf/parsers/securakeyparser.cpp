#include "lf/parsers/securakeyparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol SecuraKeyParser::protocol() const { return Lf::Protocol::SecuraKey; }
QString SecuraKeyParser::name() const { return QStringLiteral("SecuraKey"); }

int SecuraKeyParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression longLine(
        QStringLiteral("(?i)Securakey\\s*-\\s*len:"));
    static const QRegularExpression shortLine(
        QStringLiteral("(?i)Securakey\\s+Short\\s*-\\s*CN:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)Securakey"));

    if (longLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Securakey length line"));
        return 100;
    }
    if (shortLine.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Securakey short line"));
        return 95;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Securakey keyword"));
        return 50;
    }
    return 0;
}

LFParseResult SecuraKeyParser::parse(const QString &normalizedText,
                                     const QString &originalText) const
{
    static const QRegularExpression longLine(
        QStringLiteral("Securakey\\s*-\\s*len:\\s*(\\d+)\\s+FC:\\s*(?:0x)?([0-9A-Fa-f]+)\\s+Card:\\s*(\\d+)"));
    static const QRegularExpression shortLine(
        QStringLiteral("Securakey\\s+Short\\s*-\\s*CN:\\s*(\\d+)"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{12,24})"));
    static const QRegularExpression wiegand(
        QStringLiteral("Wiegand:\\s*([0-9A-Fa-f]{4,16}).*?parity\\s*\\(\\s*(ok|fail)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("SecuraKey");
    result.sourceText = originalText;

    QRegularExpressionMatch match = longLine.match(normalizedText);
    if (match.hasMatch()) {
        const int bits = match.captured(1).toInt();
        result.bitLength.set(bits);
        quint32 fc = 0;
        bool ok = false;
        const quint32 parsed = match.captured(2).toUInt(&ok, 16);
        if (ok) {
            fc = parsed;
            result.facilityCode.set(fc);
            result.addExtra(QStringLiteral("facilityCodeHex"),
                            QStringLiteral("0x%1").arg(QString::number(fc, 16).toUpper()));
        }
        quint64 cn = 0;
        if (LfParse::toU64(match.captured(3), &cn))
            result.cardNumber.set(cn);
        result.format = QStringLiteral("SecuraKey %1-bit").arg(bits);
    } else {
        match = shortLine.match(normalizedText);
        if (match.hasMatch()) {
            quint64 cn = 0;
            if (LfParse::toU64(match.captured(1), &cn))
                result.cardNumber.set(cn);
            result.format = QStringLiteral("SecuraKey Short");
        } else {
            result.format = QStringLiteral("SecuraKey");
        }
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 12, 24);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    const QRegularExpressionMatch wiegandMatch = wiegand.match(normalizedText);
    if (wiegandMatch.hasMatch()) {
        result.addExtra(QStringLiteral("wiegand"), wiegandMatch.captured(1).toUpper());
        result.addExtra(QStringLiteral("parity"), wiegandMatch.captured(2).toLower());
        if (wiegandMatch.captured(2).compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0)
            result.addWarning(QStringLiteral("SecuraKey Wiegand parity failed on the client side."));
    }

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.cardNumber.isPresent() && !result.rawData.isEmpty())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else if (!result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("SecuraKey marker found but no card fields were extracted."));
    }
    return result;
}
