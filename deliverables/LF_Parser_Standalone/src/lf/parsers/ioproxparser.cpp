#include "lf/parsers/ioproxparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol IOProxParser::protocol() const { return Lf::Protocol::IOProx; }
QString IOProxParser::name() const { return QStringLiteral("IO Prox"); }

int IOProxParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression xsf(
        QStringLiteral("(?i)IO\\s*Prox\\s*-\\s*XSF\\("));
    static const QRegularExpression ssf(
        QStringLiteral("(?i)IO\\s*Prox\\s*SSF\\s*-"));
    static const QRegularExpression withRaw(
        QStringLiteral("(?i)IO\\s*Prox\\b[^\\n]*\\bRaw:"));
    static const QRegularExpression keyword(QStringLiteral("(?i)\\bIO\\s*Prox\\b"));

    if (xsf.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("IO Prox XSF line"));
        return 100;
    }
    if (ssf.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("IO Prox SSF line"));
        return 95;
    }
    if (withRaw.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("IO Prox raw line"));
        return 80;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("IO Prox keyword"));
        return 50;
    }
    return 0;
}

LFParseResult IOProxParser::parse(const QString &normalizedText,
                                  const QString &originalText) const
{
    static const QRegularExpression xsf(
        QStringLiteral("IO\\s*Prox\\s*-\\s*XSF\\((\\d+)\\)([0-9A-Fa-f]{2,4}):(\\d+)"));
    static const QRegularExpression ssf(
        QStringLiteral("IO\\s*Prox\\s*SSF\\s*-\\s*([0-9A-Fa-f]{2})([0-9A-Fa-f]{2}):(\\d+)"));
    static const QRegularExpression rawOnly(
        QStringLiteral("IO\\s*Prox[^\\n]*?Raw:\\s*([0-9A-Fa-f]{8,24})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("IO Prox");
    result.sourceText = originalText;

    QRegularExpressionMatch match = xsf.match(normalizedText);
    if (match.hasMatch()) {
        const QString version = match.captured(1);
        const QString fcHex = match.captured(2).toUpper();
        const QString cn = match.captured(3);

        quint32 fc = 0;
        bool fcOk = false;
        const quint32 parsed = fcHex.toUInt(&fcOk, 16);
        if (fcOk)
            fc = parsed;

        if (fcOk)
            result.facilityCode.set(fc);
        quint64 cnValue = 0;
        if (LfParse::toU64(cn, &cnValue))
            result.cardNumber.set(cnValue);

        result.rawData = LfParse::hex(LfParse::capture(normalizedText, rawOnly), 8, 24);
        result.hexId = result.rawData;
        result.format = QStringLiteral("IO Prox XSF");
        result.addExtra(QStringLiteral("version"), version);
        result.addExtra(QStringLiteral("facilityCodeHex"), fcHex);
        if (fcOk)
            result.addExtra(QStringLiteral("facilityCodeDecimal"), QString::number(fc));
        else
            result.addWarning(QStringLiteral("IO Prox facility code is not a valid hex value."));
    } else {
        match = ssf.match(normalizedText);
        if (match.hasMatch()) {
            result.format = QStringLiteral("IO Prox SSF");
            result.addExtra(QStringLiteral("version"), match.captured(1).toUpper());
            const QString fcHex = match.captured(2).toUpper();
            quint32 fc = 0;
            bool fcOk = false;
            const quint32 parsed = fcHex.toUInt(&fcOk, 16);
            if (fcOk) {
                fc = parsed;
                result.facilityCode.set(fc);
                result.addExtra(QStringLiteral("facilityCodeHex"), fcHex);
            }
            quint64 cnValue = 0;
            if (LfParse::toU64(match.captured(3), &cnValue))
                result.cardNumber.set(cnValue);
        } else {
            match = rawOnly.match(normalizedText);
            if (match.hasMatch()) {
                result.rawData = LfParse::hex(match.captured(1), 8, 24);
                result.hexId = result.rawData;
                result.format = QStringLiteral("IO Prox");
            }
        }
    }

    const bool hasFields = result.facilityCode.isPresent()
            || result.cardNumber.isPresent()
            || !result.rawData.isEmpty();
    if (hasFields) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.facilityCode.isPresent() && result.cardNumber.isPresent())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("IO Prox marker found but no card fields were extracted."));
    }
    return result;
}
