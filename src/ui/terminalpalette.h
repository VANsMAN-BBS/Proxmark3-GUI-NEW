#ifndef TERMINALPALETTE_H
#define TERMINALPALETTE_H

#include <QColor>
#include <QString>

/**
 * Classic-green terminal colours for the RawCommand log.
 *
 * The palette keeps classic green as the default foreground: normal output
 * (also echoed commands, success, data, hardware, protocol and hints) plus a
 * separate progress colour for the client's in-place spinner frames
 * ([\], [|], [/], [-] with a progress message).  Warnings and errors are the
 * only warm colours.  Two variants keep readable contrast against the bundled
 * QDarkStyle backgrounds:
 *
 *   dark  (#19232D background): normal #39FF14, progress #00D68F,
 *                               warning #FFD54F, error #FF5252
 *   light (#FAFAFA background): normal #008000, progress #00796B,
 *                               warning #946000, error #C62828
 *
 * Widgets must not hard-code these colours; use forDark() and the semantic
 * accessors below.
 */
struct TerminalPalette
{
    QColor normal;        // ordinary output, prompts and default text
    QColor userCommand;   // echoed command / prompt line
    QColor success;       // [+]
    QColor info;          // [=] and [*]
    QColor data;          // [#]
    QColor progress;      // in-place spinner frames [\], [|], [/], [-] progress
    QColor hardware;      // firmware / model fields
    QColor protocol;      // UID / ATQA / SAK fields
    QColor hint;          // [?]
    QColor warning;       // [!]
    QColor error;         // [!!] and [-] failure
    QColor hintCommand;   // executable command inside a hint

    TerminalPalette();

    /** Palette for the given background family. */
    static TerminalPalette forDark(bool darkBackground);

    /** Detect the active UI theme (GUIsettings.ini, then the widget palette). */
    static bool detectDarkBackground();

    /** WCAG relative luminance of one colour (0..1). */
    static qreal relativeLuminance(const QColor &colour);

    /** WCAG contrast ratio between two opaque colours (1..21). */
    static qreal contrastRatio(const QColor &first, const QColor &second);
};

#endif // TERMINALPALETTE_H
