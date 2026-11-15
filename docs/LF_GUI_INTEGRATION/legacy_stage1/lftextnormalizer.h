#ifndef LFTEXTNORMALIZER_H
#define LFTEXTNORMALIZER_H

#include <QString>

/**
 * Text preparation for the LF parsers.
 *
 * The normalizer removes only transport noise (ANSI escapes, CR endings, NUL
 * bytes, UTF-8 BOM). Hex payloads and field spacing are preserved so that
 * parsers can rely on the client's own layout.
 */
class LFTextNormalizer
{
public:
    static QString normalize(const QString &text);
    static QString stripAnsi(const QString &text);

    /**
     * Uppercases a hexadecimal payload and removes separators
     * (spaces, colons, dashes and an optional 0x prefix).
     * Returns an empty string when the payload contains non hex characters.
     */
    static QString normalizeHex(const QString &text);

    /** True when the text contains a known LF marker. */
    static bool looksLikeLfOutput(const QString &normalizedText);
};

#endif // LFTEXTNORMALIZER_H
