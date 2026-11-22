#ifndef TERMINALHIGHLIGHTER_H
#define TERMINALHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include "terminalclassifier.h"
#include "terminalpalette.h"

class QTextDocument;

/**
 * Read-only log styling.  It only assigns character formats; the document
 * text and the plain-text clipboard output are never modified.
 */
class TerminalHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit TerminalHighlighter(QTextDocument *document,
                                 bool darkBackground = TerminalPalette::detectDarkBackground());

    QTextCharFormat formatFor(TerminalLineClass lineClass) const;

    /** Rebuild formats after a live light/dark theme change. */
    void setDarkBackground(bool darkBackground);
    bool isDarkBackground() const { return m_darkBackground; }

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();

    bool m_darkBackground;
    QTextCharFormat m_normal;
    QTextCharFormat m_userCommand;
    QTextCharFormat m_success;
    QTextCharFormat m_info;
    QTextCharFormat m_data;
    QTextCharFormat m_progress;
    QTextCharFormat m_hardware;
    QTextCharFormat m_protocol;
    QTextCharFormat m_hint;
    QTextCharFormat m_warning;
    QTextCharFormat m_error;
    QTextCharFormat m_hintCommand;
};

#endif // TERMINALHIGHLIGHTER_H
