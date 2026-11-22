#ifndef TERMINALCLASSIFIER_H
#define TERMINALCLASSIFIER_H

#include <QString>
#include <QVector>

/**
 * Pure text classification for the RawCommand log.
 *
 * The classifier never changes the text.  It is intentionally independent of
 * widgets so the rules can be covered by QtTest without a GUI.
 */
enum class TerminalLineClass
{
    Plain = 0,
    UserCommand,
    Success,
    Info,
    Data,
    Progress,
    Hardware,
    Protocol,
    Hint,
    Warning,
    Error
};

struct TerminalSpan
{
    int start = 0;
    int length = 0;
    TerminalLineClass lineClass = TerminalLineClass::Plain;
};

struct TerminalHintSpan
{
    int start = 0;
    int length = 0;
    QString command;
};

class TerminalClassifier
{
public:
    /** Remove CSI/OSC escape sequences for analysis only. */
    static QString stripAnsi(const QString &text);

    /** Classify one visual line. Marker precedence: [!!] before [!]. */
    static TerminalLineClass classifyLine(const QString &text);

    /**
     * True when the text after a `[-]` marker is an in-place progress message.
     *
     * The Iceman client prints its spinner as `[\], [|], [/], [-]`, so `[-]`
     * is ambiguous: it is also the FAILED level.  Progress messages are
     * recognised by exact, anchored client message prefixes (see ui.c
     * PrintAndLogEx INPLACE call sites); a FAILED line such as
     * "`[-] failed to select card, exiting...`" is therefore not progress.
     */
    static bool isInPlaceProgress(const QString &textAfterMarker);

    /** Locate UID/ATQA/SAK (protocol) and firmware/model (hardware) fields. */
    static QVector<TerminalSpan> semanticSpans(const QString &text);

    /**
     * Locate executable commands inside `[?] Hint: Try ...`.
     *
     * Commands may be wrapped in ASCII `, ', " or in common Unicode quotation
     * marks (“ ”, ‘ ’, 「 」, 『 』, „ “, « », ‹ ›).  Every span is validated
     * against the shipped 1024-command catalog.  A span with extra arguments
     * is valid when its leading tokens exactly match a catalog command, so
     * `hf mf rdbl --blk 0 -k FFFFFFFFFFFF` remains clickable.
     *
     * The input may contain several lines (split buffers); returned offsets
     * refer to the complete string.
     */
    static QVector<TerminalHintSpan> hintSpans(const QString &text);

    /**
     * Hit-test one character offset (as returned by QTextCursor) inside a hint
     * line.  Returns the command when the offset touches a clickable span,
     * including either boundary character.
     */
    static QString hintAt(const QString &text, int offset);

    /**
     * True when the leading tokens match an official client command.
     *
     * The catalog is loaded lazily from :/commands/commands.json.  If that
     * resource is unavailable (for example in a minimal unit-test build), a
     * conservative list of official top-level names is used instead.
     */
    static bool isKnownCommandStart(const QString &command);

    /** Stable name used by tests and the palette documentation. */
    static QString className(TerminalLineClass lineClass);
};

#endif // TERMINALCLASSIFIER_H
