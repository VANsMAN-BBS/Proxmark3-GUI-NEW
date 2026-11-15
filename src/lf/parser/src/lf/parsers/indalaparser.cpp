#include "lf/parsers/indalaparser.h"

#include <QRegularExpression>
#include <QStringList>

#include "lf/lftextnormalizer.h"

Lf::Protocol IndalaParser::protocol() const
{
    return Lf::Protocol::Indala;
}

QString IndalaParser::name() const
{
    return QStringLiteral("Indala");
}

int IndalaParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression lenWithRaw(
        QStringLiteral("(?i)\\bIndala\\s*\\(len\\s*\\d+\\)\\s*Raw:"));
    static const QRegularExpression lenOnly(
        QStringLiteral("(?i)\\bIndala\\s*\\(len\\s*\\d+\\)"));
    static const QRegularExpression fmt26(
        QStringLiteral("(?i)\\bFmt\\s*26\\s+FC:\\s*\\d+\\s+Card:\\s*\\d+"));
    static const QRegularExpression rawLine(
        QStringLiteral("(?i)\\bIndala\\b[^\\n]*\\bRaw:"));
    static const QRegularExpression keyword(
        QStringLiteral("(?i)\\bIndala\\b"));

    if (lenWithRaw.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Indala length line with raw payload"));
        return 100;
    }
    if (lenOnly.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Indala length line"));
        return 90;
    }
    if (fmt26.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Indala 26-bit decoded fields"));
        return 85;
    }
    if (rawLine.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Indala raw payload"));
        return 60;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Indala keyword"));
        return 45;
    }
    return 0;
}

LFParseResult IndalaParser::parse(const QString &normalizedText,
                                  const QString &originalText) const
{
    static const QRegularExpression lenRaw(
        QStringLiteral("Indala\\s*\\(len\\s*(\\d+)\\)\\s*Raw:\\s*([0-9A-Fa-f]{8,64})"));
    static const QRegularExpression fmt(
        QStringLiteral("Fmt\\s*(\\d+)\\s+FC:\\s*(\\d+)\\s+Card:\\s*(\\d+)"));
    static const QRegularExpression parity(
        QStringLiteral("Fmt\\s*\\d+\\s+FC:\\s*\\d+\\s+Card:\\s*\\d+\\s+Parity:\\s*(\\d+)"));
    static const QRegularExpression internalId(
        QStringLiteral("Internal ID\\s*\\.*\\s*(\\d+)"));
    static const QRegularExpression heden2l(
        QStringLiteral("Heden-2L\\s*\\.*\\s*(\\d+)"));
    static const QRegularExpression keyscan(
        QStringLiteral("Keyscan\\s+40010\\s*\\.?\\s*FC:\\s*(\\d+)\\s+CN:\\s*(\\d+)"));
    static const QRegularExpression printed(
        QStringLiteral("Printed\\s*\\.*\\s*__(\\d+)__\\s*\\(\\s*0x([0-9A-Fa-f]+)\\s*\\)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Indala");
    result.sourceText = originalText;

    int frameLength = 0;
    bool unknownLength = false;

    const QRegularExpressionMatch lenMatch = lenRaw.match(normalizedText);
    if (lenMatch.hasMatch()) {
        frameLength = lenMatch.captured(1).toInt();
        result.rawData = LFTextNormalizer::normalizeHex(lenMatch.captured(2));
    }

    if (frameLength <= 0) {
        static const QRegularExpression lenOnly(
            QStringLiteral("Indala\\s*\\(len\\s*(\\d+)\\)"));
        const QRegularExpressionMatch lenOnlyMatch = lenOnly.match(normalizedText);
        if (lenOnlyMatch.hasMatch())
            frameLength = lenOnlyMatch.captured(1).toInt();
    }

    if (frameLength > 0)
        result.bitLength.set(frameLength);

    static const QRegularExpression oddSize(
        QStringLiteral("Odd size,  false positive"));
    if (oddSize.match(normalizedText).hasMatch()) {
        unknownLength = true;
        result.addWarning(QStringLiteral("Client reported an odd Indala length; "
                                         "the result may be a false positive."));
    }

    const QRegularExpressionMatch fmtMatch = fmt.match(normalizedText);
    if (fmtMatch.hasMatch()) {
        const QString wiegandFormat = fmtMatch.captured(1);
        result.facilityCode.set(fmtMatch.captured(2).toUInt());
        result.cardNumber.set(fmtMatch.captured(3).toULongLong());
        result.addExtra(QStringLiteral("wiegandFormat"), wiegandFormat);
        if (wiegandFormat != QStringLiteral("26"))
            result.addWarning(QStringLiteral("Indala decoded Wiegand format is %1, not 26.")
                              .arg(wiegandFormat));
    }

    const QRegularExpressionMatch parityMatch = parity.match(normalizedText);
    if (parityMatch.hasMatch())
        result.addExtra(QStringLiteral("parity"), parityMatch.captured(1));

    const QRegularExpressionMatch internalMatch = internalId.match(normalizedText);
    if (internalMatch.hasMatch()) {
        result.addExtra(QStringLiteral("internalId"), internalMatch.captured(1));
        if (!result.cardNumber.isPresent()) {
            bool ok = false;
            const qulonglong value = internalMatch.captured(1).toULongLong(&ok);
            if (ok) {
                result.decimalId.set(static_cast<quint64>(value));
                result.addWarning(QStringLiteral("Card number is absent; "
                                                 "the client's internal ID is reported as decimal ID."));
            }
        }
    }

    const QRegularExpressionMatch hedenMatch = heden2l.match(normalizedText);
    if (hedenMatch.hasMatch())
        result.addExtra(QStringLiteral("heden2l"), hedenMatch.captured(1));

    const QRegularExpressionMatch keyscanMatch = keyscan.match(normalizedText);
    if (keyscanMatch.hasMatch()) {
        result.addExtra(QStringLiteral("keyscan40010Fc"), keyscanMatch.captured(1));
        result.addExtra(QStringLiteral("keyscan40010Cn"), keyscanMatch.captured(2));
        result.addWarning(QStringLiteral("Keyscan 40010 values are an alternative "
                                         "interpretation and are not used as FC/CN."));
    }

    const QRegularExpressionMatch printedMatch = printed.match(normalizedText);
    if (printedMatch.hasMatch()) {
        result.addExtra(QStringLiteral("printed"), printedMatch.captured(1));
        result.addExtra(QStringLiteral("printedHex"), printedMatch.captured(2));
    }

    if (frameLength > 0) {
        if (unknownLength)
            result.format = QStringLiteral("Indala %1-bit (odd length)").arg(frameLength);
        else
            result.format = QStringLiteral("Indala %1-bit").arg(frameLength);
    } else if (fmtMatch.hasMatch()) {
        result.format = QStringLiteral("Indala (Fmt %1)").arg(fmtMatch.captured(1));
    } else {
        result.format = QStringLiteral("Indala");
    }

    if (!result.rawData.isEmpty() && result.facilityCode.isPresent()
            && result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = parityMatch.hasMatch() ? Lf::Confidence::High
                                                   : Lf::Confidence::Medium;
    } else if (!result.rawData.isEmpty() || (result.facilityCode.isPresent()
                                             && result.cardNumber.isPresent())) {
        result.status = Lf::Status::Identified;
        result.confidence = Lf::Confidence::Medium;
        if (result.rawData.isEmpty())
            result.addWarning(QStringLiteral("Indala raw payload is absent; "
                                             "parsed from decoded fields only."));
        else if (!result.cardNumber.isPresent())
            result.addWarning(QStringLiteral("Indala decoded FC/CN fields are absent; "
                                             "only the raw payload is available."));
    } else if (frameLength > 0 || !result.extraFields.isEmpty()) {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Indala marker found but no raw payload or card fields were extracted."));
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Indala marker found but no identity fields were extracted."));
    }

    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    return result;
}
