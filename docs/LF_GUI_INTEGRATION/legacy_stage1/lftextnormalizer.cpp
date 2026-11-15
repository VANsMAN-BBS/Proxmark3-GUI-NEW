#include "lftextnormalizer.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

bool isHexChar(QChar ch)
{
    const ushort value = ch.unicode();
    return (value >= '0' && value <= '9')
            || (value >= 'a' && value <= 'f')
            || (value >= 'A' && value <= 'F');
}

} // namespace

QString LFTextNormalizer::stripAnsi(const QString &text)
{
    // CSI and OSC sequences. Kept identical in spirit to the client session
    // cleaner but owned by this module so the parser can be used standalone.
    static const QRegularExpression ansi(
        QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1B\\\\))"));

    QString result = text;
    result.remove(ansi);
    return result;
}

QString LFTextNormalizer::normalize(const QString &text)
{
    if (text.isEmpty())
        return QString();

    QString result = text;

    if (result.startsWith(QChar(0xFEFF)))
        result.remove(0, 1);

    result = stripAnsi(result);
    result.remove(QChar(0));

    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    // Right-trim every line. Left indentation is preserved because client
    // output aligns decoded fields with leading spaces.
    const QStringList lines = result.split(QLatin1Char('\n'));
    QStringList cleaned;
    cleaned.reserve(lines.size());
    for (const QString &line : lines) {
        int end = line.size();
        while (end > 0 && line.at(end - 1).isSpace())
            --end;
        cleaned.append(line.left(end));
    }

    return cleaned.join(QLatin1Char('\n'));
}

QString LFTextNormalizer::normalizeHex(const QString &text)
{
    QString value = text.trimmed();
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        value.remove(0, 2);

    QString result;
    result.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isSpace() || ch == QLatin1Char(':') || ch == QLatin1Char('-'))
            continue;
        if (!isHexChar(ch))
            return QString();
        result.append(ch.toUpper());
    }
    return result;
}

bool LFTextNormalizer::looksLikeLfOutput(const QString &normalizedText)
{
    static const QRegularExpression marker(
        QStringLiteral("(?i)(EM\\s*410x|EM410x|HID\\s*Prox|AWID|Indala|"
                       "125/134\\s*kHz|low\\s*frequency)"));

    return marker.match(normalizedText).hasMatch();
}
