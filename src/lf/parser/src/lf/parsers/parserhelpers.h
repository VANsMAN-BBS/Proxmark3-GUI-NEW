#ifndef LF_PARSERHELPERS_H
#define LF_PARSERHELPERS_H

#include <QRegularExpression>
#include <QString>

#include "lf/lfparsetypes.h"

/**
 * Small internal helpers shared by the protocol parsers.
 * Not part of the public installed headers.
 */
namespace LfParse {

inline bool toU32(const QString &text, quint32 *value)
{
    bool ok = false;
    const qulonglong parsed = text.toULongLong(&ok);
    if (!ok || parsed > 0xFFFFFFFFULL)
        return false;
    *value = static_cast<quint32>(parsed);
    return true;
}

inline bool toU64(const QString &text, quint64 *value)
{
    bool ok = false;
    const qulonglong parsed = text.toULongLong(&ok);
    if (!ok)
        return false;
    *value = static_cast<quint64>(parsed);
    return true;
}

inline bool toInt(const QString &text, int *value)
{
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (!ok)
        return false;
    *value = parsed;
    return true;
}

/**
 * Uppercases a hexadecimal payload and removes separators.
 * Returns an empty string when the length is outside [minLen, maxLen]
 * or the payload contains non hex characters.
 */
inline QString hex(const QString &text, int minLen = 4, int maxLen = 64)
{
    QString value = text.trimmed();
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        value.remove(0, 2);

    QString result;
    result.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isSpace() || ch == QLatin1Char(':') || ch == QLatin1Char('-'))
            continue;
        const ushort code = ch.unicode();
        const bool isHex = (code >= '0' && code <= '9')
                || (code >= 'a' && code <= 'f')
                || (code >= 'A' && code <= 'F');
        if (!isHex)
            return QString();
        result.append(ch.toUpper());
    }

    if (result.size() < minLen || result.size() > maxLen)
        return QString();
    return result;
}

inline QString capture(const QString &text, const QRegularExpression &expression, int group = 1)
{
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(group) : QString();
}

inline bool has(const QString &text, const QRegularExpression &expression)
{
    return expression.match(text).hasMatch();
}

} // namespace LfParse

#endif // LF_PARSERHELPERS_H
