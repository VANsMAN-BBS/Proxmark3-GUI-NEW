#include "terminalhighlighter.h"

#include <QTextDocument>

TerminalHighlighter::TerminalHighlighter(QTextDocument *document, bool darkBackground)
    : QSyntaxHighlighter(document)
    , m_darkBackground(darkBackground)
{
    rebuildFormats();
}

void TerminalHighlighter::rebuildFormats()
{
    const TerminalPalette palette = TerminalPalette::forDark(m_darkBackground);

    m_normal.setForeground(palette.normal);
    m_userCommand.setForeground(palette.userCommand);
    m_success.setForeground(palette.success);
    m_info.setForeground(palette.info);
    m_data.setForeground(palette.data);
    m_progress.setForeground(palette.progress);
    m_progress.setFontItalic(true);
    m_hardware.setForeground(palette.hardware);
    m_protocol.setForeground(palette.protocol);
    m_hint.setForeground(palette.hint);
    m_warning.setForeground(palette.warning);
    m_error.setForeground(palette.error);

    m_hintCommand.setForeground(palette.hintCommand);
    m_hintCommand.setFontUnderline(true);
    m_hintCommand.setUnderlineStyle(QTextCharFormat::SingleUnderline);
}

void TerminalHighlighter::setDarkBackground(bool darkBackground)
{
    if (m_darkBackground == darkBackground) {
        return;
    }
    m_darkBackground = darkBackground;
    rebuildFormats();
    rehighlight();
}

QTextCharFormat TerminalHighlighter::formatFor(TerminalLineClass lineClass) const
{
    switch (lineClass) {
    case TerminalLineClass::UserCommand: return m_userCommand;
    case TerminalLineClass::Success: return m_success;
    case TerminalLineClass::Info: return m_info;
    case TerminalLineClass::Data: return m_data;
    case TerminalLineClass::Progress: return m_progress;
    case TerminalLineClass::Hardware: return m_hardware;
    case TerminalLineClass::Protocol: return m_protocol;
    case TerminalLineClass::Hint: return m_hint;
    case TerminalLineClass::Warning: return m_warning;
    case TerminalLineClass::Error: return m_error;
    case TerminalLineClass::Plain: break;
    }
    return m_normal;
}

void TerminalHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    // Classic terminal: every line starts in the normal green.  Semantic
    // classes and fields only override the parts that carry more meaning.
    setFormat(0, text.length(), m_normal);

    const TerminalLineClass lineClass = TerminalClassifier::classifyLine(text);
    if (lineClass != TerminalLineClass::Plain) {
        setFormat(0, text.length(), formatFor(lineClass));
    }

    // Semantic fields win over the line colour, e.g. UID on a [+] line.
    const QVector<TerminalSpan> fields = TerminalClassifier::semanticSpans(text);
    for (const TerminalSpan &field : fields) {
        setFormat(field.start, field.length, formatFor(field.lineClass));
    }

    // The executable span inside a hint stays in the same green colour but is
    // underlined so it remains visibly clickable.
    if (lineClass == TerminalLineClass::Hint) {
        const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(text);
        for (const TerminalHintSpan &hint : hints) {
            setFormat(hint.start, hint.length, m_hintCommand);
        }
    }
}
